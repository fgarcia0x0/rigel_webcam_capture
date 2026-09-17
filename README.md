# Rigel Webcam Capture

![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![Linux](https://img.shields.io/badge/Linux-V4L2-informational)
![Windows](https://img.shields.io/badge/Windows-Media%20Foundation-informational)
![License](https://img.shields.io/badge/license-Apache%202.0-green.svg)

A cross-platform webcam capture library and desktop viewer, written from scratch in C++23 directly against the operating system's native capture APIs — **V4L2** on Linux and **Media Foundation + Direct3D11** on Windows. No OpenCV, no GStreamer, no `libv4l` convenience wrapper: the capture path (device enumeration, format negotiation, buffer/mmap management, streaming, property controls) is hand-rolled `ioctl`/COM-interop code, not a thin call into a framework that already does it for you.

```
rwc/          core library: platform abstraction, capture backends, decoders
rwc_app/      SDL3 + Dear ImGui desktop viewer built on top of rwc
tests/        GoogleTest suite that exercises property controls against real hardware
tools/        capture_bench, a standalone CLI harness used for profiling
```

## Why this exists

Most "webcam capture" side projects reach for OpenCV or GStreamer and call it a day. This one goes a layer lower: it talks to `/dev/videoN` with raw `ioctl`/`mmap`/`select` calls on Linux, and to the Media Foundation COM API with a Direct3D11 staging-texture pool on Windows, because the goal was to actually understand and control the full pipeline — device capability negotiation, kernel buffer lifecycle, decode, color space handling, and GPU upload — rather than depend on a library that hides all of it.

The two codecs that need real decoding (MJPEG and H.264) are handled by industry-standard libraries (`libjpeg-turbo`, `OpenH264`) rather than reinvented — writing a JPEG or H.264 decoder from scratch is its own multi-year project, not something a capture library should own. Everything *around* those libraries — the capture thread, the buffering strategy, the colorspace-correct GPU upload, the lock-free handoff between threads — is original code.

## Highlights

- **Raw capture, both platforms.** Linux: `VIDIOC_REQBUFS`/`QUERYBUF`/`QBUF`/`DQBUF` with `mmap`'d kernel buffers and a `select()`-driven capture thread. Windows: `IMFSourceReader` fed through a `ID3D11Device` staging-texture pool for zero-copy-ish GPU handoff.
- **Lock-free SPSC ring buffer** (`rwc::swsr_ring_buffer`) hands decoded frames from the capture thread to the consumer without a mutex, using atomic acquire/release ordering and a CAS loop on the overwrite-oldest path.
- **A real frame buffer pool**, not per-frame `malloc`/`free`. Output buffers are recycled through a `unique_ptr` with a type-erased deleter, so the public API still looks like a normal owning pointer while the backend quietly reuses memory.
- **NV12 decode pipeline.** MJPEG decodes straight to NV12 in one fused pass (no redundant chroma upsample/downsample round-trip); H.264 reformats OpenH264's native I420 output into NV12 losslessly (no color math at all); YUYV goes through `libyuv` SIMD instead of a scalar per-pixel loop. Output buffers are half the size of RGB24, with the source's actual colorspace (JPEG full-range vs. BT.601 studio-range) tracked and applied correctly when the GPU texture is created — not just visually similar.
- **Cross-platform abstraction that stays clean**: one `webcam_device` interface, one `webcam_manager` factory, platform-specific code entirely behind it. The SDL3/ImGui app never knows whether it's talking to V4L2 or Media Foundation.
- **Measured, not assumed.** The NV12 migration was validated with `valgrind --tool=massif` before/after profiling on real capture sessions (see [Performance](#performance)), not just "should be faster."

## The app

`rwc_app` is an SDL3 + Dear ImGui desktop viewer built on top of the library: live preview, device/resolution/format/FPS selection, per-camera control panel (exposure, focus, zoom, white balance, gain, brightness, contrast, saturation, gamma, sharpness, back-light compensation, power-line frequency — with auto/manual toggles where the device supports them), frame capture to BMP, horizontal flip, always-on-top, VSync modes, and live renderer switching between Vulkan, OpenGL, Direct3D11/12, and Metal (whichever SDL3 backends are available on the platform).

## Architecture

```
rwc_app  (SDL3 + Dear ImGui — device/format/property controls, live preview)
   │
   │  webcam_device / webcam_manager   (public API)
   ▼
┌───────────────────────────┐
│      rwc (core library)    │
│  webcam_device interface   │
└──────────────┬─────────────┘
               │
     ┌─────────┴──────────┐
     ▼                     ▼
v4l2_webcam_device    mmf_webcam_device
 (Linux, V4L2)         (Windows, Media Foundation + D3D11)
     │
     ▼
capture thread  ──►  swsr_ring_buffer  ──►  read_frame()   (poll API, lock-free handoff)
     │
     ▼
webcam_image_decoder
  ├─ MJPEG → NV12   (libjpeg-turbo, fused decode + chroma downsample, single pass)
  ├─ H.264 → NV12   (OpenH264, lossless I420 reformat, no color math)
  └─ YUYV  → RGB24  (libyuv SIMD, I422 intermediate, no chroma loss)
     │
     ▼
frame_buffer_pool   (recycled output buffers, type-erased deleter)
```

Frames cross the capture-thread → consumer-thread boundary through the lock-free ring buffer; nothing about buffering, decoding, or pooling is visible above the `webcam_device` interface.

## Performance

Buffer-format and pooling work was validated end-to-end with `valgrind --tool=massif`, comparing a 10-second capture session before and after, per codec, on a 2560x1440 UVC webcam:

| Codec | Peak heap (before) | Peak heap (after) | Change |
|---|---|---|---|
| YUYV (640x480) | 5.24 MiB | 0.96 MiB | **-82%** |
| MJPEG (2560x1440) | 67.54 MiB | 21.28 MiB | **-68%** |
| H.264 (2560x1440) | 54.67 MiB | 54.33 MiB | ~flat* |

\* H.264's peak is dominated by OpenH264's own internal reference-picture buffers, not by anything this project controls.

Switching MJPEG/H.264 output from RGB24 to NV12 also halves both the CPU-side buffer size and the GPU texture size (1.5 vs. 3 bytes/pixel), with no measured quality loss for H.264 (bit-identical to the RGB24 path — NV12 is a lossless reformat of OpenH264's native output) and an imperceptible mean error (~4.7/255) for MJPEG, verified by decoding real captured frames through both paths and comparing.

## Building

Dependencies (`libjpeg-turbo`, `libyuv`, `OpenH264`) are pulled through a [vcpkg](https://github.com/microsoft/vcpkg) manifest; SDL3, Dear ImGui, and GoogleTest are vendored as git submodules.

```bash
git clone --recursive https://github.com/fgarcia0x0/rigel_webcam_capture.git
cd rigel_webcam_capture
./run.sh              # Debug build, then runs rwc_app
./run.sh --release     # Release build
./run.sh --build-only  # build without launching the app
```

`run.sh` auto-detects `VCPKG_ROOT` (checking `~/vcpkg` and `~/tools/vcpkg`) or reads it from the environment. Manually, it's a standard CMake + vcpkg toolchain build:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build -j
```

CMake options:

| Option | Default | Description |
|---|---|---|
| `RWC_BUILD_APP` | `ON` | Build the SDL3/ImGui viewer |
| `RWC_ENABLE_TESTING` | `ON` | Build the GoogleTest suite |
| `RWC_BUILD_BENCH_TOOLS` | `ON` | Build `capture_bench`, the profiling CLI |
| `BUILD_SHARED_LIBS` | `OFF` | Build `rwc` as a shared library |

## Testing

`tests/` is a GoogleTest suite that opens the first real webcam device and exercises property read/write/reset against it — there's no mock device layer, it validates against actual hardware behavior. `tools/capture_bench` is a headless CLI that streams a chosen codec for a fixed duration and reports frame/drop counters, used for the `massif` profiling above without the UI/rendering stack in the way.

## License

Apache License 2.0 — see [LICENSE](LICENSE).

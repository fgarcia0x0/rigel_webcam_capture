# rigel_webcam_capture (rwc)

**rigel_webcam_capture** is a lightweight, multi-threaded C++ library designed to capture and decode webcam frames. It provides an easy-to-use API for accessing webcam streams, decoding popular formats, and converting frames to `rgb24`.

## Features

 **Simple API** for webcam frame capture
- Made in C++ 23
- Have vcpkg manisfest file for dependencies
- Supports most webcam formats:
  - MJPEG
  - H.264
  - YUYV (YUYV422)
-  **Multi-threaded** design for efficient processing
-  Webcam **capability querying** (resolution, formats, etcc)
-  Webcam **controls configuration** (brightness, focus, zoom, contrast e etcc)
- **Linux-only support for now** (uses V4L2)

## Dependencies

- [FFmpeg / libavcodec](https://ffmpeg.org/) (for decoding)
- [libswscale](https://ffmpeg.org/libswscale.html) (for pixel format conversion)
- [V4L2](https://www.kernel.org/doc/html/v4.9/media/uapi/v4l/v4l2.html) (Video4Linux2 API)

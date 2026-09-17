// Dev-only benchmarking tool: opens a webcam, streams a single codec for a fixed
// duration and reports frame/drop counters. Used to compare heap allocation
// behavior (under valgrind --tool=massif) before/after changes to the capture
// pipeline, isolated from the UI/SDL/imgui stack.
#include <rwc/core/webcam_device.hpp>
#include <rwc/core/webcam_manager.h>
#include <rwc/core/webcam_utils.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>
#include <thread>

namespace
{
    std::optional<uint32_t> codec_from_name(std::string_view name)
    {
        if (name == "yuyv") return rwc::RWC_WEBCAM_CODEC_TYPE_YUYV;
        if (name == "mjpeg") return rwc::RWC_WEBCAM_CODEC_TYPE_MJPEG;
        if (name == "h264") return rwc::RWC_WEBCAM_CODEC_TYPE_H264;
        return std::nullopt;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <yuyv|mjpeg|h264> [duration_seconds=10] [device_index=0]\n", argv[0]);
        return 1;
    }

    auto codec = codec_from_name(argv[1]);
    if (!codec)
    {
        std::fprintf(stderr, "error: unknown codec '%s' (expected yuyv, mjpeg or h264)\n", argv[1]);
        return 1;
    }

    auto duration = std::chrono::seconds{ argc > 2 ? std::atoi(argv[2]) : 10 };
    uint32_t device_index = argc > 3 ? static_cast<uint32_t>(std::atoi(argv[3])) : 0;

    auto device = rwc::webcam_manager::create_device();
    if (!device)
    {
        std::fprintf(stderr, "error: could not create webcam device\n");
        return 1;
    }

    if (auto status = device->open(device_index); status != rwc::webcam_error_status::ok)
    {
        std::fprintf(stderr, "error: could not open device %u (status=%d)\n", device_index, static_cast<int>(status));
        return 1;
    }

    auto format = rwc::webcam_utils::select_capture_format(device->device_info().formats, codec);
    if (!format)
    {
        std::fprintf(stderr, "error: device does not support codec '%s'\n", argv[1]);
        return 1;
    }

    std::printf("format: %s\n", format->to_string().c_str());

    if (!device->set_current_format(*format))
    {
        std::fprintf(stderr, "error: could not set capture format\n");
        return 1;
    }

    if (auto status = device->start_stream(); status != rwc::webcam_error_status::ok)
    {
        std::fprintf(stderr, "error: could not start streaming (status=%d)\n", static_cast<int>(status));
        return 1;
    }

    uint64_t frames_read{};
    uint64_t poll_misses{};
    uint64_t bytes_total{};

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration)
    {
        auto frame = device->read_frame();
        if (frame.has_value())
        {
            ++frames_read;
            bytes_total += frame->size;
        }
        else if (frame.error() == rwc::webcam_error_status::frame_not_ready)
        {
            ++poll_misses;
            std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
        }
        else
        {
            std::fprintf(stderr, "error: read_frame failed (status=%d)\n", static_cast<int>(frame.error()));
            break;
        }
    }

    device->stop_stream();
    device->close();

    std::printf("codec=%s frames_read=%llu poll_misses=%llu bytes_total=%llu\n",
                argv[1],
                static_cast<unsigned long long>(frames_read),
                static_cast<unsigned long long>(poll_misses),
                static_cast<unsigned long long>(bytes_total));

    return 0;
}

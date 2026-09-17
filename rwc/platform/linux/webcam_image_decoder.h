#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rwc
{
    struct webcam_image_decoder
    {
    public:
        webcam_image_decoder();

        webcam_image_decoder(webcam_image_decoder&&);
        webcam_image_decoder& operator=(webcam_image_decoder&&);

        // dest's required size and layout depend on the codec (see
        // webcam_utils::pixel_format_for_codec): width*height*3 interleaved
        // RGB24 for YUYV/YUY2, or width*height*3/2 for MJPEG/JPEG/H264 (a Y
        // plane immediately followed by an interleaved U,V plane, both with
        // stride == width).
        bool decode(std::span<const uint8_t> src, std::span<uint8_t> dest,
                    uint32_t width, uint32_t height, uint32_t codec_type);

        ~webcam_image_decoder();

    private:
        // The OpenH264 decoder context (~3MB+) is only created the first time an
        // H264 frame is actually decoded, so YUYV/MJPEG-only sessions never pay
        // for it.
        struct context;
        std::unique_ptr<context> m_context;

        // Reused across frames for the YUYV->RGB24 conversion's I422
        // intermediate (see yuyv_to_rgb24 in the .cpp), instead of allocating
        // it fresh every frame. Resized only when the frame dimensions change.
        std::vector<uint8_t> m_yuyv_scratch;
    };
}

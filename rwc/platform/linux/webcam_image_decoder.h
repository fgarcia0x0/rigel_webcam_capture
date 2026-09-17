#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace rwc
{
    struct webcam_image_decoder
    {
    public:
        webcam_image_decoder();

        webcam_image_decoder(webcam_image_decoder&&);
        webcam_image_decoder& operator=(webcam_image_decoder&&);

        bool decode_to_rgb24(std::span<const uint8_t> src, std::span<uint8_t> dest, uint32_t codec_type);

        ~webcam_image_decoder();

    private:
        // The OpenH264 decoder context (~3MB+) is only created the first time an
        // H264 frame is actually decoded, so YUYV/MJPEG-only sessions never pay
        // for it.
        struct context;
        std::unique_ptr<context> m_context;
    };
}

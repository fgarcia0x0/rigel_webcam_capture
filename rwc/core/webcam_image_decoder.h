#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <memory>

namespace rwc
{
    struct webcam_image_decoder
    {
    public:
        webcam_image_decoder();

        webcam_image_decoder(webcam_image_decoder&&);
        webcam_image_decoder& operator=(webcam_image_decoder&&);

        bool yuyv_to_rgb24(const uint8_t* src, uint8_t* dest, size_t src_pixel_count);
        bool jpeg_to_rgb24(const uint8_t* src, uint8_t* dest, size_t src_pixel_count);

        ~webcam_image_decoder();

    private:
        struct context;
        std::unique_ptr<context> m_context;
    };
}

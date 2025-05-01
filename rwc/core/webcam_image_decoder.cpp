#include <rwc/core/webcam_image_decoder.h>
#include <rwc/platform/platform.hpp>
#include <rwc/logger/logger.h>

#include <algorithm>
#include <turbojpeg.h>

namespace rwc
{
    struct webcam_image_decoder::context
    {
        tjhandle jpeg_handle = nullptr;
    };

    webcam_image_decoder::webcam_image_decoder()
        : m_context(std::make_unique<context>())
    {
    }

    webcam_image_decoder::~webcam_image_decoder()
    {
        if (m_context->jpeg_handle)
            tjDestroy(m_context->jpeg_handle);
    }

    webcam_image_decoder::webcam_image_decoder(webcam_image_decoder&&) = default;
    webcam_image_decoder& webcam_image_decoder::operator=(webcam_image_decoder&&) = default;

    bool webcam_image_decoder::yuyv_to_rgb24(const uint8_t* src, uint8_t* dest, size_t src_pixel_count)
    {
        if (!src || !dest || !src_pixel_count)
            return false;
        
        size_t pixel_count = src_pixel_count;

        for (; pixel_count > 3; pixel_count -= 4)
        {
            const int16_t y0 = *src++;
            const int16_t cr = *src++;
            const int16_t y1 = *src++;
            const int16_t cb = *src++;

            const int16_t yy0 = 19 * (y0 - 16);
            const int16_t yy1 = 19 * (y1 - 16);

            *dest++ = uint8_t(std::clamp<int32_t>((yy0 + 32*(cb - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(std::clamp<int32_t>((yy0 - 13*(cr - 128) - 6*(cb - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(std::clamp<int32_t>((yy0 + 26*(cr - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(std::clamp<int32_t>((yy1 + 32*(cb - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(std::clamp<int32_t>((yy1 - 13*(cr - 128) - 6*(cb - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(std::clamp<int32_t>((yy1 + 26*(cr - 128)) >> 4, 0, 255));
        }

        return true;
    }
    
    bool webcam_image_decoder::jpeg_to_rgb24(const uint8_t* src, uint8_t* dest, size_t src_pixel_count)
    {
        if (!src || !dest || !src_pixel_count)
            return false;

        if (!m_context->jpeg_handle)
        {
            m_context->jpeg_handle = tjInitDecompress();
            if (!m_context->jpeg_handle)
            {
                RWC_LOG_ERROR("Failed to initialize libjpeg-turbo ({})", tjGetErrorStr());
                return false;
            }
        }

        int width{};
        int height{};
        int subsamp{};
        int colorspace{};
        int status{};

        status = tjDecompressHeader3(m_context->jpeg_handle, src, src_pixel_count, &width, &height, &subsamp, &colorspace);
        if (status != 0)
        {
            RWC_LOG_ERROR("Failed to decode jpeg image header ({})", tjGetErrorStr());
            return false;
        }

        status = tjDecompress2(m_context->jpeg_handle, src, src_pixel_count, dest, width, 0, height, TJPF_RGB, TJFLAG_FASTDCT);
        if (status != 0)
        {
            RWC_LOG_ERROR("Failed to decompress jpeg image to raw bytes [w: {}, h: {}, size: {}] ({})", width, height, src_pixel_count, tjGetErrorStr());
            return false;
        }
        
        return true;
    }
}

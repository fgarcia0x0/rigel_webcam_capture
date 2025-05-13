#include <rwc/platform/linux/webcam_image_decoder.h>
#include <rwc/core/webcam_device.hpp>
#include <rwc/platform/platform.hpp>
#include <rwc/logger/logger.h>
#include <rwc/utils/scope_exit.hpp>

#include <memory>
#include <cstring>
#include <new>
#include <cstddef>

#include <libyuv/convert_argb.h>
#include <libyuv/mjpeg_decoder.h>

namespace rwc
{
    struct webcam_image_decoder::context
    {
        libyuv::MJpegDecoder mjpeg_decoder;
    };

    static bool mjpeg_to_rgb24(std::span<const uint8_t> src, std::span<uint8_t> dest, libyuv::MJpegDecoder& mjpeg_decoder)
    {
        if (mjpeg_decoder.LoadFrame(src.data(), src.size()) != LIBYUV_TRUE)
            return false;

        int width = mjpeg_decoder.GetWidth();
        int height = mjpeg_decoder.GetHeight();
        int channels = mjpeg_decoder.GetNumComponents();
        int color_space = mjpeg_decoder.GetColorSpace();

        if (color_space != libyuv::MJpegDecoder::kColorSpaceYCbCr) 
            return false;

        // Allocate buffer for YUV
        int y_size = width * height;
        int uv_width = (width + 1) / 2;
        int uv_height = (height + 1) / 2;
        int uv_size = uv_width * uv_height;
        size_t num_bytes = static_cast<size_t>(y_size + 2 * uv_size);

        std::unique_ptr<uint8_t[]> yuv_data{ new (std::nothrow) uint8_t[num_bytes] };
        if (!yuv_data)
            return false;

        uint8_t* y_plane = yuv_data.get();
        uint8_t* u_plane = y_plane + y_size;
        uint8_t* v_plane = u_plane + uv_size;
        uint8_t* planes[] = { y_plane, u_plane, v_plane };
        
        if (!mjpeg_decoder.DecodeToBuffers(planes, width, height))
            return false;

        mjpeg_decoder.UnloadFrame();

        int status = libyuv::I420ToRAW(y_plane, width, 
                                       u_plane, uv_width,
                                       v_plane, uv_width,
                                       dest.data(), width * channels,
                                       width, height);
                                
                                       
        if (status != 0)
            return false;

        return true;
    }

    webcam_image_decoder::webcam_image_decoder()
        : m_context(std::make_unique<context>())
    {
    }

    webcam_image_decoder::~webcam_image_decoder()
    {
    }

    webcam_image_decoder::webcam_image_decoder(webcam_image_decoder&&) = default;
    webcam_image_decoder& webcam_image_decoder::operator=(webcam_image_decoder&&) = default;

    static inline uint8_t clamp(int32_t value, int32_t min, int32_t max) noexcept
    {
        return (value < min) ? min : ((max < value) ? max : value);
    }

    static inline bool yuyv_to_rgb24(const uint8_t* src, uint8_t* dest, size_t src_pixel_count)
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

            *dest++ = uint8_t(clamp((yy0 + 32*(cb - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(clamp((yy0 - 13*(cr - 128) - 6*(cb - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(clamp((yy0 + 26*(cr - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(clamp((yy1 + 32*(cb - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(clamp((yy1 - 13*(cr - 128) - 6*(cb - 128)) >> 4, 0, 255));
            *dest++ = uint8_t(clamp((yy1 + 26*(cr - 128)) >> 4, 0, 255));
        }

        return true;
    }

    bool webcam_image_decoder::decode_to_rgb24(std::span<const uint8_t> src, 
                                               std::span<uint8_t> dest, 
                                               uint32_t codec_type)
    {
        if (codec_type == RWC_WEBCAM_CODEC_TYPE_YUYV || codec_type == RWC_WEBCAM_CODEC_TYPE_YUY2)
            return yuyv_to_rgb24(src.data(), dest.data(), src.size());
        else if (codec_type == RWC_WEBCAM_CODEC_TYPE_MJPEG)
            return mjpeg_to_rgb24(src, dest, m_context->mjpeg_decoder);
        else
            return false;
    }
}

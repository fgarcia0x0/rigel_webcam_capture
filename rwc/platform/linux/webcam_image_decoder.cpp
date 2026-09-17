#include <rwc/platform/linux/webcam_image_decoder.h>
#include <rwc/core/webcam_device.hpp>
#include <rwc/platform/platform.hpp>
#include <rwc/logger/logger.h>

#include <memory>
#include <csetjmp>

#include <libyuv/convert_argb.h>
#include <wels/codec_api.h>
#include <jpeglib.h>

namespace rwc
{
    struct webcam_image_decoder::context
    {
        ISVCDecoder* h264_decoder{ nullptr };

        context()
        {
            if (WelsCreateDecoder(&h264_decoder) != 0)
            {
                h264_decoder = nullptr;
                return;
            }

            SDecodingParam decoding_param{};
            if (h264_decoder->Initialize(&decoding_param) != 0)
            {
                WelsDestroyDecoder(h264_decoder);
                h264_decoder = nullptr;
            }
        }

        context(const context&) = delete;
        context& operator=(const context&) = delete;

        ~context()
        {
            if (h264_decoder)
            {
                h264_decoder->Uninitialize();
                WelsDestroyDecoder(h264_decoder);
            }
        }
    };

    struct jpeg_error_context
    {
        jpeg_error_mgr pub;
        std::jmp_buf setjmp_buffer;
    };

    static void jpeg_error_exit(j_common_ptr cinfo)
    {
        auto* error_ctx = reinterpret_cast<jpeg_error_context*>(cinfo->err);
        std::longjmp(error_ctx->setjmp_buffer, 1);
    }

    // Decodes straight to interleaved RGB using libjpeg-turbo directly (JDCT_ISLOW,
    // fancy chroma upsampling and block smoothing enabled), instead of libyuv's
    // MJpegDecoder wrapper, which hardcodes JDCT_IFAST/no fancy upsampling for speed
    // and produces visibly softer/blockier output.
    static bool mjpeg_to_rgb24(std::span<const uint8_t> src, std::span<uint8_t> dest)
    {
        jpeg_decompress_struct cinfo{};
        jpeg_error_context error_ctx{};

        cinfo.err = jpeg_std_error(&error_ctx.pub);
        error_ctx.pub.error_exit = jpeg_error_exit;

        if (setjmp(error_ctx.setjmp_buffer))
        {
            jpeg_destroy_decompress(&cinfo);
            return false;
        }

        jpeg_create_decompress(&cinfo);

        jpeg_mem_src(&cinfo, src.data(), static_cast<unsigned long>(src.size()));
        jpeg_read_header(&cinfo, TRUE);

        cinfo.dct_method = JDCT_ISLOW;
        cinfo.do_fancy_upsampling = TRUE;
        cinfo.do_block_smoothing = TRUE;
        cinfo.out_color_space = JCS_EXT_RGB;

        jpeg_start_decompress(&cinfo);

        const uint32_t row_stride = cinfo.output_width * static_cast<uint32_t>(cinfo.output_components);
        const size_t required_size = static_cast<size_t>(row_stride) * cinfo.output_height;

        if (required_size > dest.size())
        {
            jpeg_destroy_decompress(&cinfo);
            return false;
        }

        while (cinfo.output_scanline < cinfo.output_height)
        {
            uint8_t* row_ptr = dest.data() + static_cast<size_t>(cinfo.output_scanline) * row_stride;
            jpeg_read_scanlines(&cinfo, &row_ptr, 1);
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);

        return true;
    }

    // The camera only sends SPS/PPS once, at the very start of the stream, so the
    // decoder must be kept alive and fed sequentially for the whole streaming
    // session (see webcam_image_decoder::context). There is no V4L2 control to
    // request a fresh keyframe mid-stream for this class of UVC device; recovering
    // from a lost decode sync requires restarting the whole V4L2 stream.
    static bool h264_to_rgb24(std::span<const uint8_t> src, std::span<uint8_t> dest, ISVCDecoder* decoder)
    {
        if (!decoder)
            return false;

        uint8_t* planes[3] = {};
        SBufferInfo buffer_info{};

        // Note: the decoded picture is read from buffer_info.pDst, not from planes, as
        // openh264's own decoder tests and reference decoder do.
        DECODING_STATE state = decoder->DecodeFrameNoDelay(src.data(), static_cast<int>(src.size()), planes, &buffer_info);
        // dsRefLost/dsNoParamSets mean the stream lost sync, usually because the driver
        // dropped a frame. The camera only sends new parameter sets with the next
        // keyframe, so the picture stays broken until then.
        if (state != dsErrorFree)
        {
            RWC_LOG_WARN("H264 stream out of sync [state=0x{:x}]", static_cast<int>(state));
            return false;
        }

        // The decoder needs more data before it can output a picture. Expected on the
        // first buffer of a stream, so it is not an error.
        if (buffer_info.iBufferStatus != 1)
            return false;

        const int width = buffer_info.UsrData.sSystemBuffer.iWidth;
        const int height = buffer_info.UsrData.sSystemBuffer.iHeight;
        const int y_stride = buffer_info.UsrData.sSystemBuffer.iStride[0];
        const int uv_stride = buffer_info.UsrData.sSystemBuffer.iStride[1];

        const size_t required_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
        if (required_size > dest.size())
        {
            RWC_LOG_ERROR("H264 decode buffer too small [required={} dest.size={} w={} h={}]",
                          required_size, dest.size(), width, height);
            return false;
        }

        int status = libyuv::I420ToRAW(buffer_info.pDst[0], y_stride,
                                       buffer_info.pDst[1], uv_stride,
                                       buffer_info.pDst[2], uv_stride,
                                       dest.data(), width * 3,
                                       width, height);

        if (status != 0)
        {
            RWC_LOG_ERROR("H264 I420ToRAW failed [status={} w={} h={} y_stride={} uv_stride={}]",
                          status, width, height, y_stride, uv_stride);
        }

        return status == 0;
    }

    webcam_image_decoder::webcam_image_decoder() = default;

    webcam_image_decoder::~webcam_image_decoder()
    {
    }

    webcam_image_decoder::webcam_image_decoder(webcam_image_decoder&&) = default;
    webcam_image_decoder& webcam_image_decoder::operator=(webcam_image_decoder&&) = default;

    static inline uint8_t clamp(int32_t value, int32_t min, int32_t max) noexcept
    {
        return (value < min) ? min : ((max < value) ? max : value);
    }

    // src is packed YUYV (2 bytes/pixel); dest is interleaved RGB24 (3 bytes/pixel).
    static inline bool yuyv_to_rgb24(std::span<const uint8_t> src, std::span<uint8_t> dest)
    {
        // YUYV encodes 2 pixels per 4-byte macropixel, so src must hold a whole
        // number of macropixels and dest must be large enough for the RGB24
        // output those macropixels expand to.
        const size_t macropixel_count = src.size() / 4;
        const size_t required_dest_size = macropixel_count * 2 * 3;

        if (src.empty() || src.size() % 4 != 0 || dest.size() < required_dest_size)
            return false;

        const uint8_t* src_ptr = src.data();
        uint8_t* dest_ptr = dest.data();

        for (size_t remaining_bytes = src.size(); remaining_bytes > 3; remaining_bytes -= 4)
        {
            const int16_t y0 = *src_ptr++;
            const int16_t cr = *src_ptr++;
            const int16_t y1 = *src_ptr++;
            const int16_t cb = *src_ptr++;

            const int16_t yy0 = 19 * (y0 - 16);
            const int16_t yy1 = 19 * (y1 - 16);

            *dest_ptr++ = uint8_t(clamp((yy0 + 32*(cb - 128)) >> 4, 0, 255));
            *dest_ptr++ = uint8_t(clamp((yy0 - 13*(cr - 128) - 6*(cb - 128)) >> 4, 0, 255));
            *dest_ptr++ = uint8_t(clamp((yy0 + 26*(cr - 128)) >> 4, 0, 255));
            *dest_ptr++ = uint8_t(clamp((yy1 + 32*(cb - 128)) >> 4, 0, 255));
            *dest_ptr++ = uint8_t(clamp((yy1 - 13*(cr - 128) - 6*(cb - 128)) >> 4, 0, 255));
            *dest_ptr++ = uint8_t(clamp((yy1 + 26*(cr - 128)) >> 4, 0, 255));
        }

        return true;
    }

    bool webcam_image_decoder::decode_to_rgb24(std::span<const uint8_t> src,
                                               std::span<uint8_t> dest,
                                               uint32_t codec_type)
    {
        if (codec_type == RWC_WEBCAM_CODEC_TYPE_YUYV || codec_type == RWC_WEBCAM_CODEC_TYPE_YUY2)
            return yuyv_to_rgb24(src, dest);
        else if (codec_type == RWC_WEBCAM_CODEC_TYPE_MJPEG || codec_type == RWC_WEBCAM_CODEC_TYPE_JPEG)
            return mjpeg_to_rgb24(src, dest);
        else if (codec_type == RWC_WEBCAM_CODEC_TYPE_H264)
        {
            if (!m_context)
                m_context = std::make_unique<context>();
            return h264_to_rgb24(src, dest, m_context->h264_decoder);
        }
        else
            return false;
    }
}

#pragma once

#include <filesystem>
#include <rwc/core/webcam_device.hpp>
#include <rwc/platform/platform.hpp>

#include <span>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <functional>

namespace rwc
{
    struct webcam_utils
    {
        static RWC_API std::string prop_type_to_string(webcam_property_type type);

        // The pixel format a decoded frame for this codec comes back as (see
        // webcam_pixel_format) - fixed per codec, not a caller choice.
        static RWC_API webcam_pixel_format pixel_format_for_codec(uint32_t codec) noexcept;

        // Only meaningful when pixel_format_for_codec(codec) == nv12: whether
        // the chroma samples are full-range (JPEG/MJPEG's native YCbCr) or
        // studio/limited-range (H264's native YUV, the typical broadcast
        // convention). A renderer must pick a matching colorspace/matrix for
        // the two, or the picture's contrast/black level will look wrong.
        static RWC_API bool codec_uses_full_range_yuv(uint32_t codec) noexcept;

        template <typename Compare = std::greater<>, typename Projection = std::identity>
        static constexpr std::optional<capture_format_info> select_capture_format(std::span<const rwc::capture_format_info> formats,
                                                                                  std::optional<uint32_t> codec,
                                                                                  Compare comp = {},
                                                                                  Projection proj = {})
        {
            using result_type = std::remove_cvref_t<std::invoke_result_t<Projection, const capture_format_info&>>;
            std::optional<capture_format_info> best_format;
            std::optional<result_type> best_value;

            for (const auto& format : formats)
            {
                if (codec.has_value() && format.codec != codec.value())
                    continue;

                auto&& value = std::invoke(proj, format);
                if (!best_value || comp(value, *best_value))
                {
                    best_format = format;
                    best_value = std::forward<decltype(value)>(value);
                }
            }

            return best_format;
        }
    };
}

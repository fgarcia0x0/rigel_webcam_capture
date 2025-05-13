#pragma once

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

        template <typename Compare = std::greater<>, typename Projection = std::identity>
        static constexpr std::optional<capture_format_info> select_capture_format(std::span<const rwc::capture_format_info> formats,
                                                                                  uint32_t codec,
                                                                                  Compare comp = {},
                                                                                  Projection proj = {})
        {
            using result_type = std::remove_cvref_t<std::invoke_result_t<Projection, const capture_format_info&>>;
            std::optional<capture_format_info> best_format;
            std::optional<result_type> best_value;

            for (const auto& format : formats)
            {
                if (format.codec != codec)
                    continue;

                auto value = std::invoke(proj, format);
                if (!best_value || comp(value, *best_value))
                {
                    best_format = format;
                    best_value = value;
                }
            }

            return best_format;
        }
    };
}

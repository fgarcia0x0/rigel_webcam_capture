#pragma once

#include <rwc/core/webcam_device.hpp>

#include <span>
#include <chrono>
#include <cstdint>
#include <optional>

namespace rwc
{
    struct webcam_utils
    {
        static std::chrono::milliseconds current_timestamp();
        static std::optional<rwc::capture_format_info> select_best_format(std::span<const rwc::capture_format_info> formats, uint32_t codec);
        static std::string prop_type_to_string(webcam_property_type type);
    };
}

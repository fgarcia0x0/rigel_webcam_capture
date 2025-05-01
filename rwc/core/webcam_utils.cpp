#include <chrono>
#include <optional>
#include <rwc/core/webcam_utils.h>

#include <ranges>
#include <algorithm>

namespace rwc
{
    std::chrono::milliseconds webcam_utils::current_timestamp()
    {
        using milli = std::chrono::milliseconds;
        using clock = std::chrono::system_clock;
        return std::chrono::duration_cast<milli>(clock::now().time_since_epoch());
    }

    std::optional<capture_format_info> webcam_utils::select_best_format(std::span<const rwc::capture_format_info> formats, uint32_t codec)
    {
        auto fmts_filtered = formats | std::views::filter([codec](const auto& format) {
            return format.codec == codec;
        });

        if (fmts_filtered.empty())
            return std::nullopt;

        auto best_it = std::ranges::max_element(fmts_filtered, {}, [](const auto& format) {
            return format.width * format.height * format.fps;
        });

        return *best_it;
    }
    
    std::string webcam_utils::prop_type_to_string(webcam_property_type type)
    {
        std::string name = "";

        switch (type) 
        {
        case webcam_property_type::exposure: 
            name += "exposure"; 
            break;
        case webcam_property_type::auto_exposure:
            name += "auto_exposure"; 
            break;
        case webcam_property_type::focus:
            name += "focus"; 
            break;
        case webcam_property_type::auto_focus:
            name += "auto_focus"; 
            break;
        case webcam_property_type::zoom:
            name += "zoom"; 
            break;
        case webcam_property_type::white_balance:
            name += "white_balance"; 
            break;
        case webcam_property_type::auto_white_balance:
            name += "auto_white_balance"; 
            break;
        case webcam_property_type::gain:
            name += "gain"; 
            break;
        case webcam_property_type::auto_gain:
            name += "auto_gain"; 
            break;
        case webcam_property_type::brightness:
            name += "brightness"; 
            break;
        case webcam_property_type::contrast:
            name += "contrast"; 
            break;
        case webcam_property_type::saturation:
            name += "saturation"; 
            break;
        case webcam_property_type::gamma:
            name += "gamma"; 
            break;
        case webcam_property_type::hue:
            name += "hue"; 
            break;
        case webcam_property_type::sharpness:
            name += "sharpness"; 
            break;
        case webcam_property_type::back_light_comp:
            name += "back_light_comp"; 
            break;
        case webcam_property_type::power_line_freq:
            name += "power_line_freq"; 
            break;
        case webcam_property_type::last:
            break;
        }

        return name;
    }
}

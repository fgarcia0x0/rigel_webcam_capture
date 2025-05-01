#pragma once

#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <sstream>

#include <rwc/platform/platform.hpp>

namespace rwc::utils
{
    static inline std::time_t current_timestamp() noexcept
    {
        using clock = std::chrono::system_clock;
        auto dur = std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch());
        return static_cast<std::time_t>(dur.count());
    }

    static inline std::string format_timestamp(std::time_t timestamp, std::string_view fmt = "%d-%m-%Y %H:%M:%S") 
    {
        std::time_t time = timestamp;
        std::tm tm{};
        
        #if defined(RWC_PLATFORM_WINDOWS)
            localtime_s(&tm, &time);  // Windows
        #elif defined (RWC_PLATFORM_LINUX)
            localtime_r(&time, &tm);  // POSIX
        #else
            tm = *std::localtime(&time);
        #endif

        std::ostringstream oss;
        oss << std::put_time(&tm, fmt.data());

        return oss.str();
    }
}
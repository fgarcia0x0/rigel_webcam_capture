#pragma once

#include <rwc/platform/platform.hpp>
#include <rwc/logger/log_sink.h>
#include <rwc/logger/log_message.hpp>

#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <sstream>
#include <format>
#include <utility>
#include <iomanip>

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

    static inline std::string format_log_message(const log_message& msg)
    {
        constexpr const char* level_str[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL" };

        std::string_view filename = msg.loc.file_name();
        std::string ts = rwc::utils::format_timestamp(msg.timestamp);
        filename = filename.substr(filename.find_last_of("/\\") + 1);
        
        return std::format("[{}] [{}] [{}:{}] {}", ts, level_str[std::to_underlying(msg.level)], filename, msg.loc.line(), msg.msg);
    }

}
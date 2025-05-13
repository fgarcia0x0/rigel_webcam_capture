#pragma once

#include <cstdint>
#include <string>
#include <ctime>
#include <source_location>

namespace rwc
{
    enum class log_level : uint32_t
    {
        trace,
        info,
        debug,
        warn,
        error,
        critical
    };

    struct log_message
    {
        log_level level;
        std::string msg;
        std::time_t timestamp;
        std::source_location loc;
    };
}

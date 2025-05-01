#pragma once

#include <rwc/logger/logger.h>
#include <rwc/utils/utils.hpp>
#include <rwc/platform/platform.hpp>

#include <print>

namespace rwc
{
    class console_sink final : public log_sink
    {
    public:
        console_sink() = default;

        void write(const log_message& msg) override
        {
            bool is_debug = false;
            #ifdef RWC_DEBUG_MODE
                is_debug = true;
            #endif
    
            if (msg.level == log_level::debug && !is_debug)
                return;
    
            constexpr const char* level_str[] = { "TRACE", "INFO", "DEBUG", "WARN", "ERROR", "CRITICAL" };
            std::string_view filename = msg.loc.file_name();
            std::string ts = utils::format_timestamp(msg.timestamp);
            filename = filename.substr(filename.find_last_of("/\\") + 1);
            
            std::println("[{}] [{}] [{}:{}] {}", ts, level_str[uint32_t(msg.level)], filename, msg.loc.line(), msg.msg);
        }

        ~console_sink() = default;
    };
}

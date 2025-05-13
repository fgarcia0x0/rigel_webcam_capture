#include <rwc/logger/console_sink.h>
#include <rwc/utils/utils.hpp>

#include <utility>
#include <print>

namespace rwc
{
    void console_sink::write(const log_message& msg)
    {
        FILE* fileptr = {};

        if (std::to_underlying(msg.level) >= std::to_underlying(log_level::warn))
            fileptr = stderr;
        else
            fileptr = stdout;

        std::println(fileptr, "{}", rwc::utils::format_log_message(msg));
    }
}

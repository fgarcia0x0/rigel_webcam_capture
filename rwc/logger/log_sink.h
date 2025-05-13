#pragma once

#include <rwc/platform/platform.hpp>
#include <rwc/logger/log_message.hpp>

namespace rwc
{
    class RWC_API log_sink
    {
    public:
        virtual ~log_sink() = default;
        virtual void write(const log_message& msg) = 0;
    };
}

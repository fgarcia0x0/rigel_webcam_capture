#pragma once

#include <rwc/logger/log_sink.h>
#include <rwc/utils/utils.hpp>
#include <rwc/platform/platform.hpp>

namespace rwc
{
    class RWC_API console_sink final : public log_sink
    {
    public:
        console_sink() = default;
        void write(const log_message& msg) override;
        ~console_sink() = default;
    };
}

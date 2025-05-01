#pragma once

namespace rwc
{
    struct log_message;

    class log_sink
    {
    public:
        virtual ~log_sink() = default;
        virtual void write(const log_message& msg) = 0;
    };
}

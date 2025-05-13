#pragma once

#include <rwc/logger/log_sink.h>
#include <rwc/utils/utils.hpp>
#include <rwc/platform/platform.hpp>

#include <cstdio>
#include <memory>

namespace rwc
{
    class file_sink final : public log_sink
    {
    public:
        enum class access_mode { truncate, append };

        RWC_API file_sink(std::string_view filepath, access_mode mode = access_mode::append);
        RWC_API void write(const log_message& msg) override;
        RWC_API ~file_sink();
    private:
        std::unique_ptr<FILE, decltype(&fclose)> m_file_ptr;
    };
}

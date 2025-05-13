#include <rwc/logger/file_sink.h>
#include <rwc/utils/utils.hpp>

#include <print>

namespace rwc
{
    file_sink::file_sink(std::string_view filepath, access_mode mode)
        : m_file_ptr{ fopen(filepath.data(), (mode == access_mode::append) ? "a" : "w"), fclose }
    {
    }

    void file_sink::write(const log_message& msg)
    {
        if (m_file_ptr)
            std::println(m_file_ptr.get(), "{}", rwc::utils::format_log_message(msg));
    }
    
    file_sink::~file_sink() = default;
}

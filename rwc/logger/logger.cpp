#include <rwc/logger/logger.h>
#include <rwc/utils/utils.hpp>

#include <string>
#include <utility>

namespace rwc
{
    void logger::log_impl(log_level level, const std::source_location& loc, std::string_view msg)
    {
        if (std::to_underlying(level) < std::to_underlying(m_min_level.load()))
            return;

        const auto ts = rwc::utils::current_timestamp();
        log_message log_msg = { level, std::string{ msg }, ts, loc};

        auto filter_ptr = m_filter_fn.load(std::memory_order_acquire);
        if (filter_ptr && !(*filter_ptr)(log_msg)) 
            return;

        // TIP(garcia): we must create a local copy because its sink->write possible taken large amount of time due the disk writes
        std::vector<std::shared_ptr<log_sink>> sinks_copy;
        {
            std::lock_guard lock{ m_sink_mtx };
            sinks_copy = m_sinks;
        }

        for (const auto& sink : sinks_copy)
            sink->write(log_msg);
    }

    void logger::set_filter(std::function<bool(const log_message&)> filter) 
    {
        m_filter_fn.store(std::make_shared<filter_func>(std::move(filter)));
    }

    void logger::add_sink(std::shared_ptr<log_sink> sink)
    {
        std::lock_guard sink_guard{ m_sink_mtx };
        m_sinks.push_back(std::move(sink));
    }

    void logger::set_min_level(log_level level)
    {
        m_min_level = level;
    }
}

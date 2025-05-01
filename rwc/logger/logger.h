#pragma once

#include <string_view>
#include <string>
#include <mutex>
#include <atomic>
#include <memory>
#include <format>
#include <functional>
#include <source_location>

#include <rwc/platform/platform.hpp>
#include <rwc/logger/log_sink.h>

#define RWC_LOG_TRACE(...) rwc::logger::instance().log(rwc::log_level::trace,    std::source_location::current(), __VA_ARGS__)
#define RWC_LOG_INFO(...)  rwc::logger::instance().log(rwc::log_level::info,     std::source_location::current(), __VA_ARGS__)
#define RWC_LOG_DEBUG(...) rwc::logger::instance().log(rwc::log_level::debug,    std::source_location::current(), __VA_ARGS__)
#define RWC_LOG_WARN(...)  rwc::logger::instance().log(rwc::log_level::warn,     std::source_location::current(), __VA_ARGS__)
#define RWC_LOG_ERROR(...) rwc::logger::instance().log(rwc::log_level::error,    std::source_location::current(), __VA_ARGS__)
#define RWC_LOG_CRIT(...)  rwc::logger::instance().log(rwc::log_level::critical, std::source_location::current(), __VA_ARGS__)

#define RWC_LOG_INIT

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

    class logger
    {
    public:
        using filter_func = std::function<bool(const log_message&)>;
    public:
        static logger& instance() noexcept
        {
            static logger logger;
            return logger;
        }

        void set_filter(filter_func filter);
        void add_sink(std::shared_ptr<log_sink> sink);
        void set_min_level(log_level level);

        template<typename... Args>
        constexpr void log(log_level level, const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) 
        {
            std::string msg = std::format(fmt, std::forward<Args>(args)...); 
            log_impl(level, loc, msg);
        }
        
    private:
        void log_impl(log_level level, const std::source_location& loc, std::string_view msg);
        
        std::vector<std::shared_ptr<log_sink>> m_sinks;
        std::mutex m_sink_mtx;
        std::atomic<log_level> m_min_level;
        std::atomic<std::shared_ptr<filter_func>> m_filter_fn;
    };
}

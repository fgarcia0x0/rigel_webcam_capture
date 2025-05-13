#pragma once

#include <ratio>
#include <string>
#include <chrono>
#include <rwc/logger/logger.h>

namespace rwc
{
    using seconds_dbl = std::chrono::duration<double>;
    using milli_dbl = std::chrono::duration<double, std::milli>;
    using micro_dbl = std::chrono::duration<double, std::micro>;
    using nano_dbl = std::chrono::duration<double, std::nano>;

    template <typename Duration = std::chrono::milliseconds>
    class scoped_timer
    {
    public:
        using clock_type = std::chrono::steady_clock;

        scoped_timer(std::string msg) noexcept
            : m_msg{ std::move(msg) }
            , m_start{ clock_type::now() }
        {
        }

        void set_num_digits(uint32_t num_digits) noexcept
        {
            m_num_digits = num_digits;
        }

        void set_log_level(log_level level) noexcept
        {
            m_log_level = level;
        }

        void reset() noexcept
        {
            m_start = clock_type::now();
        }

        ~scoped_timer() noexcept
        {
            using Period = typename Duration::period;
            std::string_view target_suffix{};
            
            if constexpr (std::is_same_v<Period, std::ratio<1>>)
                target_suffix = "s";
            else if constexpr (std::is_same_v<Period, std::milli>)
                target_suffix = "ms";
            else if constexpr (std::is_same_v<Period, std::micro>)
                target_suffix = "µs";
            else if constexpr (std::is_same_v<Period, std::nano>)
                target_suffix = "ns";

            const auto duration = std::chrono::duration_cast<Duration>(clock_type::now() - m_start);
            rwc::logger::instance().log(m_log_level,
                                        std::source_location::current(),
                                        "[Benchmark] \"{}\" took {:.{}f} {}", m_msg,
                                        static_cast<double>(duration.count()),
                                        m_num_digits, target_suffix);
                          
        }

    private:
        std::string m_msg;
        clock_type::time_point m_start;
        uint32_t m_num_digits{ 3 };
        log_level m_log_level{ log_level::debug };
    };
}

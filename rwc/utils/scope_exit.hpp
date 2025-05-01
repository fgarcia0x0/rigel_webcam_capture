#pragma once

#include <functional>
#include <utility>

namespace rwc
{
    struct scope_exit
    {
    public:
        template <typename F, typename... Args>
        constexpr scope_exit(F&& func, Args&&... args)
        {
            m_callback = [f = std::forward<F>(func), ...args = std::forward<Args>(args)]() mutable {
                std::invoke(f, std::forward<Args>(args)...);
            };
        }

        constexpr void reset()
        {
            m_callback = {};
        }

        constexpr ~scope_exit()
        {
            if (m_callback)
                m_callback();
        }

    private:
        std::function<void(void)> m_callback;
    };
}

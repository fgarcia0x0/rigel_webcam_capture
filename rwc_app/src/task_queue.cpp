#include "task_queue.h"

#include <chrono>
#include <future>
#include <mutex>
#include <exception>

#include <rwc/logger/logger.h>

void task_queue::push_task(task_type task)
{
    std::lock_guard lock(m_task_mutex);
    m_tasks.push(std::move(task));
}

void task_queue::push_task(std::future<void> result, std::function<void(void)> callback)
{
    push_task(task_type{ std::move(result), std::move(callback) });
}

void task_queue::push_task(std::function<void()> async_callback, std::function<void(void)> sync_callback)
{
    auto&& async_task = std::async(std::launch::async, std::move(async_callback));
    task_type task_type{ .result = std::move(async_task), .callback = std::move(sync_callback) };
    push_task(std::move(task_type));
}

task_type& task_queue::front() noexcept
{
    return m_tasks.front();
}

void task_queue::pop()
{
    m_tasks.pop();
}

bool task_queue::empty() const noexcept
{
    return m_tasks.empty();
}

void task_queue::process_tasks(std::chrono::milliseconds timeout)
{
    std::lock_guard lock(m_task_mutex);

    while (!m_tasks.empty())
    {
        auto& task = m_tasks.front();
        if (task.result.wait_for(timeout) == std::future_status::ready)
        {
            // task.result.get() rethrows whatever the background work threw;
            // task.callback() runs on this (the UI) thread. Neither is allowed
            // to escape process_tasks() - an uncaught exception here would
            // blow up the main loop - but silently discarding it (as this used
            // to do) hides real bugs, so log it instead.
            try
            {
                task.result.get();
            }
            catch (const std::exception& error)
            {
                RWC_LOG_ERROR("Unhandled exception from an async task: {}", error.what());
            }
            catch (...)
            {
                RWC_LOG_ERROR("Unhandled non-standard exception from an async task");
            }

            try
            {
                if (task.callback)
                    task.callback();
            }
            catch (const std::exception& error)
            {
                RWC_LOG_ERROR("Unhandled exception from a task completion callback: {}", error.what());
            }
            catch (...)
            {
                RWC_LOG_ERROR("Unhandled non-standard exception from a task completion callback");
            }

            pop();
        }
        else
        {
            break;
        }
    }
}

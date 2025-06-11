#include "task_queue.h"

#include <chrono>
#include <future>
#include <mutex>

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
            try
            {
                task.result.get();
                if (task.callback)
                    task.callback();
            }
            catch (...) {}

            pop();
        } 
        else
        {
            break;
        }
    }
}

#pragma once

#include <queue>
#include <mutex>
#include <future>
#include <chrono>
#include <functional>

struct task_type
{
    std::future<void> result;
    std::function<void(void)> callback;
};

class task_queue
{
public:
    void push_task(task_type task);
    void push_task(std::future<void> result, std::function<void(void)> callback);
    void push_task(std::function<void()> async_callback, std::function<void(void)> sync_callback = nullptr);
    task_type& front() noexcept;
    void pop();
    bool empty() const noexcept;

    void process_tasks(std::chrono::milliseconds timeout = std::chrono::milliseconds(0));
private:
    std::queue<task_type> m_tasks;
    std::mutex m_task_mutex;
};


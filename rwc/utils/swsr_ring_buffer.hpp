#pragma once

#include <cstddef>
#include <atomic>
#include <optional>

namespace rwc
{
    // Single-Reader Single-Writer Ring Buffer
    template <typename T, size_t Capacity> requires (Capacity > 1)
    class swsr_ring_buffer
    {
    public:
        constexpr void enqueue(T item) noexcept
        {
            size_t current_tail = m_tail.load(std::memory_order_relaxed);
            size_t next_tail = (current_tail + 1) % Capacity;

            if (next_tail == m_head.load(std::memory_order_acquire)) 
            {
                // buffer is full — overwrite oldest value
                size_t new_index = m_head.load(std::memory_order_relaxed) + 1;
                m_head.store(new_index % Capacity, std::memory_order_release);
            }

            m_buffer[current_tail] = std::move(item);
            m_tail.store(next_tail, std::memory_order_release);
        }

        constexpr std::optional<T> dequeue() noexcept
        {
            size_t current_head = m_head.load(std::memory_order_relaxed);
    
            // the queue is empty
            if (current_head == m_tail.load(std::memory_order_acquire)) 
                return std::nullopt;
    
            T item{ std::move(m_buffer[current_head]) };
            m_head.store((current_head + 1) % Capacity, std::memory_order_release);

            return item;
        }
        
        constexpr size_t capacity() const noexcept
        {
            return Capacity;
        }

        constexpr size_t size() const noexcept
        {
            size_t current_head = m_head.load(std::memory_order_acquire);
            size_t current_tail = m_tail.load(std::memory_order_acquire);

            if (current_tail >= current_head) 
                return current_tail - current_head; 
            else 
                return Capacity - (current_head - current_tail);
        }

        constexpr size_t empty() const noexcept
        {
            return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
        }

        constexpr void clear() noexcept
        {
            m_head.store(0, std::memory_order_release);
            m_tail.store(0, std::memory_order_release);
        }

    private:
        T m_buffer[Capacity] = {};
        std::atomic<size_t> m_head{};
        std::atomic<size_t> m_tail{};
    };
}

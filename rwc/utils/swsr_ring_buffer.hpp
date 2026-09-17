#pragma once

#include <cstddef>
#include <atomic>
#include <optional>

namespace rwc
{
    // Single-Reader Single-Writer Ring Buffer.
    // Safe for exactly one producer thread calling enqueue() and exactly one
    // consumer thread calling dequeue()/empty()/size() concurrently. clear()
    // is not safe to call while the producer may still be enqueuing.
    template <typename T, size_t Capacity> requires (Capacity > 1)
    class swsr_ring_buffer
    {
    public:
        // Returns true if the buffer was full and the oldest, not-yet-read
        // item was overwritten to make room for this one.
        bool enqueue(T item) noexcept
        {
            size_t current_tail = m_tail.load(std::memory_order_relaxed);
            size_t next_tail = (current_tail + 1) % Capacity;

            bool dropped = false;
            if (next_tail == m_head.load(std::memory_order_acquire))
            {
                // Buffer is full — drop the oldest item to make room for the
                // newest one. m_head is also read/written by the reader
                // thread's dequeue(), so advancing it here must be a single
                // atomic read-modify-write (CAS loop). A separate load+store
                // would race with a concurrent dequeue() and could roll
                // m_head backwards, handing the reader an item that was
                // already moved-from.
                size_t head = m_head.load(std::memory_order_relaxed);
                size_t next_head;
                do
                {
                    next_head = (head + 1) % Capacity;
                } while (!m_head.compare_exchange_weak(head, next_head,
                                                         std::memory_order_release,
                                                         std::memory_order_relaxed));
                dropped = true;
                m_dropped_count.fetch_add(1, std::memory_order_relaxed);
            }

            m_buffer[current_tail] = std::move(item);
            m_tail.store(next_tail, std::memory_order_release);

            return dropped;
        }

        std::optional<T> dequeue() noexcept
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

        size_t size() const noexcept
        {
            size_t current_head = m_head.load(std::memory_order_acquire);
            size_t current_tail = m_tail.load(std::memory_order_acquire);

            if (current_tail >= current_head)
                return current_tail - current_head;
            else
                return Capacity - (current_head - current_tail);
        }

        bool empty() const noexcept
        {
            return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
        }

        // Total number of items ever overwritten by enqueue() because the
        // reader could not keep up. Monotonically increasing; not reset by clear().
        size_t dropped_count() const noexcept
        {
            return m_dropped_count.load(std::memory_order_relaxed);
        }

        // Resets to empty AND destroys every slot's current contents (e.g.
        // releasing a still-held frame buffer back to its pool) instead of
        // just rewinding the indices. Without this, an item enqueued but
        // never dequeued before a stop/restart would sit alive in m_buffer
        // until some future enqueue() happened to overwrite that exact slot
        // - on a v4l2_webcam_device, that means its pooled buffer could be
        // released back to frame_buffer_pool *after* the pool was reset()
        // for a new, differently-sized session, silently reintroducing a
        // wrong-sized buffer into a pool whose one invariant is that every
        // buffer in it is the same size.
        void clear() noexcept
        {
            for (auto& slot : m_buffer)
                slot = T{};

            m_head.store(0, std::memory_order_release);
            m_tail.store(0, std::memory_order_release);
        }

    private:
        T m_buffer[Capacity] = {};
        alignas(64) std::atomic<size_t> m_head{};
        alignas(64) std::atomic<size_t> m_tail{};
        std::atomic<size_t> m_dropped_count{};
    };
}

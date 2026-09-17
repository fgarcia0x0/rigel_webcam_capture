#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <vector>
#include <functional>

namespace rwc
{
    // Fixed-capacity free list of raw uint8_t[] blocks, recycled across
    // captured frames instead of doing a heap alloc/free pair per frame.
    //
    // acquire() may be called from any single thread (the capture thread, in
    // practice); the returned buffer's deleter (which calls release()) can
    // run on *any* thread, since ownership of a webcam_frame_rgb24 is handed
    // off to the caller of read_frame() and its buffer is freed whenever that
    // caller lets it go out of scope. release() is therefore mutex-protected.
    //
    // All buffers handed out by a given pool instance must be the same size
    // (the RGB24 frame size for the currently active stream); call reset()
    // whenever that size may have changed (e.g. before starting a new
    // streaming session) so stale, wrongly-sized buffers are never recycled.
    class frame_buffer_pool : public std::enable_shared_from_this<frame_buffer_pool>
    {
    public:
        using owned_buffer = std::unique_ptr<std::uint8_t[], std::function<void(std::uint8_t*)>>;

        explicit frame_buffer_pool(size_t max_pooled_buffers) noexcept
            : m_max_pooled_buffers(max_pooled_buffers)
        {
        }

        frame_buffer_pool(const frame_buffer_pool&) = delete;
        frame_buffer_pool& operator=(const frame_buffer_pool&) = delete;

        // Returns a buffer of exactly `size` bytes, recycled from the free
        // list when possible. Returns an empty (null) owned_buffer on
        // allocation failure.
        owned_buffer acquire(size_t size)
        {
            std::unique_ptr<std::uint8_t[]> raw = take_from_free_list();

            if (!raw)
            {
                raw.reset(new (std::nothrow) std::uint8_t[size]);
                if (!raw)
                    return owned_buffer{ nullptr, make_deleter() };
            }

            return owned_buffer{ raw.release(), make_deleter() };
        }

        // Drops every buffer currently sitting in the free list. Call this
        // before starting a new streaming session, since buffers from a
        // previous session may be sized for a different resolution.
        void reset() noexcept
        {
            std::lock_guard lock(m_mutex);
            m_free_list.clear();
        }

    private:
        std::unique_ptr<std::uint8_t[]> take_from_free_list()
        {
            std::lock_guard lock(m_mutex);
            if (m_free_list.empty())
                return nullptr;

            auto buffer = std::move(m_free_list.back());
            m_free_list.pop_back();
            return buffer;
        }

        void release(std::uint8_t* ptr) noexcept
        {
            std::lock_guard lock(m_mutex);
            if (m_free_list.size() < m_max_pooled_buffers)
            {
                m_free_list.emplace_back(ptr);
            }
            else
            {
                delete[] ptr;
            }
        }

        std::function<void(std::uint8_t*)> make_deleter()
        {
            // Captures a weak_ptr, not `this`: if the pool (and the device
            // that owns it) is destroyed while a frame handed out to a
            // caller is still alive, the buffer is just freed normally
            // instead of dereferencing a dangling pool.
            return [weak_self = weak_from_this()](std::uint8_t* ptr)
            {
                if (!ptr)
                    return;

                if (auto self = weak_self.lock())
                    self->release(ptr);
                else
                    delete[] ptr;
            };
        }

        std::mutex m_mutex;
        std::vector<std::unique_ptr<std::uint8_t[]>> m_free_list;
        size_t m_max_pooled_buffers;
    };
}

#pragma once

#include <rwc/core/webcam_device.hpp>
#include <rwc/utils/swsr_ring_buffer.hpp>
#include <rwc/platform/linux/webcam_image_decoder.h>
#include <rwc/platform/linux/frame_buffer_pool.h>

#include <span>
#include <vector>
#include <optional>
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>
#include <stop_token>

namespace rwc
{
    class v4l2_webcam_device : public webcam_device
    {
    public:
        v4l2_webcam_device() = default;
        v4l2_webcam_device(const v4l2_webcam_device&) = delete;
        v4l2_webcam_device& operator=(const v4l2_webcam_device&) = delete;
        v4l2_webcam_device(v4l2_webcam_device&&) = delete;
        v4l2_webcam_device& operator=(v4l2_webcam_device&&) = delete;

        // Device Operations
        webcam_error_status open(uint32_t index) override;
        webcam_error_status reset(uint32_t index) override;
        void close() override;
        bool is_opened() const override;
        const webcam_device_info& device_info() const override;
        capture_format_info current_format() noexcept override;
        bool set_current_format(const capture_format_info& format) override;
        bool set_current_format_by_index(uint32_t index) override;
        bool set_preferred_decode_backend(hwd_decode_backend backend) override;
        static uint32_t device_count() noexcept;

        webcam_controller* ctrl() noexcept override;

        // Streaming Operations
        bool has_pending_frame() const override;
        bool is_streaming() const override;
        webcam_error_status start_stream() override;
        void stop_stream() override;
        std::expected<webcam_frame_rgb24, webcam_error_status> read_frame() override;

        virtual ~v4l2_webcam_device() override;
    private:
        std::optional<webcam_device_info> read_device_info();
        std::vector<capture_format_info> read_webcam_formats();
        webcam_error_status setup_webcam_image();
        webcam_error_status create_webcam_buffers(uint32_t buffer_count);
        void destroy_webcam_buffers();
        bool enqueue_buffers();
        bool dequeue_buffers();
        bool webcam_start_streaming();
        bool webcam_stop_streaming();
        webcam_error_status wait_device_ready(std::chrono::seconds timeout);
        void v4l2_capture_thread(std::stop_token token);
        webcam_error_status decode_frame_to_rgb24(std::span<const uint8_t> raw_src, webcam_frame_rgb24* frame,
                                                    uint32_t codec, webcam_image_decoder* decoder);
        uint32_t to_v4l2_type(webcam_property_type type);

        struct buffer_data
        {
            void* data;
            size_t length;
        };
    private:
        int32_t m_device_fd{ -1 };
        std::string m_device_path{};
        webcam_device_info m_device_info{};
        capture_format_info m_current_format{};
        std::vector<buffer_data> m_buffer_pool;
        std::unique_ptr<std::jthread> m_v4l2_thread;
        swsr_ring_buffer<webcam_frame_rgb24, RWC_WEBCAM_STREAMING_BUFFER_COUNT> m_frame_queue;
        // Buffers actually "in flight" (queued in m_frame_queue, being
        // decoded into, or held by the caller) are never in this free list -
        // it only smooths the alloc/free churn of buffers that have already
        // been fully returned. It intentionally stays small: at max
        // resolution (e.g. 2560x1440 RGB24 is ~10.5MB/buffer) a generous cap
        // here would let the pool hoard tens of MB of idle memory instead of
        // giving it back, which would work against the whole point of pooling.
        std::shared_ptr<frame_buffer_pool> m_frame_pool{ std::make_shared<frame_buffer_pool>(2) };
        std::atomic<webcam_error_status> m_last_frame_status{ webcam_error_status::ok };
        std::atomic<bool> m_opened{ false };
        std::atomic<bool> m_streaming{ false };
        std::unique_ptr<webcam_controller> m_controller{ nullptr };
    };
}

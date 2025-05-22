#pragma once

#include <rwc/core/webcam_device.hpp>
#include <rwc/utils/swsr_ring_buffer.hpp>
#include <rwc/core/webcam_device.hpp>
#include <rwc/platform/platform.hpp>
#include <rwc/platform/windows/mmf_webcam_controller.h>

#include <vector>
#include <optional>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <stop_token>

namespace rwc
{
    class mmf_webcam_device : public webcam_device
    {
    public:
        mmf_webcam_device();
        mmf_webcam_device(const mmf_webcam_device&) = delete;
        mmf_webcam_device& operator=(const mmf_webcam_device&) = delete;
        mmf_webcam_device(mmf_webcam_device&&) = delete;
        mmf_webcam_device& operator=(mmf_webcam_device&&) = delete;

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
        static RWC_API uint32_t device_count() noexcept;

        // Ctrl Operations
        webcam_controller* ctrl() noexcept override;

        // Streaming Operations
        bool has_pending_frame() const override;
        bool is_streaming() const override;
        webcam_error_status start_stream() override;
        void stop_stream() override;
        std::expected<webcam_frame_rgb24, webcam_error_status> read_frame() override;

        virtual ~mmf_webcam_device() override;
    private:
        std::optional<webcam_device_info> read_device_info(uint32_t device_index, void* imf_device, void* source_reader);
        std::vector<capture_format_info> read_webcam_formats(uint32_t device_index, void* source_reader);
        webcam_error_status setup_webcam_image();
        void warmup_webcam(uint32_t warmup_frames);
        bool try_enable_hardware_decoding(void* attribs, void** d3d_device_out, void** d3d_device_ctx_out, void** dxgi_manager_out);
        void mmf_capture_thread(std::stop_token token);
        bool try_create_gpu_pipeline();
        webcam_error_status decode_frame_to_bgr24(void* sample, uint8_t* buffer);
        webcam_error_status cpu_frame_decode(void* sample, uint8_t* output);
        webcam_error_status gpu_frame_decode(void* sample, uint8_t* output);
        std::string get_string_attr(void* imf_activate, const void* guid_key);

    private:
        struct context;
        std::unique_ptr<context> m_context;
        std::string m_device_path{};
        webcam_device_info m_device_info{};
        capture_format_info m_current_format{};
        std::unique_ptr<std::jthread> m_mmf_thread;
        swsr_ring_buffer<webcam_frame_rgb24, RWC_WEBCAM_STREAMING_BUFFER_COUNT> m_frame_queue;
        std::atomic<webcam_error_status> m_last_frame_status{ webcam_error_status::ok };
        hwd_decode_backend m_preferred_backend{ hwd_decode_backend::none };
        std::unique_ptr<mmf_webcam_controller> m_controller;
        std::atomic<bool> m_opened{ false };
        std::atomic<bool> m_streaming{ false };
        bool m_com_initialized{ false };
    };
}

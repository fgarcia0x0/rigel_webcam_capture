#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <expected>
#include <array>
#include <chrono>
#include <memory>

namespace rwc
{    
    namespace detail
    {
        static constexpr uint32_t FourCC(const std::array<uint8_t, 4>& buffer) 
        {
            return static_cast<uint32_t>(buffer[0]) |
                   (static_cast<uint32_t>(buffer[1]) << 8) |
                   (static_cast<uint32_t>(buffer[2]) << 16) |
                   (static_cast<uint32_t>(buffer[3]) << 24);
        }
    }

    static constexpr auto RWC_WEBCAM_CODEC_TYPE_MJPEG                = detail::FourCC({'M', 'J', 'P', 'G'});
    static constexpr auto RWC_WEBCAM_CODEC_TYPE_JPEG                 = detail::FourCC({'J', 'P', 'E', 'G'});
    static constexpr auto RWC_WEBCAM_CODEC_TYPE_H264                 = detail::FourCC({'H', '2', '6', '4'});
    static constexpr auto RWC_WEBCAM_CODEC_TYPE_YUYV                 = detail::FourCC({'Y', 'U', 'Y', 'V'});
    static constexpr auto RWC_WEBCAM_CODEC_TYPE_DEFAULT              = RWC_WEBCAM_CODEC_TYPE_MJPEG;
    static constexpr auto RWC_WEBCAM_TYPICAL_FRAMERATE               = 30u;
    static constexpr auto RWC_WEBCAM_STREAMING_BUFFER_COUNT          = 2u;
    static constexpr auto RWC_WEBCAM_STREAMING_MIN_BUFFER_COUNT      = 2u;
    static constexpr auto RWC_WEBCAM_STREAMING_TIMEOUT               = std::chrono::seconds{ 2 };
    static constexpr auto RWC_WEBCAM_STREAMING_MAX_CONTINUOS_TIMEOUT = 10u;

    struct capture_format_info
    {
        uint32_t width;
        uint32_t height;
        uint32_t fps{ RWC_WEBCAM_TYPICAL_FRAMERATE };
        uint32_t codec{ RWC_WEBCAM_CODEC_TYPE_DEFAULT };

        friend constexpr auto operator<=>(const capture_format_info&, const capture_format_info&) = default;
    };

    struct webcam_device_info
    {
        std::string unique_id;
        std::string name;
        std::string path;
        std::uint32_t index;
        std::vector<capture_format_info> formats;
    };

    struct webcam_frame_rgb24
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t size;
        std::time_t timestamp;
        std::unique_ptr<std::uint8_t[]> buffer;
    };

    enum class webcam_error_status
    {
        ok = 0,
        invalid_device,
        bad_device,
        unsupported_codec,
        memory_exhausted,
        invalid_format,
        device_cannot_capture,
        device_not_available,
        device_cannot_streaming,
        device_already_opened,
        cannot_set_image_format,
        cannot_create_buffer,
        cannot_setup_buffer,
        cannot_map_buffer,
        cannot_enqueue_buffer,
        cannot_dequeue_buffer,
        cannot_start_streaming,
        cannot_stop_streaming,
        cannot_setup_framerate,
        invalid_state,
        frame_not_ready,
        timeout,
        wait_error,
        driver_not_enough_buffers,
        cannot_decode_frame,
        query_capability_failed
    };
    
    enum class webcam_property_type : uint32_t
    {
        exposure,
        auto_exposure,
        focus,
        auto_focus,
        zoom,
        white_balance,
        auto_white_balance,
        gain,
        auto_gain,
        brightness,
        contrast,
        saturation,
        gamma,
        hue,
        sharpness,
        back_light_comp,
        power_line_freq,
        last
    };

    struct webcam_ctrl_property
    {
        webcam_property_type type;
        int32_t value;
        int32_t step;
        int32_t minimum;
        int32_t maximum;
        int32_t default_value;
        bool is_auto;
        uint32_t unused;
    };

    class webcam_device
    {
    public:
        // Device Operations //
        virtual webcam_error_status open(uint32_t index = 0) = 0;
        virtual webcam_error_status reset(uint32_t index = 0) = 0;
        virtual void close() = 0;

        [[nodiscard]]
        virtual bool is_opened() const = 0;

        [[nodiscard]]
        virtual const webcam_device_info& device_info() const = 0;

        [[nodiscard]]
        virtual capture_format_info current_format() noexcept = 0;

        virtual bool set_current_format(const capture_format_info& format) = 0;
        virtual bool set_current_format_by_index(uint32_t index) = 0;

        // Device Capability Operations
        [[nodiscard]]
        virtual std::optional<webcam_ctrl_property> get_ctrl_property(webcam_property_type type) = 0;
        virtual bool set_ctrl_property(webcam_property_type type, int32_t value) = 0;
        virtual bool set_ctrl_property_default(webcam_property_type type) = 0;
        virtual void reset_ctrl_properties() = 0;

        // Streaming Operations //

        [[nodiscard]]
        virtual bool is_streaming() const = 0;

        [[nodiscard]]
        virtual bool has_pending_frame() const = 0;

        virtual webcam_error_status start_stream() = 0;

        virtual void stop_stream() = 0;

        [[nodiscard]]
        virtual std::expected<webcam_frame_rgb24, webcam_error_status> read_frame() = 0;

        virtual ~webcam_device() = default;
    };
}

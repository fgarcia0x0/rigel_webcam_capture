#pragma once

#include <rwc/core/webcam_device.hpp>
#include <rwc/platform/platform.hpp>

#include <optional>

namespace rwc
{
    class v4l2_webcam_controller : public webcam_controller
    {
    public:
        v4l2_webcam_controller(int device_fd)
        {
            load(device_fd);
        }

        v4l2_webcam_controller(const v4l2_webcam_controller&) = delete;
        v4l2_webcam_controller& operator=(const v4l2_webcam_controller&) = delete;
        v4l2_webcam_controller(v4l2_webcam_controller&&) = delete;
        v4l2_webcam_controller& operator=(v4l2_webcam_controller&&) = delete;

        bool load(int device_fd);
        std::optional<webcam_ctrl_property> read_property(webcam_property_type type) override;
        bool write_property(webcam_property_type type, int32_t value, bool auto_prop) override;
        bool write_property_default(webcam_property_type type) override;
        void reset_properties() override;

        virtual ~v4l2_webcam_controller() = default;

    private:
        int m_device_fd{ -1 };
    };
}

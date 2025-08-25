#include <rwc/platform/linux/v4l2_webcam_controller.h>
#include <rwc/core/webcam_utils.h>
#include <rwc/logger/logger.h>

#include <utility>

#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

namespace rwc
{
    static inline uint32_t to_v4l2_type(webcam_property_type type, bool is_auto)
    {
        uint32_t id{ UINT32_MAX };

        switch (type)
        {
        case webcam_property_type::exposure:
            id = is_auto ? V4L2_CID_EXPOSURE_AUTO : V4L2_CID_EXPOSURE_ABSOLUTE;
            break;
        case webcam_property_type::focus:
            id = is_auto ? V4L2_CID_FOCUS_AUTO : V4L2_CID_FOCUS_ABSOLUTE;
            break; 
        case webcam_property_type::zoom:
            id = V4L2_CID_ZOOM_ABSOLUTE;
            break;
        case webcam_property_type::white_balance:
            id = is_auto ? V4L2_CID_AUTO_WHITE_BALANCE : V4L2_CID_WHITE_BALANCE_TEMPERATURE;
            break;
        case webcam_property_type::gain:
            id = is_auto ? V4L2_CID_AUTOGAIN : V4L2_CID_GAIN;
            break;
        case webcam_property_type::brightness:
            id = V4L2_CID_BRIGHTNESS;
            break;
        case webcam_property_type::contrast:
            id = V4L2_CID_CONTRAST;
            break;
        case webcam_property_type::saturation:
            id = V4L2_CID_SATURATION;
            break;
        case webcam_property_type::gamma:
            id = V4L2_CID_GAMMA;
            break;
        case webcam_property_type::hue:
            id = V4L2_CID_HUE;
            break;
        case webcam_property_type::sharpness:
            id = V4L2_CID_SHARPNESS;
            break;
        case webcam_property_type::back_light_comp:
            id = V4L2_CID_BACKLIGHT_COMPENSATION;
            break;
        case webcam_property_type::power_line_freq:
            id = V4L2_CID_POWER_LINE_FREQUENCY;
            break;
        case webcam_property_type::last:
            break;
        }

        return id;
    }

    static inline int sys_ioctl(int fd, unsigned long request, void* arg)
    {
        int result{};

        do 
        {
            result = ioctl(fd, request, arg);
        } while (result == -1 && errno == EINTR);

        return result;
    }

    bool v4l2_webcam_controller::load(int device_fd)
    {
        m_device_fd = device_fd;
        return true;
    }

    static constexpr std::optional<uint32_t> auto_flag_for(webcam_property_type type)
    {
        if (type == webcam_property_type::exposure)
            return V4L2_CID_EXPOSURE_AUTO;
        else if (type == webcam_property_type::focus)
            return V4L2_CID_FOCUS_AUTO;
        else if (type == webcam_property_type::white_balance)
            return V4L2_CID_AUTO_WHITE_BALANCE;
        else if (type == webcam_property_type::gain)
            return V4L2_CID_AUTOGAIN;
        else if (type == webcam_property_type::exposure)
            return V4L2_CID_EXPOSURE_AUTO;
        else
            return std::nullopt;
    }

    std::optional<webcam_ctrl_property> v4l2_webcam_controller::read_property(webcam_property_type type)
    {
        if (type == webcam_property_type::last)
            return {};

        webcam_ctrl_property wc_ctrl_prop{};
        v4l2_control ctrl = {};
        v4l2_queryctrl query_ctrl = {};
        bool is_auto = false;
        bool support_auto = false;

        uint32_t id = to_v4l2_type(type, false);
        if (id == UINT32_MAX)
            return {};

        if (auto auto_id = auto_flag_for(type))
        {
            v4l2_control auto_ctrl{};
            auto_ctrl.id = *auto_id;
            
            if (sys_ioctl(m_device_fd, VIDIOC_G_CTRL, &auto_ctrl) == 0)
            {
                support_auto = true;
                is_auto = (auto_ctrl.value != 0);
                if (*auto_id == V4L2_CID_EXPOSURE_AUTO)
                    is_auto = (auto_ctrl.value != V4L2_EXPOSURE_MANUAL);
            }
        }

        ctrl.id = query_ctrl.id = static_cast<uint32_t>(id);
        if (sys_ioctl(m_device_fd, VIDIOC_G_CTRL, &ctrl) < 0)
        {
            RWC_LOG_ERROR("Failed to get property value (id={}, name={}) on VIDIOC_G_CTRL (errno={}, errno_str=\"{}\")", 
                            uint32_t(type), webcam_utils::prop_type_to_string(type), errno, strerror(errno));
            return {};        
        }

        if (sys_ioctl(m_device_fd, VIDIOC_QUERYCTRL, &query_ctrl) < 0)
        {
            RWC_LOG_ERROR("Failed to get property limits (id={}, name=\"{}\") on VIDIOC_QUERYCTRL (errno={}, errno_str=\"{}\")",
                            uint32_t(type), webcam_utils::prop_type_to_string(type), errno, strerror(errno));
            return {};
        }

        int32_t value = ctrl.value;
        if (ctrl.id == V4L2_CID_EXPOSURE_AUTO)
            value = (ctrl.value == V4L2_EXPOSURE_MANUAL) ? 0 : 1;

        wc_ctrl_prop.type = type;
        wc_ctrl_prop.value = value;
        wc_ctrl_prop.step = query_ctrl.step;
        wc_ctrl_prop.minimum = query_ctrl.minimum;
        wc_ctrl_prop.maximum = query_ctrl.maximum;
        wc_ctrl_prop.default_value = query_ctrl.default_value;
        wc_ctrl_prop.is_auto = is_auto;
        wc_ctrl_prop.support_auto = support_auto;

        return wc_ctrl_prop;
    }

    bool v4l2_webcam_controller::write_property(webcam_property_type type, int32_t value, bool auto_prop)
    {
        v4l2_control ctrl = {};
        uint32_t id = to_v4l2_type(type, auto_prop);
        
        if (id == UINT32_MAX)
            return false;

        ctrl.id = id;
        ctrl.value = value;

        if (sys_ioctl(m_device_fd, VIDIOC_S_CTRL, &ctrl) < 0)
        {
            RWC_LOG_ERROR("Failed to set property value (id={}, name={}) to [{}] on VIDIOC_S_CTRL (errno={}, errno_str=\"{}\")", 
                            uint32_t(type), webcam_utils::prop_type_to_string(type), value, errno, strerror(errno));
            return false;
        }

        return true;
    }

    bool v4l2_webcam_controller::write_property_default(webcam_property_type type)
    {
        auto prop = read_property(type);
        if (!prop)
            return false;

        return write_property(type, prop->default_value, prop->support_auto);
    }

    void v4l2_webcam_controller::reset_properties()
    {
        for (uint32_t prop_index{}; prop_index != std::to_underlying(webcam_property_type::last); ++prop_index)
            write_property_default(static_cast<webcam_property_type>(prop_index));
    }
}


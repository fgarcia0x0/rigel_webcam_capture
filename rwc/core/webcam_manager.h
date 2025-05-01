#pragma once

#include <rwc/core/webcam_device.hpp>
#include <memory>

namespace rwc
{
    struct webcam_manager
    {
        static uint32_t device_count() noexcept;
        static std::shared_ptr<webcam_device> create_device();
    };
}

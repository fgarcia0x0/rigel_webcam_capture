#include <rwc/core/webcam_manager.h>
#include <rwc/platform/platform.hpp>

#ifdef RWC_PLATFORM_LINUX
    #include <rwc/platform/linux/v4l2_webcam_device.h>
#endif

namespace rwc
{
    std::shared_ptr<webcam_device> webcam_manager::create_device()
    {
    #ifdef RWC_PLATFORM_LINUX
        return std::make_shared<v4l2_webcam_device>();
    #endif

        return nullptr;
    }

    uint32_t webcam_manager::device_count() noexcept
    {
    #ifdef RWC_PLATFORM_LINUX
        return v4l2_webcam_device::device_count();
    #else
        return 0;
    #endif
    }
}
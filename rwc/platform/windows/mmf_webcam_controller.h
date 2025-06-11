#pragma once

#include <rwc/core/webcam_device.hpp>
#include <rwc/platform/platform.hpp>

#include <wrl/client.h>

#include <memory>
#include <optional>

namespace rwc
{
    class mmf_webcam_controller : public webcam_controller
    {
    public:
        mmf_webcam_controller();

        mmf_webcam_controller(const mmf_webcam_controller&) = delete;
        mmf_webcam_controller& operator=(const mmf_webcam_controller&) = delete;
        mmf_webcam_controller(mmf_webcam_controller&&) = delete;
        mmf_webcam_controller& operator=(mmf_webcam_controller&&) = delete;

        bool load(void* mf_source_reader);

        // Ctrl Operations
        std::optional<webcam_ctrl_property> read_property(webcam_property_type type) override;
        bool write_property(webcam_property_type type, int32_t value, bool auto_prop) override;
        bool write_property_default(webcam_property_type type) override;
        void reset_properties() override;

        virtual ~mmf_webcam_controller();

    private:
        struct context;
        std::unique_ptr<context> m_context;
    };
}

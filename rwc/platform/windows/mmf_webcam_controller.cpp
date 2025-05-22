#include <rwc/platform/windows/mmf_webcam_controller.h>
#include <rwc/logger/logger.h>

#include <initguid.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <wrl/client.h>

#include <utility>

using Microsoft::WRL::ComPtr;

// We need define IksControl GUID here to avoid using IID_PPV_ARGS because it's use __uuidof() that is a lang extension
DEFINE_GUID(IID_IKsControl, 0x28f54685, 0x6fd, 0x11d2, 0xb2, 0x7a, 0x0, 0xa0, 0xc9, 0x22, 0x31, 0x96);

// Mingw cannot find definition of these guids, so we will define them here
DEFINE_GUID(PROPSETID_VIDCAP_CAMERACONTROL_WRAPPER, 0xc6e13370, 0x30ac, 0x11d0, 0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56);
DEFINE_GUID(PROPSETID_VIDCAP_VIDEOPROCAMP_WRAPPER, 0xc6e13360, 0x30ac, 0x11d0, 0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56);

namespace rwc
{
    struct KsControlMemberList
    {
        KSPROPERTY_DESCRIPTION desc;
        KSPROPERTY_MEMBERSHEADER hdr;
        KSPROPERTY_STEPPING_LONG step;
    };

    struct KsControlDefaultValue
    {
        KSPROPERTY_DESCRIPTION desc;
        KSPROPERTY_MEMBERSHEADER hdr;
        LONG value;
    };

    static inline std::optional<uint32_t> map_webcam_prop_type(webcam_property_type type, GUID* out_id)
    {
        *out_id = PROPSETID_VIDCAP_VIDEOPROCAMP_WRAPPER;

        switch (type) 
        {
            case webcam_property_type::exposure:
                *out_id = PROPSETID_VIDCAP_CAMERACONTROL_WRAPPER;
                return KSPROPERTY_CAMERACONTROL_EXPOSURE;
            case webcam_property_type::auto_exposure:
                *out_id = PROPSETID_VIDCAP_CAMERACONTROL_WRAPPER;
                return KSPROPERTY_CAMERACONTROL_EXPOSURE;
            case webcam_property_type::focus:
                *out_id = PROPSETID_VIDCAP_CAMERACONTROL_WRAPPER;
                return KSPROPERTY_CAMERACONTROL_FOCUS;
            case webcam_property_type::auto_focus:
                *out_id = PROPSETID_VIDCAP_CAMERACONTROL_WRAPPER;
                return KSPROPERTY_CAMERACONTROL_FOCUS;
            case webcam_property_type::zoom:
                *out_id = PROPSETID_VIDCAP_CAMERACONTROL_WRAPPER;
                return KSPROPERTY_CAMERACONTROL_ZOOM;
            case webcam_property_type::white_balance:
                return KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE;
            case webcam_property_type::auto_white_balance:
                return KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE;
            case webcam_property_type::gain:
                return KSPROPERTY_VIDEOPROCAMP_GAIN;
            case webcam_property_type::auto_gain:
                return KSPROPERTY_VIDEOPROCAMP_GAIN;
            case webcam_property_type::brightness:
                return KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS;
            case webcam_property_type::contrast:
                return KSPROPERTY_VIDEOPROCAMP_CONTRAST;
            case webcam_property_type::saturation:
                return KSPROPERTY_VIDEOPROCAMP_SATURATION;
            case webcam_property_type::gamma:
                return KSPROPERTY_VIDEOPROCAMP_GAMMA;
            case webcam_property_type::hue:
                return KSPROPERTY_VIDEOPROCAMP_HUE;
            case webcam_property_type::sharpness:
                return KSPROPERTY_VIDEOPROCAMP_SHARPNESS;
            case webcam_property_type::back_light_comp:
                return KSPROPERTY_VIDEOPROCAMP_BACKLIGHT_COMPENSATION;
            case webcam_property_type::power_line_freq:
                return KSPROPERTY_VIDEOPROCAMP_POWERLINE_FREQUENCY;
            case webcam_property_type::last:
                break;
            default:
                break;
        }

        return std::nullopt;
    }

    struct mmf_webcam_controller::context
    {
        ComPtr<IKsControl> iks_ctrl;
    };

    mmf_webcam_controller::mmf_webcam_controller()
        : m_context{ std::make_unique<context>() }
    {
    }

    bool mmf_webcam_controller::load(void* mf_source_reader)
    {
        auto src_reader = reinterpret_cast<IMFSourceReader *>(mf_source_reader);
        HRESULT hr = src_reader->GetServiceForStream(DWORD(MF_SOURCE_READER_MEDIASOURCE), GUID_NULL, IID_IKsControl, &m_context->iks_ctrl);
        
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("Failed to get IKsControl from IMFSourceReader (hr={:08x})", hr);
            return false;
        }

        return true;
    }
    
    std::optional<webcam_ctrl_property> mmf_webcam_controller::read_property(webcam_property_type type)
    {
        HRESULT hr = S_OK;
        GUID prop_set = {};
        auto prop_id = map_webcam_prop_type(type, &prop_set);
        if (!prop_id)
            return std::nullopt;

        KsControlMemberList ks_mem_list = {};
        KsControlDefaultValue ks_default_value = {};
        KSPROPERTY_CAMERACONTROL_S ks_prop = {};
        ULONG ret_code = 0;

        ks_prop.Property.Set = prop_set;
        ks_prop.Property.Id = prop_id.value();
        ks_prop.Property.Flags = KSPROPERTY_TYPE_BASICSUPPORT;

        PKSPROPERTY pks_prop = reinterpret_cast<PKSPROPERTY>(&ks_prop);
        auto& iks_ctrl = m_context->iks_ctrl;

        hr = iks_ctrl->KsProperty(pks_prop, sizeof(ks_prop), &ks_mem_list, sizeof(ks_mem_list), &ret_code);
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("Failed to get control range for {} (hr={:08x})", std::to_underlying(type), hr);
            return std::nullopt;
        }

        ks_prop.Property.Flags = KSPROPERTY_TYPE_DEFAULTVALUES;
        hr = iks_ctrl->KsProperty(pks_prop, sizeof(ks_prop), &ks_default_value, sizeof(ks_default_value), &ret_code);
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("Failed to get control default values (hr={:08x})", hr);
            return std::nullopt;
        }

        ks_prop.Property.Flags = KSPROPERTY_TYPE_GET;
        ks_prop.Value = -1;

        hr = iks_ctrl->KsProperty(pks_prop, sizeof(ks_prop), &ks_prop, sizeof(ks_prop), &ret_code);
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("Failed to get control value (hr={:08x})", hr);
            return std::nullopt;
        }

        webcam_ctrl_property result = {
            .type = type,
            .value = ks_prop.Value,
            .step = static_cast<int32_t>(ks_mem_list.step.SteppingDelta),
            .minimum = static_cast<int32_t>(ks_mem_list.step.Bounds.SignedMinimum),
            .maximum = static_cast<int32_t>(ks_mem_list.step.Bounds.SignedMaximum),
            .default_value = ks_default_value.value,
            .is_auto = !!(ks_prop.Flags & KSPROPERTY_CAMERACONTROL_FLAGS_AUTO),
            .unused = 0
        };

        [[maybe_unused]]
        bool prop_support_auto = !!(ks_prop.Capabilities & KSPROPERTY_CAMERACONTROL_FLAGS_AUTO);

        return result;
    }
    
    bool mmf_webcam_controller::write_property(webcam_property_type type, int32_t value)
    {
        HRESULT hr = S_OK;
        GUID prop_set = {};
        auto prop_id = map_webcam_prop_type(type, &prop_set);
        if (!prop_id)
            return false;

        KSPROPERTY_CAMERACONTROL_S ks_prop = {};
        PKSPROPERTY pks_prop = reinterpret_cast<PKSPROPERTY>(&ks_prop);
        ULONG ret_code = 0;

        ks_prop.Property.Set = prop_set;
        ks_prop.Property.Id = prop_id.value();
        ks_prop.Property.Flags = KSPROPERTY_TYPE_SET;
        ks_prop.Value = value;

        switch (type) 
        {
            case webcam_property_type::auto_exposure:
            case webcam_property_type::auto_focus:
            case webcam_property_type::auto_white_balance:
            case webcam_property_type::auto_gain:
                ks_prop.Flags = KSPROPERTY_CAMERACONTROL_FLAGS_AUTO;
                break;
            default:
                ks_prop.Flags = KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
                break;
        }

        hr = m_context->iks_ctrl->KsProperty(pks_prop, sizeof(ks_prop), &ks_prop, sizeof(ks_prop), &ret_code);
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("Failed to set control value to \"{}\" (hr={:08x})", value, hr);
            return false;
        }

        return true;
    }
    
    bool mmf_webcam_controller::write_property_default(webcam_property_type type)
    {
        auto prop = read_property(type);
        if (!prop)
            return false;

        return write_property(type, prop->default_value);
    }
    
    void mmf_webcam_controller::reset_properties()
    {
        for (uint32_t prop_index{}; prop_index != std::to_underlying(webcam_property_type::last); ++prop_index)
            write_property_default(static_cast<webcam_property_type>(prop_index));
    }
    
    mmf_webcam_controller::~mmf_webcam_controller() = default;
}

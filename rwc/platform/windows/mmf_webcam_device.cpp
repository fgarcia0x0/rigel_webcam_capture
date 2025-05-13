#include <rwc/core/webcam_device.hpp>
#include <rwc/core/webcam_utils.h>
#include <rwc/logger/logger.h>
#include <rwc/utils/scope_exit.hpp>
#include <rwc/utils/scoped_timer.hpp>
#include <rwc/utils/utils.hpp>
#include <rwc/platform/windows/mmf_webcam_device.h>

#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <stringapiset.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#include <wmcodecdsp.h> // For CLSID_CColorConvertDMO

#include <algorithm>
#include <vector>
#include <functional>
#include <cstdint>
#include <chrono>
#include <string.h>

using Microsoft::WRL::ComPtr;

static auto RWC_DEFAULT_VIDEO_DECODING_FORMAT{ MFVideoFormat_YUY2 };

namespace rwc
{
    struct mmf_webcam_device::context
    {
        ComPtr<IMFAttributes> attributes{};
        ComPtr<IMFMediaSource> media_source{};
        ComPtr<IMFSourceReader> source_reader{};
        ComPtr<ID3D11Device> d3d11_device{};
        ComPtr<IMFDXGIDeviceManager> dxgi_manager{};
        bool com_init{ false };

        IMFActivate** devices{};
        uint32_t device_count{};
        uint32_t device_index{};

        ~context()
        {
            if (media_source)
                media_source->Shutdown();

            if (devices) 
            {
                for (uint32_t i = 0; i < device_count; ++i)
                    devices[i]->Release();

                CoTaskMemFree(devices);
                devices = {};
            }

            MFShutdown();

            if (com_init)
                CoUninitialize();
        }
    };

    static inline void bgr_to_rgb(uint8_t* rgb, uint32_t size)
    {
        constexpr uint32_t bytes_per_pixel{ 3 };

        for (uint32_t i = 0; i < size; i += bytes_per_pixel) 
        {
            std::swap(rgb[i], rgb[i + bytes_per_pixel - 1]);
        }
    }

    static inline std::string fourcc_from_guid(const GUID& guid) 
    {
        char fourcc[sizeof(guid.Data1) + 1] = {};
        memcpy(fourcc, &guid.Data1, sizeof(guid.Data1));
        return std::string(fourcc);
    }

    static inline std::string wide_to_str(const wchar_t* wide_str)
    {
        if (!wide_str)
            return {};

        // Get the required buffer size
        int buffer_size = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, nullptr, 0, nullptr, nullptr);
        if (buffer_size == 0)
            return {};

        std::string result(buffer_size, 0);

        // Convert the string
        int chars_conv = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, result.data(), buffer_size, nullptr, nullptr);
        if (chars_conv == 0)
            return {};

        // Remove null terminator at the end
        result.resize(chars_conv - 1);

        return result;
    }

    mmf_webcam_device::mmf_webcam_device()
    {
    }

    webcam_error_status mmf_webcam_device::open(uint32_t index)
    {
        if (m_opened)
            return webcam_error_status::device_already_opened;

        if (m_streaming)
            return webcam_error_status::invalid_state;

        auto context = std::make_unique<mmf_webcam_device::context>();
        if (!context)
            return webcam_error_status::memory_exhausted;

        context->device_index = index;
        auto status = webcam_error_status::cannot_initialize_device;
        HRESULT hr = S_OK;

        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(hr) && hr != S_FALSE)
            return status;

        if (hr != S_FALSE)
            context->com_init = true;

        hr = MFStartup(MF_VERSION);
        if (FAILED(hr))
            return status;

        hr = MFCreateAttributes(&context->attributes, 4);
        if (FAILED(hr)) 
            return status;

        if (!try_enable_hardware_decoding(context->attributes.Get(), &context->d3d11_device, &context->dxgi_manager))
        {
            RWC_LOG_WARN("Cannot enable hardware video decoding");
        }

        hr = context->attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (FAILED(hr))
            return status;

        hr = MFEnumDeviceSources(context->attributes.Get(), &context->devices, &context->device_count);
        if (FAILED(hr) || context->device_count == 0) 
            return status;

        if (index >= context->device_count)
            return status;

        hr = context->devices[index]->ActivateObject(IID_PPV_ARGS(&context->media_source));
        if (FAILED(hr)) 
            return status;

        hr = MFCreateSourceReaderFromMediaSource(context->media_source.Get(), nullptr, &context->source_reader);
        if (FAILED(hr)) 
            return status;

        auto device_info = read_device_info(context->device_index, context->devices[index], context->source_reader.Get());
        if (!device_info)
            return webcam_error_status::query_capability_failed;

        m_context = std::move(context);
        m_device_info = std::move(device_info).value();
        m_opened = true;

        // select best format if not set
        if (!m_current_format.width || !m_current_format.height)
        {
            auto best_fmt = webcam_utils::select_capture_format(m_device_info.formats, RWC_WEBCAM_CODEC_TYPE_DEFAULT);
            m_current_format = best_fmt.value_or(m_device_info.formats[0]);
        }

        return webcam_error_status::ok;
    }
    
    webcam_error_status mmf_webcam_device::reset(uint32_t index)
    {
        close();
        return open(index);
    }
    
    void mmf_webcam_device::close()
    {
        if (m_streaming)
            stop_stream();

        if (m_opened)
            m_context.reset();

        m_last_frame_status = webcam_error_status::ok;
        m_frame_queue.clear();
        m_opened = false;
    }
    
    bool mmf_webcam_device::is_opened() const 
    {
        return m_opened;
    }

    const webcam_device_info& mmf_webcam_device::device_info() const
    {
        return m_device_info;
    }
    
    capture_format_info mmf_webcam_device::current_format() noexcept
    {
        return m_current_format;
    }

    bool mmf_webcam_device::set_current_format(const capture_format_info& format)
    {
        if (m_streaming)
            return false;

        const auto& formats = m_device_info.formats;
        if (std::find(formats.cbegin(), formats.cend(), format) == formats.cend())
            return false;

        m_current_format = format;
        return true;
    }
    
    bool mmf_webcam_device::set_current_format_by_index(uint32_t index) 
    {
        if (index >= m_device_info.formats.size())
            return false;

        m_current_format = m_device_info.formats[index];
        return true;
    }

    uint32_t mmf_webcam_device::device_count() noexcept
    {
        ComPtr<IMFAttributes> attribs = nullptr;
        IMFActivate** devices = nullptr;
        UINT32 count = 0;

        HRESULT hr = MFStartup(MF_VERSION);
        if (FAILED(hr) && hr != S_FALSE) 
        {
            RWC_LOG_ERROR("Failed to initialize Media Foundation.");
            return 0;
        }

        scope_exit on_exit{ [mf_init = hr != S_FALSE, &devices, &count]()
        {
            // Cleanup
            for (UINT32 i = 0; i < count; ++i) 
                devices[i]->Release();

            CoTaskMemFree(devices);

            if (mf_init)
                MFShutdown();
        }};
    
        // Create an attribute store to specify enumeration parameters.
        hr = MFCreateAttributes(&attribs, 1);
        if (SUCCEEDED(hr)) 
        {
            // Request video capture devices.
            hr = attribs->SetGUID(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
            );

            // Enumerate devices.
            if (SUCCEEDED(hr))
                hr = MFEnumDeviceSources(attribs.Get(), &devices, &count);
        }

        if (FAILED(hr)) 
        {
            RWC_LOG_ERROR("Failed to enumerate video capture devices (hr={})", hr);
            return 0;
        }
    
        return count;
    }

    // Ctrl Operations
    std::optional<webcam_ctrl_property> mmf_webcam_device::get_ctrl_property(webcam_property_type type)
    {
        std::ignore = type;
        return {};
    }

    bool mmf_webcam_device::set_ctrl_property(webcam_property_type type, int32_t value)
    {
        std::ignore = type;
        std::ignore = value;
        return false;
    }
    
    bool mmf_webcam_device::set_ctrl_property_default(webcam_property_type type)
    {
        std::ignore = type;
        return false;
    }
    
    void mmf_webcam_device::reset_ctrl_properties()
    {
    }

    // Streaming Operations
    bool mmf_webcam_device::has_pending_frame() const
    {
        return !m_frame_queue.empty() || m_last_frame_status != webcam_error_status::ok;
    }

    bool mmf_webcam_device::is_streaming() const
    {
        return m_streaming;
    }

    webcam_error_status mmf_webcam_device::start_stream()
    {
        if (!is_opened() || is_streaming())
            return webcam_error_status::invalid_state;

        auto status = setup_webcam_image();
        if (status != webcam_error_status::ok)
            return status;

        // discard first N frames to avoid delay
        constexpr auto num_frames_to_discard{ 3 };
        warmup_webcam(num_frames_to_discard);

        m_streaming = true;
        m_mmf_thread = std::make_unique<std::jthread>([this](auto token){ mmf_capture_thread(token); });

        return webcam_error_status::ok;
    }

    void mmf_webcam_device::stop_stream()
    {
        if (!is_opened() || !is_streaming())
            return;

        if (m_mmf_thread)
        {
            m_mmf_thread->request_stop();
            m_mmf_thread->join();
            m_mmf_thread.reset();
        }

        m_streaming = false;
    }

    std::expected<webcam_frame_rgb24, webcam_error_status> mmf_webcam_device::read_frame()
    {
        if (!is_opened() || !is_streaming())
            return std::unexpected{ webcam_error_status::invalid_state };

        if (m_last_frame_status != webcam_error_status::ok)
            return std::unexpected{ m_last_frame_status.load() };

        if (m_frame_queue.empty())
            return std::unexpected{ webcam_error_status::frame_not_ready };

        return m_frame_queue.dequeue().value();
    }

    mmf_webcam_device::~mmf_webcam_device()
    {
        close();
    }
    
    std::optional<webcam_device_info> mmf_webcam_device::read_device_info(uint32_t device_index, void* imf_device, void* source_reader)
    {
        webcam_device_info device_info{};
        const auto symlink = get_string_attr(imf_device, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
        const auto friendly_name = get_string_attr(imf_device, &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);

        device_info.unique_id = symlink; 
        device_info.name = friendly_name;
        device_info.path = symlink;
        device_info.index = device_index;

        // read supported formats
        device_info.formats = read_webcam_formats(device_index, source_reader);
        if (device_info.formats.empty())
            return std::nullopt;

        return device_info;
    }

    static ComPtr<IMFMediaType> find_media_type(IMFSourceReader* source_reader, uint32_t device_index, const capture_format_info& target_format)
    {
        ComPtr<IMFMediaType> media_type_ptr{};

        for (DWORD index{}; SUCCEEDED(source_reader->GetNativeMediaType(device_index, index, &media_type_ptr)); ++index) 
        {
            GUID subtype = {};
            UINT32 width = 0, height = 0, num = 0, den = 0;
            capture_format_info capture_format{};

            if (SUCCEEDED(media_type_ptr->GetGUID(MF_MT_SUBTYPE, &subtype))) 
            {
                std::string codec = fourcc_from_guid(subtype);
                uint32_t codec_type = 0;
                memcpy(&codec_type, codec.data(), sizeof(uint32_t));

                // Get resolution
                UINT64 size{};
                if (SUCCEEDED(media_type_ptr->GetUINT64(MF_MT_FRAME_SIZE, &size))) 
                {
                    width = (UINT32)(size >> 32);
                    height = (UINT32)(size & 0xFFFFFFFF);
                }

                // Get frame rate
                UINT64 rate{};
                if (SUCCEEDED(media_type_ptr->GetUINT64(MF_MT_FRAME_RATE, &rate))) 
                {
                    num = (UINT32)(rate >> 32);
                    den = (UINT32)(rate & 0xFFFFFFFF);
                }

                UINT32 fps = (den != 0) ? (num / den) : 0;
                if (fps != 0)
                {
                    capture_format.width = width;
                    capture_format.height = height;
                    capture_format.fps = fps;
                    capture_format.codec = codec_type;

                    if (capture_format == target_format)
                        return media_type_ptr; // found the format
                }
            }
        }

        return nullptr;
    }
    
    std::vector<capture_format_info> mmf_webcam_device::read_webcam_formats(uint32_t device_index, void* source_reader)
    {
        static constexpr uint32_t kPreAllocFormatsCount{ 32 };
        std::vector<capture_format_info> formats;
        formats.reserve(kPreAllocFormatsCount);

        DWORD index = 0;
        ComPtr<IMFMediaType> media_type_ptr = nullptr;
        auto src_reader = reinterpret_cast<IMFSourceReader *>(source_reader);

        while (SUCCEEDED(src_reader->GetNativeMediaType(device_index, index, &media_type_ptr))) 
        {
            GUID subtype = {};
            UINT32 width = 0, height = 0, num = 0, den = 0;
            capture_format_info capture_format{};

            if (SUCCEEDED(media_type_ptr->GetGUID(MF_MT_SUBTYPE, &subtype))) 
            {
                std::string codec = fourcc_from_guid(subtype);
                uint32_t codec_type = 0;
                memcpy(&codec_type, codec.data(), sizeof(uint32_t));

                // Get resolution
                UINT64 size{};
                if (SUCCEEDED(media_type_ptr->GetUINT64(MF_MT_FRAME_SIZE, &size))) 
                {
                    width = (UINT32)(size >> 32);
                    height = (UINT32)(size & 0xFFFFFFFF);
                }

                // Get frame rate
                UINT64 rate{};
                if (SUCCEEDED(media_type_ptr->GetUINT64(MF_MT_FRAME_RATE, &rate))) 
                {
                    num = (UINT32)(rate >> 32);
                    den = (UINT32)(rate & 0xFFFFFFFF);
                }

                UINT32 fps = (den != 0) ? (num / den) : 0;
                if (fps != 0)
                {
                    capture_format.width = width;
                    capture_format.height = height;
                    capture_format.fps = fps;
                    capture_format.codec = codec_type;
                    formats.push_back(std::move(capture_format));
                }
            }

            ++index;
        }
        
        std::ranges::sort(formats, std::greater<>{}, &capture_format_info::codec);
        auto last = std::unique(formats.begin(), formats.end());
        formats.erase(last, formats.end());

        return formats;
    }
    
    webcam_error_status mmf_webcam_device::setup_webcam_image()
    {
        ComPtr<IMFMediaType> in_media_type = find_media_type(m_context->source_reader.Get(), m_context->device_index, m_current_format);

        if (!in_media_type) 
        {
            RWC_LOG_ERROR("Failed to find media type for the current format.");
            return webcam_error_status::cannot_set_image_format;
        }

        // convert to YUY2
        in_media_type->SetGUID(MF_MT_SUBTYPE, RWC_DEFAULT_VIDEO_DECODING_FORMAT);
        
        auto hr = m_context->source_reader->SetCurrentMediaType(m_context->device_index, nullptr, in_media_type.Get());
        if (FAILED(hr)) 
        {
            RWC_LOG_ERROR("Failed to set media type (hr={})", hr);
            return webcam_error_status::cannot_set_image_format;
        }

        return webcam_error_status::ok;
    }
    
    void mmf_webcam_device::warmup_webcam(uint32_t warmup_frames)
    {
        DWORD stream_index = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        uint32_t frames_discarded = 0;
        ComPtr<IMFSample> sample = nullptr;

        for (uint32_t i = 0; i < warmup_frames; ++i) 
        {
            HRESULT hr = S_OK;
            do 
            {
                hr = m_context->source_reader->ReadSample(m_context->device_index,
                                                          0,
                                                          &stream_index,
                                                          &flags,
                                                          &timestamp,
                                                          &sample);
                
                if (FAILED(hr))
                    break;

                ++frames_discarded;

            } while ((!sample && !(flags & MF_SOURCE_READERF_STREAMTICK)));
        }

        RWC_LOG_DEBUG("webcam warm-up complete. discarded {} frames", frames_discarded);
    }
    
    bool mmf_webcam_device::try_enable_hardware_decoding(void* attribs, void** d3d_device_out, void** dxgi_manager_out)
    {
        constexpr D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        ComPtr<ID3D11Device> device;
        ComPtr<IMFDXGIDeviceManager> dxgi_manager{};
        auto attributes = reinterpret_cast<IMFAttributes *>(attribs);
        HRESULT hr = S_OK;

        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 
                              (0 * D3D11_CREATE_DEVICE_SINGLETHREADED) | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                              levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr, nullptr);

        if (FAILED(hr))
            return false;

        ComPtr<ID3D11Multithread> d3d11_multithread{};
        hr = device.As<ID3D11Multithread>(&d3d11_multithread);
        if (FAILED(hr) || !d3d11_multithread)
            return false;

        d3d11_multithread->SetMultithreadProtected(TRUE);

        UINT token{};
        hr = MFCreateDXGIDeviceManager(&token, &dxgi_manager);
        if (FAILED(hr))
            return false;

        hr = dxgi_manager->ResetDevice(device.Get(), token);
        if (FAILED(hr))
            return false;

        hr = attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, dxgi_manager.Get());
        if (FAILED(hr))
            return false;

        hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (FAILED(hr))
            return false;

        hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (FAILED(hr))
            return false;

        if (d3d_device_out)
        {
            *d3d_device_out = device.Get();
            device->AddRef();
        }

        if (dxgi_manager_out)
        {
            *dxgi_manager_out = dxgi_manager.Get();
            dxgi_manager->AddRef();
        }

        return true;
    }
    
    void mmf_webcam_device::mmf_capture_thread(std::stop_token token)
    {
        while (!token.stop_requested())
        {
            DWORD actual_stream_index = 0;
            DWORD stream_flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample = nullptr;
            HRESULT hr = S_OK;

            {
                scoped_timer<std::chrono::milliseconds> sample_timer{ "Acquire Frame" };
                sample_timer.set_num_digits(2);

                hr = m_context->source_reader->ReadSample(m_context->device_index, 0, &actual_stream_index, &stream_flags, &timestamp, &sample);
                if (FAILED(hr))
                {
                    m_last_frame_status = webcam_error_status::cannot_decode_frame;
                    return;
                }
            }
            
            if (!sample || stream_flags & MF_SOURCE_READERF_STREAMTICK)
                continue;

            if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM)
                return;

            ComPtr<IMFMediaBuffer> mmf_buffer{};

            auto status = decode_frame_to_bgr24(sample.Get(), &mmf_buffer);
            if (status != webcam_error_status::ok)
            {
                m_last_frame_status = status;
                continue; // ignore this frame and go to another
            }

            // read here Samle
            DWORD bytes_read = {};

            hr = mmf_buffer->GetCurrentLength(&bytes_read);
            if (FAILED(hr) || bytes_read == 0)
            {
                m_last_frame_status = webcam_error_status::cannot_setup_buffer;
                return;
            }

            uint8_t* buffer_data = nullptr;

            std::unique_ptr<uint8_t[]> img_buffer{ new (std::nothrow) uint8_t[bytes_read] };
            if (!img_buffer)
            {
                m_last_frame_status = webcam_error_status::memory_exhausted;
                return;
            }

            mmf_buffer->Lock(&buffer_data, nullptr, &bytes_read);
            if (FAILED(hr) || !buffer_data)
            {
                m_last_frame_status = webcam_error_status::cannot_map_buffer;
                return;
            }

            memcpy(img_buffer.get(), buffer_data, bytes_read);
            mmf_buffer->Unlock();

            // convert to RGB
            bgr_to_rgb(img_buffer.get(), bytes_read);

            auto ts = rwc::utils::current_timestamp();
            webcam_frame_rgb24 frame{ m_current_format.width, m_current_format.height,
                                      bytes_read, ts, std::move(img_buffer) };

            m_frame_queue.enqueue(std::move(frame));
        }
    }

    // adjust this FUNC
    webcam_error_status mmf_webcam_device::decode_frame_to_bgr24(void* sample, void** buffer)
    {
        scoped_timer<milli_dbl> timer{ "Frame Decode" };

        HRESULT hr = S_OK;
        ComPtr<IMFTransform> color_converter{};

        hr = CoCreateInstance(
            CLSID_CColorConvertDMO,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&color_converter)
        );

        if (FAILED(hr))
        {
            RWC_LOG_ERROR("Failed to create color converter (hr={})", hr);
            return webcam_error_status::cannot_setup_decoder;
        }

        ComPtr<IMFMediaType> input_type{};
        MFCreateMediaType(&input_type);
        input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        input_type->SetGUID(MF_MT_SUBTYPE, RWC_DEFAULT_VIDEO_DECODING_FORMAT);
        MFSetAttributeSize(input_type.Get(), MF_MT_FRAME_SIZE, m_current_format.width, m_current_format.height);
        MFSetAttributeRatio(input_type.Get(), MF_MT_FRAME_RATE, m_current_format.fps, 1);

        hr = color_converter->SetInputType(0, input_type.Get(), 0);
        if (FAILED(hr))
            return webcam_error_status::cannot_setup_decoder;

        ComPtr<IMFMediaType> output_type = nullptr;
        MFCreateMediaType(&output_type);
        output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
        MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE, m_current_format.width, m_current_format.height);
        MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE, m_current_format.fps, 1);
        hr = color_converter->SetOutputType(0, output_type.Get(), 0);
        if (FAILED(hr))
            return webcam_error_status::cannot_setup_decoder;

        hr = color_converter->ProcessInput(0, reinterpret_cast<IMFSample *>(sample), 0);
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("Failed to process input (hr={})", hr);
            return webcam_error_status::cannot_setup_decoder;
        }

        MFT_OUTPUT_DATA_BUFFER out_data_buffer{};
        ComPtr<IMFSample> out_sample{};
        ComPtr<IMFMediaBuffer> out_buffer{};
        MFCreateSample(&out_sample);
        MFCreateMemoryBuffer(m_current_format.width * m_current_format.height * 3, &out_buffer);
        out_sample->AddBuffer(out_buffer.Get());

        DWORD status{};
        out_data_buffer.pSample = out_sample.Get();
        hr = color_converter->ProcessOutput(0, 1, &out_data_buffer, &status);
        if (FAILED(hr) || status != S_OK)
        {
            RWC_LOG_ERROR("Failed to process output (hr={})", hr);
            return webcam_error_status::cannot_dequeue_buffer;
        }

        if (buffer)
        {
            *buffer = out_buffer.Get();
            out_buffer->AddRef();
        }

        return webcam_error_status::ok;
    }
    
    std::string mmf_webcam_device::get_string_attr(void* imf_activate, const void* guid_key)
    {
        HRESULT hr = S_OK;
        WCHAR* name = nullptr;
        auto device = reinterpret_cast<IMFActivate *>(imf_activate);
    
        UINT32 name_length = 0;
        hr = device->GetAllocatedString(*reinterpret_cast<const GUID *>(guid_key), &name, &name_length);

        if (FAILED(hr))
            return {};
        
        std::string result = wide_to_str(name);
        CoTaskMemFree(name);

        return result;
    }
}
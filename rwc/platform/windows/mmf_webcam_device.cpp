#include <rwc/core/webcam_device.hpp>
#include <rwc/core/webcam_utils.h>
#include <rwc/logger/logger.h>
#include <rwc/utils/scope_exit.hpp>
#include <rwc/utils/scoped_timer.hpp>
#include <rwc/utils/utils.hpp>
#include <rwc/platform/windows/mmf_webcam_device.h>
#include <rwc/platform/windows/staging_texture_pool.hpp>
#include <rwc/platform/windows/video_buffer_lock.hpp>

#include <mfapi.h>
#include <mfplay.h>
#include <mfidl.h>
#include <mfmediaengine.h>
#include <mfreadwrite.h>
#include <stringapiset.h>
#include <d3d11_4.h>
#include <strmif.h>
#include <wrl/client.h>
#include <wmcodecdsp.h> // For CLSID_CColorConvertDMO

#include <algorithm>
#include <vector>
#include <functional>
#include <cstdint>
#include <thread>
#include <utility>
#include <cstring>
#include <memory>

using Microsoft::WRL::ComPtr;

static auto RWC_DEFAULT_VIDEO_DECODING_FORMAT{ MFVideoFormat_NV12 };
static constexpr auto RWC_STAGING_TEXTURE_POOL_SIZE{ 2u };
static constexpr auto RWC_WEBCAM_WARMUP_FRAMECOUNT{ 2u };
static constexpr auto RWC_VIDEO_STREAM_INDEX = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

namespace rwc
{
    struct mmf_hwd_ctx
    {
        ComPtr<ID3D11Device> d3d11_device{};
        ComPtr<IMFDXGIDeviceManager> dxgi_manager{};
        ComPtr<ID3D11DeviceContext> d3d11_context{};
        ComPtr<ID3D11Texture2D> input_tex{};
        ComPtr<ID3D11Texture2D> output_tex{};
        ComPtr<ID3D11VideoDevice> video_device{};
        ComPtr<ID3D11VideoContext> video_ctx{};
        ComPtr<ID3D11VideoProcessorOutputView> output_view{};
        ComPtr<ID3D11VideoProcessorInputView> input_view{};
        ComPtr<ID3D11VideoProcessorEnumerator> vp_enum{};
        ComPtr<ID3D11VideoProcessor> video_processor{};
        staging_texture_pool staging_tex_pool{};
    };

    struct mmf_webcam_device::context
    {
        ComPtr<IMFAttributes> attributes{};
        ComPtr<IMFMediaSource> media_source{};
        ComPtr<IMFSourceReader> source_reader{};
        std::unique_ptr<mmf_hwd_ctx> hwd_ctx{};

        bool has_hwd_acc{ true };

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
        }
    };

    [[maybe_unused]]
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

        std::string result(size_t(buffer_size), 0);

        // Convert the string
        int chars_conv = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, result.data(), buffer_size, nullptr, nullptr);
        if (chars_conv == 0)
            return {};

        // Remove null terminator at the end
        result.resize(size_t(chars_conv - 1));

        return result;
    }

    static inline ComPtr<ID3D11Texture2D> create_texture(uint32_t width, uint32_t height, DXGI_FORMAT format,
                                                         uint32_t bind_flags, ID3D11Device* device)
    {
        // Create input texture
        D3D11_TEXTURE2D_DESC tex_desc = {};
        tex_desc.Width = width;
        tex_desc.Height = height;
        tex_desc.MipLevels = 1;
        tex_desc.ArraySize = 1;
        tex_desc.Format = format;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Usage = D3D11_USAGE_DEFAULT;
        tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | bind_flags;

        ComPtr<ID3D11Texture2D> tex;
        if (SUCCEEDED(device->CreateTexture2D(&tex_desc, nullptr, &tex)))
            return tex;
        
        return nullptr;
    }

    static inline void rgba_to_rgb(const uint8_t* input, uint32_t input_stride,
                                   uint8_t* output, uint32_t output_stride,
                                   uint32_t width, uint32_t height)
    {
        scoped_timer<milli_dbl> timer{ "rgba_to_rgb" };
        constexpr uint32_t rgba_bytes_per_pixel{ 4 };
        constexpr uint32_t rgb_bytes_per_pixel{ 3 };

        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t* input_row = input + y * input_stride;
            uint8_t* output_row = output + y * output_stride;
            uint32_t x = 0;

            // Process 4 pixels per iteration (loop unrolling)
            for (; x + rgb_bytes_per_pixel < width; x += rgba_bytes_per_pixel)
            {
                for (uint32_t i = 0; i < rgba_bytes_per_pixel; ++i)
                {
                    const uint8_t* pixel = input_row + (x + i) * rgba_bytes_per_pixel;
                    uint8_t* out = output_row + (x + i) * rgb_bytes_per_pixel;
                    memcpy(out, pixel, rgb_bytes_per_pixel);
                }
            }

            // Remaining pixels
            for (; x < width; ++x)
            {
                const uint8_t* pixel = input_row + x * rgba_bytes_per_pixel;
                uint8_t* out = output_row + x * rgb_bytes_per_pixel;
                memcpy(out, pixel, rgb_bytes_per_pixel);
            }
        }
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

        auto hwd_context = std::make_unique<mmf_hwd_ctx>();
        if (!hwd_context)
            return webcam_error_status::memory_exhausted;

        context->device_index = index;
        auto status = webcam_error_status::cannot_initialize_device;
        HRESULT hr = S_OK;

        if (!m_com_initialized)
        {
            hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(hr) && hr != S_FALSE)
                return status;

            hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
            if (FAILED(hr))
                return status;

            m_com_initialized = true;
        }

        hr = MFCreateAttributes(&context->attributes, 4);
        if (FAILED(hr)) 
            return status;

        if (!try_enable_hardware_decoding(context->attributes.Get(), &hwd_context->d3d11_device, &hwd_context->d3d11_context, &hwd_context->dxgi_manager))
        {
            // no hardware acceleration
            RWC_LOG_WARN("Cannot enable hardware video decoding");
            context->has_hwd_acc = false;
            hr = context->attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        }
        else 
        {
            RWC_LOG_WARN("Enabled hardware video decoding");
        }

        hr = context->attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (FAILED(hr))
            return status;

        hr = MFEnumDeviceSources(context->attributes.Get(), &context->devices, &context->device_count);
        if (FAILED(hr) || context->device_count == 0) 
            return status;

        if (index >= context->device_count)
            return status;

        hr = context->devices[index]->ActivateObject(IID_IMFMediaSource, &context->media_source);
        if (FAILED(hr)) 
            return status;

        hr = MFCreateSourceReaderFromMediaSource(context->media_source.Get(), nullptr, &context->source_reader);
        if (FAILED(hr)) 
            return status;

        if (context->has_hwd_acc)
        {
            ComPtr<IMFMediaSourceEx> media_source_ex;
            hr = context->media_source.As(&media_source_ex);
            if (SUCCEEDED(hr))
                media_source_ex->SetD3DManager(hwd_context->dxgi_manager.Get());
        }

        auto device_info = read_device_info(context->device_index, context->devices[index], context->source_reader.Get());
        if (!device_info)
            return webcam_error_status::query_capability_failed;

        if (context->has_hwd_acc)
            context->hwd_ctx = std::move(hwd_context);

        m_context = std::move(context);
        m_device_info = std::move(device_info).value();

        m_controller = std::make_unique<mmf_webcam_controller>();
        m_controller->load(m_context->source_reader.Get());
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
        {
            // m_controller must be reset before m_context, because IksControl depends of MediaSource
            m_controller.reset();
            m_context.reset();
        }

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
        if (m_streaming)
            return false;

        if (index >= m_device_info.formats.size())
            return false;

        m_current_format = m_device_info.formats[index];
        return true;
    }
    
    bool mmf_webcam_device::set_preferred_decode_backend(hwd_decode_backend backend)
    {
        m_preferred_backend = backend;
        return true;
    }

    uint32_t mmf_webcam_device::device_count() noexcept
    {
        ComPtr<IMFAttributes> attribs = nullptr;
        IMFActivate** devices = nullptr;
        UINT32 count = 0;

        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
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
    
    webcam_controller* mmf_webcam_device::ctrl() noexcept 
    {
        if (!is_opened())
            return nullptr;
        
        return m_controller.get();
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
        warmup_webcam(RWC_WEBCAM_WARMUP_FRAMECOUNT);

        // create gpu pipeline
        if (m_context->has_hwd_acc && !try_create_gpu_pipeline())
            return  webcam_error_status::cannot_setup_gpu;

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

        if (m_com_initialized)
        {
            MFShutdown();
            CoUninitialize();
        }
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
        device_info.formats = read_webcam_formats(source_reader);
        if (device_info.formats.empty())
            return std::nullopt;

        return device_info;
    }

    static inline void read_media_types(IMFSourceReader* source_reader, auto&& read_callback)
    {
        ComPtr<IMFMediaType> media_type_ptr{};

        for (DWORD index{}; SUCCEEDED(source_reader->GetNativeMediaType(RWC_VIDEO_STREAM_INDEX, index, &media_type_ptr)); ++index) 
        {
            GUID subtype = {};
            uint32_t width = 0, height = 0, num = 0, den = 0;
            capture_format_info capture_format{};

            if (SUCCEEDED(media_type_ptr->GetGUID(MF_MT_SUBTYPE, &subtype))) 
            {
                // Get resolution
                if (SUCCEEDED(MFGetAttributeRatio(media_type_ptr.Get(), MF_MT_FRAME_SIZE, &width, &height)))
                {
                    if (SUCCEEDED(MFGetAttributeRatio(media_type_ptr.Get(), MF_MT_FRAME_RATE, &num, &den)))
                    {
                        if (den != 0)
                        {
                            capture_format.width = width;
                            capture_format.height = height;
                            capture_format.fps = num / den;
                            capture_format.codec = subtype.Data1;

                            if (!read_callback(capture_format, media_type_ptr))
                                return;
                        }
                    }
                }
            }
        }
    }

    static ComPtr<IMFMediaType> find_media_type(IMFSourceReader* source_reader, const capture_format_info& target_format)
    {
        ComPtr<IMFMediaType> out_media_type;

        read_media_types(source_reader, [&out_media_type, &target_format](const auto& src_format, const auto& media_type) 
        {
            if (src_format == target_format)
            {
                out_media_type = media_type;
                return false; // break
            }
            
            return true;
        });

        return out_media_type;
    }
    
    std::vector<capture_format_info> mmf_webcam_device::read_webcam_formats(void* source_reader)
    {
        static constexpr uint32_t kPreAllocFormatsCount{ 32 };
        std::vector<capture_format_info> formats;
        formats.reserve(kPreAllocFormatsCount);

        auto src_reader = reinterpret_cast<IMFSourceReader *>(source_reader);

        // read all medias types
        read_media_types(src_reader, [&formats](auto format, const auto& /*media_type=*/) {
            formats.push_back(std::move(format));
            return true;
        });
        
        std::ranges::sort(formats, std::greater<>{}, &capture_format_info::codec);
        auto last = std::unique(formats.begin(), formats.end());
        formats.erase(last, formats.end());

        return formats;
    }
    
    webcam_error_status mmf_webcam_device::setup_webcam_image()
    {
        ComPtr<IMFMediaType> in_media_type = nullptr;

        // we need to search NV12 codecs in order to get zero-copy optimization
        if (m_context->has_hwd_acc)
        {
            capture_format_info target_format = m_current_format;
            target_format.codec = RWC_WEBCAM_CODEC_TYPE_NV12;
            in_media_type = find_media_type(m_context->source_reader.Get(), target_format);
        }

        // if NV12 not exists or hardware accel is off, fallback
        if (!in_media_type)
        {
            RWC_LOG_WARN("Hardware device cannot do zero copy optimization with NV12");
            in_media_type = find_media_type(m_context->source_reader.Get(), m_current_format);
            in_media_type->SetGUID(MF_MT_SUBTYPE, RWC_DEFAULT_VIDEO_DECODING_FORMAT);
        }

        if (!in_media_type) 
        {
            RWC_LOG_ERROR("Failed to find media type for the current format.");
            return webcam_error_status::cannot_set_image_format;
        }

        auto hr = m_context->source_reader->SetCurrentMediaType(RWC_VIDEO_STREAM_INDEX, nullptr, in_media_type.Get());
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

                RWC_LOG_INFO("hr = {}", hr);
                RWC_LOG_INFO("sample = {}", sample ? "true" : "false");
                RWC_LOG_INFO("flags = {}", flags);

                ++frames_discarded;

            } while ((!sample && !(flags & MF_SOURCE_READERF_STREAMTICK)));
        }

        RWC_LOG_DEBUG("webcam warm-up complete. discarded {} frames", frames_discarded);
    }
    
    bool mmf_webcam_device::try_enable_hardware_decoding(void* attribs, void** d3d_device_out, void** d3d_device_ctx_out, void** dxgi_manager_out)
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
        {
            RWC_LOG_ERROR("D3D11CreateDevice");
            return false;
        }

        ComPtr<ID3D11Multithread> d3d11_multithread{};
        hr = device.As<ID3D11Multithread>(&d3d11_multithread);
        if (FAILED(hr) || !d3d11_multithread)
        {
            RWC_LOG_ERROR("ID3D11Multithread");
            return false;
        }

        d3d11_multithread->SetMultithreadProtected(TRUE);

        ComPtr<ID3D11DeviceContext> dev_ctx;
        device->GetImmediateContext(&dev_ctx);

        UINT token{};
        hr = MFCreateDXGIDeviceManager(&token, &dxgi_manager);
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("MFCreateDXGIDeviceManager");
            return false;
        }

        hr = dxgi_manager->ResetDevice(device.Get(), token);
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("ResetDevice");
            return false;
        }

        hr = attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, dxgi_manager.Get());
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("MF_SOURCE_READER_D3D_MANAGER");
            return false;
        }

        hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS");
            return false;
        }

        hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (FAILED(hr))
        {
            RWC_LOG_ERROR("MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING");
            return false;
        }

        if (d3d_device_out)
            *d3d_device_out = device.Detach();

        if (d3d_device_ctx_out)
            *d3d_device_ctx_out = dev_ctx.Detach();

        if (dxgi_manager_out)
            *dxgi_manager_out = dxgi_manager.Detach();

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
                scoped_timer<milli_dbl> sample_timer{ "Acquire Frame" };
                sample_timer.set_num_digits(2);

                hr = m_context->source_reader->ReadSample(RWC_VIDEO_STREAM_INDEX, 0, &actual_stream_index, &stream_flags, &timestamp, &sample);
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

            // try allocate a buffer to the image
            uint32_t image_size = m_current_format.width * m_current_format.height * 3;
            std::unique_ptr<uint8_t[]> img_buffer{ new (std::nothrow) uint8_t[image_size] };
            if (!img_buffer)
            {
                m_last_frame_status = webcam_error_status::memory_exhausted;
                return;
            }
           
            auto status = decode_frame_to_bgr24(sample.Get(), img_buffer.get());
            if (status != webcam_error_status::ok)
            {
                if (status == webcam_error_status::frame_not_ready)
                    continue;
                
                m_last_frame_status = status;

                // TODO(garcia): Create retries for frame decoding
                return;
            }

            auto ts = rwc::utils::current_timestamp();
            webcam_frame_rgb24 frame{ m_current_format.width, m_current_format.height,
                                      image_size, ts, std::move(img_buffer) };

            m_frame_queue.enqueue(std::move(frame));
        }
    }
    
    bool mmf_webcam_device::try_create_gpu_pipeline()
    {
        auto& device = m_context->hwd_ctx->d3d11_device;
        auto& device_ctx = m_context->hwd_ctx->d3d11_context;
        auto& input_tex =  m_context->hwd_ctx->input_tex;
        auto& output_tex =  m_context->hwd_ctx->output_tex;
        auto& staging_tex_pool = m_context->hwd_ctx->staging_tex_pool;
        auto& video_device = m_context->hwd_ctx->video_device;
        auto& video_ctx = m_context->hwd_ctx->video_ctx;
        auto& video_processor = m_context->hwd_ctx->video_processor;
        auto& vp_enum = m_context->hwd_ctx->vp_enum;

        constexpr DXGI_FORMAT output_format = DXGI_FORMAT_R8G8B8A8_UNORM;

        input_tex = create_texture(m_current_format.width, m_current_format.height, 
                                   DXGI_FORMAT_NV12, D3D11_BIND_DECODER, device.Get());
        if (!input_tex)
            return false;

        output_tex = create_texture(m_current_format.width, m_current_format.height, 
                                    output_format, D3D11_BIND_RENDER_TARGET, 
                                    device.Get());
        if (!output_tex)
            return false;

        D3D11_TEXTURE2D_DESC tex_desc = {};
        tex_desc.Width = m_current_format.width;
        tex_desc.Height = m_current_format.height;
        tex_desc.MipLevels = 1;
        tex_desc.ArraySize = 1;
        tex_desc.Format = output_format;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Usage = D3D11_USAGE_STAGING;
        tex_desc.BindFlags = 0;
        tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        auto hr = device->QueryInterface(IID_ID3D11VideoDevice, &video_device);
        if (FAILED(hr)) 
            return false;

        hr = device_ctx->QueryInterface(IID_ID3D11VideoContext, &video_ctx);
        if (FAILED(hr)) 
            return false;

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content_desc = {};
        content_desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content_desc.InputWidth = m_current_format.width;
        content_desc.InputHeight = m_current_format.height;
        content_desc.OutputWidth = m_current_format.width;
        content_desc.OutputHeight = m_current_format.height;
        content_desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        hr = video_device->CreateVideoProcessorEnumerator(&content_desc, &vp_enum);
        if (FAILED(hr)) 
            return false;

        hr = video_device->CreateVideoProcessor(vp_enum.Get(), 0, &video_processor);
        if (FAILED(hr)) 
            return false;

        if (!staging_tex_pool.create(RWC_STAGING_TEXTURE_POOL_SIZE, device.Get(), tex_desc))
            return false;

        return true;
    }

    // adjust this FUNC
    webcam_error_status mmf_webcam_device::decode_frame_to_bgr24(void* sample, uint8_t* output)
    {
        scoped_timer<milli_dbl> timer{ "Frame Decode" };

        // none == automatically selects gpu as preference on windows
        bool has_gpu_preference = m_preferred_backend != hwd_decode_backend::cpu;

        if (m_context->has_hwd_acc && has_gpu_preference)
            return gpu_frame_decode(sample, output);
        else
            return cpu_frame_decode(sample, output);
    }
    
    webcam_error_status mmf_webcam_device::cpu_frame_decode(void* sample, uint8_t* output)
    {
        RWC_LOG_INFO("Using cpu frame decoding");

        HRESULT hr = S_OK;
        ComPtr<IMFTransform> color_converter{};

        hr = CoCreateInstance(
            CLSID_CColorConvertDMO,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IMFTransform,
            &color_converter
        );

        if (FAILED(hr))
        {
            RWC_LOG_ERROR("Failed to create DMO color converter (hr={})", hr);
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

        video_buffer_lock buffer_lock(std::move(out_buffer));

        // stride of nv12
        const int32_t rgb24_stride = static_cast<int32_t>(m_current_format.width * 3);
        auto buffer_result = buffer_lock.lock(rgb24_stride, m_current_format.height);

        if (!buffer_result) 
        {
            RWC_LOG_ERROR("Failed to lock buffer (hr={})", buffer_result.error());
            return webcam_error_status::cannot_setup_decoder;
        }
        
        {
            scoped_timer<milli_dbl> timer{ "memcpy" };
            uint8_t* raw_buffer = buffer_result->scanline0;
            const size_t bytes_per_pixel = 3;
            size_t size = m_current_format.width * m_current_format.height * bytes_per_pixel;
            for (size_t i{}; i < size; i += bytes_per_pixel)
            {
                output[i + 0] = raw_buffer[i + 2];
                output[i + 1] = raw_buffer[i + 1];
                output[i + 2] = raw_buffer[i + 0];
            }
        }

        return webcam_error_status::ok;
    }
    
    webcam_error_status mmf_webcam_device::gpu_frame_decode(void* sample, uint8_t* output)
    {
        RWC_LOG_INFO("Using gpu frame decoding");

        auto& d3d11_context = m_context->hwd_ctx->d3d11_context;
        auto& input_tex = m_context->hwd_ctx->input_tex;
        auto& output_tex = m_context->hwd_ctx->output_tex;
        auto& vp_enum = m_context->hwd_ctx->vp_enum;
        auto& video_device = m_context->hwd_ctx->video_device;
        auto& video_processor = m_context->hwd_ctx->video_processor;
        auto& staging_tex_pool = m_context->hwd_ctx->staging_tex_pool;
        auto& input_view = m_context->hwd_ctx->input_view;
        auto& output_view = m_context->hwd_ctx->output_view;
        HRESULT hr = S_OK;

        bool has_zero_copy = false;

        ComPtr<IMFMediaBuffer> mf_buffer;
        hr = reinterpret_cast<IMFSample *>(sample)->ConvertToContiguousBuffer(&mf_buffer);
        if (FAILED(hr)) 
            return webcam_error_status::cannot_decode_frame;

        ComPtr<IMFDXGIBuffer> dxgi_buffer;
        hr = mf_buffer.As(&dxgi_buffer);
        has_zero_copy = SUCCEEDED(hr);

        // Update input texture with frame data
        if (has_zero_copy)
        {
            RWC_LOG_INFO("Using zero-copy optimization");
            hr = dxgi_buffer->GetResource(IID_ID3D11Texture2D, &input_tex);
            if (FAILED(hr))
                return webcam_error_status::cannot_decode_frame;
        }
        else 
        {
            video_buffer_lock buffer_guard(std::move(mf_buffer));
            const auto nv12_stride = static_cast<int32_t>(m_current_format.width);

            auto buffer_result = buffer_guard.lock(nv12_stride, m_current_format.height);
            if (!buffer_result) 
                return webcam_error_status::cannot_decode_frame;

            d3d11_context->UpdateSubresource(input_tex.Get(), 0, nullptr, 
                                             buffer_result->scanline0, buffer_result->stride, 0);
        }

        // Create video processor input and output view
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_view_desc = {};
        input_view_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        input_view_desc.Texture2D.ArraySlice = 0;

        hr = video_device->CreateVideoProcessorInputView(input_tex.Get(), vp_enum.Get(), 
                                                         &input_view_desc, &input_view);

        if (FAILED(hr)) 
            return webcam_error_status::cannot_decode_frame;

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out_view_desc = {};
        out_view_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        out_view_desc.Texture2D.MipSlice = 0;

        hr = video_device->CreateVideoProcessorOutputView(output_tex.Get(), vp_enum.Get(), 
                                                          &out_view_desc, &output_view);
        if (FAILED(hr)) 
            return webcam_error_status::cannot_decode_frame;

        // process video convertion
        D3D11_VIDEO_PROCESSOR_STREAM stream = {};
        stream.Enable = TRUE;
        stream.pInputSurface = input_view.Get();

        hr = m_context->hwd_ctx->video_ctx->VideoProcessorBlt(video_processor.Get(), output_view.Get(), 0, 1, &stream);
        if (FAILED(hr)) 
            return webcam_error_status::cannot_decode_frame;

        d3d11_context->Flush();

        // Copy to staging texture
        if (staging_tex_pool.current_index() < RWC_STAGING_TEXTURE_POOL_SIZE)
        {
            for (size_t i{}; i < staging_tex_pool.buffer_count(); ++i)
            {
                d3d11_context->CopyResource(staging_tex_pool.get_write_texture(), output_tex.Get());
                staging_tex_pool.update_indices();
            }
        }
        else
        {
            d3d11_context->CopyResource(staging_tex_pool.get_write_texture(), output_tex.Get());
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = d3d11_context->Map(staging_tex_pool.get_read_texture(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) 
            return webcam_error_status::cannot_decode_frame;

        auto width = m_current_format.width;
        auto height = m_current_format.height;
        const auto input = static_cast<const uint8_t *>(mapped.pData);

        rgba_to_rgb(input, mapped.RowPitch, output, width * 3, width, height);
        
        d3d11_context->Unmap(staging_tex_pool.get_read_texture(), 0);
        staging_tex_pool.update_indices();

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

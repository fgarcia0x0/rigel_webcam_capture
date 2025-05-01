#include "rwc/core/webcam_device.hpp"
#include <cstdint>
#include <rwc/platform/linux/v4l2_webcam_device.h>
#include <rwc/core/webcam_image_decoder.h>
#include <rwc/core/webcam_utils.h>
#include <rwc/utils/scope_exit.hpp>
#include <rwc/logger/logger.h>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include <optional>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <thread>
#include <memory>
#include <stop_token>
#include <utility>
#include <cstdlib>

using namespace std::chrono_literals;

static inline int sys_ioctl(int fd, unsigned long request, void* arg)
{
    int result{};

    do 
    {
        result = ioctl(fd, request, arg);
    } while (result == -1 && errno == EINTR);

    return result;
}

namespace rwc
{
    webcam_error_status v4l2_webcam_device::open(uint32_t index)
    {
        scope_exit fd_guard = [this](){ if (m_device_fd != -1) ::close(m_device_fd); };

        m_device_path = std::format("/dev/video{}", index);
        m_device_fd = ::open(m_device_path.c_str(), O_RDWR | O_NONBLOCK);
       
        if (m_device_fd < 0)
            return webcam_error_status::invalid_device;

        v4l2_capability video_caps = {};
        if (sys_ioctl(m_device_fd, VIDIOC_QUERYCAP, &video_caps) == -1)
            return webcam_error_status::query_capability_failed;

        if (!(video_caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) 
            return webcam_error_status::device_cannot_capture;

        if (!(video_caps.capabilities & V4L2_CAP_STREAMING)) 
            return webcam_error_status::device_cannot_streaming;

        auto device_info = read_device_info();
        if (!device_info)
            return webcam_error_status::query_capability_failed;

        m_device_info = std::move(device_info).value();
        m_opened = true;

        // select best format if not set
        if (!m_current_format.width || !m_current_format.height)
        {
            auto best_fmt = webcam_utils::select_best_format(m_device_info.formats, RWC_WEBCAM_CODEC_TYPE_DEFAULT);
            m_current_format = best_fmt.value_or(m_device_info.formats[0]);
        }
        
        fd_guard.reset();
        return webcam_error_status::ok;
    }
    
    void v4l2_webcam_device::close()
    {
        if (m_streaming)
            stop_stream();

        if (m_opened)
        {
            static_cast<void>(::close(m_device_fd));
            m_device_fd = -1;
        }

        m_opened = false;
    }
    
    bool v4l2_webcam_device::is_opened() const 
    {
        return m_opened;
    }
    
    bool v4l2_webcam_device::is_streaming() const 
    {
        return m_streaming;
    }
    
    const webcam_device_info& v4l2_webcam_device::device_info() const 
    {
        return m_device_info;
    }
    
    bool v4l2_webcam_device::has_pending_frame() const 
    {
        return !m_frame_queue.empty() || m_last_frame_status != webcam_error_status::ok;
    }
    
    capture_format_info v4l2_webcam_device::current_format() noexcept
    {
        return m_current_format;
    }
    
    bool v4l2_webcam_device::set_current_format(const capture_format_info& format)
    {
        const auto& formats = m_device_info.formats;
        if (std::find(formats.cbegin(), formats.cend(), format) == formats.cend())
            return false;

        m_current_format = format;
        return true;
    }
    
    void v4l2_webcam_device::set_pixel_format(webcam_frame_pixel_format pixel_format)
    {
        m_pixel_fmt = pixel_format;
    }
    
    std::expected<webcam_frame_owner, webcam_error_status> v4l2_webcam_device::read_frame()
    {
        if (!is_opened())
            return std::unexpected{ webcam_error_status::bad_device };

        if (m_last_frame_status != webcam_error_status::ok)
            return std::unexpected{ m_last_frame_status.load() };

        if (m_frame_queue.empty())
            return std::unexpected{ webcam_error_status::frame_not_ready };

        return m_frame_queue.dequeue().value();
    }
    
    uint32_t v4l2_webcam_device::device_count() noexcept
    {
        namespace fs = std::filesystem;
        const fs::path scan_path{ "/sys/class/video4linux/" };
        uint32_t device_count{};

        for (auto&& entry : fs::directory_iterator{ scan_path })
        {
            if (entry.path().filename().string().contains("video"))
            {
                fs::path target_path{ "/dev" / entry.path().filename() };
                int fd = ::open(target_path.string().c_str(), O_RDWR | O_NONBLOCK);
                
                if (fd >= 0)
                {
                    v4l2_capability video_caps = {};
                    if (sys_ioctl(fd, VIDIOC_QUERYCAP, &video_caps) != -1)
                    {
                        if ((video_caps.capabilities & V4L2_CAP_VIDEO_CAPTURE) && 
                            (video_caps.capabilities & V4L2_CAP_STREAMING)) 
                        {
                            // check if have any formats
                            v4l2_fmtdesc format_desc = {};
                            format_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

                            if (sys_ioctl(fd, VIDIOC_ENUM_FMT, &format_desc) == 0)
                            {
                                ++device_count;
                            }
                            
                        }
                    }

                    ::close(fd);
                }
            }
        }

        return device_count;
    }
    
    std::optional<webcam_ctrl_property> v4l2_webcam_device::get_ctrl_property(webcam_property_type type)
    {
        if (type == webcam_property_type::last)
            return {};

        webcam_ctrl_property wc_ctrl_prop{};
        v4l2_control ctrl = {};
        v4l2_queryctrl query_ctrl = {};
        bool is_auto = false;

        uint32_t id = to_v4l2_type(type);
        if (id == UINT32_MAX)
            return {};

        switch (type) 
        {
            case webcam_property_type::auto_exposure:
            case webcam_property_type::auto_focus:
            case webcam_property_type::auto_white_balance:
            case webcam_property_type::auto_gain:
                is_auto = true;
                break;
            default:
                is_auto = false;
                break;
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

        return wc_ctrl_prop;
    }
    
    bool v4l2_webcam_device::set_ctrl_property(webcam_property_type type, int32_t value)
    {
        v4l2_control ctrl = {};
        uint32_t id = to_v4l2_type(type);
        
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
    
    bool v4l2_webcam_device::set_ctrl_property_default(webcam_property_type type)
    {
        auto ctrl_prop = get_ctrl_property(type);
        return ctrl_prop ? set_ctrl_property(type, ctrl_prop->default_value) : false;
    }
    
    void v4l2_webcam_device::reset_ctrl_properties()
    {
        for (uint32_t prop_index{}; prop_index != std::to_underlying(webcam_property_type::last); ++prop_index)
            set_ctrl_property_default(static_cast<webcam_property_type>(prop_index));
    }
    
    std::optional<webcam_device_info> v4l2_webcam_device::read_device_info()
    {
        std::string_view index_str = std::string_view{ m_device_path }.substr(m_device_path.find_first_of("0123456789"));
        v4l2_capability video_caps = {};
        webcam_device_info device_info = {};

        if (sys_ioctl(m_device_fd, VIDIOC_QUERYCAP, &video_caps) == -1)
            return std::nullopt;

        device_info.unique_id = std::format("{} [{}] ({})", reinterpret_cast<const char *>(video_caps.card), 
                                                            reinterpret_cast<const char *>(video_caps.driver),
                                                            reinterpret_cast<const char *>(video_caps.bus_info));

        device_info.name = std::format("{}", reinterpret_cast<const char *>(video_caps.card));
        device_info.path = m_device_path;
        device_info.index = static_cast<uint32_t>(std::strtoul(index_str.data(), nullptr, 10));

        // read supported formats
        device_info.formats = read_webcam_formats();
        if (device_info.formats.empty())
            return std::nullopt;

        return device_info;
    }
    
    std::vector<capture_format_info> v4l2_webcam_device::read_webcam_formats()
    {
        static constexpr uint32_t kPreAllocFormatsCount{ 24 };

        std::vector<capture_format_info> formats;
        formats.reserve(kPreAllocFormatsCount);

        v4l2_fmtdesc format_desc = {};
        format_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format_desc.index = 0;

        for (; sys_ioctl(m_device_fd, VIDIOC_ENUM_FMT, &format_desc) == 0; ++format_desc.index)
        {
            capture_format_info format_info = {};
            format_info.codec = format_desc.pixelformat;

            v4l2_frmsizeenum frame_size = {};
            frame_size.pixel_format = format_desc.pixelformat;

            for (; sys_ioctl(m_device_fd, VIDIOC_ENUM_FRAMESIZES, &frame_size) == 0; ++frame_size.index)
            {
                if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                {
                    v4l2_frmivalenum frame_interval = {};
                    frame_interval.width = frame_size.discrete.width;
                    frame_interval.height = frame_size.discrete.height;
                    frame_interval.pixel_format = format_desc.pixelformat;

                    for (; sys_ioctl(m_device_fd, VIDIOC_ENUM_FRAMEINTERVALS, &frame_interval) == 0; ++frame_interval.index)
                    {
                        if (frame_interval.type == V4L2_FRMIVAL_TYPE_DISCRETE)
                        {
                            if (frame_interval.discrete.numerator == 0)
                                continue;

                            format_info.width = frame_size.discrete.width;
                            format_info.height = frame_size.discrete.height;
                            format_info.fps = frame_interval.discrete.denominator / frame_interval.discrete.numerator;

                            formats.push_back(std::move(format_info));
                        }
                    }
                }
            }
        }

        return formats;
    }
    
    webcam_error_status v4l2_webcam_device::setup_webcam_image()
    {
        v4l2_format img_fmt = {};
        img_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        img_fmt.fmt.pix.width = m_current_format.width;
        img_fmt.fmt.pix.height = m_current_format.height;
        img_fmt.fmt.pix.pixelformat = m_current_format.codec;
        img_fmt.fmt.pix.field = V4L2_FIELD_NONE;

        // tell the device you are using this format
        if(sys_ioctl(m_device_fd, VIDIOC_S_FMT, &img_fmt) < 0)
            return webcam_error_status::cannot_set_image_format;

        v4l2_streamparm stream_params{};
        stream_params.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (sys_ioctl(m_device_fd, VIDIOC_G_PARM, &stream_params) < 0)
            return webcam_error_status::cannot_setup_framerate;

        if (stream_params.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)
        {
            stream_params.parm.capture.timeperframe.numerator = 1;
            stream_params.parm.capture.timeperframe.denominator = m_current_format.fps;
            
            if (sys_ioctl(m_device_fd, VIDIOC_S_PARM, &stream_params) < 0)
                return webcam_error_status::cannot_setup_framerate;
        }

        return webcam_error_status::ok;
    }

    webcam_error_status v4l2_webcam_device::create_webcam_buffers(uint32_t buffer_count)
    {
        v4l2_requestbuffers req_buffer = {};
        req_buffer.count = buffer_count;
        req_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req_buffer.memory = V4L2_MEMORY_MMAP;

        if(sys_ioctl(m_device_fd, VIDIOC_REQBUFS, &req_buffer) < 0) 
            return webcam_error_status::cannot_setup_buffer;

        if (req_buffer.count != buffer_count)
        {
            RWC_LOG_WARN("The buffer count requested [{}] was denied, driver using [{}] instead", buffer_count, req_buffer.count);
        }

        m_buffer_pool.resize(req_buffer.count);

        for (uint32_t buffer_index = 0; buffer_index < req_buffer.count; ++buffer_index) 
        {
            v4l2_buffer query_buffer = {};
            query_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            query_buffer.memory = V4L2_MEMORY_MMAP;
            query_buffer.index = buffer_index;

            if(sys_ioctl(m_device_fd, VIDIOC_QUERYBUF, &query_buffer) < 0)
            {
                destroy_webcam_buffers();
                return webcam_error_status::cannot_setup_buffer;
            }

            m_buffer_pool[buffer_index].data = mmap(nullptr, query_buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_device_fd, query_buffer.m.offset);
            m_buffer_pool[buffer_index].length = query_buffer.length;

            if (m_buffer_pool[buffer_index].data == MAP_FAILED)
            {
                destroy_webcam_buffers();
                return webcam_error_status::cannot_map_buffer;
            }
        }
 
        return webcam_error_status::ok;
    }

    void v4l2_webcam_device::destroy_webcam_buffers()
    {
        for (size_t i = 0; i < m_buffer_pool.size(); ++i)
        {
            if (m_buffer_pool[i].data != nullptr && m_buffer_pool[i].data != MAP_FAILED)
            {
                munmap(m_buffer_pool[i].data, m_buffer_pool[i].length);
            }
        }

        m_buffer_pool.clear();

        v4l2_requestbuffers req_buffer = {};
        req_buffer.count = 0;
        req_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req_buffer.memory = V4L2_MEMORY_MMAP;

        if (sys_ioctl(m_device_fd, VIDIOC_REQBUFS, &req_buffer) < 0) 
            RWC_LOG_WARN("Failed to reset device buffer count");
    }
    
    bool v4l2_webcam_device::enqueue_buffers()
    {
        for (size_t i = 0; i < m_buffer_pool.size(); ++i)
        {
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = static_cast<uint32_t>(i);

            if (sys_ioctl(m_device_fd, VIDIOC_QBUF, &buffer) < 0)
                return false;
        }

        return true;
    }
    
    bool v4l2_webcam_device::dequeue_buffers()
    {
        for (size_t i = 0; i < m_buffer_pool.size(); ++i)
        {
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = static_cast<uint32_t>(i);

            if (sys_ioctl(m_device_fd, VIDIOC_DQBUF, &buffer) < 0)
                return false;
        }
        
        return true;
    }
    
    bool v4l2_webcam_device::webcam_start_streaming()
    {
        v4l2_buf_type buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (sys_ioctl(m_device_fd, VIDIOC_STREAMON, &buffer_type) < 0)
            return false;

        return true;
    }
    
    bool v4l2_webcam_device::webcam_stop_streaming()
    {
        v4l2_buf_type buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (sys_ioctl(m_device_fd, VIDIOC_STREAMOFF, &buffer_type) < 0)
            return false;

        return true;
    }
    
    webcam_error_status v4l2_webcam_device::wait_device_ready(std::chrono::seconds timeout)
    {
        timeval timeout_value{ .tv_sec = timeout.count(), .tv_usec = 0  };
        fd_set fds_read{};
        FD_SET(m_device_fd, &fds_read);
        int result = 0;

        do
        {
            result = select(m_device_fd + 1, &fds_read, nullptr, nullptr, &timeout_value);
        } while (result == -1 && errno == EINTR);

        if (result == 0)
            return webcam_error_status::timeout;
        else if (result < 0)
            return webcam_error_status::wait_error;
        else
            return webcam_error_status::ok;
    }
    
    webcam_error_status v4l2_webcam_device::start_stream()
    {
        if (!is_opened() || is_streaming())
            return webcam_error_status::invalid_state;

        auto status = setup_webcam_image();
        if (status != webcam_error_status::ok)
            return status;

        status = create_webcam_buffers(RWC_WEBCAM_STREAMING_BUFFER_COUNT);
        if (status != webcam_error_status::ok)
            return status;

        if (!enqueue_buffers())
        {
            status = webcam_error_status::cannot_enqueue_buffer;
            destroy_webcam_buffers();
            return status;
        }

        if (!webcam_start_streaming())
        {
            status = webcam_error_status::cannot_start_streaming;
            dequeue_buffers();
            destroy_webcam_buffers();
            return status;
        }

        m_streaming = true;
        m_v4l2_thread = std::make_unique<std::jthread>([this](auto token){ v4l2_capture_thread(token); });

        return webcam_error_status::ok;
    }
    
    void v4l2_webcam_device::stop_stream()
    {
        if (!is_opened() || !is_streaming())
            return;

        m_v4l2_thread->request_stop();
        m_v4l2_thread->join();
        m_v4l2_thread.reset();
        
        webcam_stop_streaming();
        destroy_webcam_buffers();

        m_streaming = false;
    }

    void v4l2_webcam_device::v4l2_capture_thread(std::stop_token token)
    {
        uint32_t timeout_count{};
        webcam_image_decoder decoder{};

        while (!token.stop_requested())
        {
            // wait here
            webcam_error_status status = wait_device_ready(RWC_WEBCAM_STREAMING_TIMEOUT);
            if (status == webcam_error_status::timeout)
            {
                if (++timeout_count < RWC_WEBCAM_STREAMING_MAX_CONTINUOS_TIMEOUT)
                    continue;
                else 
                    RWC_LOG_WARN("Continuous Timeout: {}", timeout_count);
            }

            if (status != webcam_error_status::ok)
            {
                m_last_frame_status = status;
                return;
            }

            v4l2_buffer buffer = {};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;

            if (sys_ioctl(m_device_fd, VIDIOC_DQBUF, &buffer) < 0)
            {
                m_last_frame_status = webcam_error_status::cannot_dequeue_buffer;
                return;
            }
            
            auto img_buffer = std::make_unique<uint8_t[]>(buffer.bytesused);
            if (!img_buffer)
            {
                m_last_frame_status = webcam_error_status::cannot_create_buffer;
                return;
            }

            std::memcpy(img_buffer.get(), m_buffer_pool[buffer.index].data, buffer.bytesused);

            auto timestamp = webcam_utils::current_timestamp();
            webcam_frame_owner frame{ m_current_format.width, m_current_format.height, 
                                      buffer.bytesused, m_pixel_fmt, timestamp, std::move(img_buffer) };

            // decode frame if necessary
            if (m_pixel_fmt != webcam_frame_pixel_format::native)
            {
                bool frame_decoded = decode_frame_to_rgb24(&frame, m_current_format.codec, &decoder);
                if (!frame_decoded)
                {
                    m_last_frame_status = webcam_error_status::cannot_decode_frame;
                    continue; // ignore this frame and go to another
                }
            }
            
            m_frame_queue.enqueue(std::move(frame));

            if (sys_ioctl(m_device_fd, VIDIOC_QBUF, &buffer) < 0)
            {
                m_last_frame_status = webcam_error_status::cannot_enqueue_buffer;
                return;
            }
        }
    }
    
    bool v4l2_webcam_device::decode_frame_to_rgb24(webcam_frame_owner* frame, uint32_t codec, webcam_image_decoder* decoder)
    {
        static constexpr uint32_t rgb_bytes_per_pixel{ 3 };
        static constexpr std::array supported_codecs{ RWC_WEBCAM_CODEC_TYPE_YUYV, RWC_WEBCAM_CODEC_TYPE_MJPEG, RWC_WEBCAM_CODEC_TYPE_JPEG };

        // if not contains any supported codecs, return
        if (std::find(supported_codecs.cbegin(), supported_codecs.cend(), codec) == supported_codecs.cend())
            return false;

        uint32_t new_buffer_size = frame->width * frame->height * rgb_bytes_per_pixel;
        auto new_buffer = std::make_unique<uint8_t[]>(new_buffer_size);
        bool decoded = false;
        
        switch (codec) 
        {
            case RWC_WEBCAM_CODEC_TYPE_YUYV:
                decoded = decoder->yuyv_to_rgb24(frame->buffer.get(), new_buffer.get(), frame->size);
                break;
            case RWC_WEBCAM_CODEC_TYPE_JPEG:
            case RWC_WEBCAM_CODEC_TYPE_MJPEG:
                decoded = decoder->jpeg_to_rgb24(frame->buffer.get(), new_buffer.get(), frame->size);
                break;
            default:
                break;
        }

        if (!decoded)
            return false;

        frame->size = new_buffer_size;
        frame->buffer = std::move(new_buffer);

        return true;
    }
    
    uint32_t v4l2_webcam_device::to_v4l2_type(webcam_property_type type)
    {
        uint32_t id{ UINT32_MAX };

        switch (type)
        {
        case webcam_property_type::exposure:
            id = V4L2_CID_EXPOSURE_ABSOLUTE;
            break;
        case webcam_property_type::auto_exposure:
            id = V4L2_CID_EXPOSURE_AUTO;
            break;
        case webcam_property_type::focus:
            id = V4L2_CID_FOCUS_ABSOLUTE;
            break; 
        case webcam_property_type::auto_focus:
            id = V4L2_CID_FOCUS_AUTO;
            break;
        case webcam_property_type::zoom:
            id = V4L2_CID_ZOOM_ABSOLUTE;
            break;
        case webcam_property_type::white_balance:
            id = V4L2_CID_WHITE_BALANCE_TEMPERATURE;
            break;
        case webcam_property_type::auto_white_balance:
            id = V4L2_CID_AUTO_WHITE_BALANCE;
            break;
        case webcam_property_type::gain:
            id = V4L2_CID_GAIN;
            break;
        case webcam_property_type::auto_gain:
            id = V4L2_CID_AUTOGAIN;
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

    v4l2_webcam_device::~v4l2_webcam_device()
    {
        stop_stream();
        close();
    }
}


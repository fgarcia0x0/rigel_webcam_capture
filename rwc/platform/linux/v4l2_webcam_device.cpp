#include "rwc/core/webcam_device.hpp"
#include <rwc/platform/linux/v4l2_webcam_device.h>
#include <rwc/platform/linux/v4l2_webcam_controller.h>
#include <rwc/core/webcam_utils.h>
#include <rwc/utils/scope_exit.hpp>
#include <rwc/logger/logger.h>
#include <rwc/utils/utils.hpp>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <tuple>
#include <unistd.h>
#include <linux/videodev2.h>

#include <optional>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <thread>
#include <memory>
#include <stop_token>
#include <utility>
#include <cstdlib>
#include <cstdint>
#include <new>

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
        if (m_opened)
            return webcam_error_status::device_already_opened;

        if (m_streaming)
            return webcam_error_status::invalid_state;

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
        m_controller = std::make_unique<v4l2_webcam_controller>(m_device_fd);
        m_opened = true;

        // select best format if not set
        if (!m_current_format.width || !m_current_format.height)
        {
            auto best_fmt = webcam_utils::select_capture_format(m_device_info.formats, RWC_WEBCAM_CODEC_TYPE_DEFAULT, std::greater<>{});
            m_current_format = best_fmt.value_or(m_device_info.formats[0]);
        }
        
        fd_guard.reset();
        return webcam_error_status::ok;
    }

    webcam_error_status v4l2_webcam_device::reset(uint32_t index)
    {
        close();
        return open(index);
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

        m_last_frame_status = webcam_error_status::ok;
        m_frame_queue.clear();
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

    webcam_controller* v4l2_webcam_device::ctrl() noexcept
    {
        return m_controller.get();
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
        if (m_streaming)
            return false;

        const auto& formats = m_device_info.formats;
        if (std::find(formats.cbegin(), formats.cend(), format) == formats.cend())
            return false;

        m_current_format = format;
        return true;
    }
    
    bool v4l2_webcam_device::set_current_format_by_index(uint32_t index)
    {
        if (index >= m_device_info.formats.size())
            return false;

        m_current_format = m_device_info.formats[index];
        return true;
    }
    
    bool v4l2_webcam_device::set_preferred_decode_backend(hwd_decode_backend backend)
    {
        std::ignore = backend;
        return true;
    }
    
    std::expected<webcam_frame_rgb24, webcam_error_status> v4l2_webcam_device::read_frame()
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

        if (req_buffer.count < RWC_WEBCAM_STREAMING_MIN_BUFFER_COUNT)
        {
            RWC_LOG_ERROR("The buffer count requested [{}] is less than minimum count [{}]", buffer_count, RWC_WEBCAM_STREAMING_MIN_BUFFER_COUNT);
            return webcam_error_status::cannot_create_buffer;
        }

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

        m_last_frame_status = webcam_error_status::ok;
        m_frame_queue.clear();
        m_streaming = false;
    }

    void v4l2_webcam_device::v4l2_capture_thread(std::stop_token token)
    {
        uint32_t timeout_count{};
        webcam_image_decoder decoder{};
        uint32_t last_sequence{};
        bool have_last_sequence{};

        while (!token.stop_requested())
        {
            webcam_error_status status = wait_device_ready(RWC_WEBCAM_STREAMING_TIMEOUT);

            if (status == webcam_error_status::timeout)
            {
                if (++timeout_count < RWC_WEBCAM_STREAMING_MAX_CONTINUOS_TIMEOUT)
                {
                    continue;
                }
                else 
                {
                    RWC_LOG_WARN("Continuous Timeout: {}", timeout_count);
                    m_last_frame_status = status;
                    return;
                }
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
            
            if (have_last_sequence && buffer.sequence != last_sequence + 1)
            {
                RWC_LOG_WARN("DROPPED {} frame(s) by the driver [seq {} -> {}]",
                             buffer.sequence - last_sequence - 1, last_sequence, buffer.sequence);
            }
            last_sequence = buffer.sequence;
            have_last_sequence = true;

            std::unique_ptr<uint8_t[]> img_buffer{ new (std::nothrow) uint8_t[buffer.bytesused] };
            if (!img_buffer)
            {
                m_last_frame_status = webcam_error_status::cannot_create_buffer;
                return;
            }

            std::memcpy(img_buffer.get(), m_buffer_pool[buffer.index].data, buffer.bytesused);

            auto timestamp = rwc::utils::current_timestamp();
            webcam_frame_rgb24 frame{ m_current_format.width, m_current_format.height,
                                      buffer.bytesused, timestamp, std::move(img_buffer) };

            status = decode_frame_to_rgb24(&frame, m_current_format.codec, &decoder);
            if (status == webcam_error_status::ok)
            {
                m_frame_queue.enqueue(std::move(frame));
            }
            // else: ignore this frame and go to another. This is expected to happen
            // transiently (e.g. a corrupt frame, or the one-frame decode delay some
            // codecs like H264 have on their very first buffer), so it must not be
            // treated as a fatal/sticky error - only requeue the buffer below and retry.

            if (sys_ioctl(m_device_fd, VIDIOC_QBUF, &buffer) < 0)
            {
                m_last_frame_status = webcam_error_status::cannot_enqueue_buffer;
                return;
            }
        }
    }

    static inline auto fourcc_to_str(uint32_t code)
    {
        std::string result(sizeof(uint32_t) + 1, '\0');
        std::memcpy(result.data(), &code, sizeof(uint32_t));
        return result;
    }
    
    webcam_error_status v4l2_webcam_device::decode_frame_to_rgb24(webcam_frame_rgb24* frame, uint32_t codec, webcam_image_decoder* decoder)
    {
        static constexpr uint32_t rgb_bytes_per_pixel{ 3 };
        static constexpr std::array supported_codecs {
            RWC_WEBCAM_CODEC_TYPE_YUYV,
            RWC_WEBCAM_CODEC_TYPE_MJPEG,
            RWC_WEBCAM_CODEC_TYPE_JPEG,
            RWC_WEBCAM_CODEC_TYPE_H264
        };

        // if not contains any supported codecs, return
        if (std::find(supported_codecs.cbegin(), supported_codecs.cend(), codec) == supported_codecs.cend())
        {
            RWC_LOG_ERROR("Unsupported codec [codec=\"{}\"]", fourcc_to_str(codec));
            return webcam_error_status::unsupported_codec;
        }

        uint32_t new_buffer_size = frame->width * frame->height * rgb_bytes_per_pixel;
        std::unique_ptr<uint8_t[]> new_buffer(new (std::nothrow) uint8_t[new_buffer_size]);
        if (!new_buffer)
        {
            RWC_LOG_ERROR("Failed to allocate {:.3f} MiB of memory for frame buffer", new_buffer_size / 1024.0 / 1024.0);
            return webcam_error_status::memory_exhausted;
        }

        bool decoded = decoder->decode_to_rgb24(std::span{ frame->buffer.get(), frame->size }, 
                                                std::span{ new_buffer.get(), new_buffer_size }, 
                                                codec);
        if (!decoded)
        {
            RWC_LOG_ERROR("Failed to decode frame [codec={}]", fourcc_to_str(codec));
            return webcam_error_status::cannot_decode_frame;
        }

        frame->size = new_buffer_size;
        frame->buffer = std::move(new_buffer);

        return webcam_error_status::ok;
    }

    v4l2_webcam_device::~v4l2_webcam_device()
    {
        stop_stream();
        close();
    }
}


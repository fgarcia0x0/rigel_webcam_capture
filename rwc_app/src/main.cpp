#include <rwc/core/webcam_device.hpp>
#include <rwc/core/webcam_manager.h>
#include <rwc/core/webcam_utils.h>
#include <rwc/logger/logger.h>
#include <rwc/logger/console_sink.hpp>

#include <print>
#include <fstream>
#include <chrono>
#include <cassert>
#include <memory>
#include <utility>

int streaming_test();
int props_test();

int main(int, char**) 
{
    return streaming_test();
}

int streaming_test()
{
    // create a console sinker
    rwc::logger::instance().add_sink(std::make_shared<rwc::console_sink>());

    auto wcam = rwc::webcam_manager::create_device();
    if (wcam->open() != rwc::webcam_error_status::ok)
        return 1;

    auto fmt = *rwc::webcam_utils::select_capture_format(wcam->device_info().formats, rwc::RWC_WEBCAM_CODEC_TYPE_MJPEG, std::greater<>{});
    if (!wcam->set_current_format(fmt))
        return 1;

    if (wcam->start_stream() != rwc::webcam_error_status::ok)
        return 1;

    size_t fps{};
    constexpr size_t target_fps{ 30*3 };

    RWC_LOG_INFO("Processing Started");
    auto t0 = std::chrono::steady_clock::now();
    while (fps != target_fps)
    {
        if (wcam->has_pending_frame())
        {
            if (auto frame = wcam->read_frame(); frame)
            {
                assert(frame->buffer.get() != nullptr && frame->size > 0);
                
                std::string filename = std::format("frame_{}.ppm", ++fps);
                std::println("[+] Processing \"{}\"", filename);

                if (std::ofstream ofs{ filename, std::ios::binary })
                {
                    ofs << std::format("P6\n{} {} \n255\n", frame->width, frame->height);
                    ofs.write(reinterpret_cast<const char *>(frame->buffer.get()), frame->size);
                }
            } 
            else
            {
                std::println("Webcam Error #{}", (uint32_t) frame.error());
                break;
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(t1 - t0);

    RWC_LOG_INFO("Processing Finished");
    RWC_LOG_INFO("Elapsed time: {:.2f} secs", elapsed.count());

    wcam->stop_stream();
    wcam->close();

    return 0;
}

int props_test()
{
    // create a console sinker
    rwc::logger::instance().add_sink(std::make_shared<rwc::console_sink>());

    auto wcam = rwc::webcam_manager::create_device();
    wcam->open();

    for (uint32_t i = 0; i < std::to_underlying(rwc::webcam_property_type::last); ++i)
    {
        auto wc_prop_type = static_cast<rwc::webcam_property_type>(i);
        if (auto wc_ctrl_prop = wcam->get_ctrl_property(wc_prop_type); wc_ctrl_prop)
        {
            std::println("[+] Property \"{}\" => (v: {}, step: {}, min: {}, max: {}, dft: {})",
                         rwc::webcam_utils::prop_type_to_string(wc_prop_type),
                         wc_ctrl_prop->value, wc_ctrl_prop->step, wc_ctrl_prop->minimum, 
                         wc_ctrl_prop->maximum, wc_ctrl_prop->default_value);
        }
    }
    
    return 0;
}

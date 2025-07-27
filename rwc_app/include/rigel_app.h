#pragma once

#include <SDL3/SDL.h>

#include <rwc/core/webcam_device.hpp>

#include <memory>
#include <string_view>

#include "task_queue.h"

enum class renderer_api
{
    automatic,
    opengl,
    opengl_es,
    vulkan,
    direct3d11,
    direct3d12,
    metal
};

struct rigel_app_specs
{
    std::string_view title = "Rigel Webcam Capture App";
    int width = {};
    int height = {};
    renderer_api render_api = renderer_api::automatic;
};

class rigel_app
{
public:
    rigel_app(rigel_app_specs spec);
    ~rigel_app();

    void show_window();
    int run();

private:
    bool initialize_context();
    bool create_window(std::string_view title, int width, int height);
    bool create_renderer(std::string_view render_api);
    bool setup_window_and_renderer(std::string_view title, int width, int height, std::string_view renderer_name);
    void choose_window_size(float scale, int* out_width, int* out_height);
    void setup_ui_settings();
    void setup_ui_data();
    bool setup_webcam_device(size_t device_index = 0);
    void setup_signal_handlers();
    bool create_texture(int width, int height, uint32_t pixel_format);
    SDL_FRect adjust_aspect_ratio(int tex_width, int tex_height);
    void process_events();
    void process_webcam_frame();
    void render_frame();
    void shutdown_context();
    void shutdown();
    void save_frame_to_file(std::string_view filepath);
    bool build_render_pipeline(std::string_view renderer_name);
    void create_webcam_texture();
    void process_tasks();
    void change_webcam_capture_format(const rwc::capture_format_info& new_format_info);
    void update_webcam_properties();

protected:
    void renderer_changed_handler(size_t renderer_index);
    void aot_changed_handler(bool state);
    void webcam_device_changed_handler(size_t device_index);
    void webcam_format_changed_handler(size_t format_index);
    void webcam_resolution_changed(size_t res_index);
    void webcam_fps_changed(size_t fps_index);
    void vsync_changed_handler(size_t vsync_index);
    void webcam_property_changed_handler(const rwc::webcam_ctrl_property& property);
    void webcam_properties_reseted_handler();
    void ui_font_size_changed(float new_value);
    void webcam_image_horflip_changed(bool state);
private:
    std::shared_ptr<rwc::webcam_device> m_webcam_device;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> m_window;
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> m_renderer;
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> m_texture;
    rigel_app_specs m_app_specs;
    task_queue m_task_queue;
    bool m_running = true;
    bool m_webcam_vsync = false;
    bool m_draw_frame = false;
    bool m_image_flipped = false;
};

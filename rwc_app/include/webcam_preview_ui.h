#pragma once

#include <rwc/core/webcam_device.hpp>

#include <imgui.h>
#include <SDL3/SDL_render.h>

#include <vector>
#include <string>
#include <functional>
#include <chrono>

enum class color_theme
{
    light,
    dark,
    classic
};

struct webcam_preview_settings
{
    float default_font_scale = 1.0f;
    color_theme color_theme = color_theme::light;
    bool frame_border = false;
    float frame_round = 0.0f;
    float window_round = 0.0f;
    bool hide_sidebar = false;
    bool window_always_on_top = false;
    bool keep_aspect_ratio = false;

    std::vector<std::string> resolution_labels;
    std::vector<std::string> theme_labels = { "light", "dark", "classic" };
    std::vector<std::string> render_apis;
    std::vector<std::string> webcam_devices;
    std::vector<std::string> format_labels;
    std::vector<std::string> fps_labels;
    std::vector<rwc::webcam_ctrl_property> webcam_properties;

    // indexes
    int selected_render_api = 0;
    int selected_resolution = 0;
    int selected_format = 0;
    int selected_fps = 0;
    int selected_webcam_device = 0;
};

class webcam_preview_ui
{
public:
    static bool initialize(SDL_Window* window, SDL_Renderer* renderer);
    static webcam_preview_settings& settings() noexcept;
    static void update_settings();
    static bool process_events(const SDL_Event* event);
    static void render(SDL_Renderer* renderer);
    static void start_frame();
    static void finish_frame();
    static void shutdown();
    static const ImVec2& get_frame_buffer_scale();
    static void reload_fonts_at_scale(float scale);
    
    static void set_canvas_loading(bool state);
    static bool is_canvas_loading();
    static void trigger_save_notification();

    // ui functions
    static void update_ui();
    static void create_window_section();
    static void create_webcam_settings_section();
    static void create_webcam_controls_section();
    static void create_frametime_section();    

    // callbacks
    static void on_always_on_top_changed(std::function<void(bool)> callback);
    static void on_webcam_device_changed(std::function<void(size_t)> callback);
    static void on_renderer_changed(std::function<void(size_t)> callback);
    static void on_resolution_changed(std::function<void(size_t)> callback);
    static void on_format_changed(std::function<void(size_t)> callback);
    static void on_fps_changed(std::function<void(size_t)> callback);
    static void on_vsync_changed(std::function<void(size_t)> callback);
    static void on_webcam_property_changed(std::function<void(const rwc::webcam_ctrl_property&)> callback);
    static void on_webcam_properties_reseted(std::function<void()> callback);

private:
    static void make_vertical_separator(float ver_size);

private:
    static inline webcam_preview_settings m_preview_settings = {};
    static inline std::function<void(bool)> m_aot_sig;
    static inline std::function<void(size_t)> m_wdc_sig;
    static inline std::function<void(size_t)> m_renderer_changed_sig;
    static inline std::function<void(size_t)> m_resolution_changed_sig;
    static inline std::function<void(size_t)> m_format_changed_sig;
    static inline std::function<void(size_t)> m_fps_changed_sig;
    static inline std::function<void(size_t)> m_vsync_changed_sig;
    static inline std::function<void(const rwc::webcam_ctrl_property&)> m_webcam_property_changed_sig;
    static inline std::function<void()> m_webcam_reset_sig;
    static inline bool m_canvas_loading{ false };
    static inline bool show_save_notification{ false };
    static inline bool m_frame_started = false;
    static inline auto save_notification_start{ std::chrono::steady_clock::now() };
};

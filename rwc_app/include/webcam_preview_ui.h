#pragma once

#include <rwc/core/webcam_device.hpp>

#include <imgui.h>
#include <SDL3/SDL_render.h>

#include <utility>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <any>

enum class color_theme
{
    light,
    dark,
    classic
};

enum class ui_event_type : std::uint32_t
{
    always_on_top,
    webcam_device_changed,
    webcam_resolution_changed,
    webcam_format_changed,
    webcam_fps_changed,
    vsync_changed,
    webcam_property_changed,
    webcam_reset_changed,
    webcam_enable_hor_flip,
    renderer_changed,
    font_size_changed
};

static constexpr float BaseFontSize = 20.0f;

struct webcam_preview_settings
{
    float default_font_scale = 1.0f;
    ::color_theme color_theme = color_theme::light;
    bool frame_border = false;
    float frame_round = 0.0f;
    float window_round = 0.0f;
    float dpi_scale = 1.0f;
    float curr_font_size = BaseFontSize;
    bool hide_sidebar = false;
    bool window_always_on_top = false;
    bool keep_aspect_ratio = false;
    bool enable_dpi_scaling = true;
    bool enable_hor_flip = false;

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
    using signal_callback = std::function<void(void)>;

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
    static void create_colorpicker_ui();

    template <typename... Args, typename Func>
    static void dispatch(ui_event_type type, Func&& func)
    {
        std::function fn{ std::forward<Func>(func) };
        m_event_map[type] = std::any(std::move(fn));
    }

    template <typename... Args>
    static void notify(ui_event_type type, Args... args) 
    {
        if (auto it = m_event_map.find(type); it != m_event_map.end()) 
        {
            std::any& wrapper = it->second;
            auto* fn = std::any_cast<std::function<void(Args...)>>(&wrapper);
            if (fn) 
            {
                std::invoke(*fn, std::forward<Args>(args)...);
            }
            else 
            {
                throw std::runtime_error("Event argument types do not match registered handler.");
            }
        }
    }

private:
    static void make_vertical_separator(float ver_size);

private:
    static inline std::unordered_map<ui_event_type, std::any> m_event_map;
    static inline webcam_preview_settings m_preview_settings = {};
    static inline bool m_canvas_loading{ false };
    static inline bool show_save_notification{ false };
    static inline bool m_frame_started = false;
    static inline bool m_show_framerate_overlay = true;
    static inline float m_overlay_opacity = 0.70f;
    static inline auto save_notification_start{ std::chrono::steady_clock::now() };
};

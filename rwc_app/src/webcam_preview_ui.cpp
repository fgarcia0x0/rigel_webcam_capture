#include "webcam_preview_ui.h"
#include "SDL3/SDL_mouse.h"
#include "icons_fork_awesome.hpp"
#include "rwc/core/webcam_device.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

#include <SDL3/SDL_render.h>

#include <rwc/core/webcam_utils.h>

#include <vector>
#include <string>
#include <cstdint>
#include <numbers>

#include "fonts.hpp"
#include "icons_fork_awesome.hpp"

static bool s_need_update_renderer = false;

bool MakeCollapsingHeader(const char* label)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2());
    bool* p_open = ImGui::GetStateStorage()->GetBoolRef(ImGui::GetID(label), false);
    if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f)))
        *p_open ^= 1;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 arrow_pos = ImVec2(ImGui::GetItemRectMax().x - style.FramePadding.x - ImGui::GetFontSize(), ImGui::GetItemRectMin().y + style.FramePadding.y);
    ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrow_pos, ImGui::GetColorU32(ImGuiCol_Text), *p_open ? ImGuiDir_Down : ImGuiDir_Right);
    ImGui::PopStyleVar(3);
    return *p_open;
}

template <auto ScaleFunction = ImGui::GetFontSize>
static inline float scaled_width(float value, float base_size = 13.0f)
{
    return (value / base_size) * ScaleFunction();
};
 
static inline bool combobox_getter(void* vec, int idx, const char** out_text) 
{
    auto& vector = *static_cast<std::vector<std::string> *>(vec);
    if (idx < 0 || idx >= static_cast<int>(vector.size()))
        return false;
    *out_text = vector[idx].c_str();
    return true;
}

static inline bool show_combobox(const char* label, std::vector<std::string>& container, int& current_index) 
{
    return ImGui::Combo(label, &current_index, combobox_getter, static_cast<void *>(&container), int(container.size()));
}


struct toggle_button_settings
{
    uint32_t circle_color = IM_COL32(255, 255, 255, 255);
    uint32_t text_color = ImGui::GetColorU32(ImGuiCol_Text);
    float anim_speed = 0.085f;
    float rect_radius = 0.50f;
    float circle_radius = 0.50f;
    float width = 1.90f;
    float label_spacing = 0.5f;
};

[[maybe_unused]]
static inline bool ToggleButton(const char* str_id, bool* value, const toggle_button_settings& settings = {})
{
    ImVec4* colors = ImGui::GetStyle().Colors;
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float height = ImGui::GetFrameHeight();
    float width = height * settings.width;
    float rect_radius = height * settings.rect_radius;
    float circle_radius = height * settings.circle_radius;

    std::string_view label = str_id;
    size_t pos = label.find_first_of('#');
    if (pos != std::string_view::npos)
        label = label.substr(0, pos);
    else
        pos = label.size();

    ImVec2 rect_pos(cursor_pos.x + width, cursor_pos.y + height);
    ImVec2 text_size = ImGui::CalcTextSize(label.data(), label.data() + pos);
    ImVec2 text_pos = ImVec2(rect_pos.x + (settings.label_spacing * height), cursor_pos.y + (height - text_size.y) * 0.4f);
    ImRect total_bb(cursor_pos, ImVec2(text_pos.x + text_size.x, cursor_pos.y + height));

    ImGui::InvisibleButton(str_id, ImVec2(total_bb.GetWidth(), total_bb.GetHeight()));
    bool item_clicked = ImGui::IsItemClicked();
    if (item_clicked) 
        *value = !*value;

    ImGuiContext& gg = *GImGui;
    float time = *value ? 1.0f : 0.0f;

    if (gg.LastActiveId == gg.CurrentWindow->GetID(str_id))
    {
        float time_anim = ImSaturate(gg.LastActiveIdTimer / settings.anim_speed);
        time = *value ? (time_anim) : (1.0f - time_anim);
    }

    ImVec2 circle_pos(cursor_pos.x + circle_radius + time * (width - circle_radius * 2.0f), cursor_pos.y + circle_radius);

    if (ImGui::IsItemHovered())
        draw_list->AddRectFilled(cursor_pos, rect_pos, ImGui::GetColorU32(*value ? colors[ImGuiCol_ButtonActive] : colors[ImGuiCol_FrameBgHovered]), rect_radius);
    else
        draw_list->AddRectFilled(cursor_pos, rect_pos, ImGui::GetColorU32(*value ? colors[ImGuiCol_ButtonHovered] : colors[ImGuiCol_Border]), rect_radius);
    
    draw_list->AddCircleFilled(circle_pos, circle_radius - 1.5f, settings.circle_color);

    if (!label.empty())
        draw_list->AddText(text_pos, settings.text_color, label.data(), label.data() + pos);

    return item_clicked;
}

bool webcam_preview_ui::initialize(SDL_Window* window, SDL_Renderer* renderer)
{
    if (m_frame_started)
        finish_frame();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; 
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // load font with default scale (1.0f)
    webcam_preview_ui::reload_fonts_at_scale(1.0f);

    return true;
}

void webcam_preview_ui::update_settings()
{
    if (m_frame_started)
        finish_frame();

    auto& io = ImGui::GetIO();
    io.FontGlobalScale = m_preview_settings.default_font_scale;

    // update window properties
    auto& style = ImGui::GetStyle();
    style.FrameRounding = style.GrabRounding = m_preview_settings.frame_round;
    style.FrameBorderSize = m_preview_settings.frame_border ? 1.0f : 0.0f;
    style.WindowRounding = m_preview_settings.window_round;

    // update theme
    auto theme = m_preview_settings.color_theme;
    if (theme == color_theme::light)
        ImGui::StyleColorsLight();
    else if (theme == color_theme::dark)
        ImGui::StyleColorsDark();
    else
        ImGui::StyleColorsClassic();
}

webcam_preview_settings& webcam_preview_ui::settings() noexcept
{
    return m_preview_settings;
}

bool webcam_preview_ui::process_events(const SDL_Event* event)
{
    return ImGui_ImplSDL3_ProcessEvent(event);
}

void webcam_preview_ui::render(SDL_Renderer* renderer)
{
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}

void webcam_preview_ui::start_frame()
{
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    m_frame_started = true;
}

void webcam_preview_ui::finish_frame()
{
    ImGui::Render();
    m_frame_started = false;
}

static bool show_demo_window = true;

[[maybe_unused]]
static inline void make_spinner(float radius, float thickness, ImU32 color, float rotation_speed = 8.0f)
{
    float padding = thickness;
    ImVec2 size((radius + padding) * 2, (radius + padding) * 2);

    // Obtém tamanho da viewport atual (área da janela principal)
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Centraliza o spinner na viewport
    ImVec2 center(viewport->Pos.x + (viewport->Size.x - size.x) * 0.5f,
                  viewport->Pos.y + (viewport->Size.y - size.y) * 0.5f);

    ImGui::SetNextWindowPos(center);
    ImGui::SetNextWindowSize(size);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
      
    ImGui::Begin("##CanvasSpinner", nullptr,
                ImGuiWindowFlags_NoDecoration | 
                ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 pos = ImGui::GetCursorScreenPos();

    const float start_angle = float(ImGui::GetTime()) * rotation_speed;
    const float end_angle = start_angle + std::numbers::pi_v<float> * 1.5f;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PathClear();

    ImVec2 center_pos = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    constexpr uint32_t NUM_SEGMENTS = 32;

    for (uint32_t i = 0; i < NUM_SEGMENTS; i++)
    {
        float angle = start_angle + (i / float(NUM_SEGMENTS)) * (end_angle - start_angle);
        draw_list->PathLineTo(ImVec2(center_pos.x + cosf(angle) * radius,
                                     center_pos.y + sinf(angle) * radius));
    }

    draw_list->PathStroke(color, 0, thickness);
    ImGui::End();
    ImGui::PopStyleVar();
}

struct spinner_params
{
    float radius = 50.0f;
    float thickness = 6.0f;
    ImU32 color = IM_COL32(255, 255, 255, 255);
    float rotation_speed = 8.0f;
    float cycle_duration = 1.5f;
    float min_angle = 0.10f;
    float max_angle = 1.50f;
    float backg_opacity = 0.65f;
};

[[maybe_unused]]
static inline void make_animated_spinner(const spinner_params& params = {})
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 full_size = viewport->Size;
    ImVec2 center = ImVec2(viewport->Pos.x + full_size.x * 0.5f, viewport->Pos.y + full_size.y * 0.5f);

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(full_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);

    ImGui::Begin("ModalSpinner", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PopStyleVar(2);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Draw translucent background
    draw_list->AddRectFilled(viewport->Pos,
                             ImVec2(viewport->Pos.x + full_size.x, viewport->Pos.y + full_size.y),
                             IM_COL32(0, 0, 0, std::ceilf(params.backg_opacity * 255)));

    // Spinner arc
    float time = float(ImGui::GetTime());
    float start_angle = time * params.rotation_speed;

    // Novo controle do comprimento do arco (suave e sincronizado)
    float cycle_duration = params.cycle_duration;
    float t = fmodf(time, cycle_duration) / cycle_duration; // 0 → 1
    float arc_ratio = 0.5f + 0.5f * sinf(t * 2.0f * std::numbers::pi_v<float>);
    float end_angle = start_angle + std::numbers::pi_v<float> * (params.min_angle + params.max_angle * arc_ratio);

    constexpr uint32_t NUM_SEGMENTS = 32;
    draw_list->PathClear();

    for (uint32_t i = 0; i < NUM_SEGMENTS; ++i)
    {
        float a = start_angle + (i / float(NUM_SEGMENTS)) * (end_angle - start_angle);
        draw_list->PathLineTo(ImVec2(center.x + cosf(a) * params.radius,
                                     center.y + sinf(a) * params.radius));
    }

    draw_list->PathStroke(params.color, 0, params.thickness);
    ImGui::End();
}

static void DrawTextWithBorder(ImDrawList* draw_list, ImVec2 pos, ImU32 text_color, ImU32 border_color, const char* text, float font_scale = 1.0f) 
{
    ImGui::SetWindowFontScale(font_scale);

    const float thickness = 1.0f;

    // Offsets to create an outline effect (around 8 directions)
    const ImVec2 offsets[] = {
        {-thickness, -thickness}, {0, -thickness}, {thickness, -thickness},
        {-thickness,  0},                      {thickness,  0},
        {-thickness,  thickness}, {0, thickness}, {thickness,  thickness}
    };

    for (const ImVec2& offset : offsets) {
        draw_list->AddText(ImVec2(pos.x + offset.x, pos.y + offset.y), border_color, text);
    }

    // Draw actual text on top
    draw_list->AddText(pos, text_color, text);

    ImGui::SetWindowFontScale(1.0f);
}

void webcam_preview_ui::update_ui()
{
    start_frame();

    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H))
        m_preview_settings.hide_sidebar = !m_preview_settings.hide_sidebar;

    // show demo
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    if (!m_preview_settings.hide_sidebar && !is_canvas_loading())
    {
        static ImVec2 last_window_size = {};

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float sidebar_base_width = 400;
        ImGui::SetNextWindowSize({ scaled_width(sidebar_base_width), viewport->WorkSize.y });

        // Calculate the position before creating the window
        ImVec2 viewport_pos = viewport->WorkPos;
        ImVec2 viewport_size = viewport->WorkSize;
        ImVec2 window_pos = ImVec2(viewport_pos.x + viewport_size.x - last_window_size.x, viewport_pos.y);
        ImGui::SetNextWindowPos(window_pos);
        
        auto sidebar_flags =  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("Rigel Webcam Capture - Use Ctrl+H to hide the sidebar", nullptr, sidebar_flags))
        {
            last_window_size = ImGui::GetWindowSize();

            if (ImGui::CollapsingHeader(ICON_FK_WINDOW_RESTORE " Window Settings", ImGuiTreeNodeFlags_DefaultOpen))
            {
                make_vertical_separator(0.2f);
                create_window_section();
            }

            make_vertical_separator(0.2f);

            if (ImGui::CollapsingHeader(ICON_FK_CAMERA " Webcam Settings", ImGuiTreeNodeFlags_DefaultOpen))
            {
                make_vertical_separator(0.2f);
                create_webcam_settings_section();
            }

            make_vertical_separator(0.2f);

            if (ImGui::CollapsingHeader(ICON_FK_BRIEFCASE " Webcam Controls", ImGuiTreeNodeFlags_DefaultOpen))
            {
                make_vertical_separator(0.2f);
                create_webcam_controls_section();
            }

            make_vertical_separator(0.2f);

            if (ImGui::CollapsingHeader(ICON_FK_LINE_CHART " Frametime", ImGuiTreeNodeFlags_DefaultOpen))
            {
                make_vertical_separator(0.2f);
                create_frametime_section();
            }
        }

        ImGui::End();
    }

    if (m_show_framerate_overlay)
    {
        static float last_update = 0.0f;
        static float accumulator = 0.0f;
        static size_t frame_count = 0;
        static float framerate = 0.0f;
        static float avg_framerate = 0.0f;

        static float history[128] = {};
        static size_t index = 0;

        // Time since last update
        float current_time = float(ImGui::GetTime());
        float delta_time = current_time - last_update;

        float fps = ImGui::GetIO().Framerate;
        accumulator += fps;
        frame_count++;

        // Update every 1 second
        if (delta_time >= 1.0f)
        {
            avg_framerate = accumulator / frame_count;
            framerate = ImGui::GetIO().Framerate;
            last_update = current_time;
            accumulator = 0.0f;
            frame_count = 0;
        }

        history[index] = fps;
        index = (index + 1) % std::size(history);

        // Compute dynamic min/max from history
        float range_min = 0.0f;
        float range_max = 240.0f;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
        ImVec2 window_pos = ImVec2(ImGui::GetStyle().DisplayWindowPadding);
        ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
        
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        ImGui::SetNextWindowBgAlpha(m_overlay_opacity);
        
        ImGui::Begin("FPS Overlay", nullptr, flags);
        ImGui::Text("Frame Rate: %u fps", uint32_t(std::roundf(framerate)));
        ImGui::Text("Frame Time: %.2f ms", 1000.0f / avg_framerate);

        make_vertical_separator(0.2f);
        ImGui::PlotLines("##frame_time_graph", history, int(std::size(history)), int(index), nullptr, range_min, range_max);

        ImGui::End();
    }

    if (show_save_notification)
    {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - save_notification_start).count();

        if (elapsed < 1.5f)
        {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.7f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            ImVec2 viewportPos = ImGui::GetMainViewport()->GetCenter();
            ImVec2 viewportSize = ImGui::GetMainViewport()->Size;

            ImGui::SetNextWindowPos(viewportPos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(viewportSize.x, 0));

            ImGui::Begin("##SaveNotification", nullptr,
                         ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav);

            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            const char* title = "Frame captured and saved successfully !";
            ImVec2 text_size = ImGui::CalcTextSize(title);
            float xpadding = (viewportSize.x - text_size.x * 2.0f) * 0.5f;
            float ypadding = (viewportSize.y - text_size.y * 2.0f) * 0.5f;

            DrawTextWithBorder(ImGui::GetForegroundDrawList(), ImVec2(xpadding, ypadding), IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255), title, 1.8f);
            ImGui::End();
        }
        else 
        {
            show_save_notification = false;
        }
    }

    if (s_need_update_renderer)
    {
        s_need_update_renderer = false;
        notify<size_t>(ui_event_type::renderer_changed, m_preview_settings.selected_render_api);
    }

    if (is_canvas_loading())
        make_animated_spinner();

    finish_frame();
}

enum RefreshRateType : int
{
    RR_NO_VSYNC,
    RR_WEBCAM_VSYNC,
    RR_SCREEN_VSYNC
};

void webcam_preview_ui::create_window_section()
{
    auto& window_themes = m_preview_settings.theme_labels;
    static int selected_window_theme = 0;
    static int refresh_rate_type = RefreshRateType::RR_NO_VSYNC;
    auto& selected_render_api = settings().selected_render_api;
    
    ImGui::Text("Window Theme: ");
    if (show_combobox("##combo_window_theme", window_themes, selected_window_theme))
    {
        const auto& window_theme = window_themes[selected_window_theme];
        if (window_theme == "light")
        {
            settings().color_theme = color_theme::light;
            ImGui::StyleColorsLight();
        }
        else if (window_theme == "dark")
        {
            ImGui::StyleColorsDark();
            settings().color_theme = color_theme::dark;
        }
        else if (window_theme == "classic")
        {
            ImGui::StyleColorsClassic();
            settings().color_theme = color_theme::classic;
        }
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Always on top", &m_preview_settings.window_always_on_top))
        notify<bool>(ui_event_type::always_on_top, m_preview_settings.window_always_on_top);

    make_vertical_separator(0.5f);

    // group #2
    ImGui::BeginGroup();

    ImGui::Text("Renderer API: ");
    if (show_combobox("##combo_renderer_api", settings().render_apis, selected_render_api))
    {
        s_need_update_renderer = true;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Keep Aspect Ratio", &m_preview_settings.keep_aspect_ratio);

    ImGui::EndGroup();

    make_vertical_separator(0.5f);
    ImGui::Text("Refresh Rate: ");
    ImGui::Dummy(ImVec2(0.0f, 3.0f));

    if (ImGui::RadioButton("No VSync", &refresh_rate_type, RefreshRateType::RR_NO_VSYNC))
    {
        notify<size_t>(ui_event_type::vsync_changed, 0);
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("Webcam VSync", &refresh_rate_type, RefreshRateType::RR_WEBCAM_VSYNC))
    {
        notify<size_t>(ui_event_type::vsync_changed, 1);
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("Screen VSync", &refresh_rate_type, RefreshRateType::RR_SCREEN_VSYNC))
    {
        notify<size_t>(ui_event_type::vsync_changed, 2);
    }

    make_vertical_separator(0.5f);
    ImGui::Text("Font Config: ");
    make_vertical_separator(0.1f);

    if (ImGui::DragFloat("##FontSize", &settings().curr_font_size, 1.0f, 10.0f, 80.0f, "Font Size: %.0fpx", ImGuiSliderFlags_AlwaysClamp))
        notify<float>(ui_event_type::font_size_changed, settings().curr_font_size / BaseFontSize);
}

void webcam_preview_ui::create_webcam_settings_section()
{
    auto& settings = m_preview_settings;
    ImGui::Text("Select Webcam Device: ");

    if (show_combobox("##combo_webcam_device", settings.webcam_devices, settings.selected_webcam_device))
    {
        notify<size_t>(ui_event_type::webcam_device_changed, settings.selected_webcam_device);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    ImGui::Text("Select Resolution: ");
    if (show_combobox("##combo_resolution", settings.resolution_labels, settings.selected_resolution))
    {
        notify<size_t>(ui_event_type::webcam_resolution_changed, settings.selected_resolution);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    ImGui::Text("Select Format: ");
    if (show_combobox("##combo_format", settings.format_labels, settings.selected_format))
    {
        notify<size_t>(ui_event_type::webcam_format_changed, settings.selected_format);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    ImGui::Text("Select FPS: ");
    if (show_combobox("##combo_fps", settings.fps_labels, settings.selected_fps))
    {
        notify<size_t>(ui_event_type::webcam_fps_changed, settings.selected_fps);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    if (ToggleButton("Horizontal Flip", &settings.enable_hor_flip))
    {
        notify<bool>(ui_event_type::webcam_enable_hor_flip, settings.enable_hor_flip);
    }
}

void webcam_preview_ui::create_webcam_controls_section()
{
    if (ImGui::BeginTable("CameraControls", 3, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Slider");
        ImGui::TableSetupColumn("Auto");

        auto& webcam_props = settings().webcam_properties;

        for (size_t i = 0; i < webcam_props.size(); ++i)
        {
            auto name = rwc::webcam_utils::prop_type_to_string(webcam_props[i].type);
            ImGui::TableNextRow();

            if (webcam_props[i].is_auto)
                ImGui::BeginDisabled();

            // Column 0: Name
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name.c_str());
            ImGui::SetNextItemWidth(-1);

            // Column 1: Slider
            ImGui::TableSetColumnIndex(1);
            bool trigger_item_action = false;
            std::string id = "##webcam_prop_" + std::to_string(i);
            const char* slider_fmt = "%d";
            auto slider_flags = ImGuiSliderFlags_ClampOnInput;

            if (webcam_props[i].type == rwc::webcam_property_type::power_line_freq)
            {
                constexpr size_t plf_mode_count = 3;
                constexpr const char* plf_names[plf_mode_count] = { "None", "50Hz", "60Hz" };
                int32_t value = webcam_props[i].value;
                slider_fmt = (value >= 0 && value < plf_mode_count) ? plf_names[value] : "Unknown";
                slider_flags = ImGuiSliderFlags_NoInput;
            }

            if (ImGui::SliderInt(id.c_str(), &webcam_props[i].value, webcam_props[i].minimum, webcam_props[i].maximum, slider_fmt, slider_flags))
                trigger_item_action = true;

            if (webcam_props[i].is_auto)
                ImGui::EndDisabled();

            if (!webcam_props[i].support_auto)
                ImGui::BeginDisabled();

            // Column 3: Auto checkbox
            ImGui::TableSetColumnIndex(2);
            std::string check_id = "auto##auto" + std::to_string(i);

            if (ToggleButton(check_id.c_str(), &webcam_props[i].is_auto))
                trigger_item_action = true;

            if (trigger_item_action)
            {
                notify<const rwc::webcam_ctrl_property&>(ui_event_type::webcam_property_changed, webcam_props[i]);
            }

            if (!webcam_props[i].support_auto)
                ImGui::EndDisabled();
        }

        ImGui::EndTable();
    }

    const std::string text = std::format("{} Reset Controls", ICON_FK_REPEAT);
    float buttonWidth = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 6;
    float buttonHeight = ImGui::CalcTextSize(text.c_str()).y + ImGui::GetStyle().FramePadding.y * 6;

    ImVec2 button_size = ImVec2(buttonWidth, buttonHeight);
    float region_width = ImGui::GetContentRegionAvail().x;

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (region_width - button_size.x) * 0.5f);

    if (ImGui::Button(text.c_str(), button_size))
    {
        ImGui::OpenPopup("Webcam Controls");
    }

    // Always center this window when appearing
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Webcam Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::Text("Do you want reset all webcam controls ?");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        const float panel_width = ImGui::GetContentRegionAvail().x;
        const float width = panel_width * 0.5f - ImGui::GetStyle().FramePadding.x * 2.0f;

        if (ImGui::Button("OK", ImVec2(width, 0))) 
        { 
            notify(ui_event_type::webcam_reset_changed);
            ImGui::CloseCurrentPopup(); 
        }

        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        
        if (ImGui::Button("Cancel", ImVec2(width, 0))) 
            ImGui::CloseCurrentPopup(); 

        ImGui::EndPopup();
    }
}

void webcam_preview_ui::create_frametime_section()
{
    static int overlay_opacity = 70;
    make_vertical_separator(0.1f);

    if (ImGui::BeginTable("##overlay", 3, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("overlay_name");
        ImGui::TableSetupColumn("overlay_opacity");
        ImGui::TableSetupColumn("overlay_toggle");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Overlay");

        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##Opacity", &overlay_opacity, 0, 100, "Opacity: %d%%"))
            m_overlay_opacity = overlay_opacity / 100.0f;

        ImGui::TableSetColumnIndex(2);
        ToggleButton("Enable##overlay", &m_show_framerate_overlay);

        ImGui::EndTable();
    }

    make_vertical_separator(0.2f);
}

void webcam_preview_ui::make_vertical_separator(float ver_size)
{
    ver_size *= ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0.0f, ver_size));
}

void webcam_preview_ui::shutdown()
{
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

const ImVec2& webcam_preview_ui::get_frame_buffer_scale()
{
    return ImGui::GetIO().DisplayFramebufferScale;   
}

void webcam_preview_ui::set_canvas_loading(bool state)
{
    m_canvas_loading = state;
}

bool webcam_preview_ui::is_canvas_loading()
{
    return m_canvas_loading;
}

void webcam_preview_ui::trigger_save_notification()
{
    show_save_notification = true;
    save_notification_start = std::chrono::steady_clock::now();
}

void webcam_preview_ui::reload_fonts_at_scale(float scale)
{
    float new_size = std::roundf(BaseFontSize * scale);
    settings().curr_font_size = new_size;
    auto& io = ImGui::GetIO();

    io.Fonts->Clear();

    // load normal fonts
    std::span firacode_regular = std::span{ rigelapp::fonts::compressed::firacode_regular_compressed_data };
    ImFontConfig config = {};
    config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryCompressedTTF(firacode_regular.data(), int(firacode_regular.size()), new_size, &config);

    // load icon font
    auto font_icons = std::span{ rigelapp::fonts::compressed::fork_awesome_regular_compressed_data };
    float icon_font_size = new_size;

    config.MergeMode = true;
    config.PixelSnapH = true;
    config.GlyphMinAdvanceX = icon_font_size;
    constexpr ImWchar icon_ranges[] = { ICON_MIN_FK, ICON_MAX_16_FK, 0 };

    io.Fonts->AddFontFromMemoryCompressedTTF(font_icons.data(), int(font_icons.size()), icon_font_size, &config, icon_ranges);

    ImGui_ImplSDLRenderer3_DestroyFontsTexture();
    ImGui_ImplSDLRenderer3_CreateFontsTexture();
}

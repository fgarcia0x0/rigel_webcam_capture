#include "rigel_app.h"
#include "webcam_preview_ui.h"

#include <rwc/core/webcam_device.hpp>
#include <rwc/core/webcam_manager.h>
#include <rwc/core/webcam_utils.h>
#include <rwc/logger/logger.h>
#include <rwc/logger/console_sink.h>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <optional>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <set>
#include <cstddef>
#include <format>
#include <utility>

static constexpr auto SDL_WINDOW_DELAY_MS = 50;

static std::unordered_map<renderer_api, std::string_view> s_renderer_api_map = 
{
    { renderer_api::automatic,   "gpu" },
    { renderer_api::opengl,      "opengl" },
    { renderer_api::opengl_es,   "opengles" },
    { renderer_api::vulkan,      "vulkan" },
    { renderer_api::direct3d11,  "direct3d11" },
    { renderer_api::direct3d12,  "direct3d12" },
    { renderer_api::metal,       "metal" }
};

struct sdl_texture_info
{
    int width = 0;
    int height = 0;
    SDL_PixelFormat pixel_format = SDL_PIXELFORMAT_UNKNOWN;
    SDL_TextureAccess access = SDL_TEXTUREACCESS_TARGET;
};

static inline std::string fourcc_to_string(uint32_t value)
{
    char buffer[sizeof(uint32_t) + 1] = {};
    std::memcpy(buffer, &value, sizeof(uint32_t));
    return std::string(buffer, sizeof(uint32_t));
}

template <typename T>
static inline void remove_duplicates(std::vector<T>& container)
{
    std::set<T> unique_items(container.begin(), container.end());
    container = std::vector<T>(unique_items.begin(), unique_items.end());
}

[[maybe_unused]]
static inline std::optional<sdl_texture_info> get_texture_info(SDL_Texture* texture)
{
    if (!texture)
        return std::nullopt;

    sdl_texture_info info = {};
    float width = 0;
    float height = 0;

    SDL_GetTextureSize(texture, &width, &height);
    info.width = static_cast<int>(width);
    info.height = static_cast<int>(height);

    auto props = SDL_GetTextureProperties(texture);
    auto format = SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_FORMAT_NUMBER, SDL_PIXELFORMAT_UNKNOWN);
    auto access = SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);

    info.pixel_format = static_cast<SDL_PixelFormat>(format);
    info.access = static_cast<SDL_TextureAccess>(access);
    
    return info;
}

[[maybe_unused]]
static inline SDL_Texture* sdl_duplicate_texture(SDL_Texture* tex, SDL_Renderer* renderer) 
{
    auto tex_info = get_texture_info(tex);
    if (!tex_info)
        return nullptr;

    // Get all properties from the texture we are duplicating
    SDL_BlendMode blend_mode = {};
    SDL_GetTextureBlendMode(tex, &blend_mode);

    // Save the current rendering target (will be NULL if it is the current window)
    SDL_Texture* render_target = SDL_GetRenderTarget(renderer);

    // Create a new texture with the same properties as the one we are duplicating
    SDL_Texture* new_texture = SDL_CreateTexture(renderer, tex_info->pixel_format, tex_info->access, 
                                                 tex_info->width, tex_info->height);

    // Set its blending mode and make it the render target
    SDL_SetTextureBlendMode(new_texture, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer, new_texture);

    // Render the full original texture onto the new one
    SDL_RenderTexture(renderer, tex, NULL, NULL);

    // Change the blending mode of the new texture to the same as the original one
    SDL_SetTextureBlendMode(new_texture, blend_mode);

    // Restore the render target
    SDL_SetRenderTarget(renderer, render_target);

    // Return the new texture
    return new_texture;
}

static inline bool verify_window_visibility(SDL_Window* window)
{
    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    bool is_hidden = flags & SDL_WINDOW_HIDDEN;
    bool is_minimized = flags & SDL_WINDOW_MINIMIZED;
    return !is_hidden && !is_minimized;
}

static inline bool upload_data_to_texture(SDL_Texture* texture, const rwc::webcam_frame& frame)
{
    if (frame.format == rwc::webcam_pixel_format::nv12)
    {
        // frame.buffer is a Y plane (width*height) immediately followed by an
        // interleaved U,V plane (width*(height/2)), both stride == width -
        // exactly what SDL_UpdateNVTexture expects.
        const uint8_t* y_plane = frame.buffer.get();
        const uint8_t* uv_plane = y_plane + size_t(frame.width) * frame.height;
        bool ok = SDL_UpdateNVTexture(texture, nullptr, y_plane, int(frame.width), uv_plane, int(frame.width));
        if (!ok)
            RWC_LOG_ERROR("SDL_UpdateNVTexture failed: {}", SDL_GetError());
        return ok;
    }

    void* tex_pixels = nullptr;
    int tex_pitch = 0;

    if (!SDL_LockTexture(texture, nullptr, &tex_pixels, &tex_pitch))
        return false;

    std::memcpy(tex_pixels, frame.buffer.get(), frame.size);
    SDL_UnlockTexture(texture);

    return true;
}

[[maybe_unused]]
static inline std::vector<std::string> get_webcam_devices()
{
    std::vector<std::string> devices;

    auto wcam = rwc::webcam_manager::create_device();
    for (uint32_t index = 0; index < rwc::webcam_manager::device_count(); ++index)
    {
        if (wcam->reset(index) == rwc::webcam_error_status::ok)
        {
            devices.emplace_back(wcam->device_info().name);
        }
    }

    return devices;
}

[[maybe_unused]]
static std::vector<std::string> sdl_get_available_renderers()
{
    std::vector<std::string> renderers;

    for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) 
        renderers.emplace_back(SDL_GetRenderDriver(i));

    return renderers;
}

bool rigel_app::initialize_context()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) 
    {
        RWC_LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext())
        return false;

    return true;
}

void rigel_app::shutdown_context()
{
    ImGui::DestroyContext();

    m_texture.reset();
    m_renderer.reset();
    m_window.reset();
    
    SDL_Quit();
}

void rigel_app::shutdown()
{
    m_running = false;
    
    if (m_renderer)
        webcam_preview_ui::shutdown();

    shutdown_context();
}

rigel_app::rigel_app(rigel_app_specs specs)
    : m_window(nullptr, SDL_DestroyWindow)
    , m_renderer(nullptr, SDL_DestroyRenderer)
    , m_texture(nullptr, SDL_DestroyTexture)
    , m_app_specs(std::move(specs))
{
}

rigel_app::~rigel_app()
{
    shutdown();
}

bool rigel_app::create_window(std::string_view title, int width, int height)
{
    if (m_window)
        m_window.reset();

    constexpr SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_window.reset(SDL_CreateWindow(title.data(), width, height, window_flags));
    
    if (!m_window) 
    {
        RWC_LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    if (SDL_Surface* icon = SDL_LoadBMP("assets/icons/webcam-icon-256x256.bmp"))
    {
        SDL_SetWindowIcon(m_window.get(), icon);
        SDL_DestroySurface(icon);
    }

    return true;
}

bool rigel_app::create_renderer(std::string_view render_api)
{
    if (m_renderer)
        m_renderer.reset();

    // create renderer with the specified API
    m_renderer.reset(SDL_CreateRenderer(m_window.get(), render_api.data()));
    if (!m_renderer)
    {
        RWC_LOG_ERROR("SDL_CreateRenderer failed: {}", SDL_GetError());
        return false;
    }

    return true;
}

void rigel_app::choose_window_size(float scale, int* out_width, int* out_height)
{
    int display_count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
    SDL_DisplayID primary_display = displays[0];
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(primary_display);

    int width = int(mode->w * scale);
    int height = int(mode->h * scale);
    
    if (out_width)
        *out_width = width;

    if (out_height)
        *out_height = height;
}

bool rigel_app::setup_window_and_renderer(std::string_view title, int width, int height, std::string_view renderer_name)
{
    return create_window(title, width, height) && create_renderer(renderer_name);
}

void rigel_app::show_window()
{
    SDL_SetWindowPosition(m_window.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(m_window.get());
}

void rigel_app::setup_ui_settings()
{
    static bool ui_settings_initialized = false;

    webcam_preview_ui::initialize(m_window.get(), m_renderer.get());

    // setup ui properties
    if (!ui_settings_initialized)
    {
        auto& settings = webcam_preview_ui::settings();
        settings.default_font_scale = 1.00f;
        settings.color_theme = color_theme::light;
        settings.frame_border = true;
        settings.frame_round = 4.0f;
        settings.window_round = 4.0f;
        ui_settings_initialized = true;
    }
    
    webcam_preview_ui::update_settings();
}

bool rigel_app::begin_webcam_operation()
{
    if (m_webcam_op_pending)
        return false;

    m_webcam_op_pending = true;
    return true;
}

void rigel_app::end_webcam_operation()
{
    m_webcam_op_pending = false;
}

bool rigel_app::setup_webcam_device(size_t device_index)
{
    auto& wcam = m_webcam_device;
    wcam = rwc::webcam_manager::create_device();
    if (wcam->open(uint32_t(device_index)) != rwc::webcam_error_status::ok)
    {
        end_webcam_operation();
        return false;
    }

    auto on_webcam_stream_started = [this](){
        end_webcam_operation();
        create_webcam_texture();
    };

    auto default_format = wcam->current_format();
    if (wcam->set_current_format(default_format))
    {
        webcam_preview_ui::set_canvas_loading(true);

        m_task_queue.push_task(
            [this](){ m_webcam_device->start_stream();
                      update_webcam_properties();
                      setup_ui_data(); },
            std::move(on_webcam_stream_started)
        );
    }
    else
    {
        end_webcam_operation();
    }

    return true;
}

template <typename... Args>
static inline void dispatch_event(ui_event_type ev, auto* this_ptr, auto&& method)
{
    webcam_preview_ui::dispatch(ev, [this_ptr, method](Args... args) {
        std::invoke(method, this_ptr, std::forward<Args>(args)...);
    });
};

void rigel_app::setup_signal_handlers()
{
    dispatch_event<size_t>(ui_event_type::renderer_changed, this, &rigel_app::renderer_changed_handler);
    dispatch_event<bool>(ui_event_type::always_on_top, this, &rigel_app::aot_changed_handler);
    dispatch_event<size_t>(ui_event_type::webcam_device_changed, this, &rigel_app::webcam_device_changed_handler);
    dispatch_event<size_t>(ui_event_type::webcam_format_changed, this, &rigel_app::webcam_format_changed_handler);
    dispatch_event<size_t>(ui_event_type::webcam_resolution_changed, this, &rigel_app::webcam_resolution_changed);
    dispatch_event<size_t>(ui_event_type::vsync_changed, this, &rigel_app::vsync_changed_handler);
    dispatch_event<const rwc::webcam_ctrl_property&>(ui_event_type::webcam_property_changed, this, &rigel_app::webcam_property_changed_handler);
    dispatch_event(ui_event_type::webcam_reset_changed, this, &rigel_app::webcam_properties_reseted_handler);
    dispatch_event<float>(ui_event_type::font_size_changed, this, &rigel_app::ui_font_size_changed);
    dispatch_event<bool>(ui_event_type::webcam_enable_hor_flip, this, &rigel_app::webcam_image_horflip_changed);
}

bool rigel_app::create_texture(int width, int height, uint32_t pixel_format, SDL_Colorspace colorspace)
{
    if (m_texture)
        m_texture.reset();

    if (colorspace == SDL_COLORSPACE_UNKNOWN)
    {
        m_texture.reset(SDL_CreateTexture(m_renderer.get(), static_cast<SDL_PixelFormat>(pixel_format),
                                          SDL_TEXTUREACCESS_STREAMING, width, height));
    }
    else
    {
        // Only NV12 textures need this: SDL's default colorspace for NV12
        // (SDL_COLORSPACE_JPEG, i.e. full-range) is wrong for our H264
        // frames, which stay studio/limited-range like the H264 stream they
        // came from - see webcam_utils::codec_uses_full_range_yuv. Getting
        // this wrong doesn't fail to render, it just renders with a visibly
        // shifted black level/contrast.
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, pixel_format);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, colorspace);
        m_texture.reset(SDL_CreateTextureWithProperties(m_renderer.get(), props));
        SDL_DestroyProperties(props);
        if (!m_texture)
            RWC_LOG_ERROR("SDL_CreateTextureWithProperties failed: {}", SDL_GetError());
    }

    return !!m_texture;
}

int rigel_app::run()
{
    rwc::logger::instance().add_sink(std::make_shared<rwc::console_sink>());
    rwc::logger::instance().set_min_level(rwc::log_level::info);

    if (!initialize_context())
    {
        RWC_LOG_ERROR("Failed to initialize context");
        return EXIT_FAILURE;
    }

    if (m_app_specs.width <= 0 || m_app_specs.height <= 0)
        choose_window_size(0.75f, &m_app_specs.width, &m_app_specs.height);

    // setup ui events callbacks
    setup_signal_handlers();

    if (!build_render_pipeline(s_renderer_api_map[m_app_specs.render_api]))
    {
        RWC_LOG_ERROR("Failed to build render pipeline for renderer: {}", s_renderer_api_map[m_app_specs.render_api]);
        return EXIT_FAILURE;
    }

    if (!setup_webcam_device())
    {
        RWC_LOG_ERROR("Failed to setup webcam device");
        return EXIT_FAILURE;
    }

    while (m_running)
    {
        process_events();
        if (!m_running)
            break;

        m_task_queue.process_tasks();

        // don't render if the window is not visible
        if (!verify_window_visibility(m_window.get()))
        {
            SDL_Delay(SDL_WINDOW_DELAY_MS);
            continue;
        }

        process_webcam_frame();
        render_frame();
    }

    return EXIT_SUCCESS;
}

SDL_FRect rigel_app::adjust_aspect_ratio(int tex_width, int tex_height)
{
    SDL_FRect dest_rect = {};
    int window_width = 0, window_height = 0;
    SDL_GetRenderOutputSize(m_renderer.get(), &window_width, &window_height);

    // Compute aspect ratios
    float tex_aspect = float(tex_width) / tex_height;
    float win_aspect = float(window_width) / window_height;

    if (win_aspect > tex_aspect) 
    {
        // Window is wider than texture aspect
        dest_rect.h = float(window_height);
        dest_rect.w = tex_aspect * dest_rect.h;
        dest_rect.x = (window_width - dest_rect.w) / 2.0f;
        dest_rect.y = 0;
    } 
    else 
    {
        // Window is taller than texture aspect
        dest_rect.w = float(window_width);
        dest_rect.h = dest_rect.w / tex_aspect;
        dest_rect.x = 0;
        dest_rect.y = (window_height - dest_rect.h) / 2.0f;
    }

    return dest_rect;
}

void rigel_app::process_events()
{
    SDL_Event event = {};

    while (SDL_PollEvent(&event)) 
    {
        webcam_preview_ui::process_events(&event);

        if (event.type == SDL_EVENT_QUIT) 
        {
            m_running = false;
            break;
        }
        else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_window.get()))
        {
            m_running = false;
            break;
        }
        else if (event.type == SDL_EVENT_KEY_DOWN) 
        {
            if (event.key.key == SDLK_F11) 
            {
                bool fullscreen = SDL_GetWindowFlags(m_window.get()) & SDL_WINDOW_FULLSCREEN;
                SDL_SetWindowFullscreen(m_window.get(), !fullscreen);
            }
            else if (event.key.key == SDLK_S && (event.key.mod & SDL_KMOD_CTRL)) 
            {
                save_frame_to_file("webcam_frame.bmp");
            }
        }
        else if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
        {
            float scale = SDL_GetWindowDisplayScale(m_window.get());
            webcam_preview_ui::reload_fonts_at_scale(scale);
        }
    }
}

void rigel_app::process_webcam_frame()
{
    update_texture_warmup();

    if (m_webcam_device && m_webcam_device->has_pending_frame())
    {
        auto frame = m_webcam_device->read_frame();
        if (frame && m_texture)
        {
            bool frame_bounds_changed = frame->width != uint32_t(m_texture->w) || frame->height != uint32_t(m_texture->h);
            if (!frame_bounds_changed)
            {
                upload_data_to_texture(m_texture.get(), *frame);
                m_draw_frame = true;
            }
        }
    }
    else
    {
        m_draw_frame = false;
    }
}

void rigel_app::render_frame()
{
    if (m_webcam_vsync && !m_draw_frame)
        return;

    // update UI
    webcam_preview_ui::update_ui();

    const auto& fb_scale = webcam_preview_ui::get_frame_buffer_scale();
    SDL_SetRenderScale(m_renderer.get(), fb_scale.x, fb_scale.y);
    SDL_SetRenderDrawColor(m_renderer.get(), 50, 50, 50, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(m_renderer.get());
    
    SDL_FRect* dst_rect_ptr = nullptr;
    SDL_FRect dst_rect = {};

    if (webcam_preview_ui::settings().keep_aspect_ratio)
    {
        dst_rect = adjust_aspect_ratio(m_webcam_device->current_format().width, 
                                       m_webcam_device->current_format().height);
        dst_rect_ptr = &dst_rect;
    }

    // While warming up, we still draw the texture every frame (instead of
    // skipping it like the general loading state does) so the GPU driver's
    // YUV shader pipeline actually gets exercised and converges - see
    // update_texture_warmup(). It's then covered below so the user never
    // sees the wrong colors that come out of it during that window.
    if (m_texture && (!webcam_preview_ui::is_canvas_loading() || m_texture_warming_up))
    {
        if (m_image_flipped)
            SDL_RenderTextureRotated(m_renderer.get(), m_texture.get(), nullptr, dst_rect_ptr, 180, nullptr, SDL_FLIP_VERTICAL);
        else
            SDL_RenderTexture(m_renderer.get(), m_texture.get(), nullptr, dst_rect_ptr);
    }

    if (m_texture_warming_up)
    {
        SDL_SetRenderDrawColor(m_renderer.get(), 50, 50, 50, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(m_renderer.get(), dst_rect_ptr);
    }

    webcam_preview_ui::render(m_renderer.get());
    SDL_RenderPresent(m_renderer.get());
}

void rigel_app::save_frame_to_file(std::string_view filepath)
{
    SDL_Texture* texture = m_texture.get();
    SDL_Surface* surface = nullptr;
    
    if (SDL_LockTextureToSurface(texture, nullptr, &surface))
    {
        // NV12 (or any other FourCC/YUV) surfaces can't be saved as BMP
        // directly - convert to RGB24 first, via SDL's own (software, if
        // needed) colorspace-aware conversion.
        if (SDL_ISPIXELFORMAT_FOURCC(surface->format))
        {
            SDL_Surface* rgb_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGB24);
            if (rgb_surface)
            {
                SDL_SaveBMP(rgb_surface, filepath.data());
                SDL_DestroySurface(rgb_surface);
            }
            else
            {
                RWC_LOG_ERROR("Failed to convert frame to RGB24 for saving: {}", SDL_GetError());
            }
        }
        else
        {
            SDL_SaveBMP(surface, filepath.data());
        }

        SDL_UnlockTexture(texture);
        webcam_preview_ui::trigger_save_notification();
    }
    else 
    {
        RWC_LOG_ERROR("Failed to lock texture to surface: {}", SDL_GetError());
    }
}

void rigel_app::renderer_changed_handler(size_t renderer_index)
{
    auto& renderer_apis = webcam_preview_ui::settings().render_apis;

    if (!renderer_apis.empty())
    {
        std::string_view renderer_name = webcam_preview_ui::settings().render_apis[renderer_index];
    
        if (!build_render_pipeline(renderer_name))
        {
            RWC_LOG_ERROR("Failed to build render pipeline for renderer: {}", renderer_name);
            std::exit(EXIT_FAILURE);
        }

        webcam_preview_ui::set_canvas_loading(true);
        create_webcam_texture();
        RWC_LOG_INFO("Renderer changed to: {}", renderer_name);
    }
}

void rigel_app::aot_changed_handler(bool state)
{
    SDL_SetWindowAlwaysOnTop(m_window.get(), state);
}

bool rigel_app::build_render_pipeline(std::string_view renderer_name)
{
    if (m_renderer)
        webcam_preview_ui::shutdown();

    if (!setup_window_and_renderer(m_app_specs.title, m_app_specs.width, m_app_specs.height, renderer_name))
    {
        RWC_LOG_ERROR("Failed to create window and renderer: {}", SDL_GetError());
        return false;
    }

    setup_ui_settings();
    show_window();

    return true;
}

void rigel_app::setup_ui_data()
{
    // setup renderer APIs
    auto& settings = webcam_preview_ui::settings();
    auto& render_apis = settings.render_apis;
    render_apis.clear();
    for (auto&& renderer : sdl_get_available_renderers())
    {
        render_apis.push_back(std::move(renderer));
    }

    std::string_view current_renderer;
    if (m_renderer)
        current_renderer = SDL_GetRendererName(m_renderer.get());
    else
        current_renderer = s_renderer_api_map[m_app_specs.render_api];

    auto iter = std::find(render_apis.begin(), render_apis.end(), current_renderer);
    if (iter != render_apis.end())
        settings.selected_render_api = static_cast<int>(std::distance(render_apis.begin(), iter));

    // setup webcam devices
    settings.webcam_devices = get_webcam_devices();

    // setup webcam formats
    settings.format_labels.clear();
    for (const auto& format : m_webcam_device->device_info().formats)
        settings.format_labels.push_back(fourcc_to_string(format.codec));

    remove_duplicates(settings.format_labels);

    auto code = m_webcam_device->current_format().codec;
    auto fmt_iter = std::find(settings.format_labels.begin(), settings.format_labels.end(), fourcc_to_string(code));
    if (fmt_iter != settings.format_labels.end())
        settings.selected_format = static_cast<int>(std::distance(settings.format_labels.begin(), fmt_iter));

    // setup webcam resolutions
    auto& resolution_labels = webcam_preview_ui::settings().resolution_labels;
    auto& fps_labels = webcam_preview_ui::settings().fps_labels;
    resolution_labels.clear();
    fps_labels.clear();

    for (const auto& fmt : m_webcam_device->device_info().formats)
    {
        if (fmt.codec == m_webcam_device->current_format().codec)
        {
            resolution_labels.emplace_back(std::format("{}x{}", fmt.width, fmt.height));
            fps_labels.emplace_back(std::to_string(fmt.fps));
        }
    }

    remove_duplicates(fps_labels);
}

void rigel_app::webcam_device_changed_handler(size_t device_index)
{
    if (!begin_webcam_operation())
        return;

    webcam_preview_ui::set_canvas_loading(true);

    m_task_queue.push_task(
        [this, device_index](){ setup_webcam_device(device_index); }
    );
}

void rigel_app::create_webcam_texture()
{
    auto format = m_webcam_device->current_format();

    if (rwc::webcam_utils::pixel_format_for_codec(format.codec) == rwc::webcam_pixel_format::nv12)
    {
        SDL_Colorspace colorspace = rwc::webcam_utils::codec_uses_full_range_yuv(format.codec)
                                        ? SDL_COLORSPACE_JPEG
                                        : SDL_COLORSPACE_BT601_LIMITED;
        create_texture(int(format.width), int(format.height), SDL_PIXELFORMAT_NV12, colorspace);

        // Only NV12 has the shader warm-up glitch - keep the loading spinner
        // (already showing, set by our caller) up a little longer instead of
        // clearing it immediately.
        m_texture_warming_up = true;
        m_texture_ready_at_ms = SDL_GetTicks();
    }
    else
    {
        create_texture(int(format.width), int(format.height), SDL_PIXELFORMAT_RGB24);
        webcam_preview_ui::set_canvas_loading(false);
    }
}

void rigel_app::update_texture_warmup()
{
    if (!m_texture_warming_up)
        return;

    constexpr Uint64 kWarmUpMs = 2000;
    if (SDL_GetTicks() - m_texture_ready_at_ms >= kWarmUpMs)
    {
        m_texture_warming_up = false;
        webcam_preview_ui::set_canvas_loading(false);
    }
}

void rigel_app::webcam_resolution_changed(size_t res_index)
{
    std::string res_str = webcam_preview_ui::settings().resolution_labels[res_index];
    
    size_t sep_index = res_str.find('x');
    uint32_t res_width = std::stoul(res_str.substr(0, sep_index));
    uint32_t res_height = std::stoul(res_str.substr(sep_index + 1));

    auto new_format_info = m_webcam_device->current_format();
    new_format_info.width = res_width;
    new_format_info.height = res_height; 

    change_webcam_capture_format(new_format_info);
}

void rigel_app::webcam_fps_changed(size_t fps_index)
{
    std::string fps_str = webcam_preview_ui::settings().fps_labels[fps_index];
    uint32_t fps = std::stoul(fps_str);

    auto new_format_info = m_webcam_device->current_format();
    new_format_info.fps = fps;

    change_webcam_capture_format(new_format_info);
}

void rigel_app::webcam_format_changed_handler(size_t format_index)
{
    auto& wcam = m_webcam_device;
    auto& resolution_labels = webcam_preview_ui::settings().resolution_labels;
    auto& fps_labels = webcam_preview_ui::settings().fps_labels;
    std::string_view new_format = webcam_preview_ui::settings().format_labels[format_index];

    webcam_preview_ui::settings().selected_resolution = 0;
    webcam_preview_ui::settings().selected_fps = 0;
    resolution_labels.clear();
    fps_labels.clear();

    uint32_t codec = 0;
    for (const auto& format : wcam->device_info().formats)
    {
        std::string current_format = fourcc_to_string(format.codec);
        if (current_format == new_format)
        {
            codec = format.codec;
            resolution_labels.push_back(std::format("{}x{}", format.width, format.height));
            fps_labels.push_back(std::to_string(format.fps));
        }
    }

    auto new_format_info = rwc::webcam_utils::select_capture_format(wcam->device_info().formats, codec);
    if (new_format_info.has_value())
        change_webcam_capture_format(*new_format_info);
    
    remove_duplicates(fps_labels);
}

void rigel_app::change_webcam_capture_format(const rwc::capture_format_info& new_format_info)
{
    if (!begin_webcam_operation())
        return;

    auto& wcam = m_webcam_device;

    bool is_streaming = wcam->is_streaming();
    if (is_streaming)
        wcam->stop_stream();

    if (wcam->set_current_format(new_format_info) && is_streaming)
    {
        webcam_preview_ui::set_canvas_loading(true);
        m_task_queue.push_task(
            [this](){ m_webcam_device->start_stream(); },
            [this]() { end_webcam_operation();
                       create_webcam_texture(); }
        );
        return;
    }

    end_webcam_operation();
}

void rigel_app::vsync_changed_handler(size_t vsync_index)
{
    m_webcam_vsync = (vsync_index == 1);

    if (vsync_index == 0)
        SDL_SetRenderVSync(m_renderer.get(), 0);
    else if (vsync_index == 2)
        SDL_SetRenderVSync(m_renderer.get(), 1);
}

void rigel_app::update_webcam_properties()
{
    constexpr size_t max_webcam_props = std::to_underlying(rwc::webcam_property_type::last);

    std::vector<rwc::webcam_ctrl_property> webcam_props;
    webcam_props.reserve(max_webcam_props);
    
    for (size_t i = {}; i < max_webcam_props; ++i)
    {
        auto type = static_cast<rwc::webcam_property_type>(i);
        auto props = m_webcam_device->ctrl()->read_property(type);
        if (props.has_value())
        {
            if (props->minimum == props->maximum)
                continue;
            webcam_props.push_back(std::move(props).value());
        }
    }

    webcam_preview_ui::settings().webcam_properties = std::move(webcam_props);
}

void rigel_app::webcam_property_changed_handler(const rwc::webcam_ctrl_property& property)
{
    // propagate exception
    m_webcam_device->ctrl()->write_property(property.type, property.value, property.is_auto);
}

void rigel_app::webcam_properties_reseted_handler()
{
    if (!begin_webcam_operation())
        return;

    webcam_preview_ui::set_canvas_loading(true);

    m_task_queue.push_task(
        [this](){ m_webcam_device->ctrl()->reset_properties();
                  update_webcam_properties();
                  webcam_preview_ui::set_canvas_loading(false); },
        [this](){ end_webcam_operation(); }
    );
}

void rigel_app::ui_font_size_changed(float new_value)
{
    m_task_queue.push_task([](){}, std::bind_front(webcam_preview_ui::reload_fonts_at_scale, new_value));
}

void rigel_app::webcam_image_horflip_changed(bool state)
{
    m_image_flipped = state;
}

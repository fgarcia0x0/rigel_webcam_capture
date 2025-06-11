function(find_imgui TARGET_NAME BACKEND)
    set(IMGUI_DIR ${RWC_BASE_DIR}/vendor/imgui)
    set(IMGUI_TARGET_NAME imgui)

    # Core ImGui sources
    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp)

    # Backend selection
    if(${BACKEND} STREQUAL "sdl3")
        list(APPEND IMGUI_SOURCES
            ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
            ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
            ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp)
    else()
        message(FATAL_ERROR "Unsupported backend: ${BACKEND}")
    endif()

    # Create interface target
    add_library(${TARGET_NAME} STATIC ${IMGUI_SOURCES})

    # Add required include directories
    target_include_directories(${TARGET_NAME} PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
    )

    # SDL3 and OpenGL dependencies assumed linked elsewhere
    add_library(imgui::imgui ALIAS ${TARGET_NAME})
endfunction()

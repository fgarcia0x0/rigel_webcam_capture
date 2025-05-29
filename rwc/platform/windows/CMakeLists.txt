file(GLOB_RECURSE RWC_PLATFORM_WINDOWS_HEADERS
    ${RWC_PLATFORM_DIR}/windows/*.h
    ${RWC_PLATFORM_DIR}/windows/*.hpp
)

file(GLOB_RECURSE RWC_PLATFORM_WINDOWS_SOURCES
    ${RWC_PLATFORM_DIR}/windows/*.cpp
)

target_compile_definitions(rwc PRIVATE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
target_sources(rwc PRIVATE ${RWC_PLATFORM_WINDOWS_HEADERS} ${RWC_PLATFORM_WINDOWS_SOURCES})
target_link_libraries(rwc PRIVATE mfplat mf mfreadwrite mfuuid strmiids wmcodecdspuuid d3d11 dxguid)

# define static crt if is using msvc on windows
if (MSVC AND RWC_ENABLE_STATIC_CRT AND NOT BUILD_SHARED_LIBS)
    message("Links static CRT on rwc lib")
    set_target_properties(rwc PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if (MINGW)
    # $<$<RWC_ENABLE_STATIC_CRT>:-static>
    if (RWC_ENABLE_STATIC_CRT)
        target_link_libraries(rwc PUBLIC -static)
    endif()

    target_link_libraries(rwc PUBLIC stdc++exp)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        find_package(Threads REQUIRED)
        target_link_libraries(rwc PUBLIC Threads::Threads)
    endif()
endif()

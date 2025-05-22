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

target_include_directories(rwc PRIVATE ${SIMDLIB_INCLUDE_DIRS})

if (MINGW)
    target_link_libraries(rwc PRIVATE stdc++exp -static)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_libraries(rwc PRIVATE pthread)
    endif()
endif()

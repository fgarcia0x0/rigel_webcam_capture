file(GLOB_RECURSE RWC_PLATFORM_WINDOWS_HEADERS
    ${RWC_PLATFORM_DIR}/windows/*.h
    ${RWC_PLATFORM_DIR}/windows/*.hpp
)

file(GLOB_RECURSE RWC_PLATFORM_WINDOWS_SOURCES
    ${RWC_PLATFORM_DIR}/windows/*.cpp
)

target_compile_definitions(rwc PRIVATE _CRT_SECURE_NO_WARNINGS)
target_sources(rwc PRIVATE ${RWC_PLATFORM_WINDOWS_HEADERS} ${RWC_PLATFORM_WINDOWS_SOURCES})
target_link_libraries(rwc PRIVATE mfplat mfreadwrite mf mfuuid strmiids wmcodecdspuuid d3d11 dxguid)

if (MINGW)
    target_link_libraries(rwc PRIVATE stdc++exp)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_libraries(rwc PRIVATE pthread)
    endif()
endif()

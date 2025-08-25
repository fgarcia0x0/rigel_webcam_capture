set(RWC_PLATFORM_DIR ${CMAKE_CURRENT_LIST_DIR})

set(RWC_PLATFORM_HEADERS
    ${RWC_PLATFORM_DIR}/platform.hpp
)

set(RWC_PLATFORM_SOURCES)

target_sources(rwc PRIVATE ${RWC_PLATFORM_HEADERS} ${RWC_PLATFORM_SOURCES})

if (RWC_PLATFORM_LINUX)
    add_subdirectory(linux)
elseif (RWC_PLATFORM_WINDOWS)
    add_subdirectory(windows)
endif()

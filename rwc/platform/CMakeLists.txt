set(RWC_PLATFORM_DIR ${CMAKE_CURRENT_LIST_DIR})

file(GLOB RWC_PLATFORM_HEADERS
    ${RWC_PLATFORM_DIR}/*.h
    ${RWC_PLATFORM_DIR}/*.hpp
)

file(GLOB RWC_PLATFORM_SOURCES
    ${RWC_PLATFORM_DIR}/*.cpp
)

target_sources(rwc PRIVATE ${RWC_PLATFORM_HEADERS} ${RWC_PLATFORM_SOURCES})

if (RWC_PLATFORM_LINUX)
    add_subdirectory(linux)
elseif (RWC_PLATFORM_WINDOWS)
    add_subdirectory(windows)
endif()

file(GLOB_RECURSE RWC_PLATFORM_LINUX_HEADERS
    ${RWC_PLATFORM_DIR}/*.h
    ${RWC_PLATFORM_DIR}/*.hpp
)

file(GLOB_RECURSE RWC_PLATFORM_LINUX_SOURCES
    ${RWC_PLATFORM_DIR}/*.cpp
)

# find and links libyuv
find_package(libyuv CONFIG REQUIRED)
target_link_libraries(rwc PRIVATE yuv)

# set hidden by default
set_target_properties(rwc PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

target_sources(rwc PRIVATE ${RWC_PLATFORM_LINUX_HEADERS} ${RWC_PLATFORM_LINUX_SOURCES})
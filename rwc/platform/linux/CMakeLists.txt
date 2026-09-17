file(GLOB_RECURSE RWC_PLATFORM_LINUX_HEADERS
    ${RWC_PLATFORM_DIR}/linux/*.h
    ${RWC_PLATFORM_DIR}/linux/*.hpp
)

file(GLOB_RECURSE RWC_PLATFORM_LINUX_SOURCES
    ${RWC_PLATFORM_DIR}/linux/*.cpp
)

# find and links libyuv
find_package(libyuv CONFIG REQUIRED)
target_link_libraries(rwc PRIVATE yuv)

# find and links libjpeg-turbo (used directly for higher quality MJPG decoding)
find_package(JPEG REQUIRED)
target_link_libraries(rwc PRIVATE JPEG::JPEG)

# find and links openh264 (H264 decoding). It only ships a pkg-config file, no CMake config.
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENH264 REQUIRED IMPORTED_TARGET openh264)
target_link_libraries(rwc PRIVATE PkgConfig::OPENH264)

# set hidden by default
set_target_properties(rwc PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

target_sources(rwc PRIVATE ${RWC_PLATFORM_LINUX_HEADERS} ${RWC_PLATFORM_LINUX_SOURCES})
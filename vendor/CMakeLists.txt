# include imgui
include(${CMAKE_CURRENT_LIST_DIR}/FindImGui.cmake)

# vendor graphics lib
add_library(vendor_graphics INTERFACE)
add_library(vendor::graphics ALIAS vendor_graphics)

add_subdirectory(SDL)
find_imgui(imgui sdl3)
target_link_libraries(vendor_graphics INTERFACE SDL3::SDL3 imgui::imgui)

# vendor test libs
add_library(vendor_test INTERFACE)
add_library(vendor::test ALIAS vendor_test)

set(gtest_force_shared_crt ${BUILD_SHARED_LIBS} CACHE BOOL "Use shared CRT in GoogleTest" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "Build google-test GMOCK" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "Install google-test" FORCE)
add_subdirectory(googletest)

target_link_libraries(vendor_test INTERFACE GTest::gtest_main)

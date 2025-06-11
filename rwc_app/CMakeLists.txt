cmake_minimum_required(VERSION 3.25)
project(rwc_app LANGUAGES CXX)

set(BUILD_SHARED_LIBS OFF)

# define files
file(GLOB_RECURSE RWC_APP_HDR_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

file(GLOB_RECURSE RWC_APP_SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.rc
)

message(STATUS "RWC App Source Files: ${RWC_APP_SRC_FILES}")

# create executable file
add_executable(rwc_app ${RWC_APP_HDR_FILES} ${RWC_APP_SRC_FILES})

# link static
if (MSVC AND RWC_ENABLE_STATIC_CRT)
    set_target_properties(rwc_app PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# set cpp props
set_target_properties(rwc_app PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

# link against the library
target_link_libraries(rwc_app PRIVATE rwc vendor::graphics)

if (MINGW)
    target_link_libraries(rwc_app PRIVATE -lstdc++exp)
endif()

# setup compile flags
target_compile_options(rwc_app PUBLIC ${RWC_COMPILE_FLAGS})

target_include_directories(rwc_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
cmake_minimum_required(VERSION 3.25)
project(rwc_lib LANGUAGES CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# create executable file
add_library(rwc)

# set cpp props
set_target_properties(rwc PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# setup compile flags
target_compile_options(rwc PUBLIC ${RWC_COMPILE_FLAGS})

# remove "lib<name>" prefix
set_target_properties(rwc PROPERTIES PREFIX "")

# include dir for use (internal)
target_include_directories(rwc PUBLIC ${RWC_BASE_DIR})

# compile definitions
target_compile_definitions(rwc PRIVATE RWC_EXPORTS)

# print compiler name
message(STATUS "Target Compiler: " ${CMAKE_CXX_COMPILER_ID})

# add subdirectories
add_subdirectory(core)
add_subdirectory(logger)
add_subdirectory(utils)
add_subdirectory(platform)

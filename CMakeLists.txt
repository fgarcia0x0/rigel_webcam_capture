cmake_minimum_required(VERSION 3.25)
project(rigel-webcam-capture LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build shared libraries instead of static ones" OFF)
option(RWC_ENABLE_TESTING "Enable building of tests" ON)
option(RWC_BUILD_APP "Enable building of rwc gui app" ON)
option(RWC_ENABLE_STATIC_CRT "Enable static CRT on static libs only" ON)

# links crt statically on static libs
if (MSVC AND ${RWC_ENABLE_STATIC_CRT} AND NOT BUILD_SHARED_LIBS)
	message("Enable static CRT")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(RWC_BUILD_TYPE ${CMAKE_BUILD_TYPE})
set(RWC_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(RWC_COMPILE_FLAGS)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	set(RWC_COMPILE_FLAGS -Wall -Wextra -pedantic -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(RWC_COMPILE_FLAGS -Wall -Wextra -Wconversion -Wno-c++98-compat -Wno-c++98-compat-pedantic) 
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
	set(RWC_COMPILE_FLAGS /W3 /utf-8)
endif()

if(UNIX AND NOT APPLE)
    set(RWC_PLATFORM_LINUX TRUE)
elseif (APPLE)
	set(RWC_PLATFORM_MACOS TRUE)
elseif (WIN32)
	set(RWC_PLATFORM_WINDOWS TRUE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define a common output directory, split per build type (bin/debug, bin/release, ...)
if(CMAKE_BUILD_TYPE)
	string(TOLOWER "${CMAKE_BUILD_TYPE}" RWC_BUILD_TYPE_DIR)
else()
	set(RWC_BUILD_TYPE_DIR "debug")
endif()
set(OUTPUT_DIR "${CMAKE_HOME_DIRECTORY}/bin/${RWC_BUILD_TYPE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}" CACHE PATH "Runtime output directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}" CACHE PATH "Library output directory")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}" CACHE PATH "Archive output directory")

add_subdirectory(${RWC_BASE_DIR}/rwc)

if (RWC_BUILD_APP OR RWC_ENABLE_TESTING)
	add_subdirectory(${RWC_BASE_DIR}/vendor)
endif()

if (RWC_BUILD_APP)
	add_subdirectory(${RWC_BASE_DIR}/rwc_app)
endif()

if(RWC_ENABLE_TESTING)
    enable_testing()
    add_subdirectory(${RWC_BASE_DIR}/tests)
endif()

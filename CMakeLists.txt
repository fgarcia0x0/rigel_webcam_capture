cmake_minimum_required(VERSION 3.25)
project(rigel-webcam-capture LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build shared libraries instead of static ones" OFF)
option(RWC_ENABLE_TESTING "Enable building of tests" ON)
option(RWC_BUILD_RWC_APP "Enable building of rwc gui app" ON)
option(RWC_ENABLE_STATIC_CRT "Enable static CRT on static libs only" ON)

set(RWC_BUILD_TYPE ${CMAKE_BUILD_TYPE})
set(RWC_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(RWC_COMPILE_FLAGS)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	set(RWC_COMPILE_FLAGS -Wall -Wextra -pedantic -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(RWC_COMPILE_FLAGS -Wall -Wextra -Wconversion -Wno-c++98-compat -Wno-c++98-compat-pedantic) 
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
	set(RWC_COMPILE_FLAGS /W4 /WX /utf-8)
endif()

if(UNIX AND NOT APPLE)
    set(RWC_PLATFORM_LINUX TRUE)
elseif (APPLE)
	set(RWC_PLATFORM_MACOS TRUE)
elseif (WIN32)
	set(RWC_PLATFORM_WINDOWS TRUE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin/)

add_subdirectory(${RWC_BASE_DIR}/rwc)

if (RWC_BUILD_RWC_APP)
	add_subdirectory(${RWC_BASE_DIR}/rwc_app)
endif()

if(RWC_ENABLE_TESTING)
    enable_testing()
    add_subdirectory(${RWC_BASE_DIR}/tests)
endif()

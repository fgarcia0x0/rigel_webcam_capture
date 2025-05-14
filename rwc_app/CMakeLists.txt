cmake_minimum_required(VERSION 3.25)
project(rwc_app LANGUAGES CXX)

# define files
file(GLOB_RECURSE RWC_APP_HDR_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

file(GLOB_RECURSE RWC_APP_SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

# create executable file
add_executable(rwc_app ${RWC_APP_HDR_FILES} ${RWC_APP_SRC_FILES})

# set cpp props
target_compile_features(rwc_app PUBLIC cxx_std_23)

# link against the library
target_link_libraries(rwc_app PRIVATE rwc)

if (MINGW)
    target_link_libraries(rwc_app PRIVATE -lstdc++exp)
endif()

# setup compile flags
target_compile_options(rwc_app PUBLIC ${RWC_COMPILE_FLAGS})

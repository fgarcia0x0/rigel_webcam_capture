include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        main
)

set(BUILD_GMOCK ON)
set(INSTALL_GTEST OFF)
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

FetchContent_MakeAvailable(googletest)

add_executable(webcam_ctrl_test webcam_ctrl_test.cpp)

target_link_libraries(webcam_ctrl_test PRIVATE GTest::gtest_main rwc)

include(GoogleTest)
gtest_discover_tests(webcam_ctrl_test WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin)

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# The test suite must be linked static
if (MSVC AND RWC_ENABLE_STATIC_CRT AND NOT BUILD_SHARED_LIBS)
    message("Links static crt for test suite")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# These must also come BEFORE FetchContent_MakeAvailable()
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(googletest)

# create executable
add_executable(webcam_ctrl_test webcam_ctrl_test.cpp)

# link targets
target_link_libraries(webcam_ctrl_test PRIVATE GTest::gtest_main rwc)

include(GoogleTest)
gtest_discover_tests(webcam_ctrl_test
    WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin
)
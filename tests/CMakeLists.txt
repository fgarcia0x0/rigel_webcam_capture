# create executable
add_executable(webcam_ctrl_test webcam_ctrl_test.cpp)

# set properties
set_target_properties(webcam_ctrl_test PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

# link targets
target_link_libraries(webcam_ctrl_test PRIVATE vendor::test rwc)

# put attached dlls in the same dir as binary
if (WIN32 AND BUILD_SHARED_LIBS)
    add_custom_command(
        TARGET webcam_ctrl_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:webcam_ctrl_test> $<TARGET_FILE_DIR:webcam_ctrl_test>
        COMMAND_EXPAND_LISTS
    )
endif()

include(GoogleTest)
gtest_discover_tests(webcam_ctrl_test
                     WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin)
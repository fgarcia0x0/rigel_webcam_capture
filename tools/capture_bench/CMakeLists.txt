add_executable(capture_bench capture_bench.cpp)

set_target_properties(capture_bench PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

target_link_libraries(capture_bench PRIVATE rwc)

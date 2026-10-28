cmake_minimum_required(VERSION 3.20)
project(reims_sunshine_bridge LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

set(QMDP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(QMDP_ENABLE_REAL_QEMU_E2E OFF CACHE BOOL "" FORCE)
set(QMDP_ENABLE_DMABUF_READBACK OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../qsm-rd/qsm-direct
                 ${CMAKE_CURRENT_BINARY_DIR}/qmdp)
set(BUILD_TESTING ON)

add_library(reims_capture_bridge STATIC src/capture_feed.cpp src/input_route.cpp src/cursor_blend.cpp)
target_include_directories(reims_capture_bridge PUBLIC src)
target_link_libraries(reims_capture_bridge PUBLIC qmdp_core)

add_executable(reims_capture_tests tests/capture_feed_tests.cpp)
target_link_libraries(reims_capture_tests PRIVATE reims_capture_bridge qmdp_mock)
add_test(NAME reims_capture_tests COMMAND reims_capture_tests)

add_executable(reims_input_tests tests/input_route_tests.cpp)
target_link_libraries(reims_input_tests PRIVATE reims_capture_bridge qmdp_mock)
add_test(NAME reims_input_tests COMMAND reims_input_tests)

add_executable(reims_audio_queue_tests tests/audio_queue_tests.cpp)
target_link_libraries(reims_audio_queue_tests PRIVATE reims_capture_bridge)
add_test(NAME reims_audio_queue_tests COMMAND reims_audio_queue_tests)

add_executable(reims_cursor_blend_tests tests/cursor_blend_tests.cpp)
target_link_libraries(reims_cursor_blend_tests PRIVATE reims_capture_bridge)
add_test(NAME reims_cursor_blend_tests COMMAND reims_cursor_blend_tests)

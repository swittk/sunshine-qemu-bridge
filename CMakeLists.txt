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

add_library(reims_capture_bridge STATIC src/capture_feed.cpp)
target_include_directories(reims_capture_bridge PUBLIC src)
target_link_libraries(reims_capture_bridge PUBLIC qmdp_core)

add_executable(reims_capture_tests tests/capture_feed_tests.cpp)
target_link_libraries(reims_capture_tests PRIVATE reims_capture_bridge qmdp_mock)
add_test(NAME reims_capture_tests COMMAND reims_capture_tests)

# Compile the real Sunshine QEMU capture source against a narrow API-contract
# stub, independently of Sunshine's large third-party build dependency tree.
# This is a compile-surface check, NOT a replacement for a full Sunshine build.
add_library(sunshine_qemu_backend_contract OBJECT
    ../Sunshine/src/platform/linux/qemudbusgrab.cpp)
target_include_directories(sunshine_qemu_backend_contract BEFORE PRIVATE
    tests/stubs
    ../qsm-rd/qsm-direct/src
    src)
target_compile_options(sunshine_qemu_backend_contract PRIVATE -Wall -Wextra -Wpedantic)

# Prism FM engine standalone validation harness (framework-free; no JUCE/DPF).
# Self-contained: does NOT touch ../CMakeLists.txt or the MultiSynthDSP build.
cmake_minimum_required(VERSION 3.15)
project(prism_fm_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# core/ is two levels up (tests/fm -> tests -> core).
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(fm_test fm_test.cpp)
target_include_directories(fm_test PRIVATE ${CORE_DIR})
target_compile_options(fm_test PRIVATE -O2 -Wall -Wextra)

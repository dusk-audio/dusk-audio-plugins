# Multi-Synth core offline validation harness (framework-free; no JUCE/DPF).
cmake_minimum_required(VERSION 3.15)
project(multisynth_core_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CORE_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SHARED_DSP ${CMAKE_CURRENT_SOURCE_DIR}/../../../shared-dpf/dsp)

add_executable(render_test
    render_test.cpp
    ${CORE_DIR}/MultiSynthDSP.cpp)

target_include_directories(render_test PRIVATE ${CORE_DIR} ${SHARED_DSP})
target_compile_options(render_test PRIVATE -O2 -Wall -Wextra)

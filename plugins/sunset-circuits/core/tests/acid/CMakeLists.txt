# Acid engine standalone offline validation harness (framework-free; no JUCE/DPF).
# Self-contained: builds acid_test directly against the header-only AcidEngine.
cmake_minimum_required(VERSION 3.15)
project(acid_core_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# core/ holds AcidEngine.hpp + SynthCommon/Oscillator/Envelope headers.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(acid_test acid_test.cpp)
target_include_directories(acid_test PRIVATE ${CORE_DIR})
target_compile_options(acid_test PRIVATE -O2 -Wall -Wextra)

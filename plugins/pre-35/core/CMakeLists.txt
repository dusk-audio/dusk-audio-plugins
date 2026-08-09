# PRE-35 core — framework-free DSP (no JUCE, no DPF, no external dependencies).
#
# Standalone by design, so the model can be validated without building a plugin:
#
#     cmake -S plugins/pre-35/core -B build/pre35
#     cmake --build build/pre35 -j
#     ctest --test-dir build/pre35 --output-on-failure
#
# The DPF shell (phase 2) links pre35_core and adds nothing to this file.
cmake_minimum_required(VERSION 3.15)
project(pre35_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PRE35_BUILD_TESTS "Build the PRE-35 core gates" ON)
option(PRE35_BUILD_TOOLS "Build the PRE-35 offline render harness" ON)

# Every target here compiles with the same warning set. -Wall -Wextra is the
# floor, not an aspiration: this is DSP nobody can eyeball for correctness, so
# the compiler gets to keep every complaint it has.
add_library(pre35_warnings INTERFACE)
if(MSVC)
    target_compile_options(pre35_warnings INTERFACE /W4)
else()
    target_compile_options(pre35_warnings INTERFACE -Wall -Wextra)
endif()

add_library(pre35_core STATIC Pre35DSP.cpp)
target_include_directories(pre35_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pre35_core PRIVATE pre35_warnings)
set_target_properties(pre35_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(PRE35_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PRE35_BUILD_TOOLS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tools ${CMAKE_CURRENT_BINARY_DIR}/tools)
endif()

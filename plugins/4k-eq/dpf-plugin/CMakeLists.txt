# Standalone CMake for the DPF-based 4K EQ 2 plugin.
# Configure this directory directly:
#   cmake -B build -GNinja [-DDPF_PATH=/path/to/DPF]
#   cmake --build build

cmake_minimum_required(VERSION 3.15)
project(FourKEQ2DPF VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-dpf/cmake/DuskDpfPlugin.cmake")

dpf_add_plugin(four_k_eq_2
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    # MONOLITHIC: DISTRHO_PLUGIN_WANT_DIRECT_ACCESS makes DPF's LV2 TTL export
    # declare the UI inside the dsp binary; the LV2 build must match or hosts
    # fail to load. Single-object LV2 also gives the meters/spectrum direct access.
    MONOLITHIC
    UI_TYPE opengl
    FILES_DSP
        FourKEQPlugin.cpp
        ../core/FourKEQDSP.cpp
    FILES_UI
        FourKEQUI.cpp
        ${DUSK_DPF_UI_SOURCES})

target_include_directories(four_k_eq_2 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    ${DUSK_DPF_INCLUDE_DIRS})

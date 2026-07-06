# Standalone CMake project for the DPF-based Tape Echo plugin.
# Not wired into the top-level JUCE build; configure this directory directly:
#
#   cmake -B build -GNinja [-DDPF_PATH=/path/to/DPF]
#   cmake --build build

cmake_minimum_required(VERSION 3.15)
project(TapeEchoDPF VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(DPF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../DPF" CACHE PATH "Path to DISTRHO Plugin Framework")
set(DPFWIDGETS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../DPF-Widgets" CACHE PATH "Path to DPF-Widgets (Dear ImGui wrapper)")
if(NOT EXISTS "${DPF_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "DPF not found at ${DPF_PATH} — clone https://github.com/DISTRHO/DPF or pass -DDPF_PATH=...")
endif()
if(NOT EXISTS "${DPFWIDGETS_PATH}/opengl/DearImGui.cpp")
    message(FATAL_ERROR "DPF-Widgets not found at ${DPFWIDGETS_PATH} — clone https://github.com/DISTRHO/DPF-Widgets or pass -DDPFWIDGETS_PATH=...")
endif()

add_subdirectory("${DPF_PATH}" dpf EXCLUDE_FROM_ALL)

dpf_add_plugin(tape_echo
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    # MONOLITHIC: DISTRHO_PLUGIN_WANT_DIRECT_ACCESS makes DPF's LV2 TTL export
    # declare the UI inside the dsp binary; the LV2 build must match or hosts
    # fail to load. Single-object LV2 also gives the VU meter direct access.
    MONOLITHIC
    UI_TYPE opengl
    FILES_DSP
        TapeEchoPlugin.cpp
        ../core/TapeEchoDSP.cpp
    FILES_UI
        TapeEchoUI.cpp
        "${DPFWIDGETS_PATH}/opengl/DearImGui.cpp")

target_include_directories(tape_echo PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-dpf/ui"  # shared DuskImGuiFont.hpp
    "${DPFWIDGETS_PATH}/opengl")

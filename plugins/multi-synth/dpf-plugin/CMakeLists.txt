# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DPF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DPF-based Multi-Synth plugin (Phase 4: DSP shell +
# Dear ImGui UI — DISTRHO_PLUGIN_HAS_UI 1, UI_TYPE opengl, FILES_UI wired below).
#   cmake -B build -GNinja [-DDPF_PATH=/path/to/DPF]
#   cmake --build build

cmake_minimum_required(VERSION 3.15)
project(MultiSynthDPF VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-dpf/cmake/DuskDpfPlugin.cmake")

dpf_add_plugin(multi_synth
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    # MONOLITHIC: DISTRHO_PLUGIN_WANT_DIRECT_ACCESS makes DPF's LV2 TTL export
    # declare the UI inside the dsp binary; the LV2 build must match or hosts
    # fail to load. Also gives the Phase-4 meter/scope bridge direct access.
    MONOLITHIC
    UI_TYPE opengl
    FILES_DSP
        MultiSynthPlugin.cpp
        ../core/MultiSynthDSP.cpp
    FILES_UI
        MultiSynthUI.cpp
        ${DUSK_DPF_UI_SOURCES})

target_include_directories(multi_synth PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    ${DUSK_DPF_INCLUDE_DIRS})   # shared-dpf dsp/ui + DPF-Widgets opengl

# Auto-deploy the built CLAP/VST3/LV2 into the user plugin dirs after each build
# (disable with -DDUSK_DPF_INSTALL_LOCAL=OFF).
dusk_dpf_install_local(multi_synth)

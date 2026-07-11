# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DPF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DPF-based Sunset Circuits plugin (Phase 4: DSP shell +
# Dear ImGui UI — DISTRHO_PLUGIN_HAS_UI 1, UI_TYPE opengl, FILES_UI wired below).
#   cmake -B build -GNinja [-DDPF_PATH=/path/to/DPF]
#   cmake --build build

cmake_minimum_required(VERSION 3.15)

# Single-source version: mirror the JUCE fleet pattern (cmake/PluginVersion.cmake).
# Priority: CI PLUGIN_VERSION_OVERRIDE env -> exact git tag sunset-circuits-vX.Y.Z ->
# SUNSETCIRCUITS_DEFAULT_VERSION fallback below. The parsed value feeds project(),
# the C++ getVersion()/d_version(...) (via SC_VERSION_* compile defs) and the UI
# nameplate tooltip, so the tag, binary, and displayed version can never drift.
# Works standalone (CMAKE_SOURCE_DIR = this dir, inside the git repo) or nested.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/PluginVersion.cmake")
set(SUNSETCIRCUITS_DEFAULT_VERSION "1.0.0")
get_plugin_version("sunset-circuits" SUNSETCIRCUITS_VERSION)
if(NOT SUNSETCIRCUITS_VERSION OR SUNSETCIRCUITS_VERSION STREQUAL "0.0.0")
    set(SUNSETCIRCUITS_VERSION ${SUNSETCIRCUITS_DEFAULT_VERSION})
endif()

project(SunsetCircuitsDPF VERSION ${SUNSETCIRCUITS_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-dpf/cmake/DuskDpfPlugin.cmake")

dpf_add_plugin(sunset_circuits
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

target_include_directories(sunset_circuits PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    ${DUSK_DPF_INCLUDE_DIRS})   # shared-dpf dsp/ui + DPF-Widgets opengl

# Version reaches both the DSP shell (getVersion()/d_version) and the UI nameplate
# tooltip. PUBLIC on the base target propagates to the -dsp and -ui static libs
# (they link sunset_circuits PUBLIC), so every format binary sees the same value.
target_compile_definitions(sunset_circuits PUBLIC
    SC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SC_VERSION_STRING="${PROJECT_VERSION}")

# On older glibc (Ubuntu 20.04 / glibc 2.31, our release baseline) shm_open/
# shm_unlink live in librt and pthread_* in libpthread as separate objects that
# pugl (DPF's windowing) references; newer glibc folded them into libc, so a
# modern dev box links without them. Link them explicitly on Linux so the release
# containers link too. Applied to the final module targets where pugl resolves.
if(UNIX AND NOT APPLE)
    foreach(_fmt clap vst3 lv2 jack)
        if(TARGET sunset_circuits-${_fmt})
            # --no-as-needed (emitted before the link libraries) forces librt /
            # libpthread to stay on the line even though they appear before the
            # dgl archive that references shm_*/pthread_*; being shared libs they
            # then resolve those refs regardless of archive order.
            target_link_options(sunset_circuits-${_fmt} PRIVATE "LINKER:--no-as-needed")
            target_link_libraries(sunset_circuits-${_fmt} PRIVATE rt pthread)
        endif()
    endforeach()
endif()

# Auto-deploy the built CLAP/VST3/LV2 into the user plugin dirs after each build
# (disable with -DDUSK_DPF_INSTALL_LOCAL=OFF).
dusk_dpf_install_local(sunset_circuits)

# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DAF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DAF-based 4K EQ 2 plugin.
# Configure this directory directly:
#   cmake -B build -GNinja [-DDAF_PATH=/path/to/DAF]
#   cmake --build build

cmake_minimum_required(VERSION 3.15)
project(FourKEQ2DAF VERSION 1.0.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/cmake/DuskDafPlugin.cmake")

daf_add_plugin(4k-eq-2
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    # MONOLITHIC: DAF_PLUGIN_WANT_DIRECT_ACCESS makes DAF's LV2 TTL export
    # declare the UI inside the dsp binary; the LV2 build must match or hosts
    # fail to load. Single-object LV2 also gives the meters/spectrum direct access.
    MONOLITHIC
    UI_TYPE opengl
    FILES_DSP
        FourKEQPlugin.cpp
        ../../shared-daf/dsp/FourKEQDSP.cpp
    FILES_UI
        FourKEQUI.cpp
        ${DUSK_DAF_UI_SOURCES})

target_include_directories(4k-eq-2 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp"
    ${DUSK_DAF_INCLUDE_DIRS})

# Single source of truth for the plugin version: the project() VERSION above is
# plumbed into getVersion() and the UI nameplate via these macros (see
# FourKEQVersion.hpp). Bumped by the /release-plugin skill. PUBLIC because DAF
# compiles the UI in a separate 4k-eq-2-ui target that links this one and
# must see the same values — PRIVATE would leave the UI on the header's stale
# fallback.
target_compile_definitions(4k-eq-2 PUBLIC
    FOURKEQ2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FOURKEQ2_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FOURKEQ2_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# Auto-deploy the built CLAP/VST3/LV2 into the user plugin dirs after each build
# (disable with -DDUSK_DAF_INSTALL_LOCAL=OFF).
dusk_daf_install_local(4k-eq-2 LV2_PROGRAMS)

# Out Peak L/R are output parameters; guard them against reaching the host as
# parameter edits, in both VST3 (dusk-audio/plugins#233) and CLAP (#231). The
# harnesses dlopen() the built binaries, so they only run where they are built.
dusk_daf_add_output_param_tests(4k-eq-2)
dusk_daf_add_ui_drag_test(4k-eq-2 258 274)   # LF gain knob
dusk_daf_add_resize_test(4k-eq-2 684 23)     # GRAPH button collapses the design

# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DAF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DAF-based Multi-Q 2 plugin.
#   cmake -B build -GNinja [-DDAF_PATH=/path/to/DAF]
#   cmake --build build

cmake_minimum_required(VERSION 3.15)
project(MultiQ2DAF VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/cmake/DuskDafPlugin.cmake")

daf_add_plugin(multi-q-2
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    # MONOLITHIC: DAF_PLUGIN_WANT_DIRECT_ACCESS makes DAF's LV2 TTL export
    # declare the UI inside the dsp binary; the LV2 build must match or hosts
    # fail to load. Single-object LV2 also gives the UI direct DSP access.
    MONOLITHIC
    UI_TYPE opengl
    FILES_DSP
        MultiQPlugin.cpp
        ../core/MultiQDSP.cpp
        ../core/MultiQMatch.cpp               # Match character (spectrum-match EQ + FIR conv)
        ../../shared-daf/dsp/FourKEQDSP.cpp   # British character core (header-only Tube needs no cpp)
    FILES_UI
        MultiQUI.cpp
        ${DUSK_DAF_UI_SOURCES})

target_include_directories(multi-q-2 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    ${DUSK_DAF_INCLUDE_DIRS})

# Single source of truth for host metadata and crash-log diagnostics.
target_compile_definitions(multi-q-2 PUBLIC
    MQ2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MQ2_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MQ2_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# Auto-deploy the built CLAP/VST3/LV2 into the user plugin dirs after each build
# (disable with -DDUSK_DAF_INSTALL_LOCAL=OFF).
dusk_daf_install_local(multi-q-2 LV2_PROGRAMS)

# Fleet gates. Multi-Q 2 registered no ctest at all; see #148.
#
# No output-parameter gates: Multi-Q 2 exposes no output parameters. Its IN/OUT
# meters are read straight off the live DSP through MultiQAccess.hpp rather than
# published to the host, so the harness correctly reports that it would pass
# vacuously.
dusk_daf_add_ui_drag_test(multi-q-2 323 618)   # Digital-mode GAIN knob
dusk_daf_add_resize_test(multi-q-2)

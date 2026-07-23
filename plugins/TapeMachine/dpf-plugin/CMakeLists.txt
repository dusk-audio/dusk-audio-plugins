# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DPF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DPF-based TapeMachine 2 plugin.
#   cmake -B build -GNinja [-DDPF_PATH=/path/to/DPF]
#   cmake --build build

cmake_minimum_required(VERSION 3.15)
project(TapeMachine2DPF VERSION 1.0.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-dpf/cmake/DuskDpfPlugin.cmake")

dpf_add_plugin(tape_machine_2
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    MONOLITHIC
    UI_TYPE opengl
    FILES_DSP
        TapeMachinePlugin.cpp
        ../core/TapeMachineDSP.cpp
    FILES_UI
        TapeMachineUI.cpp
        ${DUSK_DPF_UI_SOURCES})

target_include_directories(tape_machine_2 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    ${DUSK_DPF_INCLUDE_DIRS})

# Single source of truth for the plugin version: the project() VERSION above is
# plumbed into getVersion() via these macros (see TapeMachinePlugin.cpp). Bumped
# by the /release-plugin skill (slug tapemachine-2).
target_compile_definitions(tape_machine_2 PRIVATE
    TM2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    TM2_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    TM2_VERSION_PATCH=${PROJECT_VERSION_PATCH})

dusk_dpf_install_local(tape_machine_2)

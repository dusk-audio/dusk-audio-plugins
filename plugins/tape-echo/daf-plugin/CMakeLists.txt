# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DAF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DAF-based Tape Echo plugin.
# Configure this directory directly:
#   cmake -B build -GNinja [-DDAF_PATH=/path/to/DAF]
#   cmake --build build

cmake_minimum_required(VERSION 3.15)
project(TapeEchoDAF VERSION 1.0.6)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/cmake/DuskDafPlugin.cmake")

daf_add_plugin(tape_echo
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    # MONOLITHIC: DAF_PLUGIN_WANT_DIRECT_ACCESS makes DAF's LV2 TTL export
    # declare the UI inside the dsp binary; the LV2 build must match or hosts
    # fail to load. Single-object LV2 also gives the VU meter direct access.
    MONOLITHIC
    UI_TYPE opengl
    FILES_DSP
        TapeEchoPlugin.cpp
        ../core/TapeEchoDSP.cpp
    FILES_UI
        TapeEchoUI.cpp
        ${DUSK_DAF_UI_SOURCES})

target_include_directories(tape_echo PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    ${DUSK_DAF_INCLUDE_DIRS})   # shared-daf dsp/ui + DAF-Widgets opengl

# Single source of truth for the plugin version: the project() VERSION above is
# plumbed into getVersion() and the UI version display via these macros (see
# TapeEchoVersion.hpp). Bumped by the /release-plugin skill. PUBLIC because DAF
# compiles the UI in a separate tape_echo-ui target that links this one and must
# see the same values — PRIVATE would leave the UI on the header's stale fallback.
target_compile_definitions(tape_echo PUBLIC
    TE2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    TE2_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    TE2_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# Auto-deploy the built CLAP/VST3/LV2 into the user plugin dirs after each build
# (disable with -DDUSK_DAF_INSTALL_LOCAL=OFF).
dusk_daf_install_local(tape_echo LV2_PROGRAMS)

if(NOT CMAKE_CROSSCOMPILING)
    # enable_testing() + add_test() are what make this reachable from ctest.
    # Without them the target still BUILDS but nothing ever runs it, so the
    # block-size-invariance and reset assertions it encodes are silently dead.
    enable_testing()
    add_executable(TapeEchoCoreTest
        ../core/tests/TapeEchoDSPTest.cpp
        ../core/TapeEchoDSP.cpp)
    target_include_directories(TapeEchoCoreTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
    target_compile_features(TapeEchoCoreTest PRIVATE cxx_std_17)
    add_test(NAME TapeEchoCore COMMAND TapeEchoCoreTest)

    find_program(DUSK_SERDI_EXECUTABLE serdi)
    if(DUSK_SERDI_EXECUTABLE)
        add_test(NAME DuskFixLv2Presets
            COMMAND ${CMAKE_COMMAND}
                "-DFIX_SCRIPT=${DUSK_SHARED_DAF_CMAKE_DIR}/DuskFixLv2Presets.cmake"
                "-DFIXTURE=${DUSK_SHARED_DAF_CMAKE_DIR}/tests/fixtures/daf-malformed-presets.ttl"
                "-DSERDI_EXECUTABLE=${DUSK_SERDI_EXECUTABLE}"
                "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/lv2-preset-repair-test"
                -P "${DUSK_SHARED_DAF_CMAKE_DIR}/tests/DuskFixLv2PresetsTest.cmake")
    endif()
endif()

# Record VU / Record Peak are output parameters; guard them against reaching the
# host as parameter edits, in both VST3 (dusk-audio/plugins#233) and CLAP
# (#231). The macro handles the cross-compile skip and enable_testing() itself.
dusk_daf_add_output_param_tests(tape_echo)
dusk_daf_add_ui_drag_test(tape_echo 37 232)   # INPUT knob
dusk_daf_add_resize_test(tape_echo)

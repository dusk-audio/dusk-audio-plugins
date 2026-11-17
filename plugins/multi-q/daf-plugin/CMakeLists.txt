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

# Copies the built CLAP/VST3/LV2 into the user plugin dirs after each build
# when configured with -DDUSK_DAF_INSTALL_LOCAL=ON.
dusk_daf_install_local(multi-q-2 LV2_PROGRAMS)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(MultiQCoreFiltersTest ../tests/test_multiq_core_filters.cpp)
    add_test(NAME MultiQCoreFilters COMMAND MultiQCoreFiltersTest)

    add_executable(MultiQSmoothingTest ../tests/test_multiq_smoothing.cpp)
    add_test(NAME MultiQSmoothing COMMAND MultiQSmoothingTest)

    add_executable(MultiQDigitalOversamplingTest
        ../tests/test_multiq_digital_oversampling.cpp
        ../core/MultiQDSP.cpp
        ../core/MultiQMatch.cpp
        ../../shared-daf/dsp/FourKEQDSP.cpp)
    add_test(NAME MultiQDigitalOversampling COMMAND MultiQDigitalOversamplingTest)

    add_executable(MultiQAutoGainTest
        ../tests/test_multiq_auto_gain.cpp
        ../core/MultiQDSP.cpp
        ../core/MultiQMatch.cpp
        ../../shared-daf/dsp/FourKEQDSP.cpp)
    add_test(NAME MultiQAutoGain COMMAND MultiQAutoGainTest)

    add_executable(MultiQEnableCrossfadeTest
        ../tests/test_multiq_enable_crossfade.cpp
        ../core/MultiQDSP.cpp
        ../core/MultiQMatch.cpp
        ../../shared-daf/dsp/FourKEQDSP.cpp)
    add_test(NAME MultiQEnableCrossfade COMMAND MultiQEnableCrossfadeTest)

    add_executable(MultiQMatchStateTest
        ../tests/test_multiq_match_state.cpp
        ../core/MultiQMatch.cpp)
    add_test(NAME MultiQMatchState COMMAND MultiQMatchStateTest)

    find_package(Threads REQUIRED)
    target_link_libraries(MultiQMatchStateTest PRIVATE Threads::Threads)
    add_executable(MultiQMatchSnapshotTest ../tests/test_multiq_match_snapshot.cpp)
    target_link_libraries(MultiQMatchSnapshotTest PRIVATE Threads::Threads)
    add_test(NAME MultiQMatchSnapshot COMMAND MultiQMatchSnapshotTest)

    add_executable(MultiQMatchIRTest
        ../tests/test_multiq_match_ir.cpp
        ../core/MultiQMatch.cpp)
    target_link_libraries(MultiQMatchIRTest PRIVATE Threads::Threads)
    add_test(NAME MultiQMatchIR COMMAND MultiQMatchIRTest)

    add_executable(MultiQHostStateTest
        tests/MultiQHostStateTest.cpp
        ../core/MultiQDSP.cpp
        ../core/MultiQMatch.cpp
        ../../shared-daf/dsp/FourKEQDSP.cpp
        "${DAF_PATH}/daf/src/DafPlugin.cpp")
    target_link_libraries(MultiQHostStateTest PRIVATE multi-q-2 Threads::Threads)
    target_compile_definitions(MultiQHostStateTest PRIVATE DAF_PLUGIN_TARGET_CLAP)
    add_test(NAME MultiQHostState COMMAND MultiQHostStateTest)
    set_tests_properties(MultiQHostState PROPERTIES TIMEOUT 60)

    # The latency a host reads at activation is the delay the plugin renders
    # (oversampled saturation, British, Tube, limiter) and holds while running.
    add_executable(MultiQLatencyTest
        tests/MultiQLatencyTest.cpp
        ../core/MultiQDSP.cpp
        ../core/MultiQMatch.cpp
        ../../shared-daf/dsp/FourKEQDSP.cpp
        "${DAF_PATH}/daf/src/DafPlugin.cpp")
    target_link_libraries(MultiQLatencyTest PRIVATE multi-q-2 Threads::Threads)
    target_compile_definitions(MultiQLatencyTest PRIVATE DAF_PLUGIN_TARGET_CLAP)
    add_test(NAME MultiQLatency COMMAND MultiQLatencyTest)

    # The editor's own knobs and their inline value fields (#337): the
    # production MultiQUI.cpp against Dear ImGui's real input handling, through
    # the window-free host in shared-daf/ui/tests/daf-ui-test-host. That
    # directory comes first so the editor's DafUI.hpp resolves to the host.
    # DAF's Resources.cpp is its DejaVu Sans, which the host puts first in the
    # atlas as DAF does. Needs no window or display; in CI it runs in
    # daf-build.yml's Linux x64 and arm64 jobs (the macOS and Windows jobs
    # build only the plugin targets).
    add_executable(MultiQValueFieldTest
        tests/MultiQValueFieldTest.cpp
        ../../shared-daf/dsp/FourKEQDSP.cpp
        "${DAF_PATH}/dgl/src/Resources.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_draw.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_tables.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_widgets.cpp")
    target_include_directories(MultiQValueFieldTest PRIVATE
        "${DUSK_SHARED_DAF_DIR}/ui/tests/daf-ui-test-host"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"
        ${DUSK_DAF_INCLUDE_DIRS}
        "${DAF_PATH}")   # last, for dgl/src/Resources.hpp by its full path
    target_compile_features(MultiQValueFieldTest PRIVATE cxx_std_17)
    target_compile_definitions(MultiQValueFieldTest PRIVATE
        MQ2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MQ2_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MQ2_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        MULTIQ_UI_TEST_HOME="${CMAKE_CURRENT_BINARY_DIR}/value-field-test-home")
    add_test(NAME MultiQValueField COMMAND MultiQValueFieldTest)
endif()

# Fleet gates. Multi-Q 2 registered no ctest at all; see #148.
#
# No output-parameter gates: Multi-Q 2 exposes no output parameters. Its IN/OUT
# meters are read straight off the live DSP through MultiQAccess.hpp rather than
# published to the host, so the harness correctly reports that it would pass
# vacuously.
dusk_daf_add_ui_drag_test(multi-q-2 323 618)   # Digital-mode GAIN knob
dusk_daf_add_resize_test(multi-q-2)
dusk_daf_add_clap_preset_test(multi-q-2)

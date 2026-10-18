# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DAF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DAF-based 4K EQ 2 plugin.
# Configure this directory directly:
#   cmake -B build -GNinja [-DDAF_PATH=/path/to/DAF]
#   cmake --build build

cmake_minimum_required(VERSION 3.15)
project(FourKEQ2DAF VERSION 1.0.5)

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
# A factory preset states every band and filter in Hz and leaves the legacy
# dials where they are (FourKEQBandFrequency.hpp).
dusk_daf_install_local(4k-eq-2 LV2_PROGRAMS
    LV2_PRESET_OMIT hpf_freq lpf_freq lf_freq lm_freq hm_freq hf_freq)

if(NOT CMAKE_CROSSCOMPILING)
    # The shared FourKEQDSP core: bit identity with the frozen RBJ-only core at
    # the reference design rate, matched designs below it, the Hz API, extremes.
    enable_testing()
    add_executable(FourKEQCoreTest
        ../../shared-daf/dsp/tests/FourKEQDSPTests.cpp
        ../../shared-daf/dsp/tests/reference/FourKEQDSPMainRef.cpp
        ../../shared-daf/dsp/FourKEQDSP.cpp)
    target_include_directories(FourKEQCoreTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
    target_compile_features(FourKEQCoreTest PRIVATE cxx_std_17)
    # The test compares the live core against the frozen reference core bit for
    # bit. Both call the same RBJ designers, but clang merges a sin and a cos of
    # one angle into a single sincos call only where both land in one basic
    # block, which depends on how each core's caller inlined them. Apple's arm64
    # libm sincos differs from sinf/cosf by an ulp for ~0.7% of arguments, so
    # the merge alone broke bit identity on Apple Silicon. Errno-setting math
    # stops clang from merging, so both cores call sinf and cosf. Test-only: the
    # shipped plugin is built without this flag.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(FourKEQCoreTest PRIVATE -fmath-errno)
    endif()
    add_test(NAME FourKEQCore COMMAND FourKEQCoreTest)

    # Band and filter frequency parameters (#288): which of the two parameters
    # wins, factory presets, the read-out, user preset files across the change,
    # and selector writes from two threads at once.
    add_executable(FourKEQPluginLogicTest
        tests/FourKEQPluginLogicTests.cpp
        ../../shared-daf/dsp/FourKEQDSP.cpp)
    target_include_directories(FourKEQPluginLogicTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
    target_compile_features(FourKEQPluginLogicTest PRIVATE cxx_std_17)
    find_package(Threads REQUIRED)
    target_link_libraries(FourKEQPluginLogicTest PRIVATE Threads::Threads)
    add_test(NAME FourKEQPluginLogic COMMAND FourKEQPluginLogicTest)

    # The same through the built CLAP's state and parameter paths: pre-#288
    # states and legacy automation keep their sound, the Hz parameters and the
    # factory programs play the Hz they state. dlopen(), so not on Windows.
    if(NOT WIN32)
        add_executable(FourKEQClapStateTest
            tests/FourKEQClapStateTest.cpp
            ../../shared-daf/dsp/FourKEQDSP.cpp)
        target_include_directories(FourKEQClapStateTest PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp"
            "${DAF_PATH}/daf/src")
        target_compile_features(FourKEQClapStateTest PRIVATE cxx_std_17)
        target_link_libraries(FourKEQClapStateTest PRIVATE ${CMAKE_DL_LIBS})
        add_dependencies(FourKEQClapStateTest 4k-eq-2-clap)
        add_test(NAME FourKEQClapState
                 COMMAND FourKEQClapStateTest "$<TARGET_FILE:4k-eq-2-clap>")

        # LV2 restores and applies presets by writing control ports, and DAF's
        # LV2 wrapper passes on only those that changed, which the CLAP test
        # cannot reach. Needs lilv. On Linux, where CI runs the tests, an LV2
        # build without it fails to configure rather than going green with the
        # guard unregistered.
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(FOURKEQ_LILV QUIET IMPORTED_TARGET lilv-0)
        endif()
        if(TARGET 4k-eq-2-lv2 AND NOT FOURKEQ_LILV_FOUND)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                message(FATAL_ERROR
                    "4k-eq-2: the LV2 state and preset test needs the lilv-0 development "
                    "files (liblilv-dev). Install them, or build without the LV2 format.")
            endif()
            message(STATUS "FourKEQLv2State not registered: lilv-0 development files not found")
        endif()
        if(TARGET 4k-eq-2-lv2 AND FOURKEQ_LILV_FOUND)
            add_executable(FourKEQLv2StateTest
                tests/FourKEQLv2StateTest.cpp
                ../../shared-daf/dsp/FourKEQDSP.cpp)
            target_include_directories(FourKEQLv2StateTest PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
            target_compile_features(FourKEQLv2StateTest PRIVATE cxx_std_17)
            target_link_libraries(FourKEQLv2StateTest PRIVATE PkgConfig::FOURKEQ_LILV)
            add_dependencies(FourKEQLv2StateTest 4k-eq-2-lv2)
            add_test(NAME FourKEQLv2State
                     COMMAND FourKEQLv2StateTest "${CMAKE_BINARY_DIR}/bin/4k-eq-2.lv2")
        endif()
    endif()
endif()

# Out Peak L/R are output parameters; guard them against reaching the host as
# parameter edits, in both VST3 (dusk-audio/plugins#233) and CLAP (#231). The
# harnesses dlopen() the built binaries, so they only run where they are built.
dusk_daf_add_output_param_tests(4k-eq-2)
dusk_daf_add_ui_drag_test(4k-eq-2 258 274)   # LF gain knob
dusk_daf_add_resize_test(4k-eq-2 684 23)     # GRAPH button collapses the design

# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DAF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DAF-based DuskVerb 2 plugin.
#   cmake -B build -GNinja [-DDAF_PATH=/path/to/DAF]
#   cmake --build build
#   ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.15)
project(DuskVerb2DAF VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/cmake/DuskDafPlugin.cmake")

# The reverb engines are shared verbatim with the JUCE build; they are not
# rewritten here. Listing them explicitly (rather than globbing) keeps the two
# builds' translation-unit sets visibly identical.
set(DUSKVERB_ENGINE_SOURCES
    ../src/dsp/FDNReverb.cpp
    ../src/dsp/OctaveGEQDesign.cpp
    ../src/dsp/DattorroTank.cpp
    ../src/dsp/QuadTank.cpp
    ../src/dsp/SixAPTankEngine.cpp
    ../src/dsp/DiffusionStage.cpp
    ../src/dsp/DuskVerbEngine.cpp
    ../src/dsp/OutputDiffusion.cpp
    ../src/dsp/EarlyReflections.cpp
    ../src/dsp/SpringEngine.cpp
    ../src/dsp/NonLinearEngine.cpp
    ../src/dsp/ShimmerEngine.cpp
    ../src/dsp/DattorroPlateVintage.cpp
    ../src/dsp/ReverseRoomEngine.cpp)

# The editor bundles Barlow Condensed so its metrics are identical on every OS.
# The faces live once in plugins/shared-daf/fonts; dusk_daf_embed_font turns them
# into C arrays under the build tree, so this plugin does not commit a second,
# hex-encoded copy of the same 210 KB.
set(DUSKVERB_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
dusk_daf_embed_font(kDvFontSemiBold
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/fonts/BarlowCondensed/BarlowCondensed-SemiBold.ttf"
    "${DUSKVERB_GENERATED_DIR}/DuskVerbFontSemiBold.inc")
dusk_daf_embed_font(kDvFontRegular
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/fonts/BarlowCondensed/BarlowCondensed-Regular.ttf"
    "${DUSKVERB_GENERATED_DIR}/DuskVerbFontRegular.inc")

daf_add_plugin(duskverb-2
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    # MONOLITHIC: DAF_PLUGIN_WANT_DIRECT_ACCESS makes DAF's LV2 TTL export
    # declare the UI inside the dsp binary; the LV2 build must match or hosts
    # refuse to load it. Single-object LV2 also gives the meters direct access.
    MONOLITHIC
    USE_FILE_BROWSER
    UI_TYPE opengl
    FILES_DSP
        DuskVerbPlugin.cpp
        ../core/DuskVerbDSP.cpp
        ../core/DuskVerbPresetEngineConfig.cpp
        ${DUSKVERB_ENGINE_SOURCES}
    FILES_UI
        DuskVerbUI.cpp
        ${DUSK_DAF_UI_SOURCES})

target_include_directories(duskverb-2 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${DUSKVERB_GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp"
    ${DUSK_DAF_INCLUDE_DIRS})

# MATCHING THE JUCE BUILD'S CODEGEN IS A CORRECTNESS REQUIREMENT HERE, not a
# performance preference. The acceptance gate for this port is a per-preset null
# test against the JUCE VST3, and the JUCE target compiles the very same engine
# sources with "-O3 -ffast-math" (plugins/DuskVerb/CMakeLists.txt). Reassociation
# and FMA contraction differences inside a 30-second recursive feedback loop turn
# into an audible residual, so the flags have to agree. Unity builds are also
# forbidden for this plugin for the same reason (project memory: unity build
# breaks DuskVerb).
if(NOT MSVC)
    target_compile_options(duskverb-2-dsp PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-O3 -ffast-math>)
    # Clang can erase even bit-based finite checks under -ffinite-math-only.
    # Keep the original finite-value arithmetic in the adapter, but permit
    # invalid host/state input to be detected. Engine translation units retain
    # their original flags, including denormal and reassociation behavior.
    set_property(SOURCE DuskVerbPlugin.cpp APPEND PROPERTY COMPILE_OPTIONS -fno-finite-math-only)
else()
    target_compile_options(duskverb-2-dsp PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/O2 /fp:fast>)
    set_property(SOURCE DuskVerbPlugin.cpp APPEND PROPERTY COMPILE_OPTIONS /fp:precise)
endif()

# Single source of truth for the plugin version: the project() VERSION above is
# plumbed into getVersion() and the UI nameplate. PUBLIC because DAF compiles the
# UI in a separate duskverb-2-ui target that links this one and must see the same
# values — PRIVATE would leave the UI on the header's stale fallback.
target_compile_definitions(duskverb-2 PUBLIC
    DUSKVERB2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    DUSKVERB2_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    DUSKVERB2_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# Auto-deploy the built CLAP/VST3/LV2 into the user plugin dirs after each build
# (disable with -DDUSK_DAF_INSTALL_LOCAL=OFF).
dusk_daf_install_local(duskverb-2 LV2_PROGRAMS)

if(NOT CMAKE_CROSSCOMPILING)
enable_testing()
    add_executable(DuskUserPresetStoreTest ../../shared-daf/ui/tests/DuskUserPresetStoreTest.cpp)
    target_include_directories(DuskUserPresetStoreTest PRIVATE ../../shared-daf/ui)
    target_compile_features(DuskUserPresetStoreTest PRIVATE cxx_std_17)
    add_test(NAME DuskUserPresetStore COMMAND DuskUserPresetStoreTest)

    add_executable(DuskKnobGestureTest ../../shared-daf/ui/tests/DuskKnobGestureTest.cpp
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_draw.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_tables.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_widgets.cpp")
    target_include_directories(DuskKnobGestureTest PRIVATE "${DAFWIDGETS_PATH}/imgui")
    target_compile_features(DuskKnobGestureTest PRIVATE cxx_std_17)
    add_test(NAME DuskKnobGesture COMMAND DuskKnobGestureTest)

    add_executable(DuskVerbCoreTest
        ../core/tests/DuskVerbCoreTests.cpp
        ../core/DuskVerbDSP.cpp
        ../core/DuskVerbPresetEngineConfig.cpp
        ${DUSKVERB_ENGINE_SOURCES})
    target_include_directories(DuskVerbCoreTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
    target_compile_features(DuskVerbCoreTest PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(DuskVerbCoreTest PRIVATE -O2)
    endif()
    add_test(NAME DuskVerbCore COMMAND DuskVerbCoreTest)

    add_executable(DuskVerbPluginLayerTest
        DuskVerbPluginLayerTests.cpp
        ../core/DuskVerbDSP.cpp
        ../core/DuskVerbPresetEngineConfig.cpp
        ${DUSKVERB_ENGINE_SOURCES})
    target_include_directories(DuskVerbPluginLayerTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
    target_compile_features(DuskVerbPluginLayerTest PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(DuskVerbPluginLayerTest PRIVATE -O2)
    endif()
    add_test(NAME DuskVerbPluginLayer COMMAND DuskVerbPluginLayerTest)

    add_executable(DuskVerbParameterTest DuskVerbParameterTests.cpp)
    add_executable(DuskVerbStateCodecTest DuskVerbStateCodecTests.cpp)
    add_executable(DuskVerbControlStateTest DuskVerbControlStateTests.cpp)
    add_executable(DuskVerbValueTextTest DuskVerbValueTextTests.cpp)
    foreach(testTarget DuskVerbParameterTest DuskVerbStateCodecTest DuskVerbControlStateTest DuskVerbValueTextTest)
        target_include_directories(${testTarget} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../src"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
        if(NOT MSVC)
            target_compile_options(${testTarget} PRIVATE -O3 -ffast-math -fno-finite-math-only)
        endif()
        add_test(NAME ${testTarget} COMMAND ${testTarget})
    endforeach()
    find_package(Threads REQUIRED)
    target_link_libraries(DuskVerbControlStateTest PRIVATE Threads::Threads)
    if(UNIX OR WIN32)
        add_executable(DuskVerbClapTest DuskVerbClapTests.cpp)
        target_include_directories(DuskVerbClapTest PRIVATE "${DAF_PATH}/daf/src")
        target_link_libraries(DuskVerbClapTest PRIVATE ${CMAKE_DL_LIBS})
        add_dependencies(DuskVerbClapTest duskverb-2-clap)
        add_test(NAME DuskVerbClapHosted COMMAND DuskVerbClapTest $<TARGET_FILE:duskverb-2-clap>)
        add_executable(DuskVerbClapLayoutTest DuskVerbClapLayoutTests.cpp)
        target_include_directories(DuskVerbClapLayoutTest PRIVATE "${DAF_PATH}/daf/src")
        target_link_libraries(DuskVerbClapLayoutTest PRIVATE ${CMAKE_DL_LIBS})
        add_dependencies(DuskVerbClapLayoutTest duskverb-2-clap)
        add_test(NAME DuskVerbClapLayouts COMMAND DuskVerbClapLayoutTest $<TARGET_FILE:duskverb-2-clap>)
        add_executable(DuskVerbVst3LayoutTest DuskVerbVst3LayoutTests.cpp)
        target_include_directories(DuskVerbVst3LayoutTest PRIVATE "${DAF_PATH}/daf/src")
        target_link_libraries(DuskVerbVst3LayoutTest PRIVATE ${CMAKE_DL_LIBS})
        add_dependencies(DuskVerbVst3LayoutTest duskverb-2-vst3)
        add_test(NAME DuskVerbVst3Layouts COMMAND DuskVerbVst3LayoutTest $<TARGET_FILE:duskverb-2-vst3>)
        if(UNIX AND TARGET duskverb-2-lv2)
            add_executable(DuskVerbLv2Test DuskVerbLv2Tests.cpp)
            target_include_directories(DuskVerbLv2Test PRIVATE "${DAF_PATH}/daf/src"
                "${CMAKE_CURRENT_SOURCE_DIR}/../src"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
            target_link_libraries(DuskVerbLv2Test PRIVATE ${CMAKE_DL_LIBS})
            add_dependencies(DuskVerbLv2Test duskverb-2-lv2)
            add_test(NAME DuskVerbLv2Hosted COMMAND DuskVerbLv2Test $<TARGET_FILE:duskverb-2-lv2>)
        endif()
    endif()
endif()

# Fleet gates.
#
# dusk_daf_add_output_param_tests is deliberately NOT wired: DuskVerb 2 declares
# no output parameters, so there is nothing for that harness to watch. See the
# rationale in DuskVerbParams.hpp -- an honest peak meter exposed as a readonly
# parameter fails clap-validator's param-set-wrong-namespace test, which CI gates
# with SUITE_STRICT=1 and no allowlist. The meters reach the UI through the
# same-process bridge, which is the path the UI uses on every format we ship.
# The cost is real and worth stating: the shared VST3/CLAP output-parameter
# wrapper regressions (dusk-audio/plugins#231, #233) are covered for this plugin
# only by 4k-eq, TapeMachine and tape-echo continuing to run that harness.
dusk_daf_add_resize_test(duskverb-2)
# Aimed at the centre of the hero DECAY knob, in editor coordinates at the
# default 1200x800 window (design scale 1.0, no letterboxing, so design
# coordinates ARE window coordinates there). Keep this in step with kHeroCx /
# kHeroCy in DuskVerbUI.cpp: if a layout pass moves the knob, phase A of the
# harness fails and names the coordinate to update.
dusk_daf_add_ui_drag_test(duskverb-2 570 318)

# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DAF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DAF-based DuskVerb 2 plugin.
#   cmake -B build -GNinja [-DDAF_PATH=/path/to/DAF]
#   cmake --build build
#   ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.15)
project(DuskVerb2DAF VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The download is a plugin archive, without a VC++ runtime installer. Keep the
# runtime inside the plugin so a clean Windows installation can load it.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
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

# DuskVerb 2's own framework-free DSP: the core (shared with nothing but these targets, except
# DuskVerbPresetEngineConfig.cpp, which the legacy plugin compiles too) and the engines only
# DuskVerb 2 has. Every target that compiles DuskVerbDSP.cpp takes this whole list.
set(DUSKVERB2_DSP_SOURCES
    ../core/DuskVerbDSP.cpp
    ../core/DuskVerbPresetEngineConfig.cpp
    ../core/dsp/NonLinBurstEngine.cpp)

# Tuning hooks for local tuning builds, never a release: with this ON, the DUSKVERB2_EARLYFIELD,
# DUSKVERB2_OCTAVETRIM and DUSKVERB2_BANDLIMIT environment variables replace DuskVerb 2's
# early-field, octave-trim and band-limit tables for every preset that loads
# (core/DuskVerbDv2Voicing.hpp). OFF, DuskVerbDSP never reads
# them. Like the sources above, every target that compiles DuskVerbDSP.cpp takes these definitions.
option(DUSKVERB2_TUNING_HOOKS "Let DUSKVERB2_* environment variables override DuskVerb 2's voicing (tuning builds only)" OFF)
if(DUSKVERB2_TUNING_HOOKS)
    set(DUSKVERB2_DSP_DEFINITIONS DUSKVERB2_TUNING_HOOKS=1)
else()
    set(DUSKVERB2_DSP_DEFINITIONS DUSKVERB2_TUNING_HOOKS=0)
endif()

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
        ${DUSKVERB2_DSP_SOURCES}
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
    DUSKVERB2_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    ${DUSKVERB2_DSP_DEFINITIONS})

# Copies the built CLAP/VST3/LV2 into the user plugin dirs after each build
# when configured with -DDUSK_DAF_INSTALL_LOCAL=ON.
dusk_daf_install_local(duskverb-2 LV2_PROGRAMS)

if(NOT CMAKE_CROSSCOMPILING)
enable_testing()
    # The layout-parity cross-check is GENERATED from the JUCE source by a Python
    # script, but a Python interpreter is not otherwise a build dependency of
    # this plugin. Requiring it would fail configuration outright on a machine or
    # CI image without one (the GitHub runner has none), so look for it quietly
    # and wire the test only when it is actually present. The rest of the
    # DuskVerb suite below is Python-free and must keep building regardless.
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(JUCE_LAYOUT_CHECK "${DUSKVERB_GENERATED_DIR}/JuceLayoutCheck.cpp")
        add_custom_command(OUTPUT "${JUCE_LAYOUT_CHECK}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../core/tests/generate_juce_layout_check.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/../src/PluginProcessor.cpp" "${JUCE_LAYOUT_CHECK}"
            DEPENDS ../src/PluginProcessor.cpp ../core/tests/generate_juce_layout_check.py
            VERBATIM)
        add_executable(DuskVerbLayoutParityTest "${JUCE_LAYOUT_CHECK}")
        target_include_directories(DuskVerbLayoutParityTest PRIVATE ../core)
        target_compile_features(DuskVerbLayoutParityTest PRIVATE cxx_std_17)
        add_test(NAME DuskVerbLayoutParity COMMAND DuskVerbLayoutParityTest)
    else()
        message(STATUS "Python3 interpreter not found; skipping the generated JUCE layout parity check")
    endif()
    add_executable(DuskVerbLocaleFailureTest ../core/tests/DuskVerbLocaleFailureTests.cpp)
    target_include_directories(DuskVerbLocaleFailureTest PRIVATE ../core)
    target_compile_features(DuskVerbLocaleFailureTest PRIVATE cxx_std_17)
    add_test(NAME DuskVerbLocaleFailure COMMAND DuskVerbLocaleFailureTest)
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
        ${DUSKVERB2_DSP_SOURCES}
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

    # The scaler that keeps the damping card live on the table-voiced engines.
    add_executable(DuskVerbDampingKnobTest
        ../core/tests/DuskVerbDampingKnobTests.cpp
        ${DUSKVERB2_DSP_SOURCES}
        ${DUSKVERB_ENGINE_SOURCES})
    target_include_directories(DuskVerbDampingKnobTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
    target_compile_features(DuskVerbDampingKnobTest PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(DuskVerbDampingKnobTest PRIVATE -O2)
    endif()
    add_test(NAME DuskVerbDampingKnobs COMMAND DuskVerbDampingKnobTest)

    # Every control the editor draws either changes the render on every program and engine or
    # is declared dead (and drawn dimmed) by DuskVerbKnobLiveness.hpp.
    add_executable(DuskVerbControlLivenessTest
        ../core/tests/DuskVerbControlLivenessTests.cpp
        ${DUSKVERB2_DSP_SOURCES}
        ${DUSKVERB_ENGINE_SOURCES})
    target_include_directories(DuskVerbControlLivenessTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
    target_compile_features(DuskVerbControlLivenessTest PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(DuskVerbControlLivenessTest PRIVATE -O2)
    endif()
    add_test(NAME DuskVerbControlLiveness COMMAND DuskVerbControlLivenessTest)

    add_executable(NonLinBurstEngineTest
        ../core/tests/NonLinBurstEngineTests.cpp
        ../core/dsp/NonLinBurstEngine.cpp)
    target_compile_features(NonLinBurstEngineTest PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(NonLinBurstEngineTest PRIVATE -O2)
    endif()
    add_test(NAME NonLinBurstEngine COMMAND NonLinBurstEngineTest)
    add_executable(DuskVerbTonalCorrectionTest
        DuskVerbTonalCorrectionTests.cpp
        ${DUSKVERB2_DSP_SOURCES}
        ${DUSKVERB_ENGINE_SOURCES})
    target_include_directories(DuskVerbTonalCorrectionTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
    target_compile_features(DuskVerbTonalCorrectionTest PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(DuskVerbTonalCorrectionTest PRIVATE -O2)
    endif()
    add_test(NAME DuskVerbTonalCorrection COMMAND DuskVerbTonalCorrectionTest)

    add_executable(DuskVerbPluginLayerTest
        DuskVerbPluginLayerTests.cpp
        ${DUSKVERB2_DSP_SOURCES}
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

    # The TONE graph's mapping, hit-testing and drag arithmetic (DuskVerbToneGraph.hpp).
    add_executable(DuskVerbToneGraphTest DuskVerbToneGraphTests.cpp)
    target_include_directories(DuskVerbToneGraphTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/ui")
    target_compile_features(DuskVerbToneGraphTest PRIVATE cxx_std_17)
    add_test(NAME DuskVerbToneGraph COMMAND DuskVerbToneGraphTest)

    # The early field (DuskVerbEarlyField.hpp) with its voicing table and tuning-hook parser
    # (DuskVerbDv2Voicing.hpp). `DuskVerbEarlyFieldTest --dump-layout CONFIG` prints the tap
    # layout, for the check against the tools repo's model (classic_lab/earlyfield_screen.py).
    add_executable(DuskVerbEarlyFieldTest ../core/tests/DuskVerbEarlyFieldTests.cpp)
    # The field inside DuskVerbDSP: a preset without one renders exactly as with the field off,
    # in either build; the field reaches the output where its layout says.
    add_executable(DuskVerbEarlyFieldDspTest
        ../core/tests/DuskVerbEarlyFieldDspTests.cpp
        ${DUSKVERB2_DSP_SOURCES}
        ${DUSKVERB_ENGINE_SOURCES})
    # The octave trim on the wet (DuskVerbOctaveTrim.hpp) with its voicing table and tuning-hook
    # parser. `DuskVerbOctaveTrimTest --dump-response GAINS` prints its measured magnitude beside
    # the formula's, for the check against the tools repo's model (cascade_db).
    add_executable(DuskVerbOctaveTrimTest ../core/tests/DuskVerbOctaveTrimTests.cpp)
    # The trim inside DuskVerbDSP: a preset without one renders as a flat trim does, in either
    # build; with one, the render is the stage run over the summed wet, swapped with the engines.
    add_executable(DuskVerbOctaveTrimDspTest
        ../core/tests/DuskVerbOctaveTrimDspTests.cpp
        ${DUSKVERB2_DSP_SOURCES}
        ${DUSKVERB_ENGINE_SOURCES})
    # The band limit on the wet after the trim (DuskVerbBandLimit.hpp): the stage against its closed
    # form at three rates, the voicing table and hook parser, and inside DuskVerbDSP a render with
    # one is the stage over the same render without one.
    add_executable(DuskVerbBandLimitTest
        ../core/tests/DuskVerbBandLimitTests.cpp
        ${DUSKVERB2_DSP_SOURCES}
        ${DUSKVERB_ENGINE_SOURCES})
    # DuskVerb 2's preset recall (DuskVerbPresets.hpp): the rows with Decay's reading and the
    # overrides over them; the host sees the measured RT60 while the engines still get the row's
    # decay there, bit for bit, and in proportion around it.
    add_executable(DuskVerbPresetRecallTest
        ../core/tests/DuskVerbPresetRecallTests.cpp
        ${DUSKVERB2_DSP_SOURCES}
        ${DUSKVERB_ENGINE_SOURCES})
    # ParallelMultibandTank's decorrelated side on the tank alone: L + R unchanged, the tail
    # decorrelated on mono and stereo input, equal L/R power, nothing undelayed.
    add_executable(DuskVerbPmbSideTest ../core/tests/DuskVerbPmbSideTests.cpp)
    # The tanks' time-varying feedback matrix (FDNReverb's line pairs, DattorroTank's cross-feed) on
    # each tank alone: off is the tank without it, bit for bit; on keeps the decay, at its limits
    # too, and replays.
    add_executable(DuskVerbFeedbackRotationTest ../core/tests/DuskVerbFeedbackRotationTests.cpp
        ../src/dsp/FDNReverb.cpp ../src/dsp/DattorroTank.cpp ../src/dsp/OctaveGEQDesign.cpp)
    foreach(testTarget DuskVerbEarlyFieldTest DuskVerbEarlyFieldDspTest DuskVerbOctaveTrimTest DuskVerbOctaveTrimDspTest
                       DuskVerbBandLimitTest DuskVerbPresetRecallTest DuskVerbPmbSideTest DuskVerbFeedbackRotationTest)
        target_include_directories(${testTarget} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../core"
            "${CMAKE_CURRENT_SOURCE_DIR}/../src"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
        target_compile_features(${testTarget} PRIVATE cxx_std_17)
        if(NOT MSVC)
            target_compile_options(${testTarget} PRIVATE -O2)
        endif()
    endforeach()
    add_test(NAME DuskVerbEarlyField COMMAND DuskVerbEarlyFieldTest)
    add_test(NAME DuskVerbEarlyFieldDsp COMMAND DuskVerbEarlyFieldDspTest)
    add_test(NAME DuskVerbOctaveTrim COMMAND DuskVerbOctaveTrimTest)
    add_test(NAME DuskVerbOctaveTrimDsp COMMAND DuskVerbOctaveTrimDspTest)
    add_test(NAME DuskVerbBandLimit COMMAND DuskVerbBandLimitTest)
    add_test(NAME DuskVerbPresetRecall COMMAND DuskVerbPresetRecallTest)
    add_test(NAME DuskVerbPmbSide COMMAND DuskVerbPmbSideTest)
    add_test(NAME DuskVerbFeedbackRotation COMMAND DuskVerbFeedbackRotationTest)

    foreach(testTarget DuskVerbCoreTest DuskVerbTonalCorrectionTest DuskVerbDampingKnobTest DuskVerbControlLivenessTest DuskVerbPluginLayerTest
                       DuskVerbEarlyFieldDspTest DuskVerbOctaveTrimDspTest DuskVerbBandLimitTest DuskVerbPresetRecallTest)
        target_compile_definitions(${testTarget} PRIVATE ${DUSKVERB2_DSP_DEFINITIONS})
    endforeach()

    add_executable(DuskVerbParameterTest DuskVerbParameterTests.cpp)
    add_executable(DuskVerbStateCodecTest DuskVerbStateCodecTests.cpp)
    add_executable(DuskVerbControlStateTest DuskVerbControlStateTests.cpp)
    add_executable(DuskVerbValueTextTest DuskVerbValueTextTests.cpp)
    add_executable(DuskVerbEditorIdentityTest DuskVerbEditorIdentityTests.cpp)
    foreach(testTarget DuskVerbParameterTest DuskVerbStateCodecTest DuskVerbControlStateTest DuskVerbValueTextTest
                       DuskVerbEditorIdentityTest)
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
    if(APPLE)
        add_executable(DuskVerbAUTest DuskVerbAUTests.cpp)
        target_link_libraries(DuskVerbAUTest PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
        add_dependencies(DuskVerbAUTest duskverb-2-au)
        add_test(NAME DuskVerbAUHosted COMMAND DuskVerbAUTest $<TARGET_FILE:duskverb-2-au>)
    endif()
    if(UNIX OR WIN32)
        add_executable(DuskVerbClapTest DuskVerbClapTests.cpp)
        target_include_directories(DuskVerbClapTest PRIVATE "${DAF_PATH}/daf/src")
        # The MIX LOCK contract is built when DAF's own CLAP headers carry preset loading: the
        # check dusk_daf_add_clap_preset_test uses (DUSK_DAF_HAS_CLAP_PRESETS, DuskDafPlugin.cmake).
        target_compile_definitions(DuskVerbClapTest PRIVATE
            DUSKVERB_CLAP_PRESET_LOAD=$<BOOL:${DUSK_DAF_HAS_CLAP_PRESETS}>)
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
        add_executable(DuskVerbVst3IdentityTest DuskVerbVst3IdentityTests.cpp)
        target_include_directories(DuskVerbVst3IdentityTest PRIVATE "${DAF_PATH}/daf/src")
        target_link_libraries(DuskVerbVst3IdentityTest PRIVATE ${CMAKE_DL_LIBS})
        add_dependencies(DuskVerbVst3IdentityTest duskverb-2-vst3)
        add_test(NAME DuskVerbVst3Identity COMMAND DuskVerbVst3IdentityTest $<TARGET_FILE:duskverb-2-vst3>)
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
# Aimed at the centre of the hero DECAY knob, in window pixels at the default
# 1050x700 window: the 1200x800 design drawn at scale 0.875 with no letterboxing,
# so this is kHeroCx / kHeroCy times 0.875. Keep it in step with kHeroCx /
# kHeroCy in DuskVerbUI.cpp: if a layout pass moves the knob, phase A of the
# harness fails and names the coordinate to update.
dusk_daf_add_ui_drag_test(duskverb-2 644 147)
if(TARGET duskverb-2UiDragTest)
    target_compile_definitions(duskverb-2UiDragTest PRIVATE
        DUSK_UI_TEST_EXTENSION="${CMAKE_CURRENT_SOURCE_DIR}/DuskVerbUiStateChecks.hpp")
    target_include_directories(duskverb-2UiDragTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
endif()
dusk_daf_add_clap_preset_test(duskverb-2)

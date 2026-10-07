# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DAF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DAF-based Ring Out plugin (feedback eliminator).
# Configure this directory directly:
#   cmake -B build -GNinja [-DDAF_PATH=/path/to/DAF]
#   cmake --build build
#   ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.15)
project(RingOutDAF VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/cmake/DuskDafPlugin.cmake")

# The editor bundles Barlow Condensed so its metrics are identical on every OS.
# The faces live once in plugins/shared-daf/fonts; dusk_daf_embed_font turns them
# into C arrays under the build tree, so nothing hex-encoded is committed.
set(RINGOUT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
dusk_daf_embed_font(kRoFontSemiBold
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/fonts/BarlowCondensed/BarlowCondensed-SemiBold.ttf"
    "${RINGOUT_GENERATED_DIR}/RingOutFontSemiBold.inc")
dusk_daf_embed_font(kRoFontRegular
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/fonts/BarlowCondensed/BarlowCondensed-Regular.ttf"
    "${RINGOUT_GENERATED_DIR}/RingOutFontRegular.inc")

daf_add_plugin(ring-out
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    # MONOLITHIC: DAF_PLUGIN_WANT_DIRECT_ACCESS makes DAF's LV2 TTL export
    # declare the UI inside the dsp binary; the LV2 build must match or hosts
    # fail to load. Single-object LV2 also gives the display direct access to
    # the engine (filter table, spectrum, meters).
    MONOLITHIC
    UI_TYPE opengl
    FILES_DSP
        RingOutPlugin.cpp
        ../core/RingOutDSP.cpp
    FILES_UI
        RingOutUI.cpp
        ${DUSK_DAF_UI_SOURCES})

target_include_directories(ring-out PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RINGOUT_GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    ${DUSK_DAF_INCLUDE_DIRS})   # shared-daf dsp/ui + DAF widgets

# Single source of truth for the plugin version: the project() VERSION above is
# plumbed into getVersion() and the UI nameplate via these macros (see
# RingOutVersion.hpp). Bumped by the /release-plugin skill. PUBLIC because DAF
# compiles the UI in a separate ring-out-ui target that links this one and must
# see the same values.
target_compile_definitions(ring-out PUBLIC
    RO_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    RO_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    RO_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# Auto-deploy the built CLAP/VST3/LV2/AU into the user plugin dirs after each
# build (disable with -DDUSK_DAF_INSTALL_LOCAL=OFF).
dusk_daf_install_local(ring-out LV2_PROGRAMS)

if(NOT CMAKE_CROSSCOMPILING)
    # enable_testing() + add_test() are what make this reachable from ctest.
    enable_testing()
    add_executable(RingOutCoreTest
        ../core/tests/RingOutDSPTest.cpp
        ../core/RingOutDSP.cpp)
    # shared-daf/ui for DuskLogFreqAxis.hpp, whose label() is pure and tested here.
    target_include_directories(RingOutCoreTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/ui")
    target_compile_features(RingOutCoreTest PRIVATE cxx_std_17)
    find_package(Threads REQUIRED)   # the deferred-RESET test contends the table lock from a thread
    target_link_libraries(RingOutCoreTest PRIVATE Threads::Threads)
    add_test(NAME RingOutCore COMMAND RingOutCoreTest)

    # The shared SeqLock publishes its payload as atomic words; one writer is
    # driven against several readers, so this runs under TSan as well as plain.
    add_executable(RingOutSeqLockTest
        ../../shared-daf/dsp/tests/DuskSeqLockTest.cpp)
    target_include_directories(RingOutSeqLockTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
    target_compile_features(RingOutSeqLockTest PRIVATE cxx_std_17)
    target_link_libraries(RingOutSeqLockTest PRIVATE Threads::Threads)
    add_test(NAME RingOutSeqLock COMMAND RingOutSeqLockTest)

    # User preset files: a truncated or partly corrupt file must be rejected
    # whole, because loading it as "defaults and no filters" discards a ring-out.
    add_executable(RingOutPresetTest tests/RingOutPresetTest.cpp)
    target_include_directories(RingOutPresetTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../core")
    target_compile_features(RingOutPresetTest PRIVATE cxx_std_17)
    add_test(NAME RingOutPreset COMMAND RingOutPresetTest)

    # Inline text entry belongs to the row it was opened on. The four ImGui core
    # files are compiled in directly rather than through DUSK_DAF_UI_SOURCES,
    # whose backend pulls in DGL and a window system this headless test never
    # opens -- the same split the DuskVerb knob-gesture test uses.
    add_executable(RingOutFilterEditorTest tests/RingOutFilterEditorTest.cpp
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_draw.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_tables.cpp"
        "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_widgets.cpp")
    target_include_directories(RingOutFilterEditorTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"
        ${DUSK_DAF_INCLUDE_DIRS}
        "${DAFWIDGETS_PATH}/imgui/DearImGui")   # the test includes "imgui.h" directly
    target_compile_features(RingOutFilterEditorTest PRIVATE cxx_std_17)
    add_test(NAME RingOutFilterEditor COMMAND RingOutFilterEditorTest)

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

# Input Level / Output Level are output parameters; guard them against reaching
# the host as parameter edits, in both VST3 (dusk-audio/plugins#233) and CLAP
# (#231). Noise through the plugin at default settings moves both meters.
dusk_daf_add_output_param_tests(ring-out)
dusk_daf_add_ui_drag_test(ring-out 890 480)   # GAIN OUT knob
dusk_daf_add_resize_test(ring-out)

# SETUP / ADD / RESET as a host sees them, against the built .clap: the five
# clap-validator 0.4.1 failures in miniature, and the two things a fix must not
# trade away to clear them -- the press still reaching the plugin, and a trigger
# staying out of the saved state. dlopen is what makes it Linux/macOS only, the
# same restriction as the wrapper harnesses.
if(NOT CMAKE_CROSSCOMPILING AND NOT WIN32)
    if(NOT TARGET ring-out-clap)
        message(FATAL_ERROR "the Ring Out CLAP trigger test needs the ring-out-clap target")
    endif()

    add_executable(ring-outClapTriggerTest tests/DafClapTriggerTest.cpp)
    # clap/ is DAF's own vendored interface; the harness speaks the same ABI the
    # wrapper is compiled against rather than a second copy.
    target_include_directories(ring-outClapTriggerTest PRIVATE "${DAF_PATH}/daf/src")
    target_compile_features(ring-outClapTriggerTest PRIVATE cxx_std_17)
    target_link_libraries(ring-outClapTriggerTest PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(ring-outClapTriggerTest ring-out-clap)
    add_test(NAME RingOutClapTriggers
             COMMAND ring-outClapTriggerTest "$<TARGET_FILE:ring-out-clap>")
endif()

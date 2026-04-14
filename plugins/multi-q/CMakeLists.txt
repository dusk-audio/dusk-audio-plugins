cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment (fallback to default version)
set(MULTIQ_DEFAULT_VERSION "0.10.4")
get_plugin_version("multi-q" MULTIQ_VERSION)
if(NOT MULTIQ_VERSION OR MULTIQ_VERSION STREQUAL "0.0.0")
    set(MULTIQ_VERSION ${MULTIQ_DEFAULT_VERSION})
endif()

project(MultiQ VERSION ${MULTIQ_VERSION})

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is already added by parent CMakeLists.txt

# Build format options
option(BUILD_VST3 "Build VST3 format" ON)
option(BUILD_LV2 "Build LV2 format" ON)
# AU is only supported on macOS
if(APPLE)
    option(BUILD_AU "Build AU format" ON)
else()
    option(BUILD_AU "Build AU format" OFF)
endif()

# Determine which formats to build
set(PLUGIN_FORMATS "")
if(BUILD_VST3)
    list(APPEND PLUGIN_FORMATS VST3)
endif()
if(BUILD_LV2)
    list(APPEND PLUGIN_FORMATS LV2)
endif()
if(BUILD_AU)
    list(APPEND PLUGIN_FORMATS AU)
endif()

# Create plugin target
juce_add_plugin(MultiQ
    COMPANY_NAME "Dusk Audio"
    # Note: BUNDLE_ID changed from com.LunaCoAudio.MultiQ due to trademark rebrand.
    # Existing macOS installs must re-scan plugins in their DAW.
    BUNDLE_ID "com.DuskAudio.MultiQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Dusk
    PLUGIN_CODE MulQ
    FORMATS ${PLUGIN_FORMATS}
    # Note: LV2URI changed from lunacoaudio.com due to trademark rebrand.
    # LV2 hosts referencing the old URI will need to re-scan.
    LV2URI "https://dusk-audio.github.io/plugins/multi-q"
    PRODUCT_NAME "Multi-Q"
    DESCRIPTION "Professional 8-band parametric EQ with FFT Analyzer"
    VST3_CAN_REPLACE_VST2 TRUE
    VST3_AUTO_MANIFEST FALSE
)

# Generate JuceHeader.h
juce_generate_juce_header(MultiQ)

# Add source files
target_sources(MultiQ
    PRIVATE
        MultiQ.cpp
        MultiQEditor.cpp
        EQGraphicDisplay.cpp
        FFTAnalyzer.cpp
        BritishEQCurveDisplay.cpp
        TubeEQCurveDisplay.cpp
        BandDetailPanel.cpp
        ../shared/LEDMeter.cpp
        ../4k-eq/FourKLookAndFeel.cpp
)
# Set include directories
target_include_directories(MultiQ
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
        ${CMAKE_CURRENT_SOURCE_DIR}/../4k-eq
)

# Compiler definitions
target_compile_definitions(MultiQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Set version compile definitions (from git tag or CI)
set_plugin_version_defines(MultiQ ${MULTIQ_VERSION})

# Link libraries
target_link_libraries(MultiQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        # Note: juce_recommended_lto_flags removed - causes Windows CI builds to hang
        # during vst3_helper moduleinfo.json generation (25+ min with no output)
        juce::juce_recommended_warning_flags
)

# Platform-specific compile options
if(MSVC)
    # MSVC optimization and warning flags
    target_compile_options(MultiQ PRIVATE /W4)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        target_compile_options(MultiQ PRIVATE /O2 /fp:fast)
    endif()
else()
    # GCC/Clang optimization and warning flags
    target_compile_options(MultiQ PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        target_compile_options(MultiQ PRIVATE -O3 -ffast-math -fno-finite-math-only)
    endif()
endif()

# Set binary output directory (required for CI validation to find built plugins)
set_target_properties(MultiQ PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Match EQ Test (standalone, no plugin wrapper) ---
if(NOT CMAKE_CROSSCOMPILING AND NOT WIN32)
    add_executable(MultiQMatchTest
        tests/test_match_eq.cpp
    )

    target_include_directories(MultiQMatchTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/MultiQ_artefacts/JuceLibraryCode
    )

    target_compile_definitions(MultiQMatchTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_link_libraries(MultiQMatchTest
        PRIVATE
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
    )

    add_dependencies(MultiQMatchTest MultiQ_All)
endif()

# --- DSP Coefficient Test (standalone, no JUCE dependency) ---
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(MultiQDSPTest
        tests/test_multiq_dsp.cpp
    )
    target_compile_features(MultiQDSPTest PRIVATE cxx_std_17)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(MultiQDSPTest PRIVATE m)
    endif()
endif()

# --- Mode & Integration Tests (standalone, no JUCE dependency) ---
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(MultiQModesTest
        tests/test_multiq_modes.cpp
    )
    target_compile_features(MultiQModesTest PRIVATE cxx_std_17)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(MultiQModesTest PRIVATE m)
    endif()
endif()

# --- Integration Test (JUCE-linked, processes audio through plugin) ---
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(MultiQIntegrationTest
        tests/test_multiq_integration.cpp
    )

    target_include_directories(MultiQIntegrationTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/MultiQ_artefacts/JuceLibraryCode
    )

    target_compile_definitions(MultiQIntegrationTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_compile_features(MultiQIntegrationTest PRIVATE cxx_std_17)

    target_link_libraries(MultiQIntegrationTest
        PRIVATE
            MultiQ
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
    )

    add_dependencies(MultiQIntegrationTest MultiQ_All)
endif()

# --- M/S Routing Test (JUCE-linked, verifies per-band mid/side routing) ---
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(MultiQMSRoutingTest
        tests/test_ms_routing.cpp
    )

    target_include_directories(MultiQMSRoutingTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/MultiQ_artefacts/JuceLibraryCode
    )

    target_compile_definitions(MultiQMSRoutingTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_compile_features(MultiQMSRoutingTest PRIVATE cxx_std_17)

    target_link_libraries(MultiQMSRoutingTest
        PRIVATE
            MultiQ
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
    )

    add_dependencies(MultiQMSRoutingTest MultiQ_All)
endif()

# --- Transfer-to-Digital Test (JUCE-linked, verifies cross-mode EQ transfer) ---
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(MultiQTransferTest
        tests/test_transfer_to_digital.cpp
    )

    target_include_directories(MultiQTransferTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/MultiQ_artefacts/JuceLibraryCode
    )

    target_compile_definitions(MultiQTransferTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_compile_features(MultiQTransferTest PRIVATE cxx_std_17)

    target_link_libraries(MultiQTransferTest
        PRIVATE
            MultiQ
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
    )

    add_dependencies(MultiQTransferTest MultiQ_All)
endif()

# --- Oversampling Test (JUCE-linked, verifies oversampling frequency response) ---
if(NOT CMAKE_CROSSCOMPILING AND NOT WIN32)
    add_executable(MultiQOversamplingTest
        tests/test_multiq_oversampling.cpp
    )

    target_include_directories(MultiQOversamplingTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/MultiQ_artefacts/JuceLibraryCode
    )

    target_compile_definitions(MultiQOversamplingTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_compile_features(MultiQOversamplingTest PRIVATE cxx_std_17)

    target_link_libraries(MultiQOversamplingTest
        PRIVATE
            MultiQ
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
    )

    add_dependencies(MultiQOversamplingTest MultiQ_All)
endif()

# --- Stress Test (JUCE-linked, rapid parameter changes and NaN/Inf safety) ---
if(NOT CMAKE_CROSSCOMPILING AND NOT WIN32)
    add_executable(MultiQStressTest
        tests/test_multiq_stress.cpp
    )

    target_include_directories(MultiQStressTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/MultiQ_artefacts/JuceLibraryCode
    )

    target_compile_definitions(MultiQStressTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_compile_features(MultiQStressTest PRIVATE cxx_std_17)

    target_link_libraries(MultiQStressTest
        PRIVATE
            MultiQ
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
    )

    add_dependencies(MultiQStressTest MultiQ_All)
endif()

# --- Benchmark (JUCE-linked, CPU performance measurement - informational only) ---
if(NOT CMAKE_CROSSCOMPILING AND NOT WIN32)
    add_executable(MultiQBenchmark
        tests/test_multiq_benchmark.cpp
    )

    target_include_directories(MultiQBenchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/MultiQ_artefacts/JuceLibraryCode
    )

    target_compile_definitions(MultiQBenchmark PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_compile_features(MultiQBenchmark PRIVATE cxx_std_17)

    target_link_libraries(MultiQBenchmark
        PRIVATE
            MultiQ
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
    )

    add_dependencies(MultiQBenchmark MultiQ_All)

    add_executable(MultiQPresetAudioTest
        tests/test_preset_audio.cpp
    )

    target_include_directories(MultiQPresetAudioTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/MultiQ_artefacts/JuceLibraryCode
    )

    target_compile_definitions(MultiQPresetAudioTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_compile_features(MultiQPresetAudioTest PRIVATE cxx_std_17)

    target_link_libraries(MultiQPresetAudioTest
        PRIVATE
            MultiQ
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
    )

    add_dependencies(MultiQPresetAudioTest MultiQ_All)

    # --- Analog Accuracy & Production Test ---
    add_executable(MultiQAnalogAccuracyTest
        tests/test_multiq_analog_accuracy.cpp
    )

    target_include_directories(MultiQAnalogAccuracyTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/MultiQ_artefacts/JuceLibraryCode
    )

    target_compile_definitions(MultiQAnalogAccuracyTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_compile_features(MultiQAnalogAccuracyTest PRIVATE cxx_std_17)

    target_link_libraries(MultiQAnalogAccuracyTest
        PRIVATE
            MultiQ
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
    )

    add_dependencies(MultiQAnalogAccuracyTest MultiQ_All)
endif()

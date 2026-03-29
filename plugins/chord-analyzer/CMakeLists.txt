cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment (fallback to default version)
set(CHORDANALYZER_DEFAULT_VERSION "1.0.0")
get_plugin_version("chord-analyzer" CHORDANALYZER_VERSION)
if(NOT CHORDANALYZER_VERSION OR CHORDANALYZER_VERSION STREQUAL "0.0.0")
    set(CHORDANALYZER_VERSION ${CHORDANALYZER_DEFAULT_VERSION})
endif()

project(ChordAnalyzer VERSION ${CHORDANALYZER_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Add the plugin - MIDI Effect
juce_add_plugin(ChordAnalyzer
    COMPANY_NAME "Dusk Audio"
    BUNDLE_ID "com.DuskAudio.ChordAnalyzer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Dusk
    PLUGIN_CODE ChAn
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://dusk-audio.github.io/plugins/chord-analyzer"
    PRODUCT_NAME "Chord Analyzer"
    VST3_CAN_REPLACE_VST2 FALSE
    VST3_AUTO_MANIFEST FALSE
)

juce_generate_juce_header(ChordAnalyzer)

# Source files
target_sources(ChordAnalyzer
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/ChordAnalyzer.cpp
        Source/ChordAnalyzer.h
        Source/ChordRecorder.cpp
        Source/ChordRecorder.h
        Source/TheoryTooltips.h
        Source/ChordAnalyzerLookAndFeel.h
)

# Include directories
target_include_directories(ChordAnalyzer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

# Preprocessor definitions
target_compile_definitions(ChordAnalyzer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Link JUCE modules
target_link_libraries(ChordAnalyzer
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Test target
add_executable(ChordAnalyzerTest EXCLUDE_FROM_ALL
    tests/test_chord_analyzer.cpp
    Source/ChordAnalyzer.cpp
)
target_include_directories(ChordAnalyzerTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(ChordAnalyzerTest PRIVATE
    juce::juce_core
)
target_compile_definitions(ChordAnalyzerTest PRIVATE
    JUCE_STANDALONE_APPLICATION=1
)

# Optimization for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ChordAnalyzer PRIVATE -O3 -ffast-math -fno-finite-math-only)
    endif()
endif()

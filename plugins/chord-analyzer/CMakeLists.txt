cmake_minimum_required(VERSION 3.16)

project(ChordAnalyzer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the plugin - MIDI Effect
juce_add_plugin(ChordAnalyzer
    COMPANY_NAME "Luna Co. Audio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${LUNA_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Luna
    PLUGIN_CODE ChAn
    FORMATS VST3 LV2
    LV2URI "https://lunaco.audio/chord-analyzer"
    PRODUCT_NAME "Chord Analyzer"
    VST3_CAN_REPLACE_VST2 FALSE
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
        JUCE_VST3_CAN_REPLACE_VST2=0
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

# Optimization for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ChordAnalyzer PRIVATE -O3 -ffast-math -fno-finite-math-only)
    endif()
endif()

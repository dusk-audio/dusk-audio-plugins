# GrooveMind - ML-Powered Intelligent Drummer
# A Logic Pro Drummer-inspired MIDI drum pattern generator for Linux

cmake_minimum_required(VERSION 3.15)

# Plugin name and version
set(PLUGIN_NAME "GrooveMind")
set(PLUGIN_VERSION "0.1.0")

# Create the plugin target
juce_add_plugin(${PLUGIN_NAME}
    VERSION ${PLUGIN_VERSION}
    COMPANY_NAME "Luna Co. Audio"
    COMPANY_COPYRIGHT "Copyright 2026 Luna Co. Audio"
    COMPANY_WEBSITE "https://lunaco.audio"
    COMPANY_EMAIL "support@lunaco.audio"

    PLUGIN_MANUFACTURER_CODE Luna
    PLUGIN_CODE Grvm

    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    FORMATS VST3 LV2 Standalone

    LV2URI "https://lunaco.audio/plugins/groovemind"

    PRODUCT_NAME "GrooveMind"

    # Copy plugin after build
    COPY_PLUGIN_AFTER_BUILD ${LUNA_COPY_AFTER_BUILD}
    VST3_COPY_DIR "$ENV{HOME}/.vst3"
    LV2_COPY_DIR "$ENV{HOME}/.lv2"
)

# Generate JUCE header
juce_generate_juce_header(${PLUGIN_NAME})

# Add source files
target_sources(${PLUGIN_NAME}
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PatternLibrary.cpp
        Source/PatternLibrary.h
        Source/DrummerEngine.cpp
        Source/DrummerEngine.h
        Source/GrooveHumanizer.cpp
        Source/GrooveHumanizer.h
        Source/MLInference.h
        Source/XYPad.cpp
        Source/XYPad.h
        Source/TransientDetector.cpp
        Source/TransientDetector.h
        Source/TempoTracker.cpp
        Source/TempoTracker.h
        Source/GrooveExtractor.cpp
        Source/GrooveExtractor.h
        Source/AudioAnalyzer.cpp
        Source/AudioAnalyzer.h
)

# Compile definitions
target_compile_definitions(${PLUGIN_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Link JUCE modules
target_link_libraries(${PLUGIN_NAME}
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

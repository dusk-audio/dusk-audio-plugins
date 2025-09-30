cmake_minimum_required(VERSION 3.15)

# Project name and version
project(StudioVerb VERSION 1.0.0)

# Task 8: Add CMake LSP support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Add JUCE as a subdirectory
add_subdirectory(JUCE)

# Add the plugin target
juce_add_plugin(StudioVerb
    COMPANY_NAME "Luna CO. Audio"
    COMPANY_WEBSITE "https://lunaco.audio"
    COMPANY_EMAIL "support@lunaco.audio"

    PLUGIN_MANUFACTURER_CODE LuCo
    PLUGIN_CODE StuV

    FORMATS AU VST3 LV2 Standalone  # Task 6: Added LV2 support

    PRODUCT_NAME "Studio Verb"

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    COPY_PLUGIN_AFTER_BUILD TRUE

    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect

    ICON_BIG ""
    ICON_SMALL ""

    MICROPHONE_PERMISSION_ENABLED FALSE
    MICROPHONE_PERMISSION_TEXT "This plugin does not require microphone access"
)

# Generate JUCE header
juce_generate_juce_header(StudioVerb)

# Task 6: Add LV2 manifest generation
if(LV2)
    juce_add_lv2_manifest(StudioVerb)
endif()

# Add source files
target_sources(StudioVerb
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
        ReverbEngine.cpp
        ReverbEngine.h
)

# Compile definitions
target_compile_definitions(StudioVerb
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

# Link libraries
target_link_libraries(StudioVerb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Set binary output directory
set_target_properties(StudioVerb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
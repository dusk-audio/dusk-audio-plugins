cmake_minimum_required(VERSION 3.15)

# Project name and version
project(PlateReverb VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Build options
option(BUILD_AU "Build AU format" ON)
option(BUILD_VST3 "Build VST3 format" ON)
option(BUILD_LV2 "Build LV2 format" ON)
option(BUILD_STANDALONE "Build Standalone format" ON)

# Determine formats to build
set(FORMATS "")
if(BUILD_AU AND APPLE)
    list(APPEND FORMATS AU)
endif()
if(BUILD_VST3)
    list(APPEND FORMATS VST3)
endif()
if(BUILD_LV2)
    list(APPEND FORMATS LV2)
endif()
if(BUILD_STANDALONE)
    list(APPEND FORMATS Standalone)
endif()

# Add the plugin target
juce_add_plugin(PlateReverb
    COMPANY_NAME "Luna Co. Audio"
    COMPANY_WEBSITE "https://lunaco.audio"
    COMPANY_EMAIL "support@lunaco.audio"

    PLUGIN_MANUFACTURER_CODE LuCo
    PLUGIN_CODE PlRv

    FORMATS ${FORMATS}  # Configurable formats

    PRODUCT_NAME "Plate Reverb"

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
juce_generate_juce_header(PlateReverb)

# Add source files
target_sources(PlateReverb
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
        DattorroPlate.h
        CustomDelays.cpp
        CustomDelays.h
        Utilities.h
)

# Compile definitions
target_compile_definitions(PlateReverb
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1  # Fix VST3 parameter conflict
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

# Link libraries
target_link_libraries(PlateReverb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Set binary output directory
set_target_properties(PlateReverb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

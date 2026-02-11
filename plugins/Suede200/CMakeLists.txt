# Suede 200 - Vintage Digital Reverberator

cmake_minimum_required(VERSION 3.22)

# Plugin configuration
set(PLUGIN_NAME "Suede200")
set(PLUGIN_CODE "S200")
set(PLUGIN_MANUFACTURER_CODE "Luna")
set(PLUGIN_MANUFACTURER "Dusk Audio")
set(PLUGIN_VERSION "0.1.0")

juce_add_plugin(${PLUGIN_NAME}
    COMPANY_NAME "${PLUGIN_MANUFACTURER}"
    PLUGIN_MANUFACTURER_CODE ${PLUGIN_MANUFACTURER_CODE}
    PLUGIN_CODE ${PLUGIN_CODE}
    FORMATS AU VST3 LV2 Standalone
    PRODUCT_NAME "Suede 200"
    PLUGIN_NAME "Suede 200"
    DESCRIPTION "Vintage Digital Reverberator"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES "Fx" "Reverb"
    LV2URI "https://lunaco.audio/plugins/suede-200"
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    VST3_AUTO_MANIFEST FALSE
    VST3_COPY_DIR "$ENV{HOME}/.vst3"
    LV2_COPY_DIR "$ENV{HOME}/.lv2"
)

target_sources(${PLUGIN_NAME}
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/Suede200Reverb.h
        Source/Suede200Programs.h
        Source/Suede200Presets.h
        ../shared/LEDMeter.cpp
)

target_include_directories(${PLUGIN_NAME}
    PRIVATE
        Source
        ../shared
)

target_compile_definitions(${PLUGIN_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_17)

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
        juce::juce_recommended_warning_flags
)

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_link_libraries(${PLUGIN_NAME} PRIVATE pthread)
endif()

cmake_minimum_required(VERSION 3.15)
project(StudioReverb VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define our VST3 and LV2 plugin target
juce_add_plugin(StudioReverb
    PLUGIN_NAME "StudioReverb"
    PLUGIN_CODE SRvb
    FORMATS VST3 LV2 AU Standalone
    PRODUCT_NAME "StudioReverb"
    COMPANY_NAME "Luna Co. Audio"
    COMPANY_WEBSITE "https://lunaco.audio"
    COMPANY_EMAIL "info@lunaco.audio"
    PLUGIN_MANUFACTURER_CODE Luna
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST2_CATEGORY kPlugCategEffect
    LV2_URI https://luna-audio.com/plugins/studioreverb
)

juce_generate_juce_header(StudioReverb)

target_sources(StudioReverb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DSP/DragonflyReverb.cpp
    Source/DSP/DragonflyReverb.h
)

target_compile_definitions(StudioReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

target_link_libraries(StudioReverb PRIVATE
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
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
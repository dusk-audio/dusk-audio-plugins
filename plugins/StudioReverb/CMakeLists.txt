cmake_minimum_required(VERSION 3.15)
project(StudioReverb VERSION 1.0.0)

# VST3 SDK paths
set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../vst3sdk" CACHE PATH "Path to VST3 SDK")

# Define the plugin
juce_add_plugin(StudioReverb
    PLUGIN_MANUFACTURER_CODE LuCo
    PLUGIN_CODE SRev
    FORMATS VST3 LV2
    PRODUCT_NAME "Studio Reverb"
    COMPANY_NAME "Luna Co. Audio"
    BUNDLE_ID "com.lunaco.StudioReverb"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_URL "https://lunaco.audio"
    VST3_CATEGORIES Fx Reverb
    LV2_URI "http://lunaco.audio/plugins/StudioReverb"
)

# Add source files
target_sources(StudioReverb
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor_Simple.cpp  # Using simple UI temporarily
        Source/DSP/ReverbProcessor.cpp
        Source/DSP/PlateReverb.cpp
        Source/DSP/RoomReverb.cpp
        Source/DSP/HallReverb.cpp
        Source/DSP/EarlyReflections.cpp
        Source/DSP/Freeverb3Wrapper.cpp
        # UI components disabled for now
        # Source/UI/VintageKnob.cpp
        # Source/UI/ReverbTypeSelector.cpp
        Source/Presets/PresetManager.cpp
)

# Compile definitions
target_compile_definitions(StudioReverb
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Include directories
target_include_directories(StudioReverb
    PRIVATE
        Source
        Source/DSP
        Source/UI
        Source/Presets
        ${VST3_SDK_ROOT}
)

# Generate JuceHeader.h
juce_generate_juce_header(StudioReverb)

# Link libraries
target_link_libraries(StudioReverb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Platform-specific settings
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(StudioReverb PRIVATE X11)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(StudioReverb PRIVATE gdi32 opengl32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(StudioReverb PRIVATE "-framework OpenGL")
endif()

# Installation
install(TARGETS StudioReverb
    LIBRARY DESTINATION ~/.vst3
    RUNTIME DESTINATION ~/.vst3
)
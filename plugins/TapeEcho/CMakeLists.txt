cmake_minimum_required(VERSION 3.15)
project(TapeEcho VERSION 1.0.0)

# JUCE is already added in the parent CMakeLists.txt
# No need to add it again here

# Create plugin target
juce_add_plugin(TapeEcho
    COMPANY_NAME "Luna Co. Audio"
    PLUGIN_MANUFACTURER_CODE Luna
    PLUGIN_CODE TpEc
    FORMATS VST3 AU LV2
    PRODUCT_NAME "Vintage Tape Echo"
    BUNDLE_ID "com.lunaco.tapeecho"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES "Fx" "Delay" "Reverb"
    AU_MAIN_TYPE "kAudioUnitType_Effect"
    LV2_URI https://lunaco.audio/plugins/tapeecho
)

# Add source files
target_sources(TapeEcho PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/TapeDelay.cpp
    Source/DSP/SpringReverb.cpp
    Source/DSP/PreampSaturation.cpp
    Source/GUI/VUMeter.cpp
    Source/GUI/ModeSelector.cpp
)

# Generate JuceHeader.h
juce_generate_juce_header(TapeEcho)

# Add include directories
target_include_directories(TapeEcho PRIVATE
    Source
    Source/DSP
    Source/GUI
)

# Compile definitions
target_compile_definitions(TapeEcho PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Link with JUCE modules
target_link_libraries(TapeEcho PRIVATE
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
)

# C++ standard
target_compile_features(TapeEcho PRIVATE cxx_std_17)

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(TapeEcho PRIVATE -O3 -ffast-math)
    elseif(MSVC)
        target_compile_options(TapeEcho PRIVATE /O2 /fp:fast)
    endif()
endif()

# Installation paths
if(UNIX AND NOT APPLE)
    install(TARGETS TapeEcho_VST3
        DESTINATION "~/.vst3"
        COMPONENT TapeEcho)

    if(TARGET TapeEcho_LV2)
        install(TARGETS TapeEcho_LV2
            DESTINATION "~/.lv2"
            COMPONENT TapeEcho)
    endif()
elseif(APPLE)
    install(TARGETS TapeEcho_VST3
        DESTINATION "~/Library/Audio/Plug-Ins/VST3"
        COMPONENT TapeEcho)

    if(TARGET TapeEcho_AU)
        install(TARGETS TapeEcho_AU
            DESTINATION "~/Library/Audio/Plug-Ins/Components"
            COMPONENT TapeEcho)
    endif()
endif()
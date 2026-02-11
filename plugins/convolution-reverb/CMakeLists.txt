cmake_minimum_required(VERSION 3.15)
project(ConvolutionReverb VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Plugin format options
option(BUILD_VST3 "Build VST3 format" ON)
option(BUILD_LV2 "Build LV2 format" OFF)  # Disabled due to LTO memory issues in container
# AU is only supported on macOS
if(APPLE)
    option(BUILD_AU "Build AU format" ON)
else()
    option(BUILD_AU "Build AU format" OFF)
endif()
option(BUILD_STANDALONE "Build Standalone format" ON)

# Build format list
set(PLUGIN_FORMATS Standalone)
if(BUILD_VST3)
    list(APPEND PLUGIN_FORMATS VST3)
endif()
if(BUILD_LV2)
    list(APPEND PLUGIN_FORMATS LV2)
endif()
if(BUILD_AU AND APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

# Add the plugin target
juce_add_plugin(ConvolutionReverb
    COMPANY_NAME "Dusk Audio"
    BUNDLE_ID "com.lunaco.ConvolutionReverb"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE LuCo
    PLUGIN_CODE CnvR
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://lunaco.audio/plugins/convolution-reverb"
    PRODUCT_NAME "Convolution Reverb"
    DESCRIPTION "Convolution reverb with IR browser and envelope shaping"
)

juce_generate_juce_header(ConvolutionReverb)

# Source files
target_sources(ConvolutionReverb PRIVATE
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h
    ConvolutionReverbLookAndFeel.cpp
    ConvolutionReverbLookAndFeel.h
    ConvolutionEngine.h
    IRBrowser.cpp
    IRBrowser.h
    IRWaveformDisplay.cpp
    IRWaveformDisplay.h
    EnvelopeProcessor.h
    WetSignalEQ.h
    ../shared/LEDMeter.cpp
    ../shared/LEDMeter.h
)

# Include directories
target_include_directories(ConvolutionReverb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

# Compile definitions
target_compile_definitions(ConvolutionReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

# Link libraries
target_link_libraries(ConvolutionReverb PRIVATE
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

# Release optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ConvolutionReverb PRIVATE -O3 -ffast-math)
endif()

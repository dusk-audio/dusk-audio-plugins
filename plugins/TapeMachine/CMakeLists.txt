cmake_minimum_required(VERSION 3.15)

project(TapeMachine VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# JUCE is already added by the parent CMakeLists.txt
# No need to add it again here

# Define the plugin
juce_add_plugin(TapeMachine
    COMPANY_NAME "Luna Co. Audio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE VntA
    PLUGIN_CODE Tape
    FORMATS VST3
    PRODUCT_NAME "TapeMachine"
    VST3_CAN_REPLACE_VST2 TRUE)

# Generate JUCE header
juce_generate_juce_header(TapeMachine)

# Set the C++ standard for the plugin target
target_compile_features(TapeMachine PRIVATE cxx_std_17)

# Add source files
target_sources(TapeMachine
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ImprovedTapeEmulation.cpp
        Source/GUI/VUMeter.cpp)

# Add compile definitions
target_compile_definitions(TapeMachine
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1)

# Link required JUCE modules
target_link_libraries(TapeMachine
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
        juce::juce_recommended_warning_flags)

# Platform-specific settings
if(MSVC)
    target_compile_options(TapeMachine PRIVATE /W4)
else()
    target_compile_options(TapeMachine PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Set binary output directory
set_target_properties(TapeMachine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
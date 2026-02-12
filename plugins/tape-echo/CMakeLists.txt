cmake_minimum_required(VERSION 3.15)

project(TapeEcho VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# JUCE is already added by the parent CMakeLists.txt

# Define the plugin
juce_add_plugin(TapeEcho
    COMPANY_NAME "Dusk Audio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE LuCo
    PLUGIN_CODE TpEc
    FORMATS VST3 LV2 Standalone
    PRODUCT_NAME "Tape Echo"
    LV2URI "https://lunaco.audio/plugins/tapeecho"
    VST3_CAN_REPLACE_VST2 TRUE)

# Generate JUCE header
juce_generate_juce_header(TapeEcho)

# Set the C++ standard for the plugin target
target_compile_features(TapeEcho PRIVATE cxx_std_17)

# Add source files
target_sources(TapeEcho
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DSP/TapeEcho.cpp
        Source/DSP/SpringReverb.cpp
        Source/DSP/TapeSaturation.cpp
        Source/DSP/WowFlutter.cpp
        Source/UI/TapeVisualization.cpp
        Source/UI/TapeEchoLookAndFeel.cpp
        ../shared/LEDMeter.cpp)

# Add compile definitions
target_compile_definitions(TapeEcho
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1)

# Link required JUCE modules
target_link_libraries(TapeEcho
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
        # juce::juce_recommended_lto_flags  # Disabled locally - CI release builds enable LTO
        juce::juce_recommended_warning_flags)

# Platform-specific settings
if(MSVC)
    target_compile_options(TapeEcho PRIVATE /W4)
else()
    target_compile_options(TapeEcho PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Set binary output directory
set_target_properties(TapeEcho PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

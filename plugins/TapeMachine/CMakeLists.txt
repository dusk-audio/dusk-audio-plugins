cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment (fallback to default version)
set(TAPEMACHINE_DEFAULT_VERSION "1.0.8")
get_plugin_version("tapemachine" TAPEMACHINE_VERSION)
if(NOT TAPEMACHINE_VERSION OR TAPEMACHINE_VERSION STREQUAL "0.0.0")
    set(TAPEMACHINE_VERSION ${TAPEMACHINE_DEFAULT_VERSION})
endif()

project(TapeMachine VERSION ${TAPEMACHINE_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# JUCE is already added by the parent CMakeLists.txt
# No need to add it again here

# Build format options (matches multi-comp/multi-q so CI's -DBUILD_LV2=OFF
# is honored on Windows, where LV2 turtle generation is unreliable)
option(BUILD_VST3 "Build VST3 format" ON)
option(BUILD_LV2  "Build LV2 format"  ON)
if(APPLE)
    option(BUILD_AU "Build AU format" ON)
else()
    option(BUILD_AU "Build AU format" OFF)
endif()

set(PLUGIN_FORMATS "")
if(BUILD_VST3)
    list(APPEND PLUGIN_FORMATS VST3)
endif()
if(BUILD_LV2)
    list(APPEND PLUGIN_FORMATS LV2)
endif()
if(BUILD_AU)
    list(APPEND PLUGIN_FORMATS AU)
endif()

# Define the plugin
juce_add_plugin(TapeMachine
    COMPANY_NAME "Dusk Audio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Dusk
    PLUGIN_CODE Tape
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "TapeMachine"
    LV2URI "https://dusk-audio.github.io/plugins/tapemachine"
    VST3_CAN_REPLACE_VST2 TRUE
    VST3_AUTO_MANIFEST FALSE)

# Generate JUCE header
apply_clap_extension(TapeMachine tapemachine audio-effect distortion saturation mono stereo)

juce_generate_juce_header(TapeMachine)

# Set the C++ standard for the plugin target
target_compile_features(TapeMachine PRIVATE cxx_std_17)

# Add source files
target_sources(TapeMachine
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ImprovedTapeEmulation.cpp
        Source/GUI/AnalogVUMeter.cpp
        Source/GUI/TapeMachineLookAndFeel.cpp
        Source/GUI/TapeReelComponent.cpp)

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

# Set version compile definitions (from git tag or CI)
set_plugin_version_defines(TapeMachine ${TAPEMACHINE_VERSION})

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
        # juce::juce_recommended_lto_flags  # Disabled locally - CI release builds enable LTO
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

# Opt-in unity-contract harness for GH issue #92 (autoComp undercompensation).
option(BUILD_TAPEMACHINE_TESTS "Build TapeMachine standalone test harness" OFF)
if(BUILD_TAPEMACHINE_TESTS)
    add_subdirectory(tests)
endif()
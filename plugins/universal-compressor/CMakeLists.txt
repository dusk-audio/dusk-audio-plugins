cmake_minimum_required(VERSION 3.15)

project(UniversalCompressor VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE as a subdirectory (using JUCE from projects folder)
add_subdirectory(/home/marc/projects/JUCE juce_build)

# Create plugin target
juce_add_plugin(UniversalCompressor
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME "AudioEngineering"
    BUNDLE_ID "com.AudioEngineering.UniversalCompressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE AuEn
    PLUGIN_CODE UnCo
    FORMATS AU VST3 LV2
    LV2URI "https://audioengineering.com/plugins/universal-compressor"
    PRODUCT_NAME "Universal Compressor"
    DESCRIPTION "Universal compressor with Opto, FET, VCA, and Bus emulations"
)

# Generate JuceHeader.h
juce_generate_juce_header(UniversalCompressor)

# Add source files
target_sources(UniversalCompressor
    PRIVATE
        UniversalCompressor.cpp
        AnalogLookAndFeel.cpp
        EnhancedCompressorEditor.cpp
)

# Compiler definitions
target_compile_definitions(UniversalCompressor
    PUBLIC
        # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
        JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
        JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# Link libraries
target_link_libraries(UniversalCompressor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(UniversalCompressor PRIVATE -O3 -ffast-math)
endif()

# Enable all warnings
target_compile_options(UniversalCompressor PRIVATE -Wall -Wextra -Wpedantic)
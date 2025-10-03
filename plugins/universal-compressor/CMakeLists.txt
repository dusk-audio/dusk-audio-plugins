cmake_minimum_required(VERSION 3.15)

project(UniversalCompressor VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is already added by parent CMakeLists.txt

# Build format options
option(BUILD_VST3 "Build VST3 format" ON)
option(BUILD_LV2 "Build LV2 format" ON)
option(BUILD_AU "Build AU format" OFF)

# Determine which formats to build
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

# Create plugin target
juce_add_plugin(UniversalCompressor
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME "Luna Co. Audio"
    BUNDLE_ID "com.AudioEngineering.UniversalCompressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE AuEn
    PLUGIN_CODE UnCo
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://audioengineering.com/plugins/universal-compressor"
    PRODUCT_NAME "Universal Compressor"
    DESCRIPTION "Universal compressor with Opto, FET, VCA, and Bus emulations"
    VST3_CAN_REPLACE_VST2 TRUE
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
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
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

# Enable AddressSanitizer in debug builds for memory error detection
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(UniversalCompressor PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(UniversalCompressor PRIVATE -fsanitize=address)
    message(STATUS "AddressSanitizer enabled for UniversalCompressor")
endif()

# Enable all warnings
target_compile_options(UniversalCompressor PRIVATE -Wall -Wextra -Wpedantic)

# Optional: Build unit tests
option(BUILD_UNIVERSAL_COMPRESSOR_TESTS "Build Universal Compressor unit tests" OFF)

if(BUILD_UNIVERSAL_COMPRESSOR_TESTS)
    # Create test executable
    add_executable(UniversalCompressorTests
        UniversalCompressorTests.cpp
        UniversalCompressor.cpp
        AnalogLookAndFeel.cpp
        EnhancedCompressorEditor.cpp
    )

    # Link test executable with JUCE
    target_link_libraries(UniversalCompressorTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
    )

    # Add JUCE definitions
    target_compile_definitions(UniversalCompressorTests
        PRIVATE
            JUCE_UNIT_TESTS=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )

    # Enable AddressSanitizer for tests in debug builds
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(UniversalCompressorTests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(UniversalCompressorTests PRIVATE -fsanitize=address)
        message(STATUS "AddressSanitizer enabled for UniversalCompressorTests")
    endif()

    # Generate JUCE header for tests
    juce_generate_juce_header(UniversalCompressorTests)

    message(STATUS "Universal Compressor unit tests enabled")
    message(STATUS "Run tests with: ./UniversalCompressorTests --all")
    message(STATUS "Run with ASAN: ASAN_OPTIONS=detect_leaks=1 ./UniversalCompressorTests --all")
endif()
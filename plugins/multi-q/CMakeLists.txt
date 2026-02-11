cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment (fallback to default version)
set(MULTIQ_DEFAULT_VERSION "0.9.1")
get_plugin_version("multi-q" MULTIQ_VERSION)
if(NOT MULTIQ_VERSION OR MULTIQ_VERSION STREQUAL "0.0.0")
    set(MULTIQ_VERSION ${MULTIQ_DEFAULT_VERSION})
endif()

project(MultiQ VERSION ${MULTIQ_VERSION})

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is already added by parent CMakeLists.txt

# Build format options
option(BUILD_VST3 "Build VST3 format" ON)
option(BUILD_LV2 "Build LV2 format" ON)
# AU is only supported on macOS
if(APPLE)
    option(BUILD_AU "Build AU format" ON)
else()
    option(BUILD_AU "Build AU format" OFF)
endif()

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
juce_add_plugin(MultiQ
    COMPANY_NAME "Dusk Audio"
    BUNDLE_ID "com.LunaCoAudio.MultiQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Luna
    PLUGIN_CODE MulQ
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://lunacoaudio.com/plugins/multi-q"
    PRODUCT_NAME "Multi-Q"
    DESCRIPTION "Professional 8-band parametric EQ with FFT Analyzer"
    VST3_CAN_REPLACE_VST2 TRUE
    VST3_AUTO_MANIFEST FALSE
)

# Generate JuceHeader.h
juce_generate_juce_header(MultiQ)

# Add source files
target_sources(MultiQ
    PRIVATE
        MultiQ.cpp
        MultiQEditor.cpp
        EQGraphicDisplay.cpp
        FFTAnalyzer.cpp
        BritishEQCurveDisplay.cpp
        PultecCurveDisplay.cpp
        BandDetailPanel.cpp
        ../shared/LEDMeter.cpp
        ../4k-eq/FourKLookAndFeel.cpp
)
# Set include directories
target_include_directories(MultiQ
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
        ${CMAKE_CURRENT_SOURCE_DIR}/../4k-eq
)

# Compiler definitions
target_compile_definitions(MultiQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Set version compile definitions (from git tag or CI)
set_plugin_version_defines(MultiQ ${MULTIQ_VERSION})

# Link libraries
target_link_libraries(MultiQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        # Note: juce_recommended_lto_flags removed - causes Windows CI builds to hang
        # during vst3_helper moduleinfo.json generation (25+ min with no output)
        juce::juce_recommended_warning_flags
)

# Platform-specific compile options
if(MSVC)
    # MSVC optimization and warning flags
    target_compile_options(MultiQ PRIVATE /W4)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        target_compile_options(MultiQ PRIVATE /O2 /fp:fast)
    endif()
else()
    # GCC/Clang optimization and warning flags
    target_compile_options(MultiQ PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        target_compile_options(MultiQ PRIVATE -O3 -ffast-math)
    endif()
endif()

# Set binary output directory (required for CI validation to find built plugins)
set_target_properties(MultiQ PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test target for LinearPhaseEQProcessor
# Disabled for now - use the standalone test_linear_phase.cpp instead
# The JUCE-based test requires complex linking that's better handled by the plugin itself

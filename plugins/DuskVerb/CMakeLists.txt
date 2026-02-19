cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment (fallback to default version)
set(DUSKVERB_DEFAULT_VERSION "0.2.0")
get_plugin_version("duskverb" DUSKVERB_VERSION)
if(NOT DUSKVERB_VERSION OR DUSKVERB_VERSION STREQUAL "0.0.0")
    set(DUSKVERB_VERSION ${DUSKVERB_DEFAULT_VERSION})
endif()

project(DuskVerb VERSION ${DUSKVERB_VERSION})

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
juce_add_plugin(DuskVerb
    COMPANY_NAME "Dusk Audio"
    BUNDLE_ID "com.duskaudio.DuskVerb"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Dusk
    PLUGIN_CODE DkVb
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://dusk-audio.github.io/plugins/duskverb"
    PRODUCT_NAME "DuskVerb"
    DESCRIPTION "Algorithmic reverb with Hadamard FDN"
    VST3_CAN_REPLACE_VST2 TRUE
    VST3_AUTO_MANIFEST FALSE
)

# Generate JuceHeader.h
juce_generate_juce_header(DuskVerb)

# Add source files
target_sources(DuskVerb
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/dsp/FDNReverb.cpp
        src/dsp/DiffusionStage.cpp
        src/dsp/DuskVerbEngine.cpp
        src/dsp/OutputDiffusion.cpp
        src/dsp/EarlyReflections.cpp
        ../shared/LEDMeter.cpp
)

# Set include directories
target_include_directories(DuskVerb
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

# Compiler definitions
target_compile_definitions(DuskVerb
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Set version compile definitions (from git tag or CI)
set_plugin_version_defines(DuskVerb ${DUSKVERB_VERSION})

# Link libraries
target_link_libraries(DuskVerb
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
    target_compile_options(DuskVerb PRIVATE /W4)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        target_compile_options(DuskVerb PRIVATE /O2 /fp:fast)
    endif()
else()
    # GCC/Clang optimization and warning flags
    target_compile_options(DuskVerb PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        target_compile_options(DuskVerb PRIVATE -O3 -ffast-math)
    endif()
endif()

# Set binary output directory (required for CI validation to find built plugins)
set_target_properties(DuskVerb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- IR Test Tool (standalone, no plugin wrapper) ---
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(DuskVerb_IRTest
        src/test/IRGenerator.cpp
        src/dsp/FDNReverb.cpp
        src/dsp/DiffusionStage.cpp
        src/dsp/DuskVerbEngine.cpp
        src/dsp/OutputDiffusion.cpp
        src/dsp/EarlyReflections.cpp
    )

    target_include_directories(DuskVerb_IRTest PRIVATE src)

    target_compile_definitions(DuskVerb_IRTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(DuskVerb_IRTest
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_core
            juce::juce_dsp
    )
endif()

cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment (fallback to default version)
set(FOURKEQ_DEFAULT_VERSION "1.0.9")
get_plugin_version("4k-eq" FOURKEQ_VERSION)
if(NOT FOURKEQ_VERSION OR FOURKEQ_VERSION STREQUAL "0.0.0")
    set(FOURKEQ_VERSION ${FOURKEQ_DEFAULT_VERSION})
endif()

project(FourKEQ VERSION ${FOURKEQ_VERSION})

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is already added by parent CMakeLists.txt

# Define our VST3 and LV2 plugin target
juce_add_plugin(FourKEQ
    PLUGIN_NAME "4K EQ"
    PLUGIN_CODE FKEQ
    PLUGIN_MANUFACTURER_CODE LunC
    FORMATS VST3 LV2 AU Standalone
    PRODUCT_NAME "4K EQ"
    COMPANY_NAME "Luna Co. Audio"
    COMPANY_WEBSITE "https://lunaco.audio"
    COMPANY_EMAIL "support@lunaco.audio"
    BUNDLE_ID "com.LunaCoAudio.FourKEQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${LUNA_COPY_AFTER_BUILD}
    VST3_CATEGORIES Fx EQ
    VST3_AUTO_MANIFEST FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    LV2_URI "https://lunaco.audio/plugins/4keq"
    LV2_SHARED_LIBRARY_NAME FourKEQ
)

# Generate JUCE header
juce_generate_juce_header(FourKEQ)

# Add source files
target_sources(FourKEQ
    PRIVATE
        FourKEQ.cpp
        FourKEQ.h
        PluginEditor.cpp
        PluginEditor.h
        FourKLookAndFeel.cpp
        FourKLookAndFeel.h
        EQCurveDisplay.cpp
        EQCurveDisplay.h
        SSLSaturation.h
        ../shared/PatreonBackers.h
        ../shared/SupportersOverlay.h
        ../shared/LEDMeter.cpp
        ../shared/LEDMeter.h
)

# Set include directories
target_include_directories(FourKEQ
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

# Compile definitions
target_compile_definitions(FourKEQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Set version compile definitions (from git tag or CI)
set_plugin_version_defines(FourKEQ ${FOURKEQ_VERSION})

# Link with JUCE modules
target_link_libraries(FourKEQ
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
        juce::juce_recommended_warning_flags
)

# Platform-specific settings
if(MSVC)
    target_compile_options(FourKEQ PRIVATE /W4 /WX-)
else()
    target_compile_options(FourKEQ PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Set binary output directory
set_target_properties(FourKEQ PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

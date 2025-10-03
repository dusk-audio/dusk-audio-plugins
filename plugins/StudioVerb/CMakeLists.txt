cmake_minimum_required(VERSION 3.15)

# Project name and version
project(StudioVerb VERSION 1.0.0)

# Task 8: Add CMake LSP support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Build options
option(BUILD_AU "Build AU format" ON)
option(BUILD_VST3 "Build VST3 format" ON)
option(BUILD_LV2 "Build LV2 format" ON)
option(BUILD_STANDALONE "Build Standalone format" ON)
option(ENABLE_ASAN "Enable AddressSanitizer for debug builds" ON)  # ON by default to catch bugs early

# Determine formats to build
set(FORMATS "")
if(BUILD_AU AND APPLE)
    list(APPEND FORMATS AU)
endif()
if(BUILD_VST3)
    list(APPEND FORMATS VST3)
endif()
if(BUILD_LV2)
    list(APPEND FORMATS LV2)
endif()
if(BUILD_STANDALONE)
    list(APPEND FORMATS Standalone)
endif()

# Add JUCE as a subdirectory
# JUCE is already added in the parent CMakeLists.txt

# Add the plugin target
juce_add_plugin(StudioVerb
    COMPANY_NAME "Luna CO. Audio"
    COMPANY_WEBSITE "https://lunaco.audio"
    COMPANY_EMAIL "support@lunaco.audio"

    PLUGIN_MANUFACTURER_CODE LuCo
    PLUGIN_CODE StuV

    FORMATS ${FORMATS}  # Configurable formats

    PRODUCT_NAME "Studio Verb"

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    COPY_PLUGIN_AFTER_BUILD TRUE

    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect

    ICON_BIG ""
    ICON_SMALL ""

    MICROPHONE_PERMISSION_ENABLED FALSE
    MICROPHONE_PERMISSION_TEXT "This plugin does not require microphone access"
)

# Generate JUCE header
juce_generate_juce_header(StudioVerb)

# Task 6: Add LV2 manifest generation
if(LV2)
    juce_add_lv2_manifest(StudioVerb)
endif()

# Add source files
target_sources(StudioVerb
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
        ReverbEngine.cpp
        ReverbEngine.h
)

# Compile definitions
target_compile_definitions(StudioVerb
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1  # Fix VST3 parameter conflict
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

# Link libraries
target_link_libraries(StudioVerb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Enable AddressSanitizer for debug builds if requested
if(ENABLE_ASAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # GCC and Clang support
        target_compile_options(StudioVerb PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
            $<$<CONFIG:Debug>:-g>
        )
        target_link_options(StudioVerb PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address>
        )
        message(STATUS "AddressSanitizer enabled for StudioVerb (Debug builds only) - Compiler: ${CMAKE_CXX_COMPILER_ID}")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # MSVC support (requires Visual Studio 2019 16.9 or later)
        target_compile_options(StudioVerb PRIVATE
            $<$<CONFIG:Debug>:/fsanitize=address>
        )
        # MSVC automatically links ASAN runtime when /fsanitize=address is used
        message(STATUS "AddressSanitizer enabled for StudioVerb (Debug builds only) - MSVC")
    else()
        message(WARNING "AddressSanitizer requested but not supported for compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

# Set binary output directory
set_target_properties(StudioVerb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
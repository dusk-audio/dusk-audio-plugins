cmake_minimum_required(VERSION 3.16)

project(DrummerClone VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find JUCE - only add if not already available (standalone vs parent build)
if(NOT TARGET juce::juce_core)
    if(EXISTS "/home/marc/projects/JUCE/CMakeLists.txt")
        add_subdirectory("/home/marc/projects/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" "${CMAKE_BINARY_DIR}/JUCE")
    elseif(EXISTS "$ENV{HOME}/JUCE/CMakeLists.txt")
        add_subdirectory("$ENV{HOME}/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
    else()
        find_package(JUCE CONFIG REQUIRED)
    endif()
endif()

# Add the plugin - VST3 and LV2 formats for Linux
# Note: We use IS_SYNTH TRUE to enable audio sidechain input for Follow Mode
juce_add_plugin(DrummerClone
    COMPANY_NAME "Luna Co. Audio"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE DrCl
    FORMATS VST3 LV2
    LV2URI "https://github.com/drummerclone/drummerclone"
    PRODUCT_NAME "DrummerClone"
)

# Source files
target_sources(DrummerClone
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/DrumMapping.h
        Source/TransientDetector.cpp
        Source/TransientDetector.h
        Source/MidiGrooveExtractor.cpp
        Source/MidiGrooveExtractor.h
        Source/GrooveTemplateGenerator.cpp
        Source/GrooveTemplateGenerator.h
        Source/GrooveFollower.cpp
        Source/GrooveFollower.h
        Source/GrooveLearner.cpp
        Source/GrooveLearner.h
        Source/DrummerEngine.cpp
        Source/DrummerEngine.h
        Source/FollowModePanel.cpp
        Source/FollowModePanel.h
        Source/DrummerDNA.cpp
        Source/DrummerDNA.h
        Source/VariationEngine.cpp
        Source/VariationEngine.h
        Source/MidiExporter.cpp
        Source/MidiExporter.h
        Source/StepSequencer.cpp
        Source/StepSequencer.h
        Source/ProfileEditorPanel.cpp
        Source/ProfileEditorPanel.h
        Source/PatternLibrary.cpp
        Source/PatternLibrary.h
)

# Preprocessor definitions
target_compile_definitions(DrummerClone
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Link JUCE modules
target_link_libraries(DrummerClone
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
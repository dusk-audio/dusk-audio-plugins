cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment (fallback to default version)
set(CHORDANALYZER_DEFAULT_VERSION "1.1.1")
get_plugin_version("chord-analyzer" CHORDANALYZER_VERSION)
if(NOT CHORDANALYZER_VERSION OR CHORDANALYZER_VERSION STREQUAL "0.0.0")
    set(CHORDANALYZER_VERSION ${CHORDANALYZER_DEFAULT_VERSION})
endif()

project(ChordAnalyzer VERSION ${CHORDANALYZER_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Add the plugin - MIDI Effect
juce_add_plugin(ChordAnalyzer
    COMPANY_NAME "Dusk Audio"
    BUNDLE_ID "com.DuskAudio.ChordAnalyzer"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Dusk
    PLUGIN_CODE ChAn
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://dusk-audio.github.io/plugins/chord-analyzer"
    PRODUCT_NAME "Chord Analyzer"
    VST3_CAN_REPLACE_VST2 FALSE
    VST3_AUTO_MANIFEST FALSE
)

apply_clap_extension(ChordAnalyzer chord-analyzer note-effect analyzer mono stereo)

juce_generate_juce_header(ChordAnalyzer)

# Source files
target_sources(ChordAnalyzer
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/ChordAnalyzer.cpp
        Source/ChordAnalyzer.h
        Source/ChordRecorder.cpp
        Source/ChordRecorder.h
        Source/TheoryTooltips.h
        Source/ChordAnalyzerLookAndFeel.h
)

# Include directories
target_include_directories(ChordAnalyzer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

# Preprocessor definitions
target_compile_definitions(ChordAnalyzer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
        JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
)

# Link JUCE modules
target_link_libraries(ChordAnalyzer
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Test target
add_executable(ChordAnalyzerTest EXCLUDE_FROM_ALL
    tests/test_chord_analyzer.cpp
    Source/ChordAnalyzer.cpp
)
target_include_directories(ChordAnalyzerTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(ChordAnalyzerTest PRIVATE
    juce::juce_core
)
target_compile_definitions(ChordAnalyzerTest PRIVATE
    JUCE_STANDALONE_APPLICATION=1
)

# Optimization for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ChordAnalyzer PRIVATE -O3 -ffast-math -fno-finite-math-only)
    endif()
endif()

# ===== MIDI variant (pure MIDI effect, no audio buses) =====
# JUCE-built across all formats including LV2 — desktop LV2 hosts (Ardour, Carla)
# get the full custom UI here. For Zynthian and other headless LV2 hosts that
# need native lv2:OutputPort+ControlPort to display chord info in their own UI,
# see the separate "Chord Analyzer Headless" native LV2 build below.
juce_add_plugin(ChordAnalyzerMIDI
    COMPANY_NAME "Dusk Audio"
    BUNDLE_ID "com.DuskAudio.ChordAnalyzerMIDI"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Dusk
    PLUGIN_CODE ChMi
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://dusk-audio.github.io/plugins/chord-analyzer-midi"
    PRODUCT_NAME "Chord Analyzer MIDI"
    VST3_CAN_REPLACE_VST2 FALSE
    VST3_AUTO_MANIFEST FALSE
)

juce_generate_juce_header(ChordAnalyzerMIDI)

target_sources(ChordAnalyzerMIDI
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/ChordAnalyzer.cpp
        Source/ChordAnalyzer.h
        Source/ChordRecorder.cpp
        Source/ChordRecorder.h
        Source/TheoryTooltips.h
        Source/ChordAnalyzerLookAndFeel.h
)

target_include_directories(ChordAnalyzerMIDI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_compile_definitions(ChordAnalyzerMIDI
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
        JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
        CHORD_ANALYZER_MIDI_MODE=1
)

target_link_libraries(ChordAnalyzerMIDI
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ChordAnalyzerMIDI PRIVATE -O3 -ffast-math -fno-finite-math-only)
    endif()
endif()

# ===== Headless variant (native LV2 only, for Zynthian + other headless LV2 hosts) =====
# Hand-rolled because JUCE's LV2 wrapper can't emit lv2:OutputPort + lv2:ControlPort
# for plugin parameters (everything goes through patch:Message). Zynthian only
# renders values written to native output control ports, so we wrap ChordAnalyzer
# in a thin LV2 plugin that declares the right ports. No GUI — host renders the
# control ports in its own UI (this is the entire point: lets Zynthian display
# the detected chord without our editor).
if("LV2" IN_LIST PLUGIN_FORMATS)
    # Locate the LV2 SDK headers bundled inside JUCE.
    # - JUCE 8.0.4 (CI baseline): modules/juce_audio_processors/format_types/LV2_SDK/
    # - JUCE 8.0.11+: SDK relocated into a new modules/juce_audio_processors_headless/
    #   module (the original location no longer contains LV2_SDK).
    # We try both layouts against every JUCE root the top-level CMakeLists supports.
    set(_juce_roots
        "${JUCE_PATH}"
        "${CMAKE_SOURCE_DIR}/../JUCE"
        "${CMAKE_SOURCE_DIR}/external/JUCE"
        "${CMAKE_SOURCE_DIR}/JUCE"
    )
    set(_juce_subdirs
        "modules/juce_audio_processors/format_types/LV2_SDK/lv2"
        "modules/juce_audio_processors_headless/format_types/LV2_SDK/lv2"
    )
    set(LV2_SDK_CANDIDATES "")
    foreach(_root ${_juce_roots})
        foreach(_sub ${_juce_subdirs})
            list(APPEND LV2_SDK_CANDIDATES "${_root}/${_sub}")
        endforeach()
    endforeach()
    set(LV2_SDK_DIR "")
    foreach(_candidate ${LV2_SDK_CANDIDATES})
        if(EXISTS "${_candidate}/lv2/core/lv2.h")
            set(LV2_SDK_DIR "${_candidate}")
            break()
        endif()
    endforeach()
    if(NOT LV2_SDK_DIR)
        message(FATAL_ERROR
            "Could not locate LV2 SDK headers in JUCE. Searched: ${LV2_SDK_CANDIDATES}")
    endif()
    message(STATUS "ChordAnalyzerHeadless_LV2: using LV2 SDK at ${LV2_SDK_DIR}")

    add_library(ChordAnalyzerHeadless_LV2 MODULE
        lv2/chord-analyzer-lv2.cpp
        Source/ChordAnalyzer.cpp
    )

    target_include_directories(ChordAnalyzerHeadless_LV2 PRIVATE
        Source
        "${LV2_SDK_DIR}"
    )

    target_link_libraries(ChordAnalyzerHeadless_LV2 PRIVATE
        juce::juce_core
    )

    set_target_properties(ChordAnalyzerHeadless_LV2 PROPERTIES
        PREFIX ""
        SUFFIX ".so"           # LV2 convention: .so on every platform (incl. macOS)
        OUTPUT_NAME "ChordAnalyzerHeadless"
        POSITION_INDEPENDENT_CODE ON
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(ChordAnalyzerHeadless_LV2 PRIVATE -O3 -ffast-math -fno-finite-math-only)
        endif()
    endif()

    # Assemble the .lv2 bundle (binary + manifest.ttl + chord-analyzer-headless.ttl)
    set(CHORDANALYZER_HEADLESS_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/Chord Analyzer Headless.lv2")

    add_custom_command(TARGET ChordAnalyzerHeadless_LV2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CHORDANALYZER_HEADLESS_BUNDLE}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ChordAnalyzerHeadless_LV2>"
            "${CHORDANALYZER_HEADLESS_BUNDLE}/ChordAnalyzerHeadless.so"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/lv2/manifest.ttl"
            "${CHORDANALYZER_HEADLESS_BUNDLE}/manifest.ttl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/lv2/chord-analyzer-headless.ttl"
            "${CHORDANALYZER_HEADLESS_BUNDLE}/chord-analyzer-headless.ttl"
        COMMENT "Assembling Chord Analyzer Headless.lv2 bundle"
    )

    if(DUSK_COPY_AFTER_BUILD)
        if(APPLE)
            set(LV2_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/LV2")
        elseif(UNIX)
            set(LV2_INSTALL_DIR "$ENV{HOME}/.lv2")
        elseif(WIN32)
            set(LV2_INSTALL_DIR "$ENV{APPDATA}/LV2")
        endif()

        if(DEFINED LV2_INSTALL_DIR)
            add_custom_command(TARGET ChordAnalyzerHeadless_LV2 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${LV2_INSTALL_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CHORDANALYZER_HEADLESS_BUNDLE}"
                    "${LV2_INSTALL_DIR}/Chord Analyzer Headless.lv2"
                COMMENT "Installing Chord Analyzer Headless.lv2 to ${LV2_INSTALL_DIR}"
            )
        endif()
    endif()
endif()

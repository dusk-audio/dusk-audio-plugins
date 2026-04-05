cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment (fallback to default version)
set(DUSKAMP_DEFAULT_VERSION "0.1.0")
get_plugin_version("duskamp" DUSKAMP_VERSION)
if(NOT DUSKAMP_VERSION OR DUSKAMP_VERSION STREQUAL "0.0.0")
    set(DUSKAMP_VERSION ${DUSKAMP_DEFAULT_VERSION})
endif()

project(DuskAmp VERSION ${DUSKAMP_VERSION})

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is already added by parent CMakeLists.txt

# --- External dependencies (NAM) ---
set(NAM_DIR "${CMAKE_SOURCE_DIR}/external/NeuralAmpModelerCore")
# Use NAM's bundled Eigen and nlohmann_json from its Dependencies/ folder
set(NAM_DEPS_DIR "${NAM_DIR}/Dependencies")
set(EIGEN_DIR "${NAM_DEPS_DIR}/eigen")
set(JSON_DIR "${NAM_DEPS_DIR}/nlohmann")

option(BUILD_NAM_SUPPORT "Build with Neural Amp Modeler support (requires submodules)" ON)

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
juce_add_plugin(DuskAmp
    COMPANY_NAME "Dusk Audio"
    BUNDLE_ID "com.duskaudio.DuskAmp"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Dusk
    PLUGIN_CODE DkAm
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://dusk-audio.github.io/plugins/duskamp"
    PRODUCT_NAME "DuskAmp"
    DESCRIPTION "Guitar amp simulator with DSP and NAM hybrid engine"
    VST3_CAN_REPLACE_VST2 TRUE
    VST3_AUTO_MANIFEST FALSE
)

# Generate JuceHeader.h
juce_generate_juce_header(DuskAmp)

# --- NAM source files (conditional) ---
if(BUILD_NAM_SUPPORT AND EXISTS "${NAM_DIR}/NAM/dsp.cpp")
    set(NAM_SOURCES
        ${NAM_DIR}/NAM/activations.cpp
        ${NAM_DIR}/NAM/container.cpp
        ${NAM_DIR}/NAM/conv1d.cpp
        ${NAM_DIR}/NAM/convnet.cpp
        ${NAM_DIR}/NAM/dsp.cpp
        ${NAM_DIR}/NAM/get_dsp.cpp
        ${NAM_DIR}/NAM/lstm.cpp
        ${NAM_DIR}/NAM/ring_buffer.cpp
        ${NAM_DIR}/NAM/slimmable_wavenet.cpp
        ${NAM_DIR}/NAM/util.cpp
        ${NAM_DIR}/NAM/wavenet.cpp
    )
    set(NAM_ENABLED TRUE)
    message(STATUS "DuskAmp: NAM support enabled")
else()
    set(NAM_SOURCES "")
    set(NAM_ENABLED FALSE)
    message(STATUS "DuskAmp: NAM support disabled (submodules not found)")
endif()

# Add source files
target_sources(DuskAmp
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/ui/DuskAmpLookAndFeel.cpp
        src/dsp/DuskAmpEngine.cpp
        src/dsp/GainStage.cpp
        src/dsp/PreampChain.cpp
        src/dsp/ToneStack.cpp
        src/dsp/PowerAmp.cpp
        src/dsp/ProceduralCab.cpp
        src/dsp/CabinetIR.cpp
        src/dsp/PostFX.cpp
        src/dsp/wdf/MarshallPlexiWDF.cpp
        src/dsp/wdf/FenderDeluxeWDF.cpp
        src/dsp/wdf/VoxAC30WDF.cpp
        ../shared/LEDMeter.cpp
        ${NAM_SOURCES}
)

# Browser components always compiled; NAMProcessor only with NAM support
target_sources(DuskAmp
    PRIVATE
        src/ui/FileBrowserPanel.cpp
)

if(NAM_ENABLED)
    target_sources(DuskAmp
        PRIVATE
            src/dsp/NAMProcessor.cpp
    )
endif()

# Set include directories
target_include_directories(DuskAmp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
        ${CMAKE_SOURCE_DIR}/external/chowdsp_wdf/include
)

if(NAM_ENABLED)
    target_include_directories(DuskAmp
        PUBLIC
            ${EIGEN_DIR}
            ${JSON_DIR}
            ${NAM_DIR}
    )
endif()

# Compiler definitions
target_compile_definitions(DuskAmp
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

if(NAM_ENABLED)
    target_compile_definitions(DuskAmp
        PUBLIC
            DUSKAMP_NAM_SUPPORT=1
            EIGEN_MPL2_ONLY         # Restrict Eigen to MPL2-licensed code only
    )
else()
    target_compile_definitions(DuskAmp
        PUBLIC
            DUSKAMP_NAM_SUPPORT=0
    )
endif()

# Set version compile definitions (from git tag or CI)
set_plugin_version_defines(DuskAmp ${DUSKAMP_VERSION})

# Link libraries
target_link_libraries(DuskAmp
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
    # /FS forces synchronous PDB writes (prevents mspdbsrv.exe hangs on CI)
    target_compile_options(DuskAmp PRIVATE /W4 /FS)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        target_compile_options(DuskAmp PRIVATE /O2 /fp:fast)
    endif()
else()
    # GCC/Clang optimization and warning flags
    target_compile_options(DuskAmp PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        target_compile_options(DuskAmp PRIVATE -O3 -ffast-math)
    endif()
endif()

# Set binary output directory (required for CI validation to find built plugins)
set_target_properties(DuskAmp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test executable: full NAM integration test
add_executable(DuskAmpNAMTest EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/test_nam_full.cpp
)
target_link_libraries(DuskAmpNAMTest PRIVATE
    DuskAmp
    juce::juce_audio_utils
    juce::juce_dsp
)
target_include_directories(DuskAmpNAMTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)
if(NAM_ENABLED)
    target_include_directories(DuskAmpNAMTest PRIVATE ${EIGEN_DIR} ${JSON_DIR} ${NAM_DIR})
    target_compile_definitions(DuskAmpNAMTest PRIVATE DUSKAMP_NAM_SUPPORT=1 EIGEN_MPL2_ONLY)
else()
    target_compile_definitions(DuskAmpNAMTest PRIVATE DUSKAMP_NAM_SUPPORT=0)
endif()

# Test executable: gain staging calibration (DSP vs NAM A/B)
add_executable(DuskAmpCalibration EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/test_gain_calibration.cpp
)
target_link_libraries(DuskAmpCalibration PRIVATE
    DuskAmp
    juce::juce_audio_utils
    juce::juce_dsp
)
target_include_directories(DuskAmpCalibration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)
if(NAM_ENABLED)
    target_include_directories(DuskAmpCalibration PRIVATE ${EIGEN_DIR} ${JSON_DIR} ${NAM_DIR})
    target_compile_definitions(DuskAmpCalibration PRIVATE DUSKAMP_NAM_SUPPORT=1 EIGEN_MPL2_ONLY)
else()
    target_compile_definitions(DuskAmpCalibration PRIVATE DUSKAMP_NAM_SUPPORT=0)
endif()

# Test executable: WDF model validation
add_executable(DuskAmpWDFValidate EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/test_wdf_validate.cpp
)
target_link_libraries(DuskAmpWDFValidate PRIVATE
    DuskAmp
    juce::juce_audio_utils
    juce::juce_dsp
)
target_include_directories(DuskAmpWDFValidate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)
if(NAM_ENABLED)
    target_include_directories(DuskAmpWDFValidate PRIVATE ${EIGEN_DIR} ${JSON_DIR} ${NAM_DIR})
    target_compile_definitions(DuskAmpWDFValidate PRIVATE DUSKAMP_NAM_SUPPORT=1 EIGEN_MPL2_ONLY)
else()
    target_compile_definitions(DuskAmpWDFValidate PRIVATE DUSKAMP_NAM_SUPPORT=0)
endif()

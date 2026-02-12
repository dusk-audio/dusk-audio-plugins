cmake_minimum_required(VERSION 3.15)
project(DuskPlugins VERSION 1.0.0)

# C++ standard is set per-plugin:
# - Most plugins use C++17 (set in their own CMakeLists.txt)
# - Neural Amp uses C++20 (required for NAM Core)
# This avoids conflicts and ensures each plugin compiles with its required standard

# Include global settings
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GlobalSettings.cmake)

# Include common JUCE defaults
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/JuceDefaults.cmake)

# Build system notes:
# - JUCE path: configurable via JUCE_PATH variable
# - Default: ../JUCE (sibling directory for local dev)
# - CI builds pass -DJUCE_PATH=/path/to/JUCE
# - Plugins install to ~/.vst3/ and ~/.lv2/

# JUCE path configuration
# Priority: 1) JUCE_PATH cmake variable, 2) sibling directory, 3) external/JUCE submodule
if(DEFINED JUCE_PATH)
    # Use explicitly provided path (CI builds)
    message(STATUS "Using JUCE from: ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_CURRENT_BINARY_DIR}/JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
    # Local development: sibling directory
    message(STATUS "Using JUCE from sibling directory: ${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    # Git submodule
    message(STATUS "Using JUCE from submodule: external/JUCE")
    add_subdirectory(external/JUCE)
else()
    message(FATAL_ERROR "JUCE not found! Options:\n"
        "  1. Set -DJUCE_PATH=/path/to/JUCE\n"
        "  2. Clone JUCE to ../JUCE (sibling directory)\n"
        "  3. Add as submodule: git submodule add https://github.com/juce-framework/JUCE.git external/JUCE")
endif()

# CI builds don't need to install plugins to system directories
option(DUSK_COPY_AFTER_BUILD "Copy plugins to system VST3/LV2 dirs after build" ON)

# Option to build all plugins or specific ones
option(BUILD_4K_EQ "Build 4K EQ plugin" ON)
option(BUILD_MULTI_COMP "Build Multi-Comp plugin" ON)
option(BUILD_HARMONIC_GENERATOR "Build Harmonic Generator plugin" OFF)  # Directory missing
option(BUILD_TAPE_MACHINE "Build TapeMachine plugin" ON)
option(BUILD_GROOVEMIND "Build GrooveMind plugin" ON)  # ML-based intelligent drummer
option(BUILD_CONVOLUTION_REVERB "Build Convolution Reverb plugin" ON)
option(BUILD_VELVET90 "Build Velvet 90 plugin" ON)
option(BUILD_SUEDE200 "Build Suede 200 plugin" ON)
option(BUILD_MULTI_Q "Build Multi-Q plugin" ON)
option(BUILD_NEURAL_AMP "Build Neural Amp plugin" OFF)  # Disabled: Eigen dependency adds significant compile time
option(BUILD_TAPE_ECHO "Build Tape Echo plugin" ON)
option(BUILD_CHORD_ANALYZER "Build Chord Analyzer plugin" ON)
option(BUILD_SPECTRUM_ANALYZER "Build Spectrum Analyzer plugin" ON)

# Add plugin subdirectories
if(BUILD_4K_EQ)
    add_subdirectory(plugins/4k-eq)
endif()

if(BUILD_MULTI_COMP)
    add_subdirectory(plugins/multi-comp)
endif()

if(BUILD_HARMONIC_GENERATOR)
    add_subdirectory(plugins/harmonic-generator)
endif()

if(BUILD_TAPE_MACHINE)
    add_subdirectory(plugins/TapeMachine)
endif()

if(BUILD_GROOVEMIND)
    add_subdirectory(plugins/groovemind)
endif()

if(BUILD_CONVOLUTION_REVERB)
    add_subdirectory(plugins/convolution-reverb)
endif()

if(BUILD_VELVET90)
    add_subdirectory(plugins/Velvet90)
endif()

if(BUILD_SUEDE200)
    add_subdirectory(plugins/Suede200)
endif()

if(BUILD_MULTI_Q)
    add_subdirectory(plugins/multi-q)
endif()

if(BUILD_NEURAL_AMP)
    add_subdirectory(plugins/neural-amp)
endif()

if(BUILD_TAPE_ECHO)
    add_subdirectory(plugins/tape-echo)
endif()

if(BUILD_CHORD_ANALYZER)
    add_subdirectory(plugins/chord-analyzer)
endif()

if(BUILD_SPECTRUM_ANALYZER)
    add_subdirectory(plugins/spectrum-analyzer)
endif()
cmake_minimum_required(VERSION 3.15)
project(LunaPlugins VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include global settings
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GlobalSettings.cmake)

# Include common JUCE defaults
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/JuceDefaults.cmake)

# Build system updated to:
# - Fix JUCE path from /home/marc/Projects/JUCE to /home/marc/projects/JUCE
# - Add TapeMachine plugin (currently disabled due to source code compilation errors)
# - Ensure proper plugin installation to ~/.vst3/ and ~/.lv2/

# Add JUCE as a subdirectory
# Option 1: Use system JUCE installation
# add_subdirectory(/home/marc/Projects/JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE)

# Option 2: Use JUCE as git submodule (recommended)
# Run: git submodule add https://github.com/juce-framework/JUCE.git external/JUCE
# add_subdirectory(external/JUCE)

# For now, using system installation
add_subdirectory(/home/marc/projects/JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE)

# Option to build all plugins or specific ones
option(BUILD_4K_EQ "Build 4K EQ plugin" ON)
option(BUILD_UNIVERSAL_COMPRESSOR "Build Universal Compressor plugin" ON)
option(BUILD_HARMONIC_GENERATOR "Build Harmonic Generator plugin" ON)
option(BUILD_TAPE_MACHINE "Build TapeMachine plugin" ON)  # Fixed compilation errors
option(BUILD_STUDIO_REVERB "Build Studio Reverb plugin" ON)

# Add plugin subdirectories
if(BUILD_4K_EQ)
    add_subdirectory(plugins/4k-eq)
endif()

if(BUILD_UNIVERSAL_COMPRESSOR)
    add_subdirectory(plugins/universal-compressor)
endif()

if(BUILD_HARMONIC_GENERATOR)
    add_subdirectory(plugins/harmonic-generator)
endif()

if(BUILD_TAPE_MACHINE)
    add_subdirectory(plugins/TapeMachine)
endif()

if(BUILD_STUDIO_REVERB)
    add_subdirectory(plugins/StudioReverb)
endif()
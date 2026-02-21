# Global settings for all plugins in the project
# This file sets compile definitions that need to be applied globally

# Set global compile definitions for all targets
add_compile_definitions(
    JUCE_VST3_CAN_REPLACE_VST2=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

# Additional global settings can be added here
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Windows: Use static runtime to avoid VC++ redistributable dependency
# This prevents crashes on Windows when the redistributable is not installed
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

# Export compile commands for better IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable all warnings in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Wall -Wextra -Wpedantic)
    elseif(MSVC)
        add_compile_options(/W4)
    endif()
endif()

#==============================================================================
# Build Speed Optimizations
#==============================================================================

# Compiler caching (ccache/sccache) — dramatically speeds up rebuilds
find_program(CCACHE_PROGRAM ccache)
find_program(SCCACHE_PROGRAM sccache)
if(SCCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    message(STATUS "sccache found: ${SCCACHE_PROGRAM}")
elseif(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache found: ${CCACHE_PROGRAM}")
endif()

# Unity builds — batch source files to reduce header parsing overhead
# Enable with: cmake -DDUSK_UNITY_BUILD=ON ..
# Note: Linux only. macOS ObjC++ JUCE modules are incompatible with unity builds.
option(DUSK_UNITY_BUILD "Enable unity builds for faster compilation (Linux)" OFF)
if(DUSK_UNITY_BUILD)
    if(APPLE)
        message(WARNING "DUSK_UNITY_BUILD is not supported on macOS (ObjC++ JUCE modules are incompatible). Ignoring.")
    else()
        set(CMAKE_UNITY_BUILD ON)
        set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
        message(STATUS "Unity builds enabled (batch size: 8)")
    endif()
endif()

# Note: Precompiled headers (PCH) are not used because JUCE module compilation
# is incompatible with force-included headers (.mm and split module .cpp files).
# Build speed comes from ccache (automatic) + unity builds (opt-in) + Ninja.

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
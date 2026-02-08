cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment (fallback to default version)
set(MULTICOMP_DEFAULT_VERSION "1.2.5")
get_plugin_version("multi-comp" MULTICOMP_VERSION)
if(NOT MULTICOMP_VERSION OR MULTICOMP_VERSION STREQUAL "0.0.0")
    set(MULTICOMP_VERSION ${MULTICOMP_DEFAULT_VERSION})
endif()

project(MultiComp VERSION ${MULTICOMP_VERSION})

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
juce_add_plugin(MultiComp
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME "Luna Co. Audio"
    BUNDLE_ID "com.LunaCoAudio.MultiComp"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${LUNA_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE LuCo
    PLUGIN_CODE MuCo
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://lunacoaudio.com/plugins/multi-comp"
    PRODUCT_NAME "Multi-Comp"
    DESCRIPTION "Multi-mode compressor with Opto, FET, VCA, Bus emulations and 4-band multiband compression"
    VST3_CAN_REPLACE_VST2 TRUE
    VST3_AUTO_MANIFEST FALSE
)

# Generate JuceHeader.h
juce_generate_juce_header(MultiComp)

# Add source files
target_sources(MultiComp
    PRIVATE
        multicomp.cpp
        AnalogLookAndFeel.cpp
        EnhancedCompressorEditor.cpp
        ../shared/LEDMeter.cpp
        ../shared/PatreonBackers.h
        ../shared/SupportersOverlay.h
)

# Set include directories
target_include_directories(MultiComp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

# Compiler definitions
target_compile_definitions(MultiComp
    PUBLIC
        # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
        JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
        JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Set version compile definitions (from git tag or CI)
set_plugin_version_defines(MultiComp ${MULTICOMP_VERSION})

# Link libraries
# Note: juce_recommended_lto_flags removed to avoid GCC 11 internal compiler errors
# LTO can cause ICE (internal compiler errors) in GCC 11 with complex VST3 code
target_link_libraries(MultiComp
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Optimization flags (GCC/Clang only)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Use -ffast-math for performance but preserve NaN/Inf handling with -fno-finite-math-only
        # This allows fast floating point math while keeping correct NaN/Inf semantics for DSP stability tests
        target_compile_options(MultiComp PRIVATE -O3 -ffast-math -fno-finite-math-only)
    elseif(MSVC)
        # MSVC equivalent: /O2 for optimization, /fp:fast for fast floating point
        target_compile_options(MultiComp PRIVATE /O2 /fp:fast)
    endif()
endif()

# AddressSanitizer options
option(ENABLE_ASAN "Enable AddressSanitizer for memory error detection" OFF)

# Enable AddressSanitizer in debug builds or when explicitly requested (GCC/Clang only)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR ENABLE_ASAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(MultiComp PRIVATE
            -fsanitize=address
            -fsanitize=undefined
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
        )
        target_link_options(MultiComp PRIVATE
            -fsanitize=address
            -fsanitize=undefined
        )
        message(STATUS "AddressSanitizer enabled for MultiComp")
        message(STATUS "Run with: ASAN_OPTIONS=detect_leaks=1:abort_on_error=1")
    elseif(MSVC)
        # MSVC AddressSanitizer support
        target_compile_options(MultiComp PRIVATE /fsanitize=address)
        message(STATUS "AddressSanitizer enabled for MultiComp (MSVC)")
    endif()
endif()

# Enable all warnings (compiler-specific)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MultiComp PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(MultiComp PRIVATE /W4)
endif()

# Set binary output directory (required for GitHub Actions artifact collection)
set_target_properties(MultiComp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Optional: Build unit tests
option(BUILD_MULTI_COMP_TESTS "Build Multi-Comp unit tests" OFF)

if(BUILD_MULTI_COMP_TESTS)
    # Create test executable
    add_executable(MultiCompTests
        TestRunner.cpp
        UniversalCompressorTests.cpp
        multicomp.cpp
        AnalogLookAndFeel.cpp
        EnhancedCompressorEditor.cpp
        ../shared/LEDMeter.cpp
    )

    # Link test executable with JUCE
    target_link_libraries(MultiCompTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_devices
            juce::juce_audio_formats
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
    )

    # Add JUCE definitions
    target_compile_definitions(MultiCompTests
        PRIVATE
            JUCE_UNIT_TESTS=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            JUCE_VST3_CAN_REPLACE_VST2=1
            JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
    )

    # Enable AddressSanitizer for tests in debug builds or when explicitly requested (compiler-specific)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR ENABLE_ASAN)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(MultiCompTests PRIVATE
                -fsanitize=address
                -fsanitize=undefined
                -fno-omit-frame-pointer
                -fno-optimize-sibling-calls
            )
            target_link_options(MultiCompTests PRIVATE
                -fsanitize=address
                -fsanitize=undefined
            )
            message(STATUS "AddressSanitizer and UndefinedBehaviorSanitizer enabled for MultiCompTests")
        elseif(MSVC)
            target_compile_options(MultiCompTests PRIVATE /fsanitize=address)
            message(STATUS "AddressSanitizer enabled for MultiCompTests (MSVC)")
        endif()
    endif()

    # Include directories for tests (use the plugin's generated header)
    target_include_directories(MultiCompTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../shared
            ${CMAKE_CURRENT_BINARY_DIR}/MultiComp_artefacts/JuceLibraryCode
    )

    # Add dependency on plugin target so JuceHeader.h is generated first
    add_dependencies(MultiCompTests MultiComp_All)

    message(STATUS "Multi-Comp unit tests enabled")
    message(STATUS "Run tests with: ./MultiCompTests --all")
    message(STATUS "Run with ASAN: ASAN_OPTIONS=detect_leaks=1 ./MultiCompTests --all")
endif()
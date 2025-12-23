cmake_minimum_required(VERSION 3.15)

project(MultiComp VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is already added by parent CMakeLists.txt

# Build format options
option(BUILD_VST3 "Build VST3 format" ON)
option(BUILD_LV2 "Build LV2 format" ON)
option(BUILD_AU "Build AU format" OFF)

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
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE LuCo
    PLUGIN_CODE MuCo
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "https://lunacoaudio.com/plugins/multi-comp"
    PRODUCT_NAME "Multi-Comp"
    DESCRIPTION "Multi-mode compressor with Opto, FET, VCA, Bus emulations and 4-band multiband compression"
    VST3_CAN_REPLACE_VST2 TRUE
)

# Generate JuceHeader.h
juce_generate_juce_header(MultiComp)

# Add source files
target_sources(MultiComp
    PRIVATE
        UniversalCompressor.cpp
        AnalogLookAndFeel.cpp
        EnhancedCompressorEditor.cpp
        ../shared/LEDMeter.cpp
        ../../shared/PatreonBackers.h
)

# Set include directories
target_include_directories(MultiComp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
        ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
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

# Optional: Build unit tests
option(BUILD_MULTI_COMP_TESTS "Build Multi-Comp unit tests" OFF)

if(BUILD_MULTI_COMP_TESTS)
    # Create test executable
    add_executable(MultiCompTests
        UniversalCompressorTests.cpp
        UniversalCompressor.cpp
        AnalogLookAndFeel.cpp
        EnhancedCompressorEditor.cpp
        ../shared/LEDMeter.cpp
    )

    # Link test executable with JUCE
    target_link_libraries(MultiCompTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
    )

    # Add JUCE definitions
    target_compile_definitions(MultiCompTests
        PRIVATE
            JUCE_UNIT_TESTS=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
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

    # Generate JUCE header for tests
    juce_generate_juce_header(MultiCompTests)

    message(STATUS "Multi-Comp unit tests enabled")
    message(STATUS "Run tests with: ./MultiCompTests --all")
    message(STATUS "Run with ASAN: ASAN_OPTIONS=detect_leaks=1 ./MultiCompTests --all")
endif()
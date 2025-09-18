cmake_minimum_required(VERSION 3.15)
project(HarmonicGeneratorPlugin VERSION 1.0.0)

# ---- Configuration ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- JUCE Path ----
set(JUCE_PATH "" CACHE PATH "Path to JUCE")
if(NOT JUCE_PATH)
    message(FATAL_ERROR "JUCE_PATH not set. Please run cmake with -DJUCE_PATH=/path/to/JUCE")
endif()

# ---- Dependencies ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCURL REQUIRED libcurl)

# ---- JUCE ----
add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)

# ---- Plugin Definition ----
juce_add_plugin(HarmonicGeneratorPlugin
    COMPANY_NAME "Marc Korte"
    BUNDLE_ID "com.MarcKorte.HarmonicGeneratorPlugin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Harm
    FORMATS VST3 LV2
    PRODUCT_NAME "HarmonicGenerator"
    LV2URI "https://marc-korte.com/plugins/HarmonicGenerator"
)

# ---- Sources ----
target_sources(HarmonicGeneratorPlugin
    PRIVATE
        HarmonicGeneratorPlugin.cpp
)

# ---- Include & Definitions ----
target_include_directories(HarmonicGeneratorPlugin
    PRIVATE
        ${JUCE_PATH}/modules
        ${LIBCURL_INCLUDE_DIRS}
)

# Disable GTK native windows (fixes GTK issues in Ardour)
target_compile_definitions(HarmonicGeneratorPlugin
    PRIVATE
        JUCE_DISABLE_NATIVE_WINDOWS=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_XRENDER=0
        JUCE_USE_XCURSOR=0
)

# ---- JUCE Modules ----
target_link_libraries(HarmonicGeneratorPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_core
        ${LIBCURL_LIBRARIES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

cmake_minimum_required(VERSION 3.15)
project(HarmonicGeneratorPlugin VERSION 1.0.0)

# ---- Configuration ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- Dependencies ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCURL REQUIRED libcurl)

# ---- JUCE ----
# JUCE is already added by parent CMakeLists.txt

# ---- Plugin Definition ----
juce_add_plugin(HarmonicGeneratorPlugin
    COMPANY_NAME "Luna Co. Audio"
    BUNDLE_ID "com.LunaCoAudio.HarmonicGenerator"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE Luna
    PLUGIN_CODE HrmG
    FORMATS VST3 LV2 AU Standalone
    PRODUCT_NAME "Harmonic Generator"
    LV2URI "https://lunaco.audio/plugins/HarmonicGenerator"
    DESCRIPTION "Analog-style harmonic saturation processor"
    VERSION 1.0.0
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
)

# ---- Sources ----
target_sources(HarmonicGeneratorPlugin
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
        HardwarePresets.cpp
        HardwarePresets.h
)

# ---- Include & Definitions ----
target_include_directories(HarmonicGeneratorPlugin
    PRIVATE
        ${LIBCURL_INCLUDE_DIRS}
)

# Disable GTK native windows (fixes GTK issues in Ardour)
target_compile_definitions(HarmonicGeneratorPlugin
    PRIVATE
        JUCE_DISABLE_NATIVE_WINDOWS=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_XRENDER=0
        JUCE_USE_XCURSOR=0
        JUCE_VST3_CAN_REPLACE_VST2=1
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

# Add compile definitions to fix VST3 parameter conflicts
target_compile_definitions(HarmonicGeneratorPlugin
    PUBLIC
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

# Apply common JUCE defaults
apply_juce_defaults(HarmonicGeneratorPlugin)

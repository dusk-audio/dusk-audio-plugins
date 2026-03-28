cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment
set(MULTISYNTH_DEFAULT_VERSION "1.0.0")
get_plugin_version("multi-synth" MULTISYNTH_VERSION)
if(NOT MULTISYNTH_VERSION OR MULTISYNTH_VERSION STREQUAL "0.0.0")
    set(MULTISYNTH_VERSION ${MULTISYNTH_DEFAULT_VERSION})
endif()

project(MultiSynth VERSION ${MULTISYNTH_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

juce_add_plugin(MultiSynth
    PLUGIN_NAME "Multi-Synth"
    PLUGIN_CODE MSyn
    PLUGIN_MANUFACTURER_CODE Dusk
    FORMATS VST3 LV2 AU Standalone
    PRODUCT_NAME "Multi-Synth"
    COMPANY_NAME "Dusk Audio"
    COMPANY_WEBSITE "https://dusk-audio.github.io"
    COMPANY_EMAIL "support@dusk-audio.github.io"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "$ENV{HOME}/.vst3"
    LV2_COPY_DIR "$ENV{HOME}/.lv2"
    AU_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components"
    LV2URI "https://dusk-audio.github.io/plugins/multi-synth"
    BUNDLE_ID "com.DuskAudio.MultiSynth"
    VST3_AUTO_MANIFEST FALSE
)

set_plugin_version_defines(MultiSynth ${MULTISYNTH_VERSION})

target_sources(MultiSynth PRIVATE
    MultiSynth.cpp
    MultiSynth.h
    MultiSynthEditor.cpp
    MultiSynthEditor.h
    MultiSynthLookAndFeel.cpp
    MultiSynthLookAndFeel.h
    MultiSynthVoice.cpp
    MultiSynthVoice.h
    OscillatorEngine.h
    FilterEngine.h
    EffectsEngine.h
    Arpeggiator.cpp
    Arpeggiator.h
    ModMatrix.h
    UnisonEngine.h
    ../shared/LEDMeter.cpp
)

target_compile_definitions(MultiSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

target_link_libraries(MultiSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

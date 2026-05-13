# DuskVerb render tool — hosts the actual plugin bundle (AU on macOS, VST3
# anywhere, VST2 on Linux/Windows) via JUCE plugin host infrastructure and
# renders test signals through it. Output WAVs are then analysed by
# analyze_render.py to diagnose tail artefacts (single-delay echoes, modal
# periodicity, modulation depth).
#
# Enable with -DBUILD_DUSKVERB_RENDER=ON at configure time.
#
# Workflow:
#   1. cmake .. -DBUILD_DUSKVERB_RENDER=ON
#   2. cmake --build build --target duskverb_render -j8
#   3. ./build/tests/duskverb_render/duskverb_render <preset_name>
#   4. python3 tests/duskverb_render/analyze_render.py <wav_file>
#
# VST2 hosting (e.g. for yabridge-bridged Windows plugins on Linux) is opt-in
# and requires a complete Steinberg VST 2.4 SDK on disk. Pass
# -DVST2_SDK_DIR=<path> at configure time, where <path> contains
# pluginterfaces/vst2.x/aeffect.h and aeffectx.h. The headers historically
# shipped inside Steinberg VST3 SDKs ≤ 3.6.11 (vstsdk3610_11_06_2018_build_37
# is the last one). The minimal clean-room VeSTige headers (Carla's
# ~/.local/include/vestige/) do NOT work with juce_VSTPluginFormat — JUCE
# references many opcodes and struct fields that VeSTige doesn't provide.

add_executable(duskverb_render
    render.cpp
)

target_link_libraries(duskverb_render
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_events
        juce::juce_core
        juce::juce_graphics
        juce::juce_gui_basics  # AudioPluginInstance::createEditor() needs gui_basics linked
)

target_compile_definitions(duskverb_render
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=0
        JUCE_PLUGINHOST_AU=1     # gated by JUCE to macOS only
        JUCE_PLUGINHOST_VST3=1
)

target_compile_features(duskverb_render PRIVATE cxx_std_17)

# ---- Optional VST2 hosting ----
if(VST2_SDK_DIR)
    if(NOT EXISTS "${VST2_SDK_DIR}")
        message(FATAL_ERROR "VST2_SDK_DIR='${VST2_SDK_DIR}' does not exist")
    endif()

    if(NOT EXISTS "${VST2_SDK_DIR}/pluginterfaces/vst2.x/aeffect.h")
        message(FATAL_ERROR
            "VST2_SDK_DIR='${VST2_SDK_DIR}' is missing pluginterfaces/vst2.x/aeffect.h. "
            "Point it at a Steinberg VST 2.4 SDK include root (the directory containing "
            "the 'pluginterfaces/' folder). The headers ship inside Steinberg VST3 SDKs "
            "<= 3.6.11.")
    endif()

    target_compile_definitions(duskverb_render PRIVATE JUCE_PLUGINHOST_VST=1)
    target_include_directories(duskverb_render PRIVATE "${VST2_SDK_DIR}")
    message(STATUS "duskverb_render: VST2 hosting enabled (SDK: ${VST2_SDK_DIR})")

    if(UNIX AND NOT APPLE)
        target_link_libraries(duskverb_render PRIVATE dl)
    endif()
endif()

if(APPLE)
    # AU hosting needs these frameworks
    target_link_libraries(duskverb_render PRIVATE
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework CoreMIDI"
        "-framework AudioToolbox"
    )
endif()

set_target_properties(duskverb_render PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/duskverb_render"
    # JUCE module .cpp files must be in separate translation units — unity
    # builds mash them together and trigger "Incorrect use of JUCE cpp file"
    # plus VST2 namespace-include race conditions when VeSTige is in scope.
    UNITY_BUILD OFF
)

# Apple-native AU renderer for plugins that don't load via JUCE's
# setStateInformation path (Arturia LX-24's .aupreset format goes through
# kAudioUnitProperty_ClassInfo which JUCE silently drops for non-JUCE AUs).
if(APPLE)
    add_executable(au_native_render
        au_native_render.mm
    )
    target_link_libraries(au_native_render PRIVATE
        "-framework AudioUnit"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework Foundation"
    )
    target_compile_features(au_native_render PRIVATE cxx_std_17)
    set_target_properties(au_native_render PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/duskverb_render"
    )
endif()

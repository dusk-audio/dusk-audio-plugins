# DuskVerb render tool — loads the actual AU bundle via JUCE plugin host
# infrastructure and renders test signals through it. Output WAVs are then
# analysed by analyze_render.py to diagnose tail artefacts (single-delay
# echoes, modal periodicity, modulation depth).
#
# Enable with -DBUILD_DUSKVERB_RENDER=ON at configure time.
# Requires DuskVerb_AU to have been built and installed first.
#
# Workflow:
#   1. cmake .. -DBUILD_DUSKVERB_RENDER=ON
#   2. cmake --build build --target duskverb_render -j8
#   3. ./build/tests/duskverb_render/duskverb_render <preset_index>
#   4. python3 tests/duskverb_render/analyze_render.py <wav_file>

add_executable(duskverb_render
    render.cpp
)

target_link_libraries(duskverb_render
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors_headless
        juce::juce_events
        juce::juce_core
        juce::juce_graphics  # satisfies juce::Colour pulled in transitively
)

target_compile_definitions(duskverb_render
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
)

target_compile_features(duskverb_render PRIVATE cxx_std_17)

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

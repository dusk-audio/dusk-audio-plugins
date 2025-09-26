cmake_minimum_required(VERSION 3.15)
project(StudioReverb VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define our VST3 and LV2 plugin target
juce_add_plugin(StudioReverb
    PLUGIN_NAME "StudioReverb"
    PLUGIN_CODE SRvb
    FORMATS VST3 LV2 AU Standalone
    PRODUCT_NAME "StudioReverb"
    COMPANY_NAME "Luna Co. Audio"
    COMPANY_WEBSITE "https://lunaco.audio"
    COMPANY_EMAIL "info@lunaco.audio"
    PLUGIN_MANUFACTURER_CODE Luna
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST2_CATEGORY kPlugCategEffect
    LV2_URI https://luna-audio.com/plugins/studioreverb
)

juce_generate_juce_header(StudioReverb)

# Plugin sources - only include necessary freeverb files used by our algorithms
set(FREEVERB_SOURCES
    # Core classes used by all reverbs
    Source/freeverb/revbase.cpp
    Source/freeverb/slot.cpp
    Source/freeverb/delay.cpp
    Source/freeverb/delayline.cpp
    Source/freeverb/comb.cpp
    Source/freeverb/allpass.cpp
    Source/freeverb/biquad.cpp
    Source/freeverb/efilter.cpp
    Source/freeverb/utils.cpp

    # Specific reverb algorithms we actually use
    Source/freeverb/earlyref.cpp      # Early reflections (all algorithms)
    Source/freeverb/zrev.cpp           # Base for zrev2
    Source/freeverb/zrev2.cpp          # Hall algorithm
    Source/freeverb/progenitor.cpp     # Base for progenitor2
    Source/freeverb/progenitor2.cpp    # Room algorithm
    Source/freeverb/strev.cpp          # Plate Tank algorithm
    Source/freeverb/nrev.cpp           # Plate Simple algorithm
    Source/freeverb/nrevb.cpp          # Plate Nested algorithm (Dark Plate)

    # Note: The following files are NOT needed and have been removed:
    # - irmodel*.cpp, compmodel.cpp, scomp.cpp, slimit.cpp, limitmodel.cpp
    # - fir*.cpp, firwindow.cpp, stenh.cpp, rms.cpp, mls.cpp, sweep.cpp
    # - blockDelay.cpp, dl_gardner.cpp, frag.cpp
    # These were compiled but never used by our Dragonfly algorithms
)

target_sources(StudioReverb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DSP/DragonflyReverb.cpp
    Source/DSP/DragonflyReverb.h
    Source/PresetManager.cpp
    Source/PresetManager.h
    ${FREEVERB_SOURCES}
)

target_compile_definitions(StudioReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
    FV3_EARLYREF_PRESET_1=1
    LIBFV3_FLOAT=1  # Use float precision for Freeverb3
)

# Include freeverb headers
target_include_directories(StudioReverb PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/freeverb
)

target_link_libraries(StudioReverb PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Add test executable
add_executable(TestStudioReverb
    Test/TestReverb.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/DragonflyReverb.cpp
    Source/PresetManager.cpp
    ${FREEVERB_SOURCES}
)

target_include_directories(TestStudioReverb
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_BINARY_DIR}/StudioReverb_artefacts/JuceLibraryCode
)
target_compile_definitions(TestStudioReverb
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
        FV3_EARLYREF_PRESET_1=1
        LIBFV3_FLOAT=1
        JucePlugin_Name="StudioReverb"
        JucePlugin_Desc="StudioReverb"
        JucePlugin_Manufacturer="Luna Co. Audio"
        JucePlugin_ManufacturerWebsite="https://lunaco.audio"
        JucePlugin_ManufacturerEmail="info@lunaco.audio"
        JucePlugin_ManufacturerCode=0x4c756e61  # 'Luna'
        JucePlugin_PluginCode=0x53527662         # 'SRvb'
        JucePlugin_IsSynth=0
        JucePlugin_WantsMidiInput=0
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=0
        JucePlugin_Version=1.0.0
        JucePlugin_VersionCode=0x10000
        JucePlugin_VersionString="1.0.0"
        JucePlugin_VSTUniqueID=JucePlugin_PluginCode
        JucePlugin_VSTCategory=kPlugCategEffect
        JucePlugin_AUMainType='aufx'
        JucePlugin_AUSubType=JucePlugin_PluginCode
        JucePlugin_AUExportPrefix=StudioReverbAU
        JucePlugin_AUExportPrefixQuoted="StudioReverbAU"
        JucePlugin_AUManufacturerCode=JucePlugin_ManufacturerCode
        JucePlugin_CFBundleIdentifier=com.lunaco.StudioReverb
        JucePlugin_AAXIdentifier=com.lunaco.StudioReverb
        JucePlugin_AAXManufacturerCode=JucePlugin_ManufacturerCode
        JucePlugin_AAXProductId=JucePlugin_PluginCode
        JucePlugin_AAXCategory=0
        JucePlugin_AAXDisableBypass=0
        JucePlugin_AAXDisableMultiMono=0
        JucePlugin_IAAType=0x61757267  # 'aurx'
        JucePlugin_IAASubType=JucePlugin_PluginCode
        JucePlugin_IAAName="Luna Co. Audio: StudioReverb"
        JucePlugin_VSTNumMidiInputs=0
        JucePlugin_VSTNumMidiOutputs=0)

# Test target removed - no longer needed
# target_link_libraries(TestStudioReverb ...)
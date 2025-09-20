# VintageVerb Plugin CMake Configuration
cmake_minimum_required(VERSION 3.15)

# Define the plugin
juce_add_plugin(VintageVerb
    COMPANY_NAME "Luna Co. Audio"
    BUNDLE_ID "com.VintageAudioLabs.VintageVerb"
    PLUGIN_MANUFACTURER_CODE VALB
    PLUGIN_CODE VVrb
    FORMATS VST3 AU LV2 Standalone
    PRODUCT_NAME "VintageVerb"
    LV2URI "https://vintageaudiolabs.com/plugins/vintage-verb"

    # Plugin characteristics
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    # Copy plugin after build
    COPY_PLUGIN_AFTER_BUILD TRUE

    # Plugin version
    VERSION 1.0.0

    # Plugin description
    DESCRIPTION "Classic Digital Reverb Emulation"

    # Bus layouts
    PLUGIN_AU_MAIN_TYPE kAudioUnitType_Effect
)

# Set C++ standard
target_compile_features(VintageVerb PRIVATE cxx_std_17)

# Generate JUCE header
juce_generate_juce_header(VintageVerb)

# Add source files
target_sources(VintageVerb
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginEditor.h

        # DSP Components
        DSP/ReverbEngine.cpp
        DSP/ReverbEngine.h
        DSP/VintageColoration.cpp
        DSP/VintageColoration.h
        DSP/DualEngineRouter.cpp
        DSP/DualEngineRouter.h

        # Presets
        Presets/PresetManager.cpp
        Presets/PresetManager.h

        # GUI Components (to be added)
        # GUI/ReverbDisplay.cpp
        # GUI/ReverbDisplay.h
        # GUI/VintageKnobs.cpp
        # GUI/VintageKnobs.h
        # GUI/LookAndFeel.cpp
        # GUI/LookAndFeel.h
)

# Add binary data if needed (for graphics, presets, etc.)
# juce_add_binary_data(VintageVerbData
#     SOURCES
#         Resources/background.png
#         Resources/knobs.png
# )

# Link with binary data
# target_link_libraries(VintageVerb PRIVATE VintageVerbData)

# Preprocessor definitions
target_compile_definitions(VintageVerb
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

# Link with JUCE modules
target_link_libraries(VintageVerb
    PRIVATE
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
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Platform-specific settings
if(APPLE)
    # macOS/iOS specific settings
    set_target_properties(VintageVerb PROPERTIES
        XCODE_ATTRIBUTE_CLANG_LINK_OBJC_RUNTIME "NO"
    )
elseif(WIN32)
    # Windows specific settings
    target_compile_definitions(VintageVerb PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
    )
endif()

# Optimization flags for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(VintageVerb PRIVATE
            -O3
            -ffast-math
            -ftree-vectorize
            -march=native
        )
    elseif(MSVC)
        target_compile_options(VintageVerb PRIVATE
            /O2
            /fp:fast
            /arch:AVX2
        )
    endif()
endif()

# Install to user plugin directories
if(UNIX AND NOT APPLE)
    # Linux
    install(TARGETS VintageVerb_VST3
        DESTINATION "$ENV{HOME}/.vst3"
        COMPONENT VintageVerb)

    if(TARGET VintageVerb_LV2)
        install(TARGETS VintageVerb_LV2
            DESTINATION "$ENV{HOME}/.lv2"
            COMPONENT VintageVerb)
    endif()
elseif(APPLE)
    # macOS
    install(TARGETS VintageVerb_VST3
        DESTINATION "~/Library/Audio/Plug-Ins/VST3"
        COMPONENT VintageVerb)

    if(TARGET VintageVerb_AU)
        install(TARGETS VintageVerb_AU
            DESTINATION "~/Library/Audio/Plug-Ins/Components"
            COMPONENT VintageVerb)
    endif()
endif()

# Fix VST3 compilation
if(TARGET VintageVerb_VST3)
    target_compile_definitions(VintageVerb_VST3
        PRIVATE
            JUCE_VST3_CAN_REPLACE_VST2=1
    )
endif()
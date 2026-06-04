# Standalone unity-contract harness for GH issue #92.
# Opt-in: configure the top-level build with -DBUILD_TAPEMACHINE_TESTS=ON.
# Reuses the TapeMachine shared-code target (compiled plugin DSP) so the harness
# drives the real processBlock, not a copy.

juce_add_console_app(AutoCompSweep PRODUCT_NAME "AutoCompSweep")

juce_generate_juce_header(AutoCompSweep)

target_sources(AutoCompSweep PRIVATE AutoCompSweep.cpp)

target_compile_features(AutoCompSweep PRIVATE cxx_std_17)

# Pull in the plugin's source dir so PluginProcessor.h resolves, and the same
# JucePlugin_* compile defs the plugin is built with.
target_include_directories(AutoCompSweep PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Source")

target_compile_definitions(AutoCompSweep PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1)

# Link the compiled plugin shared code + the JUCE modules it needs.
target_link_libraries(AutoCompSweep PRIVATE
    TapeMachine
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
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

set_target_properties(AutoCompSweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

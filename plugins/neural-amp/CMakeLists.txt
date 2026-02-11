# Neural Amp - Neural Amp Modeler Plugin

cmake_minimum_required(VERSION 3.22)

# Plugin configuration
set(PLUGIN_NAME "NeuralAmp")
set(PLUGIN_CODE "NAmp")
set(PLUGIN_MANUFACTURER_CODE "Luna")
set(PLUGIN_MANUFACTURER "Dusk Audio")
set(PLUGIN_VERSION "1.0.0")

# Build the NeuralAmpModelerCore library
set(NAM_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Modules/NeuralAmpModelerCore)
set(NAM_DEPS_PATH ${NAM_CORE_DIR}/Dependencies)

# NAM Core sources
set(NAM_CORE_SOURCES
    ${NAM_CORE_DIR}/NAM/activations.cpp
    ${NAM_CORE_DIR}/NAM/conv1d.cpp
    ${NAM_CORE_DIR}/NAM/convnet.cpp
    ${NAM_CORE_DIR}/NAM/dsp.cpp
    ${NAM_CORE_DIR}/NAM/get_dsp.cpp
    ${NAM_CORE_DIR}/NAM/lstm.cpp
    ${NAM_CORE_DIR}/NAM/ring_buffer.cpp
    ${NAM_CORE_DIR}/NAM/util.cpp
    ${NAM_CORE_DIR}/NAM/wavenet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/factory_init.cpp
)

# Create NAM static library
add_library(nam_core STATIC ${NAM_CORE_SOURCES})

target_include_directories(nam_core
    PUBLIC
        ${NAM_CORE_DIR}
        ${NAM_CORE_DIR}/NAM
        ${NAM_DEPS_PATH}/eigen
        ${NAM_DEPS_PATH}/nlohmann
)

# Eigen alignment settings - prevent crashes on some platforms
target_compile_definitions(nam_core
    PUBLIC
        EIGEN_MAX_ALIGN_BYTES=0
        EIGEN_DONT_VECTORIZE
        NAM_SAMPLE_FLOAT
)

# NAM requires C++20 for std::filesystem
target_compile_features(nam_core PUBLIC cxx_std_20)

# Linux needs stdc++fs for older compilers
if(UNIX AND NOT APPLE)
    target_link_libraries(nam_core PUBLIC stdc++fs)
endif()

# Create the JUCE plugin
juce_add_plugin(${PLUGIN_NAME}
    COMPANY_NAME "${PLUGIN_MANUFACTURER}"
    PLUGIN_MANUFACTURER_CODE ${PLUGIN_MANUFACTURER_CODE}
    PLUGIN_CODE ${PLUGIN_CODE}
    FORMATS VST3 LV2 Standalone
    PRODUCT_NAME "Neural Amp"
    PLUGIN_NAME "Neural Amp"
    DESCRIPTION "Neural Amp Modeler with Cabinet IR"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES "Fx" "Distortion"
    LV2URI "https://lunaco.audio/plugins/neuralamp"
    COPY_PLUGIN_AFTER_BUILD ${DUSK_COPY_AFTER_BUILD}
    VST3_COPY_DIR "$ENV{HOME}/.vst3"
    LV2_COPY_DIR "$ENV{HOME}/.lv2"
)

target_sources(${PLUGIN_NAME}
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/NAMProcessor.h
        Source/NAMProcessor.cpp
        Source/CabinetProcessor.h
        Source/NoiseGate.h
        Source/ToneStack.h
        ../shared/LEDMeter.cpp
)

target_include_directories(${PLUGIN_NAME}
    PRIVATE
        Source
        ../shared
        ${NAM_CORE_DIR}
        ${NAM_CORE_DIR}/NAM
        ${NAM_DEPS_PATH}/eigen
        ${NAM_DEPS_PATH}/nlohmann
)

target_compile_definitions(${PLUGIN_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
        # Eigen settings for NAM
        EIGEN_MAX_ALIGN_BYTES=0
        EIGEN_DONT_VECTORIZE
        NAM_SAMPLE_FLOAT
)

# Plugin requires C++20 for NAM integration
target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_20)

target_link_libraries(${PLUGIN_NAME}
    PRIVATE
        nam_core
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
        juce::juce_recommended_warning_flags
)

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_link_libraries(${PLUGIN_NAME} PRIVATE pthread stdc++fs)
endif()

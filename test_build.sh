#!/bin/bash

# Test minimal plugin build
echo "Testing minimal VST3 build..."

# Clean
rm -rf test_build
mkdir -p test_build
cd test_build

# Create a minimal CMakeLists.txt
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.15)
project(TestPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# Add JUCE
add_subdirectory(/home/marc/projects/JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE)

# Create a test plugin
juce_add_plugin(TestPlugin
    COMPANY_NAME "Test"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Test
    PLUGIN_CODE Tst1
    PRODUCT_NAME "Test Plugin")

# Generate header
juce_generate_juce_header(TestPlugin)

# Add minimal source
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/TestProcessor.cpp" "
#include <JuceHeader.h>

class TestProcessor : public juce::AudioProcessor
{
public:
    TestProcessor() : AudioProcessor(BusesProperties()
        .withInput(\"Input\", juce::AudioChannelSet::stereo(), true)
        .withOutput(\"Output\", juce::AudioChannelSet::stereo(), true)) {}

    ~TestProcessor() override = default;

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return \"Test\"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestProcessor();
}
")

target_sources(TestPlugin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/TestProcessor.cpp")

# Link JUCE modules
target_link_libraries(TestPlugin
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

target_compile_definitions(TestPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
EOF

# Configure
echo "Configuring..."
cmake . -DCMAKE_BUILD_TYPE=Release

# Build
echo "Building..."
make -j1

# Check result
echo ""
echo "Checking build output..."
find . -name "*.vst3" -type d -exec ls -la {}/Contents/x86_64-linux/ \; 2>/dev/null

if [ -f "TestPlugin_artefacts/Release/VST3/Test Plugin.vst3/Contents/x86_64-linux/Test Plugin.so" ]; then
    echo "✓ Test build successful!"
    ls -lh "TestPlugin_artefacts/Release/VST3/Test Plugin.vst3/Contents/x86_64-linux/Test Plugin.so"
else
    echo "✗ Test build failed - no .so file generated"
fi
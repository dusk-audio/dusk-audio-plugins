#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

enum class CompressorMode : int
{
    Opto = 0,    // LA-2A style optical compressor
    FET = 1,     // 1176 style FET compressor  
    VCA = 2,     // DBX 160 style VCA compressor
    Bus = 3      // SSL Bus style compressor
};

class UniversalCompressor : public juce::AudioProcessor
{
public:
    UniversalCompressor();
    ~UniversalCompressor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Universal Compressor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    double getLatencyInSamples() const;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Metering
    float getInputLevel() const { return inputMeter.load(); }
    float getOutputLevel() const { return outputMeter.load(); }
    float getGainReduction() const { return grMeter.load(); }
    
    // Parameter access
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    CompressorMode getCurrentMode() const;
    
    // LV2 inline display support
    #if JucePlugin_Build_LV2
    void lv2_inline_display(void* context, uint32_t w, uint32_t h) const;
    #endif

private:
    // Core DSP classes
    class OptoCompressor;
    class FETCompressor;
    class VCACompressor;
    class BusCompressor;
    class AntiAliasing;
    
    // Parameter state
    juce::AudioProcessorValueTreeState parameters;
    
    // DSP components
    std::unique_ptr<OptoCompressor> optoCompressor;
    std::unique_ptr<FETCompressor> fetCompressor;
    std::unique_ptr<VCACompressor> vcaCompressor;
    std::unique_ptr<BusCompressor> busCompressor;
    std::unique_ptr<AntiAliasing> antiAliasing;
    
    // Metering
    std::atomic<float> inputMeter{-60.0f};
    std::atomic<float> outputMeter{-60.0f};
    std::atomic<float> grMeter{0.0f};
    
    // Stereo linking
    float linkedGainReduction[2] = {0.0f, 0.0f};
    // stereoLinkAmount now controlled by parameter
    
    // Processing state
    double currentSampleRate{0.0};  // Set by prepareToPlay from DAW
    int currentBlockSize{0};  // Set by prepareToPlay from DAW
    
    // Lookup tables for performance optimization
    class LookupTables
    {
    public:
        static constexpr int TABLE_SIZE = 4096;
        std::array<float, TABLE_SIZE> expTable;  // Exponential lookup
        std::array<float, TABLE_SIZE> logTable;  // Logarithm lookup
        
        void initialize();
        inline float fastExp(float x) const;
        inline float fastLog(float x) const;
    };
    std::unique_ptr<LookupTables> lookupTables;
    
    // Parameter creation
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UniversalCompressor)
};
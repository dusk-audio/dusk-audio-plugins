#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

enum class CompressorMode : int
{
    Opto = 0,       // LA-2A style optical compressor (Vintage Opto)
    FET = 1,        // 1176 Bluestripe style FET compressor (Vintage FET - aggressive)
    VCA = 2,        // DBX 160 style VCA compressor (Classic VCA)
    Bus = 3,        // SSL G-Series Bus compressor (Vintage VCA)
    StudioFET = 4,  // 1176 Rev E Blackface style (Studio FET - cleaner)
    StudioVCA = 5   // Focusrite Red 3 style (Studio VCA - modern)
};

// Distortion type for output saturation
enum class DistortionType : int
{
    Off = 0,
    Soft = 1,    // Gentle tape-like saturation
    Hard = 2,    // Aggressive transistor clipping
    Clip = 3     // Hard digital clip
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

    // Metering - use relaxed memory ordering for UI thread reads
    float getInputLevel() const { return inputMeter.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return outputMeter.load(std::memory_order_relaxed); }
    float getGainReduction() const { return grMeter.load(std::memory_order_relaxed); }
    float getLinkedGainReduction(int channel) const {
        return channel >= 0 && channel < 2 ? linkedGainReduction[channel].load(std::memory_order_relaxed) : 0.0f;
    }
    
    // Parameter access
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    CompressorMode getCurrentMode() const;

private:
    // Core DSP classes
    class OptoCompressor;
    class FETCompressor;
    class VCACompressor;
    class BusCompressor;
    class StudioFETCompressor;
    class StudioVCACompressor;
    class SidechainFilter;
    class AntiAliasing;

    // Parameter state
    juce::AudioProcessorValueTreeState parameters;

    // DSP components
    std::unique_ptr<OptoCompressor> optoCompressor;
    std::unique_ptr<FETCompressor> fetCompressor;
    std::unique_ptr<VCACompressor> vcaCompressor;
    std::unique_ptr<BusCompressor> busCompressor;
    std::unique_ptr<StudioFETCompressor> studioFetCompressor;
    std::unique_ptr<StudioVCACompressor> studioVcaCompressor;
    std::unique_ptr<SidechainFilter> sidechainFilter;
    std::unique_ptr<AntiAliasing> antiAliasing;
    
    // Metering
    std::atomic<float> inputMeter{-60.0f};
    std::atomic<float> outputMeter{-60.0f};
    std::atomic<float> grMeter{0.0f};

    // Stereo linking (thread-safe for UI/audio thread access)
    std::atomic<float> linkedGainReduction[2];
    // stereoLinkAmount now controlled by parameter
    
    // Processing state
    double currentSampleRate{0.0};  // Set by prepareToPlay from DAW
    int currentBlockSize{0};  // Set by prepareToPlay from DAW

    // Pre-allocated buffers for processBlock (avoids allocation in audio thread)
    juce::AudioBuffer<float> dryBuffer;           // For parallel compression mix
    juce::AudioBuffer<float> filteredSidechain;   // HP-filtered sidechain signal
    juce::AudioBuffer<float> linkedSidechain;     // Stereo-linked sidechain signal
    
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
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
    StudioVCA = 5,  // Focusrite Red 3 style (Studio VCA - modern)
    Digital = 6     // Transparent digital compressor
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
    float getSidechainLevel() const { return sidechainMeter.load(std::memory_order_relaxed); }
    float getLinkedGainReduction(int channel) const {
        return channel >= 0 && channel < 2 ? linkedGainReduction[channel].load(std::memory_order_relaxed) : 0.0f;
    }

    // GR History for visualization (circular buffer, ~128 samples at 30Hz = ~4 seconds)
    // Note: grHistory is not atomic. The UI thread may see partially updated values (tearing).
    // This is acceptable for visualization - occasional stale data won't affect display quality.
    // Using a lock-free ring buffer or mutex would add unnecessary overhead for this use case.
    static constexpr int GR_HISTORY_SIZE = 128;
    const std::array<float, GR_HISTORY_SIZE>& getGRHistory() const { return grHistory; }
    int getGRHistoryWritePos() const { return grHistoryWritePos.load(std::memory_order_relaxed); }
    
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
    class DigitalCompressor;
    class SidechainFilter;
    class AntiAliasing;
    class LookaheadBuffer;  // Shared lookahead for all modes
    class TruePeakDetector; // ITU-R BS.1770 compliant inter-sample peak detection

    // Forward declaration for SidechainEQ (defined in .cpp)
    // Note: Not a nested class - defined at file scope in .cpp

    // Parameter state
    juce::AudioProcessorValueTreeState parameters;

    // DSP components
    std::unique_ptr<OptoCompressor> optoCompressor;
    std::unique_ptr<FETCompressor> fetCompressor;
    std::unique_ptr<VCACompressor> vcaCompressor;
    std::unique_ptr<BusCompressor> busCompressor;
    std::unique_ptr<StudioFETCompressor> studioFetCompressor;
    std::unique_ptr<StudioVCACompressor> studioVcaCompressor;
    std::unique_ptr<DigitalCompressor> digitalCompressor;
    std::unique_ptr<SidechainFilter> sidechainFilter;
    std::unique_ptr<AntiAliasing> antiAliasing;
    std::unique_ptr<LookaheadBuffer> lookaheadBuffer;  // Global lookahead for all modes
    std::unique_ptr<class SidechainEQ> sidechainEQ;    // Low/high shelf EQ for sidechain
    std::unique_ptr<TruePeakDetector> truePeakDetector; // True-peak detection for sidechain

    // Metering
    std::atomic<float> inputMeter{-60.0f};
    std::atomic<float> outputMeter{-60.0f};
    std::atomic<float> grMeter{0.0f};
    std::atomic<float> sidechainMeter{-60.0f};  // Sidechain activity level

    // GR History buffer for visualization
    std::array<float, GR_HISTORY_SIZE> grHistory{};
    std::atomic<int> grHistoryWritePos{0};
    int grHistoryUpdateCounter{0};  // Update every N blocks for ~30Hz

    // Stereo linking (thread-safe for UI/audio thread access)
    // Initialized in constructor via .store() since atomic arrays can't use copy initialization
    std::atomic<float> linkedGainReduction[2];
    // stereoLinkAmount now controlled by parameter
    
    // Processing state
    double currentSampleRate{0.0};  // Set by prepareToPlay from DAW
    int currentBlockSize{0};  // Set by prepareToPlay from DAW

    // Smoothed auto-makeup gain to avoid audible distortion from abrupt changes
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedAutoMakeupGain{1.0f};

    // Pre-allocated buffers for processBlock (avoids allocation in audio thread)
    juce::AudioBuffer<float> dryBuffer;           // For parallel compression mix
    juce::AudioBuffer<float> filteredSidechain;   // HP-filtered sidechain signal
    juce::AudioBuffer<float> linkedSidechain;     // Stereo-linked sidechain signal
    juce::AudioBuffer<float> externalSidechain;   // External sidechain input buffer
    juce::AudioBuffer<float> interpolatedSidechain;  // Pre-interpolated sidechain for oversampling

    // Pre-smoothed gain buffer for auto-makeup optimization
    alignas(64) std::array<float, 8192> smoothedGainBuffer{};
    
    // Lookup tables for performance optimization
    class LookupTables
    {
    public:
        static constexpr int TABLE_SIZE = 4096;
        static constexpr int ALLBUTTONS_TABLE_SIZE = 512;  // For all-buttons transfer curves

        std::array<float, TABLE_SIZE> expTable;  // Exponential lookup
        std::array<float, TABLE_SIZE> logTable;  // Logarithm lookup

        // All-buttons (FET) compression transfer curves
        std::array<float, ALLBUTTONS_TABLE_SIZE> allButtonsModernCurve;   // Modern/default curve
        std::array<float, ALLBUTTONS_TABLE_SIZE> allButtonsMeasuredCurve; // Hardware-measured curve

        void initialize();
        inline float fastExp(float x) const;
        inline float fastLog(float x) const;

        // Get all-buttons gain reduction from lookup table
        // overThreshDb: 0-30dB range, returns gain reduction in dB
        float getAllButtonsReduction(float overThreshDb, bool useMeasuredCurve) const;
    };
    std::unique_ptr<LookupTables> lookupTables;

    // Transient shaper for FET all-buttons mode
    class TransientShaper;
    std::unique_ptr<TransientShaper> transientShaper;
    
    // Parameter creation
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UniversalCompressor)
};
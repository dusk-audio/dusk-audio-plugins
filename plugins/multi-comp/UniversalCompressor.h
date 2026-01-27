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
    Digital = 6,    // Transparent digital compressor
    Multiband = 7   // 4-band multiband compressor
};

// Number of compressor modes for parameter normalization
constexpr int kNumCompressorModes = 8;
constexpr int kMaxCompressorModeIndex = static_cast<int>(CompressorMode::Multiband);  // 7

// Multiband constants
constexpr int kNumMultibandBands = 4;

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

    const juce::String getName() const override { return "Multi-Comp"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    double getLatencyInSamples() const;

    // Bus layout support for sidechain
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // Factory presets
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    // Preset categories for UI
    struct PresetInfo {
        juce::String name;
        juce::String category;
        CompressorMode mode;
    };
    static const std::vector<PresetInfo>& getPresetList();

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Reset DSP state (called after setStateInformation to ensure clean audio output)
    void resetDSPState();

    // Metering - use relaxed memory ordering for UI thread reads
    // Combined (max of L/R) for backwards compatibility
    float getInputLevel() const { return inputMeter.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return outputMeter.load(std::memory_order_relaxed); }
    float getGainReduction() const { return grMeter.load(std::memory_order_relaxed); }

    // Per-channel metering for stereo display
    float getInputLevelL() const { return inputMeterL.load(std::memory_order_relaxed); }
    float getInputLevelR() const { return inputMeterR.load(std::memory_order_relaxed); }
    float getOutputLevelL() const { return outputMeterL.load(std::memory_order_relaxed); }
    float getOutputLevelR() const { return outputMeterR.load(std::memory_order_relaxed); }
    float getSidechainLevel() const { return sidechainMeter.load(std::memory_order_relaxed); }
    float getLinkedGainReduction(int channel) const {
        return channel >= 0 && channel < 2 ? linkedGainReduction[channel].load(std::memory_order_relaxed) : 0.0f;
    }

    // Per-band gain reduction for multiband mode visualization
    float getBandGainReduction(int band) const {
        return band >= 0 && band < kNumMultibandBands ? bandGainReduction[band].load(std::memory_order_relaxed) : 0.0f;
    }

    // GR History for visualization (circular buffer, ~128 samples at 30Hz = ~4 seconds)
    // Thread-safe with atomic<float> array for clean UI reads
    static constexpr int GR_HISTORY_SIZE = 128;
    float getGRHistoryValue(int index) const {
        return grHistory[index % GR_HISTORY_SIZE].load(std::memory_order_relaxed);
    }
    int getGRHistoryWritePos() const { return grHistoryWritePos.load(std::memory_order_relaxed); }

    // Actual release time for program-dependent release visualization (in ms)
    float getActualReleaseTime() const { return actualReleaseTimeMs.load(std::memory_order_relaxed); }

    // Parameter access
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    CompressorMode getCurrentMode() const;

    // Preset change listener for UI updates (called on message thread)
    class PresetChangeListener
    {
    public:
        virtual ~PresetChangeListener() = default;
        // presetIndex: the preset being loaded
        // targetMode: the mode the preset will set (-1 if unknown/default)
        virtual void presetChanged(int presetIndex, int targetMode) = 0;
    };
    void addPresetChangeListener(PresetChangeListener* listener) { presetChangeListeners.add(listener); }
    void removePresetChangeListener(PresetChangeListener* listener) { presetChangeListeners.remove(listener); }

private:
    // Core DSP classes
    class OptoCompressor;
    class FETCompressor;
    class VCACompressor;
    class BusCompressor;
    class StudioFETCompressor;
    class StudioVCACompressor;
    class DigitalCompressor;
    class MultibandCompressor;  // 4-band multiband compressor with Linkwitz-Riley crossovers
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
    std::unique_ptr<MultibandCompressor> multibandCompressor;
    std::unique_ptr<SidechainFilter> sidechainFilter;
    std::unique_ptr<AntiAliasing> antiAliasing;
    std::unique_ptr<LookaheadBuffer> lookaheadBuffer;  // Global lookahead for all modes
    std::unique_ptr<class SidechainEQ> sidechainEQ;    // Low/high shelf EQ for sidechain
    std::unique_ptr<TruePeakDetector> truePeakDetector; // True-peak detection for sidechain

    // Metering (combined L/R max for backwards compatibility)
    std::atomic<float> inputMeter{-60.0f};
    std::atomic<float> outputMeter{-60.0f};
    std::atomic<float> grMeter{0.0f};
    std::atomic<float> sidechainMeter{-60.0f};  // Sidechain activity level

    // Per-channel metering for stereo display
    std::atomic<float> inputMeterL{-60.0f};
    std::atomic<float> inputMeterR{-60.0f};
    std::atomic<float> outputMeterL{-60.0f};
    std::atomic<float> outputMeterR{-60.0f};

    // GR History buffer for visualization
    // Using atomic<float> array for thread-safe UI reads without tearing
    std::array<std::atomic<float>, GR_HISTORY_SIZE> grHistory{};
    std::atomic<int> grHistoryWritePos{0};
    int grHistoryUpdateCounter{0};  // Update every N blocks for ~30Hz

    // GR meter delay buffer - delays GR display to match audio output latency
    // This ensures the meter shows GR synchronized with what you hear (after PDC)
    // Stores one GR value per block, delay is measured in blocks
    static constexpr int MAX_GR_DELAY_SAMPLES = 256;  // Enough for ~256 blocks of delay
    std::array<float, MAX_GR_DELAY_SAMPLES> grDelayBuffer{};
    std::atomic<int> grDelayWritePos{0};
    std::atomic<int> grDelaySamples{0};  // Current delay in blocks (set in prepareToPlay)
    // Actual release time for UI visualization (program-dependent release)
    std::atomic<float> actualReleaseTimeMs{100.0f};

    // Stereo linking (thread-safe for UI/audio thread access)
    // Initialized in constructor via .store() since atomic arrays can't use copy initialization
    std::atomic<float> linkedGainReduction[2];
    // stereoLinkAmount now controlled by parameter

    // Per-band gain reduction for multiband mode (thread-safe for UI)
    std::atomic<float> bandGainReduction[kNumMultibandBands];
    
    // Processing state
    double currentSampleRate{0.0};  // Set by prepareToPlay from DAW
    int currentBlockSize{0};  // Set by prepareToPlay from DAW
    int currentOversamplingFactor{-1};  // Track current oversampling to detect changes
    int lastCompressorMode{-1};  // Track mode changes to reset auto-gain accumulators

    // Current preset index (for UI preset menu, not exposed as VST3 Program parameter)
    int currentPresetIndex = 0;

    // Preset change listeners for UI updates
    juce::ListenerList<PresetChangeListener> presetChangeListeners;

    // Smoothed auto-makeup gain to avoid audible distortion from abrupt changes
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedAutoMakeupGain{1.0f};

    // RMS-based auto-gain accumulators for professional-grade level matching
    // Using ~200ms averaging window (industry standard for perceived loudness)
    float inputRmsAccumulator = 0.0f;   // Running RMS of input signal
    float outputRmsAccumulator = 0.0f;  // Running RMS of output signal (before auto-gain)
    float rmsCoefficient = 0.0f;        // One-pole filter coefficient for RMS averaging
    bool primeRmsAccumulators = false;  // Flag to instantly prime accumulators on mode change

    // Pre-allocated buffers for processBlock (avoids allocation in audio thread)
    juce::AudioBuffer<float> dryBuffer;           // For parallel compression mix
    juce::AudioBuffer<float> filteredSidechain;   // HP-filtered sidechain signal
    juce::AudioBuffer<float> linkedSidechain;     // Stereo-linked sidechain signal
    juce::AudioBuffer<float> externalSidechain;   // External sidechain input buffer
    juce::AudioBuffer<float> interpolatedSidechain;  // Pre-interpolated sidechain for oversampling

    // Delay line for dry signal compensation when oversampling is enabled
    // This ensures dry and wet signals are time-aligned when mixing for parallel compression
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelayLine{512};
    int currentDryDelayInSamples{0};   // Current oversampling latency for dry signal delay
    int preparedDelayLineChannels{0};  // Number of channels the delay line was prepared for
    bool delayLineReady{false};        // Safety flag: true only after successful prepare()

    // Pre-smoothed gain buffer for auto-makeup optimization
    alignas(64) std::array<float, 8192> smoothedGainBuffer{};

    // Random generator for analog noise (class member to avoid per-block construction)
    juce::Random noiseRandom;

    // Smoothed crossover frequencies to prevent zipper noise
    juce::SmoothedValue<float> smoothedCrossover1{200.0f};
    juce::SmoothedValue<float> smoothedCrossover2{2000.0f};
    juce::SmoothedValue<float> smoothedCrossover3{8000.0f};

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
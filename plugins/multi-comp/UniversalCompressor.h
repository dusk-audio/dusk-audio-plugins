#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
#include "../shared/DryWetMixer.h"
#include "AutoGainMatcher.h"

enum class CompressorMode : int
{
    Opto = 0,       // Vintage optical compressor (Vintage Opto)
    FET = 1,        // Vintage FET compressor (Vintage FET - aggressive)
    VCA = 2,        // Classic VCA compressor (Classic VCA)
    Bus = 3,        // Vintage VCA bus compressor (Vintage VCA)
    StudioFET = 4,  // Studio FET compressor (Studio FET - cleaner)
    StudioVCA = 5,  // Modern VCA compressor (Studio VCA - modern)
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

class UniversalCompressor : public juce::AudioProcessor,
                            private juce::AsyncUpdater
{
public:
    UniversalCompressor();
    ~UniversalCompressor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Multi-Comp"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    // Recompute the reported plugin latency and publish it to the host. Safe to
    // call from ANY thread: applies immediately on the message thread, defers to
    // an AsyncUpdater when called from the audio thread (CLAP — and clean host
    // behaviour — forbids setLatencySamples() during process()).
    void updateLatencyReport();
    juce::AudioProcessorParameter* getBypassParameter() const override;

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

    // Channel configuration - for UI to determine mono/stereo display mode
    int getNumChannels() const { return currentNumChannels.load(std::memory_order_relaxed); }
    float getSidechainLevel() const { return sidechainMeter.load(std::memory_order_relaxed); }
    bool isExternalSidechainActive() const { return externalSidechainActive.load(std::memory_order_relaxed); }
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

    // Minimal-processing fast path. When enabled, processBlock skips
    // sidechain HP/EQ filtering, true-peak detection, transient shaper,
    // global lookahead, mix wet/dry crossfade, auto-makeup smoothing,
    // bypass-fade crossfader, stereo linking, and INTERNAL OVERSAMPLING.
    // The mode-specific process() is still called per-sample at NATIVE
    // rate using the input as its own sidechain. Intended for per-channel
    // mixer use (host applies its own bypass/mute/solo gating, no need
    // for the bus-grade extras). Disabled by default to preserve the
    // existing standalone-plugin behaviour.
    void setMinimalProcessing (bool enabled) noexcept { minimalProcessingMode.store (enabled, std::memory_order_relaxed); }
    bool getMinimalProcessing() const noexcept       { return minimalProcessingMode.load (std::memory_order_relaxed); }

    // Internal oversampling enable. Default true (preserves the donor's
    // historical behaviour for standalone-plugin users). When false, the
    // standard processBlock path runs the mode at native sample rate. Has
    // no effect on the minimal-processing fast path, which is already
    // native-rate. Hosts that want per-effect 1x default with a global
    // quality switch can flip this from outside.
    void setInternalOversamplingEnabled (bool enabled) noexcept { internalOversamplingEnabled.store (enabled, std::memory_order_relaxed); }
    bool getInternalOversamplingEnabled() const noexcept       { return internalOversamplingEnabled.load (std::memory_order_relaxed); }

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

    // Per-channel mixer fast-path flag — see public setMinimalProcessing.
    std::atomic<bool> minimalProcessingMode{false};
    // Internal-oversampling flag — see public setInternalOversamplingEnabled.
    std::atomic<bool> internalOversamplingEnabled{true};

    // Metering (combined L/R max for backwards compatibility)
    std::atomic<float> inputMeter{-60.0f};
    std::atomic<float> outputMeter{-60.0f};
    std::atomic<float> grMeter{0.0f};
    std::atomic<float> sidechainMeter{-60.0f};  // Sidechain activity level
    std::atomic<bool> externalSidechainActive{false};  // True when external sidechain is active

    // Per-channel metering for stereo display
    std::atomic<float> inputMeterL{-60.0f};
    std::atomic<float> inputMeterR{-60.0f};
    std::atomic<float> outputMeterL{-60.0f};
    std::atomic<float> outputMeterR{-60.0f};

    // Channel configuration (set in prepareToPlay, read by UI for mono/stereo display)
    std::atomic<int> currentNumChannels{2};

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
    int lastReportedLookaheadSamples{0};  // Track lookahead changes for PDC updates

    // Current preset index (for UI preset menu, not exposed as VST3 Program parameter)
    int currentPresetIndex = 0;

    // Preset change listeners for UI updates
    juce::ListenerList<PresetChangeListener> presetChangeListeners;

    // Smoothed auto-makeup gain to avoid audible distortion from abrupt changes
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedAutoMakeupGain{1.0f};

    // Auto-gain by slow in/out level matching (replaces GR inversion, which
    // tracked the compression envelope closely enough to pump).
    MultiComp::AutoGainMatcher autoGainMatcher;

    bool wasBypassedLastBlock = false;
    bool wasMinimalLastBlock  = false;  // Track minimal→standard transition for PDC restore

    // Bypass crossfade state (smooth transition from bypass to active)
    int bypassFadeRemaining{0};
    int bypassFadeLengthSamples{256};   // Base fade (5ms), set in prepareToPlay
    int bypassFadeActualLength{256};    // Per-transition length (may be extended for Digital lookahead)
    juce::AudioBuffer<float> bypassFadeBuffer;

    // Dedicated delay line for time-aligning the bypass fade dry signal.
    // Separate from the processing lookahead to avoid double-advance issues.
    juce::AudioBuffer<float> bypassFadeDelayBuf;
    std::vector<int> bypassFadeDelayWritePos;
    int bypassFadeDelaySize{0};

    // Input-history ring for latency-aligned dry output on bypass / 0% mix (issue #106). Written
    // every block, read back delayed by getLatencySamples() so bypassed output stays phase-aligned
    // with the host's PDC WITHOUT changing the reported latency from the audio thread. Zeroing
    // latency on bypass made Cubase restart its audio engine in a feedback loop (prepareToPlay set
    // N -> bypassed block set 0 -> restart -> repeat), dropping playback.
    juce::AudioBuffer<float> bypassAlignBuf;
    std::array<int, 2> bypassAlignWritePos{{0, 0}};
    int bypassAlignSize{0};

    // Pre-allocated buffers for processBlock (avoids allocation in audio thread)
    juce::AudioBuffer<float> filteredSidechain;   // HP-filtered sidechain signal
    juce::AudioBuffer<float> linkedSidechain;     // Stereo-linked sidechain signal
    juce::AudioBuffer<float> externalSidechain;   // External sidechain input buffer
    juce::AudioBuffer<float> interpolatedSidechain;  // Pre-interpolated sidechain for oversampling
    juce::AudioBuffer<float> doubleConvBuffer;       // Float staging for double-precision processing

    // Phase-coherent dry/wet mixer (prevents comb filtering with oversampling)
    // Replaces manual dryBuffer, oversampledDryBuffer, and delay line implementation
    DuskAudio::DryWetMixer dryWetMixer;

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
    
    // Pure computation of the current reported latency (no host call) — shared by
    // updateLatencyReport() and the deferred async apply.
    int computeLatencySamples() const;

    // RMS of a block across all channels, used by the auto-gain matcher.
    static float blockRms(const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples);

    // Drives the auto-gain matcher and applies the smoothed makeup in place.
    // Shared by the multiband path and the standard path — they used to carry
    // near-identical copies of this logic.
    void applyAutoGain(juce::AudioBuffer<float>& buffer, int numChannels, int numSamples,
                       float inRms, bool autoMakeupEnabled);
    // Message-thread apply of the pending latency (AsyncUpdater callback).
    void handleAsyncUpdate() override;
    std::atomic<int> pendingLatencySamples_{0};
    // True while a processBlock call is on the stack. updateLatencyReport() keys
    // on THIS (not thread identity) to decide whether to defer setLatencySamples,
    // because a validator/host may drive process() on the message thread.
    std::atomic<bool> inProcessBlock_{false};
    struct ScopedProcessFlag
    {
        std::atomic<bool>& f;
        explicit ScopedProcessFlag(std::atomic<bool>& x) : f(x) { f.store(true,  std::memory_order_relaxed); }
        ~ScopedProcessFlag()                                   { f.store(false, std::memory_order_relaxed); }
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UniversalCompressor)
};
#pragma once

#include <JuceHeader.h>
#include <memory>

class ImprovedTapeEmulation;
class WowFlutterProcessor;  // Forward declaration for shared wow/flutter

class TapeMachineAudioProcessor : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    TapeMachineAudioProcessor();
    ~TapeMachineAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Level metering - use relaxed ordering for UI reads (no synchronization needed)
    // Audio thread writes, UI thread reads - eventual consistency is fine for metering
    float getInputLevelL() const { return inputLevelL.load(std::memory_order_relaxed); }
    float getInputLevelR() const { return inputLevelR.load(std::memory_order_relaxed); }
    float getOutputLevelL() const { return outputLevelL.load(std::memory_order_relaxed); }
    float getOutputLevelR() const { return outputLevelR.load(std::memory_order_relaxed); }

    // Transport state for reel animation
    bool isProcessing() const { return isProcessingAudio.load(std::memory_order_relaxed); }

    // Mono/stereo state for VU meter display
    bool isMonoTrack() const { return isMonoInput.load(std::memory_order_relaxed); }

    enum TapeMachine
    {
        Swiss = 0,
        American
    };

    enum TapeSpeed
    {
        Speed_7_5_IPS = 0,
        Speed_15_IPS,
        Speed_30_IPS
    };

    enum TapeType
    {
        FormulaClassic = 0,
        FormulaHighOutput,
        FormulaBalanced,
        FormulaVintage
    };

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::unique_ptr<ImprovedTapeEmulation> tapeEmulationLeft;
    std::unique_ptr<ImprovedTapeEmulation> tapeEmulationRight;

    // Shared wow/flutter processor for stereo coherence (real tape motor affects both channels identically)
    std::unique_ptr<WowFlutterProcessor> sharedWowFlutter;

    // Add bias parameter for improved tape emulation
    std::atomic<float>* biasParam = nullptr;
    std::atomic<float>* calibrationParam = nullptr;

    // Oversampling with 2x/4x selection using FIR equiripple filters
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
    std::atomic<float>* oversamplingParam = nullptr;
    int currentOversamplingFactor = 4;  // Default to 4x

    // For recreating oversamplers when settings change
    double lastPreparedSampleRate = 0.0;
    int lastPreparedBlockSize = 0;
    int lastOversamplingChoice = -1;

    // Crossfade state for smooth oversampling transitions
    bool oversamplingTransitionActive = false;
    int oversamplingTransitionSamples = 0;
    static constexpr int OVERSAMPLING_CROSSFADE_SAMPLES = 512;  // ~10ms at 48kHz

    juce::dsp::ProcessorChain<
        juce::dsp::Gain<float>,
        juce::dsp::StateVariableTPTFilter<float>,
        juce::dsp::StateVariableTPTFilter<float>,
        juce::dsp::Gain<float>
    > processorChainLeft, processorChainRight;

    float currentSampleRate = 44100.0f;
    float currentOversampledRate = 176400.0f;  // Default value; computed dynamically in prepareToPlay()

    // Latency must never be changed from the audio thread. Bitwig's CLAP host treats every
    // setLatencySamples() call as a restart request, and doing it mid-render (per block, on
    // oversampling/bypass changes) made offline export loop forever (issue #94; VST3 tolerated it).
    // The audio thread only does an RT-safe atomic store of the requested latency here (−1 = no
    // request); a slow message-thread Timer consumes it and calls setLatencySamples() off-thread.
    // (An AsyncUpdater would post a message from the audio thread, which can allocate — not
    // strictly RT-safe — so a polled atomic is used instead.)
    void timerCallback() override;
    std::atomic<int> requestedLatencySamples { -1 };

    // Reports latency to the host ONLY when it actually changes. JUCE fires
    // audioProcessorChanged(latencyChanged) on every setLatencySamples() call and the CLAP wrapper
    // turns that into host->request_restart(); since activate()->prepareToPlay() always re-reports
    // the same latency, an unconditional call makes CLAP hosts (Bitwig) restart on every activate,
    // an infinite restart loop that hung offline audio export (issue #94). This member persists
    // across deactivate/reactivate (deliberately NOT reset in releaseResources) so reactivation
    // sees an unchanged value and stays quiet. Message-thread only, so no atomic needed.
    int lastReportedLatency = -1;
    void setLatencyIfChanged (int newLatency);

    std::atomic<float>* tapeMachineParam = nullptr;
    std::atomic<float>* tapeSpeedParam = nullptr;
    std::atomic<float>* tapeTypeParam = nullptr;
    std::atomic<float>* signalPathParam = nullptr;
    std::atomic<float>* eqStandardParam = nullptr;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* highpassFreqParam = nullptr;
    std::atomic<float>* lowpassFreqParam = nullptr;
    std::atomic<float>* noiseAmountParam = nullptr;
    std::atomic<float>* wowAmountParam = nullptr;
    std::atomic<float>* flutterAmountParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* autoCompParam = nullptr;
    std::atomic<float>* autoCalParam = nullptr;
    void updateFilters();

    // Level metering (RMS-based for VU accuracy)
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };
    std::atomic<bool> isProcessingAudio { false };
    std::atomic<bool> isMonoInput { false };  // True when on a mono track

    // RMS integration for VU-accurate metering (300ms time constant)
    float rmsInputL = 0.0f;
    float rmsInputR = 0.0f;
    float rmsOutputL = 0.0f;
    float rmsOutputR = 0.0f;

    // Filter frequency tracking (instance variables instead of statics)
    float lastHpFreq = -1.0f;
    float lastLpFreq = -1.0f;

    // Smoothed parameters to prevent zipper noise
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSaturation;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedNoiseAmount;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWow;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFlutter;

    // Filter bypass states
    bool bypassHighpass = true;
    bool bypassLowpass = true;

    // Preset management
    int currentPresetIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeMachineAudioProcessor)
};

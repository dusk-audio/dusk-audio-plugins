#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include "SSLSaturation.h"

// Forward declaration for LV2 inline display

//==============================================================================
/**
    SSL 4000 Series Console EQ Emulation

    Features:
    - 4-band parametric EQ (LF, LM, HM, HF)
    - High-pass and low-pass filters
    - Brown/Black knob variants
    - 2x/4x oversampling for anti-aliasing
    - Analog-modeled nonlinearities

    Version: 1.0.0
    Build information available via BUILD_DATE and BUILD_TIME constants
*/
class FourKEQ : public juce::AudioProcessor,
                private juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    // Version information
    static constexpr const char* PLUGIN_VERSION = "1.0.2";
    static constexpr const char* BUILD_DATE = __DATE__;
    static constexpr const char* BUILD_TIME = __TIME__;

    //==============================================================================
    FourKEQ();
    ~FourKEQ() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int /*index*/, const juce::String& /*newName*/) override {}

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Public parameter access for GUI and inline display
    juce::AudioProcessorValueTreeState parameters;

    // Audio buffers for spectrum analyzer (accessed from both audio and UI threads)
    juce::AudioBuffer<float> spectrumBuffer;      // Post-EQ (default)
    juce::AudioBuffer<float> spectrumBufferPre;   // Pre-EQ
    juce::CriticalSection spectrumBufferLock;

    // Level meters (thread-safe atomic values for GUI display)
    std::atomic<float> inputLevelL{-96.0f};   // Input level left channel (dBFS)
    std::atomic<float> inputLevelR{-96.0f};   // Input level right channel (dBFS)
    std::atomic<float> outputLevelL{-96.0f};  // Output level left channel (dBFS)
    std::atomic<float> outputLevelR{-96.0f};  // Output level right channel (dBFS)

    // Channel count for UI mono/stereo display (set in prepareToPlay)
    std::atomic<int> currentNumChannels{2};
    int getNumChannels() const { return currentNumChannels.load(std::memory_order_relaxed); }

private:
    //==============================================================================
    // AudioProcessorValueTreeState::Listener implementation
    void parameterChanged(const juce::String& parameterID, float newValue) override
    {
        // Signal that parameters have changed for filter update
        parametersChanged.store(true);

        // Set specific dirty flags for optimized updates
        if (parameterID == "hpf_freq")
            hpfDirty.store(true);
        else if (parameterID == "lpf_freq")
            lpfDirty.store(true);
        else if (parameterID == "lf_gain" || parameterID == "lf_freq" || parameterID == "lf_bell")
            lfDirty.store(true);
        else if (parameterID == "lm_gain" || parameterID == "lm_freq" || parameterID == "lm_q")
            lmDirty.store(true);
        else if (parameterID == "hm_gain" || parameterID == "hm_freq" || parameterID == "hm_q")
            hmDirty.store(true);
        else if (parameterID == "hf_gain" || parameterID == "hf_freq" || parameterID == "hf_bell")
            hfDirty.store(true);
        else if (parameterID == "eq_type")
        {
            // EQ type change affects all bands
            lfDirty.store(true);
            lmDirty.store(true);
            hmDirty.store(true);
            hfDirty.store(true);
        }
    }

    //==============================================================================
    // Filter chain for stereo processing
    struct FilterBand
    {
        juce::dsp::IIR::Filter<float> filter;
        juce::dsp::IIR::Filter<float> filterR;  // Right channel

        void reset()
        {
            filter.reset();
            filterR.reset();
        }

        void prepare(const juce::dsp::ProcessSpec& spec)
        {
            filter.prepare(spec);
            filterR.prepare(spec);
        }

        // Process a single sample through the appropriate channel filter
        inline float processSample(float sample, bool useLeftChannel)
        {
            return useLeftChannel ? filter.processSample(sample) : filterR.processSample(sample);
        }
    };

    // HPF: True 3rd order (18 dB/oct) - 1st order + 2nd order Butterworth
    struct HighPassFilter
    {
        // 1st order HPF stage (6dB/oct)
        juce::dsp::IIR::Filter<float> stage1L;
        juce::dsp::IIR::Filter<float> stage1R;

        // 2nd order HPF stage (12dB/oct, Butterworth Q=0.707)
        FilterBand stage2;

        void reset()
        {
            stage1L.reset();
            stage1R.reset();
            stage2.reset();
        }

        void prepare(const juce::dsp::ProcessSpec& spec)
        {
            stage1L.prepare(spec);
            stage1R.prepare(spec);
            stage2.prepare(spec);
        }

        // Process a single sample through HPF stages
        inline float processSample(float sample, bool useLeftChannel)
        {
            // Stage 1: 1st-order filter
            float processed = useLeftChannel ? stage1L.processSample(sample) : stage1R.processSample(sample);
            // Stage 2: 2nd-order filter
            return stage2.processSample(processed, useLeftChannel);
        }
    };

    // LPF: 2nd order (12 dB/oct)
    FilterBand lpfFilter;

    // HPF structure
    HighPassFilter hpfFilter;

    // 4-band EQ filters
    FilterBand lfFilter;   // Low frequency
    FilterBand lmFilter;   // Low-mid frequency
    FilterBand hmFilter;   // High-mid frequency
    FilterBand hfFilter;   // High frequency

    // Oversampling
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
    int oversamplingFactor = 2;

    // SSL-accurate saturation modeling
    SSLSaturation sslSaturation;

    // Transformer phase shift modeling (all-pass filters for phase rotation)
    // Models the low-frequency phase shift characteristic of SSL transformers
    // This contributes to the "3D" quality and depth of SSL EQ
    struct TransformerPhaseShift
    {
        juce::dsp::IIR::Filter<float> allPassL;
        juce::dsp::IIR::Filter<float> allPassR;

        void reset()
        {
            allPassL.reset();
            allPassR.reset();
        }

        void prepare(const juce::dsp::ProcessSpec& spec)
        {
            allPassL.prepare(spec);
            allPassR.prepare(spec);
        }

        void setFrequency(double sampleRate, float freq)
        {
            // All-pass filter for phase rotation without magnitude change
            // Models transformer low-frequency phase shift (typically 100-500Hz)
            // Uses first-order all-pass: H(s) = (s - a) / (s + a)
            float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
            float tan_w0 = std::tan(w0 / 2.0f);

            // First-order all-pass coefficients
            float a0 = 1.0f + tan_w0;
            float a1 = (1.0f - tan_w0) / a0;
            float b0 = a1;
            float b1 = 1.0f;

            auto coeffs = juce::dsp::IIR::Coefficients<float>::Ptr(
                new juce::dsp::IIR::Coefficients<float>(b0, b1, 0.0f, 1.0f, a1, 0.0f));
            allPassL.coefficients = coeffs;
            allPassR.coefficients = coeffs;
        }

        inline float processSample(float sample, bool useLeftChannel)
        {
            return useLeftChannel ? allPassL.processSample(sample) : allPassR.processSample(sample);
        }
    };

    TransformerPhaseShift phaseShift;

    // Parameter pointers with safe accessors
    std::atomic<float>* hpfFreqParam = nullptr;
    std::atomic<float>* hpfEnabledParam = nullptr;
    std::atomic<float>* lpfFreqParam = nullptr;
    std::atomic<float>* lpfEnabledParam = nullptr;

    std::atomic<float>* lfGainParam = nullptr;
    std::atomic<float>* lfFreqParam = nullptr;
    std::atomic<float>* lfBellParam = nullptr;

    std::atomic<float>* lmGainParam = nullptr;
    std::atomic<float>* lmFreqParam = nullptr;
    std::atomic<float>* lmQParam = nullptr;

    std::atomic<float>* hmGainParam = nullptr;
    std::atomic<float>* hmFreqParam = nullptr;
    std::atomic<float>* hmQParam = nullptr;

    std::atomic<float>* hfGainParam = nullptr;
    std::atomic<float>* hfFreqParam = nullptr;
    std::atomic<float>* hfBellParam = nullptr;

    std::atomic<float>* eqTypeParam = nullptr;     // 0 = Brown, 1 = Black
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* inputGainParam = nullptr;   // Input gain control
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* saturationParam = nullptr;
    std::atomic<float>* oversamplingParam = nullptr; // 0 = 2x, 1 = 4x
    std::atomic<float>* msModeParam = nullptr;  // M/S processing
    std::atomic<float>* spectrumPrePostParam = nullptr;  // 0 = post-EQ, 1 = pre-EQ
    std::atomic<float>* autoGainParam = nullptr;  // Auto-gain compensation toggle

    // Safe parameter accessors with fallback defaults
    inline float safeGetParam(std::atomic<float>* param, float defaultValue) const
    {
        return param ? param->load() : defaultValue;
    }

    // Processing state
    double currentSampleRate = 44100.0;
    double lastPreparedSampleRate = 0.0;
    int lastOversamplingFactor = 0;
    int lastPreparedBlockSize = 0;

    // Validation flags
    bool paramsValid = false;  // Set true only if all critical params initialized

    // Thread-safe parameter cache (updated atomically in processBlock)
    struct CachedParams
    {
        float hpfFreq = 20.0f;
        float lpfFreq = 20000.0f;
        float lfGain = 0.0f;
        float lfFreq = 100.0f;
        float lfBell = 0.0f;
        float lmGain = 0.0f;
        float lmFreq = 600.0f;
        float lmQ = 0.7f;
        float hmGain = 0.0f;
        float hmFreq = 2000.0f;
        float hmQ = 0.7f;
        float hfGain = 0.0f;
        float hfFreq = 8000.0f;
        float hfBell = 0.0f;
        float eqType = 0.0f;
    } cachedParams;

    // Atomic flag for any parameter change (set by listener, checked by audio thread)
    std::atomic<bool> parametersChanged{true};

    // Per-band dirty flags for optimized filter updates
    std::atomic<bool> hpfDirty{true};
    std::atomic<bool> lpfDirty{true};
    std::atomic<bool> lfDirty{true};
    std::atomic<bool> lmDirty{true};
    std::atomic<bool> hmDirty{true};
    std::atomic<bool> hfDirty{true};

    // Filter enable state tracking (for reset on re-enable to avoid artifacts)
    bool lastHpfEnabled = false;
    bool lastLpfEnabled = false;

    // Preset management
    int currentPreset = 0;
    void loadFactoryPreset(int index);

    // Filter update methods
    void updateFilters();
    void updateHPF(double sampleRate);
    void updateLPF(double sampleRate);
    void updateLFBand(double sampleRate);
    void updateLMBand(double sampleRate);
    void updateHMBand(double sampleRate);
    void updateHFBand(double sampleRate);

    // Helper methods
    float calculateDynamicQ(float gain, float baseQ) const;
    float calculateAutoGainCompensation() const;

    // SSL-specific filter coefficient generation
    // Based on hardware measurements and analog prototype matching
    juce::dsp::IIR::Coefficients<float>::Ptr makeSSLShelf(
        double sampleRate, float freq, float q, float gainDB, bool isHighShelf, bool isBlackMode) const;
    juce::dsp::IIR::Coefficients<float>::Ptr makeSSLPeak(
        double sampleRate, float freq, float q, float gainDB, bool isBlackMode) const;

    // Parameter creation
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKEQ)
};

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include "EQBand.h"
#include "BritishEQProcessor.h"
#include "PultecProcessor.h"
#include "DynamicEQProcessor.h"
#include "LinearPhaseEQProcessor.h"
#include "MultiQPresets.h"

//==============================================================================
/**
    Multi-Q: Professional 8-Band Parametric EQ with FFT Analyzer

    Features:
    - 8 color-coded frequency bands (HPF, Low Shelf, 4x Parametric, High Shelf, LPF)
    - Real-time FFT Analyzer with Peak/RMS modes
    - Q-Coupling (automatic Q adjustment based on gain)
    - Oversampling (HQ mode) for analog-matching response at high frequencies
    - Stereo/Mid-Side processing options
    - Zero-latency (without oversampling), minimal latency with HQ mode

    Design Goals:
    - Analog-matching frequency response using oversampling to prevent Nyquist cramping
    - Matched analog gain at all frequencies
    - No "digital" sound - response matches analog prototypes

    Version: 1.0.0
*/
class MultiQ : public juce::AudioProcessor,
               private juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    static constexpr const char* PLUGIN_VERSION = "1.0.0";
    static constexpr int NUM_BANDS = 8;

    //==============================================================================
    MultiQ();
    ~MultiQ() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getLatencySamples() const;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

private:
    int currentPresetIndex = 0;
    std::vector<MultiQPresets::Preset> factoryPresets;

public:

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Undo/Redo system
    juce::UndoManager undoManager;
    juce::UndoManager& getUndoManager() { return undoManager; }

    //==============================================================================
    // Public parameter access for GUI
    juce::AudioProcessorValueTreeState parameters;

    // FFT data access for analyzer display
    // Thread-safe ring buffer for audio capture
    void pushSamplesToAnalyzer(const float* samples, int numSamples, bool isPreEQ);

    // Get magnitude data for display (call from UI thread)
    const std::array<float, 2048>& getAnalyzerMagnitudes() const { return analyzerMagnitudes; }
    bool isAnalyzerDataReady() const { return analyzerDataReady.load(); }
    void clearAnalyzerDataReady() { analyzerDataReady.store(false); }

    // Level meters (thread-safe atomic values)
    std::atomic<float> inputLevelL{-96.0f};
    std::atomic<float> inputLevelR{-96.0f};
    std::atomic<float> outputLevelL{-96.0f};
    std::atomic<float> outputLevelR{-96.0f};

    // Clip indicators (set by audio thread, cleared by UI thread on click)
    std::atomic<bool> inputClipped{false};
    std::atomic<bool> outputClipped{false};

    // Get current Q-couple mode for UI display
    QCoupleMode getCurrentQCoupleMode() const;

    // Get effective Q value after coupling (for UI display)
    float getEffectiveQ(int bandNum) const;

    // Get frequency response magnitude at a specific frequency (for curve display)
    float getFrequencyResponseMagnitude(float frequencyHz) const;

    // Get frequency response including dynamic gain (for dynamic curve overlay)
    float getFrequencyResponseWithDynamics(float frequencyHz) const;

    // Get current dynamic gain for a band (for UI visualization, thread-safe)
    float getDynamicGain(int bandIndex) const { return dynamicEQ.getCurrentDynamicGain(bandIndex); }

    // Get current processing mode (0=Stereo, 1=Left, 2=Right, 3=Mid, 4=Side)
    int getProcessingMode() const { return static_cast<int>(safeGetParam(processingModeParam, 0.0f)); }

    // Get dynamics threshold for a band (for visualization)
    float getDynamicsThreshold(int bandIndex) const
    {
        if (bandIndex >= 0 && bandIndex < NUM_BANDS && bandDynThresholdParams[static_cast<size_t>(bandIndex)])
            return bandDynThresholdParams[static_cast<size_t>(bandIndex)]->load();
        return 0.0f;
    }

    // Check if dynamics are enabled for a band
    bool isDynamicsEnabled(int bandIndex) const;

    // Check if in Dynamic EQ mode
    bool isInDynamicMode() const;

    //==============================================================================
    // Band Solo functionality
    // When a band is soloed, only that band's effect is heard (all others bypassed)
    void setSoloedBand(int bandIndex)
    {
        if (bandIndex < -1 || bandIndex >= NUM_BANDS)
            bandIndex = -1;
        soloedBand.store(bandIndex);
    }  // -1 = no solo
    int getSoloedBand() const { return soloedBand.load(); }
    bool isBandSoloed(int bandIndex) const { return soloedBand.load() == bandIndex; }
    bool isAnySoloed() const { return soloedBand.load() >= 0; }

private:
    std::atomic<int> soloedBand{-1};  // -1 = no solo, 0-7 = that band is soloed    //==============================================================================
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==============================================================================
    // Filter structures for each band type

    // Multi-stage cascaded filter for variable slope HPF/LPF
    struct CascadedFilter
    {
        static constexpr int MAX_STAGES = 8;  // Up to 16th order (96 dB/oct)
        std::array<juce::dsp::IIR::Filter<float>, MAX_STAGES> stagesL;
        std::array<juce::dsp::IIR::Filter<float>, MAX_STAGES> stagesR;
        std::atomic<int> activeStages{1};

        void reset()
        {
            for (auto& f : stagesL) f.reset();
            for (auto& f : stagesR) f.reset();
        }

        void prepare(const juce::dsp::ProcessSpec& spec)
        {
            for (auto& f : stagesL) f.prepare(spec);
            for (auto& f : stagesR) f.prepare(spec);
        }

        float processSampleL(float sample)
        {
            for (int i = 0; i < activeStages; ++i)
                sample = stagesL[static_cast<size_t>(i)].processSample(sample);
            return sample;
        }

        float processSampleR(float sample)
        {
            for (int i = 0; i < activeStages; ++i)
                sample = stagesR[static_cast<size_t>(i)].processSample(sample);
            return sample;
        }
    };
    // Standard IIR filter for shelf and parametric bands
    struct StereoFilter
    {
        juce::dsp::IIR::Filter<float> filterL;
        juce::dsp::IIR::Filter<float> filterR;

        void reset()
        {
            filterL.reset();
            filterR.reset();
        }

        void prepare(const juce::dsp::ProcessSpec& spec)
        {
            filterL.prepare(spec);
            filterR.prepare(spec);
        }

        void setCoefficients(juce::dsp::IIR::Coefficients<float>::Ptr coeffs)
        {
            filterL.coefficients = coeffs;
            filterR.coefficients = coeffs;
        }

        float processSampleL(float sample) { return filterL.processSample(sample); }
        float processSampleR(float sample) { return filterR.processSample(sample); }
    };

    // Band 1: HPF (variable slope)
    CascadedFilter hpfFilter;

    // Bands 2-7: Shelf and Parametric filters
    std::array<StereoFilter, 6> eqFilters;  // Indices 0-5 for bands 2-7

    // Dynamic gain filters (bands 2-7) - applies dynamic EQ gain independently of static gain
    std::array<StereoFilter, 6> dynGainFilters;  // Same indices as eqFilters

    // Band 8: LPF (variable slope)
    CascadedFilter lpfFilter;

    // Oversampling for analog-matched response (prevents Nyquist cramping)
    // Pre-allocated at both 2x and 4x to avoid runtime allocation when switching
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
    int oversamplingMode = 0;  // 0=Off, 1=2x, 2=4x
    bool oversamplerReady = false;  // Flag to track if oversamplers are initialized

    // Pre-allocated scratch buffer for British/Pultec processing (avoids heap alloc in processBlock)
    juce::AudioBuffer<float> scratchBuffer;
    int maxOversampledBlockSize = 0;  // Maximum block size after oversampling

    // Current sample rate (may be oversampled)
    double currentSampleRate = 44100.0;
    double baseSampleRate = 44100.0;  // Original sample rate before oversampling

    //==============================================================================
    // Crossfade smoothing (prevents clicks on state changes)

    // Bypass crossfade (~5ms)
    juce::SmoothedValue<float> bypassSmoothed{0.0f};
    juce::AudioBuffer<float> dryBuffer;  // Copy of input for bypass crossfade

    // Per-band enable/disable crossfade (~3ms)
    std::array<juce::SmoothedValue<float>, NUM_BANDS> bandEnableSmoothed;

    // EQ type switching crossfade (~10ms)
    juce::SmoothedValue<float> eqTypeCrossfade{1.0f};  // 1.0 = fully new type
    juce::AudioBuffer<float> prevTypeBuffer;  // Saved output from previous EQ type
    EQType previousEQType = EQType::Digital;
    bool eqTypeChanging = false;

    // Oversampling mode switch crossfade (~5ms)
    juce::SmoothedValue<float> osCrossfade{1.0f};  // 1.0 = fully new mode
    juce::AudioBuffer<float> prevOsBuffer;  // Saved output from previous OS mode
    bool osChanging = false;

    //==============================================================================
    // Parameter pointers
    std::array<std::atomic<float>*, NUM_BANDS> bandEnabledParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandFreqParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandGainParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandQParams{};
    std::array<std::atomic<float>*, 2> bandSlopeParams{};  // Only for bands 1 and 8

    std::atomic<float>* masterGainParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* hqEnabledParam = nullptr;
    std::atomic<float>* linearPhaseEnabledParam = nullptr;
    std::atomic<float>* linearPhaseLengthParam = nullptr;
    std::atomic<float>* processingModeParam = nullptr;
    std::atomic<float>* qCoupleModeParam = nullptr;

    std::atomic<float>* analyzerEnabledParam = nullptr;
    std::atomic<float>* analyzerPrePostParam = nullptr;
    std::atomic<float>* analyzerModeParam = nullptr;
    std::atomic<float>* analyzerResolutionParam = nullptr;
    std::atomic<float>* analyzerDecayParam = nullptr;

    std::atomic<float>* displayScaleModeParam = nullptr;
    std::atomic<float>* visualizeMasterGainParam = nullptr;

    // EQ Type parameter
    std::atomic<float>* eqTypeParam = nullptr;

    // British mode specific parameters
    std::atomic<float>* britishHpfFreqParam = nullptr;
    std::atomic<float>* britishHpfEnabledParam = nullptr;
    std::atomic<float>* britishLpfFreqParam = nullptr;
    std::atomic<float>* britishLpfEnabledParam = nullptr;
    std::atomic<float>* britishLfGainParam = nullptr;
    std::atomic<float>* britishLfFreqParam = nullptr;
    std::atomic<float>* britishLfBellParam = nullptr;
    std::atomic<float>* britishLmGainParam = nullptr;
    std::atomic<float>* britishLmFreqParam = nullptr;
    std::atomic<float>* britishLmQParam = nullptr;
    std::atomic<float>* britishHmGainParam = nullptr;
    std::atomic<float>* britishHmFreqParam = nullptr;
    std::atomic<float>* britishHmQParam = nullptr;
    std::atomic<float>* britishHfGainParam = nullptr;
    std::atomic<float>* britishHfFreqParam = nullptr;
    std::atomic<float>* britishHfBellParam = nullptr;
    std::atomic<float>* britishModeParam = nullptr;  // Brown/Black
    std::atomic<float>* britishSaturationParam = nullptr;
    std::atomic<float>* britishInputGainParam = nullptr;
    std::atomic<float>* britishOutputGainParam = nullptr;

    // British EQ processor
    BritishEQProcessor britishEQ;
    std::atomic<bool> britishParamsChanged{true};

    // Pultec EQ processor
    PultecProcessor pultecEQ;
    std::atomic<bool> pultecParamsChanged{true};

    // Pultec mode specific parameters
    std::atomic<float>* pultecLfBoostGainParam = nullptr;
    std::atomic<float>* pultecLfBoostFreqParam = nullptr;
    std::atomic<float>* pultecLfAttenGainParam = nullptr;
    std::atomic<float>* pultecHfBoostGainParam = nullptr;
    std::atomic<float>* pultecHfBoostFreqParam = nullptr;
    std::atomic<float>* pultecHfBoostBandwidthParam = nullptr;
    std::atomic<float>* pultecHfAttenGainParam = nullptr;
    std::atomic<float>* pultecHfAttenFreqParam = nullptr;
    std::atomic<float>* pultecInputGainParam = nullptr;
    std::atomic<float>* pultecOutputGainParam = nullptr;
    std::atomic<float>* pultecTubeDriveParam = nullptr;

    // Pultec Mid Dip/Peak section parameters
    std::atomic<float>* pultecMidEnabledParam = nullptr;
    std::atomic<float>* pultecMidLowFreqParam = nullptr;
    std::atomic<float>* pultecMidLowPeakParam = nullptr;
    std::atomic<float>* pultecMidDipFreqParam = nullptr;
    std::atomic<float>* pultecMidDipParam = nullptr;
    std::atomic<float>* pultecMidHighFreqParam = nullptr;
    std::atomic<float>* pultecMidHighPeakParam = nullptr;

    // Dynamic EQ processor
    DynamicEQProcessor dynamicEQ;
    std::atomic<bool> dynamicParamsChanged{true};

    // Linear Phase EQ processor (one per channel for stereo)
    std::array<LinearPhaseEQProcessor, 2> linearPhaseEQ;
    bool linearPhaseModeEnabled = false;
    std::atomic<bool> linearPhaseParamsChanged{true};

    // Dynamic mode per-band parameters
    std::array<std::atomic<float>*, NUM_BANDS> bandDynEnabledParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynThresholdParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynAttackParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynReleaseParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynRangeParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynRatioParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandShapeParams{};  // Shape for parametric bands (Peaking/Notch/BandPass)
    std::array<std::atomic<float>*, NUM_BANDS> bandRoutingParams{};  // Per-band channel routing
    std::atomic<float>* dynDetectionModeParam = nullptr;

    // Safe parameter accessor
    float safeGetParam(std::atomic<float>* param, float defaultValue) const
    {
        return param ? param->load() : defaultValue;
    }

    //==============================================================================
    // Auto-gain compensation (maintains consistent loudness for A/B comparison)
    std::atomic<float>* autoGainEnabledParam = nullptr;
    juce::SmoothedValue<float> autoGainCompensation{1.0f};  // Linear gain multiplier
    float inputRmsSum = 0.0f;
    float outputRmsSum = 0.0f;
    int rmsSampleCount = 0;
    static constexpr int RMS_WINDOW_SAMPLES = 4410;  // ~100ms at 44.1kHz

    //==============================================================================
    // Filter coefficient calculation methods (analog-matched)

    // High-pass filter coefficients with proper pre-warping
    void updateHPFCoefficients(double sampleRate);

    // Low-pass filter coefficients with proper pre-warping
    void updateLPFCoefficients(double sampleRate);

    // Shelving filter coefficients (analog-matched)
    juce::dsp::IIR::Coefficients<float>::Ptr makeLowShelfCoefficients(
        double sampleRate, float freq, float gain, float q) const;
    juce::dsp::IIR::Coefficients<float>::Ptr makeHighShelfCoefficients(
        double sampleRate, float freq, float gain, float q) const;

    // Parametric (peaking) filter coefficients (analog-matched)
    juce::dsp::IIR::Coefficients<float>::Ptr makePeakingCoefficients(
        double sampleRate, float freq, float gain, float q) const;

    // Notch (band-reject) filter coefficients (analog-matched)
    juce::dsp::IIR::Coefficients<float>::Ptr makeNotchCoefficients(
        double sampleRate, float freq, float q) const;

    // Bandpass filter coefficients (analog-matched)
    juce::dsp::IIR::Coefficients<float>::Ptr makeBandPassCoefficients(
        double sampleRate, float freq, float q) const;

    // Tilt shelf filter coefficients (1st-order shelving tilt)
    juce::dsp::IIR::Coefficients<float>::Ptr makeTiltShelfCoefficients(
        double sampleRate, float freq, float gain) const;

    // Update all filter coefficients
    void updateAllFilters();
    void updateBandFilter(int bandIndex);

    // Update dynamic gain filter for a band (uses same freq/Q but dynamic gain)
    void updateDynGainFilter(int bandIndex, float dynGainDb);

    //==============================================================================
    // Atomic dirty flags for filter updates
    std::atomic<bool> filtersNeedUpdate{true};
    std::array<std::atomic<bool>, NUM_BANDS> bandDirty{};

    //==============================================================================
    // FFT Analyzer
    static constexpr int FFT_ORDER_LOW = 11;    // 2048
    static constexpr int FFT_ORDER_MEDIUM = 12; // 4096
    static constexpr int FFT_ORDER_HIGH = 13;   // 8192

    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> fftWindow;

    std::vector<float> fftInputBuffer;
    std::vector<float> fftOutputBuffer;
    std::array<float, 2048> analyzerMagnitudes{};  // Always 2048 bins for display

    juce::AbstractFifo analyzerFifo{8192};
    std::vector<float> analyzerAudioBuffer;
    std::atomic<bool> analyzerDataReady{false};

    int currentFFTSize = 4096;
    void updateFFTSize(AnalyzerResolution resolution);
    void processFFT();

    // Peak hold and decay for analyzer
    std::array<float, 2048> peakHoldValues{};
    float analyzerDecayRate = 20.0f;  // dB per second

    //==============================================================================
    // Parameter layout creation
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    // M/S encoding/decoding
    // Note: These handle the case where left and right may be the same pointer (mono)
    void encodeMS(float& left, float& right) const
    {
        float l = left;
        float r = right;
        left = (l + r) * 0.5f;   // mid
        right = (l - r) * 0.5f;  // side
    }

    void decodeMS(float& mid, float& side) const
    {
        float m = mid;
        float s = side;
        mid = m + s;   // left
        side = m - s;  // right
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiQ)
};

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include "EQBand.h"
#include "BritishEQProcessor.h"
#include "PultecProcessor.h"
#include "DynamicEQProcessor.h"

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

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

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

    // Get current Q-couple mode for UI display
    QCoupleMode getCurrentQCoupleMode() const;

    // Get effective Q value after coupling (for UI display)
    float getEffectiveQ(int bandNum) const;

    // Get frequency response magnitude at a specific frequency (for curve display)
    float getFrequencyResponseMagnitude(float frequencyHz) const;

private:
    //==============================================================================
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==============================================================================
    // Filter structures for each band type

    // Multi-stage cascaded filter for variable slope HPF/LPF
    struct CascadedFilter
    {
        static constexpr int MAX_STAGES = 4;  // Up to 8th order (48 dB/oct)
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

    // Band 8: LPF (variable slope)
    CascadedFilter lpfFilter;

    // Oversampling for analog-matched response (prevents Nyquist cramping)
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    bool hqModeEnabled = false;

    // Current sample rate (may be oversampled)
    double currentSampleRate = 44100.0;
    double baseSampleRate = 44100.0;  // Original sample rate before oversampling

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

    // Dynamic mode per-band parameters
    std::array<std::atomic<float>*, NUM_BANDS> bandDynEnabledParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynThresholdParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynAttackParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynReleaseParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynRangeParams{};
    std::atomic<float>* dynDetectionModeParam = nullptr;

    // Safe parameter accessor
    float safeGetParam(std::atomic<float>* param, float defaultValue) const
    {
        return param ? param->load() : defaultValue;
    }

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

    // Update all filter coefficients
    void updateAllFilters();
    void updateBandFilter(int bandIndex);

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

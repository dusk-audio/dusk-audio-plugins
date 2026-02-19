#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include "EQBand.h"
#include "BritishEQProcessor.h"
#include "TubeEQProcessor.h"
#include "DynamicEQProcessor.h"
#include "LinearPhaseEQProcessor.h"
#include "OutputLimiter.h"
#include "EQMatchProcessor.h"
#include "../shared/AnalogEmulation/WaveshaperCurves.h"
#include "MultiQPresets.h"

//==============================================================================
// Biquad coefficient storage with magnitude evaluation (no heap allocation)
//==============================================================================
struct BiquadCoeffs
{
    // Stored as normalized: {b0/a0, b1/a0, b2/a0, 1, a1/a0, a2/a0}
    float coeffs[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};

    /** Evaluate |H(e^jw)| at the given frequency (returns linear magnitude) */
    double getMagnitudeForFrequency(double freq, double sampleRate) const
    {
        double w = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
        double cosw = std::cos(w);
        double cos2w = 2.0 * cosw * cosw - 1.0;
        double sinw = std::sin(w);
        double sin2w = 2.0 * sinw * cosw;

        double numR = coeffs[0] + coeffs[1] * cosw + coeffs[2] * cos2w;
        double numI = -(coeffs[1] * sinw + coeffs[2] * sin2w);
        double denR = coeffs[3] + coeffs[4] * cosw + coeffs[5] * cos2w;
        double denI = -(coeffs[4] * sinw + coeffs[5] * sin2w);

        double numMagSq = numR * numR + numI * numI;
        double denMagSq = denR * denR + denI * denI;

        if (denMagSq < 1e-20) return 1.0;
        return std::sqrt(numMagSq / denMagSq);
    }

    void setIdentity()
    {
        coeffs[0] = 1.0f; coeffs[1] = 0.0f; coeffs[2] = 0.0f;
        coeffs[3] = 1.0f; coeffs[4] = 0.0f; coeffs[5] = 0.0f;
    }

    /** Copy these coefficients into a JUCE IIR filter's pre-allocated Coefficients (no heap allocation) */
    void applyToFilter(juce::dsp::IIR::Filter<float>& filter) const
    {
        auto* raw = filter.coefficients->getRawCoefficients();
        for (int i = 0; i < 6; ++i)
            raw[i] = coeffs[i];
    }
};

//==============================================================================
// Cytomic SVF (State Variable Filter) for per-sample coefficient interpolation
// Based on Andrew Simper's "Linear Trapezoidal Integrated SVF" design
//==============================================================================
struct SVFCoeffs
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;  // SVF core
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;  // Mix: out = m0*x + m1*v1 + m2*v2

    void setIdentity()
    {
        a1 = 1.0f; a2 = 0.0f; a3 = 0.0f;
        m0 = 1.0f; m1 = 0.0f; m2 = 0.0f;
    }
};

struct CytomicSVF
{
    float ic1eq = 0.0f;  // Integrator state 1
    float ic2eq = 0.0f;  // Integrator state 2
    SVFCoeffs coeffs;     // Current (smoothed) coefficients
    SVFCoeffs target;     // Target coefficients
    float smoothCoeff = 0.02f;  // Interpolation rate (~1ms at 44.1kHz)
    bool converged = true;      // Skip interpolation when coefficients match target

    void reset()
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    void setTarget(const SVFCoeffs& t)
    {
        target = t;
        converged = false;  // New target — need to interpolate
    }

    void snapToTarget()
    {
        coeffs = target;
        converged = true;
    }

    void setSmoothCoeff(float c) { smoothCoeff = c; }

    float processSample(float x)
    {
        // Per-sample coefficient interpolation (skip when converged)
        if (!converged)
        {
            float s = smoothCoeff;
            coeffs.a1 += s * (target.a1 - coeffs.a1);
            coeffs.a2 += s * (target.a2 - coeffs.a2);
            coeffs.a3 += s * (target.a3 - coeffs.a3);
            coeffs.m0 += s * (target.m0 - coeffs.m0);
            coeffs.m1 += s * (target.m1 - coeffs.m1);
            coeffs.m2 += s * (target.m2 - coeffs.m2);

            // Check convergence (all coefficients within epsilon of target)
            constexpr float eps = 1e-7f;
            if (std::abs(coeffs.a1 - target.a1) < eps &&
                std::abs(coeffs.a2 - target.a2) < eps &&
                std::abs(coeffs.a3 - target.a3) < eps &&
                std::abs(coeffs.m0 - target.m0) < eps &&
                std::abs(coeffs.m1 - target.m1) < eps &&
                std::abs(coeffs.m2 - target.m2) < eps)
            {
                coeffs = target;  // Snap to exact values
                converged = true;
            }
        }

        // SVF tick (linear trapezoidal integration)
        float v3 = x - ic2eq;
        float v1 = coeffs.a1 * ic1eq + coeffs.a2 * v3;
        float v2 = ic2eq + coeffs.a2 * ic1eq + coeffs.a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        return coeffs.m0 * x + coeffs.m1 * v1 + coeffs.m2 * v2;
    }
};

struct StereoSVF
{
    CytomicSVF filterL;
    CytomicSVF filterR;

    void reset()
    {
        filterL.reset();
        filterR.reset();
    }

    void setTarget(const SVFCoeffs& c)
    {
        filterL.setTarget(c);
        filterR.setTarget(c);
    }

    void snapToTarget()
    {
        filterL.snapToTarget();
        filterR.snapToTarget();
    }

    void setSmoothCoeff(float c)
    {
        filterL.setSmoothCoeff(c);
        filterR.setSmoothCoeff(c);
    }

    float processSampleL(float s) { return filterL.processSample(s); }
    float processSampleR(float s) { return filterR.processSample(s); }
};

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

    // Pre-EQ analyzer data (for dual spectrum overlay)
    const std::array<float, 2048>& getPreAnalyzerMagnitudes() const { return preAnalyzerMagnitudes; }
    bool isPreAnalyzerDataReady() const { return preAnalyzerDataReady.load(); }
    void clearPreAnalyzerDataReady() { preAnalyzerDataReady.store(false); }

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

    // Delta solo: hear only what a band changes (output = with_band - without_band)
    void setDeltaSoloMode(bool delta) { deltaSoloMode.store(delta); }
    bool isDeltaSoloMode() const { return deltaSoloMode.load(); }

    //==============================================================================
    // Output limiter (mastering safety brickwall)
    float getLimiterGainReduction() const { return outputLimiter.getGainReduction(); }
    bool isLimiterEnabled() const;

    //==============================================================================
    // Cross-mode band transfer
    // Transfers the current British/Tube EQ curve to Digital mode parameters
    void transferCurrentEQToDigital();

    //==============================================================================
    // EQ Match (capture reference/source spectra, compute parametric fit, apply)
    void captureMatchReference();   // Snapshot current analyzer as reference
    void captureMatchSource();      // Snapshot current analyzer as source
    int  computeEQMatch(float strength = 1.0f);  // Fit bands, returns count
    void applyEQMatch();            // Write fitted bands to Digital mode params
    bool hasMatchReference() const { return eqMatchProcessor.isReferenceSet(); }
    bool hasMatchSource() const    { return eqMatchProcessor.isTargetSet(); }
    void clearEQMatch()            { eqMatchProcessor.clearReference(); eqMatchProcessor.clearTarget(); }

private:
    std::atomic<int> soloedBand{-1};  // -1 = no solo, 0-7 = that band is soloed
    std::atomic<bool> deltaSoloMode{false};  // true = delta solo (hear band's effect only)
    //==============================================================================
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
    // Band 1: HPF (variable slope)
    CascadedFilter hpfFilter;

    // Bands 2-7: Cytomic SVF filters (per-sample coefficient interpolation)
    std::array<StereoSVF, 6> svfFilters;  // Indices 0-5 for bands 2-7

    // Dynamic gain SVF filters (bands 2-7) - applies dynamic EQ gain independently of static gain
    std::array<StereoSVF, 6> svfDynGainFilters;  // Same indices as svfFilters

    // Band 8: LPF (variable slope)
    CascadedFilter lpfFilter;

    // Oversampling for analog-matched response (prevents Nyquist cramping)
    // Pre-allocated at both 2x and 4x to avoid runtime allocation when switching
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
    int oversamplingMode = 0;  // 0=Off, 1=2x, 2=4x
    bool oversamplerReady = false;  // Flag to track if oversamplers are initialized

    // Pre-allocated scratch buffer for British/Tube EQ processing (avoids heap alloc in processBlock)
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

    // Tube EQ processor
    TubeEQProcessor tubeEQ;
    std::atomic<bool> tubeEQParamsChanged{true};

    // Tube EQ mode specific parameters
    std::atomic<float>* tubeEQLfBoostGainParam = nullptr;
    std::atomic<float>* tubeEQLfBoostFreqParam = nullptr;
    std::atomic<float>* tubeEQLfAttenGainParam = nullptr;
    std::atomic<float>* tubeEQHfBoostGainParam = nullptr;
    std::atomic<float>* tubeEQHfBoostFreqParam = nullptr;
    std::atomic<float>* tubeEQHfBoostBandwidthParam = nullptr;
    std::atomic<float>* tubeEQHfAttenGainParam = nullptr;
    std::atomic<float>* tubeEQHfAttenFreqParam = nullptr;
    std::atomic<float>* tubeEQInputGainParam = nullptr;
    std::atomic<float>* tubeEQOutputGainParam = nullptr;
    std::atomic<float>* tubeEQTubeDriveParam = nullptr;

    // Tube EQ Mid Dip/Peak section parameters
    std::atomic<float>* tubeEQMidEnabledParam = nullptr;
    std::atomic<float>* tubeEQMidLowFreqParam = nullptr;
    std::atomic<float>* tubeEQMidLowPeakParam = nullptr;
    std::atomic<float>* tubeEQMidDipFreqParam = nullptr;
    std::atomic<float>* tubeEQMidDipParam = nullptr;
    std::atomic<float>* tubeEQMidHighFreqParam = nullptr;
    std::atomic<float>* tubeEQMidHighPeakParam = nullptr;

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

    // Per-band saturation parameters (bands 2-7 only, indices 1-6)
    std::array<std::atomic<float>*, NUM_BANDS> bandSatTypeParams{};   // 0=Off, 1=Tape, 2=Tube, 3=Console, 4=FET
    std::array<std::atomic<float>*, NUM_BANDS> bandSatDriveParams{};  // 0.0-1.0

    // Safe parameter accessor
    float safeGetParam(std::atomic<float>* param, float defaultValue) const
    {
        return param ? param->load() : defaultValue;
    }

    //==============================================================================
    // EQ Match processor (spectrum capture + parametric fitting)
    EQMatchProcessor eqMatchProcessor;

    //==============================================================================
    // Output limiter (brickwall safety limiter for mastering)
    OutputLimiter outputLimiter;
    std::atomic<float>* limiterEnabledParam = nullptr;
    std::atomic<float>* limiterCeilingParam = nullptr;

    //==============================================================================
    // Auto-gain compensation (maintains consistent loudness for A/B comparison)
    std::atomic<float>* autoGainEnabledParam = nullptr;
    juce::SmoothedValue<float> autoGainCompensation{1.0f};  // Linear gain multiplier
    float inputRmsSum = 0.0f;
    float outputRmsSum = 0.0f;
    int rmsSampleCount = 0;
    static constexpr int RMS_WINDOW_SAMPLES = 22050;  // ~500ms at 44.1kHz (mastering-appropriate)

    //==============================================================================
    // Non-allocating coefficient computation (Audio EQ Cookbook with pre-warping)
    // These compute directly into BiquadCoeffs without any heap allocation,
    // making them safe to call from the audio thread.

    static double preWarpFrequency(double freq, double sampleRate);
    static void computePeakingCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q);
    static void computeLowShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q);
    static void computeHighShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q);
    static void computeNotchCoeffs(BiquadCoeffs& c, double sr, double freq, float q);
    static void computeBandPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q);
    static void computeHighPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q);
    static void computeLowPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q);
    static void computeFirstOrderHighPassCoeffs(BiquadCoeffs& c, double sr, double freq);
    static void computeFirstOrderLowPassCoeffs(BiquadCoeffs& c, double sr, double freq);
    static void computeTiltShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB);

    //==============================================================================
    // SVF coefficient computation (Cytomic SVF topology)
    // These compute SVFCoeffs for the audio processing path.
    // The biquad methods above are retained for UI curve display and HPF/LPF.

    static void computeSVFPeaking(SVFCoeffs& c, double sr, double freq, float gainDB, float q);
    static void computeSVFLowShelf(SVFCoeffs& c, double sr, double freq, float gainDB, float q);
    static void computeSVFHighShelf(SVFCoeffs& c, double sr, double freq, float gainDB, float q);
    static void computeSVFNotch(SVFCoeffs& c, double sr, double freq, float q);
    static void computeSVFBandPass(SVFCoeffs& c, double sr, double freq, float q);
    static void computeSVFHighPass(SVFCoeffs& c, double sr, double freq, float q);
    static void computeSVFTiltShelf(SVFCoeffs& c, double sr, double freq, float gainDB);

    // Compute SVF band coefficients based on band index and current parameters
    void computeBandSVFCoeffs(int bandIndex, SVFCoeffs& c) const;
    void computeBandSVFCoeffsWithGain(int bandIndex, float overrideGainDB, SVFCoeffs& c) const;

    // SVF smoothing coefficient (sample-rate dependent, ~1ms transition)
    float svfSmoothCoeff = 0.02f;

    // Compute band coefficients into BiquadCoeffs based on band index and current parameters
    void computeBandCoeffs(int bandIndex, BiquadCoeffs& c) const;
    void computeBandCoeffsWithGain(int bandIndex, float overrideGainDB, BiquadCoeffs& c) const;

    //==============================================================================
    // Filter update methods (use non-allocating coefficient computation)

    void updateHPFCoefficients(double sampleRate);
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
    void updateDynGainFilter(int bandIndex, float dynGainDb);

    //==============================================================================
    // UI coefficient storage (written by audio thread, read by UI thread for curve display)
    // Benign data race: UI may briefly see partially-updated coefficients (visual glitch only)
    std::array<BiquadCoeffs, 6> uiBandCoeffs{};       // Bands 2-7 (index 0-5)
    std::array<BiquadCoeffs, CascadedFilter::MAX_STAGES> uiHpfCoeffs{};
    std::array<BiquadCoeffs, CascadedFilter::MAX_STAGES> uiLpfCoeffs{};
    int uiHpfStages = 0;
    int uiLpfStages = 0;

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

    // Pre-EQ analyzer (for dual spectrum overlay)
    juce::AbstractFifo preAnalyzerFifo{8192};
    std::vector<float> preAnalyzerAudioBuffer;
    std::array<float, 2048> preAnalyzerMagnitudes{};
    std::atomic<bool> preAnalyzerDataReady{false};
    std::vector<float> preFFTInputBuffer;
    std::array<float, 2048> prePeakHoldValues{};

    int currentFFTSize = 4096;
    void updateFFTSize(AnalyzerResolution resolution);
    void processFFT();
    void processPreFFT();

    // Shared FFT-to-magnitudes conversion (avoids code duplication)
    void convertFFTToMagnitudes(std::vector<float>& fftBuffer,
                                std::array<float, 2048>& magnitudes,
                                std::array<float, 2048>& peakHold,
                                std::atomic<bool>& readyFlag);

    // Scratch buffer for block-based mono downmix (analyzer feed)
    std::vector<float> analyzerMonoBuffer;

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

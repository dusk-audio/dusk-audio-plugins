#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include "EQBand.h"
#include "BritishEQProcessor.h"
#include "TubeEQProcessor.h"
#include "DynamicEQProcessor.h"
#include "OutputLimiter.h"
#include "EQMatchProcessor.h"
#include "../shared/AnalogEmulation/WaveshaperCurves.h"
#include "MultiQPresets.h"
#include "SafeFloat.h"

// Biquad coefficient storage with magnitude evaluation (no heap allocation)
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

    /** Copy these coefficients into a JUCE IIR filter's pre-allocated Coefficients.
        JUCE normalizes by a0 and stores only 5 elements for a biquad: {b0, b1, b2, a1, a2}
        (a0 is divided out during Coefficients construction, not stored).
        Our format is {b0/a0, b1/a0, b2/a0, 1, a1/a0, a2/a0} — skip index 3 (a0). */
    void applyToFilter(juce::dsp::IIR::Filter<float>& filter) const
    {
        if (filter.coefficients == nullptr)
            return;
        auto* raw = filter.coefficients->getRawCoefficients();
        raw[0] = coeffs[0];  // b0/a0
        raw[1] = coeffs[1];  // b1/a0
        raw[2] = coeffs[2];  // b2/a0
        raw[3] = coeffs[4];  // a1/a0 (skip coeffs[3] which is a0=1)
        raw[4] = coeffs[5];  // a2/a0
    }
};

// Cytomic SVF (State Variable Filter) for per-sample coefficient interpolation
// Based on Andrew Simper's "Linear Trapezoidal Integrated SVF" design
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

    void setSmoothCoeff(float c) { smoothCoeff = juce::jlimit(0.0f, 1.0f, c); }

    float processSample(float x)
    {
        // Sanitize input to prevent NaN/Inf from entering or passing through the filter
        if (!safeIsFinite(x))
            x = 0.0f;

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

        // Sanitize state variables to prevent NaN/Inf propagation (permanent corruption)
        if (!safeIsFinite(ic1eq) || !safeIsFinite(ic2eq))
        {
            ic1eq = 0.0f;
            ic2eq = 0.0f;
            return 0.0f;  // Zero output when state is corrupted
        }

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

/**
    Stereo Direct Form II Transposed biquad.

    Used for the static EQ bands (svfFilters) instead of CytomicSVF.
    The SVF topology produces a topology-dependent frequency response that varies
    with sample rate (because `g = tan(π·fc/sr)` has different behaviour at large g
    values near Nyquist). DF2T with AnalogMatchedBiquad coefficients uses
    `cos(2π·fc/sr)` for exact digital centre placement and pre-warped bandwidth,
    giving the same filter shape at every OS rate.

    Per-sample coefficient smoothing (first-order IIR ramp on each b/a coefficient)
    prevents zipper noise during automation or knob drags — same approach as StereoSVF.
    setSmoothCoeff sets the per-sample decay; snapToTarget jumps immediately.
*/
struct StereoBiquad
{
    BiquadCoeffs coeffs;       // current (smoothed) coefficients
    BiquadCoeffs target;       // target coefficients set by setCoeffs()
    float smoothCoeff = 0.02f; // per-sample: coeffs → target (~1ms at 44.1kHz, set via setSmoothCoeff)
    float s1L = 0.0f, s2L = 0.0f;
    float s1R = 0.0f, s2R = 0.0f;

    void setCoeffs(const BiquadCoeffs& c) { target = c; }
    void reset() { s1L = s2L = s1R = s2R = 0.0f; coeffs = target; }
    void snapToTarget() { coeffs = target; }
    void setSmoothCoeff(float c) { smoothCoeff = c; }

    float processSampleL(float x)
    {
        if (!safeIsFinite(x)) x = 0.0f;
        // Step coefficients toward target
        for (int i = 0; i < 6; ++i)
            coeffs.coeffs[i] += smoothCoeff * (target.coeffs[i] - coeffs.coeffs[i]);
        float y = coeffs.coeffs[0] * x + s1L;
        s1L = coeffs.coeffs[1] * x - coeffs.coeffs[4] * y + s2L;
        s2L = coeffs.coeffs[2] * x - coeffs.coeffs[5] * y;
        if (!safeIsFinite(y)) { s1L = s2L = 0.0f; return 0.0f; }
        return y;
    }

    float processSampleR(float x)
    {
        if (!safeIsFinite(x)) x = 0.0f;
        float y = coeffs.coeffs[0] * x + s1R;
        s1R = coeffs.coeffs[1] * x - coeffs.coeffs[4] * y + s2R;
        s2R = coeffs.coeffs[2] * x - coeffs.coeffs[5] * y;
        if (!safeIsFinite(y)) { s1R = s2R = 0.0f; return 0.0f; }
        return y;
    }
};

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
    static constexpr const char* PLUGIN_VERSION = "1.0.0";
    static constexpr int NUM_BANDS = 8;

    MultiQ();
    ~MultiQ() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    juce::AudioProcessorParameter* getBypassParameter() const override;

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getLatencySamples() const;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

public:
    const std::vector<MultiQPresets::Preset>& getFactoryPresets() const { return factoryPresets; }

private:
    int currentPresetIndex = 0;
    std::vector<MultiQPresets::Preset> factoryPresets;

public:

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Undo/Redo system
    juce::UndoManager undoManager;
    juce::UndoManager& getUndoManager() { return undoManager; }

    // Guard: set during transferCurrentEQToDigital() to prevent the preset selector
    // from loading "Init" (which would reset all band gains) via the async UI update chain.
    std::atomic<bool> transferInProgress{false};

    // Public parameter access for GUI
    juce::AudioProcessorValueTreeState parameters;

    // FFT data access for analyzer display
    // Thread-safe ring buffer for audio capture
    void pushSamplesToAnalyzer(const float* samples, int numSamples, bool isPreEQ);

    // Get magnitude data for display (call from UI thread).
    // Post-EQ analyzer data (locked copy to prevent data race with audio thread)
    std::array<float, 2048> getAnalyzerMagnitudes()
    {
        juce::SpinLock::ScopedLockType lock(analyzerMagnitudesLock);
        return analyzerMagnitudes;
    }
    bool isAnalyzerDataReady() const { return analyzerDataReady.load(); }
    void clearAnalyzerDataReady() { analyzerDataReady.store(false); }

    // Pre-EQ analyzer data (for dual spectrum overlay)
    std::array<float, 2048> getPreAnalyzerMagnitudes()
    {
        juce::SpinLock::ScopedLockType lock(preAnalyzerMagnitudesLock);
        return preAnalyzerMagnitudes;
    }
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

    // Get per-band frequency response magnitude in dB (for individual band curve display)
    float getPerBandMagnitude(int bandIndex, float frequencyHz) const;

    // Compute per-band magnitude on-the-fly from current parameters (not from double buffer)
    float computePerBandMagnitudeFresh(int bandIndex, float frequencyHz) const;

    // Get frequency response including dynamic gain (for dynamic curve overlay)
    float getFrequencyResponseWithDynamics(float frequencyHz) const;

    // Get current dynamic gain for a band (for UI visualization, thread-safe)
    float getDynamicGain(int bandIndex) const { return dynamicEQ.getCurrentDynamicGain(bandIndex); }

    // Get current processing mode (0=Stereo, 1=Left, 2=Right, 3=Mid, 4=Side)
    int getProcessingMode() const { return static_cast<int>(safeGetParam(processingModeParam, 0.0f)); }

    // Get base sample rate (before oversampling) for UI frequency calculations
    double getBaseSampleRate() const { return baseSampleRate; }

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

    // Output limiter (mastering safety brickwall)
    float getLimiterGainReduction() const { return outputLimiter.getGainReduction(); }
    bool isLimiterEnabled() const;

    // Cross-mode band transfer
    // Transfers the current British/Tube EQ curve to Digital mode parameters
    void transferCurrentEQToDigital();

    // Reset all parameters to default flat EQ (Init preset).
    // Called ONLY from the plugin's own UI (Init preset selection).
    // setCurrentProgram(0) does NOT call this — that would let Logic Pro's
    // host-driven program changes silently wipe user settings.
    void resetToInit();

    // Match EQ — Logic Pro Match EQ style (Welch's method + FIR convolution)
    void startLearnCurrent()
    {
        pendingStopLearning.store(false);         // Cancel any pending stop
        pendingStartLearnReference.store(false);   // Cancel any pending reference start
        pendingStartLearnCurrent.store(true);
    }
    void startLearnReference()
    {
        pendingStopLearning.store(false);
        pendingStartLearnCurrent.store(false);
        pendingStartLearnReference.store(true);
    }
    void stopLearning()
    {
        pendingStartLearnCurrent.store(false);    // Cancel any pending starts
        pendingStartLearnReference.store(false);
        pendingStopLearning.store(true);
    }
    bool computeMatchCorrection();  // Compute smoothed FIR, load into convolution
    void clearMatchEQ()         { pendingMatchClear.store(true); }

    // Match EQ state queries — pending-aware to avoid UI/audio thread race
    bool isMatchLearning() const { return eqMatchProcessor.isLearning(); }
    bool isMatchLearningOrPending() const
    {
        return eqMatchProcessor.isLearning()
            || pendingStartLearnCurrent.load(std::memory_order_acquire)
            || pendingStartLearnReference.load(std::memory_order_acquire);
    }
    bool isMatchLearningCurrent() const { return eqMatchProcessor.isLearningCurrent(); }
    bool isMatchLearningCurrentOrPending() const
    {
        return eqMatchProcessor.isLearningCurrent()
            || pendingStartLearnCurrent.load(std::memory_order_acquire);
    }
    bool isMatchLearningReference() const { return eqMatchProcessor.isLearningReference(); }
    bool isMatchLearningReferenceOrPending() const
    {
        return eqMatchProcessor.isLearningReference()
            || pendingStartLearnReference.load(std::memory_order_acquire);
    }
    int getMatchLearningFrameCount() const { return eqMatchProcessor.getLearningFrameCount(); }
    // All three return false immediately when a clear is pending so the graph
    // goes fully blank before the audio thread processes the deferred clear.
    bool hasMatchCurrentSpectrum() const
    {
        if (pendingMatchClear.load(std::memory_order_acquire) ||
            pendingMatchFadeOut.load(std::memory_order_acquire))
            return false;
        return eqMatchProcessor.hasCurrentSpectrum();
    }
    bool hasMatchReferenceSpectrum() const
    {
        if (pendingMatchClear.load(std::memory_order_acquire) ||
            pendingMatchFadeOut.load(std::memory_order_acquire))
            return false;
        return eqMatchProcessor.hasReferenceSpectrum();
    }
    bool hasMatchCorrectionCurve() const
    {
        if (pendingMatchClear.load(std::memory_order_acquire) ||
            pendingMatchFadeOut.load(std::memory_order_acquire))
            return false;
        return eqMatchProcessor.hasCorrectionCurve();
    }
    bool isMatchConvolutionActive() const { return matchConvolutionActive.load(std::memory_order_acquire); }

    bool isMatchMode() const
    {
        return static_cast<int>(safeGetParam(eqTypeParam, 0.0f)) == static_cast<int>(EQType::Match);
    }

    // Spectrum data for display (thread-safe)
    void getMatchCurrentSpectrumDB(std::array<float, EQMatchProcessor::NUM_BINS>& out) const
    {
        eqMatchProcessor.getCurrentSpectrumDB(out);
    }
    void getMatchReferenceSpectrumDB(std::array<float, EQMatchProcessor::NUM_BINS>& out) const
    {
        eqMatchProcessor.getReferenceSpectrumDB(out);
    }
    void getMatchCorrectionCurveDB(std::array<float, EQMatchProcessor::NUM_BINS>& out) const
    {
        eqMatchProcessor.getCorrectionCurveDB(out);
    }

private:
    std::atomic<int> soloedBand{-1};  // -1 = no solo, 0-7 = that band is soloed
    std::atomic<bool> deltaSoloMode{false};  // true = delta solo (hear band's effect only)
    void parameterChanged(const juce::String& parameterID, float newValue) override;

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
            int stages = activeStages.load(std::memory_order_acquire);
            for (int i = 0; i < stages; ++i)
            {
                float prev = sample;
                sample = stagesL[static_cast<size_t>(i)].processSample(sample);
                if (!safeIsFinite(sample))
                {
                    stagesL[static_cast<size_t>(i)].reset();
                    sample = prev;  // Use input as fallback instead of 0
                }
            }
            return sample;
        }

        float processSampleR(float sample)
        {
            int stages = activeStages.load(std::memory_order_acquire);
            for (int i = 0; i < stages; ++i)
            {
                float prev = sample;
                sample = stagesR[static_cast<size_t>(i)].processSample(sample);
                if (!safeIsFinite(sample))
                {
                    stagesR[static_cast<size_t>(i)].reset();
                    sample = prev;
                }
            }
            return sample;
        }
    };
    // Band 1: HPF (variable slope)
    CascadedFilter hpfFilter;

    // Bands 2-7: Cytomic SVF filters (per-sample coefficient interpolation)
    std::array<StereoBiquad, 6> svfFilters;  // Indices 0-5 for bands 2-7 (DF2T, AnalogMatchedBiquad coeffs)

    // Dynamic gain SVF filters (bands 2-7) - applies dynamic EQ gain independently of static gain
    std::array<StereoSVF, 6> svfDynGainFilters;  // Same indices as svfFilters

    // Band 8: LPF (variable slope)
    CascadedFilter lpfFilter;

    // Oversampling for analog-matched response (prevents Nyquist cramping)
    // Pre-allocated at both 2x and 4x to avoid runtime allocation when switching
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
    std::atomic<int> oversamplingMode{0};  // 0=Off, 1=2x, 2=4x
    bool oversamplerReady = false;  // Flag to track if oversamplers are initialized
    int lastPreparedBlockSize = 0;  // Track block size for oversampler reinitialization

    // Pre-allocated scratch buffer for British/Tube EQ processing (avoids heap alloc in processBlock)
    juce::AudioBuffer<float> scratchBuffer;
    int maxOversampledBlockSize = 0;  // Maximum block size after oversampling

    // Current sample rate (may be oversampled)
    // Atomic: written by audio thread (prepareToPlay/processBlock), read by GUI thread (getFrequencyResponseMagnitude)
    std::atomic<double> currentSampleRate{44100.0};
    std::atomic<double> baseSampleRate{44100.0};

    // Crossfade smoothing (prevents clicks on state changes)

    // Bypass crossfade (~5ms)
    juce::SmoothedValue<float> bypassSmoothed{0.0f};
    juce::AudioBuffer<float> dryBuffer;  // Copy of input for bypass crossfade

    // Bypass delay line: maintains time-alignment during bypass so DAW PDC stays consistent.
    // Without this, toggling bypass triggers DAW PDC recalculation → audible click/gap.
    juce::AudioBuffer<float> bypassDelayBuffer;
    int bypassDelayWritePos = 0;
    int bypassDelayLength = 0;   // Updated when latency changes
    int bypassDelayFillCount = 0; // Samples written since last clear; gates delayed-read path

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

    // Oversampling latency compensation: always report max (4x) latency so DAW PDC stays aligned
    int maxOversamplerLatency = 0;  // Cached 4x oversampler latency (set in prepareToPlay)
    juce::AudioBuffer<float> osCompDelayBuffer;  // Compensation delay for Off/2x modes
    int osCompDelayWritePos = 0;
    int osCompDelaySamples = 0;  // Current compensation: maxLatency - currentModeLatency

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

    // Match EQ parameters
    std::atomic<float>* matchApplyParam = nullptr;
    std::atomic<float>* matchSmoothingParam = nullptr;
    std::atomic<float>* matchLimitBoostParam = nullptr;
    std::atomic<float>* matchLimitCutParam = nullptr;

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


    // Dynamic mode per-band parameters
    std::array<std::atomic<float>*, NUM_BANDS> bandDynEnabledParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynThresholdParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynAttackParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynReleaseParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynRangeParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandDynRatioParams{};
    std::array<std::atomic<float>*, NUM_BANDS> bandShapeParams{};  // Shape for parametric bands (Peaking/Notch/BandPass)
    std::array<std::atomic<float>*, NUM_BANDS> bandRoutingParams{};  // Per-band channel routing
    std::array<std::atomic<float>*, NUM_BANDS> bandInvertParams{};       // Invert gain (boost↔cut)
    std::array<std::atomic<float>*, NUM_BANDS> bandPhaseInvertParams{};  // Flip polarity of band effect
    std::array<std::atomic<float>*, NUM_BANDS> bandPanParams{};          // Stereo pan of band effect
    std::array<float, NUM_BANDS> prevBandPhaseInvertGain{};  // Smoothed phase invert: +1 or -1
    std::array<float, NUM_BANDS> prevBandPanVal{};           // Smoothed pan value
    std::atomic<float>* dynDetectionModeParam = nullptr;

    // Per-band saturation parameters (bands 2-7 only, indices 1-6)
    std::array<std::atomic<float>*, NUM_BANDS> bandSatTypeParams{};   // 0=Off, 1=Tape, 2=Tube, 3=Console, 4=FET
    std::array<std::atomic<float>*, NUM_BANDS> bandSatDriveParams{};  // 0.0-1.0

    // Safe parameter accessor
    float safeGetParam(std::atomic<float>* param, float defaultValue) const
    {
        return param ? param->load() : defaultValue;
    }

    // Match EQ processor (Welch's method + FIR generation)
    EQMatchProcessor eqMatchProcessor;

    // Match EQ convolution engine (applies the correction FIR)
    juce::dsp::Convolution matchConvolution;
    std::atomic<bool> matchConvolutionActive{false};  // True when FIR is loaded and active
    juce::SmoothedValue<float> matchConvWetGain { 1.0f };  // Fades 1→0 on clear (audio thread only)
    std::atomic<bool> pendingMatchFadeOut { false };       // Set by audio thread to complete clear after fade
    juce::AudioBuffer<float> matchDryBuffer;               // Pre-allocated dry copy for crossfade blend

    // Thread-safe pending operations (UI -> audio thread)
    std::atomic<bool> pendingMatchClear{false};
    std::atomic<bool> pendingStartLearnCurrent{false};
    std::atomic<bool> pendingStartLearnReference{false};
    std::atomic<bool> pendingStopLearning{false};

    // Output limiter (brickwall safety limiter for mastering)
    OutputLimiter outputLimiter;
    std::atomic<float>* limiterEnabledParam = nullptr;
    std::atomic<float>* limiterCeilingParam = nullptr;

    // Auto-gain compensation (maintains consistent loudness for A/B comparison)
    std::atomic<float>* autoGainEnabledParam = nullptr;
    juce::SmoothedValue<float> autoGainCompensation{1.0f};  // Linear gain multiplier
    float inputRmsSum = 0.0f;
    float outputRmsSum = 0.0f;
    float inputPeakMax = 0.0f;   // window max peak for peak-safe autogain
    float outputPeakMax = 0.0f;
    int rmsSampleCount = 0;
    int rmsWindowSamples = 88200;  // 2s at 44.1kHz — updated in prepareToPlay

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
    void computeBandCoeffsAtRate(int bandIndex, BiquadCoeffs& c, double sr) const;
    void computeBandCoeffsWithGain(int bandIndex, float overrideGainDB, BiquadCoeffs& c, double sr) const;

    // Filter update methods (use non-allocating coefficient computation)

    void updateHPFCoefficients(double sampleRate);
    void updateLPFCoefficients(double sampleRate);
    void updateAllFilters();
    void updateBandFilter(int bandIndex);
    void updateDynGainFilter(int bandIndex, float dynGainDb);

    // UI coefficient storage: lock-free double buffer for thread-safe audio→UI transfer.
    // Audio thread writes to the inactive buffer, then atomically publishes via index swap.
    // UI thread reads the active buffer after acquiring the index.
    struct UICoeffBuffer
    {
        std::array<BiquadCoeffs, 6> bandCoeffs{};       // Bands 2-7 (index 0-5)
        std::array<BiquadCoeffs, CascadedFilter::MAX_STAGES> hpfCoeffs{};
        std::array<BiquadCoeffs, CascadedFilter::MAX_STAGES> lpfCoeffs{};
        int hpfStages = 0;
        int lpfStages = 0;
    };
    std::array<UICoeffBuffer, 2> uiCoeffBuffers{};
    std::atomic<int> uiCoeffActiveIndex{0};  // Audio publishes with release, UI reads with acquire

    // Helpers for the double buffer
    UICoeffBuffer& uiWriteBuffer()
    {
        return uiCoeffBuffers[static_cast<size_t>(1 - uiCoeffActiveIndex.load(std::memory_order_relaxed))];
    }
    void publishUICoeffs()
    {
        // Only called from audio thread (single producer)
        uiCoeffActiveIndex.fetch_xor(1, std::memory_order_release);
    }
    const UICoeffBuffer& uiReadBuffer() const
    {
        return uiCoeffBuffers[static_cast<size_t>(uiCoeffActiveIndex.load(std::memory_order_acquire))];
    }

    // Atomic dirty flags for filter updates
    std::atomic<bool> filtersNeedUpdate{true};
    std::array<std::atomic<bool>, NUM_BANDS> bandDirty{};

    // FFT Analyzer — pre-allocated for all 3 sizes to avoid RT allocation
    static constexpr int FFT_ORDER_LOW = 11;    // 2048
    static constexpr int FFT_ORDER_MEDIUM = 12; // 4096
    static constexpr int FFT_ORDER_HIGH = 13;   // 8192
    static constexpr int NUM_FFT_SIZES = 3;

    // Pre-allocated FFT objects and windowing tables for each resolution
    struct FFTSlot {
        std::unique_ptr<juce::dsp::FFT> fft;
        std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
        std::vector<float> inputBuffer;      // Post-EQ scratch
        std::vector<float> preInputBuffer;   // Pre-EQ scratch
        int size = 0;                         // FFT size (e.g. 2048, 4096, 8192)
    };
    std::array<FFTSlot, NUM_FFT_SIZES> fftSlots;
    int activeFFTSlot = 1;  // Default = medium (index 1)

    std::array<float, 2048> analyzerMagnitudes{};  // Always 2048 bins for display
    mutable juce::SpinLock analyzerMagnitudesLock;  // Protects analyzerMagnitudes + match data for UI reads

    juce::AbstractFifo analyzerFifo{8192};
    std::vector<float> analyzerAudioBuffer;
    std::atomic<bool> analyzerDataReady{false};

    // Pre-EQ analyzer (for dual spectrum overlay)
    juce::AbstractFifo preAnalyzerFifo{8192};
    std::vector<float> preAnalyzerAudioBuffer;
    std::array<float, 2048> preAnalyzerMagnitudes{};
    juce::SpinLock preAnalyzerMagnitudesLock;  // Protects preAnalyzerMagnitudes (audio→GUI)
    std::atomic<bool> preAnalyzerDataReady{false};
    std::array<float, 2048> prePeakHoldValues{};

    int currentFFTSize = 4096;
    std::atomic<int> pendingFFTOrder{-1};  // -1 = no pending change
    void updateFFTSize(int order);
    void processFFT();
    void processPreFFT();

    // Shared FFT-to-magnitudes conversion (avoids code duplication)
    void convertFFTToMagnitudes(std::vector<float>& fftBuffer,
                                std::array<float, 2048>& magnitudes,
                                std::array<float, 2048>& peakHold);

    // Scratch buffer for block-based mono downmix (analyzer feed)
    std::vector<float> analyzerMonoBuffer;

    // Peak hold and decay for analyzer
    std::array<float, 2048> peakHoldValues{};
    float analyzerDecayRate = 20.0f;  // dB per second

    // Parameter layout creation
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // M/S encoding/decoding
    // Note: These copy to locals first, so they are safe even if left==right (mono)
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
    JUCE_DECLARE_WEAK_REFERENCEABLE(MultiQ)
};

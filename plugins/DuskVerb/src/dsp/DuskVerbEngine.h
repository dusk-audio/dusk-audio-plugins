#pragma once

#include "AlgorithmConfig.h"
#include "DattorroTank.h"
#include "DiffusionStage.h"
#include "EarlyReflections.h"
#include "FDNReverb.h"
#include "OutputDiffusion.h"
#include "QuadTank.h"

#include <cmath>
#include <vector>

// One-pole exponential smoother for per-sample parameter interpolation.
// Prevents zipper noise when parameters change between processing sub-blocks.
struct OnePoleSmoother
{
    float current = 0.0f;
    float target = 0.0f;
    float coeff = 0.0f;

    void reset (float value) { current = target = value; }

    void setSmoothingTime (double sampleRate, float timeMs)
    {
        coeff = std::exp (-1000.0f / (std::max (timeMs, 0.1f)
                                      * static_cast<float> (sampleRate)));
    }

    void setTarget (float t) { target = t; }

    float next()
    {
        current = target + coeff * (current - target);
        return current;
    }
};

class DuskVerbEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int numSamples);

    void setAlgorithm (int index);
    void setDecayTime (float seconds);
    void setBassMultiply (float mult);
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);
    void setModDepth (float depth);
    void setModRate (float hz);
    void setSize (float size);
    void setPreDelay (float milliseconds);
    void setDiffusion (float amount);
    void setOutputDiffusion (float amount);
    void setERLevel (float level);
    void setERSize (float size);
    void setMix (float dryWet);
    void setLoCut (float hz);
    void setHiCut (float hz);
    void setWidth (float width);
    void setFreeze (bool frozen);
    void setGateParams (float holdMs, float releaseMs);
    void setGainTrim (float dB);
    void setInputOnset (float ms);
    void loadCorrectionFilter (int presetIndex);
    void loadPresetTapPositions (int presetIndex);
    void setDattorroDelayScale (float scale);
    void setDattorroSoftOnsetMs (float ms);
    void setLateFeedForwardLevel (float level);
    void setDattorroLimiter (float thresholdDb, float releaseMs);
    void updateTapPositions (float l0, float l1, float l2, float l3, float l4, float l5, float l6,
                             float r0, float r1, float r2, float r3, float r4, float r5, float r6);
    void updateTapPositionsAndGains (float l0, float l1, float l2, float l3, float l4, float l5, float l6,
                                     float r0, float r1, float r2, float r3, float r4, float r5, float r6,
                                     float gl0, float gl1, float gl2, float gl3, float gl4, float gl5, float gl6,
                                     float gr0, float gr1, float gr2, float gr3, float gr4, float gr5, float gr6);
    // Apply gains on top of existing tap positions (does not change positions)
    void applyTapGains (float gl0, float gl1, float gl2, float gl3, float gl4, float gl5, float gl6,
                        float gr0, float gr1, float gr2, float gr3, float gr4, float gr5, float gr6);
    void setCustomERTaps (const CustomERTap* taps, int numTaps);
    void loadPresetERTaps (const char* presetName);  // Looks up VV-extracted taps by name

private:
    DiffusionStage diffuser_;
    FDNReverb fdn_;
    DattorroTank dattorroTank_;
    OutputDiffusion outputDiffuser_;
    EarlyReflections er_;
    bool useDattorroTank_ = false;
    bool useQuadTank_ = false;
    QuadTank quadTank_;
    QuadTank hybridQuadTank_;  // Dedicated secondary engine for hybrid dual-engine mode

    const AlgorithmConfig* config_ = &kHall;

    std::vector<float> scratchL_;
    std::vector<float> scratchR_;
    std::vector<float> erOutL_;
    std::vector<float> erOutR_;

    // Hybrid dual-engine: secondary engine scratch buffers
    std::vector<float> hybridL_;
    std::vector<float> hybridR_;
    float hybridBlend_ = 0.0f;
    const AlgorithmConfig* hybridConfig_ = nullptr;
    std::vector<float> preDiffL_;
    std::vector<float> preDiffR_;

    std::vector<float> preDelayBufL_;
    std::vector<float> preDelayBufR_;
    int preDelayWritePos_ = 0;
    int preDelayMask_ = 0;
    int preDelaySamples_ = 0;

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 0;

    // Per-sample smoothed output parameters (prevents zipper noise on fast automation)
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother erLevelSmoother_;
    OnePoleSmoother widthSmoother_;
    OnePoleSmoother loCutSmoother_;
    OnePoleSmoother hiCutSmoother_;

    float erLevelScale_ = 1.0f;
    float lateGainScale_ = 1.0f;
    float decayGainComp_ = 1.0f; // Decay-dependent output compensation (short-decay boost)
    float erCrossfeed_ = 0.0f;
    OnePoleSmoother outputGainSmoother_; // Per-algorithm output gain (smoothed to avoid clicks)

    float decayTime_ = 2.5f; // Cached for decay-linked output diffusion
    float gainTrimLinear_ = 1.0f; // Per-preset level correction (linear multiplier from dB)

    // Per-preset output peak limiter (applied after late gain, before output diffuser)
    float outputLimiterThreshold_ = 0.0f; // 0 = disabled. Linear amplitude.
    float outputLimiterRelease_ = 0.999f; // One-pole release (~30ms at 48kHz)
    float outputLimiterEnv_ = 0.0f;       // Current peak envelope

    // Cached raw param values for re-application after algorithm switch
    float lastDiffusion_ = 0.75f;
    float lastOutputDiffusion_ = 0.5f;
    float lastModDepth_ = 0.4f;
    float lastModRate_ = 0.8f;
    float lastTrebleMult_ = 0.5f;
    float lastBassMult_ = 1.2f;
    float lastERLevel_ = 0.5f;

    // Input bandwidth filter (Dattorro-style one-pole LP, ~10kHz)
    float inputBwCoeff_ = 0.0f;
    float inputBwStateL_ = 0.0f;
    float inputBwStateR_ = 0.0f;

    // DC blocker (first-order highpass, ~5Hz cutoff)
    float dcCoeff_ = 0.9993f;
    float dcX1L_ = 0.0f, dcY1L_ = 0.0f;
    float dcX1R_ = 0.0f, dcY1R_ = 0.0f;

    // Output EQ: second-order Butterworth biquads (wet signal only)
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1L = 0.0f, z2L = 0.0f;
        float z1R = 0.0f, z2R = 0.0f;

        float processL (float x)
        {
            float y = b0 * x + z1L;
            z1L = b1 * x - a1 * y + z2L;
            z2L = b2 * x - a2 * y;
            return y;
        }
        float processR (float x)
        {
            float y = b0 * x + z1R;
            z1R = b1 * x - a1 * y + z2R;
            z2R = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1L = z2L = z1R = z2R = 0.0f; }
    };

    Biquad loCutFilter_;
    Biquad hiCutFilter_;
    Biquad hiCutFilter2_;  // Second cascade stage: 4-pole (24 dB/oct) total for vintage anti-aliasing
    float loCutHz_ = 20.0f;
    float hiCutHz_ = 20000.0f;
    void updateLoCutCoeffs();
    void updateHiCutCoeffs();

    // Per-algorithm output EQ: low shelf at 250Hz + mid parametric + high shelf.
    Biquad lowShelfFilter_;
    Biquad highShelfFilter_;
    Biquad midEQFilter_;
    bool lowShelfEnabled_ = false;
    bool highShelfEnabled_ = false;
    bool midEQEnabled_ = false;
    void updateOutputEQCoeffs();

    // Anti-alias filtering is handled inside the FDN feedback loop (first-order LP at ~17kHz).
    // Accumulates naturally across iterations — no output-stage filter needed.

    // Freeze mode
    bool frozen_ = false;

    // Saturation: when false, bypass fastTanh for clean linear output
    bool enableSaturation_ = false;

    // RMS-tracking peak limiter: reduces crest factor by limiting instantaneous
    // peaks relative to short-term RMS. Targets FDN's Hadamard peak correlation.
    float crestLimitRatio_ = 0.0f;   // 0 = disabled, 1.5 = limit peaks to 1.5× RMS
    static constexpr float kRmsWindowMs = 5.0f;
    int rmsWindowSamples_ = 0;
    std::vector<float> rmsBufferL_;
    std::vector<float> rmsBufferR_;
    float rmsSumL_ = 0.0f;
    float rmsSumR_ = 0.0f;
    int rmsIndex_ = 0;

    // Late feed-forward: pre-diffusion late reverb blended into output
    float lateFeedForwardLevel_ = 0.0f;

    // Late reverb onset envelope: ramps FDN output from 0→1 to match VV's
    // slower density buildup (parallel FDN inherently produces too much early energy)
    float lateOnsetRamp_ = 1.0f;      // Current envelope value (0→1)
    float lateOnsetIncrement_ = 0.0f; // Per-sample increment (1.0 / rampSamples)
    int   lateOnsetSamples_ = 0;      // Total ramp duration in samples

    // Per-preset spectral correction filter: 5-stage biquad cascade that shapes
    // DattorroTank output to match VV's exact frequency response.
    // Coefficients generated offline by generate_correction_filters.py.
    static constexpr int kNumCorrectionStages = 8;
    Biquad correctionFilter_[kNumCorrectionStages] {};
    bool correctionFilterActive_ = false;

    // Gate envelope: truncates reverb tail (gated reverb effect)
    bool gateEnabled_ = false;
    int gateHoldSamples_ = 0;
    float gateReleaseCoeff_ = 0.0f;
    float gateEnvelope_ = 0.0f;
    int gateHoldCounter_ = 0;
    bool gateTriggered_ = false;

    // Algorithm crossfade: mute-and-morph to prevent clicks on algorithm switch
    static constexpr int kFadeSamples = 64;
    int pendingAlgorithm_ = -1;
    int fadeCounter_ = 0;
    bool fadingOut_ = false;
    bool firstAlgorithmSet_ = true;

    void applyAlgorithm (int index);
};

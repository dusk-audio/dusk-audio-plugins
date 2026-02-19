#pragma once

#include "AlgorithmConfig.h"
#include "DiffusionStage.h"
#include "EarlyReflections.h"
#include "FDNReverb.h"
#include "OutputDiffusion.h"

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

private:
    DiffusionStage diffuser_;
    FDNReverb fdn_;
    OutputDiffusion outputDiffuser_;
    EarlyReflections er_;

    const AlgorithmConfig* config_ = &kHall;

    std::vector<float> scratchL_;
    std::vector<float> scratchR_;
    std::vector<float> erOutL_;
    std::vector<float> erOutR_;

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

    float decayTime_ = 2.5f; // Cached for decay-linked output diffusion

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
    float loCutHz_ = 20.0f;
    float hiCutHz_ = 20000.0f;
    void updateLoCutCoeffs();
    void updateHiCutCoeffs();

    // Freeze mode
    bool frozen_ = false;

    // Algorithm crossfade: mute-and-morph to prevent clicks on algorithm switch
    static constexpr int kFadeSamples = 64;
    int pendingAlgorithm_ = -1;
    int fadeCounter_ = 0;
    bool fadingOut_ = false;
    bool firstAlgorithmSet_ = true;

    void applyAlgorithm (int index);
};

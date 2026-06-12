#pragma once

#include "DiffusionStage.h" // reuses ModulatedAllpass
#include <algorithm>

// Post-FDN output diffusion: 4 cascaded allpass filters per channel.
// Lower coefficient than input diffusion to add density without smearing stereo image.
//
// Activated per-preset (Bright Hall, 2026-06-10) to smear the FDN's sparse
// high-frequency tail modes into a denser, smoother wash — the 16-line FDN
// leaves isolated HF resonances above 4 kHz (tail spectral kurtosis ~19 vs a
// dense reference ~12) that read as a metallic ring. delayScale spreads the
// allpass delays so the smearing reaches the closely-spaced HF modes.
class OutputDiffusion
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int numSamples);
    void setDiffusion (float amount);
    void clear()
    {
        for (int s = 0; s < kNumStages; ++s) { leftAP_[s].clear(); rightAP_[s].clear(); }
    }
    // 0 disables the built-in LFO wobble on every output allpass (the
    // cumulative effect across 4 stages × 2 channels can cause pitch
    // smearing for presets that stack this on top of a modulated tank).
    void setLfoDepthScale (float scale)
    {
        lfoScale_ = scale;
        for (int s = 0; s < kNumStages; ++s)
        {
            leftAP_[s].setLfoDepthScale (scale);
            rightAP_[s].setLfoDepthScale (scale);
        }
    }
    // Scale the base allpass delays (× scale). Longer delays decorrelate the
    // closely-spaced HF modes more (denser smearing). Re-prepares the stages
    // (message-thread / preset-apply only — allocates).
    void setDelayScale (float scale)
    {
        delayScale_ = std::max (0.25f, scale);
        if (prepared_)
            buildStages();
    }

private:
    void buildStages();

    // 8 stages (was 4): turning the FDN's sparse HF modes into a dense
    // Gaussian wash needs a deep dispersion cascade — 4 weak allpasses only
    // dropped tail kurtosis 19.3->18.0. Mutually-prime delays for maximal
    // dispersion density.
    static constexpr int kNumStages = 8;
    static constexpr int kBaseDelays[kNumStages] = { 523, 163, 349, 241, 431, 283, 617, 191 };

    ModulatedAllpass leftAP_[kNumStages];
    ModulatedAllpass rightAP_[kNumStages];

    float  diffusionCoeff_ = 0.4f;
    float  delayScale_     = 1.0f;
    float  lfoScale_       = 1.0f;
    double sampleRate_     = 44100.0;
    bool   prepared_       = false;
};

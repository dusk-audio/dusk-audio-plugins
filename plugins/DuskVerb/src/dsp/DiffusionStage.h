#pragma once

#include "DspUtils.h"

#include <vector>

// Single modulated allpass filter with circular buffer and cubic interpolation.
// Uses the Schroeder allpass topology: H(z) = (z^-D - g) / (1 - g*z^-D)
//
// In addition to the existing sine LFO modulation, this AP also supports a
// Lexicon-style spin-and-wander jitter on the read position. Rationale: the
// LFO is too slow (~0.3-0.8 Hz, ±1 sample) to break the AP's modal
// phase-locking when the delay is long. The jitter operates in a faster
// band (auto-set so its period ≈ 2× delay) so it disrupts ring coherence
// while staying inaudible as flutter — the same fix used in the density
// cascades of SixAPTank / Dattorro / QuadTank.
class ModulatedAllpass
{
public:
    void prepare (int bufferSize, float delayInSamples, float lfoRateHz,
                  float lfoDepthSamples, float lfoStartPhase, double sampleRate);
    float process (float input, float g);
    void clear();
    // Scale the LFO depth by a factor (0 = disable modulation, 1 = original).
    // Used to tame the cumulative seasick wobble when many stages cascade.
    void setLfoDepthScale (float scale) { lfoDepth_ = baseLfoDepth_ * scale; }

    // Enable spin-and-wander jitter. fraction is the depth as a fraction of
    // delaySamples (typical 0.015 = 1.5 %). 0 disables. Must call AFTER
    // prepare() so sampleRate_ is set.
    void enableJitter (float fraction, std::uint32_t seed);

private:
    std::vector<float> buffer_;
    int writePos_ = 0;
    int mask_ = 0;
    float delaySamples_ = 0.0f;
    float lfoPhase_ = 0.0f;
    float lfoPhaseInc_ = 0.0f;
    float lfoDepth_ = 0.0f;
    float baseLfoDepth_ = 0.0f;  // Captured in prepare() for later rescaling.
    float sampleRate_ = 44100.0f;

    DspUtils::RandomWalkLFO jitterLFO_;
    float                   jitterDepthFraction_ = 0.0f;

    static constexpr float kTwoPi = 6.283185307179586f;
};

// Cascaded modulated allpass input diffuser (4 stages per channel, stereo).
// Smears transients into a dense wash before the FDN.
class DiffusionStage
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int numSamples);
    void setDiffusion (float amount);
    void setMaxCoefficients (float max12, float max34);

    // Zero every allpass delay buffer + write position. LFO phases keep their
    // state (they track musical time, not signal energy). Used by the
    // processor's preset-swap path to flush stale signal content before an
    // idle engine is brought back online.
    void clear()
    {
        for (int s = 0; s < kNumStages; ++s)
        {
            leftAP_[s] .clear();
            rightAP_[s].clear();
        }
    }

private:
    static constexpr int kNumStages = 4;
    // {142, 107, 211, 167} samples at 44.1k base — chosen to balance two
    // competing concerns measured on real renders:
    //   • Canonical Dattorro 1997 values {142, 107, 379, 277} produced an
    //     audible 6-9 ms echo (ring at stage 3+4 delay periods) clearly
    //     visible on every 6-AP hall preset (+20 dB second event 15 ms
    //     after pre-delay end). Spin-and-wander jitter helps but can't
    //     fully break Schroeder ringing because it modulates the READ
    //     position, not the recursive feedback path.
    //   • Very-short version {142, 107, 79, 67} eliminated the ring but
    //     gave too-sharp wet onsets (insufficient cascade smear width).
    //   • {142, 107, 211, 167} brings the longest stage to 4.4 ms at 44.1k
    //     base (4.8 ms at 48k after rateRatio) — JUST below the 5 ms
    //     psychoacoustic echo threshold so any ring is heard as colour,
    //     not echo. Total cascade still gives 13.7 ms smear width.
    static constexpr int kBaseDelays[kNumStages] = { 142, 107, 211, 167 };

    ModulatedAllpass leftAP_[kNumStages];
    ModulatedAllpass rightAP_[kNumStages];
public:
    // Propagate a global LFO depth scale to every stage. 0 disables the
    // chorus-like wobble entirely (static allpasses). Called per-preset
    // from the wrapper prepare() — e.g. Vocal Plate zeros these to kill
    // the 16-AP cumulative seasick modulation.
    void setLfoDepthScale (float scale)
    {
        for (int s = 0; s < kNumStages; ++s)
        {
            leftAP_[s].setLfoDepthScale (scale);
            rightAP_[s].setLfoDepthScale (scale);
        }
    }

private:

    float diffusionCoeff12_ = 0.45f;  // Stages 1-2: higher diffusion (Dattorro: max 0.75)
    float diffusionCoeff34_ = 0.375f; // Stages 3-4: lower for transient clarity (Dattorro: max 0.625)
    // Restored to Dattorro 1997 values for engines that need full input
    // diffusion (Dattorro / QuadTank / FDN). For SixAPTank we BYPASS
    // the diffuser entirely — see DuskVerbEngine::process() — because the
    // 6-AP density cascade with spin-and-wander handles input smearing,
    // and the cascaded Schroeder peaks at sum-of-delays (~14 ms) produced
    // an audible discrete echo on every 6-AP hall preset (verified by
    // render-tool measurement: +20 dB event 15 ms after pre-delay end).
    float maxCoeff12_ = 0.75f;
    float maxCoeff34_ = 0.625f;
    float lastDiffusionAmount_ = 0.6f;
};

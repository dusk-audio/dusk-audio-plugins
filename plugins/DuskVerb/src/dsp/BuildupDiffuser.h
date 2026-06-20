#pragma once

#include "DiffusionStage.h"   // ModulatedAllpass (Schroeder AP + spin-and-wander jitter)

#include <cstdint>

// BuildupDiffuser — a LONG cascaded-allpass front-end that spreads a transient's
// energy over ~80-130 ms so the late tank's impulse response BUILDS GRADUALLY
// instead of being dense from sample zero.
//
// Why this exists (hall ER+tail topology, 2026-06-19): the VVV/Lexicon halls
// front a snare with sparse discrete early reflections (onset → gap → loud 2nd
// hit ~130-140 ms) over a diffuse tail that BUILDS over ~150 ms. DuskVerb's
// DenseHall tank is dense from sample zero, so it FILLS the gap the discrete 2nd
// hit needs (no dip → a prominent late tap swallows the attack; proven
// exhaustively). A plain pre-delay (tankOnset) only SHIFTS the dense onset (sharp
// "sounds-like-a-delay" kick + collapses the energy spread). A long allpass
// cascade instead SMEARS the onset into a rising cloud — continuous-processing-
// safe (no per-hit envelope), so the tank's early output is quiet and ramps up,
// leaving the early window to the SparseField ER (with its dip + burst2 tap).
//
// Schroeder allpasses are energy-preserving (flat magnitude) → no spectral tilt,
// only temporal smear. Stereo, independent L/R delay sets → the buildup stays
// decorrelated (wide). Bypassed (amount 0 / not prepared) → caller feeds the tank
// directly → bit-null.
class BuildupDiffuser
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate;
        buildCascade();
        prepared_ = true;
    }

    // amount 0 → bypass (bit-null: caller should skip this stage). 1 → full cascade.
    void setAmount (float a) { amount_ = std::clamp (a, 0.0f, 1.0f); }

    // Per-preset BUILD TIME = hall size: scales the cascade delays, so a bigger hall
    // (later energy centroid) builds over a longer window. 1.0 = ~84 ms (Bright Hall);
    // a larger value (e.g. 1.6 → ~134 ms) suits a much larger space (Blade Runner,
    // anchor t50 ~446 ms vs ~204). Re-builds the cascade (message-thread preset-apply;
    // change-guarded so it only re-allocates when the scale actually moves).
    void setTimeScale (float s)
    {
        const float v = std::clamp (s, 0.5f, 3.0f);
        if (std::abs (v - timeScale_) <= 1.0e-4f) return;
        timeScale_ = v;
        if (prepared_) buildCascade();
    }
    bool active() const      { return prepared_ && amount_ > 1.0e-6f; }

    void setLfoDepthScale (float scale)
    {
        for (int s = 0; s < kNumStages; ++s) { apL_[s].setLfoDepthScale (scale); apR_[s].setLfoDepthScale (scale); }
    }

    // In-place: fully smears L/R over the cascade (the tank then builds gradually
    // from the spread input). A dry/wet crossfade was tried + reverted — blending
    // the DIRECT transient back in re-front-loads the energy (first50 hot). The
    // buildup TIME (where t50 lands) is set by the cascade DELAYS (baked), not by
    // `amount`; `amount` only scales the wet level so a preset can ease it in.
    void process (float* left, float* right, int numSamples)
    {
        if (! active()) return;
        const float g = 0.62f;                  // full diffusion (below the 0.7 ring threshold)
        const float wet = amount_;
        for (int i = 0; i < numSamples; ++i)
        {
            float dl = left[i], dr = right[i];
            for (int s = 0; s < kNumStages; ++s) { dl = apL_[s].process (dl, g); dr = apR_[s].process (dr, g); }
            left[i]  = wet * dl;
            right[i] = wet * dr;
        }
    }

    void clear()
    {
        for (int s = 0; s < kNumStages; ++s) { apL_[s].clear(); apR_[s].clear(); }
    }

private:
    // (Re)build the allpass cascade at the current sampleRate_ × timeScale_.
    void buildCascade()
    {
        const float r = static_cast<float> (sampleRate_ / 44100.0) * timeScale_;   // 44.1k-referenced × build-time
        for (int s = 0; s < kNumStages; ++s)
        {
            const int dL = static_cast<int> (kBaseDelaysL[s] * r);
            const int dR = static_cast<int> (kBaseDelaysR[s] * r);
            // ModulatedAllpass::prepare does mask_=bufferSize-1 with NO rounding → the
            // bufferSize MUST be a power of 2 (else the & mask read wraps to garbage and
            // the all-pass leaks ~33 dB). Round up past the delay + LFO/interp headroom.
            const int bufL = DspUtils::nextPowerOf2 (dL + 64);
            const int bufR = DspUtils::nextPowerOf2 (dR + 64);
            apL_[s].prepare (bufL, static_cast<float> (dL), kLfoRate[s], 1.5f, 0.13f * s,      sampleRate_);
            apR_[s].prepare (bufR, static_cast<float> (dR), kLfoRate[s], 1.5f, 0.13f * s + 0.5f, sampleRate_);
            // NB: NO spin-and-wander jitter here. Jitter modulates the allpass READ
            // position, which breaks the Schroeder feedforward/feedback match — on
            // these LONG stages (vs DiffusionStage's ~14 ms) it costs ~33 dB of tail
            // energy (measured). The slow LFO (≤0.31 Hz, ±1.5 smp) is enough to avoid
            // modal phase-lock without destroying the all-pass (unity-gain) identity.
        }
    }

    static constexpr int kNumStages = 8;
    // Increasing, mutually-prime delays (samples @ 44.1k) → ~84 ms cumulative smear at
    // timeScale 1.0, with rising echo density (the buildup). Tuned so the DenseHall
    // tail's energy centroid (t50) lands ~204 ms on Bright Hall (a longer cascade
    // overshot to ~294 ms). setTimeScale scales these for larger halls. L/R differ.
    static constexpr int   kBaseDelaysL[kNumStages] = { 163, 241, 331, 419, 491, 577, 683, 797 };
    static constexpr int   kBaseDelaysR[kNumStages] = { 179, 257, 349, 433, 509, 601, 709, 827 };
    static constexpr float kLfoRate[kNumStages]     = { 0.31f, 0.27f, 0.23f, 0.19f, 0.17f, 0.13f, 0.11f, 0.09f };

    ModulatedAllpass apL_[kNumStages];
    ModulatedAllpass apR_[kNumStages];
    double sampleRate_ = 44100.0;
    float  amount_     = 0.0f;     // 0 = bypass (bit-null)
    float  timeScale_  = 1.0f;     // build-time = hall size (scales the cascade delays)
    bool   prepared_   = false;
};

#pragma once

#include "DspUtils.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace duskverb::dsp
{

// =====================================================================
// RingReverb — Griesinger/Carnes sequential ring topology
// =====================================================================
//
// Sequential ring of 6 modulated delay stages with embedded allpass
// diffusion in each stage, fed by a 6-stage pre-diffuser network. This
// is NOT an FDN — it is the ring topology Griesinger and Carnes
// popularized in 1990s hardware (the same family of algorithms used in
// classic studio hall presets).
//
// Topology:
//
//   in (L,R)
//     ↓
//   6-stage pre-diffuser (Shape & Spread)
//     ↓
//   ┌─── feedback (next-sample) ────────────────────────────┐
//   ↓                                                       │
//   stage 0 → stage 1 → stage 2 → stage 3 → stage 4 → stage 5
//     ↓        ↓         ↓         ↓         ↓         ↓
//     tap      tap       tap       tap       tap       tap
//     └────────┴─────────┴─────────┴─────────┴─────────┘
//                              ↓
//                         L/R output sum
//
// Each ring stage contains:
//   1. Modulated delay line (linear-interp tap read, random-walk LFO)
//   2. 1-pole damping LP (per-stage HF roll)
//   3. Embedded 3-AP diffuser chain (phase smear IN the feedback path)
//   4. Stage gain scalar (product across 6 stages = aggregate RT60)
//
// RT60 calculation (Griesinger formula):
//   total loop = sum(stage_delays); each stage gain such that
//   product(stage_gains) = 10^(-3 · loop_time_seconds / RT60).
//   Per-stage gain ~0.86 for 1.69s RT60 + 217 ms loop. Well below
//   the unity ceiling so the ring stays unconditionally stable
//   regardless of allpass coefficient or damping.
//
// L and R run independent rings with slightly different delay primes
// (kRingDelayOffsetR) for stereo decorrelation. Cross-coupling at the
// ring inputs is controlled by setStereoWidth.

class RingReverb
{
public:
    static constexpr int kPreDiffStages = 6;
    static constexpr int kRingStages    = 6;
    static constexpr int kEmbeddedStages = 3;

    void prepare (double sampleRate, int maxBlockSize);
    void clearBuffers();
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);

    // Tank parameters.
    void setDecayTime    (float seconds);     // total ring RT60
    void setSize         (float scale);       // 0.5..2.0 delay scaling
    void setDamping      (float coeff);       // shared per-stage HF damping mix
    void setDampingFc    (float hz);          // shared per-stage damping LP fc
    void setSpread       (float multiplier);  // 0.5..2.0 — preDiff delay scaling
    void setShape        (float coeff);       // 0.0..0.85 — preDiff AP coefficient
    void setSpin         (float hz);          // 0.01..20 — per-stage LFO rate
    void setWander       (float samples);     // 0..64 — per-stage LFO depth
    void setStereoWidth  (float w);           // -1..+1 — L/R cross-coupling
    void setFreeze       (bool frozen);

private:
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float  kSafetyClip     = 8.0f;

    // ─── Pre-Diffusion Stage (Schroeder allpass) ───────────────────
    struct PreDiffStage
    {
        std::vector<float> buf;
        int writePos = 0, mask = 0, delaySamples = 0;

        float process (float input, float g)
        {
            const int r = (writePos - delaySamples) & mask;
            const float vd = buf[static_cast<size_t> (r)];
            const float vn = input + g * vd;
            buf[static_cast<size_t> (writePos)] = vn;
            writePos = (writePos + 1) & mask;
            return vd - g * vn;
        }
        void clear()
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            writePos = 0;
        }
    };

    // ─── Embedded Allpass (3 stages live INSIDE each RingStage) ──
    struct EmbeddedAllpass
    {
        std::vector<float> buf;
        int writePos = 0, mask = 0, delaySamples = 0;

        float process (float input, float g)
        {
            const int r = (writePos - delaySamples) & mask;
            const float vd = buf[static_cast<size_t> (r)];
            const float vn = input + g * vd;
            buf[static_cast<size_t> (writePos)] = vn;
            writePos = (writePos + 1) & mask;
            return vd - g * vn;
        }
        void clear()
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            writePos = 0;
        }
    };

    struct EmbeddedChain
    {
        EmbeddedAllpass stages[kEmbeddedStages];
        float process (float x, float g)
        {
            for (int s = 0; s < kEmbeddedStages; ++s)
                x = stages[s].process (x, g);
            return x;
        }
        void clear() { for (auto& s : stages) s.clear(); }
    };

    // ─── Ring Stage (delay + AP chain + damping + per-stage LFO) ──
    struct RingStage
    {
        std::vector<float> buf;
        int writePos = 0, mask = 0, baseDelaySamples = 0;
        EmbeddedChain embeddedDiff;
        // Per-stage random-walk LFO state (deterministic seed per stage).
        uint32_t rwSeed   = 1u;
        float    rwState  = 0.0f;
        float    rwAlpha  = 0.001f;  // recomputed from Spin Hz + sr
        // Per-stage damping.
        float dampState = 0.0f;
        float dampAlpha = 0.5f;
        float dampMix   = 0.0f;
        // Per-stage feedback scalar — product across all 6 = aggregate RT60.
        float stageGain = 0.96f;
        // Per-stage output tap weights — signed for L/R decorrelation.
        float outTapL = 0.0f;
        float outTapR = 0.0f;

        void clear()
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            writePos = 0;
            embeddedDiff.clear();
            rwState   = 0.0f;
            dampState = 0.0f;
        }
    };

    // Pre-diffuser prime delays (samples @ 44.1k). 6 each for L + R,
    // offset so the two channels have decorrelated input pre-diffusion.
    // Geometric ratio ~1.7 between stages: 1.5/2.6/4.5/8.0/14.0/24.9 ms.
    static constexpr int kPreDiffPrimesL[kPreDiffStages] = {
        67, 113, 197, 353, 617, 1097
    };
    static constexpr int kPreDiffPrimesR[kPreDiffStages] = {
        71, 127, 211, 367, 641, 1117
    };

    // Ring stage base delays @ 44.1k. ~13/19/27/37/53/71 ms, summing to
    // ~220 ms total loop time. All primes mutually coprime + coprime
    // with preDiff primes + embedded AP primes.
    static constexpr int kRingBaseDelays[kRingStages] = {
        577, 839, 1193, 1637, 2339, 3137
    };
    // R-channel offsets (added to L base) — small primes for decorrelation.
    static constexpr int kRingDelayOffsetR[kRingStages] = {
        7, 11, 13, 17, 19, 23
    };

    // Embedded allpass primes — 6 stages × 3 nested per stage = 18 per
    // ring (L + R = 36 total). All primes coprime with the above sets.
    // Short delays (1-5 ms) so phase smear is per-recirculation, not
    // adding much to total loop time.
    static constexpr int kEmbeddedPrimes[kRingStages][kEmbeddedStages] = {
        { 89, 149, 233}, { 97, 163, 251}, {101, 167, 263},
        {103, 173, 269}, {107, 179, 277}, {109, 181, 281}
    };

    // L + R independent rings + pre-diffusers.
    PreDiffStage preDiffL_[kPreDiffStages] {};
    PreDiffStage preDiffR_[kPreDiffStages] {};
    RingStage    stagesL_ [kRingStages]    {};
    RingStage    stagesR_ [kRingStages]    {};

    // Per-sample ring feedback (output of stage 5 fed back to stage 0
    // input on the next sample). State persists between blocks.
    float ringFeedbackL_ = 0.0f;
    float ringFeedbackR_ = 0.0f;

    // Snapshot / config (message-thread side).
    double sampleRate_     = 44100.0;
    int    maxBlockSize_   = 0;
    bool   prepared_       = false;
    bool   frozen_         = false;
    float  decayTime_      = 1.6859f;
    float  sizeScale_      = 1.0f;
    float  dampingMix_     = 0.0f;
    float  dampingFcHz_    = 6000.0f;
    float  spreadMult_     = 1.0f;
    float  shapeCoeff_     = 0.625f;
    float  spinHz_         = 2.9f;
    float  wanderSamples_  = 8.0f;
    float  stereoWidth_    = 0.0f;

    void recomputeStageGains();
    void recomputeLFORates();
    void recomputeDamping();
    void recomputePreDiffStages();
    void recomputeRingDelays();
    void recomputeOutputTaps();
};

} // namespace duskverb::dsp

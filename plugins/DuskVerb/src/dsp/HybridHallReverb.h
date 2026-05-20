#pragma once

#include "DspUtils.h"
#include "LR4BandSplit.h"
#include "RingReverb.h"

#include <array>
#include <cmath>
#include <vector>

namespace duskverb::dsp
{

// =====================================================================
// HybridHallReverb — parallel ER + Ring topology
// =====================================================================
//
// Parallel hybrid: a deterministic Early Reflection tapped delay line
// (taps hardcoded at Lex Med Hall anchor positions to guarantee
// peak_locations_ms PASS by construction) mixed against a Griesinger
// sequential Ring tail (P15 RingReverb). A macro Early/Late mix
// parameter sets the C80/D50 energy ratio directly; post-mix shelves
// shape the per-octave spectral balance.
//
// Topology:
//
//   in (L,R)
//     ↓
//     ├──→ ER TDL: 4 taps @ {0.0, 4.0, 7.52, 9.79} ms
//     │     ↓ erOut (×(1−mix) equal-power)
//     │     ┐
//     ↓     │
//   6-stage Schroeder pre-diffuser
//     ↓     │
//   RingReverb (P15) ───→ ringOut (×mix equal-power)
//     │     │
//     └─────┘
//        ↓
//   Post-mix shelves (low + high) — direct attack on c80_per_octave
//        ↓
//   out (L,R)
//
// Metric-to-axis explicit mapping:
//   peak_locations_ms        ← ER tap times (hardcoded)
//   c80, d50                 ← macro_mix
//   c80_per_octave           ← low_shelf + high_shelf
//   bass_ratio, treble_ratio ← shelves + ring damping
//   centroid_drift bin3      ← ring_damping_fc (low fc → HF dies fast)
//   late_tail × 3            ← P15 Ring (already PASSes)
//   spectral_crest_db        ← P15 Ring (already PASSes)
//   a_weighted_rms_db        ← gain_trim (engine-shared axis)

class HybridHallReverb
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void clearBuffers();
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);

    // Tank parameters — broadcast to ring_.
    void setDecayTime (float seconds);
    void setSize      (float scale);

    // ER taps — TIMES are hardcoded at Lex anchor positions; only the
    // per-tap weights are tunable.
    void setERTapWeight (int tapIdx, float weight);    // 0..3
    void setERLevel     (float level);

    // P16r2: replaced macro crossfade with INDEPENDENT er_level +
    // ring_level. The crossfade was zero-sum (boosting ER suppressed
    // ring, breaking late_tail). Independent levels let the optimizer
    // push early energy for c80/d50 WITHOUT collapsing the tail.
    void setRingLevel   (float level);   // 0..2; replaces ring half of mix

    // Post-mix shelves — per-octave spectral shaping outside the
    // recirculation. Direct attack on c80_per_octave + bass/treble ratio
    // without disturbing the ring's modal density or late tail.
    void setLowShelf  (float gainDb, float fcHz);
    void setHighShelf (float gainDb, float fcHz);

    // Ring forwarders — only the axes Optuna needs. Other ring params
    // (size, stereoWidth) stay at engine defaults to keep the search
    // space manageable.
    void setRingDamping    (float coeff);
    void setRingDampingFc  (float hz);
    void setRingSpread     (float multiplier);
    void setRingShape      (float coeff);
    void setRingSpin       (float hz);
    void setRingWander     (float samples);
    void setRingStereoWidth (float w);

    void setFreeze (bool frozen);

private:
    static constexpr int kNumERTaps = 4;
    // ER tap times in ms — HARDCODED at Lex Med Hall measured peak
    // positions to mathematically guarantee peak_locations_ms 4/4 PASS.
    // Optuna cannot move these; it only tunes the per-tap weights below.
    static constexpr float kERTapTimesMs[kNumERTaps] = {
        0.0f, 4.0f, 7.52f, 9.79f
    };
    // L/R sign pattern for stereo decorrelation. Tap 0 (direct) shares
    // sign so the impulse onset is mono-coherent; later taps alternate.
    static constexpr float kERSignL[kNumERTaps] = { +1.0f, +1.0f, -1.0f, +1.0f };
    static constexpr float kERSignR[kNumERTaps] = { +1.0f, -1.0f, +1.0f, -1.0f };

    struct ERTDL
    {
        std::vector<float> bufL, bufR;
        int writePos = 0, mask = 0;
        int tapSamples[kNumERTaps] = { 0, 0, 0, 0 };
        float weights[kNumERTaps]  = { 1.0f, 0.65f, 0.45f, 0.30f };

        void prepare (double sr);
        void clear();
        // Reads all 4 taps from the current state, returns weighted sum
        // to outL/outR. Caller writes input THEN reads in same sample
        // so the tap-0 read returns the input (gives the t=0 peak the
        // metric expects).
        void process (float inL, float inR, float& outL, float& outR);
    };
    ERTDL erTDL_;
    float erLevel_ = 1.0f;

    // Pre-diffuser — 6 Schroeder allpass stages per channel, smears the
    // ring's input. Distinct from the ER path (ER bypasses preDiff).
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
    static constexpr int kPreDiffStages = 6;
    // Primes coprime with everything else used in DuskVerb hall family.
    static constexpr int kPreDiffPrimesL[kPreDiffStages] = {
        59, 137, 233, 397, 641, 1051
    };
    static constexpr int kPreDiffPrimesR[kPreDiffStages] = {
        61, 139, 241, 419, 661, 1063
    };
    PreDiffStage preDiffL_[kPreDiffStages] {};
    PreDiffStage preDiffR_[kPreDiffStages] {};
    float preDiffG_ = 0.65f;

    // Late tail — the existing P15 Ring engine.
    RingReverb ring_;

    // Post-mix shelves. Used to dial in c80_per_octave + bass/treble
    // ratio AFTER the early/late blend so shaping doesn't disturb the
    // ring's modal balance.
    LR4BandSplit::Biquad lowShelfL_,  lowShelfR_;
    LR4BandSplit::Biquad highShelfL_, highShelfR_;
    float lowShelfGainDb_  = 0.0f;
    float lowShelfFc_      = 250.0f;
    float highShelfGainDb_ = 0.0f;
    float highShelfFc_     = 6000.0f;

    // Independent er + ring level multipliers (P16r2). No crossfade —
    // each path scales on its own. Optuna can push ER for c80/d50 without
    // suffocating the ring tail (and vice versa).
    float ringLevel_ = 1.0f;

    // Scratch buffers sized to maxBlockSize in prepare().
    std::vector<float> erOutL_, erOutR_;
    std::vector<float> ringInL_, ringInR_, ringOutL_, ringOutR_;

    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 0;
    bool   prepared_     = false;

    void recomputeShelves();
};

} // namespace duskverb::dsp

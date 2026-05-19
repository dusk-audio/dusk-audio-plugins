#pragma once

#include "DspUtils.h"

#include <array>
#include <cmath>
#include <vector>

namespace duskverb::dsp
{

// =====================================================================
// HallSubTank — single-band 8-channel Hadamard FDN
// =====================================================================
//
// Building block for HallReverb (and its forks ConcertHallReverb /
// RandomHallReverb). Three SubTanks run in parallel after an LR4
// 3-band split — each handles one frequency band with independently
// tunable decay, damping, and modulation. The whole point of this
// architecture is per-band RT60 independence: closing rt60_per_band
// 8/8 requires that bass / mid / treble decay times be decoupled at
// the topology level, not just shaped by a global damping filter.
//
// Each SubTank:
//   - 8 delay lines (caller supplies prime base lengths per band so
//     bass / mid / treble loops don't share modal frequencies)
//   - Hadamard mixing each sample (1/√8 normalized in-place WHT)
//   - One-pole high-shelf damping in each feedback path (per-channel
//     state, shared coefficient — the band's "darkness")
//   - Deterministic sine LFO per channel with anti-correlated L/R
//     phases (cosine read + π offset between paired channels), so
//     stereo_corr_stability stays low by construction (same Pillar 2
//     trick that FoilPlateEngine uses to close stab to ≤ 0.05)
//   - Stereo I/O: input is mono-summed (L+R)/2 internally then
//     distributed across channels; output is decorrelated via signed
//     tap routing (4 L taps + 4 R taps with alternating signs)
//   - Per-channel feedback gain derived from decayTime × per-channel
//     loop length
//
// Out of scope for THIS class (lives in HallReverb container):
//   - LR4 band split (uses duskverb::dsp::LR4BandSplit upstream)
//   - Multi-tap input injection (Phase 3)
//   - Post-tank M/S widener (Phase 4)
//   - Per-band gain trim / output EQ
//
class HallSubTank
{
public:
    static constexpr int N = 8;
    static constexpr int kNumOutputTaps = 4;     // 4 L + 4 R

    HallSubTank();

    // Caller passes 8 prime base delay lengths in samples at 44.1k. SubTank
    // scales them by (sampleRate / 44100) × sizeScale at prepare/setSize time.
    // maxBlockSize is informational — buffers are allocated for the max
    // delay-line length, not per-block.
    void prepare (double sampleRate, const int* baseDelays8, int maxBlockSize);
    void clear();
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    // Decay time: RT60 (seconds) at this band. Internally translates to
    // per-channel feedback gain = 10^(-3 × loopSeconds / decayTime).
    void setDecayTime    (float seconds);
    // Damping coefficient [0, 1] — 0 = bypass, 1 = aggressive HF roll-off in
    // every feedback path. Implemented as one-pole shelf coefficient.
    void setDamping      (float amount);
    // Modulation depth in samples (0..N samples typical), shared across all
    // 8 LFOs (each channel has its own phase offset for decorrelation).
    void setModDepth     (float samples);
    void setModRate      (float hz);
    // Size: multiplies base delay lengths. 1.0 = nominal; range [0.5, 2.0]
    // typical. Larger size → longer per-loop period → longer RT60 for the
    // same feedback gain.
    void setSize         (float sizeScale);
    void setFreeze       (bool frozen);
    // Global LFO phase offset applied on top of each channel's per-channel
    // phase. Use to break periodicity between the three SubTanks in
    // HallReverb (e.g. 0, π/3, 2π/3 across bands).
    void setLFOPhaseOffset (float radians);

private:
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float  kTwoPi          = 6.283185307179586f;
    // 1/sqrt(8) Hadamard normalization, applied once per sample inside the
    // mixing kernel so the FDN matrix is unitary.
    static constexpr float  kHadamardNorm   = 0.353553390593274f;
    // Soft-clip ceiling — catches numerical blow-up under freeze / extreme
    // mod-depth. Same value FDNReverb uses.
    static constexpr float  kSafetyClip     = 8.0f;

    struct Delay
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask     = 0;
    };

    // Per-channel state (RT-side; written each sample, never the snapshot).
    Delay delays_       [N] {};
    float dampState_    [N] {};        // one-pole shelf z^-1 per channel
    float lfoPhase_     [N] {};        // current phase in [0, 2π)
    float lfoPhaseInc_  [N] {};        // 2π × rate / sr

    // Per-channel feedback gain (derived from decay × loop length).
    float feedbackGain_ [N] {};

    // Scaled delay lengths in samples (base × sr-ratio × sizeScale, rounded).
    int   scaledDelay_  [N] {};

    // Configuration (message-thread); recompute called from setters.
    double sampleRate_       = 44100.0;
    int    baseDelays_  [N]  {};
    float  sizeScale_        = 1.0f;
    float  decayTime_        = 1.5f;
    float  dampingCoeff_     = 0.0f;     // 0 = bypass; one-pole shelf coeff
    float  modDepthSamples_  = 0.0f;
    float  modRateHz_        = 1.0f;
    float  bandPhaseOffset_  = 0.0f;
    bool   frozen_           = false;
    bool   prepared_         = false;

    // Output tap routing — alternating signs decorrelate L/R from the same
    // set of channels. Indexes chosen so left and right read disjoint
    // channel subsets (no shared mode visible in either output).
    static constexpr int   kLeftTaps  [kNumOutputTaps] = { 0, 2, 4, 6 };
    static constexpr int   kRightTaps [kNumOutputTaps] = { 1, 3, 5, 7 };
    static constexpr float kLeftSigns [kNumOutputTaps] = { +1.0f, -1.0f, +1.0f, -1.0f };
    static constexpr float kRightSigns[kNumOutputTaps] = { -1.0f, +1.0f, -1.0f, +1.0f };

    void recomputeDelayLengths();
    void recomputeFeedbackGains();
    void recomputeLFORates();
};

} // namespace duskverb::dsp

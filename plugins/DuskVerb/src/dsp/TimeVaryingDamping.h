#pragma once

// =====================================================================
// PHASE 3 SANDBOX — NOT YET WIRED INTO FDN OR QUADTANK.
//
// Status: 2026-05-28 — drafted, isolated, awaiting V1 listening-test
// approval before integration. Do NOT #include this from FDNReverb.cpp
// or QuadTank.cpp until V1 baseline is signed off audibly. While this
// file sits unincluded, the existing shipped binary remains bit-identical
// to the calibrated V1 master at commit 8a36e88.
//
// ---------------------------------------------------------------------
// TimeVaryingDamping
// ---------------------------------------------------------------------
//
// Per-line energy-following high-shelf with coefficient lerping between
// pre-computed "early" (transient / bright) and "late" (decayed / dark)
// RBJ biquad coefficient sets. Replaces static ThreeBandDamping's fixed
// high-band attenuation with a program-dependent dynamic tilt that
// matches the physical behavior of premium hardware reverbs.
//
// DESIGN DECISIONS (validated 2026-05-28 in architectural review):
//
// 1. ENERGY-FOLLOWING (Option B), NOT WALL-CLOCK COUNTER
//    Wall-clock timer with triggerEnvelope() resets would snap the
//    timeline of all circulating tail energy on every new transient.
//    Instead, each delay line carries its own single-pole envelope of
//    its own circulating energy. New input rises the envelope fast,
//    natural decay relaxes it back toward zero, no reset needed.
//
// 2. PER-LINE INDEPENDENT TRACKERS (16 channels max)
//    Each of the 16 FDN delay lines tracks its OWN energy. Short lines
//    react to new input quickly (front-of-room reflections); long lines
//    receive energy delayed via the mix matrix, brighten later, stay
//    dark longer. Matches physical room behavior. Per-sample cost: 1×
//    abs + 1× compare + 1× multiply per channel — negligible.
//
// 3. ENVELOPE TRACKS POST-MATRIX, PRE-DAMPING INPUT
//    Tracking the filter's OWN output would create an unstable feedback
//    loop in the estimator (envelope chases filter chases envelope).
//    Tracking the matrix output BEFORE this filter modifies it
//    decouples the estimator from its own feedback path.
//
// 4. COEFFICIENT LERP, NOT GAIN RE-DESIGN
//    earlyCoeffs and lateCoeffs are computed once at preset-apply time
//    via full RBJ cookbook (with sin/cos/sqrt). Per-sample processing
//    linearly interpolates the raw b0/b1/b2/a1/a2 values directly.
//    Zero transcendental math in the inner loop. Zipper-free transitions.
//
// 5. BIT-IDENTICAL FALLBACK
//    When earlyMultiplier == lateMultiplier, the lerp collapses to a
//    static assignment of earlyCoeffs. The transposed-Direct-Form-II
//    (TDF-II) biquad behavior is then equivalent to a single static RBJ
//    high-shelf (same transfer function) — preserves the
//    V1 baseline exactly for any preset that doesn't opt into dynamic
//    damping. isBitIdentical() exposes the detection for unit tests.
//
// 6. TIME-CONSTANT STABILITY MARGIN
//    Envelope release defaults to 1 s. For typical FDN longest delay
//    lines ~250 ms, this is 4× the slowest circulation period — keeps
//    the envelope follower from chasing its own tail at the FDN's
//    natural recirculation rate. Tunable per preset.
//
// Allocation-free. Hot path is per-sample, per-line: 1 fabs, 1 select,
// 1 multiply (envelope), 1 div+min (t), 5 lerps (coeffs), TDF-II biquad.
// The biquad (see process()) is implemented as transposed Direct-Form II
// (2 state accumulators z1/z2, structure y=b0·x+z1; z1=b1·x−a1·y+z2;
// z2=b2·x−a2·y) — intentional: TDF-II is the standard choice for
// time-varying coefficients (per-sample-lerped here) because its state is
// the filter output history, so coefficient changes don't inject transients
// the way a Direct-Form-I delay-line would.
// =====================================================================

#include <algorithm>
#include <cmath>

namespace DspUtils {

class TimeVaryingDamping
{
public:
    static constexpr int   kMaxChannels = 16;
    static constexpr float kTwoPi       = 6.283185307179586f;
    static constexpr float kSqrt12      = 0.7071067811865475f;

    // Per-band damping config. Set at preset-apply time; designCoeffs()
    // bakes the early/late RBJ shelf math, then per-sample processing
    // interpolates between them.
    //
    // Multiplier convention matches existing ThreeBandDamping:
    //   1.0  = flat (no shelving)
    //   0.5  = -6 dB attenuation above corner
    //   2.0  = +6 dB boost above corner
    struct BandSettings
    {
        float earlyMultiplier = 1.0f;   // gain above corner when energy is fresh
        float lateMultiplier  = 1.0f;   // gain above corner when energy has decayed
        float crossoverHz     = 6000.0f;
    };

    struct ShelfCoeffs
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
    };

    // Per-channel runtime state. POD; allocation-free.
    struct ChannelState
    {
        float envelope = 0.0f;        // single-pole follower of |matrixOut|
        float z1       = 0.0f;        // transposed Direct-Form-II (TDF-II) accumulators
        float z2       = 0.0f;        //   (see process(): output-history state, not DF1 delays)
    };

    // ---- Lifecycle ----

    void prepare (float sampleRate, int numChannels = kMaxChannels)
    {
        sampleRate_  = std::max (sampleRate, 8000.0f);
        numChannels_ = std::clamp (numChannels, 1, kMaxChannels);
        updateEnvelopeCoeff();
        reset();
    }

    void reset()
    {
        for (int i = 0; i < kMaxChannels; ++i)
            channels_[i] = ChannelState{};
    }

    // ---- Tuning ----

    // Envelope follower release time (seconds). Default 1 s — well above
    // typical FDN longest-line recirculation period (~250 ms) so the
    // estimator doesn't chase its own tail. Tune shorter for snappy
    // chamber-style early/late transitions, longer for slow hall morphs.
    void setEnvelopeReleaseSec (float seconds)
    {
        envelopeReleaseSec_ = std::max (seconds, 0.05f);
        updateEnvelopeCoeff();
    }

    // Input level at which the interpolation factor saturates to t = 1.0
    // (fully "early" / bright state). Default 1.0 assumes the matrix
    // output is peak-normalized; tank-internal signals may need lower.
    void setRefLevel (float refLevel)
    {
        refLevel_ = std::max (refLevel, 1.0e-6f);
    }

    // Pre-compute the early + late RBJ high-shelf coefficient sets once.
    // After this call, per-sample processing is transcendental-free.
    // Detects bit-identity fallback automatically.
    void designCoeffs (const BandSettings& bs)
    {
        earlyCoeffs_ = designHighShelfRBJ (bs.earlyMultiplier, bs.crossoverHz);
        lateCoeffs_  = designHighShelfRBJ (bs.lateMultiplier,  bs.crossoverHz);
        bitIdenticalFallback_ =
            (std::fabs (bs.earlyMultiplier - bs.lateMultiplier) < 1.0e-7f);
    }

    // ---- Audio thread (per-sample, per-channel) ----

    // matrixOut: the post-feedback-matrix delay-line value, BEFORE damping.
    // This is the "live circulating feedback energy" each line carries.
    // Returns the damped sample for that line.
    //
    // ZERO heap, ZERO transcendentals, ZERO branching beyond the bit-
    // identity early-out. Direct-form-I biquad path is the hot loop.
    inline float process (int ch, float matrixOut)
    {
        if (ch < 0 || ch >= numChannels_)
            return matrixOut;

        auto& st = channels_[ch];

        // 1) Per-line peak-detect envelope.
        //    Instant attack (rises with input), single-pole release toward 0.
        const float absVal = std::fabs (matrixOut);
        st.envelope = (absVal > st.envelope)
                    ? absVal
                    : st.envelope * envCoeff_;

        // 2) Interpolation factor in [0, 1].
        //    t = 1 → bright/early (energy is fresh)
        //    t = 0 → dark/late   (energy has decayed)
        const float t = std::min (st.envelope / refLevel_, 1.0f);

        // 3) Coefficient blend (bit-identity bypass when set up flat).
        ShelfCoeffs c;
        if (bitIdenticalFallback_)
        {
            c = earlyCoeffs_;   // == lateCoeffs_
        }
        else
        {
            const float oneMinusT = 1.0f - t;
            c.b0 = oneMinusT * lateCoeffs_.b0 + t * earlyCoeffs_.b0;
            c.b1 = oneMinusT * lateCoeffs_.b1 + t * earlyCoeffs_.b1;
            c.b2 = oneMinusT * lateCoeffs_.b2 + t * earlyCoeffs_.b2;
            c.a1 = oneMinusT * lateCoeffs_.a1 + t * earlyCoeffs_.a1;
            c.a2 = oneMinusT * lateCoeffs_.a2 + t * earlyCoeffs_.a2;
        }

        // 4) Transposed Direct-Form II (TDF-II) biquad — 2 state variables,
        //    output-first form, preferred for time-varying coefficients.
        const float y = c.b0 * matrixOut + st.z1;
        st.z1 = c.b1 * matrixOut - c.a1 * y + st.z2;
        st.z2 = c.b2 * matrixOut - c.a2 * y;
        return y;
    }

    // ---- Introspection ----

    bool isBitIdentical() const noexcept { return bitIdenticalFallback_; }
    int  numChannels()    const noexcept { return numChannels_; }
    float envelopeAt(int ch) const noexcept
    {
        return (ch >= 0 && ch < kMaxChannels) ? channels_[ch].envelope : 0.0f;
    }

    const ShelfCoeffs& earlyCoeffs() const noexcept { return earlyCoeffs_; }
    const ShelfCoeffs& lateCoeffs()  const noexcept { return lateCoeffs_; }

private:
    void updateEnvelopeCoeff()
    {
        envCoeff_ = std::exp (-1.0f / (envelopeReleaseSec_ * sampleRate_));
    }

    // RBJ cookbook 2nd-order high-shelf. Pure offline math; called only
    // during designCoeffs() at preset-apply time. NEVER called per-sample.
    ShelfCoeffs designHighShelfRBJ (float gainMultiplier, float fcHz) const
    {
        const float gainLin = std::max (gainMultiplier, 1.0e-6f);
        // RBJ A = sqrt(gainLin); shelf gain at high frequencies = A^2.
        const float A     = std::sqrt (gainLin);
        const float sqrtA = std::sqrt (A);
        const float fcCl  = std::min (fcHz, 0.49f * sampleRate_);
        const float w0    = kTwoPi * fcCl / sampleRate_;
        const float cosw  = std::cos (w0);
        const float sinw  = std::sin (w0);
        const float alpha = sinw / (2.0f * kSqrt12);
        const float a0    = (A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqrtA * alpha;

        ShelfCoeffs c;
        c.b0 =  A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqrtA * alpha) / a0;
        c.b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw)                 / a0;
        c.b2 =  A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
        c.a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)                      / a0;
        c.a2 =        ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
        return c;
    }

    // ---- State ----

    float sampleRate_   = 44100.0f;
    int   numChannels_  = kMaxChannels;

    ChannelState channels_[kMaxChannels]{};

    ShelfCoeffs earlyCoeffs_{};
    ShelfCoeffs lateCoeffs_{};
    bool        bitIdenticalFallback_ = true;   // safe default until designCoeffs runs

    // Envelope-follower tuning.
    float envelopeReleaseSec_ = 1.0f;
    float envCoeff_           = 0.999979f;   // ~1 s release @ 48 kHz
    float refLevel_           = 1.0f;
};

} // namespace DspUtils

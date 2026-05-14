#pragma once

#include <algorithm>
#include <cmath>

// =====================================================================
// ShelfBiquad — internal helper. RBJ-cookbook 2nd-order shelving filter
// (low-shelf or high-shelf at Q = 1/√2 for Butterworth-aligned response).
//
// Used by TwoBandDamping and ThreeBandDamping below. Direct-form-1 with
// scalar mono state. Allocation-free.
// =====================================================================
struct ShelfBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    static constexpr float kTwoPi  = 6.283185307179586f;
    static constexpr float kSqrt12 = 0.7071067811865475f;  // 1/√2

    void designHighShelf (float gainLin, float fcHz, float sampleRate)
    {
        const float A     = std::sqrt (std::max (gainLin, 1.0e-12f));
        const float sqrtA = std::sqrt (A);
        const float w0    = kTwoPi * std::min (fcHz, 0.49f * sampleRate) / sampleRate;
        const float cosw  = std::cos (w0);
        const float sinw  = std::sin (w0);
        const float alpha = sinw / (2.0f * kSqrt12);
        const float a0    = (A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqrtA * alpha;
        b0 =  A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqrtA * alpha) / a0;
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw)                 / a0;
        b2 =  A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)                      / a0;
        a2 =        ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
    }

    void designLowShelf (float gainLin, float fcHz, float sampleRate)
    {
        const float A     = std::sqrt (std::max (gainLin, 1.0e-12f));
        const float sqrtA = std::sqrt (A);
        const float w0    = kTwoPi * std::min (fcHz, 0.49f * sampleRate) / sampleRate;
        const float cosw  = std::cos (w0);
        const float sinw  = std::sin (w0);
        const float alpha = sinw / (2.0f * kSqrt12);
        const float a0    = (A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqrtA * alpha;
        b0 =  A * ((A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqrtA * alpha) / a0;
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw)                 / a0;
        b2 =  A * ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw)                    / a0;
        a2 =         ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
    }

    float process (float x)
    {
        // Direct-form-1. State variables hold the delay-line samples.
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.0f; }
};

// =====================================================================
// TwoBandDamping — feedback-loop damping with a 2nd-order RBJ high-shelf.
//
// Replaces the previous 1st-order LP cross-mix (6 dB/oct skirt) with a
// proper 2nd-order shelving biquad (12 dB/oct skirt around the corner).
// The transition between bass and treble is sharper and more musical —
// the "air" frequencies shape with definition rather than a slow analog
// tilt. DC and Nyquist asymptotes match the previous class exactly so
// per-band RT60 calibration carries over.
//
// API preserved: setCoefficients(gLow, gHigh, crossoverCoeff).
// crossoverCoeff is the historical exp(-2π·fc/sr) value — fc is recovered
// from it so existing engine call-sites don't need to change. prepare(sr)
// MUST be called before setCoefficients so the biquad knows the host
// sample rate.
// =====================================================================
class TwoBandDamping
{
public:
    void prepare (float sampleRate)
    {
        sampleRate_ = sampleRate;
        reset();
    }

    void setCoefficients (float gLow, float gHigh, float crossoverCoeff)
    {
        broadbandGain_ = gLow;
        // Above the corner, the shelf scales the broadband gain by gHigh/gLow
        // so the frequency response asymptotes to gHigh — preserves the API
        // semantics of the old 1st-order shelf.
        const float coeff = std::clamp (crossoverCoeff, 1.0e-6f, 1.0f - 1.0e-6f);
        const float fc    = -std::log (coeff) * sampleRate_
                          * (1.0f / 6.283185307179586f);
        const float shelfGain = gHigh / std::max (gLow, 1.0e-12f);
        shelf_.designHighShelf (shelfGain, fc, sampleRate_);
    }

    float process (float input)
    {
        return broadbandGain_ * shelf_.process (input);
    }

    void reset() { shelf_.reset(); }

private:
    float       sampleRate_    = 44100.0f;
    float       broadbandGain_ = 1.0f;
    ShelfBiquad shelf_;
};

// =====================================================================
// ThreeBandDamping — feedback-loop damping with a low-shelf + high-shelf
// 2nd-order RBJ biquad cascade.
//
// Replaces the previous cascaded 1st-order LP splits. Mid frequencies pass
// at gMid (broadband multiplier); the low-shelf scales below the low corner
// to gLow, the high-shelf scales above the high corner to gHigh. The
// transitions are 12 dB/oct each — sharper, more musical "air" management
// than the old 6 dB/oct cross-mix.
//
// API preserved: setCoefficients(gLow, gMid, gHigh, lowCoeff, highCoeff).
// Both crossover coeffs are exp(-2π·fc/sr) values; fc is recovered for
// the biquad design. prepare(sr) MUST be called before setCoefficients.
// =====================================================================
class ThreeBandDamping
{
public:
    // POD coefficient block. Used by the snapshot path: filter state stays
    // inside the filter object; coefficients live in the snapshot and are
    // passed by const-ref to process() each sample. Zero-tear: the snapshot
    // pointer swap is atomic, so the RT thread never sees half-written
    // biquad coefficients.
    struct Coeffs
    {
        // Low-shelf biquad
        float lowB0 = 1.0f, lowB1 = 0.0f, lowB2 = 0.0f, lowA1 = 0.0f, lowA2 = 0.0f;
        // High-shelf biquad
        float highB0 = 1.0f, highB1 = 0.0f, highB2 = 0.0f, highA1 = 0.0f, highA2 = 0.0f;
        float broadbandGain = 1.0f;
    };

    // Pure-trig portion of the shelf design, depends only on the two
    // crossover frequencies + sample rate. Hoisted out of designCoeffs so a
    // caller iterating over many channels (e.g. FDNReverb's 16-line damping)
    // pays the cos/sin/sqrt cost once instead of 16×. Mirrors the RBJ
    // cookbook intermediate variables.
    struct Crossover
    {
        float lowCosw     = 1.0f;
        float lowAlpha    = 0.0f;
        float highCosw    = 1.0f;
        float highAlpha   = 0.0f;

        static Crossover from (float lowCrossoverCoeff,
                               float highCrossoverCoeff,
                               float sampleRate)
        {
            constexpr float kTwoPi   = 6.283185307179586f;
            constexpr float kInvTwoPi = 1.0f / kTwoPi;
            constexpr float kSqrt12  = 0.7071067811865475f;
            constexpr float kInv2Sq12 = 1.0f / (2.0f * kSqrt12);

            const float lowCoeff  = std::clamp (lowCrossoverCoeff,  1.0e-6f, 1.0f - 1.0e-6f);
            const float highCoeff = std::clamp (highCrossoverCoeff, 1.0e-6f, 1.0f - 1.0e-6f);
            const float lowFc  = -std::log (lowCoeff)  * sampleRate * kInvTwoPi;
            const float highFc = -std::log (highCoeff) * sampleRate * kInvTwoPi;
            const float lowW0  = kTwoPi * std::min (lowFc,  0.49f * sampleRate) / sampleRate;
            const float highW0 = kTwoPi * std::min (highFc, 0.49f * sampleRate) / sampleRate;

            Crossover x;
            x.lowCosw   = std::cos (lowW0);
            x.lowAlpha  = std::sin (lowW0)  * kInv2Sq12;
            x.highCosw  = std::cos (highW0);
            x.highAlpha = std::sin (highW0) * kInv2Sq12;
            return x;
        }
    };

    void prepare (float sampleRate)
    {
        sampleRate_ = sampleRate;
        reset();
    }

    // Legacy API: writes coefficients into the internal ShelfBiquad objects.
    // Kept for engines (Dattorro, QuadTank, SixAPTank) not yet migrated to
    // the snapshot path.
    void setCoefficients (float gLow, float gMid, float gHigh,
                          float lowCrossoverCoeff, float highCrossoverCoeff)
    {
        broadbandGain_ = gMid;
        const float lowCoeff  = std::clamp (lowCrossoverCoeff,  1.0e-6f, 1.0f - 1.0e-6f);
        const float highCoeff = std::clamp (highCrossoverCoeff, 1.0e-6f, 1.0f - 1.0e-6f);
        const float lowFc  = -std::log (lowCoeff)  * sampleRate_ * (1.0f / 6.283185307179586f);
        const float highFc = -std::log (highCoeff) * sampleRate_ * (1.0f / 6.283185307179586f);
        lastLowShelfGain_  = gLow  / std::max (gMid, 1.0e-12f);
        lastHighShelfGain_ = gHigh / std::max (gMid, 1.0e-12f);
        lastLowFcHz_       = lowFc;
        lastHighFcHz_      = highFc;
        lowShelf_ .designLowShelf  (lastLowShelfGain_,  lowFc,  sampleRate_);
        highShelf_.designHighShelf (lastHighShelfGain_, highFc, sampleRate_);
    }

    void setLowCrossoverCoeff (float coeff)
    {
        const float c = std::clamp (coeff, 1.0e-6f, 1.0f - 1.0e-6f);
        lastLowFcHz_ = -std::log (c) * sampleRate_ * (1.0f / 6.283185307179586f);
        lowShelf_.designLowShelf (lastLowShelfGain_, lastLowFcHz_, sampleRate_);
    }

    void setHighCrossoverCoeff (float coeff)
    {
        const float c = std::clamp (coeff, 1.0e-6f, 1.0f - 1.0e-6f);
        lastHighFcHz_ = -std::log (c) * sampleRate_ * (1.0f / 6.283185307179586f);
        highShelf_.designHighShelf (lastHighShelfGain_, lastHighFcHz_, sampleRate_);
    }

    // Snapshot-path coefficient design: pure function of inputs, returns a
    // Coeffs POD by value. Caller stores into LiveParams; no filter state
    // mutation. Mirrors the cascade order of the legacy path.
    static Coeffs designCoeffs (float gLow, float gMid, float gHigh,
                                float lowCrossoverCoeff, float highCrossoverCoeff,
                                float sampleRate)
    {
        return designCoeffs (gLow, gMid, gHigh,
                             Crossover::from (lowCrossoverCoeff, highCrossoverCoeff, sampleRate));
    }

    // Precomputed-trig overload: the cos/sin/alpha terms come from a
    // Crossover instance built once per call site. Per-channel cost is now
    // pure algebra + 4 sqrts; no transcendentals beyond what Crossover::from
    // already paid once.
    static Coeffs designCoeffs (float gLow, float gMid, float gHigh,
                                const Crossover& xover)
    {
        const float invMid = 1.0f / std::max (gMid, 1.0e-12f);

        Coeffs c;
        // Low-shelf (RBJ cookbook with prebaked cos/sin)
        {
            const float A     = std::sqrt (std::max (gLow * invMid, 1.0e-12f));
            const float sqrtA = std::sqrt (A);
            const float cosw  = xover.lowCosw;
            const float alpha = xover.lowAlpha;
            const float a0    = (A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqrtA * alpha;
            const float invA0 = 1.0f / a0;
            c.lowB0 =  A * ((A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqrtA * alpha) * invA0;
            c.lowB1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw)                  * invA0;
            c.lowB2 =  A * ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) * invA0;
            c.lowA1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw)                     * invA0;
            c.lowA2 =         ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) * invA0;
        }
        // High-shelf
        {
            const float A     = std::sqrt (std::max (gHigh * invMid, 1.0e-12f));
            const float sqrtA = std::sqrt (A);
            const float cosw  = xover.highCosw;
            const float alpha = xover.highAlpha;
            const float a0    = (A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqrtA * alpha;
            const float invA0 = 1.0f / a0;
            c.highB0 =  A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqrtA * alpha) * invA0;
            c.highB1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw)                 * invA0;
            c.highB2 =  A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) * invA0;
            c.highA1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)                      * invA0;
            c.highA2 =        ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) * invA0;
        }
        c.broadbandGain = gMid;
        return c;
    }

    // Legacy process: reads coefficients from internal biquads.
    float process (float input)
    {
        return broadbandGain_ * lowShelf_.process (highShelf_.process (input));
    }

    // Snapshot process: coefficients come by const-ref each sample.
    // Filter state (z1/z2 inside each shelf) stays on the RT side.
    // Cascade order matches the legacy path (high-shelf, then low-shelf).
    float process (float input, const Coeffs& c)
    {
        // High-shelf
        const float yHigh = c.highB0 * input + highShelf_.z1;
        highShelf_.z1 = c.highB1 * input - c.highA1 * yHigh + highShelf_.z2;
        highShelf_.z2 = c.highB2 * input - c.highA2 * yHigh;
        // Low-shelf
        const float yLow  = c.lowB0 * yHigh + lowShelf_.z1;
        lowShelf_.z1  = c.lowB1 * yHigh - c.lowA1 * yLow + lowShelf_.z2;
        lowShelf_.z2  = c.lowB2 * yHigh - c.lowA2 * yLow;
        return c.broadbandGain * yLow;
    }

    void reset()
    {
        lowShelf_.reset();
        highShelf_.reset();
    }

private:
    float       sampleRate_         = 44100.0f;
    float       broadbandGain_      = 1.0f;
    float       lastLowShelfGain_   = 1.0f;
    float       lastHighShelfGain_  = 1.0f;
    float       lastLowFcHz_        = 250.0f;
    float       lastHighFcHz_       = 4000.0f;
    ShelfBiquad lowShelf_;
    ShelfBiquad highShelf_;
};

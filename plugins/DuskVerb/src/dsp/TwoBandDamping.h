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
        const float lowCoeff  = std::clamp (lowCrossoverCoeff,  1.0e-6f, 1.0f - 1.0e-6f);
        const float highCoeff = std::clamp (highCrossoverCoeff, 1.0e-6f, 1.0f - 1.0e-6f);
        const float lowFc  = -std::log (lowCoeff)  * sampleRate * (1.0f / 6.283185307179586f);
        const float highFc = -std::log (highCoeff) * sampleRate * (1.0f / 6.283185307179586f);
        const float lowShelfGain  = gLow  / std::max (gMid, 1.0e-12f);
        const float highShelfGain = gHigh / std::max (gMid, 1.0e-12f);

        ShelfBiquad lowBq, highBq;
        lowBq .designLowShelf  (lowShelfGain,  lowFc,  sampleRate);
        highBq.designHighShelf (highShelfGain, highFc, sampleRate);

        Coeffs c;
        c.lowB0 = lowBq.b0;   c.lowB1 = lowBq.b1;   c.lowB2 = lowBq.b2;
        c.lowA1 = lowBq.a1;   c.lowA2 = lowBq.a2;
        c.highB0 = highBq.b0; c.highB1 = highBq.b1; c.highB2 = highBq.b2;
        c.highA1 = highBq.a1; c.highA2 = highBq.a2;
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

// =====================================================================
// FiveBandDamping — feedback-loop damping with FIVE decay plateaus
// (sub | low-mid | mid | hi-mid | air) via a cascade of four 2nd-order
// RBJ shelving biquads. Gives the FDN the per-band T60-shaping freedom the
// 3-band lacked: 1k / 2k / 4k / 8k can decay independently to track a
// vintage plate's steep HF rolloff, and the sub band decouples from the
// low-mids. Mid passes at the broadband gain; each shelf sets one band's
// asymptote RELATIVE TO ITS NEIGHBOUR (adjacent-ratio form) so the
// plateaus compose without overlap-multiplication:
//
//   <Xsub : gSub | Xsub..Xlow : gLoMid | Xlow..Xhigh : gMid
//                | Xhigh..Xair : gHiMid | >Xair : gAir
//
//     LowShelf (Xsub)  ratio = gSub   / gLoMid
//     LowShelf (Xlow)  ratio = gLoMid / gMid
//     HighShelf(Xhigh) ratio = gHiMid / gMid
//     HighShelf(Xair)  ratio = gAir   / gHiMid
//     broadband        = gMid
//
// Cascade order: LowShelf(Xsub) -> LowShelf(Xlow) -> HighShelf(Xhigh)
//              -> HighShelf(Xair) -> * broadband.
//
// Transparent fallback (gSub=gLow, gLoMid=gHiMid=gMid, gAir=gHigh,
// Xsub=lowX, Xair=highX) makes the two middle shelves identity (ratio 1.0
// → b1=a1, b2=a2 → H(z)≡1, state stays 0 → bit-exact passthrough) and
// reduces to the legacy ThreeBandDamping pair (low-shelf @ lowX gLow/gMid,
// high-shelf @ highX gHigh/gMid). Existing presets are behaviourally
// unchanged — identical to FP epsilon (the active shelves run low→high
// here vs high→low in ThreeBandDamping; LTI-equivalent, ~-140 dB residual).
//
// Snapshot path only (FDN). Coeffs POD passed by const-ref each sample;
// biquad state (z1/z2) stays RT-side. Allocation-free.
// =====================================================================
class FiveBandDamping
{
public:
    struct Coeffs
    {
        float subB0 = 1.0f, subB1 = 0.0f, subB2 = 0.0f, subA1 = 0.0f, subA2 = 0.0f;
        float lowB0 = 1.0f, lowB1 = 0.0f, lowB2 = 0.0f, lowA1 = 0.0f, lowA2 = 0.0f;
        float hiB0  = 1.0f, hiB1  = 0.0f, hiB2  = 0.0f, hiA1  = 0.0f, hiA2  = 0.0f;
        float airB0 = 1.0f, airB1 = 0.0f, airB2 = 0.0f, airA1 = 0.0f, airA2 = 0.0f;
        float broadbandGain = 1.0f;
    };

    void prepare (float sampleRate) { sampleRate_ = sampleRate; reset(); }
    void reset() { subS_.reset(); lowS_.reset(); hiS_.reset(); airS_.reset(); }

    // Pure design function. gMid is the broadband (mid-band) gain; the other
    // four gains are absolute band targets, converted to adjacent ratios so
    // the cascade yields independent plateaus. Crossover args are the
    // historical exp(-2π·fc/sr) coefficients; fc recovered for the design.
    static Coeffs designCoeffs (float gSub, float gLoMid, float gMid,
                                float gHiMid, float gAir,
                                float subCoeff, float lowCoeff,
                                float highCoeff, float airCoeff,
                                float sampleRate)
    {
        auto fc = [sampleRate] (float coeff)
        {
            const float c = std::clamp (coeff, 1.0e-6f, 1.0f - 1.0e-6f);
            return -std::log (c) * sampleRate * (1.0f / 6.283185307179586f);
        };
        const float gMidSafe   = std::max (gMid,   1.0e-12f);
        const float gLoMidSafe = std::max (gLoMid, 1.0e-12f);
        const float gHiMidSafe = std::max (gHiMid, 1.0e-12f);

        ShelfBiquad subBq, lowBq, hiBq, airBq;
        subBq.designLowShelf  (gSub   / gLoMidSafe, fc (subCoeff),  sampleRate);
        lowBq.designLowShelf  (gLoMid / gMidSafe,   fc (lowCoeff),  sampleRate);
        hiBq .designHighShelf (gHiMid / gMidSafe,   fc (highCoeff), sampleRate);
        airBq.designHighShelf (gAir   / gHiMidSafe, fc (airCoeff),  sampleRate);

        Coeffs c;
        c.subB0 = subBq.b0; c.subB1 = subBq.b1; c.subB2 = subBq.b2; c.subA1 = subBq.a1; c.subA2 = subBq.a2;
        c.lowB0 = lowBq.b0; c.lowB1 = lowBq.b1; c.lowB2 = lowBq.b2; c.lowA1 = lowBq.a1; c.lowA2 = lowBq.a2;
        c.hiB0  = hiBq.b0;  c.hiB1  = hiBq.b1;  c.hiB2  = hiBq.b2;  c.hiA1  = hiBq.a1;  c.hiA2  = hiBq.a2;
        c.airB0 = airBq.b0; c.airB1 = airBq.b1; c.airB2 = airBq.b2; c.airA1 = airBq.a1; c.airA2 = airBq.a2;
        c.broadbandGain = gMid;
        return c;
    }

    // Snapshot process: four DF1 shelving biquads in series, then broadband.
    float process (float input, const Coeffs& c)
    {
        // LowShelf (Xsub)
        float y = c.subB0 * input + subS_.z1;
        subS_.z1 = c.subB1 * input - c.subA1 * y + subS_.z2;
        subS_.z2 = c.subB2 * input - c.subA2 * y;
        // LowShelf (Xlow)
        float x = y;
        y = c.lowB0 * x + lowS_.z1;
        lowS_.z1 = c.lowB1 * x - c.lowA1 * y + lowS_.z2;
        lowS_.z2 = c.lowB2 * x - c.lowA2 * y;
        // HighShelf (Xhigh)
        x = y;
        y = c.hiB0 * x + hiS_.z1;
        hiS_.z1 = c.hiB1 * x - c.hiA1 * y + hiS_.z2;
        hiS_.z2 = c.hiB2 * x - c.hiA2 * y;
        // HighShelf (Xair)
        x = y;
        y = c.airB0 * x + airS_.z1;
        airS_.z1 = c.airB1 * x - c.airA1 * y + airS_.z2;
        airS_.z2 = c.airB2 * x - c.airA2 * y;
        return c.broadbandGain * y;
    }

private:
    float       sampleRate_ = 44100.0f;
    ShelfBiquad subS_, lowS_, hiS_, airS_;   // hold z1/z2 state only
};

// =====================================================================
// OctaveBandDamping — feedback-loop damping with NINE decay plateaus, one
// per ISO octave centre (63 | 125 | 250 | 500 | 1k | 2k | 4k | 8k | 16k Hz).
// The Jot/Schlecht "accurate reverberation-time control": a per-OCTAVE
// proportional GEQ inside the FDN loop so each octave's T60 is set
// independently — the 9-vs-5 coupling wall the FiveBandDamping cannot pass
// (5 bands can't satisfy 9 octave T60 gates without adjacent octaves
// dragging each other).
//
// Same adjacent-ratio shelf cascade as FiveBandDamping, generalised: with
// the 1 kHz band (index 4) as the broadband mid, the four octaves below it
// are set by four low-shelves at the inter-octave crossovers and the four
// above by four high-shelves. Each shelf carries the ADJACENT ratio
// g[k]/g[k+1] (low side) or g[k+1]/g[k] (high side) so the plateaus compose
// without overlap-multiplication — identical algebra to FiveBandDamping,
// verified there:
//
//   low  shelves k=0..3 : lowShelf  @ X[k], ratio g[k]   / g[k+1]
//   high shelves k=4..7 : highShelf @ X[k], ratio g[k+1] / g[k]
//   broadband           = g[4]
//
// g[k] are ABSOLUTE per-round-trip loop gains at octave k (10^(-3·L/(T60_k·sr))),
// so this filter alone carries the FULL per-octave decay — the FiveBandDamping
// stage is flattened to identity when AccurateHall runs the octave GEQ.
//
// Snapshot path only (FDN/AccurateHall). Coeffs POD by const-ref each sample;
// biquad state stays RT-side. Allocation-free. EIGHT DF1 biquads per line.
// =====================================================================
class OctaveBandDamping
{
public:
    static constexpr int kNumBands   = 9;
    static constexpr int kNumShelves = 8;
    static constexpr int kMidBand    = 4;   // 1 kHz = broadband

    struct Coeffs
    {
        // [shelf][b0,b1,b2,a1,a2]
        float b0[kNumShelves]; float b1[kNumShelves]; float b2[kNumShelves];
        float a1[kNumShelves]; float a2[kNumShelves];
        float broadbandGain = 1.0f;
        Coeffs()
        {
            for (int s = 0; s < kNumShelves; ++s)
            { b0[s] = 1.0f; b1[s] = b2[s] = a1[s] = a2[s] = 0.0f; }
        }
    };

    void prepare (float sampleRate) { sampleRate_ = sampleRate; reset(); }
    void reset() { for (auto& s : shelf_) s.reset(); }

    // gBand[0..8] = absolute per-round-trip loop gain at each octave centre.
    // xoverHz[0..7] = inter-octave crossover frequencies (geometric means of
    // adjacent centres = the full_check T60-gate band edges).
    static Coeffs designCoeffs (const float* gBand, const float* xoverHz,
                                float sampleRate)
    {
        Coeffs c;
        ShelfBiquad bq;
        // Low side: octaves 0..3 below the 1 kHz mid.
        for (int k = 0; k < kMidBand; ++k)
        {
            const float ratio = gBand[k] / std::max (gBand[k + 1], 1.0e-12f);
            bq.designLowShelf (ratio, xoverHz[k], sampleRate);
            c.b0[k] = bq.b0; c.b1[k] = bq.b1; c.b2[k] = bq.b2; c.a1[k] = bq.a1; c.a2[k] = bq.a2;
        }
        // High side: octaves 5..8 above the mid.
        for (int k = kMidBand; k < kNumShelves; ++k)
        {
            const float ratio = gBand[k + 1] / std::max (gBand[k], 1.0e-12f);
            bq.designHighShelf (ratio, xoverHz[k], sampleRate);
            c.b0[k] = bq.b0; c.b1[k] = bq.b1; c.b2[k] = bq.b2; c.a1[k] = bq.a1; c.a2[k] = bq.a2;
        }
        c.broadbandGain = gBand[kMidBand];
        return c;
    }

    // Eight DF1 shelving biquads in series, then broadband gain.
    float process (float input, const Coeffs& c)
    {
        float x = input;
        for (int s = 0; s < kNumShelves; ++s)
        {
            const float y = c.b0[s] * x + shelf_[s].z1;
            shelf_[s].z1 = c.b1[s] * x - c.a1[s] * y + shelf_[s].z2;
            shelf_[s].z2 = c.b2[s] * x - c.a2[s] * y;
            x = y;
        }
        return c.broadbandGain * x;
    }

private:
    float       sampleRate_ = 44100.0f;
    ShelfBiquad shelf_[kNumShelves];   // z1/z2 state only
};

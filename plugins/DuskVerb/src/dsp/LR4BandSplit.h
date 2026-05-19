#pragma once

#include <algorithm>
#include <cmath>

namespace duskverb::dsp
{

// Linkwitz-Riley 24 dB/oct 3-band split (LP cascade for bass, HP cascade for
// treble, BP = LP(fHigh) − bass for mid).
//
// Mid is computed as an explicit LP(fHigh) minus the bass output rather than
// (x − bass − treble), which avoids the phase-ripple leakage that bled bass
// into mid-band measurements in earlier implementations.
//
// Shared DSP primitive — used by FoilPlateEngine and the Hall family
// (HallReverb, ConcertHallReverb, RandomHallReverb) to pre-split input into
// independent per-band reverberators.
struct LR4BandSplit
{
    static constexpr float kTwoPi      = 6.283185307179586f;
    static constexpr float kSqrt12_LR4 = 0.7071067811865475f;

    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void designLP (float fcHz, float sr)
        {
            const float w0    = kTwoPi * std::min (fcHz, 0.49f * sr) / sr;
            const float cosw  = std::cos (w0);
            const float sinw  = std::sin (w0);
            const float alpha = sinw / (2.0f * kSqrt12_LR4);
            const float a0    = 1.0f + alpha;
            b0 = (1.0f - cosw) * 0.5f / a0;
            b1 = (1.0f - cosw)        / a0;
            b2 = (1.0f - cosw) * 0.5f / a0;
            a1 = -2.0f * cosw         / a0;
            a2 = (1.0f - alpha)       / a0;
        }

        void designHP (float fcHz, float sr)
        {
            const float w0    = kTwoPi * std::min (fcHz, 0.49f * sr) / sr;
            const float cosw  = std::cos (w0);
            const float sinw  = std::sin (w0);
            const float alpha = sinw / (2.0f * kSqrt12_LR4);
            const float a0    = 1.0f + alpha;
            b0 =  (1.0f + cosw) * 0.5f / a0;
            b1 = -(1.0f + cosw)        / a0;
            b2 =  (1.0f + cosw) * 0.5f / a0;
            a1 = -2.0f * cosw          / a0;
            a2 = (1.0f - alpha)        / a0;
        }

        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() { z1 = z2 = 0.0f; }
    };

    Biquad lpA,   lpB;            // bass band: LP(fLow) cascaded
    Biquad hpA,   hpB;            // treble band: HP(fHigh) cascaded
    Biquad midLpA, midLpB;        // mid band: LP(fHigh) cascaded (then minus bass)

    void prepare (float /*sr*/) { reset(); }

    void setCrossovers (float fLowHz, float fHighHz, float sr)
    {
        const float fLowClamped  = std::clamp (fLowHz, 20.0f, 0.45f * sr);
        const float fHighClamped = std::max (fHighHz, fLowClamped + 10.0f);
        lpA    .designLP (fLowClamped,  sr);
        lpB    .designLP (fLowClamped,  sr);
        hpA    .designHP (fHighClamped, sr);
        hpB    .designHP (fHighClamped, sr);
        midLpA .designLP (fHighClamped, sr);
        midLpB .designLP (fHighClamped, sr);
    }

    void reset()
    {
        lpA.reset();    lpB.reset();
        hpA.reset();    hpB.reset();
        midLpA.reset(); midLpB.reset();
    }

    void split (float x, float& outBass, float& outMid, float& outTreble)
    {
        outBass   = lpB.process    (lpA.process    (x));
        outTreble = hpB.process    (hpA.process    (x));
        const float lowAndMid = midLpB.process (midLpA.process (x));
        outMid    = lowAndMid - outBass;
    }
};

} // namespace duskverb::dsp

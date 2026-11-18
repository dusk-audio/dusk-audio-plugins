// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// StereoBands: DuskVerb 2's per-band stereo width on the summed wet. The side signal is split at
// 300 Hz and 5 kHz by Linkwitz-Riley 4 crossovers (the low band phase-aligned through the 5 kHz
// pair, so the three bands sum to an allpassed side) and each band's side is multiplied by its
// row's gain; the mid is untouched, so the mono sum is too. The gains act on top of the side the
// engine's Width control made (the engine patch then puts the engine's own width bands at unity).
//
// Why not the engine's own width bands: they split the side with a complementary one-pole pair at
// the same frequencies (DuskVerbEngine's effSide block), which carries a band's gain an octave and
// more into its neighbour. Live Room's side x3 below 300 Hz then drags 300 Hz to 1.6 kHz anti-phase
// (Phase C0.3); with these crossovers the lows keep x3 while the mids sit where the reference does.
//
// A preset without a row leaves the wet exactly as it is: process() does nothing while no gains
// play and no fade runs. A swap crossfades linearly over the preset fade, between the outgoing
// gains (or the untouched wet) and the incoming ones, as the octave trim does.
//
// Real time: prepare() designs the filters; everything else is audio-thread safe.

#pragma once

#include "../../shared-daf/dsp/DuskFilters.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace duskverb
{

class StereoBands
{
public:
    // Side gains below 300 Hz, 300 Hz to 5 kHz and above 5 kHz; any below zero: no row (off).
    using Gains = std::array<float, 3>;
    static constexpr float kLowHz = 300.0f, kHighHz = 5000.0f;
    static constexpr float kMaxGain = 4.0f;
    static constexpr Gains kOff { -1.0f, -1.0f, -1.0f };

    static constexpr bool valid (const Gains& g) noexcept
    {
        const bool off = g[0] < 0.0f || g[1] < 0.0f || g[2] < 0.0f;
        if (off)
            return g[0] < 0.0f && g[1] < 0.0f && g[2] < 0.0f;
        return g[0] <= kMaxGain && g[1] <= kMaxGain && g[2] <= kMaxGain;
    }
    static constexpr bool playing (const Gains& g) noexcept { return g[0] >= 0.0f && g[1] >= 0.0f && g[2] >= 0.0f; }

    void prepare (double sampleRate) noexcept
    {
        const auto lp = [sampleRate] (double hz) { return butterworth2 (sampleRate, hz, false); };
        const auto hp = [sampleRate] (double hz) { return butterworth2 (sampleRate, hz, true); };
        for (int s = 0; s < 2; ++s)
        {
            lowLp_[s].setCoeffs (lp (kLowHz));
            lowHp_[s].setCoeffs (hp (kLowHz));
            highLp_[s].setCoeffs (lp (kHighHz));
            highHp_[s].setCoeffs (hp (kHighHz));
            alignLp_[s].setCoeffs (lp (kHighHz));
            alignHp_[s].setCoeffs (hp (kHighHz));
        }
        clear();
    }

    // The filters and any fade start over; the gains playing stay.
    void clear() noexcept
    {
        for (int s = 0; s < 2; ++s)
            for (auto* f : { &lowLp_[s], &lowHp_[s], &highLp_[s], &highHp_[s], &alignLp_[s], &alignHp_[s] })
                f->reset();
        fadeDone_ = fadeTotal_ = 0;
        outgoing_ = current_;
    }

    void swapTo (const Gains& gains, int fadeSamples) noexcept
    {
        if (gains == current_)
            return;
        if (! playing (current_) && ! (fadeDone_ < fadeTotal_))
            for (int s = 0; s < 2; ++s)       // waking from off: the filters start from silence
                for (auto* f : { &lowLp_[s], &lowHp_[s], &highLp_[s], &highHp_[s], &alignLp_[s], &alignHp_[s] })
                    f->reset();
        outgoing_ = current_;
        current_ = gains;
        fadeTotal_ = std::max (0, fadeSamples);
        fadeDone_ = 0;
    }

    const Gains& gains() const noexcept { return current_; }
    bool active() const noexcept { return playing (current_) || fadeDone_ < fadeTotal_; }

    // In place on the wet.
    void process (float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0 || ! active())
            return;
        const float invSpan = 1.0f / static_cast<float> (std::max (1, fadeTotal_ - 1));
        for (int i = 0; i < numSamples; ++i)
        {
            if (! playing (current_) && fadeDone_ >= fadeTotal_)
                return;                        // faded out to no row: the rest is left exactly as it is
            const float mid  = 0.5f * (left[i] + right[i]);
            const float side = 0.5f * (left[i] - right[i]);
            float low  = run2 (lowLp_, side);
            float rest = run2 (lowHp_, side);
            const float midBand  = run2 (highLp_, rest);
            const float highBand = run2 (highHp_, rest);
            low = run2 (alignLp_, low) + run2 (alignHp_, low);
            const float in = shaped (current_, side, low, midBand, highBand);
            float out = in;
            if (fadeDone_ < fadeTotal_)
            {
                const float t = std::min (1.0f, static_cast<float> (fadeDone_) * invSpan);
                const float old = shaped (outgoing_, side, low, midBand, highBand);
                out = old + t * (in - old);
                ++fadeDone_;
            }
            left[i]  = mid + out;
            right[i] = mid - out;
        }
    }

private:
    // A row's side, or the untouched one for no row.
    static float shaped (const Gains& g, float side, float low, float midBand, float highBand) noexcept
    {
        return playing (g) ? g[0] * low + g[1] * midBand + g[2] * highBand : side;
    }

    // Two cascaded Butterworth sections: one Linkwitz-Riley 4 branch.
    static float run2 (duskaudio::DoubleBiquad (&f)[2], float x) noexcept { return f[1].process (f[0].process (x)); }

    // RBJ second-order Butterworth (Q = 1/sqrt 2) low- or high-pass, in double.
    static duskaudio::BiquadCoeffsDouble butterworth2 (double fs, double hz, bool highPass) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * std::min (hz, duskaudio::kMaxDesignFreqRatio * fs) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70710678118654752);
        const double a0 = 1.0 + alpha;
        duskaudio::BiquadCoeffsDouble c;
        c.b1 = (highPass ? -(1.0 + cw) : (1.0 - cw)) / a0;
        c.b0 = c.b2 = (highPass ? (1.0 + cw) : (1.0 - cw)) * 0.5 / a0;
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    duskaudio::DoubleBiquad lowLp_[2], lowHp_[2], highLp_[2], highHp_[2], alignLp_[2], alignHp_[2];
    Gains current_ = kOff, outgoing_ = kOff;
    int fadeTotal_ = 0, fadeDone_ = 0;
};

} // namespace duskverb

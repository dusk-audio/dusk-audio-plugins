// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// BandLimit: DuskVerb 2's per-preset band limit on the summed wet, right after the octave trim:
// an RBJ peaking lift under a Butterworth low-pass, for a reference that has no top octave.
// 79 Vocal Chamber's reference walls at ~8 kHz (-8.7 dB at 8 kHz, -50 dB at 10 kHz against its
// 2-5 kHz) where DuskVerb 2 runs full band, and DuskVerb 2 is 4-9 dB dark at 6-8 kHz in every
// window of its impulse, so the wall alone darkens it further; the octave trim (3 dB either way,
// an octave wide) cannot lift 6-8 kHz under the wall without lifting 2-4 kHz with it.
//
// The lift is one RBJ peaking section, the octave trim's with its own Q:
//
//   A = 10^(gain / 40), w0 = 2 pi f0 / fs, alpha = sin (w0) / (2 Q): b0 = 1 + alpha A,
//   b1 = a1 = -2 cos (w0), b2 = 1 - alpha A, a0 = 1 + alpha / A, a2 = 1 - alpha / A.
//
// The low-pass is a Butterworth of even order N at the corner fc, as N / 2 RBJ low-pass sections
// in cascade, lowest Q first:
//
//   Q_k = 1 / (2 cos ((2k - 1) pi / (2N))), k = 1 .. N / 2; alpha = sin (w0) / (2 Q_k):
//   b0 = b2 = (1 - cos w0) / 2, b1 = 1 - cos w0, a0 = 1 + alpha, a1 = -2 cos w0, a2 = 1 - alpha,
//
// the bilinear transform of the analog Butterworth with the corner prewarped, so its magnitude is
// exactly |H(f)|^2 = 1 / (1 + (tan (pi f / fs) / tan (pi fc / fs))^(2N)): what scipy's
// butter (N, fc, fs=fs) designs, the tools repo's model (classic_lab/phaseC/bandlimit_refine.py).
// Both are designed and run in double (duskaudio::DoubleBiquad), lift first. Not oversampled, as
// the octave trim is not: the low-pass's warp only steepens its slope past the corner, whose
// place is exact at every rate, and a lift under 10 kHz keeps its shape at 44.1 kHz and up. A
// section at or above the designers' Nyquist ceiling (duskaudio::kMaxDesignFreqRatio x fs) has
// nothing to do there and does not run.
//
// No row: nothing runs, and the wet passes untouched, bit for bit. A swap fades linearly from the
// setting playing to the next over the engines' preset fade, both cascades on the same wet, the
// incoming one started from rest, as the octave trim does (DuskVerbOctaveTrim.hpp says why).
//
// Real time: fixed tables, no allocation anywhere. Deterministic and block-size invariant.

#pragma once

#include "../../shared-daf/dsp/DuskFilters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace duskverb
{

// A row: the low-pass (corner and order both 0: none) and the lift under it (all three 0: none).
struct BandLimitSetting
{
    float cornerHz = 0.0f;
    int order = 0;
    float liftHz = 0.0f;
    float liftDb = 0.0f;
    float liftQ = 0.0f;

    constexpr bool operator== (const BandLimitSetting& o) const noexcept
    {
        return cornerHz == o.cornerHz && order == o.order && liftHz == o.liftHz && liftDb == o.liftDb && liftQ == o.liftQ;
    }
    constexpr bool operator!= (const BandLimitSetting& o) const noexcept { return ! (*this == o); }
};

class BandLimit
{
public:
    static constexpr int kMaxOrder = 24;
    static constexpr float kMinHz = 1000.0f, kMaxHz = 20000.0f;
    static constexpr float kMaxLiftDb = 12.0f;
    static constexpr float kMinQ = 0.25f, kMaxQ = 6.0f;

    static constexpr bool lowpasses (const BandLimitSetting& s) noexcept { return s.order > 0; }
    static constexpr bool lifts (const BandLimitSetting& s) noexcept { return s.liftDb != 0.0f; }

    // The low-pass off (corner and order 0) or an even order in 2..kMaxOrder with a corner in
    // kMinHz..kMaxHz, and the lift off (all three 0) or a non-zero gain within kMaxLiftDb either way
    // at kMinHz..kMaxHz and a Q in kMinQ..kMaxQ (NaN is none of these): the voicing table is
    // checked against it when it compiles and the tuning hook refuses anything else; swapTo()
    // does not check again.
    static constexpr bool valid (const BandLimitSetting& s) noexcept
    {
        const bool lowpassOk = s.order == 0
            ? s.cornerHz == 0.0f
            : s.order >= 2 && s.order <= kMaxOrder && s.order % 2 == 0 && s.cornerHz >= kMinHz && s.cornerHz <= kMaxHz;
        const bool liftOk = s.liftDb == 0.0f
            ? s.liftHz == 0.0f && s.liftQ == 0.0f
            : s.liftDb >= -kMaxLiftDb && s.liftDb <= kMaxLiftDb && s.liftHz >= kMinHz && s.liftHz <= kMaxHz
                  && s.liftQ >= kMinQ && s.liftQ <= kMaxQ;
        return lowpassOk && liftOk;
    }

    // Both cascades designed at `sampleRate`, at rest, and any fade ended. The setting playing stays.
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        for (Cascade& cascade : cascades_)
            design (cascade);
        clear();
    }

    // Both cascades at rest, and any fade ended: the setting fading out is dropped.
    void clear() noexcept
    {
        fadeTotal_ = fadeDone_ = 0;
        for (Cascade& cascade : cascades_)
            rest (cascade);
    }

    // `setting` from here on, faded in over `fadeSamples` (0: at once) from the one playing.
    // DuskVerbDSP swaps only between fades; a swap arriving mid-fade ends the fade in flight first.
    void swapTo (const BandLimitSetting& setting, int fadeSamples) noexcept
    {
        if (setting == cascades_[current_].setting)
            return;
        current_ ^= 1;
        Cascade& incoming = cascades_[current_];
        incoming.setting = setting;
        design (incoming);
        rest (incoming);
        fadeTotal_ = std::max (0, fadeSamples);
        fadeDone_ = 0;
    }

    const BandLimitSetting& setting() const noexcept { return cascades_[current_].setting; }

    // Whether process() has anything to do: a section running, or a fade.
    bool active() const noexcept { return cascades_[current_].sections > 0 || fadeDone_ < fadeTotal_; }

    // In place on the wet; untouched while nothing runs and no fade does.
    void process (float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0 || ! active())
            return;
        float* const io[2] = { left, right };
        Cascade& incoming = cascades_[current_];
        int done = 0;
        if (fadeDone_ < fadeTotal_)
        {
            // Both cascades on the same wet, the incoming one's output faded in over the
            // outgoing one's: t as DuskVerbDSP forms it for the engines.
            Cascade& outgoing = cascades_[current_ ^ 1];
            const int fade = std::min (numSamples, fadeTotal_ - fadeDone_);
            const float invSpan = 1.0f / static_cast<float> (std::max (1, fadeTotal_ - 1));
            for (int at = 0; at < fade; at += kChunk)
            {
                const int n = std::min (kChunk, fade - at);
                for (int side = 0; side < 2; ++side)
                {
                    float* const x = io[side] + at;
                    std::copy (x, x + n, scratch_.begin());
                    run (outgoing, side, scratch_.data(), n);
                    run (incoming, side, x, n);
                    for (int i = 0; i < n; ++i)
                    {
                        const float t = std::min (1.0f, static_cast<float> (fadeDone_ + at + i) * invSpan);
                        const float out = scratch_[static_cast<size_t> (i)];
                        x[i] = out + t * (x[i] - out);
                    }
                }
            }
            fadeDone_ += fade;
            if (fadeDone_ == fadeTotal_)
                fadeTotal_ = fadeDone_ = 0;               // the outgoing setting is gone
            done = fade;
        }
        for (int side = 0; side < 2; ++side)
            run (incoming, side, io[side] + done, numSamples - done);
    }

private:
    static constexpr int kChunk = 256;
    static constexpr int kMaxSections = 1 + kMaxOrder / 2;

    struct Cascade
    {
        BandLimitSetting setting {};
        int sections = 0;                                     // how many run, the lift first
        duskaudio::DoubleBiquad section[2][kMaxSections];     // [side][section]
    };

    void design (Cascade& c) const noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        const double ceiling = duskaudio::kMaxDesignFreqRatio * sampleRate_;
        const BandLimitSetting& s = c.setting;
        c.sections = 0;
        const auto add = [&c] (const duskaudio::BiquadCoeffsDouble& coeffs) {
            c.section[0][c.sections].setCoeffs (coeffs);
            c.section[1][c.sections].setCoeffs (coeffs);
            ++c.sections;
        };
        if (lifts (s) && static_cast<double> (s.liftHz) < ceiling)
        {
            const double A = std::pow (10.0, static_cast<double> (s.liftDb) / 40.0);
            const double w0 = 2.0 * kPi * static_cast<double> (s.liftHz) / sampleRate_;
            const double alpha = std::sin (w0) / (2.0 * static_cast<double> (s.liftQ)), cw = std::cos (w0);
            const double inv = 1.0 / (1.0 + alpha / A);
            add ({ (1.0 + alpha * A) * inv, -2.0 * cw * inv, (1.0 - alpha * A) * inv, -2.0 * cw * inv, (1.0 - alpha / A) * inv });
        }
        if (lowpasses (s) && static_cast<double> (s.cornerHz) < ceiling)
        {
            const double w0 = 2.0 * kPi * static_cast<double> (s.cornerHz) / sampleRate_;
            const double cw = std::cos (w0), sw = std::sin (w0);
            for (int k = 1; k <= s.order / 2; ++k)           // Q rises with k: the lowest Q runs first
            {
                const double q = 1.0 / (2.0 * std::cos ((2.0 * k - 1.0) * kPi / (2.0 * s.order)));
                const double alpha = sw / (2.0 * q), inv = 1.0 / (1.0 + alpha);
                add ({ 0.5 * (1.0 - cw) * inv, (1.0 - cw) * inv, 0.5 * (1.0 - cw) * inv, -2.0 * cw * inv, (1.0 - alpha) * inv });
            }
        }
    }

    static void rest (Cascade& c) noexcept
    {
        for (auto& side : c.section)
            for (auto& s : side)
                s.reset();
    }

    // One side through every section that runs, a section over the whole span at a time. A local
    // copy keeps the state out of memory the samples might alias.
    static void run (Cascade& c, int side, float* x, int n) noexcept
    {
        for (int k = 0; k < c.sections; ++k)
        {
            duskaudio::DoubleBiquad s = c.section[side][k];
            for (int i = 0; i < n; ++i)
                x[i] = s.process (x[i]);
            c.section[side][k] = s;
        }
    }

    double sampleRate_ = 48000.0;
    Cascade cascades_[2];
    int current_ = 0;                                      // the setting playing (or fading in)
    int fadeTotal_ = 0, fadeDone_ = 0;
    std::array<float, kChunk> scratch_ {};
};

} // namespace duskverb

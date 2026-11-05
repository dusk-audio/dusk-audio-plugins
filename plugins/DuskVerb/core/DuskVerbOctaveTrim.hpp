// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// OctaveTrim: DuskVerb 2's static per-preset octave trim, run once on the summed wet (after the
// engines' crossfade and the early fields, before the ducker and the dry/wet mix). Nine RBJ
// peaking sections in cascade, one octave apart from 63 Hz to 16128 Hz at Q 1.41 (an octave
// wide), each within 3 dB either way, lowest first. Its magnitude is the sections' magnitudes
// multiplied:
//
//   |H(f)| = product over the sections that run of |H_b(e^jw)|, w = 2 pi f / fs, where
//   H_b(z) = (b0 + b1 z^-1 + b2 z^-2) / (a0 + a1 z^-1 + a2 z^-2), A = 10^(gain / 40),
//   w0 = 2 pi f_b / fs, alpha = sin (w0) / (2 Q): b0 = 1 + alpha A, b1 = a1 = -2 cos (w0),
//   b2 = 1 - alpha A, a0 = 1 + alpha / A, a2 = 1 - alpha / A.
//
// The sections are designed and run in double (duskaudio::DoubleBiquad), so the stage follows
// that formula to within 1e-5 dB at every rate. In float (Biquad::peak) the low sections' pole
// and zero pairs sit too near z = 1: measured, the response strayed from the formula by up to
// 0.03 dB at 48 kHz and 0.4 dB at 192 kHz, and their rounding noise came within 61 dB of the
// signal below 200 Hz.
//
// Neighbouring sections overlap: an octave from its centre a section keeps about a fifth of its
// gain in dB, so a band's centre reads its own gain plus its neighbours' skirts (all nine at
// +3 dB read about +4.4 dB in the middle bands). A section at 0 dB is left out, and so is one
// whose centre is at or above the designers' Nyquist ceiling (duskaudio::kMaxDesignFreqRatio x
// fs: the 16 kHz band below 32.27 kHz), where it could only be pulled under Nyquist. A flat trim
// (every band at 0 dB) therefore never runs, and the wet passes it untouched, bit for bit.
//
// A swap fades from the trim playing to the next over the engines' preset fade: both cascades
// run on the same wet and their outputs are crossfaded linearly, with the engines' t. The
// outputs are nearly the same signal, so a linear fade keeps the level where an equal-power one
// would lift it by up to 3 dB mid-fade. The incoming cascade starts from rest: measured on
// Large Chamber's wet (three stimuli, five pairs of trims), that came closer to two cascades
// that ran all along than taking over the outgoing cascade's state did in 14 of 15 cases. A
// swap to the trim already playing is none.
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

inline constexpr int kOctaveTrimBands = 9;

// dB per band, 63 Hz first.
using OctaveTrimGains = std::array<float, kOctaveTrimBands>;

class OctaveTrim
{
public:
    static constexpr std::array<float, kOctaveTrimBands> kBandHz { 63.0f, 126.0f, 252.0f, 504.0f, 1008.0f,
                                                                   2016.0f, 4032.0f, 8064.0f, 16128.0f };
    static constexpr double kQ = 1.41;
    static constexpr float kMaxDb = 3.0f;

    // Every band within kMaxDb either way (NaN is not): the voicing table is checked against it
    // when it compiles and the tuning hook refuses anything else; swapTo() does not check again.
    static constexpr bool valid (const OctaveTrimGains& gains) noexcept
    {
        for (const float db : gains)
            if (! (db >= -kMaxDb && db <= kMaxDb))
                return false;
        return true;
    }

    static constexpr bool flat (const OctaveTrimGains& gains) noexcept
    {
        for (const float db : gains)
            if (db != 0.0f)
                return false;
        return true;
    }

    // Whether band `band`'s centre is inside the designers' ceiling at `sampleRate`.
    static bool inBand (int band, double sampleRate) noexcept
    {
        return static_cast<double> (kBandHz[static_cast<size_t> (band)]) < duskaudio::kMaxDesignFreqRatio * sampleRate;
    }

    // Both cascades designed at `sampleRate`, at rest, and any fade ended. The trim playing stays.
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        for (Cascade& cascade : cascades_)
            design (cascade);
        clear();
    }

    // Both cascades at rest, and any fade ended: the trim fading out is dropped.
    void clear() noexcept
    {
        fadeTotal_ = fadeDone_ = 0;
        for (Cascade& cascade : cascades_)
            rest (cascade);
    }

    // `gains` from here on, faded in over `fadeSamples` (0: at once) from the trim playing.
    // DuskVerbDSP swaps only between fades; a swap arriving mid-fade ends the fade in flight first.
    void swapTo (const OctaveTrimGains& gains, int fadeSamples) noexcept
    {
        if (gains == cascades_[current_].gains)
            return;
        current_ ^= 1;
        Cascade& incoming = cascades_[current_];
        incoming.gains = gains;
        design (incoming);
        rest (incoming);
        fadeTotal_ = std::max (0, fadeSamples);
        fadeDone_ = 0;
    }

    const OctaveTrimGains& gains() const noexcept { return cascades_[current_].gains; }

    // Whether process() has anything to do: a trim playing that is not flat, or a fade.
    bool active() const noexcept { return cascades_[current_].sections > 0 || fadeDone_ < fadeTotal_; }

    // In place on the wet; untouched while the trim is flat and no fade runs.
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
                fadeTotal_ = fadeDone_ = 0;               // the outgoing trim is gone
            done = fade;
        }
        for (int side = 0; side < 2; ++side)
            run (incoming, side, io[side] + done, numSamples - done);
    }

private:
    static constexpr int kChunk = 256;

    struct Cascade
    {
        OctaveTrimGains gains {};
        std::array<bool, kOctaveTrimBands> runs {};
        int sections = 0;                                       // how many run
        duskaudio::DoubleBiquad section[2][kOctaveTrimBands];   // [side][band]
    };

    // Biquad::peak's RBJ peaking section, in double.
    static duskaudio::BiquadCoeffsDouble peak (double sampleRate, double hz, double db) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w0 = 2.0 * 3.14159265358979323846 * hz / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * kQ), cw = std::cos (w0);
        const double inv = 1.0 / (1.0 + alpha / A);
        return { (1.0 + alpha * A) * inv, -2.0 * cw * inv, (1.0 - alpha * A) * inv, -2.0 * cw * inv, (1.0 - alpha / A) * inv };
    }

    void design (Cascade& c) const noexcept
    {
        c.sections = 0;
        for (int b = 0; b < kOctaveTrimBands; ++b)
        {
            const size_t k = static_cast<size_t> (b);
            c.runs[k] = c.gains[k] != 0.0f && inBand (b, sampleRate_);
            if (! c.runs[k])
                continue;
            ++c.sections;
            const duskaudio::BiquadCoeffsDouble coeffs = peak (sampleRate_, kBandHz[k], c.gains[k]);
            c.section[0][k].setCoeffs (coeffs);
            c.section[1][k].setCoeffs (coeffs);
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
        for (int b = 0; b < kOctaveTrimBands; ++b)
        {
            if (! c.runs[static_cast<size_t> (b)])
                continue;
            duskaudio::DoubleBiquad s = c.section[side][b];
            for (int i = 0; i < n; ++i)
                x[i] = s.process (x[i]);
            c.section[side][b] = s;
        }
    }

    double sampleRate_ = 48000.0;
    Cascade cascades_[2];
    int current_ = 0;                                      // the trim playing (or fading in)
    int fadeTotal_ = 0, fadeDone_ = 0;
    std::array<float, kChunk> scratch_ {};
};

} // namespace duskverb

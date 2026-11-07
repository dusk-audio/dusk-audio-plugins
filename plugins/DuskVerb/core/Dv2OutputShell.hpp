// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// Dv2OutputShell: the output chain DuskVerbEngine runs after each of its engines, for what
// DuskVerb 2 renders outside them: the engines only DuskVerb 2 has, and the early field's post
// path. Lo Cut (2nd-order Butterworth high-pass, 5-500 Hz), Hi Cut (a high shelf at the corner,
// Q 1/sqrt(2), as deep as the preset's shelf depth), Mono Below (the lows under a first-order
// split at 20-300 Hz summed to mono by its depth; off at 20 Hz), Width (the side signal times
// width, 0-2) and the output trim (dB), in DuskVerbEngine's order and with its ranges, filter
// designs and smoothing times, so a control means the same thing on every engine. The cut and
// mono corners glide per block over 30 ms; width and trim glide per sample (2 ms, 5 ms).

#pragma once

#include "../src/dsp/DuskVerbEngine.h"     // OnePoleSmoother

#include <algorithm>
#include <cmath>

namespace duskverb
{

class Dv2OutputShell
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        width_.setSmoothingTime (sampleRate, 2.0f);
        trim_.setSmoothingTime (sampleRate, 5.0f);
        loCut_.setSmoothingTime (sampleRate, 30.0f);
        hiCut_.setSmoothingTime (sampleRate, 30.0f);
        monoBelow_.setSmoothingTime (sampleRate, 30.0f);
        snap();
        clear();
    }

    void clear() noexcept
    {
        for (auto& f : lo_) f.reset();
        for (auto& f : hi_) f.reset();
        monoLp_[0] = monoLp_[1] = 0.0f;
    }

    // Every smoother to its target, and the filters designed for it: for a fresh start (a
    // preset swap), so the first block does not glide in from the previous values.
    void snap() noexcept
    {
        width_.current = width_.target;
        trim_.current = trim_.target;
        loCut_.current = loCut_.target;
        hiCut_.current = hiCut_.target;
        monoBelow_.current = monoBelow_.target;
        designLoCut (loCut_.current);
        designHiCut (hiCut_.current);
        designMonoBelow (monoBelow_.current);
    }

    // The filter state of `other`, for a voice taking over from it (the early field installed at
    // a preset swap carries on from the one fading out); the coefficients and smoothers stay.
    void copyStateFrom (const Dv2OutputShell& other) noexcept
    {
        for (int s = 0; s < 2; ++s)
        {
            const auto lo = other.lo_[s].state(), hi = other.hi_[s].state();
            lo_[s].setState (lo.first, lo.second);
            hi_[s].setState (hi.first, hi.second);
            monoLp_[s] = other.monoLp_[s];
        }
    }

    void setLoCut (float hz) noexcept      { loCut_.setTarget (std::clamp (hz, 5.0f, 500.0f)); }
    void setHiCut (float hz) noexcept      { hiCut_.setTarget (std::clamp (hz, 1000.0f, 20000.0f)); }
    void setMonoBelow (float hz) noexcept  { monoBelow_.setTarget (std::clamp (hz, 20.0f, 300.0f)); }
    void setMonoBelowDepth (float depth) noexcept { monoDepth_ = std::clamp (depth, 0.0f, 1.0f); }
    void setWidth (float width) noexcept   { width_.setTarget (std::clamp (width, 0.0f, 2.0f)); }
    void setGainTrim (float db) noexcept   { trim_.setTarget (std::pow (10.0f, std::clamp (db, -48.0f, 48.0f) / 20.0f)); }
    void setHiCutShelfDb (float db) noexcept
    {
        db = std::clamp (db, -24.0f, 0.0f);
        if (db != shelfDb_)
        {
            shelfDb_ = db;
            designHiCut (hiCut_.current);
        }
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;
        // The block's input level, for zeroDecayedState(), read before the loop overwrites it.
        float inputPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            inputPeak = std::max (inputPeak, std::max (std::abs (left[i]), std::abs (right[i])));
        const float lo = loCut_.skip (numSamples), hi = hiCut_.skip (numSamples);
        const float mono = monoBelow_.skip (numSamples);
        if (std::abs (lo - appliedLoCut_) > 0.5f) designLoCut (lo);
        if (std::abs (hi - appliedHiCut_) > 1.0f) designHiCut (hi);
        if (std::abs (mono - appliedMonoBelow_) > 0.5f) designMonoBelow (mono);
        for (int i = 0; i < numSamples; ++i)
        {
            float l = hi_[0].process (lo_[0].process (left[i]));
            float r = hi_[1].process (lo_[1].process (right[i]));
            if (monoOn_)
            {
                // DuskVerbEngine's Mono Maker: low plus high is the input, so the split is flat.
                monoLp_[0] = (1.0f - monoCoeff_) * l + monoCoeff_ * monoLp_[0];
                monoLp_[1] = (1.0f - monoCoeff_) * r + monoCoeff_ * monoLp_[1];
                const float lows = 0.5f * (monoLp_[0] + monoLp_[1]);
                const float highL = l - monoLp_[0], highR = r - monoLp_[1];
                l = monoDepth_ * lows + (1.0f - monoDepth_) * monoLp_[0] + highL;
                r = monoDepth_ * lows + (1.0f - monoDepth_) * monoLp_[1] + highR;
            }
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width_.next();
            const float g = trim_.next();
            left[i]  = (mid + side) * g;
            right[i] = (mid - side) * g;
        }
        zeroDecayedState (inputPeak);
    }

private:
    // Once a block's input and every filter state are under kZeroBelow, the tail has decayed out
    // of anything audible and all the states are set to exactly 0 together. That is what ends a
    // decay at 0 on every CPU: left to the arithmetic, flush-to-zero can keep a recursion going
    // for good (a 5 Hz Lo Cut at 48 kHz sits near 8.6e-36 for seconds on x86-64 and arm64 alike;
    // arm64's fused multiply-adds only stretched the 20 Hz case the core test meets). All at once
    // and only then, because one state can be under 1e-30 while the filter still passes a signal
    // (a 0 dB Hi Cut shelf does), and zeroing it alone would move that signal. Mono Below's state
    // counts only while it runs: switched off it is left as it is, for when it comes back.
    static constexpr float kZeroBelow = 1.0e-30f;
    static bool decayed (float s) noexcept { return std::abs (s) < kZeroBelow; }
    static bool decayed (const duskaudio::Biquad& f) noexcept
    {
        const auto s = f.state();
        return decayed (s.first) && decayed (s.second);
    }
    void zeroDecayedState (float inputPeak) noexcept
    {
        if (! decayed (inputPeak) || ! decayed (lo_[0]) || ! decayed (lo_[1])
            || ! decayed (hi_[0]) || ! decayed (hi_[1]))
            return;
        if (monoOn_ && ! (decayed (monoLp_[0]) && decayed (monoLp_[1])))
            return;
        for (auto& f : lo_) f.reset();
        for (auto& f : hi_) f.reset();
        if (monoOn_)
            monoLp_[0] = monoLp_[1] = 0.0f;
    }

    // RBJ 2nd-order Butterworth high-pass: DuskVerbEngine::updateLoCutCoeffs.
    void designLoCut (float hz) noexcept
    {
        appliedLoCut_ = hz;
        const float fs = static_cast<float> (sampleRate_);
        const float fc = std::clamp (hz, 5.0f, 0.49f * fs);
        const float w0 = 2.0f * 3.14159265358979f * fc / fs;
        const float cosw = std::cos (w0), alpha = std::sin (w0) / (2.0f * 0.7071067811865475f);
        const float a0 = 1.0f + alpha;
        const duskaudio::BiquadCoeffs c { (1.0f + cosw) * 0.5f / a0, -(1.0f + cosw) / a0, (1.0f + cosw) * 0.5f / a0,
                                          -2.0f * cosw / a0, (1.0f - alpha) / a0 };
        for (auto& f : lo_) f.setCoeffs (c);
    }

    // RBJ high shelf at Q 1/sqrt(2), shelfDb_ deep: DuskVerbEngine::updateHiCutCoeffs.
    void designHiCut (float hz) noexcept
    {
        appliedHiCut_ = hz;
        const float fs = static_cast<float> (sampleRate_);
        const float fc = std::clamp (hz, 1000.0f, 0.49f * fs);
        const float A = std::max (std::pow (10.0f, shelfDb_ / 40.0f), 1.0e-6f), sqrtA = std::sqrt (A);
        const float w0 = 2.0f * 3.14159265358979f * fc / fs;
        const float cosw = std::cos (w0), alpha = std::sin (w0) / (2.0f * 0.7071067811865475f);
        const float t = 2.0f * sqrtA * alpha, ap = A + 1.0f, am = A - 1.0f;
        const float a0 = ap - am * cosw + t;
        const duskaudio::BiquadCoeffs c { A * (ap + am * cosw + t) / a0, -2.0f * A * (am + ap * cosw) / a0,
                                          A * (ap + am * cosw - t) / a0, 2.0f * (am - ap * cosw) / a0,
                                          (ap - am * cosw - t) / a0 };
        for (auto& f : hi_) f.setCoeffs (c);
    }

    // One-pole lowpass at the corner, engaged above 22 Hz: DuskVerbEngine's Mono Maker.
    void designMonoBelow (float hz) noexcept
    {
        appliedMonoBelow_ = hz;
        monoCoeff_ = std::exp (-2.0f * 3.14159265358979f * hz / static_cast<float> (sampleRate_));
        monoOn_ = hz > 22.0f;
    }

    double sampleRate_ = 48000.0;
    OnePoleSmoother width_ { 1.0f, 1.0f, 0.0f }, trim_ { 1.0f, 1.0f, 0.0f };
    OnePoleSmoother loCut_ { 20.0f, 20.0f, 0.0f }, hiCut_ { 20000.0f, 20000.0f, 0.0f };
    OnePoleSmoother monoBelow_ { 20.0f, 20.0f, 0.0f };
    float shelfDb_ = -12.0f;
    float appliedLoCut_ = -1.0f, appliedHiCut_ = -1.0f, appliedMonoBelow_ = -1.0f;
    float monoDepth_ = 1.0f, monoCoeff_ = 0.0f, monoLp_[2] {};
    bool monoOn_ = false;
    duskaudio::Biquad lo_[2], hi_[2];
};

} // namespace duskverb

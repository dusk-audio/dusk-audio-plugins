#pragma once

#include "TwoBandDamping.h"   // ShelfBiquad lives here

#include <cmath>

// 3-band parametric output EQ for the wet signal — Phase 5 feature added
// 2026-05-02 to close the spectral gap on plate / hall / chamber presets
// where the engine's intrinsic frequency response can't reach the Lex
// reference exactly. Lives in the shell after hiCutFilter, before
// monoMaker / width / mix.
//
//   Low-shelf  — corrective bass shaping (cut mud or pad warmth)
//   Mid bell   — surgical notch on modal humps (e.g. plate engine's
//                +2-3 dB peak at 4-8 kHz)
//   High shelf — air-band lift to push centroid up toward Lex's bright
//                tail without raising the in-loop HF retention (which
//                would re-introduce the modal hump)
//
// Defaults to flat (all gains 0 dB) so any preset that doesn't configure
// it passes audio through unchanged — preserves backwards-compat with
// every existing preset.
struct OutputEq
{
    // ShelfBiquad has mono state; instantiate per-channel for stereo.
    ShelfBiquad lowShelfL,  lowShelfR;
    ShelfBiquad highShelfL, highShelfR;

    // Peaking ("bell") biquad — RBJ-cookbook formula, direct-form I,
    // per-channel state. Used for the mid notch.
    struct PeakBiquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float zL1 = 0.0f, zL2 = 0.0f, zR1 = 0.0f, zR2 = 0.0f;

        static constexpr float kTwoPi = 6.283185307179586f;

        void designPeak (float gainLin, float fcHz, float Q, float sampleRate)
        {
            const float A     = std::sqrt (std::max (gainLin, 1.0e-12f));
            const float w0    = kTwoPi * std::min (fcHz, 0.49f * sampleRate) / sampleRate;
            const float cosw  = std::cos (w0);
            const float sinw  = std::sin (w0);
            const float alpha = sinw / (2.0f * std::max (Q, 0.1f));
            const float a0    = 1.0f + alpha / A;
            b0 = (1.0f + alpha * A) / a0;
            b1 = (-2.0f * cosw)     / a0;
            b2 = (1.0f - alpha * A) / a0;
            a1 = (-2.0f * cosw)     / a0;
            a2 = (1.0f - alpha / A) / a0;
        }

        float processL (float x)
        {
            const float y = b0 * x + zL1;
            zL1 = b1 * x - a1 * y + zL2;
            zL2 = b2 * x - a2 * y;
            return y;
        }
        float processR (float x)
        {
            const float y = b0 * x + zR1;
            zR1 = b1 * x - a1 * y + zR2;
            zR2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { zL1 = zL2 = zR1 = zR2 = 0.0f; }
    };

    PeakBiquad midBell;

    // Cached state so repeated equal calls don't recompute biquad coeffs.
    float sampleRate_ = 44100.0f;
    float lastLowGainDb_  = 0.0f, lastLowFcHz_  =  150.0f;
    float lastMidGainDb_  = 0.0f, lastMidFcHz_  = 5000.0f, lastMidQ_ = 1.0f;
    float lastHighGainDb_ = 0.0f, lastHighFcHz_ = 8000.0f;

    void prepare (float sampleRate)
    {
        sampleRate_ = sampleRate;
        reset();
        // Re-design with cached values so prepare() can be called after
        // setX() during a preset crossfade without losing config.
        applyLowShelf();
        applyMidBell();
        applyHighShelf();
    }

    void setLowShelf (float gainDb, float fcHz)
    {
        lastLowGainDb_ = gainDb;
        lastLowFcHz_   = fcHz;
        applyLowShelf();
    }

    void setMidBell (float gainDb, float fcHz, float Q)
    {
        lastMidGainDb_ = gainDb;
        lastMidFcHz_   = fcHz;
        lastMidQ_      = Q;
        applyMidBell();
    }

    void setHighShelf (float gainDb, float fcHz)
    {
        lastHighGainDb_ = gainDb;
        lastHighFcHz_   = fcHz;
        applyHighShelf();
    }

    // Per-sample stereo processing. When all three bands are at 0 dB the
    // biquads compute identity (within float precision) so the perf cost
    // is ~6 multiplies per sample per channel — negligible.
    void processStereo (float& l, float& r)
    {
        l = lowShelfL.process (l);
        r = lowShelfR.process (r);
        l = midBell.processL (l);
        r = midBell.processR (r);
        l = highShelfL.process (l);
        r = highShelfR.process (r);
    }

    void reset()
    {
        lowShelfL.reset();
        lowShelfR.reset();
        midBell.reset();
        highShelfL.reset();
        highShelfR.reset();
    }

private:
    void applyLowShelf()
    {
        const float gainLin = std::pow (10.0f, lastLowGainDb_ / 20.0f);
        lowShelfL.designLowShelf (gainLin, lastLowFcHz_, sampleRate_);
        lowShelfR.designLowShelf (gainLin, lastLowFcHz_, sampleRate_);
    }
    void applyMidBell()
    {
        const float gainLin = std::pow (10.0f, lastMidGainDb_ / 20.0f);
        midBell.designPeak (gainLin, lastMidFcHz_, lastMidQ_, sampleRate_);
    }
    void applyHighShelf()
    {
        const float gainLin = std::pow (10.0f, lastHighGainDb_ / 20.0f);
        highShelfL.designHighShelf (gainLin, lastHighFcHz_, sampleRate_);
        highShelfR.designHighShelf (gainLin, lastHighFcHz_, sampleRate_);
    }
};

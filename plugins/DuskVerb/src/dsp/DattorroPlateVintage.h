#pragma once

#include "DattorroTank.h"

#include <algorithm>
#include <cmath>
#include <vector>

// DattorroPlateVintage — Dattorro figure-8 tank + vintage-Lex corrective
// EQ chain for the "Vintage Vocal Plate" preset.
//
// Built 2026-05-13 as a structural fix for the boxiness artifact that
// the bare Dattorro tank produces in the 200-500 Hz band on noise-burst
// tails (~+12 dB peak/flank ratio above the Lex Vintage Plate target).
// Param sweeps (low_crossover × bass_mult × mid_mult) couldn't move the
// box ratio below +9 dB above Lex — it's intrinsic to the tank's modal
// structure. The existing DattorroTank stays untouched (other presets
// depend on its current sound); this wrapper adds a fixed post-EQ chain
// to carve out the 300-400 Hz hump and lift the Lex-style ~8 kHz
// brick-wall ceiling without altering the underlying tank.
//
// Interface mirrors DattorroVintage so the DuskVerbEngine glue layer
// is a pure rename — call sites unchanged. HDP-specific setters
// (setBassChokeHz, setSparseTapLevel, setSubMultiply, setSubCrossoverFreq)
// are no-ops here because the Dattorro topology doesn't use them.
class DattorroPlateVintage
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void clearBuffers();
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    // Forwarded to internal DattorroTank — same semantics as DattorroTank.
    void setDecayTime         (float seconds);
    void setSize              (float size);
    void setBassMultiply      (float mult);
    void setMidMultiply       (float mult);
    void setTrebleMultiply    (float mult);
    void setCrossoverFreq     (float hz);
    void setHighCrossoverFreq (float hz);
    void setSaturation        (float amount);
    void setModDepth          (float depth);
    void setModRate           (float hz);
    void setTankDiffusion     (float amount);
    void setFreeze            (bool frozen);

    // HDP-compat no-ops. The Dattorro architecture has no in-loop bass
    // choke, no sparse-tap ER generator, no 4-band damper sub-band.
    // Calls accepted silently so the engine glue layer stays uniform.
    void setSubMultiply       (float /*mult*/) {}
    void setSubCrossoverFreq  (float /*hz*/)   {}
    void setBassChokeHz       (float /*hz*/)   {}
    void setSparseTapLevel    (float /*level*/) {}

private:
    DattorroTank tank_;

    // RBJ peaking biquad — vintage-Lex corrective EQ. Two stages used:
    //   1) box-cut: 350 Hz peak, Q=1.4, gain set in prepare() to flatten
    //      the Dattorro tank's intrinsic 200-500 Hz hump.
    //   2) low_shelf-style "tightness" cut: gentler low-mid trim that
    //      keeps the bass from getting woofy after the box is gone.
    struct PeakBiquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1L = 0.0f, z2L = 0.0f, z1R = 0.0f, z2R = 0.0f;
        void design (float fcHz, float Q, float gainDb, float sr)
        {
            const float A     = std::pow (10.0f, gainDb / 40.0f);
            const float w0    = 6.283185307f * std::min (fcHz, 0.49f * sr) / sr;
            const float cosw  = std::cos (w0);
            const float sinw  = std::sin (w0);
            const float alpha = sinw / (2.0f * std::max (Q, 0.1f));
            const float a0    = 1.0f + alpha / A;
            b0 = (1.0f + alpha * A) / a0;
            b1 = -2.0f * cosw / a0;
            b2 = (1.0f - alpha * A) / a0;
            a1 = -2.0f * cosw / a0;
            a2 = (1.0f - alpha / A) / a0;
        }
        float processL (float x)
        {
            const float y = b0 * x + z1L;
            z1L = b1 * x - a1 * y + z2L;
            z2L = b2 * x - a2 * y;
            return y;
        }
        float processR (float x)
        {
            const float y = b0 * x + z1R;
            z1R = b1 * x - a1 * y + z2R;
            z2R = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1L = z2L = z1R = z2R = 0.0f; }
    };

    PeakBiquad boxCut_;       // 350 Hz, narrow, -6 dB
    PeakBiquad lowMidTrim_;   // 200 Hz, broad, -2 dB
    bool prepared_ = false;

    // ─────────────────────────────────────────────────────────
    // Multi-tap input injection — added 2026-05-19 to close
    // EDT / D50 / 250 Hz RT60 gap vs Lex Vocal Plate anchor.
    //
    // The bare DattorroTank produces a fast-rising IR whose first
    // 50 ms holds too much energy (D50 over-positive) and whose
    // initial decay slope is steeper than Lex's (EDT under-target).
    // Spreading the dry input across 4 staggered predelay taps
    // before the tank lets the tank's recirculation present an
    // energy buildup (≈ 0–35 ms) rather than a single spike.
    // Outputs of the four taps overlap at the tank input and
    // recirculate through the figure-8 normally, so RT60 stays
    // anchored to the existing decay / damping params.
    //
    // Tap times + weights tuned against the lex anchor; see
    // the matching commit message.
    // ─────────────────────────────────────────────────────────
    static constexpr int   kPredelayMaxSamplesAt48k = 7680;   // 160 ms
    static constexpr int   kTap0SamplesAt48k        =  960;   //  20 ms (primary, after silent predelay)
    static constexpr int   kTap1SamplesAt48k        = 1440;   //  30 ms
    static constexpr int   kTap2SamplesAt48k        = 2400;   //  50 ms
    static constexpr int   kTap3SamplesAt48k        = 3840;   //  80 ms (secondary plateau)
    static constexpr int   kTap4SamplesAt48k        = 5280;   // 110 ms (late tail)
    static constexpr float kTap0Weight              = 1.00f;
    static constexpr float kTap1Weight              = 0.50f;
    static constexpr float kTap2Weight              = 0.40f;
    static constexpr float kTap3Weight              = 0.35f;
    static constexpr float kTap4Weight              = 0.05f;
    // Sum of all tap weights = 2.30. Normalised at runtime (kTapNorm)
    // so the tank sees DC-gain = 1.0 regardless of how many taps fire
    // — preserves the tank's pre-multi-tap saturation calibration.
    static constexpr float kTapWeightSum            = kTap0Weight + kTap1Weight
                                                    + kTap2Weight + kTap3Weight
                                                    + kTap4Weight;
    static constexpr float kTapNorm                 = 1.0f / kTapWeightSum;

    struct MultiTapDelay
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask     = 0;
        void allocate (int requestedMax);
        void clear();
        void  write (float sample);
        float read  (int delaySamples) const;
    };

    MultiTapDelay predelayL_, predelayR_;
    std::vector<float> tankInL_, tankInR_;     // pre-tank scratch buffers
    double sampleRate_ = 48000.0;

    // Cached at prepare() so process() does no per-block math for
    // sample-rate-scaled tap positions.
    int tapSamples_[5] = {};
    bool lowMidTrimEnabled_ = false;   // skip biquad when gain == 0 dB
};

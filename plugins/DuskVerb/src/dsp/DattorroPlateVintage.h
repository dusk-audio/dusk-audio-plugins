#pragma once

#include "DattorroTank.h"

#include <algorithm>   // std::min, std::max, std::fill (don't rely on transitive include)
#include <cmath>
#include <vector>

// DattorroPlateVintage — Dattorro figure-8 tank wrapped in a vintage-hardware
// corrective signal chain. Dedicated to the "Vintage Vocal Plate" preset
// (no other factory preset routes through this engine).
//
// Signal flow:
//   input → bassLift (pre-tank low-shelf, default 0 dB / no-op)
//         → hfLift   (pre-tank +22 dB shelf @ 6 kHz)
//         → DattorroTank (with in-loop structural HF damping @ 8 kHz)
//         → boxCut   (post-tank 320 Hz peaking, -3.5 dB; configurable)
//         → lowMidTrim (no-op until measurement justifies)
//         → output
//
// 2026-05-13: Built as a structural fix for the 200-500 Hz box artifact
//   on noise-burst tails (~+12 dB above the anchor). Initial design carved the
//   hump with a post-tank peaking biquad and kept the tank untouched.
//
// 2026-05-24: Refit to match the anchor's bright-early / dark-late spectrum
//   (cent_50 ≈ 10.5 kHz, cent_500 ≈ 4.3 kHz). Moved the HF shelf PRE-tank
//   (the tank impulse response is LTI w.r.t. external filters so position
//   is acoustically equivalent to post-tank — pre is cleaner topologically
//   for future time-varying replacements). Wired up DattorroTank's
//   per-loop structural HF damper (was off by default) so the bright
//   shelf entering the tank attenuates over each modal pass, producing
//   The anchor-style steep early-to-late HF rolloff. Combined with the
//   factory preset's lowered Treble Multiply (0.95) and Gain Trim (4 dB
//   to make headroom for the +22 dB shelf), both centroid metrics now
//   land within ±10 % of the anchor.
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
    void setDensityDepth      (float depth01);
    void setModReduction      (float reduction01);
    void setInputDiffusionScale (float scale01);
    void setSoftOnsetMs       (float ms);
    void setOctaveT60         (int band, float seconds);
    void setOctaveDecayRef    (float seconds);
    void setTonalCorrDb       (int band, float dB);
    void setBloomAttackMs     (float ms);
    void setBloomExp          (float e);
    void setFreeze            (bool frozen);

    // HDP-compat no-ops. The Dattorro architecture has no in-loop bass
    // choke, no sparse-tap ER generator, no 4-band damper sub-band.
    // Calls accepted silently so the engine glue layer stays uniform.
    void setSubMultiply       (float /*mult*/) {}
    void setSubCrossoverFreq  (float /*hz*/)   {}
    void setBassChokeHz       (float /*hz*/)   {}
    void setSparseTapLevel    (float /*level*/) {}

    // Per-preset brightness controls. Calling these after prepare() re-designs
    // the pre-tank HF shelf and re-applies the in-loop structural HF damper.
    // Defaults match the Vintage Vocal Plate calibration (shelf +22 dB at
    // 6 kHz, struct HF damp at 8 kHz). Other presets can dial these to match
    // their own anchor brightness without forking the engine.
    void setHfShelfGainDb     (float gainDb);     // pre-tank shelf gain (dB)
    void setHfShelfFreqHz     (float fcHz);       // pre-tank shelf corner (Hz)
    void setStructHfDampHz    (float hz);         // in-loop per-pass LP cutoff (Hz)

    // Per-preset corrective EQ controls. boxCut is the post-tank 320 Hz
    // peaking notch that flattens the Dattorro tank's intrinsic 200-500 Hz
    // hump; bassShelf is a pre-tank low-shelf that re-injects bass energy
    // the boxCut otherwise removes. Defaults match the original VVP
    // calibration (boxCut -3.5 dB @ 320 Hz, bassShelf 0 dB / no-op).
    void setBoxCutGainDb      (float gainDb);     // post-tank notch gain (dB, negative = cut)
    void setBoxCutFreqHz      (float fcHz);       // post-tank notch corner (Hz)
    void setBassShelfGainDb   (float gainDb);     // pre-tank low-shelf gain (dB)
    void setBassShelfFreqHz   (float fcHz);       // pre-tank low-shelf corner (Hz)

    // Front-load early-reflection network (2026-06-19). The Dattorro tank is
    // dense-from-onset (~44% of the energy in the first 50 ms); the Lexicon
    // Vintage Plate BUILDS over ~90 ms to a prominent reflection then tails
    // (~11% first-50 ms, peak/tap @ ~92 ms). This injects a sparse, lightly-
    // diffused, band-limited early field (0..~tapMs, building) AND pre-delays
    // the tank input so its dense field arrives AFTER the early build — the
    // delayed tank onset becomes the prominent arrival. Gives the slow-build
    // envelope + discrete early tap WITHOUT bloom's envelope smoothing (which
    // collapsed env_p2p) or a raw dry tap's spectral spike (which wrecked the
    // band balance). erGain 0 = network bypassed, tank runs un-delayed = the
    // pre-network engine byte-for-byte.
    void setFrontLoad (float erGain, float predelayMs, float tapMs, float lpHz);

private:
    DattorroTank tank_;

    // RBJ peaking biquad — vintage-hardware corrective EQ. Two stages used:
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
        // RBJ high-shelf biquad. Used to lift the upper octaves on
        // the post-tank chain so the Dattorro plate can match brighter
        // vintage-hardware tilt targets without altering the in-loop tank.
        void designHighShelf (float fcHz, float Q, float gainDb, float sr)
        {
            const float A     = std::pow (10.0f, gainDb / 40.0f);
            const float w0    = 6.283185307f * std::min (fcHz, 0.49f * sr) / sr;
            const float cosw  = std::cos (w0);
            const float sinw  = std::sin (w0);
            const float alpha = sinw / (2.0f * std::max (Q, 0.1f));
            const float sqA   = std::sqrt (std::max (A, 1.0e-12f));
            const float a0    = (A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqA * alpha;
            b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqA * alpha) / a0;
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw)              / a0;
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqA * alpha) / a0;
            a1 = 2.0f *      ((A - 1.0f) - (A + 1.0f) * cosw)              / a0;
            a2 =             ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqA * alpha) / a0;
        }
        // RBJ low-shelf biquad. Used pre-tank to inject bass energy
        // that the post-tank boxCut otherwise carves out, restoring
        // the warmth lost to the 320 Hz corrective notch on dark plates.
        void designLowShelf (float fcHz, float Q, float gainDb, float sr)
        {
            const float A     = std::pow (10.0f, gainDb / 40.0f);
            const float w0    = 6.283185307f * std::min (fcHz, 0.49f * sr) / sr;
            const float cosw  = std::cos (w0);
            const float sinw  = std::sin (w0);
            const float alpha = sinw / (2.0f * std::max (Q, 0.1f));
            const float sqA   = std::sqrt (std::max (A, 1.0e-12f));
            const float a0    = (A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqA * alpha;
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqA * alpha) / a0;
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw)               / a0;
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqA * alpha) / a0;
            a1 = -2.0f *     ((A - 1.0f) + (A + 1.0f) * cosw)              / a0;
            a2 =             ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqA * alpha) / a0;
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

    PeakBiquad boxCut_;       // post-tank 320 Hz narrow notch (configurable)
    PeakBiquad lowMidTrim_;   // 200 Hz, broad, -2 dB (no-op until justified)
    // HF shelf — applied PRE-tank so the in-loop structural HF damping
    // attenuates the boosted top-end naturally over each modal pass.
    // Early reflections retain the boost (few passes through damping);
    // late tail darkens (many passes). Combined with structural HF
    // damping at 8 kHz inside the tank, this matches the anchor's bright-
    // early / dark-late spectrum without a static post-EQ shelf.
    PeakBiquad hfLift_;
    // Bass shelf — applied PRE-tank to inject low-frequency energy that
    // the post-tank boxCut otherwise removes. Defaults to 0 dB (no-op);
    // dark plates with -100 % bass deficits can dial in +6 to +12 dB to
    // restore warmth without disabling the corrective boxCut notch.
    PeakBiquad bassLift_;
    // Scratch buffers for the pre-tank EQ stage. Filtered input is
    // written here, then fed to tank_.process. Sized in prepare().
    std::vector<float> preTankL_;
    std::vector<float> preTankR_;

    // ── Front-load early-reflection network (see setFrontLoad) ──
    // Power-of-2 ring delay (single-write, fractional-free integer taps).
    struct RingDelay
    {
        std::vector<float> buf; int mask = 0, w = 0;
        void prepare (int maxSamples)
        {
            // +1 so the buffer is strictly LARGER than maxSamples: with a pure
            // pow2 size, mask == size-1 would alias the max delay onto zero delay.
            // One extra slot keeps the oldest readable sample distinct from now.
            int n = 1; while (n < std::max (2, maxSamples + 1)) n <<= 1;
            buf.assign (static_cast<size_t> (n), 0.0f); mask = n - 1; w = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        inline void  write (float x)   { buf[static_cast<size_t> (w)] = x; w = (w + 1) & mask; }
        inline float readAgo (int d) const { return buf[static_cast<size_t> ((w - d) & mask)]; }
    };
    RingDelay erDelayL_, erDelayR_;             // early-field tap source (band-limited pre-tank signal)
    RingDelay tankPreL_, tankPreR_;             // pre-delays the tank INPUT so it fills in after the build
    std::vector<float> earlyL_, earlyR_;        // early-field scratch (summed post-tank)
    float erLpZL_ = 0.0f, erLpZR_ = 0.0f;       // 1-pole LP state (band-limit the early field)
    float erLpCoeff_ = 1.0f;
    float frontErGain_      = 0.0f;             // 0 = network bypassed (bit-null)
    float frontPredelayMs_  = 60.0f;
    int   frontPredelaySamp_ = 0;
    float frontTapMs_       = 80.0f;
    float frontLpHz_        = 7000.0f;
    // Early-field tap pattern: a FEW well-separated DISCRETE taps (NOT diffused —
    // diffusion fills the envelope dips and collapses env_p2p, the anchor's defining
    // dynamic). Fractions of frontTapMs_ + building gains; silence between taps =
    // the envelope ripple the Lexicon plate has. The delayed-tank onset supplies the
    // final peak. R taps offset a hair for L/R decorrelation (stereo_corr).
    static constexpr int   kErTaps = 3;
    static constexpr float kErTapFrac[kErTaps] = { 0.30f, 0.60f, 0.92f };
    static constexpr float kErTapGain[kErTaps] = { 0.50f, 0.72f, 1.00f };
    // Per-preset EQ state — cached so individual setter calls re-design
    // the affected filter without losing the other axis's setting. Defaults
    // match the original Vintage Vocal Plate calibration.
    float hfShelfGainDb_     = 22.0f;
    float hfShelfFreqHz_     = 6000.0f;
    float structHfDampHz_    = 8000.0f;
    float boxCutGainDb_      = -3.5f;
    float boxCutFreqHz_      = 320.0f;
    float bassShelfGainDb_   = 0.0f;
    float bassShelfFreqHz_   = 180.0f;
    float sampleRate_        = 48000.0f;
    bool prepared_ = false;
};

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// WheelMod.hpp — the Oracle engine's global Wheel Mod section (issue #303).
//
// Framework-free C++17, header-only. One instance per ENGINE, not per voice: the
// classic American poly has a single modulation bus, so one free-running LFO and
// one noise source modulate every voice in the same way. The mod wheel scales that
// bus between two limits and five destination switches send it to oscillator A
// frequency, oscillator B frequency, oscillator A pulse width, oscillator B pulse
// width and filter cutoff.
//
//     LFO (saw / triangle / square, combinable) --+
//                                                 +-- SOURCE MIX --x-- depth --> destinations
//     NOISE (shaped)  or  S&H (noise at LFO rate) +               |
//                                    depth = lower + (upper - lower) x wheel
//
// Runs at the HOST rate, once per host sample (process()), so the modulation is
// the same signal at every oversampling factor; the voices read the frame it
// returns. Audio-thread contract: no allocation, no locks, no I/O, bounded work.
// Wiring: MultiSynthDSP sets it every block and runs it every host sample in
// every mode, and publishes the frame into VoiceParameters (wheel* fields) only
// in Oracle; Voice.hpp adds it to the two pitches, the two pulse widths and the
// cutoff.
//
// Every number below is a measurement of the reference model taken on 2026-11-17
// (instrument host renders, osc B as a triangle carrier at A4/A7, slow LFO, wheel
// at full scale unless noted). One "bus unit" is the saw alone at full depth:
// 1175 cents on oscillator B frequency. The comments give each value in the unit
// it was measured in.
//
// OFF CONTRACT. With no destination switched on (and none still fading out),
// active() is false and every field of the returned frame is exactly +0.0f. The
// voices apply the frame only while active() is true, so a patch that never
// switches a destination on renders bit-identically to an engine without this
// section; that is what lets the parameters ship "off by default".

#pragma once

#include "SynthCommon.hpp"
#include "Envelope.hpp"      // LFO::kSeekFadeSeconds
#include "DuskSmoothed.hpp"

#include <cmath>

namespace msynth
{

// Per-sample output, in the units the voice hooks consume.
struct WheelModFrame
{
    float freqACents = 0.0f;   // oscillator A pitch offset, cents
    float freqBCents = 0.0f;   // oscillator B pitch offset, cents
    float pwA = 0.0f;          // oscillator A pulse-width offset, duty fraction
    float pwB = 0.0f;          // oscillator B pulse-width offset, duty fraction
    float filterOct = 0.0f;    // filter cutoff offset, octaves
};

// Block-rate settings, already in engine units (the parameter table holds them
// 1:1; see MultiSynthDSP pWheel*). Out-of-range or non-finite values are clamped
// or ignored by WheelMod::set, never trusted.
struct WheelModSettings
{
    float rateHz = 0.8866f;          // free-running LFO rate
    bool  saw = false, tri = true, square = false;
    float sourceMix = 0.0f;          // 0 = all LFO .. 1 = all noise / S&H
    bool  sampleAndHold = false;     // noise source: false = shaped noise, true = S&H
    float lowerLimit = 0.0f;         // depth at wheel 0
    float upperLimit = 1.0f;         // depth at wheel 1
    bool  freqA = false, freqB = false, pwA = false, pwB = false, filter = false;
};

class WheelMod
{
public:
    // ---- the reference laws -----------------------------------------------
    // LFO rate: exponential knob law, 0.0300 Hz at the bottom (period 33.33 s,
    // measured) to 26.2 Hz at the top (knob 25 / 50 / 75 measured 0.1628 /
    // 0.883 / 4.795 Hz, all within 0.6 % of this law). A logarithmic parameter
    // between these two ends IS the knob law.
    static constexpr float kRateMinHz = 0.03f;
    static constexpr float kRateMaxHz = 26.2f;

    // Destination depth per bus unit at full depth. Frequency: the saw alone
    // spans 0..+1175.06 cents on oscillator B (oscillator A measured 0.9 % more
    // on the square, +811 against +804 cents; one value serves both). Pulse
    // width: the square at full wheel moves the duty by +0.802 (A) / +0.798 (B),
    // linear in the wheel (32 and 64 measured), and the square is 0.6843 units.
    // Filter: 0.0695 octave per wheel step with the square (8.83 octaves at full
    // wheel); the saw at wheel 16 gives 1.461 x the square's swing, i.e. one bus
    // feeds every destination.
    static constexpr float kFreqCentsPerUnit = 1175.06f;
    static constexpr float kPwPerUnit        = 1.1695f;
    static constexpr float kFilterOctPerUnit = 12.91f;

    // LFO shape combinations. The three switches can be combined, but the
    // reference does not simply add the shapes: each combination has its own
    // gains and offset, measured as cents = D + saw*S + tri*T + square*Q over a
    // shared phase (fit residual under 1 cent, every combination at full wheel;
    // all three together from the half-wheel render, whose phase fit is clean).
    // Bases over the phase p in [0, 1): saw = p (rising, resets at p = 0);
    // square = 1 while p < 0.5, else 0; triangle = +0.5 at p = 0 falling to -0.5
    // at p = 0.5 and back. The saw reset, the square's rising edge and the
    // triangle's peak coincide (measured). Square and saw are unipolar, the
    // triangle is bipolar and carries a small positive offset. Values in bus
    // units (cents / 1175.06). Index: bit 0 saw, bit 1 triangle, bit 2 square.
    struct ShapeMix { float offset, saw, tri, square; };
    static constexpr ShapeMix kShapeMix[8] = {
        { 0.0f,      0.0f,      0.0f,     0.0f      },  // none: no LFO (measured flat)
        { 0.0f,      1.0f,      0.0f,     0.0f      },  // saw:  0 .. +1175 c
        { 0.036526f, 0.0f,      1.00660f, 0.0f      },  // tri:  -548 .. +634 c
        { 0.036458f, 1.588804f, 1.00660f, 0.0f      },  // saw + tri
        { 0.0f,      0.0f,      0.0f,     0.684297f },  // square: 0 / +804 c
        { 0.0f,      0.890508f, 0.0f,     0.533743f },  // saw + square
        { 0.038492f, 0.0f,      1.00660f, 1.083315f },  // tri + square
        { 0.040262f, 1.412982f, 1.00660f, 0.843012f },  // all three
    };

    // S&H: uniform random steps (excess kurtosis -1.20, 411 steps), one per LFO
    // cycle, taken at the saw reset; +-801 cents on oscillator B.
    static constexpr float kShHalfRange = 0.68167f;

    // Shaped noise. Three 160 s reference renders (48, 96 and 44.1 kHz; its noise
    // restarts identically at instance start, and each rate gives a different,
    // partly correlated realization), pooled: a zero-mean, NEGATIVELY SKEWED and
    // BOUNDED distribution, 70.87 cents RMS at full wheel, skew -0.956, excess
    // kurtosis 2.07, never below -480 or above +297 cents (the same shape at half
    // wheel, so it is the source, not the converter). Spectrum of the deviation,
    // dB re its 4-16 Hz level: flat from 2 Hz to 32 Hz, two real poles at 155 Hz
    // (-1.4 / -4.5 / -11.4 / -21.3 dB at 64 / 128 / 256 / 512 Hz), and a rise
    // below 1 Hz (+2.0 dB at 0.25 Hz).
    // Built as uniform white -> two one-pole lowpasses at kNoiseCornerHz -> a
    // unit-variance Gaussian x -> the measured QUANTILE MAP (kNoiseMap, the
    // reference's quantile at Phi(x), so the distribution and its bounds are the
    // measured ones; a two-parameter cubic matched skew and kurtosis but reached
    // -650 cents, half as far again as the reference ever goes) -> a first-order
    // low shelf (+4.1 dB below 0.2 Hz, least-squares fit to the 0.25..2 Hz bands).
    static constexpr float kNoiseCornerHz  = 155.0f;
    static constexpr float kNoiseShelfHz   = 0.20f;
    static constexpr float kNoiseShelfK    = 0.6032454f;  // DC gain 1 + K = +4.1 dB
    // Knots at x = -4, -3.75, ... +4 (33), in bus units at full depth; zero mean
    // under a unit Gaussian. The ends are the reference's observed extremes; x is
    // clamped to +-4 (one sample in 16000), where the map holds them.
    static constexpr int   kNoiseMapKnots = 33;
    static constexpr float kNoiseMapLo = -4.0f, kNoiseMapStep = 0.25f;
    static constexpr float kNoiseMap[kNoiseMapKnots] = {
        -0.408281f, -0.377236f, -0.362539f, -0.326167f, -0.277473f, -0.241220f, -0.205682f,
        -0.173874f, -0.145618f, -0.120597f, -0.096792f, -0.074820f, -0.055003f, -0.037144f,
        -0.020655f, -0.005805f,  0.008034f,  0.021118f,  0.033291f,  0.044670f,  0.055478f,
         0.065951f,  0.076329f,  0.086628f,  0.096993f,  0.107879f,  0.119094f,  0.131477f,
         0.144610f,  0.160992f,  0.178224f,  0.197902f,  0.252809f,
    };

    // A destination switched on or off while the bus is non-zero would step the
    // pitch, pulse width or cutoff on that sample; it fades over the LFO's own seek
    // crossfade length (5 ms, smoothstep, C1 at both ends). Nothing else fades:
    // shape and S&H switches change the bus at once, like the panel switches.
    static constexpr float kDestFadeSeconds = LFO::kSeekFadeSeconds;

    // ---- lifecycle (message thread) ----------------------------------------
    void prepare(double hostRate) noexcept
    {
        sr = hostRate > 0.0 ? hostRate : 44100.0;
        const double a = std::exp(-2.0 * 3.14159265358979323846 * (double)kNoiseCornerHz / sr);
        lpCoef = (float)(1.0 - a);
        // Two cascaded y += (1-a)(x - y) stages: sum of h^2 = (1-a)^4 (1+a^2)/(1-a^2)^3.
        // Uniform [-1, 1) has variance 1/3, so this gain makes the second stage's
        // output unit variance at every sample rate.
        const double one = 1.0 - a * a;
        const double hh = std::pow(1.0 - a, 4.0) * (1.0 + a * a) / (one * one * one);
        whiteGain = (float)std::sqrt(3.0 / hh);
        shelfCoef = (float)(1.0 - std::exp(-2.0 * 3.14159265358979323846 * (double)kNoiseShelfHz / sr));
        fadeStep = (float)(1.0 / ((double)kDestFadeSeconds * sr));
        depth.prepare(sr, kParamSmoothTau);
        mix.prepare(sr, kParamSmoothTau);
        setRate(rateHz);
        reset();
    }

    // Phase 0 (the reference starts its LFO there at instance start), fresh
    // random streams, nothing faded in. The next set() lands every smoothed value.
    void reset() noexcept
    {
        phase = 0.0;
        noiseRng.seed(0x5EED7A11u);
        shRng.seed(0x5A4D0E1Fu);
        lp1 = lp2 = shelf = 0.0f;
        shValue = kShHalfRange * shRng.nextBipolar();
        for (float& f : fade) f = 0.0f;
        primed = false;
        lastLfo = lastNoise = lastBus = 0.0f;
    }

    // ---- block rate (audio thread) -----------------------------------------
    // wheel01 is the mod wheel 0..1. snap lands the depth and mix smoothers and
    // the destination fades on their targets instead of gliding (a program
    // change, or the first block after reset, which snaps regardless).
    void set(const WheelModSettings& s, float wheel01, bool snap) noexcept
    {
        if (std::isfinite(s.rateHz)) setRate(s.rateHz);
        shape = kShapeMix[(s.saw ? 1 : 0) | (s.tri ? 2 : 0) | (s.square ? 4 : 0)];
        useSH = s.sampleAndHold;

        const float w  = clamp01(wheel01);
        const float lo = clamp01(s.lowerLimit), hi = clamp01(s.upperLimit);
        const float m  = clamp01(s.sourceMix);
        const float d  = lo + (hi - lo) * w;
        depth.setTarget(d);
        mix.setTarget(m);

        enabled[kFreqA] = s.freqA; enabled[kFreqB] = s.freqB;
        enabled[kPwA] = s.pwA;     enabled[kPwB] = s.pwB;
        enabled[kFilter] = s.filter;

        if (snap || !primed)
        {
            depth.snap(d);
            mix.snap(m);
            for (int k = 0; k < kNumDest; ++k) fade[k] = enabled[k] ? 1.0f : 0.0f;
            primed = true;
        }
    }

    enum Dest { kFreqA = 0, kFreqB, kPwA, kPwB, kFilter, kNumDest };

    // True while any destination is switched on or still fading out. While it is
    // false the frame is all +0.0f and the voices must not apply it (OFF CONTRACT).
    bool active() const noexcept
    {
        for (int d = 0; d < kNumDest; ++d)
            if (destActive((Dest)d)) return true;
        return false;
    }
    // The same for one destination: its frame field is +0.0f while this is false.
    bool destActive(Dest d) const noexcept { return enabled[d] || fade[d] > 0.0f; }

    // ---- sample rate (audio thread) ----------------------------------------
    // One host sample. Every source advances whether or not a destination is on,
    // so the LFO keeps free-running and switching a destination on lands on the
    // phase it would have had.
    WheelModFrame process() noexcept
    {
        // LFO and S&H at the current phase, then advance. The S&H takes its new
        // value as the phase wraps, so it steps on the same sample as the saw
        // reset (read before the advance, or it would lead the reset by one).
        const float p = (float)phase;
        const float sq  = p < 0.5f ? 1.0f : 0.0f;
        const float tri = p < 0.5f ? 0.5f - 2.0f * p : 2.0f * p - 1.5f;
        const float lfo = shape.offset + shape.saw * p + shape.tri * tri + shape.square * sq;
        const float held = shValue;
        phase += phaseInc;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            if (!(phase < 1.0)) phase = 0.0;   // cannot happen at kRateMaxHz; NaN-proof
            shValue = kShHalfRange * shRng.nextBipolar();
        }

        // Shaped noise (always advanced, so the stream never depends on the mix).
        lp1 += lpCoef * (whiteGain * noiseRng.nextBipolar() - lp1);
        lp2 += lpCoef * (lp1 - lp2);
        const float y = noiseMap(lp2);
        shelf += shelfCoef * (y - shelf);
        const float noise = y + kNoiseShelfK * shelf;

        const float src = useSH ? held : noise;
        const float m = mix.next();
        const float bus = (1.0f - m) * lfo + m * src;   // exact at m = 0 and m = 1
        const float s = bus * depth.next();
        lastLfo = lfo; lastNoise = noise; lastBus = bus;

        WheelModFrame out;
        out.freqACents = destOut(kFreqA, s * kFreqCentsPerUnit);
        out.freqBCents = destOut(kFreqB, s * kFreqCentsPerUnit);
        out.pwA        = destOut(kPwA,   s * kPwPerUnit);
        out.pwB        = destOut(kPwB,   s * kPwPerUnit);
        out.filterOct  = destOut(kFilter, s * kFilterOctPerUnit);
        return out;
    }

    // ---- observables (tests; the UI may light an LFO lamp from phase) ----------
    double lfoPhase() const noexcept   { return phase; }
    float  lastLfoValue() const noexcept   { return lastLfo; }    // bus units, before the mix
    float  lastNoiseValue() const noexcept { return lastNoise; }  // shaped noise, bus units
    float  lastBusValue() const noexcept   { return lastBus; }    // after the mix, before depth
    float  depthValue() const noexcept     { return depth.value(); }

private:
    // The quantile map, linear between knots; x clamped to the map's range.
    static float noiseMap(float x) noexcept
    {
        const float u = (clampf(x, kNoiseMapLo, -kNoiseMapLo) - kNoiseMapLo) * (1.0f / kNoiseMapStep);
        const int   i = clampi((int)u, 0, kNoiseMapKnots - 2);
        const float f = u - (float)i;
        return kNoiseMap[i] + f * (kNoiseMap[i + 1] - kNoiseMap[i]);
    }

    static float clamp01(float v) noexcept { return std::isfinite(v) ? clampf(v, 0.0f, 1.0f) : 0.0f; }

    void setRate(float hz) noexcept
    {
        rateHz = clampf(hz, kRateMinHz, kRateMaxHz);
        phaseInc = (double)rateHz / sr;
    }

    // Advance destination d's fade one sample and scale its value; exactly +0.0f
    // once a switched-off destination has faded out.
    float destOut(int d, float v) noexcept
    {
        float& f = fade[d];
        if (enabled[d]) { if (f < 1.0f) { f += fadeStep; if (f > 1.0f) f = 1.0f; } }
        else if (f > 0.0f) { f -= fadeStep; if (f < 0.0f) f = 0.0f; }
        if (f <= 0.0f) return 0.0f;
        return f >= 1.0f ? v : v * (f * f * (3.0f - 2.0f * f));
    }

    double sr = 44100.0;
    double phase = 0.0;        // double: at 0.03 Hz / 192 kHz a float accumulator rounds the step
    double phaseInc = 0.0;
    float  rateHz = 0.8866f;
    ShapeMix shape = kShapeMix[2];
    bool   useSH = false;

    Xorshift noiseRng, shRng;
    float lpCoef = 0.0f, whiteGain = 1.0f, shelfCoef = 0.0f;
    float lp1 = 0.0f, lp2 = 0.0f, shelf = 0.0f;
    float shValue = 0.0f;

    duskaudio::SmoothedValue depth, mix;
    bool  enabled[kNumDest] = {};
    float fade[kNumDest] = {};
    float fadeStep = 1.0f;
    bool  primed = false;

    float lastLfo = 0.0f, lastNoise = 0.0f, lastBus = 0.0f;
};

} // namespace msynth

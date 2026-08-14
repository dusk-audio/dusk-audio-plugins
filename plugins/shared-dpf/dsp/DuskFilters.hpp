// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// DuskFilters.hpp — framework-free filter primitives.
//
// OnePoleLP/HP + DCBlocker are lifted verbatim from
// plugins/tape-echo/core/TapeEchoDSP.hpp. Biquad generalizes tape-echo's
// ShelfFilter into a transposed-direct-form-II biquad with a full set of
// coefficient designers matching juce::dsp::IIR bit-for-bit:
//   - firstOrderHighPass / firstOrderAllPass  (order-1, b2=a2=0)
//   - lowPass / highPass (Q)                  (JUCE bilinear form)
//   - lowShelf / highShelf / peak (Q, gainDb) (RBJ cookbook, alpha=sinw/2Q)
// JUCE's Filter::processSample is TDF-II transposed with a0-normalized
// coefficients [b0,b1,b2,a1,a2]; process() below is the identical recurrence,
// so identical coefficients reproduce JUCE sample-for-sample.

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace duskaudio
{

constexpr float kDuskTwoPi = 6.28318530717958647692f;
constexpr float kDuskPi    = 3.14159265358979323846f;

//==============================================================================
// Nyquist guard for biquad DESIGN frequencies.
//
// Every designer below prewarps through tan(pi*f/fs) or cos/sin(2*pi*f/fs),
// which are only meaningful for f < fs/2. Past Nyquist the mapping wraps and
// the coefficients stop describing the intended filter:
//   - tan(pi*f/fs) grows without bound as f approaches fs/2. It does NOT reach
//     Inf: pi/2 is not representable in double, so tan tops out near 1.6e16
//     (measured) and the reciprocals below stay finite. What it produces is a
//     corner nowhere near the one requested;
//   - at f == 0.75*fs tan passes through exactly -1, where firstOrderLowPass's
//     (k+1) and firstOrderAllPass's (1+t) are zero. This one IS a divide by zero;
//   - sin(w0) goes negative across (0.5*fs, fs), so alpha goes negative, so the
//     RBJ denominator (1 + alpha/A) shrinks or flips sign and the poles land
//     OUTSIDE the unit circle. The filter then diverges geometrically until the
//     state overflows to +/-Inf, at which point a transposed-direct-form-II
//     update evaluating Inf - Inf yields NaN.
// The third path is how 4K EQ 2 produced a NaN at a 1234.57 Hz host rate under
// clap-validator's process-varying-sample-rates: its band corners (up to 20 kHz)
// sit far above Nyquist once the host rate drops that low.
//
// NaN is the loud failure, not the only one, and not the common one. Where a host
// carries its own finiteness guard (Multi-Q's StereoBiquad and CytomicSVF zero the
// state and return 0.0 on a non-finite sample) the same divergence never reaches
// the output at all: it surfaces as repeated state resets, heard as dropouts or
// crackle, and every output-level check sails straight past it. Do not treat a
// clean NaN sweep as evidence that a designer is sound.
//
// Clamping HERE rather than at each call site is deliberate: this header is
// compiled into every DPF plugin, and a guard that a call site can forget is a
// guard that will be forgotten. A few call sites still clamp on their own and are
// commented where they do, because they are NOT redundant with this one: they
// either feed a downstream derivation (TapeMachine's hfCutoff) or deliberately cap
// tighter than this ceiling as part of the voicing (TapeMachine's recordHeadCutoff
// and biasFilter, TapeEcho's 0.45*fs sites). Everything else relies on this.
//
// The three NAMED clamp helpers in the tree now all delegate to
// nyquistSafeDesignHzD below at their own ceiling, rather than re-implementing
// the comparison: TapeMachine 2's nyquistSafeHz, TapeEcho's
// safeBiquadFrequency, and the JUCE TapeMachine v1's nyquistSafeHz are thin
// aliases. Do not add a fourth implementation; alias this one.
//
// NOT yet covered, so do not read the above as "every clamp in the repo":
// DuskVerb (a JUCE plugin that does not compile this header) still clamps
// inline at roughly thirty sites, as std::min(fc, 0.45f * sr) or
// std::clamp(fc, 20.0f, 0.45f * sr) -- ceiling-only, in float, and mostly
// without the 1 Hz floor. Those are one-pole/TPT coefficients rather than RBJ
// biquads, so the failure mode differs, but they are the remaining divergent
// copies. Changing the ceiling here does NOT change them.
//
// WHAT THE CEILING BUYS, AND WHAT IT DOES NOT. It trades divergence for a corner
// parked just under Nyquist. At the ceiling the poles sit at |z| ~ 0.999, which
// rings for on the order of a thousand samples rather than blowing up. That is an
// unambiguous improvement on NaN, but it is NOT a voicing-safe clamp: a filter
// whose corner has been pulled down to the ceiling is no longer the filter that was
// asked for. The ceiling exists so degenerate rates stay bounded, not so they sound
// right. Keep design frequencies below it by construction wherever tone matters.
//
// CEILING: kMaxDesignFreqRatio below. It is deliberately TIGHT to Nyquist, and it
// is NOT the same number as the 0.45 * fs ceiling inside TapeMachine's private
// DBiquad. Do not "harmonize" them; each is set by its own hard constraint:
//
//   THIS header (0.4998): 4K EQ 2 and Multi-Q 2 design at the HOST rate (their
//     oversampling defaults to 1x) and their LPF parameter tops out at 20000 Hz.
//     20000 / 44100 = 0.4535, so any ceiling at or below 0.4535 would detune a
//     wide-open LPF at 44.1 kHz: an audible regression in shipping plugins. The
//     ceiling must therefore stay >= 0.4535. 0.4998 is the value FourKEQDSP.cpp's
//     LPF clamp used before this guard subsumed it, and the value Multi-Q's own
//     amb::clampFreq uses, so it keeps one number across the family. At 44.1 kHz
//     it is 22041 Hz, above
//     every design frequency any of these plugins can request at any supported
//     rate (highest: the 16 kHz British HF band, 0.363 of fs at 44.1 kHz).
//
//   TapeMachine's DBiquad (0.45): that core runs at 2x the host rate, so its
//     highest corner ratio is ~0.227 and the margin is enormous. Its 0.45 matches
//     the safeFreq / maxFilterFreq convention that file was already written to.
//
// Stability at this ceiling is verified, not assumed: w0 = 0.9996*pi keeps
// sin(w0) > 0, so alpha > 0 and |pole| < 1 for every RBJ designer here, and
// tan(0.4998*pi) is ~1591, large but far from overflow in float.
//
// PRECONDITION: fs is positive and finite. Every caller's prepare path already
// enforces that, and re-validating it here would be theatre: fs is a divisor in
// every designer below, so a non-positive or non-finite fs produces NaN
// coefficients no matter what frequency this function hands back.
constexpr double kMaxDesignFreqRatio = 0.4998;

// The ONE implementation of the clamp ordering. TapeMachine's DBiquad guard
// (nyquistSafeHz) is a thin alias over this at its own ceiling, so the two cannot
// drift apart.
//
// maxFraction has NO default on purpose. It is a FRACTION of fs, and a defaulted
// parameter of that shape invites a caller to pass a frequency in Hz and silently
// get a ceiling of fs * 12000. Callers state the ceiling they mean; the float
// wrapper below is the one that binds kMaxDesignFreqRatio.
inline double nyquistSafeDesignHzD(double fs, double freq, double maxFraction) noexcept
{
    // Floor FIRST, ceiling LAST. The reverse order lets the 1 Hz floor win at
    // absurdly low fs and hand the designer a corner at or above Nyquist, which
    // is the exact instability this guard exists to prevent. The order is also
    // what makes a NaN freq land on the ceiling instead of propagating: both
    // comparisons are false, so max returns the NaN and min then returns fs*frac.
    return std::min(fs * maxFraction, std::max(freq, 1.0));
}

inline float nyquistSafeDesignHz(double fs, float freq) noexcept
{
    return (float)nyquistSafeDesignHzD(fs, (double)freq, kMaxDesignFreqRatio);
}

//==============================================================================
class OnePoleLP
{
public:
    void  setCutoff(float hz, double fs) noexcept { c = 1.0f - std::exp(-kDuskTwoPi * hz / (float)fs); }
    void  reset() noexcept { z = 0.0f; }
    float process(float x) noexcept { z += c * (x - z); return z; }

private:
    float c = 1.0f, z = 0.0f;
};

class OnePoleHP
{
public:
    void  setCutoff(float hz, double fs) noexcept { lp.setCutoff(hz, fs); }
    void  reset() noexcept { lp.reset(); }
    float process(float x) noexcept { return x - lp.process(x); }

private:
    OnePoleLP lp;
};

class DCBlocker
{
public:
    // Derive the pole from a target cutoff so the corner TRACKS the sample rate.
    // A fixed pole raises the effective cutoff as fs rises (0.9975 is ~20 Hz at
    // 48 kHz but ~40 Hz at 96 kHz), over-attenuating the low end. Call once in
    // prepare()/reset(); callers that never set a rate keep the ~20 Hz @ 48 kHz
    // default below.
    void  setSampleRate(double fs, float cutoffHz = 20.0f) noexcept
    {
        R = std::exp(-kDuskTwoPi * cutoffHz / (float)fs);
    }
    void  reset() noexcept { x1 = y1 = 0.0f; }
    float process(float x) noexcept
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float R  = 0.9975f;   // ~20 Hz @ 48 kHz until setSampleRate() is called
    float x1 = 0.0f, y1 = 0.0f;
};

//==============================================================================
// Normalized biquad coefficients (a0 == 1). Layout matches JUCE's stored
// coefficient array [b0, b1, b2, a1, a2].
struct BiquadCoeffs
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

class Biquad
{
public:
    void setCoeffs(const BiquadCoeffs& k) noexcept { c = k; }
    const BiquadCoeffs& coeffs() const noexcept    { return c; }
    void reset() noexcept { z1 = z2 = 0.0f; }

    // Transposed Direct Form II — identical recurrence to juce::dsp::IIR::Filter.
    float process(float x) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    // Linear magnitude of H(e^{jw}) at normalized angular frequency w = 2*pi*f/fs.
    // Evaluated on the UI thread for response-curve drawing (never probes audio).
    double magnitude(double w) const noexcept
    {
        const double cw = std::cos(w), sw = std::sin(w);
        const double c2w = std::cos(2.0 * w), s2w = std::sin(2.0 * w);
        const double nr = c.b0 + c.b1 * cw + c.b2 * c2w;
        const double ni = -(c.b1 * sw + c.b2 * s2w);
        const double dr = 1.0 + c.a1 * cw + c.a2 * c2w;
        const double di = -(c.a1 * sw + c.a2 * s2w);
        const double num = nr * nr + ni * ni;
        const double den = dr * dr + di * di;
        return den > 0.0 ? std::sqrt(num / den) : 0.0;
    }

    // Complex H(e^{jw}). Needed by parallel-summing EQs where band responses are
    // summed as complex numbers (1 + sum K_i H_i) before taking the magnitude.
    std::complex<double> response(double w) const noexcept
    {
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
        const std::complex<double> num = (double)c.b0 + (double)c.b1 * z1 + (double)c.b2 * z2;
        const std::complex<double> den = 1.0 + (double)c.a1 * z1 + (double)c.a2 * z2;
        return num / den;
    }

    //--- coefficient designers (all a0-normalized, matching juce::dsp::IIR) ----

    // JUCE ArrayCoefficients::makeFirstOrderHighPass -> {1, -1, n+1, n-1}
    static BiquadCoeffs firstOrderHighPass(double fs, float freq) noexcept
    {
        freq = nyquistSafeDesignHz(fs, freq);
        const float n = std::tan(kDuskPi * freq / (float)fs);
        const float inv = 1.0f / (n + 1.0f);
        return { inv, -inv, 0.0f, (n - 1.0f) * inv, 0.0f };
    }

    // First-order allpass: a1 = (1 - tan(w0/2)) / (1 + tan(w0/2)).
    // Matches FourKEQ TransformerPhaseShift::setFrequency.
    static BiquadCoeffs firstOrderAllPass(double fs, float freq) noexcept
    {
        freq = nyquistSafeDesignHz(fs, freq);
        const float w0 = kDuskTwoPi * freq / (float)fs;
        const float t  = std::tan(w0 * 0.5f);
        const float a  = (1.0f - t) / (1.0f + t);
        return { a, 1.0f, 0.0f, a, 0.0f };
    }

    // First-order low-pass: unity DC gain, -> 0 at Nyquist, 6 dB/oct. Used as a
    // parallel-shelf building block (dry + K*LP = first-order low shelf).
    static BiquadCoeffs firstOrderLowPass(double fs, float freq) noexcept
    {
        freq = nyquistSafeDesignHz(fs, freq);
        const float k = std::tan(kDuskPi * freq / (float)fs);
        const float inv = 1.0f / (k + 1.0f);
        return { k * inv, k * inv, 0.0f, (k - 1.0f) * inv, 0.0f };
    }

    // RBJ constant-0-dB-peak band-pass: |H(fc)| = 1, -> 0 at DC and Nyquist.
    // The parallel EQ building block for peaks/bells (dry + K*BP = bell).
    static BiquadCoeffs bandPassConstantPeak(double fs, float freq, float Q) noexcept
    {
        freq = nyquistSafeDesignHz(fs, freq);
        const float w0 = kDuskTwoPi * freq / (float)fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * Q);
        const float inv = 1.0f / (1.0f + alpha);
        return { alpha * inv, 0.0f, -alpha * inv, -2.0f * cw * inv, (1.0f - alpha) * inv };
    }

    // JUCE ArrayCoefficients::makeLowPass(fs, freq, Q).
    static BiquadCoeffs lowPass(double fs, float freq, float Q) noexcept
    {
        freq = nyquistSafeDesignHz(fs, freq);
        const float n = 1.0f / std::tan(kDuskPi * freq / (float)fs);
        const float nSq = n * n;
        const float invQ = 1.0f / Q;
        const float c1 = 1.0f / (1.0f + invQ * n + nSq);
        return { c1, c1 * 2.0f, c1, c1 * 2.0f * (1.0f - nSq), c1 * (1.0f - invQ * n + nSq) };
    }

    // JUCE ArrayCoefficients::makeHighPass(fs, freq, Q).
    static BiquadCoeffs highPass(double fs, float freq, float Q) noexcept
    {
        freq = nyquistSafeDesignHz(fs, freq);
        const float n = std::tan(kDuskPi * freq / (float)fs);
        const float nSq = n * n;
        const float invQ = 1.0f / Q;
        const float c1 = 1.0f / (1.0f + invQ * n + nSq);
        return { c1, c1 * -2.0f, c1, c1 * 2.0f * (nSq - 1.0f), c1 * (1.0f - invQ * n + nSq) };
    }

    // RBJ peaking EQ, alpha = sin(w0)/(2Q). Matches FourKEQ::makeConsolePeak raw math.
    static BiquadCoeffs peak(double fs, float freq, float gainDb, float Q) noexcept
    {
        freq = nyquistSafeDesignHz(fs, freq);
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = kDuskTwoPi * freq / (float)fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * Q);
        const float b0 = 1.0f + alpha * A;
        const float b1 = -2.0f * cw;
        const float b2 = 1.0f - alpha * A;
        const float a0 = 1.0f + alpha / A;
        const float a1 = -2.0f * cw;
        const float a2 = 1.0f - alpha / A;
        const float inv = 1.0f / a0;
        return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
    }

    // RBJ low/high shelf, alpha = sin(w0)/(2Q). Matches FourKEQ::makeConsoleShelf.
    static BiquadCoeffs shelf(double fs, float freq, float gainDb, float Q, bool high) noexcept
    {
        freq = nyquistSafeDesignHz(fs, freq);
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = kDuskTwoPi * freq / (float)fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * Q);
        const float sqA2a = 2.0f * std::sqrt(A) * alpha;
        float b0, b1, b2, a0, a1, a2;
        if (high)
        {
            b0 =  A * ((A + 1.0f) + (A - 1.0f) * cw + sqA2a);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
            b2 =  A * ((A + 1.0f) + (A - 1.0f) * cw - sqA2a);
            a0 =      (A + 1.0f) - (A - 1.0f) * cw + sqA2a;
            a1 =  2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
            a2 =      (A + 1.0f) - (A - 1.0f) * cw - sqA2a;
        }
        else
        {
            b0 =  A * ((A + 1.0f) - (A - 1.0f) * cw + sqA2a);
            b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
            b2 =  A * ((A + 1.0f) - (A - 1.0f) * cw - sqA2a);
            a0 =      (A + 1.0f) + (A - 1.0f) * cw + sqA2a;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
            a2 =      (A + 1.0f) + (A - 1.0f) * cw - sqA2a;
        }
        const float inv = 1.0f / a0;
        return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
    }

    // RBJ low/high shelf in shelf-slope form with S=1 (alpha = sin(w0)/2 * sqrt(2)).
    // Deliberately a DIFFERENT float op-order than shelf(...,Q) above: at Q=1/sqrt(2)
    // the two are algebraically equal but diverge by ~1 ULP in ~40% of frequencies,
    // so this exact variant is kept to reproduce tape-echo's original ShelfFilter
    // bit-for-bit (offline null-render requirement). high=false selects the low shelf.
    static BiquadCoeffs shelfSlope1(double fs, float freq, float gainDb, bool high) noexcept
    {
        freq = nyquistSafeDesignHz(fs, freq);
        const float A     = std::pow(10.0f, gainDb / 40.0f);
        const float w0    = kDuskTwoPi * freq / (float)fs;
        const float cosw  = std::cos(w0);
        const float sinw  = std::sin(w0);
        const float alpha = 0.5f * sinw * std::sqrt(2.0f); // S = 1
        const float sqA2a = 2.0f * std::sqrt(A) * alpha;

        float b0f, b1f, b2f, a0f, a1f, a2f;
        if (!high)
        {
            b0f =     A * ((A + 1) - (A - 1) * cosw + sqA2a);
            b1f = 2 * A * ((A - 1) - (A + 1) * cosw);
            b2f =     A * ((A + 1) - (A - 1) * cosw - sqA2a);
            a0f =         (A + 1) + (A - 1) * cosw + sqA2a;
            a1f =    -2 * ((A - 1) + (A + 1) * cosw);
            a2f =         (A + 1) + (A - 1) * cosw - sqA2a;
        }
        else
        {
            b0f =     A * ((A + 1) + (A - 1) * cosw + sqA2a);
            b1f =-2 * A * ((A - 1) + (A + 1) * cosw);
            b2f =     A * ((A + 1) + (A - 1) * cosw - sqA2a);
            a0f =         (A + 1) - (A - 1) * cosw + sqA2a;
            a1f =     2 * ((A - 1) - (A + 1) * cosw);
            a2f =         (A + 1) - (A - 1) * cosw - sqA2a;
        }
        const float inv = 1.0f / a0f;
        return { b0f * inv, b1f * inv, b2f * inv, a1f * inv, a2f * inv };
    }

private:
    BiquadCoeffs c;
    float z1 = 0.0f, z2 = 0.0f;
};

} // namespace duskaudio

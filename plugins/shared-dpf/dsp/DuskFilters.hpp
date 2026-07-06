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

#include <cmath>
#include <complex>

namespace duskaudio
{

constexpr float kDuskTwoPi = 6.28318530717958647692f;
constexpr float kDuskPi    = 3.14159265358979323846f;

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
        const float n = std::tan(kDuskPi * freq / (float)fs);
        const float inv = 1.0f / (n + 1.0f);
        return { inv, -inv, 0.0f, (n - 1.0f) * inv, 0.0f };
    }

    // First-order allpass: a1 = (1 - tan(w0/2)) / (1 + tan(w0/2)).
    // Matches FourKEQ TransformerPhaseShift::setFrequency.
    static BiquadCoeffs firstOrderAllPass(double fs, float freq) noexcept
    {
        const float w0 = kDuskTwoPi * freq / (float)fs;
        const float t  = std::tan(w0 * 0.5f);
        const float a  = (1.0f - t) / (1.0f + t);
        return { a, 1.0f, 0.0f, a, 0.0f };
    }

    // First-order low-pass: unity DC gain, -> 0 at Nyquist, 6 dB/oct. Used as a
    // parallel-shelf building block (dry + K*LP = first-order low shelf).
    static BiquadCoeffs firstOrderLowPass(double fs, float freq) noexcept
    {
        const float k = std::tan(kDuskPi * freq / (float)fs);
        const float inv = 1.0f / (k + 1.0f);
        return { k * inv, k * inv, 0.0f, (k - 1.0f) * inv, 0.0f };
    }

    // RBJ constant-0-dB-peak band-pass: |H(fc)| = 1, -> 0 at DC and Nyquist.
    // The parallel EQ building block for peaks/bells (dry + K*BP = bell).
    static BiquadCoeffs bandPassConstantPeak(double fs, float freq, float Q) noexcept
    {
        const float w0 = kDuskTwoPi * freq / (float)fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * Q);
        const float inv = 1.0f / (1.0f + alpha);
        return { alpha * inv, 0.0f, -alpha * inv, -2.0f * cw * inv, (1.0f - alpha) * inv };
    }

    // JUCE ArrayCoefficients::makeLowPass(fs, freq, Q).
    static BiquadCoeffs lowPass(double fs, float freq, float Q) noexcept
    {
        const float n = 1.0f / std::tan(kDuskPi * freq / (float)fs);
        const float nSq = n * n;
        const float invQ = 1.0f / Q;
        const float c1 = 1.0f / (1.0f + invQ * n + nSq);
        return { c1, c1 * 2.0f, c1, c1 * 2.0f * (1.0f - nSq), c1 * (1.0f - invQ * n + nSq) };
    }

    // JUCE ArrayCoefficients::makeHighPass(fs, freq, Q).
    static BiquadCoeffs highPass(double fs, float freq, float Q) noexcept
    {
        const float n = std::tan(kDuskPi * freq / (float)fs);
        const float nSq = n * n;
        const float invQ = 1.0f / Q;
        const float c1 = 1.0f / (1.0f + invQ * n + nSq);
        return { c1, c1 * -2.0f, c1, c1 * 2.0f * (nSq - 1.0f), c1 * (1.0f - invQ * n + nSq) };
    }

    // RBJ peaking EQ, alpha = sin(w0)/(2Q). Matches FourKEQ::makeConsolePeak raw math.
    static BiquadCoeffs peak(double fs, float freq, float gainDb, float Q) noexcept
    {
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

private:
    BiquadCoeffs c;
    float z1 = 0.0f, z2 = 0.0f;
};

} // namespace duskaudio

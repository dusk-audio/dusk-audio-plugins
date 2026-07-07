// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQFilters.hpp — framework-free filter primitives for the Multi-Q DSP core.
//
// Verbatim ports of the JUCE build's filter math (MultiQ.h structs +
// AnalogMatchedBiquad.h), stripped of JUCE types so the core has zero framework
// dependency. The COEFFICIENT MATH and the per-sample RECURRENCES are byte-for-
// byte identical to the JUCE originals — proven by the null harness in
// plugins/multi-q/tests/test_multiq_core_filters.cpp. Do NOT "simplify" the op
// order: the A/B against the JUCE build depends on it (double intermediates,
// float storage, exact normalisation).
//
// Deliberately dropped vs the JUCE header: MqBiquadCoeffs::applyToFilter (wrote
// into a juce::dsp::IIR::Filter — the core drives StereoBiquad directly).

#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace duskaudio
{

// juce::MathConstants<double>::pi — same literal, so magnitude/coeff math is
// bit-identical to the JUCE build.
inline constexpr double kMultiQPi = 3.14159265358979323846;

// Bitwise finite check (verbatim from SafeFloat.h) — immune to -ffast-math /
// finite-math-only optimizations that make std::isfinite unreliable.
inline bool safeIsFinite(float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

//==============================================================================
// Biquad coefficient storage with magnitude evaluation (no heap allocation).
// Stored normalized: {b0/a0, b1/a0, b2/a0, 1, a1/a0, a2/a0}.
struct MqBiquadCoeffs
{
    float coeffs[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};

    // Evaluate |H(e^jw)| at the given frequency (returns linear magnitude).
    double getMagnitudeForFrequency(double freq, double sampleRate) const
    {
        double w = 2.0 * kMultiQPi * freq / sampleRate;
        double cosw = std::cos(w);
        double cos2w = 2.0 * cosw * cosw - 1.0;
        double sinw = std::sin(w);
        double sin2w = 2.0 * sinw * cosw;

        double numR = coeffs[0] + coeffs[1] * cosw + coeffs[2] * cos2w;
        double numI = -(coeffs[1] * sinw + coeffs[2] * sin2w);
        double denR = coeffs[3] + coeffs[4] * cosw + coeffs[5] * cos2w;
        double denI = -(coeffs[4] * sinw + coeffs[5] * sin2w);

        double numMagSq = numR * numR + numI * numI;
        double denMagSq = denR * denR + denI * denI;

        if (denMagSq < 1e-20) return 1.0;
        return std::sqrt(numMagSq / denMagSq);
    }

    void setIdentity()
    {
        coeffs[0] = 1.0f; coeffs[1] = 0.0f; coeffs[2] = 0.0f;
        coeffs[3] = 1.0f; coeffs[4] = 0.0f; coeffs[5] = 0.0f;
    }
};

//==============================================================================
// Cytomic SVF (Andrew Simper's "Linear Trapezoidal Integrated SVF").
// Used for the Digital dynamic-gain filters. NOTE: this is a DIFFERENT topology
// and coefficient set from shared-dpf DuskSVF (JUCE-TPT {g,R2,h}); do not
// substitute — the dynamic-EQ response depends on this exact form.
struct SVFCoeffs
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;  // SVF core
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;  // Mix: out = m0*x + m1*v1 + m2*v2

    void setIdentity()
    {
        a1 = 1.0f; a2 = 0.0f; a3 = 0.0f;
        m0 = 1.0f; m1 = 0.0f; m2 = 0.0f;
    }
};

struct CytomicSVF
{
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
    SVFCoeffs coeffs;
    SVFCoeffs target;
    float smoothCoeff = 0.02f;
    bool converged = true;

    void reset()
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    void setTarget(const SVFCoeffs& t)
    {
        target = t;
        converged = false;
    }

    void snapToTarget()
    {
        coeffs = target;
        converged = true;
    }

    void setSmoothCoeff(float c) { smoothCoeff = std::clamp(c, 0.0f, 1.0f); }

    void stepSmoothing()
    {
        if (!converged)
        {
            float s = smoothCoeff;
            coeffs.a1 += s * (target.a1 - coeffs.a1);
            coeffs.a2 += s * (target.a2 - coeffs.a2);
            coeffs.a3 += s * (target.a3 - coeffs.a3);
            coeffs.m0 += s * (target.m0 - coeffs.m0);
            coeffs.m1 += s * (target.m1 - coeffs.m1);
            coeffs.m2 += s * (target.m2 - coeffs.m2);

            constexpr float eps = 1e-7f;
            if (std::abs(coeffs.a1 - target.a1) < eps &&
                std::abs(coeffs.a2 - target.a2) < eps &&
                std::abs(coeffs.a3 - target.a3) < eps &&
                std::abs(coeffs.m0 - target.m0) < eps &&
                std::abs(coeffs.m1 - target.m1) < eps &&
                std::abs(coeffs.m2 - target.m2) < eps)
            {
                coeffs = target;
                converged = true;
            }
        }
    }

    float processSample(float x)
    {
        if (!safeIsFinite(x))
            x = 0.0f;

        float v3 = x - ic2eq;
        float v1 = coeffs.a1 * ic1eq + coeffs.a2 * v3;
        float v2 = ic2eq + coeffs.a2 * ic1eq + coeffs.a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        if (!safeIsFinite(ic1eq) || !safeIsFinite(ic2eq))
        {
            ic1eq = 0.0f;
            ic2eq = 0.0f;
            return 0.0f;
        }

        return coeffs.m0 * x + coeffs.m1 * v1 + coeffs.m2 * v2;
    }
};

struct StereoSVF
{
    CytomicSVF filterL;
    CytomicSVF filterR;

    void reset() { filterL.reset(); filterR.reset(); }
    void setTarget(const SVFCoeffs& c) { filterL.setTarget(c); filterR.setTarget(c); }
    void snapToTarget() { filterL.snapToTarget(); filterR.snapToTarget(); }
    void setSmoothCoeff(float c) { filterL.setSmoothCoeff(c); filterR.setSmoothCoeff(c); }

    // Advance smoothing on BOTH channels so the idle side converges even when
    // routing processes only one channel per sample.
    void stepSmoothing() { filterL.stepSmoothing(); filterR.stepSmoothing(); }

    float processSampleL(float s) { return filterL.processSample(s); }
    float processSampleR(float s) { return filterR.processSample(s); }
};

//==============================================================================
// Stereo Direct Form II Transposed biquad with per-sample coefficient ramp.
// Used for the Digital static bands (AnalogMatchedBiquad coeffs → cramping-free,
// sample-rate-independent shape). setSmoothCoeff sets the per-sample decay.
struct StereoBiquad
{
    MqBiquadCoeffs coeffs;
    MqBiquadCoeffs target;
    float smoothCoeff = 0.02f;
    float s1L = 0.0f, s2L = 0.0f;
    float s1R = 0.0f, s2R = 0.0f;

    void setCoeffs(const MqBiquadCoeffs& c) { target = c; }
    void reset() { s1L = s2L = s1R = s2R = 0.0f; coeffs = target; }
    void snapToTarget() { coeffs = target; }
    void setSmoothCoeff(float c) { smoothCoeff = c; }

    void stepSmoothing()
    {
        for (int i = 0; i < 6; ++i)
            coeffs.coeffs[i] += smoothCoeff * (target.coeffs[i] - coeffs.coeffs[i]);
    }

    float processSampleL(float x)
    {
        if (!safeIsFinite(x)) x = 0.0f;
        float y = coeffs.coeffs[0] * x + s1L;
        s1L = coeffs.coeffs[1] * x - coeffs.coeffs[4] * y + s2L;
        s2L = coeffs.coeffs[2] * x - coeffs.coeffs[5] * y;
        if (!safeIsFinite(y)) { s1L = s2L = 0.0f; return 0.0f; }
        return y;
    }

    float processSampleR(float x)
    {
        if (!safeIsFinite(x)) x = 0.0f;
        float y = coeffs.coeffs[0] * x + s1R;
        s1R = coeffs.coeffs[1] * x - coeffs.coeffs[4] * y + s2R;
        s2R = coeffs.coeffs[2] * x - coeffs.coeffs[5] * y;
        if (!safeIsFinite(y)) { s1R = s2R = 0.0f; return 0.0f; }
        return y;
    }
};

//==============================================================================
// Analog-matched biquad coefficient designers — cramping-free at all sample
// rates. Verbatim from AnalogMatchedBiquad.h (Orfanidis bandwidth-matched +
// RBJ shelf/HP/LP). Pre-warps BANDWIDTH (kbw = tan(pi*bw/sr)) for peak/notch/BP
// and turnover (k = tan(pi*fc/sr)) for shelves/HP/LP.
//
// ⚠ Do NOT swap these for shared-dpf DuskFilters::peak/shelf — those use RBJ
// alpha = sin(w0)/2Q, a different transfer function that CRAMS near Nyquist.
namespace amb
{
    static inline double clampFreq(double fc, double sr)
    {
        return std::max(1.0, std::min(fc, sr * 0.4998));
    }

    static void computePeaking(MqBiquadCoeffs& c, double fc, double sr,
                                double gainDB, double Q)
    {
        fc = clampFreq(fc, sr);
        Q  = std::max(0.01, Q);

        if (std::abs(gainDB) < 0.01) { c.setIdentity(); return; }

        const double W0d  = 2.0 * kMultiQPi * fc / sr;
        const double bw   = fc / Q;
        const double kbw  = std::tan(kMultiQPi * std::min(bw, sr * 0.4998) / sr);
        const double A    = std::pow(10.0, gainDB / 40.0);
        const double cosW = std::cos(W0d);

        const double b0 = 1.0 + kbw * A,  b2 = 1.0 - kbw * A;
        const double a0 = 1.0 + kbw / A,  a2 = 1.0 - kbw / A;
        const double b1 = -2.0 * cosW;
        const double a1 = -2.0 * cosW;

        c.coeffs[0] = static_cast<float>(b0 / a0);
        c.coeffs[1] = static_cast<float>(b1 / a0);
        c.coeffs[2] = static_cast<float>(b2 / a0);
        c.coeffs[3] = 1.0f;
        c.coeffs[4] = static_cast<float>(a1 / a0);
        c.coeffs[5] = static_cast<float>(a2 / a0);
    }

    static void computeLowShelf(MqBiquadCoeffs& c, double fc, double sr,
                                  double gainDB, double Q)
    {
        fc = clampFreq(fc, sr);
        Q  = std::max(0.01, Q);

        if (std::abs(gainDB) < 0.01) { c.setIdentity(); return; }

        const double A    = std::pow(10.0, gainDB / 40.0);
        const double k    = std::tan(kMultiQPi * fc / sr);
        const double k2   = k * k;
        const double sqA  = std::sqrt(A);
        const double cosW = (1.0 - k2) / (1.0 + k2);
        const double sinW = 2.0 * k / (1.0 + k2);
        const double alpha = sinW / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / Q - 1.0) + 2.0);

        const double b0 =  A * ((A + 1.0) - (A - 1.0) * cosW + 2.0 * sqA * alpha);
        const double b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosW);
        const double b2 =  A * ((A + 1.0) - (A - 1.0) * cosW - 2.0 * sqA * alpha);
        const double a0 = (A + 1.0) + (A - 1.0) * cosW + 2.0 * sqA * alpha;
        const double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW);
        const double a2 = (A + 1.0) + (A - 1.0) * cosW - 2.0 * sqA * alpha;

        c.coeffs[0] = static_cast<float>(b0 / a0);
        c.coeffs[1] = static_cast<float>(b1 / a0);
        c.coeffs[2] = static_cast<float>(b2 / a0);
        c.coeffs[3] = 1.0f;
        c.coeffs[4] = static_cast<float>(a1 / a0);
        c.coeffs[5] = static_cast<float>(a2 / a0);
    }

    static void computeHighShelf(MqBiquadCoeffs& c, double fc, double sr,
                                   double gainDB, double Q)
    {
        fc = clampFreq(fc, sr);
        Q  = std::max(0.01, Q);

        if (std::abs(gainDB) < 0.01) { c.setIdentity(); return; }

        const double A    = std::pow(10.0, gainDB / 40.0);
        const double sqA  = std::sqrt(A);
        const double k    = std::tan(kMultiQPi * fc / sr);
        const double k2   = k * k;
        const double cosW = (1.0 - k2) / (1.0 + k2);
        const double sinW = 2.0 * k / (1.0 + k2);
        const double alpha = sinW / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / Q - 1.0) + 2.0);

        const double b0 =  A * ((A + 1.0) + (A - 1.0) * cosW + 2.0 * sqA * alpha);
        const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW);
        const double b2 =  A * ((A + 1.0) + (A - 1.0) * cosW - 2.0 * sqA * alpha);
        const double a0 = (A + 1.0) - (A - 1.0) * cosW + 2.0 * sqA * alpha;
        const double a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cosW);
        const double a2 = (A + 1.0) - (A - 1.0) * cosW - 2.0 * sqA * alpha;

        c.coeffs[0] = static_cast<float>(b0 / a0);
        c.coeffs[1] = static_cast<float>(b1 / a0);
        c.coeffs[2] = static_cast<float>(b2 / a0);
        c.coeffs[3] = 1.0f;
        c.coeffs[4] = static_cast<float>(a1 / a0);
        c.coeffs[5] = static_cast<float>(a2 / a0);
    }

    static void computeHighPass(MqBiquadCoeffs& c, double fc, double sr, double Q)
    {
        fc = clampFreq(fc, sr);
        Q  = std::max(0.01, Q);
        const double k    = std::tan(kMultiQPi * fc / sr);
        const double norm = 1.0 / (k * k + k / Q + 1.0);

        c.coeffs[0] = static_cast<float>(norm);
        c.coeffs[1] = static_cast<float>(-2.0 * norm);
        c.coeffs[2] = static_cast<float>(norm);
        c.coeffs[3] = 1.0f;
        c.coeffs[4] = static_cast<float>(2.0 * (k * k - 1.0) * norm);
        c.coeffs[5] = static_cast<float>((k * k - k / Q + 1.0) * norm);
    }

    static void computeLowPass(MqBiquadCoeffs& c, double fc, double sr, double Q)
    {
        fc = clampFreq(fc, sr);
        Q  = std::max(0.01, Q);
        const double k    = std::tan(kMultiQPi * fc / sr);
        const double k2   = k * k;
        const double norm = 1.0 / (k2 + k / Q + 1.0);

        c.coeffs[0] = static_cast<float>(k2 * norm);
        c.coeffs[1] = static_cast<float>(2.0 * k2 * norm);
        c.coeffs[2] = static_cast<float>(k2 * norm);
        c.coeffs[3] = 1.0f;
        c.coeffs[4] = static_cast<float>(2.0 * (k2 - 1.0) * norm);
        c.coeffs[5] = static_cast<float>((k2 - k / Q + 1.0) * norm);
    }

    static void computeFirstOrderHighPass(MqBiquadCoeffs& c, double fc, double sr)
    {
        fc = clampFreq(fc, sr);
        const double k    = std::tan(kMultiQPi * fc / sr);
        const double norm = 1.0 / (1.0 + k);

        c.coeffs[0] = static_cast<float>(norm);
        c.coeffs[1] = static_cast<float>(-norm);
        c.coeffs[2] = 0.0f;
        c.coeffs[3] = 1.0f;
        c.coeffs[4] = static_cast<float>((k - 1.0) * norm);
        c.coeffs[5] = 0.0f;
    }

    static void computeFirstOrderLowPass(MqBiquadCoeffs& c, double fc, double sr)
    {
        fc = clampFreq(fc, sr);
        const double k    = std::tan(kMultiQPi * fc / sr);
        const double norm = 1.0 / (1.0 + k);

        c.coeffs[0] = static_cast<float>(k * norm);
        c.coeffs[1] = static_cast<float>(k * norm);
        c.coeffs[2] = 0.0f;
        c.coeffs[3] = 1.0f;
        c.coeffs[4] = static_cast<float>((k - 1.0) * norm);
        c.coeffs[5] = 0.0f;
    }

    static void computeNotch(MqBiquadCoeffs& c, double fc, double sr, double Q)
    {
        fc = clampFreq(fc, sr);
        Q  = std::max(0.01, Q);
        const double W0d  = 2.0 * kMultiQPi * fc / sr;
        const double cosW = std::cos(W0d);
        const double kbw  = std::tan(kMultiQPi * std::min(fc / Q, sr * 0.4998) / sr);
        const double norm = 1.0 / (1.0 + kbw);

        c.coeffs[0] = static_cast<float>(norm);
        c.coeffs[1] = static_cast<float>(-2.0 * cosW * norm);
        c.coeffs[2] = static_cast<float>(norm);
        c.coeffs[3] = 1.0f;
        c.coeffs[4] = static_cast<float>(-2.0 * cosW * norm);
        c.coeffs[5] = static_cast<float>((1.0 - kbw) * norm);
    }

    static void computeBandPass(MqBiquadCoeffs& c, double fc, double sr, double Q)
    {
        fc = clampFreq(fc, sr);
        Q  = std::max(0.01, Q);
        const double W0d  = 2.0 * kMultiQPi * fc / sr;
        const double cosW = std::cos(W0d);
        const double kbw  = std::tan(kMultiQPi * std::min(fc / Q, sr * 0.4998) / sr);
        const double norm = 1.0 / (1.0 + kbw);

        c.coeffs[0] = static_cast<float>(kbw * norm);
        c.coeffs[1] = 0.0f;
        c.coeffs[2] = static_cast<float>(-kbw * norm);
        c.coeffs[3] = 1.0f;
        c.coeffs[4] = static_cast<float>(-2.0 * cosW * norm);
        c.coeffs[5] = static_cast<float>((1.0 - kbw) * norm);
    }
} // namespace amb

//==============================================================================
// Cytomic SVF coefficient designers for the dynamic-gain path (verbatim from
// MultiQ::computeSVF*). Transfer function matches the corresponding biquad; the
// SVF topology is used so the gain can be modulated per block without zipper.
namespace svfdes
{
    static void computePeaking(SVFCoeffs& c, double sr, double freq, float gainDB, float q)
    {
        freq = std::max(1.0, std::min(freq, sr * 0.4998));
        double A   = std::pow(10.0, gainDB / 40.0);
        double g   = std::tan(kMultiQPi * freq / sr);
        double bw  = freq / std::max(0.01, (double)q);
        double kbw = std::tan(kMultiQPi * std::min(bw, sr * 0.4998) / sr);
        double k   = std::max(0.001, kbw / (g * A));
        c.a1 = (float)(1.0 / (1.0 + g * (g + k)));
        c.a2 = (float)(g * c.a1);
        c.a3 = (float)(g * c.a2);
        c.m0 = 1.0f;
        c.m1 = (float)(k * (A * A - 1.0));
        c.m2 = 0.0f;
    }

    static void computeLowShelf(SVFCoeffs& c, double sr, double freq, float gainDB, float q)
    {
        double A = std::pow(10.0, gainDB / 40.0);
        double g = std::tan(kMultiQPi * freq / sr) / std::sqrt(A);
        double k = 1.0 / q;
        c.a1 = (float)(1.0 / (1.0 + g * (g + k)));
        c.a2 = (float)(g * c.a1);
        c.a3 = (float)(g * c.a2);
        c.m0 = 1.0f;
        c.m1 = (float)(k * (A - 1.0));
        c.m2 = (float)(A * A - 1.0);
    }

    static void computeHighShelf(SVFCoeffs& c, double sr, double freq, float gainDB, float q)
    {
        double A = std::pow(10.0, gainDB / 40.0);
        double g = std::tan(kMultiQPi * freq / sr) * std::sqrt(A);
        double k = 1.0 / q;
        c.a1 = (float)(1.0 / (1.0 + g * (g + k)));
        c.a2 = (float)(g * c.a1);
        c.a3 = (float)(g * c.a2);
        c.m0 = (float)(A * A);
        c.m1 = (float)(k * A * (1.0 - A));
        c.m2 = (float)(1.0 - A * A);
    }

    // tilt shelf = low shelf at Q=0.5 (matches the 1st-order biquad tilt).
    static void computeTiltShelf(SVFCoeffs& c, double sr, double freq, float gainDB)
    {
        computeLowShelf(c, sr, freq, gainDB, 0.5f);
    }
} // namespace svfdes

} // namespace duskaudio

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Filters.hpp — real-pole/real-zero analog prototypes and their discrete
// realisation, for the PRE-35 core. Header-only, framework-free, C++17.
//
// Every filter in the M-35 model is a cascade of FIRST-ORDER sections with real
// poles and real zeros: transformer corners, the amplifier's GBW shelf, the
// iron sidechain weighting, the 1/f noise tail. There is not one resonant
// section in the device, so there is no biquad here and no Q anywhere.
//
// The prototypes are ANALOG (rad/s). They are bilinear-transformed at the rate
// the core is actually running at, which is the whole reason Pre35Coefficients
// carries corner frequencies instead of z-domain numbers: the transformer's HF
// corner sits above 30 kHz and cannot be placed at a 48 kHz sample rate at all.
//
// The transform is scipy's `bilinear_zpk` convention EXACTLY — s -> 2 fs (1-z^-1)
// / (1+z^-1) with NO frequency prewarping — because the Python reference renderer
// (fit/render_ref.py) is the spec and prewarping here would silently de-null the
// port. Prewarping is unnecessary anyway: the core runs oversampled, where the
// untransformed error is already below 0.05 dB in band.
//
// REAL-TIME: setFromAnalog() computes coefficients and is control-rate, not
// per-sample. process() allocates nothing, locks nothing, and flushes its state
// to zero below kDenormFloor so a decaying tail cannot fall into denormal-land
// and stall the audio thread.

#pragma once

#include <algorithm>
#include <cmath>

namespace pre35
{

//==============================================================================
// Denormal hygiene. The LF highpass runs at a pole radius of 1 - 6e-5 at 384 kHz,
// so its state decays for a very long time and WILL reach denormal range on a
// fade to silence. 1e-100 is ~-2000 dBFS: unreachable by any real signal, and far
// above the double denormal threshold (~2.2e-308), so the flush is free of any
// audible consequence.
inline constexpr double kDenormFloor = 1.0e-100;

inline double flushDenormal(double v) noexcept
{
    return (v > -kDenormFloor && v < kDenormFloor) ? 0.0 : v;
}

inline constexpr double kPi = 3.14159265358979323846;

inline double dbToLin(double db) noexcept { return std::pow(10.0, db / 20.0); }

//==============================================================================
/** An analog transfer function with real zeros and real poles, in rad/s.

    H(s) = gain * prod(s - zeros[i]) / prod(s - poles[j])

    Capacity is fixed at kMaxTerms so the struct is trivially copyable and can be
    built on the audio thread without allocating. The largest user is the iron
    sidechain: 5 poles, 4 zeros.
*/
struct AnalogZPK
{
    static constexpr int kMaxTerms = 8;

    double zeros[kMaxTerms] {};
    double poles[kMaxTerms] {};
    int    numZeros = 0;
    int    numPoles = 0;
    double gain     = 1.0;

    void addZero(double s) noexcept { if (numZeros < kMaxTerms) zeros[numZeros++] = s; }
    void addPole(double s) noexcept { if (numPoles < kMaxTerms) poles[numPoles++] = s; }

    /** |H(j2*pi*f)| in dB. Real poles/zeros make every factor a plain
        sqrt(w^2 + a^2), so this needs no complex arithmetic. Mirrors
        fit.model._zpk_db, including its 1e-300 guard against log10(0).
    */
    double magnitudeDb(double f) const noexcept
    {
        const double w = 2.0 * kPi * f;
        double mag = std::abs(gain);
        for (int i = 0; i < numZeros; ++i)
            mag *= std::sqrt(w * w + zeros[i] * zeros[i]);
        for (int j = 0; j < numPoles; ++j)
            mag /= std::sqrt(w * w + poles[j] * poles[j]);
        return 20.0 * std::log10(mag + 1.0e-300);
    }
};

//==============================================================================
/** Cascade of first-order digital sections, built from an AnalogZPK by bilinear
    transform. Direct Form I, doubles throughout.

    Each section is (1 - zd z^-1) / (1 - pd z^-1); the overall gain is applied
    once, at the input. Relative-degree padding (a zero at z = -1 per excess
    pole) matches scipy's bilinear_zpk, so the realised transfer function is the
    same one `sosfilt(zpk2sos(bilinear_zpk(...)))` produces in the reference.

    Poles are paired with their nearest zero, which is what zpk2sos does and what
    keeps the highpass section (zero at exactly z = +1, pole a hair inside it)
    from being split across the cascade where it would cost precision.
*/
class FirstOrderCascade
{
public:
    static constexpr int kMaxSections = AnalogZPK::kMaxTerms;

    /** Recompute coefficients, PRESERVING filter state.

        State is preserved so a control-rate retune (the GBW shelf tracking the
        trim knob) does not click. If the section count changes the old state no
        longer describes the new topology, so it is dropped — with this model
        that cannot happen at run time (every pad yields three sections), but a
        future model must not inherit a silent misalignment.

        @returns false if the prototype does not fit (more poles than
                 kMaxSections, or more zeros than poles); coefficients unchanged.
    */
    bool setFromAnalog(const AnalogZPK& a, double sampleRate) noexcept
    {
        if (a.numPoles > kMaxSections || a.numZeros > a.numPoles || sampleRate <= 0.0)
            return false;

        const double fs2 = 2.0 * sampleRate;

        double zd[kMaxSections] {};
        double pd[kMaxSections] {};
        double numProd = 1.0;
        double denProd = 1.0;

        for (int i = 0; i < a.numZeros; ++i)
        {
            zd[i]    = (fs2 + a.zeros[i]) / (fs2 - a.zeros[i]);
            numProd *= (fs2 - a.zeros[i]);
        }
        for (int j = 0; j < a.numPoles; ++j)
        {
            pd[j]    = (fs2 + a.poles[j]) / (fs2 - a.poles[j]);
            denProd *= (fs2 - a.poles[j]);
        }

        // scipy appends one zero at z = -1 per unit of relative degree.
        int nz = a.numZeros;
        while (nz < a.numPoles)
            zd[nz++] = -1.0;

        const int n = a.numPoles;
        if (n != numSections)
        {
            for (int i = 0; i < kMaxSections; ++i)
                sections[i] = Section {};
            numSections = n;
        }

        // Nearest-zero pairing, greedy over poles. n <= 8, so the O(n^2) scan is
        // cheaper than any bookkeeping that would avoid it.
        bool used[kMaxSections] {};
        for (int j = 0; j < n; ++j)
        {
            int    best     = -1;
            double bestDist = 0.0;
            for (int i = 0; i < n; ++i)
            {
                if (used[i])
                    continue;
                const double d = std::abs(pd[j] - zd[i]);
                if (best < 0 || d < bestDist) { best = i; bestDist = d; }
            }
            if (best < 0)
                return false;            // unreachable: nz == n by construction
            used[best] = true;
            sections[j].b1 = -zd[best];
            sections[j].a1 =  pd[j];
        }

        overallGain = a.gain * (numProd / denProd);
        return true;
    }

    void reset() noexcept
    {
        for (int i = 0; i < kMaxSections; ++i) { sections[i].x1 = 0.0; sections[i].y1 = 0.0; }
    }

    double process(double x) noexcept
    {
        double y = x * overallGain;
        for (int i = 0; i < numSections; ++i)
        {
            Section& s = sections[i];
            const double in = y;
            y = in + s.b1 * s.x1 + s.a1 * s.y1;
            s.x1 = in;
            s.y1 = flushDenormal(y);
            y    = s.y1;
        }
        return y;
    }

    int  getNumSections() const noexcept { return numSections; }
    double gain()         const noexcept { return overallGain; }

    /** Pole radius of section i — |pole| >= 1 means the realisation is unstable.
        Test-facing; not used on the audio path. */
    double poleRadius(int i) const noexcept { return std::abs(sections[i].a1); }

    /** |H(e^jw)| in dB at frequency f, for coefficient sanity checks. */
    double magnitudeDb(double f, double sampleRate) const noexcept
    {
        const double w  = 2.0 * kPi * f / sampleRate;
        const double cw = std::cos(w), sw = std::sin(w);
        double re = overallGain, im = 0.0;
        for (int i = 0; i < numSections; ++i)
        {
            // numerator 1 + b1 e^-jw, denominator 1 - a1 e^-jw
            const double nr = 1.0 + sections[i].b1 * cw, ni = -sections[i].b1 * sw;
            const double dr = 1.0 - sections[i].a1 * cw, di =  sections[i].a1 * sw;
            const double tr = re * nr - im * ni, ti = re * ni + im * nr;
            const double dd = dr * dr + di * di;
            re = (tr * dr + ti * di) / dd;
            im = (ti * dr - tr * di) / dd;
        }
        return 20.0 * std::log10(std::sqrt(re * re + im * im) + 1.0e-300);
    }

private:
    struct Section
    {
        double b1 = 0.0;   ///< -zd
        double a1 = 0.0;   ///< +pd
        double x1 = 0.0;
        double y1 = 0.0;
    };

    Section sections[kMaxSections] {};
    int     numSections = 0;
    double  overallGain = 1.0;
};

} // namespace pre35

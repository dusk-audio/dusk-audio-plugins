// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// TestSupport.hpp — shared plumbing for the PRE-35 core gates.
//
// Plain main() tests, no framework: the core has no dependencies and its gates
// should not introduce one. Each test binary returns 0 when every check passes
// and 1 otherwise, and prints one line per check either way so a failure says
// what the number actually was.
//
// The harmonic measurement here deliberately reproduces
// dusk-audio-tools tools/m35/fit/fit_iron.py::measure_thd — Hann window on a
// whole number of cycles, 5-bin sum around each harmonic — so a THD number
// printed by this suite is directly comparable to one printed by the Python
// validation, instead of being "the same idea, measured differently".

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace pre35test
{

//==============================================================================
class Report
{
public:
    explicit Report(const char* name) : suite(name)
    {
        std::printf("== %s ==\n", suite.c_str());
    }

    void check(bool ok, const std::string& what, const std::string& detail = {})
    {
        ++total;
        if (! ok)
            ++failed;
        std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(),
                    detail.empty() ? "" : " — ", detail.c_str());
    }

    /** value must be within tol of target. */
    void near(double value, double target, double tol, const std::string& what,
              const char* unit = "dB")
    {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "%.6g vs %.6g %s (|d| %.3g, tol %.3g)",
                      value, target, unit, std::fabs(value - target), tol);
        check(std::fabs(value - target) <= tol, what, buf);
    }

    void below(double value, double limit, const std::string& what, const char* unit = "dB")
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%.6g %s (limit %.6g)", value, unit, limit);
        check(value <= limit, what, buf);
    }

    void note(const std::string& text) { std::printf("  ---- %s\n", text.c_str()); }

    int exitCode() const
    {
        std::printf("== %s: %d/%d passed ==\n", suite.c_str(), total - failed, total);
        return failed == 0 ? 0 : 1;
    }

private:
    std::string suite;
    int total = 0;
    int failed = 0;
};

//==============================================================================
inline double linToDb(double v) { return 20.0 * std::log10(v > 1e-300 ? v : 1e-300); }
inline double dbToLinT(double db) { return std::pow(10.0, db / 20.0); }

inline constexpr double kPiT = 3.14159265358979323846;

/** Sine of `amplitude` (peak) at f0, `seconds` long. */
inline std::vector<float> makeSine(double f0, double amplitude, double seconds, double sr)
{
    const size_t n = (size_t)std::llround(seconds * sr);
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = (float)(amplitude * std::sin(2.0 * kPiT * f0 * (double)i / sr));
    return v;
}

/** RMS of a span. */
inline double rms(const float* x, size_t n)
{
    double s = 0.0;
    for (size_t i = 0; i < n; ++i)
        s += (double)x[i] * (double)x[i];
    return n ? std::sqrt(s / (double)n) : 0.0;
}

//==============================================================================
/** Magnitude of one DFT bin of an already-windowed segment. */
inline double dftBinMag(const std::vector<double>& seg, int k)
{
    const double n = (double)seg.size();
    double re = 0.0, im = 0.0;
    for (size_t m = 0; m < seg.size(); ++m)
    {
        const double a = -2.0 * kPiT * (double)k * (double)m / n;
        re += seg[m] * std::cos(a);
        im += seg[m] * std::sin(a);
    }
    return std::sqrt(re * re + im * im);
}

/** h3/h1 and h2/h1 in dBc from a steady tone, on a whole number of cycles.

    Port of fit_iron.measure_thd: Hann window, nearest bin, 5-bin sum. The 5-bin
    sum is what makes the number robust to the window's own leakage — a single
    bin under-reads by ~6 dB with a Hann.
*/
struct ThdResult { double thd3Dbc; double thd2Dbc; double fundamental; };

inline ThdResult measureThd(const std::vector<float>& y, double f0, double sr,
                            double skipSeconds)
{
    const size_t skip = (size_t)std::llround(skipSeconds * sr);
    if (skip >= y.size())
        return { 0.0, 0.0, 0.0 };

    const size_t avail = y.size() - skip;
    const long   cycles = (long)std::floor((double)avail * f0 / sr);
    const size_t n = (size_t)std::llround((double)cycles * sr / f0);
    if (n < 16 || n > avail)
        return { 0.0, 0.0, 0.0 };

    std::vector<double> seg(n);
    for (size_t m = 0; m < n; ++m)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPiT * (double)m / (double)(n - 1));
        seg[m] = (double)y[skip + m] * w;
    }

    auto amp = [&](double target) {
        const int centre = (int)std::llround(target * (double)n / sr);
        double sum = 0.0;
        for (int k = centre - 2; k <= centre + 2; ++k)
            if (k >= 0 && (size_t)k <= n / 2)
                sum += dftBinMag(seg, k);
        return sum;
    };

    const double a1 = amp(f0);
    if (! (a1 > 0.0))                       // silent or unmeasurable fundamental:
        return { 0.0, 0.0, 0.0 };           // same sentinel as the guards above
    return { linToDb(amp(3.0 * f0) / a1), linToDb(amp(2.0 * f0) / a1), a1 };
}

/** Largest discontinuity in a rendered steady sine, as a fraction of the local
    envelope.

    An exact sine obeys y[n+1] - 2 cos(w) y[n] + y[n-1] == 0 for every n, so that
    second difference is a click detector that does not care how loud the tone is
    or where in its cycle the click landed. A SMOOTH amplitude ramp leaves a
    residual proportional to the ramp's curvature (tiny); a STEP leaves one
    proportional to the step itself.

    Normalising by a local block-max envelope is what makes the number comparable
    between a transition that ends 39 dB down and one that ends 58 dB down —
    otherwise a bigger gain change always looks like a bigger click.
*/
inline double maxSineDiscontinuityFrom(const std::vector<float>& y, double f0, double sr)
{
    const int n = (int)y.size();
    const int period = std::max(1, (int)std::llround(sr / f0));
    if (n < 4 * period)
        return 0.0;

    const int nb = (n + period - 1) / period;
    std::vector<double> blockMax((size_t)nb, 0.0);
    for (int i = 0; i < n; ++i)
    {
        double& m = blockMax[(size_t)(i / period)];
        m = std::max(m, (double)std::fabs(y[i]));
    }

    const double c = 2.0 * std::cos(2.0 * kPiT * f0 / sr);
    double worst = 0.0;
    for (int i = 1; i + 1 < n; ++i)
    {
        const int b = i / period;
        double env = blockMax[(size_t)b];
        if (b > 0)      env = std::max(env, blockMax[(size_t)(b - 1)]);
        if (b + 1 < nb) env = std::max(env, blockMax[(size_t)(b + 1)]);
        if (env <= 1e-30)
            continue;
        const double resid = std::fabs((double)y[i + 1] - c * (double)y[i] + (double)y[i - 1]);
        worst = std::max(worst, resid / env);
    }
    return worst;
}

/** maxSineDiscontinuityFrom, skipping the first `skipSeconds`.

    The start of any render IS a discontinuity — the tone switches on into cold
    filters — and measuring through it sets a floor that can hide the click this
    function exists to find. Skip past the onset; keep the event.
*/
inline double maxSineDiscontinuity(const std::vector<float>& y, double f0, double sr,
                                   double skipSeconds)
{
    const size_t skip = std::min((size_t)std::llround(skipSeconds * sr), y.size());
    return maxSineDiscontinuityFrom(std::vector<float>(y.begin() + (long)skip, y.end()),
                                    f0, sr);
}

/** Amplitude of a single frequency, on a whole number of cycles, rectangular
    window (exact bin isolation — use this for gain, not for THD). */
inline double measureAmplitude(const std::vector<float>& y, double f0, double sr,
                               double skipSeconds)
{
    const size_t skip = (size_t)std::llround(skipSeconds * sr);
    if (skip >= y.size())
        return 0.0;
    const size_t avail = y.size() - skip;
    const long   cycles = (long)std::floor((double)avail * f0 / sr);
    const size_t n = (size_t)std::llround((double)cycles * sr / f0);
    if (n < 16 || n > avail)
        return 0.0;

    std::vector<double> seg(y.begin() + (long)skip, y.begin() + (long)(skip + n));
    const int k = (int)std::llround(f0 * (double)n / sr);
    return 2.0 * dftBinMag(seg, k) / (double)n;
}

} // namespace pre35test

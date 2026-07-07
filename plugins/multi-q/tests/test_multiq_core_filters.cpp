// Null harness for the Multi-Q core filter primitives (Phase 2a).
//
// Proves plugins/multi-q/core/MultiQFilters.hpp reproduces the JUCE build's
// filter math EXACTLY:
//   (1) Coefficient designers vs the UNMODIFIED original AnalogMatchedBiquad.h
//       (byte-compare all 6 coeffs over random params) — catches any
//       transcription slip in the core copy.
//   (2) Per-sample recurrences (StereoBiquad DF2T, CytomicSVF) vs independent
//       canonical references over random coeffs + random signal.
//
// Framework-free: no JUCE, no cmake. Build:
//   g++ -std=c++17 -O2 test_multiq_core_filters.cpp -o /tmp/mqfilt && /tmp/mqfilt

#include <cstdio>
#include <cstring>
#include <cmath>
#include <random>
#include <cstdint>

// ---- reference: the ORIGINAL AnalogMatchedBiquad.h, included unmodified ------
// It only needs a BiquadCoeffs type with coeffs[6] + setIdentity() in scope.
struct BiquadCoeffs
{
    float coeffs[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    void setIdentity()
    {
        coeffs[0] = 1.0f; coeffs[1] = 0.0f; coeffs[2] = 0.0f;
        coeffs[3] = 1.0f; coeffs[4] = 0.0f; coeffs[5] = 0.0f;
    }
};
#include "../AnalogMatchedBiquad.h"   // namespace AnalogMatchedBiquad (reference)

// ---- port under test --------------------------------------------------------
#include "../core/MultiQFilters.hpp"  // duskaudio::amb + structs

static int failures = 0;

static bool coeffsEqual(const BiquadCoeffs& a, const duskaudio::MqBiquadCoeffs& b)
{
    return std::memcmp(a.coeffs, b.coeffs, sizeof(float) * 6) == 0;
}

template <typename RefFn, typename CoreFn>
static void checkDesigner(const char* name, RefFn ref, CoreFn core,
                          std::mt19937& rng, bool hasGain)
{
    std::uniform_real_distribution<double> fcD(10.0, 22000.0);
    std::uniform_real_distribution<double> srD(0.0, 3.0);
    std::uniform_real_distribution<double> gD(-24.0, 24.0);
    std::uniform_real_distribution<double> qD(0.1, 40.0);
    const double srs[4] = {44100.0, 48000.0, 88200.0, 96000.0};
    int mism = 0;
    for (int i = 0; i < 200000; ++i)
    {
        double sr = srs[(int)srD(rng) & 3];
        double fc = fcD(rng);
        if (fc > sr * 0.499) fc = sr * 0.499;
        double g = gD(rng), q = qD(rng);
        BiquadCoeffs r; duskaudio::MqBiquadCoeffs c;
        ref(r, fc, sr, g, q);
        core(c, fc, sr, g, q);
        if (!coeffsEqual(r, c)) { if (mism < 3) printf("    MISMATCH %s fc=%.2f sr=%.0f g=%.2f q=%.3f\n", name, fc, sr, g, q); ++mism; }
    }
    printf("  %-22s %s (%d/200000 mismatches)\n", name, mism == 0 ? "PASS" : "FAIL", mism);
    if (mism) ++failures;
    (void)hasGain;
}

// canonical DF2T biquad (Direct Form II Transposed), the exact recurrence the
// JUCE StereoBiquad uses (finite-guards omitted; test signal is finite).
struct RefDF2T
{
    float b0, b1, b2, a1, a2;
    float s1 = 0, s2 = 0;
    float tick(float x)
    {
        float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
};

// canonical Cytomic trapezoidal SVF (Simper), matching CytomicSVF::processSample.
struct RefCytomic
{
    float a1, a2, a3, m0, m1, m2;
    float ic1 = 0, ic2 = 0;
    float tick(float x)
    {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return m0 * x + m1 * v1 + m2 * v2;
    }
};

int main()
{
    std::mt19937 rng(0xC0FFEE);
    printf("== coefficient designers (core vs unmodified AnalogMatchedBiquad.h) ==\n");
    using RC = BiquadCoeffs; using CC = duskaudio::MqBiquadCoeffs;
    checkDesigner("peaking",
        [](RC& c, double f, double s, double g, double q){ AnalogMatchedBiquad::computePeaking(c, f, s, g, q); },
        [](CC& c, double f, double s, double g, double q){ duskaudio::amb::computePeaking(c, f, s, g, q); }, rng, true);
    checkDesigner("lowShelf",
        [](RC& c, double f, double s, double g, double q){ AnalogMatchedBiquad::computeLowShelf(c, f, s, g, q); },
        [](CC& c, double f, double s, double g, double q){ duskaudio::amb::computeLowShelf(c, f, s, g, q); }, rng, true);
    checkDesigner("highShelf",
        [](RC& c, double f, double s, double g, double q){ AnalogMatchedBiquad::computeHighShelf(c, f, s, g, q); },
        [](CC& c, double f, double s, double g, double q){ duskaudio::amb::computeHighShelf(c, f, s, g, q); }, rng, true);
    checkDesigner("highPass",
        [](RC& c, double f, double s, double, double q){ AnalogMatchedBiquad::computeHighPass(c, f, s, q); },
        [](CC& c, double f, double s, double, double q){ duskaudio::amb::computeHighPass(c, f, s, q); }, rng, false);
    checkDesigner("lowPass",
        [](RC& c, double f, double s, double, double q){ AnalogMatchedBiquad::computeLowPass(c, f, s, q); },
        [](CC& c, double f, double s, double, double q){ duskaudio::amb::computeLowPass(c, f, s, q); }, rng, false);
    checkDesigner("firstOrderHighPass",
        [](RC& c, double f, double s, double, double){ AnalogMatchedBiquad::computeFirstOrderHighPass(c, f, s); },
        [](CC& c, double f, double s, double, double){ duskaudio::amb::computeFirstOrderHighPass(c, f, s); }, rng, false);
    checkDesigner("firstOrderLowPass",
        [](RC& c, double f, double s, double, double){ AnalogMatchedBiquad::computeFirstOrderLowPass(c, f, s); },
        [](CC& c, double f, double s, double, double){ duskaudio::amb::computeFirstOrderLowPass(c, f, s); }, rng, false);
    checkDesigner("notch",
        [](RC& c, double f, double s, double, double q){ AnalogMatchedBiquad::computeNotch(c, f, s, q); },
        [](CC& c, double f, double s, double, double q){ duskaudio::amb::computeNotch(c, f, s, q); }, rng, false);
    checkDesigner("bandPass",
        [](RC& c, double f, double s, double, double q){ AnalogMatchedBiquad::computeBandPass(c, f, s, q); },
        [](CC& c, double f, double s, double, double q){ duskaudio::amb::computeBandPass(c, f, s, q); }, rng, false);

    printf("\n== per-sample recurrences (core vs canonical reference) ==\n");
    // StereoBiquad DF2T: design a random stable-ish biquad, snap coeffs, stream noise.
    {
        std::uniform_real_distribution<double> f(50, 18000), q(0.3, 8), g(-18, 18);
        int mism = 0;
        for (int t = 0; t < 2000; ++t)
        {
            duskaudio::MqBiquadCoeffs cc;
            duskaudio::amb::computePeaking(cc, f(rng), 48000.0, g(rng), q(rng));
            duskaudio::StereoBiquad sb; sb.setCoeffs(cc); sb.snapToTarget(); sb.smoothCoeff = 1.0f;
            RefDF2T ref{cc.coeffs[0], cc.coeffs[1], cc.coeffs[2], cc.coeffs[4], cc.coeffs[5]};
            std::uniform_real_distribution<float> sig(-1.f, 1.f);
            for (int n = 0; n < 256; ++n)
            {
                float x = sig(rng);
                sb.stepSmoothing();
                float yc = sb.processSampleL(x);
                float yr = ref.tick(x);
                if (yc != yr) { ++mism; break; }
            }
        }
        printf("  %-22s %s\n", "StereoBiquad DF2T", mism == 0 ? "PASS" : "FAIL");
        if (mism) ++failures;
    }
    // CytomicSVF: physically-valid STABLE coeffs from a real SVF design
    // (g=tan(pi*fc/sr), k=1/Q) so neither impl hits the NaN-sanitise path;
    // random filter type picks the m0/m1/m2 mix (LP/BP/HP/notch/peak).
    {
        std::uniform_real_distribution<float> fD(50.f, 18000.f), qD(0.3f, 8.f), sig(-1.f, 1.f);
        std::uniform_int_distribution<int> typeD(0, 4);
        int mism = 0;
        for (int t = 0; t < 2000; ++t)
        {
            const double sr = 48000.0;
            const double g = std::tan(duskaudio::kMultiQPi * fD(rng) / sr);
            const double k = 1.0 / qD(rng);
            const double a1 = 1.0 / (1.0 + g * (g + k));
            const double a2 = g * a1;
            const double a3 = g * a2;
            duskaudio::SVFCoeffs sc;
            sc.a1 = (float)a1; sc.a2 = (float)a2; sc.a3 = (float)a3;
            switch (typeD(rng))
            {
                case 0: sc.m0 = 0; sc.m1 = 0;        sc.m2 = 1;              break; // LP
                case 1: sc.m0 = 0; sc.m1 = 1;        sc.m2 = 0;              break; // BP
                case 2: sc.m0 = 1; sc.m1 = -(float)k; sc.m2 = -1;           break; // HP
                case 3: sc.m0 = 1; sc.m1 = -(float)k; sc.m2 = 0;            break; // notch
                default:sc.m0 = 1; sc.m1 = -(float)k; sc.m2 = -2;          break; // peak-ish
            }
            duskaudio::CytomicSVF sv; sv.setTarget(sc); sv.snapToTarget();
            RefCytomic ref{sc.a1, sc.a2, sc.a3, sc.m0, sc.m1, sc.m2};
            for (int n = 0; n < 256; ++n)
            {
                float x = sig(rng);
                sv.stepSmoothing();
                float yc = sv.processSample(x);
                float yr = ref.tick(x);
                // core sanitises NaN/Inf state; reference does not — only compare finite.
                if (std::isfinite(yr) && yc != yr) { ++mism; break; }
            }
        }
        printf("  %-22s %s\n", "CytomicSVF", mism == 0 ? "PASS" : "FAIL");
        if (mism) ++failures;
    }

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// sin_test — numeric contract for FMEngine.hpp's sinTurns(), the polynomial that
// replaced libm sinf in the Prism operator loop.
//
// A hand-fitted approximation has no natural regression guard: a coefficient
// typo, a re-fit at a different degree, or a compiler that contracts the Horner
// chain differently all still SOUND like FM. This asserts the three properties
// the engine actually relies on, in the shipped header, at the shipped
// precision — no numpy, no audio, no thresholds anyone has to re-calibrate by
// ear.
//
//   1. ACCURACY over one period: max |sinTurns(x) - sin(2*pi*x)| <= 1e-5.
//      The design target was 1e-4 (-80 dB); the fit measures 6.7e-6 (-103 dB),
//      so the bar is set at 1e-5 — tight enough to catch a degraded fit, loose
//      enough that it is not a float-rounding tripwire.
//   2. EXACT ZEROS at x = 0, +-0.5. The x*(1-4x^2) factor exists to make the
//      phase wrap bit-exactly silent; if a re-fit ever drops it, the residual
//      becomes a once-per-cycle step and this fails.
//   3. ACCURACY ACROSS THE FULL REACHABLE RANGE. Prism can drive the argument to
//      +-33 turns (level clamp 4 -> 16 turns of depth, in-degree 2, plus 1 turn
//      of op-4 feedback). The range reduction is exact in turns, so the error
//      there must be no worse than the argument's own float32 quantisation
//      (ulp(33)/2 = 1e-6 turns = 6.2e-6 in value) plus the fit residual.
//      This is ALSO the guard on the reduction's two standing assumptions --
//      round-to-nearest, and a compiler that does not cancel (x + K) - K back to
//      x (legal only under -ffast-math). If either breaks, the reduction stops
//      reducing and this check goes from 7e-6 to order 1.

#include "FMEngine.hpp"

#include <cmath>
#include <cstdio>

int main()
{
    bool ok = true;

    // ---- 1. accuracy over one period -------------------------------------
    constexpr int    kN   = 4000001;
    constexpr double kTol = 1.0e-5;
    double worst = 0.0, worstAt = 0.0;
    for (int i = 0; i < kN; ++i)
    {
        const double xd = -0.5 + (double)i / (double)(kN - 1);
        const float  x  = (float)xd;
        const double e  = std::fabs((double)msynth::sinTurns(x) - std::sin(2.0 * M_PI * (double)x));
        if (e > worst) { worst = e; worstAt = (double)x; }
    }
    const bool accOk = worst <= kTol;
    ok = ok && accOk;
    std::printf("  [1] max abs error over [-0.5, 0.5] turns: %.3e (%.1f dB) at x=%+.6f"
                "  (tol %.0e)  -> %s\n",
                worst, 20.0 * std::log10(worst), worstAt, kTol, accOk ? "PASS" : "FAIL");

    // ---- 2. exact zeros at the wrap --------------------------------------
    const float z0 = msynth::sinTurns(0.0f);
    const float zp = msynth::sinTurns(0.5f);
    const float zm = msynth::sinTurns(-0.5f);
    const bool zeroOk = (z0 == 0.0f) && (zp == 0.0f) && (zm == 0.0f);
    ok = ok && zeroOk;
    std::printf("  [2] wrap zeros: sinTurns(0)=%g  sinTurns(+0.5)=%g  sinTurns(-0.5)=%g"
                "  -> %s\n", (double)z0, (double)zp, (double)zm, zeroOk ? "PASS" : "FAIL");

    // ---- 3. accuracy across the full reachable argument range -------------
    // Stepped by an irrational-ish increment so the samples do not land on the
    // same reduced phases every turn.
    constexpr double kRangeTol = 2.0e-5;
    double rworst = 0.0, rworstAt = 0.0;
    for (int i = -2000000; i <= 2000000; ++i)
    {
        const float x = (float)((double)i * 34.0 / 2000000.0);
        if (std::fabs((double)x) > 34.0) continue;
        const double e = std::fabs((double)msynth::sinTurns(x)
                                   - std::sin(2.0 * M_PI * (double)x));
        if (e > rworst) { rworst = e; rworstAt = (double)x; }
    }
    const bool rangeOk = rworst <= kRangeTol;
    ok = ok && rangeOk;
    std::printf("  [3] max abs error over [-34, +34] turns: %.3e (%.1f dB) at x=%+.4f"
                "  (tol %.0e)  -> %s\n",
                rworst, 20.0 * std::log10(rworst), rworstAt, kRangeTol,
                rangeOk ? "PASS" : "FAIL");

    std::printf("sin_gate: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// IronLayer.hpp — constant-percentage odd hysteresis, level-tracked Chebyshev
// shaper. The input transformer's contribution to the M-35 channel, ported from
// fit/model.py::IronLayer.
//
// The whole trick is that the SHAPE is Chebyshev and the DEPTH is level-tracked:
//
//     w  = W(x)                        LF weighting, |W(20 Hz)| = 1
//     Aw = sqrt(2 <w^2>)               peak-equivalent envelope of w
//     u  = w / Aw                      unit-amplitude drive
//     y  = x + Aw ( d3 (4u^3 - 3u) + d2 (2u^2 - 1) )
//
// For a sine at f, 4u^3-3u is exactly -sin(3wt) and 2u^2-1 exactly -cos(2wt):
// pure harmonics and, crucially, NO fundamental term, so the layer cannot shift
// the gain it is bolted onto. The amplitude of each is Aw = A |W(f)|, so
// h3/h1 = d3 |W(f)| — independent of A (constant percentage) and shaped by W
// alone (the f^-alpha law). Dividing by Aw and multiplying it back is what buys
// the level independence; W is what buys the frequency law.
//
// 2u^2-1 has zero mean because Aw is defined as sqrt(2) RMS(w). The even term
// does put a little energy at DC, though: its depth is modulated by the drive
// detector's own 2f ripple, and that ripple times cheb2 (also at 2f) lands at DC
// and 4f. Neither is at the fundamental, so the layer still cannot shift the
// gain it is bolted onto, which is what chain_test's 0.005 dB gate holds.
//
// The EVEN harmonic rides a second weighting filter W2 and a second detector,
// because h2 falls at alpha2 (about -14 dB/oct) where h3 falls at alpha3 (about
// -9.3). Its depth also tracks drive at roughly 1 dB per dB of ratio, where h3's
// does not move at all — so h3 is constant-percentage and h2 is not, and they
// cannot share machinery.
//
// VALIDITY: the constant-percentage law was measured over 20-100 Hz inside a
// ~16 dB drive window. Below that window the core is in its Rayleigh region and
// makes measurably less distortion; above it the amplifier clips. Neither end is
// modelled, so this layer EXTRAPOLATES outside kIronValid{Lo,Hi}Hz — it does not
// predict there.
//
// REAL-TIME: prepare() computes coefficients. processSample() does two cascades,
// three one-poles, three sqrts, two divides, one pow and a handful of multiplies.
// The pow is the expensive one and it arrived with the even harmonic's drive
// tracking; it is per sample at the oversampled rate, so budget for it. No
// allocation, no denormal traps (all three detector outputs are floored at
// 10^(kDetectorFloorDbfs/20), so no divide can see zero, and every accumulator
// is flushed). The only data-dependent tests are the envelope floors and the two
// Chebyshev-domain clamps, all min/max shapes.

#pragma once

#include "Filters.hpp"
#include "Pre35Model.hpp"

#include <cmath>

namespace pre35
{

class IronLayer
{
public:
    /** @param sampleRate the rate this layer actually runs at — the OVERSAMPLED
                          rate, since the shaper triples bandwidth. */
    void prepare(double sampleRate) noexcept
    {
        sr = sampleRate;
        // setFromAnalog returns false when the prototype does not fit the
        // cascade, and a failed cascade degrades to a wrong-gain pass-through
        // rather than to silence, so ignoring this is a silent-failure hazard.
        // Two filters, two chances; both are recorded and gated.
        const bool okW  = sidechain.setFromAnalog(buildSidechainZPK(), sampleRate);
        const bool okW2 = sidechain2.setFromAnalog(buildSidechain2ZPK(), sampleRate);
        cascadesValid = okW && okW2;

        d3 = coeffs::kIronPowerLaw.r3Ref;

        detectorPole = std::exp(-1.0 / (coeffs::kDetectorTauS * sampleRate));
        envFloor     = dbToLin(coeffs::kDetectorFloorDbfs);

        reset();
    }

    void reset() noexcept
    {
        sidechain.reset();
        sidechain2.reset();
        meanSquare2 = 0.0;
        meanSquareX = 0.0;
        meanSquare = 0.0;
    }

    /** @param amount 1.0 reproduces the fitted device exactly; 0 bypasses the
                     layer bit-for-bit; >1 exaggerates it beyond anything the
                     measurement supports. */
    double processSample(double x, double amount) noexcept
    {
        const double w = sidechain.process(x);

        // lfilter([1-a], [1, -a]) — a one-pole mean-square of the weighted signal.
        const double a = detectorPole;
        meanSquare = flushDenormal((1.0 - a) * (w * w) + a * meanSquare);

        const double ms = meanSquare > 0.0 ? meanSquare : 0.0;
        const double aw = std::sqrt(2.0 * ms + envFloor * envFloor);

        // u is unit-amplitude only once the detector has CAUGHT UP. A cold start or
        // the first samples after reset() divide by an envelope still climbing off
        // zero, so |u| can reach the hundreds for one time constant, and cheb3 goes
        // cubic on it: the shaper would emit w^3 / aw^2 before the model is even
        // valid. Clamp to the domain the Chebyshev polynomials are defined on.
        //
        // This is a no-op on every settled signal, not an approximation of one: for
        // a steady sine the detector's own 2f ripple leaves |u| at 1 + 1.3e-4, and
        // the gates (THD3 vs spec, the ripple-fundamental prediction, +6 dB at
        // amount 2) are unmoved to the digits they print.
        const double raw = w / aw;
        const double u   = raw > 1.0 ? 1.0 : (raw < -1.0 ? -1.0 : raw);

        const double cheb3 = 4.0 * u * u * u - 3.0 * u;   // -sin(3wt) for a sine

        // ---- even harmonic ---------------------------------------------------
        // Its own weighting filter, because h2 falls at alpha2 (~-14 dB/oct)
        // where h3 falls at alpha3 (~-9.3). Sharing W would force both to the
        // same slope and get h2 wrong by ~10 dB across the band.
        const double w2 = sidechain2.process(x);
        meanSquare2 = flushDenormal((1.0 - a) * (w2 * w2) + a * meanSquare2);
        const double ms2 = meanSquare2 > 0.0 ? meanSquare2 : 0.0;
        const double aw2 = std::sqrt(2.0 * ms2 + envFloor * envFloor);

        const double raw2 = w2 / aw2;
        const double u2   = raw2 > 1.0 ? 1.0 : (raw2 < -1.0 ? -1.0 : raw2);
        const double cheb2 = 2.0 * u2 * u2 - 1.0;         // -cos(2wt) for a sine

        // And its own DEPTH, which tracks drive where the odd harmonic's does
        // not: d2Eff = d2Ref * (ax/axRef)^slope, ~1 dB per dB of ratio, so the
        // harmonic's own amplitude rises ~2 dB/dB. That is the square law the
        // bench measured, and it is what makes the layer respond to how hard it
        // is driven instead of sitting at a fixed percentage.
        //
        // `ax` is the envelope of the UNWEIGHTED signal, deliberately. Using aw2
        // is the obvious-looking choice and lands the frequency dependence
        // twice, making the effective exponent alpha2*(1+slope) instead of
        // alpha2 - a flat 16.7 dB error at 50 Hz when it was tried. Flux is set
        // by the actual level at the transformer, not a weighted copy of it.
        meanSquareX = flushDenormal((1.0 - a) * (x * x) + a * meanSquareX);
        const double msx = meanSquareX > 0.0 ? meanSquareX : 0.0;
        const double ax  = std::sqrt(2.0 * msx + envFloor * envFloor);
        const double d2Eff = coeffs::kIronH2.d2Ref
                           * std::pow(ax / coeffs::kIronH2.axRefLin,
                                      coeffs::kIronH2.slope);

        return x + amount * (aw * d3 * cheb3 + aw2 * d2Eff * cheb2);
    }

    /** Envelope-detector time constant expressed in samples at the prepared rate;
        test-facing. */
    double detectorPoleValue() const noexcept { return detectorPole; }
    double sampleRate()        const noexcept { return sr; }
    double d3Value()           const noexcept { return d3; }

    const FirstOrderCascade& sidechainFilter() const noexcept { return sidechain; }

    /** TEST-ONLY. False if either weighting cascade failed to realise, which
        would otherwise be a silent wrong-gain pass-through. */
    bool cascadesAreValid() const noexcept { return cascadesValid; }

    /** TEST-ONLY. The drive-tracked even depth at a given drive envelope, so a
        gate can pin the law without keeping its own copy of the algebra. */
    double d2EffAt(double axLin) const noexcept
    {
        return coeffs::kIronH2.d2Ref
             * std::pow(axLin / coeffs::kIronH2.axRefLin, coeffs::kIronH2.slope);
    }

    /** TEST-ONLY. Peak-equivalent envelope of the UNWEIGHTED input, i.e. the
        drive the even depth is currently tracking. */
    double driveEnvelope() const noexcept
    {
        const double ms = meanSquareX > 0.0 ? meanSquareX : 0.0;
        return std::sqrt(2.0 * ms + envFloor * envFloor);
    }

private:
    FirstOrderCascade sidechain;    ///< W,  the odd harmonic's weighting
    FirstOrderCascade sidechain2;   ///< W2, the even harmonic's, steeper
    double sr           = 0.0;
    double d3           = 0.0;
    double detectorPole = 0.0;
    double envFloor     = 0.0;
    double meanSquare   = 0.0;   ///< detector on W(x)
    double meanSquare2  = 0.0;   ///< detector on W2(x)
    double meanSquareX  = 0.0;   ///< detector on the UNWEIGHTED x, the drive
    bool   cascadesValid = false;
};

} // namespace pre35

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
// 2u^2-1 has exactly zero mean because Aw is defined as sqrt(2) RMS(w), so the
// even term adds no DC beyond the detector's own lag.
//
// VALIDITY: the constant-percentage law was measured over 20-100 Hz inside a
// ~16 dB drive window. Below that window the core is in its Rayleigh region and
// makes measurably less distortion; above it the amplifier clips. Neither end is
// modelled, so this layer EXTRAPOLATES outside kIronValid{Lo,Hi}Hz — it does not
// predict there.
//
// REAL-TIME: prepare() computes coefficients. processSample() does one cascade,
// one one-pole, one sqrt, one divide and a handful of multiplies. No allocation,
// no branches on data, no denormal traps (the detector output is floored at
// 10^(kDetectorFloorDbfs/20), so the divide can never see zero).

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
        sidechain.setFromAnalog(buildSidechainZPK(), sampleRate);

        d3 = coeffs::kIronPowerLaw.r3Ref;
        d2 = d3 * dbToLin(-coeffs::kIronH2OffsetDb);

        detectorPole = std::exp(-1.0 / (coeffs::kDetectorTauS * sampleRate));
        envFloor     = dbToLin(coeffs::kDetectorFloorDbfs);

        reset();
    }

    void reset() noexcept
    {
        sidechain.reset();
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
        const double u  = w / aw;

        const double cheb3 = 4.0 * u * u * u - 3.0 * u;   // -sin(3wt) for a sine
        const double cheb2 = 2.0 * u * u - 1.0;           // -cos(2wt) for a sine

        return x + amount * aw * (d3 * cheb3 + d2 * cheb2);
    }

    /** Envelope-detector time constant expressed in samples at the prepared rate;
        test-facing. */
    double detectorPoleValue() const noexcept { return detectorPole; }
    double sampleRate()        const noexcept { return sr; }
    double d3Value()           const noexcept { return d3; }
    double d2Value()           const noexcept { return d2; }

    const FirstOrderCascade& sidechainFilter() const noexcept { return sidechain; }

private:
    FirstOrderCascade sidechain;
    double sr           = 0.0;
    double d3           = 0.0;
    double d2           = 0.0;
    double detectorPole = 0.0;
    double envFloor     = 0.0;
    double meanSquare   = 0.0;
};

} // namespace pre35

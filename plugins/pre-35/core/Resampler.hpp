// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Resampler.hpp — the PRE-35 core's oversampler.
//
// WHY OVERSAMPLE AT ALL. Two reasons, both measured rather than assumed:
//   1. The input transformer's HF corner is 33-57 kHz depending on the pad. A
//      bilinear transform at 48 kHz cannot place a pole above Nyquist at all, so
//      the response is simply not realisable at the host rate.
//   2. The iron layer's Chebyshev shaper triples bandwidth. Third harmonics of
//      anything above 8 kHz fold back without it.
//
// WHY THIS PARTICULAR FILTER. The Python reference renderer runs the chain
// through `scipy.signal.resample_poly(x, 8, 1)` and back, and the reference is
// the spec. resample_poly designs its antialias filter as
//
//     firwin(2 * (10 * factor) + 1, 1 / factor, window = ('kaiser', 5.0))
//
// and scales it by `up`. This file reproduces that design exactly — same window,
// same cutoff, same length, same DC normalisation — so the C++ port nulls against
// the Python instead of merely resembling it. A "better" resampler here (steeper,
// higher stopband) would put its own passband ripple in a different place and eat
// most of the 0.1 dB null budget at 18 kHz for nothing.
//
// That ripple is not hypothetical. The kaiser-5 prototype is +0.0045 dB at 1 kHz,
// so the round trip is +0.0090 dB there — measurable against a 0.05 dB gain gate,
// and present in the reference too. Matching the design means it cancels instead
// of being spent. Verified: this implementation and scipy agree on the round-trip
// gain to 2e-7 dB and on the DC residual (1.112101e-7) to the last digit.
//
// STREAMING vs OFFLINE. resample_poly is an offline, zero-group-delay operation:
// it zero-pads and then discards the leading samples so output[i] lines up with
// input[i]. Streaming cannot see the future, so this implementation carries the
// filter's own delay — 10 * factor oversampled samples per stage, which is
// exactly 10 HOST samples per stage regardless of factor, so 20 host samples of
// latency end to end. Report it, align for it, do not pretend it is not there.
//
// REAL-TIME: everything is fixed-size (no std::vector, no heap at any point) and
// prepare() only fills arrays. The interpolator is polyphase (factor * 21 taps
// per host sample); the decimator is direct form, because a decimator's
// "efficient" polyphase form computes exactly the same 161 products — you already
// only evaluate one output in `factor`.

#pragma once

#include <cmath>
#include <cstddef>

namespace pre35
{

//==============================================================================
/** Modified Bessel function of the first kind, order 0, by its defining series.

    I0(x) = sum_k (x^2/4)^k / (k!)^2 — converges in ~20 terms for the beta = 5
    this file needs, to full double precision. The Abramowitz-Stegun polynomial
    everybody reaches for is only good to ~1e-7 and there is no reason to accept
    that here.
*/
inline double besselI0(double x) noexcept
{
    const double t = 0.25 * x * x;
    double term = 1.0;
    double sum  = 1.0;
    for (int k = 1; k < 128; ++k)
    {
        term *= t / (static_cast<double>(k) * static_cast<double>(k));
        sum  += term;
        if (term < 1.0e-18 * sum)
            break;
    }
    return sum;
}

/** numpy.sinc: sin(pi x) / (pi x), and 1 at x = 0. */
inline double sincPi(double x) noexcept
{
    if (x == 0.0)
        return 1.0;
    const double px = 3.14159265358979323846 * x;
    return std::sin(px) / px;
}

//==============================================================================
inline constexpr int kMaxOversampleFactor = 8;
inline constexpr int kMaxResamplerTaps    = 20 * kMaxOversampleFactor + 1;   // 161
inline constexpr int kMaxTapsPerPhase     = kMaxResamplerTaps / kMaxOversampleFactor + 1;

/** Every caller sizes its tap array off this, and designResamplerTaps() clamps
    `factor` to the same range, so the two can never disagree. */
inline constexpr int tapCountFor(int factor) noexcept
{
    return 20 * (factor < 1 ? 1 : (factor > kMaxOversampleFactor ? kMaxOversampleFactor
                                                                 : factor)) + 1;
}

/** scipy resample_poly's prototype filter, tap for tap.

    firwin(numTaps, fc, window=('kaiser', 5.0)) with the default scale=True:
    a windowed sinc normalised so the DC gain is exactly 1.

    `factor` is clamped to 1..kMaxOversampleFactor HERE rather than trusted from
    the caller: the tap arrays this writes into are fixed-size members sized off
    kMaxResamplerTaps, so an out-of-range factor would be a buffer overrun and not
    merely a wrong filter. Both current callers clamp too; this is the guarantee
    that survives the next one.

    @param factor   oversampling ratio, clamped to 1..kMaxOversampleFactor
    @param taps     output, at least tapCountFor(factor) doubles
    @returns        number of taps written
*/
inline int designResamplerTaps(int factor, double* taps) noexcept
{
    if (factor < 1)
        factor = 1;
    else if (factor > kMaxOversampleFactor)
        factor = kMaxOversampleFactor;

    const int    halfLen = 10 * factor;
    const int    numTaps = 2 * halfLen + 1;      // == tapCountFor(factor)
    const double fc      = 1.0 / static_cast<double>(factor);
    const double alpha   = 0.5 * static_cast<double>(numTaps - 1);
    const double beta    = 5.0;
    const double i0Beta  = besselI0(beta);

    double sum = 0.0;
    for (int n = 0; n < numTaps; ++n)
    {
        const double m = static_cast<double>(n) - alpha;
        const double r = m / alpha;
        const double arg = 1.0 - r * r;
        const double win = besselI0(beta * std::sqrt(arg > 0.0 ? arg : 0.0)) / i0Beta;
        taps[n] = fc * sincPi(fc * m) * win;
        sum += taps[n];
    }
    for (int n = 0; n < numTaps; ++n)
        taps[n] /= sum;

    return numTaps;
}

/** The decimator's window is anchored `factor - 1` samples behind the newest
    sample of the block it was just fed, so its ring has to be that much longer
    than the filter. */
inline constexpr int kMaxDecimatorRing = kMaxResamplerTaps + kMaxOversampleFactor - 1;

/** Filter delay of ONE stage, in host samples. halfLen = 10 * factor oversampled
    samples is 10 host samples for every factor, which is why the number below is
    a constant and not a function of the rate. */
inline constexpr int kResamplerStageLatency = 10;

//==============================================================================
/** 1 -> factor polyphase interpolator. */
class Upsampler
{
public:
    void prepare(int oversampleFactor) noexcept
    {
        factor = (oversampleFactor < 1) ? 1
               : (oversampleFactor > kMaxOversampleFactor ? kMaxOversampleFactor
                                                          : oversampleFactor);
        if (factor == 1)
        {
            tapsPerPhase = 1;
            phase[0] = 1.0;
            reset();
            return;
        }

        double proto[kMaxResamplerTaps];
        const int numTaps = designResamplerTaps(factor, proto);

        // resample_poly scales the prototype by `up`; without it the interpolator
        // would lose exactly `factor` in level (zero-stuffing spreads the energy).
        tapsPerPhase = (numTaps + factor - 1) / factor;
        for (int p = 0; p < factor; ++p)
            for (int q = 0; q < tapsPerPhase; ++q)
            {
                const int idx = q * factor + p;
                phase[p * kMaxTapsPerPhase + q] =
                    (idx < numTaps) ? proto[idx] * static_cast<double>(factor) : 0.0;
            }

        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < 2 * kMaxTapsPerPhase; ++i)
            hist[i] = 0.0;
        pos = 0;
    }

    /** Writes exactly `factor` oversampled samples for one host sample. */
    void processSample(double x, double* out) noexcept
    {
        // Ring stored twice so the tap loop reads a contiguous descending window
        // with no wrap test: hist[pos + T - q] is always the q-th newest.
        hist[pos] = x;
        hist[pos + tapsPerPhase] = x;

        const int base = pos + tapsPerPhase;
        for (int p = 0; p < factor; ++p)
        {
            const double* h = phase + p * kMaxTapsPerPhase;
            double acc = 0.0;
            for (int q = 0; q < tapsPerPhase; ++q)
                acc += h[q] * hist[base - q];
            out[p] = acc;
        }

        pos = (pos + 1 == tapsPerPhase) ? 0 : pos + 1;
    }

    int getFactor() const noexcept { return factor; }

private:
    double phase[kMaxOversampleFactor * kMaxTapsPerPhase] {};
    double hist [2 * kMaxTapsPerPhase] {};
    int    factor       = 1;
    int    tapsPerPhase = 1;
    int    pos          = 0;
};

//==============================================================================
/** factor -> 1 decimator. */
class Downsampler
{
public:
    void prepare(int oversampleFactor) noexcept
    {
        factor = (oversampleFactor < 1) ? 1
               : (oversampleFactor > kMaxOversampleFactor ? kMaxOversampleFactor
                                                          : oversampleFactor);
        if (factor == 1)
        {
            taps[0] = 1.0;
            numTaps = 1;
        }
        else
        {
            numTaps = designResamplerTaps(factor, taps);   // down: no `up` scaling
        }

        ringLen = numTaps + factor - 1;
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < 2 * kMaxDecimatorRing; ++i)
            ring[i] = 0.0;
        pos = ringLen - 1;
    }

    /** Consumes `factor` oversampled samples, returns one host sample. */
    double processBlock(const double* in) noexcept
    {
        // Ring stored twice so the tap loop reads a contiguous descending window
        // with no wrap test.
        for (int i = 0; i < factor; ++i)
        {
            pos = (pos + 1 == ringLen) ? 0 : pos + 1;
            ring[pos] = in[i];
            ring[pos + ringLen] = in[i];
        }

        // The output belongs to the FIRST sample of the block just pushed — that
        // is the decimation phase the reference lands on. Anchoring on the newest
        // sample instead would offset the whole render by (factor-1)/factor of a
        // host sample, which no magnitude gate would catch.
        //
        // Zero-delay would need the window centred (factor-1)/2 ahead of that;
        // streaming takes the causal window and carries the delay (file header).
        const int base = pos + ringLen - (factor - 1);
        double acc = 0.0;
        for (int j = 0; j < numTaps; ++j)
            acc += taps[j] * ring[base - j];
        return acc;
    }

    int getFactor()  const noexcept { return factor; }
    int getNumTaps() const noexcept { return numTaps; }

private:
    double taps[kMaxResamplerTaps] {};
    double ring[2 * kMaxDecimatorRing] {};
    int    factor  = 1;
    int    numTaps = 1;
    int    ringLen = 1;
    int    pos     = 0;
};

} // namespace pre35

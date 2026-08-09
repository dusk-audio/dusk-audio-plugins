// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Noise.hpp — the M-35's input-referred noise floor.
//
// Referred to the AMPLIFIER input, i.e. behind the pad: engaging 40 dB of pad
// does NOT drop the hiss, and winding the trim up DOES raise it. That asymmetry
// is the whole reason the noise is injected where it is in the chain rather than
// bolted onto the output.
//
// Shape is flat plus a 1/f tail below the corner: PSD ~ N0^2 (1 + fc/f). The
// tail is a separate shaped generator summed with an independent white one, so
// the two are uncorrelated exactly as the model assumes.
//
// NOT BIT-IDENTICAL TO THE PYTHON REFERENCE, deliberately. render_ref.py draws
// from numpy's PCG64 + ziggurat; reproducing that stream in C++ would be a large
// amount of code in service of matching a noise floor sample-for-sample, which
// nothing needs. What IS reproduced is the spectrum and the broadband level, and
// the null test runs with noise off precisely so this is never the thing that
// moves a gate.
//
// RATE COMPENSATION. The reference generates noise at the HOST rate and
// interpolates it into the oversampled chain. Generating directly at the
// oversampled rate is cheaper and lands in the same place, provided the drive is
// scaled by sqrt(factor): white noise generated at N times the rate carries 1/N
// of the in-band power once the decimator throws the rest away.
//
// REAL-TIME: no allocation, no locks. One Marsaglia polar draw per sample yields
// BOTH gaussians needed (they are independent by construction), so the log/sqrt
// cost is paid once per sample, not twice.

#pragma once

#include "Filters.hpp"
#include "Pre35Model.hpp"

#include <cmath>
#include <cstdint>

namespace pre35
{

//==============================================================================
/** xoshiro256** — small, fast, deterministic, and not std::mt19937 (which is
    19937 bits of state to carry around for a hiss generator). */
class Xoshiro256
{
public:
    explicit Xoshiro256(uint64_t seed = 0x50524533u) noexcept { setSeed(seed); }

    void setSeed(uint64_t seed) noexcept
    {
        // SplitMix64 to expand one seed word into the four state words. A raw
        // all-zero state is a fixed point of xoshiro, so this matters.
        for (int i = 0; i < 4; ++i)
        {
            seed += 0x9E3779B97F4A7C15ull;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            s[i] = z ^ (z >> 31);
        }
    }

    uint64_t nextBits() noexcept
    {
        const uint64_t result = rotl(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    /** Uniform in (-1, 1). 53-bit mantissa, so the ladder is smooth. */
    double nextBipolar() noexcept
    {
        const double u = static_cast<double>(nextBits() >> 11) * (1.0 / 9007199254740992.0);
        return 2.0 * u - 1.0;
    }

private:
    static uint64_t rotl(uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }
    uint64_t s[4] {};
};

//==============================================================================
class NoiseGenerator
{
public:
    /** @param oversampledRate   the rate this runs at
        @param oversampleFactor  ratio to the host rate, for the sqrt scaling */
    void prepare(double oversampledRate, int oversampleFactor, uint64_t seed) noexcept
    {
        tail.setFromAnalog(buildNoiseTailZPK(), oversampledRate);
        level = dbToLin(coeffs::kNoise.inputReferredDbfs)
              * std::sqrt(static_cast<double>(oversampleFactor < 1 ? 1 : oversampleFactor));
        rng.setSeed(seed);
        reset();
    }

    void reset() noexcept
    {
        tail.reset();
    }

    double nextSample() noexcept
    {
        double white = 0.0;
        double pinkDrive = 0.0;
        gaussianPair(white, pinkDrive);
        return level * (white + tail.process(pinkDrive));
    }

private:
    /** Marsaglia polar method. Both outputs are independent standard normals, so
        one draw feeds the flat term and the 1/f term without correlating them. */
    void gaussianPair(double& a, double& b) noexcept
    {
        double u, v, s;
        do
        {
            u = rng.nextBipolar();
            v = rng.nextBipolar();
            s = u * u + v * v;
        } while (s >= 1.0 || s <= 0.0);

        const double f = std::sqrt(-2.0 * std::log(s) / s);
        a = u * f;
        b = v * f;
    }

    FirstOrderCascade tail;
    Xoshiro256 rng;
    double level = 0.0;
};

} // namespace pre35

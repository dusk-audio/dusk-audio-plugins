// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// Deterministic programme for the core capture test; see CaptureStimulus.hpp.
// Compiled with -ffp-contract=off (core/CMakeLists.txt): the polynomial below
// is written as multiply-then-add, and fusing it would change the bits.

#include "CaptureStimulus.hpp"

#include <cstring>
#include <random>
#include <stdexcept>

namespace spectrum_capture
{
namespace
{
constexpr double kHalfPi = 1.57079632679489661923;

// sin(x) and cos(x) for x in [0, pi/2], Taylor series through x^19 / x^20
// (truncation error below 3e-16). Basic IEEE operations only.
double sinQuadrant(double x)
{
    const double x2 = x * x;
    double p = 1.0 / 121645100408832000.0;      // 1/19!
    p = p * x2 - 1.0 / 355687428096000.0;       // 1/17!
    p = p * x2 + 1.0 / 1307674368000.0;         // 1/15!
    p = p * x2 - 1.0 / 6227020800.0;            // 1/13!
    p = p * x2 + 1.0 / 39916800.0;              // 1/11!
    p = p * x2 - 1.0 / 362880.0;                // 1/9!
    p = p * x2 + 1.0 / 5040.0;                  // 1/7!
    p = p * x2 - 1.0 / 120.0;                   // 1/5!
    p = p * x2 + 1.0 / 6.0;                     // 1/3!
    p = p * x2 - 1.0;
    return -(p * x);
}

double cosQuadrant(double x)
{
    const double x2 = x * x;
    double p = 1.0 / 2432902008176640000.0;     // 1/20!
    p = p * x2 - 1.0 / 6402373705728000.0;      // 1/18!
    p = p * x2 + 1.0 / 20922789888000.0;        // 1/16!
    p = p * x2 - 1.0 / 87178291200.0;           // 1/14!
    p = p * x2 + 1.0 / 479001600.0;             // 1/12!
    p = p * x2 - 1.0 / 3628800.0;               // 1/10!
    p = p * x2 + 1.0 / 40320.0;                 // 1/8!
    p = p * x2 - 1.0 / 720.0;                   // 1/6!
    p = p * x2 + 1.0 / 24.0;                    // 1/4!
    p = p * x2 - 1.0 / 2.0;                     // 1/2!
    return p * x2 + 1.0;
}

// sin(2*pi*k/q) for 0 <= k < q. The quadrant and the remainder within it come
// from exact integer arithmetic, so only the polynomial rounds.
double unitSine(std::uint64_t k, std::uint64_t q)
{
    const std::uint64_t quadrant = (4 * k) / q;
    const std::uint64_t remainder = 4 * k - quadrant * q;
    const double x = kHalfPi * (static_cast<double>(remainder) / static_cast<double>(q));
    switch (quadrant)
    {
        case 0:  return sinQuadrant(x);
        case 1:  return cosQuadrant(x);
        case 2:  return -sinQuadrant(x);
        default: return -cosQuadrant(x);
    }
}

std::uint64_t gcd(std::uint64_t a, std::uint64_t b)
{
    while (b != 0)
    {
        const std::uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// Uniform in [-1, 1): the top 24 bits of each mt19937 word, centred and scaled
// by a power of two, which float and double both hold exactly.
double noiseSample(std::mt19937& rng)
{
    const auto top24 = static_cast<std::int32_t>(rng() >> 8);
    return static_cast<double>(top24 - 8388608) / 8388608.0;
}

// Amplitudes as decimal constants (no pow at run time): 10^(dB/20).
constexpr double kMinus18dB = 0.12589254117941672104;
constexpr double kMinus27dB = 0.044668359215096311;
constexpr double kMinus30dB = 0.031622776601683793320;
constexpr double kMinus62dB = 0.00079432823472428130;
constexpr double kMinus72dB = 0.00025118864315095795;
} // namespace

StereoProgramme makeProgramme(int sampleRate)
{
    if (sampleRate <= 0 || sampleRate % 100 != 0)
        throw std::invalid_argument("makeProgramme: sample rate must be a multiple of 100 Hz");

    const auto sr = static_cast<std::uint64_t>(sampleRate);
    const std::size_t total = static_cast<std::size_t>(sr) * kProgrammeSeconds;
    StereoProgramme out;
    out.left.assign(total, 0.0f);
    out.right.assign(total, 0.0f);

    // A tone of f Hz as the reduced fraction p/q of the sample rate: phase
    // index (i*p) mod q, exact for every sample.
    struct Tone
    {
        std::uint64_t p, q;
        double at(std::uint64_t phaseIndex) const { return unitSine((phaseIndex * p) % q, q); }
    };
    const auto tone = [sr](std::uint64_t hz) {
        const std::uint64_t g = gcd(hz, sr);
        return Tone { hz / g, sr / g };
    };
    const Tone k1 = tone(1000);
    const Tone k15 = tone(1500);
    constexpr std::uint64_t kRightDelay = 7;   // samples
    std::mt19937 noise(0x5EED0010u);

    for (std::size_t n = 0; n < total; ++n)
    {
        const std::uint64_t i = n;
        double l = 0.0;
        double r = 0.0;
        if (i < 2 * sr)
        {
            l = (i < sr ? kMinus62dB : kMinus72dB) * k1.at(i);
            r = l;
        }
        else if (i < 6 * sr)
        {
            l = kMinus18dB * k1.at(i);
            r = kMinus18dB * k1.at(i + k1.q - kRightDelay);
        }
        else if (i < 9 * sr)
        {
            // Both tones complete whole cycles at 9 s, so the segment stops on
            // a zero crossing and the 100 ms block holding the loudness
            // filter's ring-down sits more than 2 dB from the -70 LUFS gate at
            // every rate (noise stopped here put it within 0.06 dB at 48 kHz).
            l = kMinus30dB * k1.at(i);
            r = kMinus27dB * k15.at(i);
        }
        else if (i < 10 * sr)
        {
            // silence
        }
        else if (i < 12 * sr)
        {
            l = 1.25 * unitSine((2 * i + 1) % 8, 8);   // fs/4, phase pi/4
            r = l;
        }
        else
        {
            // Right = -0.5 x left: the noise's own contributions to the three
            // correlation sums are in exact proportion in every build (the
            // scaling is exact), while the analyzer's mono feed still carries
            // noise. The readings also include the earlier segments' decaying
            // sums, so they are not -1 (see CaptureStimulus.hpp).
            l = 0.25 * noiseSample(noise);
            r = -0.5 * l;
        }
        out.left[n] = static_cast<float>(l);
        out.right[n] = static_cast<float>(r);
    }
    return out;
}

std::uint64_t hashProgramme(const StereoProgramme& programme)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const auto* channel : { &programme.left, &programme.right })
    {
        for (const float sample : *channel)
        {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &sample, sizeof(bits));
            for (int byte = 0; byte < 4; ++byte)
            {
                h ^= (bits >> (8 * byte)) & 0xFFu;
                h *= 1099511628211ull;
            }
        }
    }
    return h;
}
} // namespace spectrum_capture

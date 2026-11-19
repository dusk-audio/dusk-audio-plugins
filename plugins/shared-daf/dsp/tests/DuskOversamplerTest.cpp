// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// HalfbandFIR::outAfterStuffedZeroExact() must return the bits out() returns on the
// zero-stuffed phase, and the two oversamplers that use it, LocalAAStage and
// OversamplerT, must match copies that run the full FIR on every phase. "The bits" is
// the point: a result that is -0 in one and +0 in the other compares equal as a float
// and is still a difference, because a nonlinearity behind the stage may read the sign.
//
// The case that separates the exact shortcut from a plain `0.5f * centre + 0.0f` is a
// negative centre sample whose half underflows to zero, which needs flush-to-zero for
// a normal float, so every check runs with and without it.
#include "DuskDenormals.hpp"
#include "DuskOversampler.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace
{
int failures = 0;

uint32_t bitsOf (float v) noexcept
{
    uint32_t b;
    std::memcpy (&b, &v, sizeof b);
    return b;
}

float fromBits (uint32_t b) noexcept
{
    float v;
    std::memcpy (&v, &b, sizeof v);
    return v;
}

// Same bits, or both NaN: which NaN an operation returns is the platform's choice.
bool same (float a, float b) noexcept
{
    return (std::isnan (a) && std::isnan (b)) || bitsOf (a) == bitsOf (b);
}

std::vector<float> hostileValues()
{
    const float mn = std::numeric_limits<float>::min();          // smallest normal
    const float dn = std::numeric_limits<float>::denorm_min();
    std::vector<float> v = {
        0.0f, -0.0f, dn, -dn, 3.0f * dn, -3.0f * dn, mn, -mn, 1.5f * mn, -1.5f * mn,
        1.999f * mn, -1.999f * mn, 2.0f * mn, -2.0f * mn, 1e-30f, -1e-30f, 1e-5f, -1e-5f,
        0.25f, -0.25f, 1.0f, -1.0f, 4.0f, -4.0f, 1e12f, -1e12f,
        std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
    };
    std::mt19937 rng (20261119u);
    for (int i = 0; i < 200000; ++i)
        v.push_back (fromBits (static_cast<uint32_t> (rng())));               // any bit pattern
    for (int i = 0; i < 200000; ++i)                                           // tiny, either sign
        v.push_back (fromBits ((static_cast<uint32_t> (rng()) & 0x81ffffffu)));
    return v;
}

// One real sample then one stuffed zero, as every zero-stuffing upsampler pushes them.
template <int L, int NSide>
void checkRing (const float* taps, const std::vector<float>& values, const char* name)
{
    duskaudio::HalfbandFIR<L, NSide> fir;
    long bad = 0;
    for (const float v : values)
    {
        fir.push (v);
        fir.push (0.0f);
        const float full = fir.out (taps);
        const float fast = fir.outAfterStuffedZeroExact();
        if (! same (full, fast) && bad++ < 4)
            std::printf ("FAIL %s: sample %08x  out() %08x  shortcut %08x\n", name,
                         static_cast<unsigned> (bitsOf (v)), static_cast<unsigned> (bitsOf (full)),
                         static_cast<unsigned> (bitsOf (fast)));
    }
    if (bad != 0)
    {
        ++failures;
        std::printf ("FAIL %s: %ld of %zu zero-stuffed outputs differ from out()\n", name, bad, values.size());
    }
}

// LocalAAStage as it was before the shortcut: the full FIR on every phase.
class FullFirStage
{
public:
    template <class Fn>
    float process (float x, Fn&& f) noexcept
    {
        upA.push (x);       const float a0 = 2.0f * upA.out (duskaudio::hbtaps::kA);
        upA.push (0.0f);    const float a1 = 2.0f * upA.out (duskaudio::hbtaps::kA);
        downA.push (inner (a0, f));
        downA.push (inner (a1, f));
        return downA.out (duskaudio::hbtaps::kAdeep);
    }

private:
    template <class Fn>
    float inner (float s, Fn&& f) noexcept
    {
        upB.push (s);       const float b0 = 2.0f * upB.out (duskaudio::hbtaps::kB);
        upB.push (0.0f);    const float b1 = 2.0f * upB.out (duskaudio::hbtaps::kB);
        downB.push (f (b0));
        downB.push (f (b1));
        return downB.out (duskaudio::hbtaps::kB);
    }
    duskaudio::HalfbandFIR<47, 12> upA;
    duskaudio::HalfbandFIR<71, 18> downA;
    duskaudio::HalfbandFIR<15, 4>  upB, downB;
};

// The functor reads the sign of its input, zero included, so a -0 that should have
// been +0 anywhere inside the stage reaches the output as a full-size step.
void checkStage (const char* name)
{
    const auto signReader = [] (float v) noexcept { return std::copysign (0.25f, v) + v; };
    duskaudio::LocalAAStage stage;
    FullFirStage reference;
    std::mt19937 rng (7u);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    long bad = 0;
    const long n = 400000;
    for (long i = 0; i < n; ++i)
    {
        // Bursts that decay through the underflow range into silence, again and again.
        const long phase = i % 4000;
        const float env = phase < 400 ? 1.0f : std::pow (0.97f, static_cast<float> (phase - 400));
        const float x = phase > 3600 ? 0.0f : env * uni (rng);
        const float got = stage.process (x, signReader);
        const float want = reference.process (x, signReader);
        if (! same (got, want) && bad++ < 4)
            std::printf ("FAIL %s: sample %ld  full FIR %08x  LocalAAStage %08x\n", name, i,
                         static_cast<unsigned> (bitsOf (want)), static_cast<unsigned> (bitsOf (got)));
    }
    if (bad != 0)
    {
        ++failures;
        std::printf ("FAIL %s: %ld of %ld outputs differ from the full-FIR stage\n", name, bad, n);
    }
}

// OversamplerT with the full FIR on every phase, both entry points.
// Only its base<->2x stage can show the -0 / +0 case: the 2x<->4x stage's centre is always
// an output of the first stage, which is 2.0f times a float, so its half cannot underflow
// to zero. The 4x runs below therefore fail through the first stage or not at all.
template <class StageA>
class FullFirOversampler
{
public:
    void setFactor (int f) noexcept { factor = f; }

    template <class Fn>
    float processSample (float x, Fn&& f) noexcept
    {
        if (factor == 2)
            return process2x (x, f);
        return process2x (x, [this, &f] (float s) noexcept { return process4xInner (s, f); });
    }

    void upsampleSample (float x, float* phases) noexcept
    {
        upA.push (x);       const float a0 = 2.0f * upA.out (StageA::taps);
        upA.push (0.0f);    const float a1 = 2.0f * upA.out (StageA::taps);
        if (factor == 2) { phases[0] = a0; phases[1] = a1; return; }
        upB.push (a0);      phases[0] = 2.0f * upB.out (duskaudio::hbtaps::kB);
        upB.push (0.0f);    phases[1] = 2.0f * upB.out (duskaudio::hbtaps::kB);
        upB.push (a1);      phases[2] = 2.0f * upB.out (duskaudio::hbtaps::kB);
        upB.push (0.0f);    phases[3] = 2.0f * upB.out (duskaudio::hbtaps::kB);
    }

private:
    template <class Fn>
    float process2x (float x, Fn&& f) noexcept
    {
        upA.push (x);       const float a0 = 2.0f * upA.out (StageA::taps);
        upA.push (0.0f);    const float a1 = 2.0f * upA.out (StageA::taps);
        downA.push (f (a0));
        downA.push (f (a1));
        return downA.out (StageA::taps);
    }

    template <class Fn>
    float process4xInner (float s, Fn&& f) noexcept
    {
        upB.push (s);       const float b0 = 2.0f * upB.out (duskaudio::hbtaps::kB);
        upB.push (0.0f);    const float b1 = 2.0f * upB.out (duskaudio::hbtaps::kB);
        downB.push (f (b0));
        downB.push (f (b1));
        return downB.out (duskaudio::hbtaps::kB);
    }

    int factor = 2;
    duskaudio::HalfbandFIR<StageA::L, StageA::NSide> upA, downA;
    duskaudio::HalfbandFIR<15, 4> upB, downB;
};

// Bursts that decay through the underflow range into silence, again and again.
float decayingBurst (long i, std::mt19937& rng, std::uniform_real_distribution<float>& uni)
{
    const long phase = i % 4000;
    const float env = phase < 400 ? 1.0f : std::pow (0.97f, static_cast<float> (phase - 400));
    return phase > 3600 ? 0.0f : env * uni (rng);
}

template <class StageA>
void checkOversampler (int factor, const char* name)
{
    const auto signReader = [] (float v) noexcept { return std::copysign (0.25f, v) + v; };
    duskaudio::OversamplerT<StageA> os, split;
    FullFirOversampler<StageA> reference, splitReference;
    os.setFactor (factor); split.setFactor (factor);
    reference.setFactor (factor); splitReference.setFactor (factor);
    std::mt19937 rng (11u);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    long bad = 0, badPhases = 0;
    const long n = 200000;
    for (long i = 0; i < n; ++i)
    {
        const float x = decayingBurst (i, rng, uni);
        const float got = os.processSample (x, signReader);
        const float want = reference.processSample (x, signReader);
        if (! same (got, want) && bad++ < 4)
            std::printf ("FAIL %s: sample %ld  full FIR %08x  OversamplerT %08x\n", name, i,
                         static_cast<unsigned> (bitsOf (want)), static_cast<unsigned> (bitsOf (got)));
        // The split entry point: the upsampled phases themselves, before any functor.
        float p[4] = {}, q[4] = {};
        split.upsampleSample (x, p);
        splitReference.upsampleSample (x, q);
        for (int k = 0; k < factor; ++k)
            if (! same (p[k], q[k]))
                ++badPhases;
        (void) split.downsampleSample (p);
    }
    if (bad != 0 || badPhases != 0)
    {
        ++failures;
        std::printf ("FAIL %s: %ld of %ld outputs and %ld upsampled phases differ from the full-FIR oversampler\n",
                     name, bad, n, badPhases);
    }
}

void runOversamplers (const char* mode)
{
    char name[96];
    for (const int factor : { 2, 4 })
    {
        std::snprintf (name, sizeof name, "Oversampler %dx, %s", factor, mode);
        checkOversampler<duskaudio::hbtaps::StageA> (factor, name);
        std::snprintf (name, sizeof name, "OversamplerWide %dx, %s", factor, mode);
        checkOversampler<duskaudio::hbtaps::StageAwide> (factor, name);
    }
}

void runAll (const char* mode)
{
    const std::vector<float> values = hostileValues();
    char name[96];
    std::snprintf (name, sizeof name, "47-tap ring, %s", mode);
    checkRing<47, 12> (duskaudio::hbtaps::kA, values, name);
    std::snprintf (name, sizeof name, "15-tap ring, %s", mode);
    checkRing<15, 4> (duskaudio::hbtaps::kB, values, name);
    std::snprintf (name, sizeof name, "127-tap ring, %s", mode);
    checkRing<127, 32> (duskaudio::hbtaps::kAwide, values, name);
    std::snprintf (name, sizeof name, "LocalAAStage, %s", mode);
    checkStage (name);
}
} // namespace

int main()
{
    runAll ("denormals kept");
    runOversamplers ("denormals kept");
    {
        const duskaudio::ScopedFlushDenormals flush;
        runAll ("flush-to-zero");
        runOversamplers ("flush-to-zero");
    }
    if (failures == 0)
        std::printf ("DuskOversamplerTest: all checks passed\n");
    return failures == 0 ? 0 : 1;
}

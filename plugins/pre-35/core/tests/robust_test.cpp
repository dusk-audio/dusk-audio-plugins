// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// robust_test — what the core does when it is treated badly.
//
// The chain carries a highpass at 3.9 Hz running at 384 kHz (pole radius
// 1 - 6e-5) and a 0.25 s mean-square detector. Both hold state for a very long
// time, which is exactly the shape of thing that ends up in denormal-land on a
// fade to silence and stalls an audio thread. And a host CAN hand over a NaN.
//
// So: prove the tails reach exactly zero, prove a non-finite input does not
// poison the IIR state permanently, prove parameter automation stays bounded,
// and prove the noise floor is the level and the shape the model says it is
// (including that the PAD DOES NOT ATTENUATE IT — the whole point of referring
// it to the amplifier input).

#include "Pre35DSP.hpp"
#include "TestSupport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace pre35;
using namespace pre35test;

namespace
{

constexpr double kSr = 48000.0;
constexpr int    kOs = 8;

bool allFinite(const std::vector<float>& v)
{
    for (float x : v)
        if (! std::isfinite(x))
            return false;
    return true;
}

void testNonFiniteInput(Report& r)
{
    // The input sanitiser maps every non-finite sample to 0 before it can reach a
    // filter state. So the strong statement is not "the output looks alright" —
    // it is that a poisoned buffer and the same buffer with those samples already
    // zeroed render BIT-IDENTICALLY. Nothing survived; there is nothing to settle.
    auto render = [](bool poisoned) {
        Pre35DSP dsp;
        dsp.setPadIndex(0);
        dsp.setTrimPercent(50.0);
        dsp.setIronAmount(1.0);
        dsp.prepare(kSr, kOs);

        std::vector<float> buf(1024, 0.1f);
        buf[10]  = poisoned ? std::numeric_limits<float>::quiet_NaN()  : 0.0f;
        buf[11]  = poisoned ? std::numeric_limits<float>::infinity()   : 0.0f;
        buf[500] = poisoned ? -std::numeric_limits<float>::infinity()  : 0.0f;
        buf[501] = poisoned ? std::numeric_limits<float>::quiet_NaN()  : 0.0f;
        dsp.process(buf.data(), (int)buf.size());

        std::vector<float> quiet((size_t)(kSr * 0.5), 0.0f);
        dsp.process(quiet.data(), (int)quiet.size());
        buf.insert(buf.end(), quiet.begin(), quiet.end());
        return buf;
    };

    const auto poisoned = render(true);
    const auto clean    = render(false);

    r.check(allFinite(poisoned), "NaN/Inf input produces finite output");

    bool identical = poisoned.size() == clean.size();
    for (size_t i = 0; i < poisoned.size() && identical; ++i)
        identical = poisoned[i] == clean[i];
    r.check(identical, "a poisoned buffer renders identically to a pre-zeroed one");
}

void testDenormalDecay(Report& r)
{
    Pre35DSP dsp;
    dsp.setPadIndex(0);
    // Explicit, not inherited from the core's default: the assertions below are
    // for EXACT zero, so a future flip of that default would turn this gate into a
    // hiss measurement rather than a denormal one. testSilenceIn says it too.
    dsp.setNoiseEnabled(false);
    dsp.setTrimPercent(100.0);      // the longest tail the chain can have
    dsp.setIronAmount(1.0);
    dsp.prepare(kSr, kOs);

    std::vector<float> burst = makeSine(50.0, 0.5, 0.25, kSr);
    dsp.process(burst.data(), (int)burst.size());

    // THE TAIL IS GENUINELY LONG. The iron layer's mean-square detector releases
    // over 0.25 s, its envelope over 0.5 s, and while it is releasing the layer
    // emits a slowly falling near-DC term that the 3.9 Hz highpass then has to
    // chase. The last non-zero float lands around 17.7 s after the burst — and
    // the Python reference decays through the same numbers (5.72e-3 vs 5.70e-3 at
    // 0.5 s, 2.70e-11 vs 2.70e-11 at 8 s), so this is the model, not a leak.
    //
    // What matters here is that it TERMINATES: the cascade flushes its state below
    // kDenormFloor, which is ~2e208 times above the double denormal threshold, so
    // no part of the chain can ever grind on subnormals.
    std::vector<float> tail((size_t)(kSr * 25.0), 0.0f);
    dsp.process(tail.data(), (int)tail.size());
    r.check(allFinite(tail), "silence after a burst stays finite");

    size_t lastNonZero = 0;
    for (size_t i = 0; i < tail.size(); ++i)
        if (tail[i] != 0.0f)
            lastNonZero = i;
    r.note("last non-zero tail sample at " + std::to_string((double)lastNonZero / kSr) + " s");

    bool exactlyZero = true;
    for (size_t i = tail.size() - (size_t)kSr; i < tail.size(); ++i)
        exactlyZero = exactlyZero && (tail[i] == 0.0f);
    r.check(exactlyZero, "tail reaches exactly zero (no denormal grind)");
}

void testSilenceIn(Report& r)
{
    Pre35DSP dsp;
    dsp.setNoiseEnabled(false);
    dsp.setTrimPercent(100.0);
    dsp.prepare(kSr, kOs);

    // Not instantly zero, and that is the model rather than a bug: with no signal
    // the detector sits on its floor, u = 0, and the even Chebyshev term 2u^2 - 1
    // is -1 there, so the layer emits a constant -d2 * envFloor. That is 3e-10
    // before the amplifier and the highpass nulls it — but it nulls it
    // asymptotically, so the first hundred milliseconds are not silent.
    std::vector<float> quiet((size_t)(kSr * 5.0), 0.0f);
    dsp.process(quiet.data(), (int)quiet.size());
    r.check(allFinite(quiet), "silence in stays finite");

    r.below(linToDb(rms(quiet.data(), 4096)), -130.0,
            "silence-in residue is inaudible from the first block", "dBFS");

    bool settled = true;
    for (size_t i = quiet.size() - 4096; i < quiet.size(); ++i)
        settled = settled && (quiet[i] == 0.0f);
    r.check(settled, "silence in, exact silence out once the highpass has settled");
}

void testParameterFuzz(Report& r)
{
    Pre35DSP dsp;
    dsp.prepare(kSr, kOs);
    dsp.setNoiseEnabled(true);

    // A cheap deterministic stream — the point is to move every control while
    // audio is flowing, not to be statistically interesting.
    uint64_t s = 12345;
    auto next = [&]() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return (double)((s >> 33) & 0xFFFFFF) / 16777215.0;
    };

    bool ok = true;
    double peak = 0.0;
    std::vector<float> buf(256);
    for (int block = 0; block < 400; ++block)
    {
        // next() is inclusive of 1.0 (the mask can return 0xFFFFFF), so the naive
        // (int)(next() * 3.0) yields 3 once in 2^24. setPadIndex clamps, so that
        // was never an out-of-range index into kPads — but it made the fuzz
        // silently retest pad 2 instead of covering 0..2, so bound it here.
        dsp.setPadIndex(std::min((int)(next() * 3.0), coeffs::kNumPads - 1));
        dsp.setTrimPercent(next() * 100.0);
        dsp.setIronAmount(next() * 2.0);
        dsp.setAutoGain(next() > 0.5);
        dsp.setOutputGainDb(next() * 24.0 - 12.0);

        for (size_t i = 0; i < buf.size(); ++i)
            buf[i] = (float)(0.05 * std::sin(2.0 * kPiT * 220.0
                                             * (double)(block * 256 + (int)i) / kSr));
        dsp.process(buf.data(), (int)buf.size());
        ok = ok && allFinite(buf);
        for (float v : buf)
            peak = std::max(peak, (double)std::fabs(v));
    }
    r.check(ok, "output stays finite under continuous parameter automation");
    // -20 dBFS in with up to +58 dB of trim and +12 dB of output trim: the bound
    // is generous on purpose, it is there to catch a runaway, not to grade level.
    r.below(linToDb(peak), 60.0, "output stays bounded under automation", "dBFS");
}

void testDeterminism(Report& r)
{
    auto render = [](uint64_t seed) {
        Pre35DSP dsp;
        dsp.setNoiseSeed(seed);
        dsp.setNoiseEnabled(true);
        dsp.setTrimPercent(60.0);
        dsp.prepare(kSr, kOs);
        std::vector<float> buf((size_t)(kSr * 0.2), 0.0f);
        dsp.process(buf.data(), (int)buf.size());
        return buf;
    };

    const auto a = render(7), b = render(7), c = render(8);
    bool same = a.size() == b.size();
    for (size_t i = 0; i < a.size() && same; ++i)
        same = a[i] == b[i];
    r.check(same, "same seed renders bit-identically");

    bool differs = false;
    for (size_t i = 0; i < a.size() && ! differs; ++i)
        differs = a[i] != c[i];
    r.check(differs, "a different seed renders a different floor");
}

void testNoiseFloor(Report& r)
{
    // Input-referred to the AMPLIFIER input: the pad sits ahead of it, so 40 dB
    // of pad must NOT drop the hiss, and the trim must lift it one for one.
    auto floorDbfs = [](int padIndex, double trim) {
        Pre35DSP dsp;
        dsp.setNoiseSeed(0x1234);
        dsp.setNoiseEnabled(true);
        dsp.setPadIndex(padIndex);
        dsp.setTrimPercent(trim);
        dsp.prepare(kSr, kOs);
        std::vector<float> buf((size_t)(kSr * 4.0), 0.0f);
        dsp.process(buf.data(), (int)buf.size());
        // Skip the first 0.5 s: the 1/f shaper's own settling is not the floor.
        const size_t skip = (size_t)(kSr * 0.5);
        return linToDb(rms(buf.data() + skip, buf.size() - skip));
    };

    const double at0  = floorDbfs(0, 0.0);
    const double at40 = floorDbfs(2, 0.0);
    const double at0Full = floorDbfs(0, 100.0);

    r.note("floor: pad 0 trim 0 = " + std::to_string(at0) + " dBFS, pad 40 trim 0 = "
           + std::to_string(at40) + ", pad 0 trim 100 = " + std::to_string(at0Full));

    // Gated against the REFERENCE, measured the same way:
    //     render_ref.py --model model_ch1_send.json --pad P --trim T
    //                   --oversample 8 --noise --seed 0     (silence in)
    //     -> -79.6926 / -79.2751 / -54.9104 dBFS
    // and NOT against the naive -108.5 + taper, which is 0.79 dB high because it
    // ignores what the LF highpass does to the 1/f tail. The RNG differs from
    // numpy's by design, so the tolerance covers the seed-to-seed spread
    // (measured at 0.04 dB across six seeds) rather than demanding a bit-match.
    //
    // Re-baselined 2026-08-12 when the coefficients moved from the DIRECT-tap
    // model to the SEND-tap one. The floor dropped 3.54 dB across the board,
    // which is exactly the two models' taper difference (g0 33.411 -> 29.874):
    // the noise is input-referred, so it scales with the taper. Nothing about
    // the noise model itself changed.
    r.near(at0, -79.6926, 0.2, "noise floor, pad 0 trim 0, vs the reference render", "dBFS");
    r.near(at40, -79.2751, 0.2, "noise floor, pad 40 trim 0, vs the reference render", "dBFS");
    r.near(at0Full, -54.9104, 0.2, "noise floor, pad 0 trim 100, vs the reference render", "dBFS");

    // The headline property: the pad is AHEAD of the amplifier the noise is
    // referred to, so 39 dB of pad must not take 39 dB off the hiss. What little
    // it does change (0.42 dB) is its different LF highpass corner reshaping the
    // 1/f tail, and the reference moves by the same 0.42 dB: the two reference
    // floors quoted above differ by -79.2751 - -79.6926 = 0.4175 dB.
    r.below(std::fabs(at40 - at0), 1.0, "40 dB of pad does not attenuate the noise");
    r.near(at40 - at0, 0.4175, 0.15, "pad 40 vs pad 0 floor difference matches the reference");

    r.near(at0Full - at0, coeffs::kTaper.g1Db - coeffs::kTaper.g0Db, 0.2,
           "noise tracks the trim taper one for one", "dB");
}

} // namespace

int main()
{
    Report r("pre35 robust_test");
    testNonFiniteInput(r);
    testDenormalDecay(r);
    testSilenceIn(r);
    testParameterFuzz(r);
    testDeterminism(r);
    testNoiseFloor(r);
    return r.exitCode();
}

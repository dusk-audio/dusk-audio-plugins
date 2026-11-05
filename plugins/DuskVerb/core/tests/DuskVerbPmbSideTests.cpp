// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// ParallelMultibandTank's decorrelated side (setDecorrelatedSide), on the tank alone. With the
// legacy taps a band's side is line 2's read plus its R input undelayed: on a short band the
// tail collapses to the centre, and a mono input gets an anti-phase copy of itself on the side.
// With the side from line 1's read:
//   * L + R is the legacy tank's, to float rounding (every mono measure of the tank is unchanged);
//   * the tail decorrelates on a mono and on a decorrelated stereo input, short decay and long,
//     in the low bands where the legacy taps collapse it (band 0: +0.94 -> +0.2, measured);
//   * L and R stay balanced: read where line 1 stands, the side is covariant with the mid (the
//     two-pass paths 1->5 and 5->1 are equally long) and L ran 0.85 dB over R broadband, 1 dB in
//     the top band; read further back, -0.2 dB (legacy -0.07). One burst's per-band balance
//     wanders +-0.6 dB in either design (eight-line bands, few passes), so a band is judged on
//     its mean over bursts: within 0.6 dB of centre (in place: up to +0.98);
//   * the tank keeps no left/right provenance, as before (the lean is the engine's post-steer):
//     a hard-left and a hard-right input give the same balance;
//   * nothing reaches the output before the first delay line does;
//   * clear() and toggling leave no state behind.
// And the mode smear's band shares (setModeSmearBandShare): 1 in every band is the smear as it
// was, 0 in every band no smear at all, and a share confines the smear to its band. And the
// densifier coefficient (setDensifierCoeff): below 0 is the tank's own, bit for bit, a band's
// coefficient changes that band alone, and one over 0.75 plays as 0.75.

#include "dsp/ParallelMultibandTank.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
int failures = 0;

void checkf (bool ok, const char* fmt, ...)
{
    if (ok) return;
    va_list args;
    va_start (args, fmt);
    std::printf ("FAIL: ");
    std::vprintf (fmt, args);
    std::printf ("\n");
    va_end (args);
    ++failures;
}

constexpr double kSr = 48000.0;
constexpr int kBlock = 512;
constexpr double kBurstSeconds = 0.5, kLengthSeconds = 2.0;   // noise, then the tail

int samples (double seconds, double sampleRate) { return static_cast<int> (seconds * sampleRate); }

struct Bands { float t60[6], level[6], direct[6], width[6]; };
// Medium Drum Room's shipping table (short) and Vocal Hall's (long), widths flat at 1.
const Bands kShort { { 0.7972f, 0.85f, 0.8504f, 0.7594f, 0.7922f, 0.8217f }, { 0.9f, 0.9f, 0.6f, 0.45f, 0.35f, 0.3f },
                     { 0, 0, 0, 0, 0, 0 }, { 1, 1, 1, 1, 1, 1 } };
const Bands kLong  { { 2.1f, 1.93f, 1.55f, 1.13f, 0.99f, 0.78f }, { 0.74f, 1.18f, 1.12f, 1.2f, 0.66f, 0.24f },
                     { 0, 0, 0, 0, 0, 0 }, { 1, 1, 1, 1, 1, 1 } };

struct Stereo { std::vector<float> l, r; };

enum class Input { Mono, Decorrelated, Left, Right, Impulse };

Stereo input (Input kind, double sampleRate = kSr, uint32_t seed = 0x12345678u)
{
    const int length = samples (kLengthSeconds, sampleRate), burst = samples (kBurstSeconds, sampleRate);
    Stereo in { std::vector<float> (static_cast<size_t> (length)), std::vector<float> (static_cast<size_t> (length)) };
    if (kind == Input::Impulse)
    {
        in.l[0] = in.r[0] = 1.0f;
        return in;
    }
    uint32_t a = seed, b = seed ^ 0x8bb84b93u;
    const auto next = [] (uint32_t& s) { s = s * 1664525u + 1013904223u; return static_cast<float> (s >> 8) / 8388608.0f - 1.0f; };
    for (int i = 0; i < burst; ++i)
    {
        const float nl = 0.5f * next (a), nr = 0.5f * next (b);
        const size_t k = static_cast<size_t> (i);
        switch (kind)
        {
            case Input::Mono:         in.l[k] = in.r[k] = nl; break;
            case Input::Decorrelated: in.l[k] = nl; in.r[k] = nr; break;
            case Input::Left:         in.l[k] = nl; break;
            case Input::Right:        in.r[k] = nl; break;
            case Input::Impulse:      break;
        }
    }
    return in;
}

std::unique_ptr<ParallelMultibandTank> tank (const Bands& bands, bool side, double sampleRate = kSr)
{
    auto t = std::make_unique<ParallelMultibandTank>();
    t->prepare (sampleRate, kBlock);
    for (int b = 0; b < 6; ++b)
        t->setBand (b, bands.t60[b], bands.level[b], bands.direct[b], bands.width[b]);
    t->setDecorrelatedSide (side);
    return t;
}

Stereo run (ParallelMultibandTank& t, const Stereo& in)
{
    const int length = static_cast<int> (in.l.size());
    Stereo out { std::vector<float> (in.l.size()), std::vector<float> (in.r.size()) };
    for (int s = 0; s < length; s += kBlock)
        t.process (in.l.data() + s, in.r.data() + s, out.l.data() + s, out.r.data() + s, std::min (kBlock, length - s));
    return out;
}

Stereo render (const Bands& bands, bool side, Input kind, double sampleRate = kSr, uint32_t seed = 0x12345678u)
{
    const auto t = tank (bands, side, sampleRate);
    return run (*t, input (kind, sampleRate, seed));
}

// `bands` with every band but `b` silent.
Bands solo (const Bands& bands, int b)
{
    Bands out = bands;
    for (int k = 0; k < 6; ++k)
        out.level[k] = k == b ? bands.level[k] : 0.0f;
    return out;
}

// Over the tail, from 50 ms past the input's end to 1 s past it.
double correlation (const Stereo& x, double sampleRate = kSr)
{
    double lr = 0.0, ll = 0.0, rr = 0.0;
    const int burst = samples (kBurstSeconds, sampleRate);
    for (int i = burst + samples (0.05, sampleRate); i < burst + samples (1.0, sampleRate); ++i)
    {
        const double l = x.l[static_cast<size_t> (i)], r = x.r[static_cast<size_t> (i)];
        lr += l * r; ll += l * l; rr += r * r;
    }
    return lr / std::sqrt (std::max (ll * rr, 1.0e-300));
}

double balanceDb (const Stereo& x)          // L over R energy, whole render
{
    double ll = 0.0, rr = 0.0;
    for (size_t i = 0; i < x.l.size(); ++i) { ll += static_cast<double> (x.l[i]) * x.l[i]; rr += static_cast<double> (x.r[i]) * x.r[i]; }
    return 10.0 * std::log10 (std::max (ll, 1.0e-300) / std::max (rr, 1.0e-300));
}

void testMonoSumUnchanged()
{
    for (const Bands* bands : { &kShort, &kLong })
        for (Input kind : { Input::Mono, Input::Decorrelated, Input::Left, Input::Right, Input::Impulse })
        {
            const Stereo off = render (*bands, false, kind), on = render (*bands, true, kind);
            double peak = 0.0, worst = 0.0;
            for (size_t i = 0; i < off.l.size(); ++i)
            {
                const double sumOff = static_cast<double> (off.l[i]) + off.r[i], sumOn = static_cast<double> (on.l[i]) + on.r[i];
                peak = std::max (peak, std::abs (sumOff));
                worst = std::max (worst, std::abs (sumOn - sumOff));
            }
            checkf (peak > 1.0e-4, "the tank answers input %d", static_cast<int> (kind));
            checkf (worst <= 1.0e-5 * peak, "%s decay, input %d: L + R is the legacy tank's (worst %.3g of peak %.3g)",
                    bands == &kShort ? "short" : "long", static_cast<int> (kind), worst, peak);
        }
}

void testDecorrelates()
{
    for (const Bands* bands : { &kShort, &kLong })
    {
        const char* name = bands == &kShort ? "short" : "long";
        const double monoOff = correlation (render (*bands, false, Input::Mono));
        const double monoOn = correlation (render (*bands, true, Input::Mono));
        const double stereoOn = correlation (render (*bands, true, Input::Decorrelated));
        std::printf ("%-5s decay  tail L/R correlation: mono in %+.3f -> %+.3f, decorrelated in %+.3f\n",
                     name, monoOff, monoOn, stereoOn);
        checkf (std::abs (monoOn) < 0.15, "%s decay: a mono input's tail decorrelates (%+.3f)", name, monoOn);
        checkf (std::abs (stereoOn) < 0.15, "%s decay: a decorrelated input's tail stays decorrelated (%+.3f)", name, stereoOn);
    }
    // The low bands, where the legacy taps collapse the tail (the defect): measured on the short
    // table, band 0 +0.94 -> +0.23 and band 1 +0.81 -> +0.39 for a mono input.
    for (int b = 0; b < 2; ++b)
    {
        const double legacy = correlation (render (solo (kShort, b), false, Input::Mono));
        const double side = correlation (render (solo (kShort, b), true, Input::Mono));
        std::printf ("short decay, band %d alone: mono in %+.3f -> %+.3f\n", b, legacy, side);
        checkf (legacy > 0.75, "band %d: the legacy taps collapse a mono input (%+.3f, the defect)", b, legacy);
        checkf (side < 0.45 && side < legacy - 0.35, "band %d: the side decorrelates it (%+.3f)", b, side);
    }
}

void testBalance()
{
    for (const Bands* bands : { &kShort, &kLong })
    {
        const char* name = bands == &kShort ? "short" : "long";
        const double mono = balanceDb (render (*bands, true, Input::Mono));
        const double left = balanceDb (render (*bands, true, Input::Left));
        const double right = balanceDb (render (*bands, true, Input::Right));
        std::printf ("%-5s decay  L/R balance: mono %+.2f dB, hard left %+.2f dB, hard right %+.2f dB\n", name, mono, left, right);
        checkf (std::abs (mono) < 0.5, "%s decay: a mono input keeps L and R within 0.5 dB (%+.2f dB)", name, mono);
        checkf (std::abs (left - right) < 0.3, "%s decay: the side the input is on does not move the balance (%+.2f vs %+.2f dB)",
                name, left, right);
    }
}

// A band's balance for a mono input, averaged over eight bursts.
double meanBandBalanceDb (const Bands& bands, int b, double sampleRate)
{
    double sum = 0.0;
    for (uint32_t k = 0; k < 8; ++k)
        sum += balanceDb (render (solo (bands, b), true, Input::Mono, sampleRate, 0x2545f491u + 7919u * k));
    return sum / 8.0;
}

// Band by band: read in place, the top band sat at +0.98 dB and band 1 at +0.93 (long decay).
// Bands 3 and 4 stay within 0.5 dB either way and are left out (each band costs eight renders).
void testBandBalance()
{
    for (const Bands* bands : { &kShort, &kLong })
        for (int b : { 0, 1, 2, 5 })
        {
            const double mean = meanBandBalanceDb (*bands, b, kSr);
            checkf (std::abs (mean) < 0.6, "%s decay, band %d: a mono input averages within 0.6 dB of centre (%+.2f dB)",
                    bands == &kShort ? "short" : "long", b, mean);
        }
}

// The read offsets are times and the lines keep headroom for them at every rate: the side
// balances and decorrelates at 44.1 and 96 kHz as at 48 kHz (bursts, windows and lengths in
// seconds; the noise is white at each rate).
void testOtherRates()
{
    for (const double sampleRate : { 44100.0, 96000.0 })
        for (const Bands* bands : { &kShort, &kLong })
        {
            const char* name = bands == &kShort ? "short" : "long";
            const Stereo all = render (*bands, true, Input::Mono, sampleRate);
            const double corr = correlation (all, sampleRate);
            checkf (std::abs (corr) < 0.15, "%g Hz, %s decay: the tail decorrelates (%+.3f)", sampleRate, name, corr);
            checkf (std::abs (balanceDb (all)) < 0.5, "%g Hz, %s decay: L and R within 0.5 dB (%+.2f dB)", sampleRate, name, balanceDb (all));
            for (int b = 0; b < 2 && sampleRate > kSr; ++b)     // the lowest bands, at the higher rate
            {
                const double mean = meanBandBalanceDb (*bands, b, sampleRate);
                checkf (std::abs (mean) < 0.6, "%g Hz, %s decay, band %d: averages within 0.6 dB of centre (%+.2f dB)",
                        sampleRate, name, b, mean);
            }
        }
}

void testNothingUndelayed()
{
    // The shortest line (band 5, line 0) is 467 samples at 48 kHz: before it, the side-on tank
    // is silent, where the legacy side passes the band-filtered R input straight through.
    const Stereo on = render (kShort, true, Input::Impulse), off = render (kShort, false, Input::Impulse);
    double early = 0.0, legacyEarly = 0.0;
    for (int i = 0; i < 460; ++i)
    {
        early = std::max ({ early, static_cast<double> (std::abs (on.l[static_cast<size_t> (i)])), static_cast<double> (std::abs (on.r[static_cast<size_t> (i)])) });
        legacyEarly = std::max (legacyEarly, static_cast<double> (std::abs (off.l[static_cast<size_t> (i)])));
    }
    // The legacy mid carries the undelayed term on both taps, cancelled to float rounding.
    checkf (early < 1.0e-6, "nothing reaches the output before the first line (peak %g)", early);
    checkf (legacyEarly > 1.0e-3, "the legacy side passes the input undelayed (peak %g)", legacyEarly);
}

void testStateIsCleared()
{
    // clear() wipes the side filter: two tanks with the same history but for the side (on in
    // one, off in the other, whose side filter never runs) render alike once cleared. (clear()
    // keeps the wander LFO's phase, so a cleared tank is compared with a cleared tank.)
    auto withSide = tank (kShort, true), without = tank (kShort, false);
    run (*withSide, input (Input::Mono));
    run (*without, input (Input::Mono));
    withSide->clear();
    without->clear();
    without->setDecorrelatedSide (true);
    const Stereo a = run (*withSide, input (Input::Decorrelated)), b = run (*without, input (Input::Decorrelated));
    checkf (a.l == b.l && a.r == b.r, "clear() leaves no side state behind");

    // Switching on clears it too: a side switched off and on again starts as one never on.
    auto again = tank (kShort, true), never = tank (kShort, false);
    run (*again, input (Input::Mono));
    run (*never, input (Input::Mono));
    again->setDecorrelatedSide (false);
    again->setDecorrelatedSide (true);
    never->setDecorrelatedSide (true);
    const Stereo c = run (*again, input (Input::Decorrelated)), d = run (*never, input (Input::Decorrelated));
    checkf (c.l == d.l && c.r == d.r, "switching on starts the side filter over");

    // Off and on again mid-stream starts the side filter over, finite throughout.
    auto toggled = tank (kShort, true);
    const Stereo in = input (Input::Mono);
    const int length = static_cast<int> (in.l.size());
    Stereo out { std::vector<float> (in.l.size()), std::vector<float> (in.r.size()) };
    bool finite = true;
    for (int s = 0; s < length; s += kBlock)
    {
        const int n = std::min (kBlock, length - s);
        toggled->setDecorrelatedSide ((s / kBlock) % 3 != 1);
        toggled->process (in.l.data() + s, in.r.data() + s, out.l.data() + s, out.r.data() + s, n);
        for (int i = s; i < s + n; ++i)
            finite = finite && std::isfinite (out.l[static_cast<size_t> (i)]) && std::isfinite (out.r[static_cast<size_t> (i)]);
    }
    checkf (finite, "toggling mid-stream stays finite");
}
void testSmearShare()
{
    // The mode smear's band shares: 1 in every band is the smear as it was before shares existed,
    // 0 in every band reads exactly as no smear, and a share confines the smear to its band.
    const auto smeared = [] (const Bands& bands, const std::array<float, 6>* share, float depth) {
        auto t = tank (bands, false);
        if (share != nullptr)
            for (int b = 0; b < 6; ++b)
                t->setModeSmearBandShare (b, (*share)[static_cast<size_t> (b)]);
        t->setModeSmear (depth, 2.5f);
        return run (*t, input (Input::Mono));
    };
    const std::array<float, 6> all { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, none {}, band1 { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    const Stereo plain = smeared (kShort, nullptr, 80.0f), full = smeared (kShort, &all, 80.0f);
    checkf (plain.l == full.l && plain.r == full.r, "share 1 in every band is the smear as it was");
    const Stereo off = smeared (kShort, nullptr, 0.0f), zero = smeared (kShort, &none, 80.0f);
    checkf (off.l == zero.l && off.r == zero.r, "share 0 in every band reads as no smear");
    checkf (plain.l != off.l, "the smear changes the render at all");

    const Stereo otherOff = smeared (solo (kShort, 3), nullptr, 0.0f), other = smeared (solo (kShort, 3), &band1, 80.0f);
    checkf (otherOff.l == other.l && otherOff.r == other.r, "a smear shared to band 1 leaves band 3 still");
    const Stereo ownFull = smeared (solo (kShort, 1), nullptr, 80.0f), own = smeared (solo (kShort, 1), &band1, 80.0f);
    checkf (ownFull.l == own.l && ownFull.r == own.r, "band 1 takes its whole share");
}

void testDensifierCoeff()
{
    const auto densified = [] (const Bands& bands, int band, float k) {
        auto t = tank (bands, false);
        if (band >= 0)
            t->setDensifierCoeff (band, k);
        return run (*t, input (Input::Mono));
    };
    const Stereo plain = densified (kShort, -1, 0.0f);
    auto t = tank (kShort, false);
    for (int b = 0; b < 6; ++b)
    {
        t->setDensifierCoeff (b, 0.15f);
        t->setDensifierCoeff (b, -1.0f);
    }
    const Stereo restored = run (*t, input (Input::Mono));
    checkf (plain.l == restored.l && plain.r == restored.r, "a densifier coefficient below 0 restores the tank's own");
    const Stereo low = densified (kShort, 1, 0.15f);
    checkf (plain.l != low.l, "a band's densifier coefficient changes the render at all");
    const Stereo otherPlain = densified (solo (kShort, 3), -1, 0.0f), other = densified (solo (kShort, 3), 1, 0.15f);
    checkf (otherPlain.l == other.l && otherPlain.r == other.r, "band 1's densifier coefficient leaves band 3 alone");
    const Stereo capped = densified (kShort, 1, 0.75f), over = densified (kShort, 1, 2.0f);
    checkf (capped.l == over.l && capped.r == over.r, "a densifier coefficient over 0.75 plays as 0.75");
}
} // namespace

int main()
{
    testMonoSumUnchanged();
    testDecorrelates();
    testBalance();
    testBandBalance();
    testOtherRates();
    testNothingUndelayed();
    testStateIsCleared();
    testSmearShare();
    testDensifierCoeff();
    if (failures != 0)
    {
        std::printf ("%d failure(s)\n", failures);
        return 1;
    }
    std::printf ("PMB decorrelated side OK\n");
    return 0;
}

// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// The tanks' time-varying feedback matrix, on each tank alone: FDNReverb's line-pair turn
// (setFeedbackRotation) and DattorroTank's cross-feed turn (setCrossFeedRotation). The turn rides
// in its own instantiation of the loop and is orthogonal at every sample, so on each tank:
//   * a peak angle of 0 is the tank without it, bit for bit, also after it was set on and back;
//   * on, it moves the tail, and keeps the decay: T60 within 3 % at 0.3 rad and 0.5 Hz, within
//     5 % at the pi/2 and 10 Hz limits (no gain anywhere, so the loop stays as stable as it was);
//   * it replays: two tanks agree bit for bit, and clearBuffers() starts the turn and the delay
//     modulation over. (This also holds FDNReverb::clearBuffers() to the per-line rate spread
//     prepare() gives the delay-mod LFOs: it once re-seeded them all at the base rate, so they
//     stepped in lockstep after every host reset and the tank replayed only to -60 dB.)
// And the FDN's one-rate modulation (setModRateSpread(false), which the shimmer and NonLin engines
// keep because their presets were voiced on it): it reaches the tail, and clearBuffers() keeps it.

#include "dsp/DattorroTank.h"
#include "dsp/FDNReverb.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
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
constexpr double kBurstSeconds = 0.3, kLengthSeconds = 3.0;
constexpr float kDecaySeconds = 2.0f;

struct Stereo { std::vector<float> l, r; };

// Each tank's turn under one name.
void turn (FDNReverb& t, float peak, float rate) { t.setFeedbackRotation (peak, rate); }
void turn (DattorroTank& t, float peak, float rate) { t.setCrossFeedRotation (peak, rate); }

template <typename Tank>
void prepare (Tank& tank)
{
    tank.prepare (kSr, kBlock);
    tank.setDecayTime (kDecaySeconds);
}

// A deterministic noise burst, then silence.
template <typename Tank>
Stereo render (Tank& tank)
{
    const int n = static_cast<int> (kLengthSeconds * kSr), burst = static_cast<int> (kBurstSeconds * kSr);
    std::vector<float> inL (static_cast<size_t> (n), 0.0f), inR (static_cast<size_t> (n), 0.0f);
    uint32_t s = 0x1234567u;
    auto noise = [&s] { s = s * 1664525u + 1013904223u; return static_cast<float> (static_cast<int32_t> (s)) * (1.0f / 2147483648.0f); };
    for (int i = 0; i < burst; ++i) { inL[static_cast<size_t> (i)] = 0.5f * noise(); inR[static_cast<size_t> (i)] = 0.5f * noise(); }
    Stereo out { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
    for (int i = 0; i < n; i += kBlock)
    {
        const int m = std::min (kBlock, n - i);
        tank.process (inL.data() + i, inR.data() + i, out.l.data() + i, out.r.data() + i, m);
    }
    return out;
}

// T60 from the Schroeder curve of the tail after the burst, fitted from -5 to -25 dB.
double t60 (const Stereo& x)
{
    const size_t from = static_cast<size_t> (kBurstSeconds * kSr), n = x.l.size();
    std::vector<double> edc (n - from);
    double acc = 0.0;
    for (size_t i = n; i-- > from;)
    {
        acc += static_cast<double> (x.l[i]) * x.l[i] + static_cast<double> (x.r[i]) * x.r[i];
        edc[i - from] = acc;
    }
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / edc[0] + 1.0e-30);
        if (db > -5.0 || db < -25.0) continue;
        const double t = static_cast<double> (i) / kSr;
        sx += t; sy += db; sxx += t * t; sxy += t * db; ++count;
    }
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
    return -60.0 / slope;
}

bool identical (const Stereo& a, const Stereo& b)
{
    return a.l == b.l && a.r == b.r;
}

// Largest difference against the tail's peak, in dB.
double differenceDb (const Stereo& a, const Stereo& b)
{
    double peak = 0.0, diff = 0.0;
    for (size_t i = 0; i < a.l.size(); ++i)
    {
        peak = std::max ({ peak, std::fabs (static_cast<double> (a.l[i])), std::fabs (static_cast<double> (a.r[i])) });
        diff = std::max ({ diff, std::fabs (static_cast<double> (a.l[i]) - b.l[i]), std::fabs (static_cast<double> (a.r[i]) - b.r[i]) });
    }
    return 20.0 * std::log10 (diff / peak + 1.0e-30);
}

template <typename Tank>
void checkTank (const char* name)
{
    Tank plain;
    prepare (plain);
    const Stereo still = render (plain);
    const double t60Still = t60 (still);
    std::printf ("%s, no turn: T60 %.3f s\n", name, t60Still);

    // Off: never set, and set on then back to 0 before the first sample, are the same tank.
    {
        Tank tank;
        prepare (tank);
        turn (tank, 0.3f, 0.5f);
        turn (tank, 0.0f, 0.5f);
        checkf (identical (render (tank), still), "%s: peak 0 after 0.3 rad differs from the tank without the turn", name);
    }

    // On: the tail moves, the decay holds; at the limits too.
    struct Case { float peak, rate; double tolerance; };
    for (const Case c : { Case { 0.3f, 0.5f, 0.03 }, Case { 1.5707964f, 10.0f, 0.05 } })
    {
        Tank tank;
        prepare (tank);
        turn (tank, c.peak, c.rate);
        const Stereo moved = render (tank);
        const double t = t60 (moved), d = differenceDb (moved, still);
        std::printf ("%s, turn %.3f rad at %.1f Hz: T60 %.3f s (%+.2f %%), differs by %.1f dB\n", name,
                     static_cast<double> (c.peak), static_cast<double> (c.rate), t, 100.0 * (t / t60Still - 1.0), d);
        checkf (std::fabs (t / t60Still - 1.0) <= c.tolerance, "%s, turn %.3f rad at %.1f Hz: T60 %.3f s against %.3f s",
                name, static_cast<double> (c.peak), static_cast<double> (c.rate), t, t60Still);
        checkf (d > -40.0, "%s, turn %.3f rad at %.1f Hz leaves the tail as it was (%.1f dB)", name,
                static_cast<double> (c.peak), static_cast<double> (c.rate), d);
    }

    // Replay: two tanks agree, and clearBuffers() starts the turn and the delay modulation over.
    // The modulation is set rather than left at the defaults, so the check keeps its teeth.
    {
        Tank a, b;
        for (Tank* t : { &a, &b })
        {
            prepare (*t);
            t->setModDepth (0.5f);
            t->setModRate (1.0f);
            turn (*t, 0.3f, 0.5f);
        }
        const Stereo first = render (a);
        checkf (identical (first, render (b)), "%s: two tanks with the same turn differ", name);
        a.clearBuffers();
        const Stereo again = render (a);
        checkf (identical (first, again), "%s: after clearBuffers() the tank does not start over (%.1f dB)",
                name, differenceDb (again, first));
    }
}

// The one-rate modulation the shimmer and NonLin engines keep, at their tank's defaults (depth 0.5,
// 1 Hz): it changes the tail against the per-line spread, and a cleared tank replays it.
void checkOneRateModulation()
{
    const auto renderWith = [] (bool spread, bool clearedAfterARun)
    {
        FDNReverb tank;
        prepare (tank);
        tank.setModDepth (0.5f);
        tank.setModRate (1.0f);
        tank.setModRateSpread (spread);
        if (clearedAfterARun)
        {
            render (tank);
            tank.clearBuffers();
        }
        return render (tank);
    };
    const Stereo spread = renderWith (true, false), oneRate = renderWith (false, false);
    std::printf ("FDN, one delay-mod rate against the spread: differs by %.1f dB\n", differenceDb (oneRate, spread));
    checkf (! identical (oneRate, spread), "FDN: setModRateSpread(false) leaves the tail as it was");
    checkf (identical (renderWith (false, true), oneRate), "FDN: after clearBuffers() the one-rate modulation does not start over");
}
} // namespace

int main()
{
    checkTank<FDNReverb> ("FDN");
    checkOneRateModulation();
    checkTank<DattorroTank> ("Dattorro");
    std::printf (failures == 0 ? "all feedback rotation checks passed\n" : "%d feedback rotation check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}

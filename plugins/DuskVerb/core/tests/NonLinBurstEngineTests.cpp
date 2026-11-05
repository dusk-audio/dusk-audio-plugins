// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// NonLinBurstEngine gates: parity with the reference model, and the properties a classic
// nonlinear program must have (linear, block-size invariant, silent when idle, finite, no
// allocation on the audio thread, a decay control that means what it says).

#include "../dsp/NonLinBurstEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

static std::atomic<bool> gCountAllocations { false };
static std::atomic<int>  gAllocations { 0 };
void* operator new (std::size_t size)
{
    if (gCountAllocations.load (std::memory_order_relaxed))
        gAllocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

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

using duskverb::NonLinBurstEngine;

struct Stereo { std::vector<float> l, r; };

Stereo render (NonLinBurstEngine& e, const Stereo& in, int block)
{
    const int n = static_cast<int> (in.l.size());
    Stereo out { std::vector<float> (in.l.size()), std::vector<float> (in.r.size()) };
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min (block, n - i);
        e.process (in.l.data() + i, in.r.data() + i, out.l.data() + i, out.r.data() + i, m);
    }
    return out;
}

Stereo noiseBurst (int n, float scale)
{
    Stereo s { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
    std::uint32_t x = 12345;
    for (int i = 0; i < n / 4; ++i)
    {
        x = x * 1664525u + 1013904223u; s.l[static_cast<size_t> (i)] = scale * (static_cast<float> (x >> 8) / 8388608.0f - 1.0f);
        x = x * 1664525u + 1013904223u; s.r[static_cast<size_t> (i)] = scale * (static_cast<float> (x >> 8) / 8388608.0f - 1.0f);
    }
    return s;
}

// The tap layout at 48 kHz, knee 181.5 ms, is the reference model's (proto.py at the fitted
// knee, 2026-10-24): same count, same delays, same line choice, gains to rounding of the
// fitted constants. A change on either side without the other fails here.
void testMatchesReferenceModel()
{
    NonLinBurstEngine e;
    e.prepare (48000.0, 256);
    e.setKnee (181.5f);
    float silence[1] = { 0.0f }, out[1];
    e.process (silence, silence, out, out, 1);           // applies the knee
    struct Golden { int count; int delay[6]; float gain[6]; int dark[12]; };
    const Golden golden[2] = {
        { 179, { 648, 1167, 1611, 2112, 2327, 2407 },
          { -0.0353215f, 0.426402f, 0.0121816f, 0.0410722f, 0.169531f, 0.161831f },
          { 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0 } },
        { 178, { 648, 1447, 1742, 1913, 2240, 2383 },
          { 0.0736731f, 0.170351f, -0.113638f, -0.0884075f, -0.0456963f, 0.164361f },
          { 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0 } },
    };
    for (int ch = 0; ch < 2; ++ch)
    {
        const Golden& g = golden[ch];
        checkf (e.tapCount (ch) == g.count, "channel %d: %d taps, reference %d", ch, e.tapCount (ch), g.count);
        for (int k = 0; k < 6; ++k)
        {
            const auto& t = e.tap (ch, k);
            checkf (t.delay == g.delay[k], "channel %d tap %d: delay %d, reference %d", ch, k, t.delay, g.delay[k]);
            checkf (std::abs (t.gain - g.gain[k]) <= 1e-3f * std::abs (g.gain[k]) + 1e-6f,
                    "channel %d tap %d: gain %g, reference %g", ch, k, t.gain, g.gain[k]);
        }
        for (int k = 0; k < 12; ++k)
            checkf (e.tap (ch, k).dark == (g.dark[k] != 0), "channel %d tap %d: line choice differs from the reference", ch, k);
    }
}

void testLinearAndBlockSizeInvariant()
{
    const int n = 48000;
    NonLinBurstEngine a, b;
    a.prepare (48000.0, 4096);
    a.setKnee (181.5f);
    b.prepare (48000.0, 4096);
    b.setKnee (181.5f);
    const Stereo ref = render (a, noiseBurst (n, 0.5f), 256);
    const Stereo quiet = render (b, noiseBurst (n, 0.05f), 256);
    double e = 0.0, r = 0.0;
    for (int i = 0; i < n; ++i)
    {
        e += static_cast<double> (ref.l[static_cast<size_t> (i)]) * ref.l[static_cast<size_t> (i)];
        const double d = 10.0 * quiet.l[static_cast<size_t> (i)] - ref.l[static_cast<size_t> (i)];
        r += d * d;
    }
    checkf (e > 0.0 && 10.0 * std::log10 (r / e) < -100.0, "-20 dB in is not -20 dB out (residual %.1f dB)", 10.0 * std::log10 (r / e));

    for (int block : { 1, 7, 64, 1000, 4096 })
    {
        NonLinBurstEngine c;
        c.prepare (48000.0, 4096);
        c.setKnee (181.5f);
        const Stereo o = render (c, noiseBurst (n, 0.5f), block);
        bool same = true;
        for (int i = 0; i < n; ++i)
            same &= o.l[static_cast<size_t> (i)] == ref.l[static_cast<size_t> (i)] && o.r[static_cast<size_t> (i)] == ref.r[static_cast<size_t> (i)];
        checkf (same, "block size %d changes the output", block);
    }
}

void testSilentFiniteAndCleared()
{
    NonLinBurstEngine e;
    e.prepare (48000.0, 512);
    e.setKnee (283.5f);
    const int n = 3 * 48000;
    Stereo in { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
    in.l[0] = in.r[0] = 1.0f;
    const Stereo out = render (e, in, 512);
    bool finite = true;
    double late = 0.0, peak = 0.0;
    for (int i = 0; i < n; ++i)
    {
        finite &= std::isfinite (out.l[static_cast<size_t> (i)]) && std::isfinite (out.r[static_cast<size_t> (i)]);
        peak = std::max (peak, static_cast<double> (std::abs (out.l[static_cast<size_t> (i)])));
        if (i > 2 * 48000) late = std::max (late, static_cast<double> (std::abs (out.l[static_cast<size_t> (i)])));
    }
    checkf (finite, "the impulse response is not finite");
    checkf (late < peak * 1e-6, "still sounding 2 s after an impulse (%.1f dB re peak)", 20.0 * std::log10 (late / peak + 1e-30));

    // clear() is silence at once.
    e.clear();
    Stereo zero { std::vector<float> (4800), std::vector<float> (4800) };
    const Stereo after = render (e, zero, 512);
    bool silent = true;
    for (float v : after.l) silent &= v == 0.0f;
    for (float v : after.r) silent &= v == 0.0f;
    checkf (silent, "not silent after clear()");
}

// Decay Time is the time from the input to -60 dB under the plateau, measured on the output.
void testDecayMeansWhatItSays()
{
    for (float seconds : { 0.27f, 0.35f, 0.46f, 0.6f, 0.75f })
    {
        NonLinBurstEngine e;
        e.prepare (48000.0, 256);
        e.setKnee (NonLinBurstEngine::kneeForDecaySeconds (seconds));
        const int n = 48000;
        Stereo in { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
        in.l[0] = in.r[0] = 1.0f;
        const Stereo out = render (e, in, 256);
        // 10 ms power envelope on a 1 ms grid, re the median plateau.
        std::vector<double> env;
        for (int t = 0; t + 480 <= n; t += 48)
        {
            double p = 0.0;
            for (int i = t; i < t + 480; ++i)
                p += static_cast<double> (out.l[static_cast<size_t> (i)]) * out.l[static_cast<size_t> (i)]
                   + static_cast<double> (out.r[static_cast<size_t> (i)]) * out.r[static_cast<size_t> (i)];
            env.push_back (10.0 * std::log10 (p / 480.0 + 1e-30));
        }
        std::vector<double> body (env.begin() + 40, env.begin() + 100);
        std::nth_element (body.begin(), body.begin() + 30, body.end());
        const double plateau = body[30];
        int last = 0;
        for (int i = 0; i < static_cast<int> (env.size()); ++i)
            if (env[static_cast<size_t> (i)] >= plateau - 60.0) last = i;
        const double t60 = last + 5.0;     // the 10 ms window is centred 5 ms after its start
        checkf (std::abs (t60 - seconds * 1000.0) <= 15.0, "Decay %.2f s: the output reaches -60 dB at %.0f ms", seconds, t60);
    }
}

void testNoAllocationOnTheAudioThread()
{
    NonLinBurstEngine e;
    e.prepare (48000.0, 256);
    std::vector<float> l (256, 0.1f), r (256, 0.1f), ol (256), orr (256);
    gAllocations.store (0);
    gCountAllocations.store (true);
    for (int i = 0; i < 50; ++i)
    {
        e.setKnee (100.0f + 10.0f * static_cast<float> (i));
        e.setHfDamp (i % 2 ? 4250.0f : NonLinBurstEngine::kHfDampOffHz);
        e.setPreDelay (static_cast<float> (i));
        e.process (l.data(), r.data(), ol.data(), orr.data(), 256);
    }
    e.clear();
    gCountAllocations.store (false);
    checkf (gAllocations.load() == 0, "%d heap allocations on the audio thread", gAllocations.load());
}

void testSampleRates()
{
    // The burst keeps its timing at every rate: the last -40 dB point within 3 %.
    auto lastMinus40 = [] (double sr) {
        NonLinBurstEngine e;
        e.prepare (sr, 512);
        e.setKnee (181.5f);
        const int n = static_cast<int> (sr * 0.6);
        Stereo in { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
        in.l[0] = in.r[0] = 1.0f;
        const Stereo out = render (e, in, 512);
        const int w = static_cast<int> (sr * 0.01), hop = static_cast<int> (sr * 0.001);
        std::vector<double> env;
        for (int t = 0; t + w <= n; t += hop)
        {
            double p = 0.0;
            for (int i = t; i < t + w; ++i) p += static_cast<double> (out.l[static_cast<size_t> (i)]) * out.l[static_cast<size_t> (i)];
            env.push_back (10.0 * std::log10 (p / w + 1e-30));
        }
        double peak = -1e9;
        for (double v : env) peak = std::max (peak, v);
        int last = 0;
        for (int i = 0; i < static_cast<int> (env.size()); ++i) if (env[static_cast<size_t> (i)] >= peak - 40.0) last = i;
        return static_cast<double> (last);
    };
    const double ref = lastMinus40 (48000.0);
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        const double t = lastMinus40 (sr);
        checkf (std::abs (t - ref) <= 0.03 * ref, "%.0f Hz: last -40 dB at %.0f ms, 48 kHz at %.0f ms", sr, t, ref);
    }
}

bool sameTaps (const NonLinBurstEngine& a, const NonLinBurstEngine& b, int delayOffset)
{
    for (int ch = 0; ch < 2; ++ch)
    {
        if (a.tapCount (ch) != b.tapCount (ch))
            return false;
        for (int k = 0; k < a.tapCount (ch); ++k)
        {
            const auto& x = a.tap (ch, k);
            const auto& y = b.tap (ch, k);
            if (x.gain != y.gain || x.dark != y.dark || x.delay + delayOffset != y.delay)
                return false;
        }
    }
    return true;
}

// A knee at rest is applied exactly, whatever path led to it: a nearby knee first and then
// the target twice (a knob coming to rest within the 0.25 ms step) lands on the taps the
// target alone gives.
// Silence past any tap-set crossfade, so a change that waited for one has been applied
// (engines prepared at 48 kHz with 256-sample blocks).
void settle (NonLinBurstEngine& e)
{
    static float silence[256] = {}, out[256];
    const int fade = static_cast<int> (std::ceil (NonLinBurstEngine::kTapFadeMs * 48.0f));
    for (int done = 0; done < fade + 512; done += 256)
        e.process (silence, silence, out, out, 256);
}

void testKneeAtRestIsPathIndependent()
{
    float silence[1] = { 0.0f }, out[1];
    NonLinBurstEngine direct, stepped;
    direct.prepare (48000.0, 256);
    stepped.prepare (48000.0, 256);
    direct.setKnee (181.5f);
    direct.process (silence, silence, out, out, 1);
    stepped.setKnee (181.4f);
    stepped.process (silence, silence, out, out, 1);
    stepped.setKnee (181.5f);
    stepped.process (silence, silence, out, out, 1);
    settle (stepped);
    checkf (! sameTaps (direct, stepped, 0), "a moving knee within 0.25 ms is not followed at once");
    stepped.setKnee (181.5f);
    stepped.process (silence, silence, out, out, 1);
    settle (stepped);
    checkf (sameTaps (direct, stepped, 0), "a knee at rest lands on different taps through a nearby one");
}

// Pre-delay moves every tap by the same time and changes nothing else.
void testPreDelayOnlyMovesTheTaps()
{
    float silence[1] = { 0.0f }, out[1];
    NonLinBurstEngine plain, delayed;
    plain.prepare (48000.0, 256);
    delayed.prepare (48000.0, 256);
    plain.setKnee (181.5f);
    delayed.setKnee (181.5f);
    delayed.setPreDelay (100.0f);
    delayed.process (silence, silence, out, out, 1);
    settle (delayed);
    delayed.setPreDelay (37.5f);            // 1800 samples at 48 kHz
    plain.process (silence, silence, out, out, 1);
    delayed.process (silence, silence, out, out, 1);
    settle (delayed);
    checkf (sameTaps (plain, delayed, 1800), "a 37.5 ms pre-delay is not the same taps 1800 samples later");
}

// Switching the HF damping in starts from the signal: damping in during a loud burst, out
// before it dies, then in again in silence must stay silent instead of replaying the state
// the filter held when it went out.
void testHfDampSwitchInIsClean()
{
    NonLinBurstEngine e;
    e.prepare (48000.0, 512);
    e.setKnee (181.5f);
    e.setHfDamp (4250.0f);
    const int loudLength = 14336;           // 0.3 s of noise, a whole number of blocks
    Stereo loud = noiseBurst (4 * loudLength, 0.5f);
    loud.l.resize (static_cast<size_t> (loudLength));
    loud.r.resize (static_cast<size_t> (loudLength));
    const Stereo ringing = render (e, loud, 512);
    checkf (std::abs (ringing.l.back()) > 1e-3f, "the burst is still sounding when the damping goes out");
    e.setHfDamp (NonLinBurstEngine::kHfDampOffHz);
    const Stereo quiet { std::vector<float> (2 * 48000), std::vector<float> (2 * 48000) };
    render (e, quiet, 512);
    e.setHfDamp (4250.0f);
    const Stereo after = render (e, Stereo { std::vector<float> (512), std::vector<float> (512) }, 512);
    float peak = 0.0f;
    for (size_t i = 0; i < after.l.size(); ++i)
        peak = std::max ({ peak, std::abs (after.l[i]), std::abs (after.r[i]) });
    checkf (peak < 1e-9f, "switching the damping in after silence emits %g", peak);
}
// Moving the knee or the pre-delay must not zipper or click: each new tap set crossfades in.
// A 1 kHz sine goes through while the control sweeps; what lands above 6 kHz (a 48 dB/oct
// high-pass puts the tone itself near -124 dB) is splatter. Measured with the fade: -119 dB
// for the knee and -87 dB for the pre-delay; stepped instead: -63 and -41 dB.
void testSweepsDoNotZipper()
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr) * 3, block = 64;
    for (int mode = 0; mode < 2; ++mode)
    {
        NonLinBurstEngine e;
        e.prepare (sr, block);
        e.setKnee (150.0f);
        std::vector<float> in (static_cast<size_t> (n)), l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
            in[static_cast<size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 1000.0 * i / sr));
        for (int i = 0; i < n; i += block)
        {
            const double t = i / sr;
            if (mode == 0) e.setKnee (static_cast<float> (150.0 + 50.0 * std::sin (2.0 * 3.14159265358979 * 0.5 * t)));
            else           e.setPreDelay (static_cast<float> (20.0 + 10.0 * std::sin (2.0 * 3.14159265358979 * 0.5 * t)));
            e.process (in.data() + i, in.data() + i, l.data() + i, r.data() + i, block);
        }
        duskaudio::Biquad hp[4];
        for (auto& f : hp) f.setCoeffs (duskaudio::Biquad::highPass (sr, 6000.0f, 0.70710678f));
        double total = 0.0, splatter = 0.0;
        for (int i = 0; i < n; ++i)
        {
            float y = l[static_cast<size_t> (i)];
            for (auto& f : hp) y = f.process (y);         // from the start: no filter switch-on step
            if (i < static_cast<int> (sr))
                continue;                                  // count past the first second
            total += static_cast<double> (l[static_cast<size_t> (i)]) * l[static_cast<size_t> (i)];
            splatter += static_cast<double> (y) * y;
        }
        const double db = 10.0 * std::log10 (splatter / total + 1e-30);
        checkf (db < -80.0, "%s: %.1f dB above 6 kHz re the output (zipper or clicks)",
                mode == 0 ? "knee sweep" : "pre-delay sweep", db);
    }
}
} // namespace

int main()
{
    testMatchesReferenceModel();
    testLinearAndBlockSizeInvariant();
    testSilentFiniteAndCleared();
    testDecayMeansWhatItSays();
    testNoAllocationOnTheAudioThread();
    testSampleRates();
    testSweepsDoNotZipper();
    testKneeAtRestIsPathIndependent();
    testPreDelayOnlyMovesTheTaps();
    testHfDampSwitchInIsClean();
    if (failures != 0)
    {
        std::printf ("%d NonLinBurstEngine check(s) failed\n", failures);
        return 1;
    }
    std::printf ("NonLinBurstEngine: all checks passed\n");
    return 0;
}

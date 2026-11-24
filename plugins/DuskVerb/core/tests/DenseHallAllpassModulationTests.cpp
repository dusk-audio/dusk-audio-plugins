// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// DenseHallReverb::setAllpassModulation: with both on, the tank renders what it rendered before
// the setting existed; with the input allpasses still, what the tank is fed keeps its top octave;
// with the loop allpasses still as well, the top octave also lasts longer; and a prepare() keeps
// the setting. The legacy plug-in's renders are byte-compared against a build without the setting
// when it changes (it never passes anything but the default).

#include "DenseHallReverb.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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
constexpr int kBlock = 256;
constexpr int kFrames = 48000;      // one second of impulse response

struct Stereo { std::vector<float> l, r; };

void configure (DenseHallReverb& tank)
{
    tank.setDecayTime (2.5f);
    tank.setSize (0.55f);
    tank.setModDepth (0.3f);
    tank.setModRate (1.0f);
}

// One second of impulse response, the tank prepared afresh.
Stereo impulseResponse (DenseHallReverb& tank, double sampleRate = kSr)
{
    tank.prepare (sampleRate, kBlock);
    configure (tank);
    tank.clear();
    Stereo in { std::vector<float> (kFrames, 0.0f), std::vector<float> (kFrames, 0.0f) };
    Stereo out = in;
    in.l[0] = in.r[0] = 1.0f;
    for (int i = 0; i < kFrames; i += kBlock)
    {
        const int n = std::min (kBlock, kFrames - i);
        tank.process (in.l.data() + i, in.r.data() + i, out.l.data() + i, out.r.data() + i, n);
    }
    return out;
}

// Energy of the left channel between two times inside loHz..hiHz (Goertzel every 100 Hz), in dB.
double bandDb (const Stereo& x, double loHz, double hiHz, double fromS, double toS, double sampleRate = kSr)
{
    const int a = static_cast<int> (fromS * sampleRate), b = static_cast<int> (toS * sampleRate);
    double total = 0.0;
    for (double hz = loHz; hz <= hiHz; hz += 100.0)
    {
        const double w = 2.0 * 3.14159265358979323846 * hz / sampleRate, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = a; i < b; ++i)
        {
            const double s0 = static_cast<double> (x.l[static_cast<size_t> (i)]) + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        total += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return 10.0 * std::log10 (total + 1e-300);
}

bool same (const Stereo& a, const Stereo& b)
{
    return std::memcmp (a.l.data(), b.l.data(), a.l.size() * sizeof (float)) == 0
        && std::memcmp (a.r.data(), b.r.data(), a.r.size() * sizeof (float)) == 0;
}

void testDefaultIsTheTankAsItWas()
{
    DenseHallReverb untouched, on;
    on.setAllpassModulation (true, true);
    const Stereo a = impulseResponse (untouched), b = impulseResponse (on);
    checkf (same (a, b), "both modulations on renders differently from a tank the setting never touched");

    DenseHallReverb back;
    back.setAllpassModulation (false, false);
    back.setAllpassModulation (true, true);
    checkf (same (a, impulseResponse (back)), "off and on again does not render as on");
}

void testStillAllpassesKeepTheTop()
{
    DenseHallReverb modulated, stillInput, stillBoth;
    stillInput.setAllpassModulation (false, true);
    stillBoth.setAllpassModulation (false, false);
    const Stereo m = impulseResponse (modulated), i = impulseResponse (stillInput), b = impulseResponse (stillBoth);

    // What the tank is fed: the first 150 ms, before the loop has gone round more than twice.
    const double mid = bandDb (i, 500.0, 4000.0, 0.0, 0.15) - bandDb (m, 500.0, 4000.0, 0.0, 0.15);
    const double top = bandDb (i, 15000.0, 17000.0, 0.0, 0.15) - bandDb (m, 15000.0, 17000.0, 0.0, 0.15);
    std::printf ("DenseHall: input allpasses still, first 150 ms: %+.2f dB at 0.5-4 kHz, %+.2f dB at 15-17 kHz\n", mid, top);
    checkf (std::abs (mid) < 1.0, "still input allpasses change 0.5-4 kHz by %+.2f dB", mid);
    checkf (top > 6.0, "still input allpasses give back only %+.2f dB at 15-17 kHz (ten cubic reads lose about 15)", top);

    // The loop: later in the tail the top octave has been round more often, so it gains more.
    const double early = bandDb (b, 15000.0, 17000.0, 0.0, 0.15) - bandDb (i, 15000.0, 17000.0, 0.0, 0.15);
    const double late = bandDb (b, 15000.0, 17000.0, 0.5, 1.0) - bandDb (i, 15000.0, 17000.0, 0.5, 1.0);
    std::printf ("DenseHall: loop allpasses still as well, 15-17 kHz: %+.2f dB in the first 150 ms, %+.2f dB at 0.5-1 s\n", early, late);
    checkf (late > early + 3.0, "still loop allpasses do not slow the top octave's decay (%+.2f dB late, %+.2f dB early)", late, early);
    // Below 4 kHz a cubic read loses next to nothing, so the mids move only as far as one tail's
    // modes differ from another's.
    const double lateMid = bandDb (b, 500.0, 4000.0, 0.5, 1.0) - bandDb (i, 500.0, 4000.0, 0.5, 1.0);
    checkf (std::abs (lateMid) < 4.0 && late > lateMid + 8.0,
            "still loop allpasses change 0.5-4 kHz at 0.5-1 s by %+.2f dB (15-17 kHz by %+.2f dB)", lateMid, late);
}

void testPrepareKeepsTheSetting()
{
    DenseHallReverb still, modulated;
    still.setAllpassModulation (false, true);
    still.prepare (96000.0, kBlock);                    // a first prepare at another rate
    const Stereo s = impulseResponse (still), m = impulseResponse (modulated);    // both prepared again, at 48 kHz
    const double top = bandDb (s, 15000.0, 17000.0, 0.0, 0.15) - bandDb (m, 15000.0, 17000.0, 0.0, 0.15);
    checkf (top > 6.0, "after another prepare the input allpasses are modulated again (%+.2f dB at 15-17 kHz)", top);
}
} // namespace

int main()
{
    testDefaultIsTheTankAsItWas();
    testStillAllpassesKeepTheTop();
    testPrepareKeepsTheSetting();
    if (failures != 0)
    {
        std::printf ("%d DenseHall allpass modulation check(s) failed\n", failures);
        return 1;
    }
    std::printf ("DenseHall allpass modulation: all checks passed\n");
    return 0;
}

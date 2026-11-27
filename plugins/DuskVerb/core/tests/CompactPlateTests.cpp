// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#include "dsp/CompactPlate.hpp"

#include <cstdio>

namespace
{
using Plate = duskverb::CompactPlate;
using Stereo = std::array<std::vector<float>, 2>;
constexpr Plate::Controls initial {12, 1, 0.15f, 0.4f, 0.6f, 0.5f,
                                   0.37f, 1.51f, 1, 1, 1, 280, 5200, false};

Stereo silence (int count)
{
    return {std::vector<float> (count), std::vector<float> (count)};
}

double energy (const Stereo& data, int first)
{
    double result = 0;
    for (const auto& channel : data)
        for (std::size_t i = first; i < channel.size(); ++i)
            result += static_cast<double> (channel[i]) * channel[i];
    return result;
}

// Change the requested size again within the 15 ms geometry crossfade.
Stereo render (Plate& plate, int rate, int block)
{
    Stereo s = silence (rate);
    s[0][0] = 0.75f;
    s[1][31] = -0.5f;
    auto c = initial;
    int pos = 0;
    for (int end : {rate / 12, rate / 12 + 41, rate})
    {
        plate.set (c);
        while (pos < end)
        {
            int span = std::min (end - pos, block);
            plate.process (nullptr, nullptr, 0);
            plate.process (s[0].data() + pos, s[1].data() + pos, span);
            pos += span;
        }
        c.size += 0.35f;
        c.predelay += 10;
    }
    return s;
}

bool check (bool condition, int rate, const char* message)
{
    if (!condition) std::printf ("FAIL %d Hz %s\n", rate, message);
    return condition;
}

bool decayAtCrossovers (int rate, float low, float high)
{
    Plate plate;
    plate.prepare (rate);
    auto c = initial;
    c.decayRatio = 5 / duskverb::vocalplate::shownDecay;
    c.bass = 2.5f;
    c.mid = 0.3f;
    c.treble = 1.04f;
    c.modDepth = 0.96f;
    c.crossover = low;
    c.highCrossover = high;
    plate.set (c);
    double first = 0, last = 0;
    auto s = silence (rate);
    for (int second = 0; second < 20; ++second)
    {
        for (auto& channel : s) std::fill (channel.begin(), channel.end(), 0.0f);
        if (second == 0) s[0][0] = 1;
        plate.process (s[0].data(), s[1].data(), rate);
        if (second == 0) first = energy (s, 0);
        if (second == 19) last = energy (s, 0);
    }
    std::printf ("%d Hz, crossovers %.0f/%.0f: first %.4g, twentieth %.4g\n", rate, low, high, first, last);
    return check (first > 0.01 && last < first * 0.01, rate, "excited feedback must decay");
}

bool smoothControl (bool depth)
{
    constexpr int rate = 48000, change = rate / 2;
    auto run = [&] (bool automate) {
        Plate plate;
        plate.prepare (rate);
        auto c = initial;
        c.erLevel = c.modDepth = 0;
        plate.set (c);
        auto s = silence (change + 1440);
        for (int n = 0; n < change + 1440; ++n)
            s[0][n] = s[1][n] = 0.3f * std::sin (n * (6.28318530718 * 7000 / rate));
        plate.process (s[0].data(), s[1].data(), change);
        if (depth) c.modDepth = 1; else c.erLevel = 1;
        if (automate) plate.set (c);
        plate.process (s[0].data() + change, s[1].data() + change, 1440);
        return s;
    };
    const auto held = run (false), changed = run (true);
    const auto peakDifference = [&] (int begin, int end) {
        float peak = 0;
        for (int n = change + begin; n < change + end; ++n)
            for (int ch = 0; ch < 2; ++ch)
                peak = std::max (peak, std::abs (held[ch][n] - changed[ch][n]));
        return peak;
    };
    const float first = peakDifference (0, 4), settled = peakDifference (960, 1440);
    std::printf ("%s step: first four samples %.4g, settled %.4g\n", depth ? "depth" : "early gain", first, settled);
    return check (settled > 0.01f && first < settled * 0.08f, rate, "control moves without a step");
}
}

int main()
{
    bool ok = true;
    ok &= smoothControl (false);
    ok &= smoothControl (true);
    for (int rate : {44100, 48000, 96000})
    {
        ok &= decayAtCrossovers (rate, 4000, 5200);
        ok &= decayAtCrossovers (rate, 4000, 1000);
        Plate plate;
        plate.prepare (rate);
        const auto contiguous = render (plate, rate, 128);
        plate.prepare (rate);
        const auto split = render (plate, rate, 17);
        ok &= check (energy (contiguous, rate / 8) > 0.001 && contiguous == split, rate,
                     "re-prepare and split callbacks replay the same sounding transition");
        plate.clear();
        ok &= check (render (plate, rate, 128) == contiguous, rate,
                     "clear replays a previously sounding voice");

        plate.clear();
        auto c = initial;
        plate.set (c);
        Stereo onset = silence (rate / 8);
        onset[0][0] = 1;
        plate.process (onset[0].data(), onset[1].data(), rate / 8);
        c.freeze = true;
        plate.set (c);
        Stereo tail = silence (3 * rate);
        plate.process (tail[0].data(), tail[1].data(), 3 * rate);
        const double held = energy (tail, 2 * rate);
        c.freeze = false;
        plate.set (c);
        tail = silence (3 * rate);
        plate.process (tail[0].data(), tail[1].data(), 3 * rate);
        ok &= check (held > 0.001 && energy (tail, 2 * rate) < held * 0.0001, rate,
                     "Freeze holds the excited tank and release lets it decay");

        plate.clear();
        c.freeze = true;
        plate.set (c);
        onset = silence (rate / 3);
        onset[0][0] = 1;
        plate.process (onset[0].data(), onset[1].data(), rate / 3);
        const double frozenInput = energy (onset, 0);
        c.freeze = false;
        plate.set (c);
        onset[0][rate / 16] = 1;
        plate.process (onset[0].data(), onset[1].data(), rate / 3);
        ok &= check (frozenInput == 0 && energy (onset, rate / 16) > 0.001, rate,
                     "Freeze rejects fresh input and release accepts it");
    }
    if (ok) std::puts ("Compact plate replay, automation and freeze passed at 44100, 48000 and 96000 Hz");
    return ok ? 0 : 1;
}

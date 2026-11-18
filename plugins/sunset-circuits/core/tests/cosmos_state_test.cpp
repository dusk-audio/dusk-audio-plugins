// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later.
// Lifecycle coverage for the Cosmos paths. Allocations here are test setup.
#include "Effects.hpp"
#include "FilterEngine.hpp"
#include <cmath>
#include <cstdio>

using namespace msynth;

static int failures = 0;
static void check(bool ok, const char* name, int sr, int mode)
{
    std::printf("%s %s %d Hz mode %d\n", ok ? "PASS" : "FAIL", name, sr, mode);
    failures += !ok;
}

static void drive(CosmosChorusEffect& effect, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        float l = std::sin((float)i * 0.11f), r = l * .7f;
        effect.process(l, r);
    }
}

static bool same(CosmosChorusEffect& a, CosmosChorusEffect& b)
{
    bool equal = true;
    for (int i = 0; i < 8192; ++i)
    {
        float al = std::sin((float)i * .07f) * .2f, ar = al * .6f;
        float bl = al, br = ar;
        a.process(al, ar); b.process(bl, br);
        equal &= al == bl && ar == br && std::isfinite(al) && std::isfinite(ar);
    }
    return equal;
}

int main()
{
    for (const int sr : {8000, 22050, 44100, 48000, 96000})
    {
        for (int m = 1; m <= 3; ++m)
        {
            const auto mode = (CosmosChorusMode)m;
            CosmosChorusEffect a, b;
            a.prepare(sr, 256); b.prepare(sr, 256);
            a.setMode(mode); b.setMode(mode);
            drive(a, sr / 2);
            a.reset();
            check(same(a, b), "chorus reset matches fresh", sr, m);

            a.prepare(sr, 256); a.prepare(sr, 256);
            b.prepare(sr, 256);
            a.setMode(mode); b.setMode(mode);
            check(same(a, b), "chorus repeated prepare", sr, m);

            for (int from = 0; from <= 3; ++from)
            {
                a.setMode(CosmosChorusMode::Both);
                drive(a, sr / 2);
                a.setMode((CosmosChorusMode)from);
                bool bypass = true;
                for (int i = 0; i < sr / 2; ++i)
                {
                    float l = 0, r = 0;
                    a.process(l, r);
                    if (from == 0) bypass &= l == 0 && r == 0;
                }
                a.setMode(mode);
                float peak = 0;
                for (int i = 0; i < sr / 10; ++i)
                {
                    float l = 0, r = 0;
                    a.process(l, r);
                    peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
                }
                check(bypass && peak < 0.0001f, "chorus enable has no stale audio", sr, m);
            }
        }

        CosmosFilter a, b;
        a.prepare(sr); b.prepare(sr);
        a.setParameters(1000, 1, 20);
        for (int i = 0; i < sr; ++i) a.process(i < 100 ? .2f : 0);
        a.reset();
        a.setParameters(1000, 1, 20); b.setParameters(1000, 1, 20);
        bool equal = true;
        for (int i = 0; i < sr; ++i) equal &= a.process(0) == b.process(0);
        check(equal, "filter reset matches fresh", sr, 0);
        a.prepare(sr); a.prepare(sr); b.prepare(sr);
        a.setParameters(500, .95f, 20); b.setParameters(500, .95f, 20);
        equal = true;
        for (int i = 0; i < sr; ++i) equal &= a.process(.1f) == b.process(.1f);
        check(equal, "filter repeated prepare", sr, 0);
    }
    std::printf("cosmos_state_test: %d failures\n", failures);
    return failures != 0;
}

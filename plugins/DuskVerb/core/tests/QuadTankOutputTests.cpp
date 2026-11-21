// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#include "QuadTank.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr int maxBlock = 257;

bool compare (QuadTank& single, QuadTank& batched, double rate)
{
    for (auto* tank : { &single, &batched })
    {
        tank->prepare (rate, maxBlock);
        tank->setFreeze (false);
        tank->setSizeRange (0.45f, 1.5f);
        tank->setSize (0.5773f);
        tank->setModDepth (0.44f);
        tank->setStereoInput (0.51f);
        tank->setStereoMod (0.63f, 0.28f);
        tank->clearBuffers();
        tank->process (nullptr, nullptr, nullptr, nullptr, 0);
    }

    std::array<float, maxBlock> left, right, side, expectedL, expectedR;
    constexpr std::array<int, 8> sizes { 257, 1, 7, 63, 127, 128, 129, 31 };
    std::uint32_t seed = 4321;
    auto noise = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float> (seed >> 8) / 16777216.0f - 0.5f;
    };
    float peak = 0.0f;
    for (int block = 0; block < 480; ++block)
    {
        for (auto* tank : { &single, &batched })
        {
            if (block == 125) tank->setSize (1.0f);
            if (block == 187) tank->setFreeze (true);
            if (block == 190) tank->setFreeze (false);
            if (block == 210) tank->setSizeRange (0.001f, 0.002f); // Taps close to the write head.
            if (block == 251)
            {
                tank->setSizeRange (0.13f, 0.8f);
                tank->setModulationTopology (DspUtils::ModulationTopology::CoherentLoop);
                tank->setStereoMod (0.0f, 0.0f);
                tank->clearBuffers();
            }
        }
        const int count = sizes[block % sizes.size()];
        const bool hasSide = block % 2 == 0;
        const bool mono = block % 3 == 0;
        for (int i = 0; i < count; ++i)
        {
            left[i] = noise();
            right[i] = mono ? left[i] : noise();
            side[i] = noise();
            single.process (&left[i], &right[i], &expectedL[i], &expectedR[i], 1,
                            hasSide ? &side[i] : nullptr);
        }
        const bool sharedOutput = block % 11 == 0;
        batched.process (left.data(), right.data(), left.data(), sharedOutput ? left.data() : right.data(),
                         count, hasSide ? side.data() : nullptr);
        for (int i = 0; i < count; ++i)
        {
            if (std::memcmp (&left[i], sharedOutput ? &expectedR[i] : &expectedL[i], sizeof (float))
                || (!sharedOutput && std::memcmp (&right[i], &expectedR[i], sizeof (float))))
            {
                std::fprintf (stderr, "%.0f Hz, block %d, sample %d: batched output differs\n", rate, block, i);
                return false;
            }
            peak = std::max (peak, std::abs (left[i]));
        }
    }
    return peak > 1.0e-3f;
}
}

int main()
{
    QuadTank single, batched;
    // Reuse the instances through rate changes and repeated prepare at the same rate.
    for (double rate : { 8000.0, 44100.0, 48000.0, 48000.0, 96000.0, 192000.0 })
        if (!compare (single, batched, rate)) return 1;
    std::puts ("QuadTank batched output matches single samples at all five sample rates.");
    return 0;
}

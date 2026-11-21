// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#include "../../src/dsp/VelvetTail.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
bool compareBlocks (VelvetTail& single, VelvetTail& batched, VelvetTail& sharedOutput, int rate)
{
    single.prepare (rate, 257);
    batched.prepare (rate, 257);
    sharedOutput.prepare (rate, 257);
    const int frames = rate * 2 + 127; // wrap the delay ring with signal still playing
    std::vector<float> left (frames), right (frames), expectedL (frames), expectedR (frames), shared (frames);
    std::uint32_t state = 321;
    const auto noise = [&state] {
        state = 1664525u * state + 1013904223u;
        return static_cast<float> (state >> 8) / 16777216.0f - 0.5f;
    };
    for (int i = 0; i < frames; ++i)
    {
        left[i] = noise();
        right[i] = noise();
    }

    constexpr std::array<int, 7> sizes { 1, 13, 63, 64, 65, 128, 257 };
    int block = 0;
    for (int position = 0; position < frames; ++block)
    {
        if (block == 31) // rebuild taps while both rings contain audio
        {
            for (VelvetTail* tail : { &single, &batched, &sharedOutput })
            {
                tail->setGlobalDecayScale (3.7f);
                tail->setSizeScale (1.6f);
            }
        }
        const int count = std::min (sizes[block % sizes.size()], frames - position);
        for (int i = position; i < position + count; ++i)
            single.process (&left[i], &right[i], &expectedL[i], &expectedR[i], 1);
        sharedOutput.process (&left[position], &right[position], &shared[position], &shared[position], count);
        batched.process (&left[position], &right[position], &left[position], &right[position], count);
        position += count;
    }
    const size_t bytes = left.size() * sizeof (float);
    if (std::memcmp (left.data(), expectedL.data(), bytes) != 0
        || std::memcmp (right.data(), expectedR.data(), bytes) != 0
        || std::memcmp (expectedL.data(), expectedR.data(), bytes) == 0)
    {
        std::printf ("FAIL: single-sample and in-place block renders at %d Hz\n", rate);
        return false;
    }

    if (std::memcmp (shared.data(), expectedR.data(), bytes) != 0)
    {
        std::printf ("FAIL: shared output buffer differs from the right channel at %d Hz\n", rate);
        return false;
    }

    batched.clear();
    std::fill (left.begin(), left.end(), 0.0f);
    std::fill (right.begin(), right.end(), 0.0f);
    batched.process (left.data(), right.data(), left.data(), right.data(), frames);
    const auto nonzero = [] (float value) { return value != 0.0f; };
    if (std::any_of (left.begin(), left.end(), nonzero) || std::any_of (right.begin(), right.end(), nonzero))
    {
        std::printf ("FAIL: cleared velvet tail emitted audio at %d Hz\n", rate);
        return false;
    }
    return true;
}
}

int main()
{
    VelvetTail single, batched, sharedOutput;
    if (! compareBlocks (single, batched, sharedOutput, 8000)
        || ! compareBlocks (single, batched, sharedOutput, 48000))
        return 1;
    std::puts ("VelvetTail: block sizes, in-place processing, tap rebuilds and reset passed");
    return 0;
}

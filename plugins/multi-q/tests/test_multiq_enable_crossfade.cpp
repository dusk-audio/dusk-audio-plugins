// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: band enable and band solo crossfade in 3 ms, as in the JUCE Multi-Q
// (MultiQ.cpp prepareToPlay: bandEnableSmoothed[i].reset(sampleRate, 0.003)).
//
// A +12 dB band shapes a sine; at a block boundary that band is switched off,
// once by its Enabled parameter and once by soloing another band whose gain is
// 0 dB. Every remaining band is then an exact identity, so once the switched
// band has faded out the output must equal the input bit for bit, exactly at
// the end of the 3 ms ramp.
#include "../core/MultiQDSP.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

// Returns how many samples after the switch the output first equals the input
// for good (to the end of the render), or -1 if it never does.
long fadeLength(double sr, bool bySolo)
{
    constexpr int block = 512;
    auto dsp = std::make_unique<duskaudio::MultiQDSP>();
    duskaudio::MultiQDSP::Params p;
    p.bandEnabled[0] = p.bandEnabled[7] = false;        // HPF/LPF off
    p.bandFreq[2] = 1000.0f; p.bandGain[2] = 12.0f;     // the band that fades out
    p.bandShape[2] = 0;                                 // peaking
    dsp->prepare(sr, block);

    const int switchBlock = (int)std::ceil(0.25 * sr / block);   // after start-up
    const int blocks = switchBlock + (int)std::ceil(0.05 * sr / block);
    std::vector<float> inL(block), inR(block), outL(block), outR(block);
    long firstExact = -1, n = 0;
    const long switchAt = (long)switchBlock * block;
    for (int b = 0; b < blocks; ++b)
    {
        if (b == switchBlock)
        {
            if (bySolo) p.soloBand = 4;                 // band 5, gain 0 dB
            else        p.bandEnabled[2] = false;
        }
        for (int i = 0; i < block; ++i, ++n)
            inL[(size_t)i] = inR[(size_t)i] = 0.1f * (float)std::sin(2.0 * 3.141592653589793 * 1000.0 * n / sr);
        const float* in[] = {inL.data(), inR.data()};
        float* out[] = {outL.data(), outR.data()};
        dsp->process(in, out, 2, block, p);
        for (int i = 0; i < block; ++i)
        {
            const long t = (long)b * block + i;
            if (t < switchAt) continue;
            const bool exact = std::memcmp(&outL[(size_t)i], &inL[(size_t)i], sizeof(float)) == 0
                            && std::memcmp(&outR[(size_t)i], &inR[(size_t)i], sizeof(float)) == 0;
            if (!exact) firstExact = -1;
            else if (firstExact < 0) firstExact = t - switchAt;
        }
    }
    return firstExact;
}
}

int main()
{
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (bool bySolo : {false, true})
        {
            // The smoother takes floor(0.003 * sr) steps (as JUCE's SmoothedValue)
            // and the first of them lands on the switch sample, so the output is
            // exact from that many samples minus one: 131 / 143 / 287.
            const long expected = (long)std::floor(0.003 * sr) - 1;
            const long fade = fadeLength(sr, bySolo);
            std::printf("Enable crossfade: sr=%.0f %s fade=%ld samples (expected %ld)\n",
                        sr, bySolo ? "solo  " : "enable", fade, expected);
            check(fade >= 0, "the switched-off band stops contributing");
            check(fade == expected, "the band fades out over exactly the 3 ms ramp");
        }
    std::printf("Multi-Q enable crossfade: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

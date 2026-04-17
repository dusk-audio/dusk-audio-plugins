// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared deterministic synthetic-DI generator for DuskAmp test harnesses.
// Used by tests/duskamp_golden_render/ and tests/duskamp_cpu_harness/ so both
// drive the plugin with byte-for-byte the same input signal. Sections:
//   0 –10 s : log chirp 80 Hz → 8 kHz
//  10 –12 s : silence
//  12 –13 s : 100 ms-spaced impulses
//  13 –20 s : re-triggered decaying 440 Hz sine
//  20 –25 s : pseudo-pink noise from a fixed-seed LFSR
//  25 –30 s : silence

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdint>

namespace DuskAmpTest
{
    inline juce::AudioBuffer<float> makeSyntheticDI (double sampleRate = 48000.0,
                                                     int durationSeconds = 30)
    {
        constexpr double pi = 3.14159265358979323846;
        const int totalSamples = static_cast<int> (sampleRate * durationSeconds);

        juce::AudioBuffer<float> buf (1, totalSamples);
        auto* d = buf.getWritePointer (0);

        int i = 0;

        // 0–10 s: log chirp
        const int chirpEnd = static_cast<int> (10.0 * sampleRate);
        const double f0 = 80.0, f1 = 8000.0, T = 10.0;
        double phase = 0.0;
        for (; i < chirpEnd; ++i)
        {
            const double t = i / sampleRate;
            const double freq = f0 * std::pow (f1 / f0, t / T);
            phase += 2.0 * pi * freq / sampleRate;
            d[i] = 0.3f * static_cast<float> (std::sin (phase));
        }

        // 10–12 s: silence
        const int silenceEnd = static_cast<int> (12.0 * sampleRate);
        for (; i < silenceEnd; ++i)
            d[i] = 0.0f;

        // 12–13 s: impulses every 100 ms
        const int pulseEnd = static_cast<int> (13.0 * sampleRate);
        const int pulsePeriod = static_cast<int> (0.1 * sampleRate);
        for (; i < pulseEnd; ++i)
            d[i] = ((i - silenceEnd) % pulsePeriod == 0) ? 0.7f : 0.0f;

        // 13–20 s: re-triggered decaying sine
        const int sineStart = pulseEnd;
        const int sineEnd = static_cast<int> (20.0 * sampleRate);
        for (; i < sineEnd; ++i)
        {
            const double t = (i - sineStart) / sampleRate;
            const double tMod = std::fmod (t, 1.75);
            const double env = std::exp (-tMod / 1.5);
            d[i] = 0.4f * static_cast<float> (env * std::sin (2.0 * pi * 440.0 * t));
        }

        // 20–25 s: pseudo-pink noise (Voss-McCartney 3-pole approximation)
        const int noiseEnd = static_cast<int> (25.0 * sampleRate);
        std::uint32_t lfsr = 0xACE1u;
        float pinkState[3] = { 0.0f, 0.0f, 0.0f };
        for (; i < noiseEnd; ++i)
        {
            const unsigned bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
            lfsr = (lfsr >> 1) | (bit << 15);
            const float white = static_cast<float> (static_cast<int> (lfsr & 0xFFFFu) - 0x8000) / 32768.0f;
            pinkState[0] = 0.99765f * pinkState[0] + white * 0.0990460f;
            pinkState[1] = 0.96300f * pinkState[1] + white * 0.2965164f;
            pinkState[2] = 0.57000f * pinkState[2] + white * 1.0526913f;
            const float pink = pinkState[0] + pinkState[1] + pinkState[2] + white * 0.1848f;
            d[i] = 0.15f * pink;
        }

        // 25 s → end: silence
        for (; i < totalSamples; ++i)
            d[i] = 0.0f;

        return buf;
    }
}

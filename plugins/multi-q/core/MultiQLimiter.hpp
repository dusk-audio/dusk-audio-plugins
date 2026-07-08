// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQLimiter.hpp — framework-free (ZERO JUCE) port of OutputLimiter.h.
//
// Transparent brickwall mastering-safety limiter with ~1 ms lookahead, instant
// attack, program-dependent (~100 ms) release, and stereo-linked gain reduction.
// Verbatim transcription of plugins/multi-q/OutputLimiter.h with the JUCE facade
// removed:
//   juce::Decibels::decibelsToGain(dB)  → std::pow(10.f, dB/20.f)
//   juce::Decibels::gainToDecibels(g)   → 20.f*std::log10(g)
//   std::atomic guards (cross-thread)   → plain members (config is pushed once
//                                         per block from the audio thread by the
//                                         DSP core snapshot); currentGR stays an
//                                         atomic so the UI can read it any thread.
//
// The delay buffers are sized once in prepare() (message thread). process() does
// no allocation or locking, so it is real-time safe. When disabled the process()
// call is a no-op and adds no latency (matching the JUCE build, so an OFF limiter
// leaves the signal — and the validated Digital A/B — bit-identical).

#pragma once

#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>

namespace duskaudio
{

class MultiQLimiter
{
public:
    MultiQLimiter() = default;

    // Called from prepare() — guaranteed not to overlap with process().
    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;

        // Lookahead: ~1 ms (44-48 samples at 44.1-48 kHz).
        int la = std::max(1, static_cast<int>(sr * 0.001));
        lookaheadSamples = la;
        holdSamples = la;

        delayL.assign(static_cast<size_t>(la), 0.0f);
        delayR.assign(static_cast<size_t>(la), 0.0f);
        delayPos = 0;

        gainReduction = 1.0f;
        holdCounter = 0;

        // Attack: instant (1 sample). Release: ~100 ms program-dependent.
        releaseCoeff = static_cast<float>(std::exp(-1.0 / (0.1 * sr)));

        reset();
    }

    void setCeiling(float ceilingDB)
    {
        float linear = std::pow(10.0f, ceilingDB / 20.0f);
        ceiling = std::max(linear, 1e-6f);
    }

    void setEnabled(bool enable)
    {
        if (enable != enabled)
        {
            enabled = enable;
            // Reset delay line on re-enable to avoid outputting stale samples.
            if (enable)
                needsReset = true;
        }
    }
    bool isEnabled() const { return enabled; }

    void requestReset() { needsReset = true; }

    // Process a stereo buffer in-place. right may equal left for mono.
    void process(float* left, float* right, int numSamples)
    {
        if (!enabled || numSamples == 0)
        {
            currentGR.store(0.0f, std::memory_order_relaxed);
            return;
        }

        int la = lookaheadSamples;
        if (la <= 0 || delayL.size() < static_cast<size_t>(la)
                     || delayR.size() < static_cast<size_t>(la))
        {
            currentGR.store(0.0f, std::memory_order_relaxed);
            return;
        }

        if (needsReset)
        {
            needsReset = false;
            reset();
        }

        if (delayPos >= la)
            delayPos = 0;

        const float ceilVal = ceiling;
        float maxGR = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // Read from delay line (lookahead), then write current sample.
            float delayedL = delayL[static_cast<size_t>(delayPos)];
            float delayedR = delayR[static_cast<size_t>(delayPos)];

            delayL[static_cast<size_t>(delayPos)] = left[i];
            delayR[static_cast<size_t>(delayPos)] = right[i];
            delayPos = (delayPos + 1) % la;

            // Peak detection on incoming (future) samples — stereo linked.
            float peak = std::max(std::abs(left[i]), std::abs(right[i]));

            float targetGR = 1.0f;
            if (peak > ceilVal)
                targetGR = ceilVal / peak;

            // Envelope: instant attack, hold, smooth release.
            if (targetGR < gainReduction)
            {
                gainReduction = targetGR;      // Instant attack
                holdCounter = holdSamples + 1; // Reset hold (covers lookahead window)
            }
            else if (holdCounter > 0)
            {
                --holdCounter;                 // Hold at current GR level
            }
            else
            {
                gainReduction = gainReduction + (1.0f - gainReduction) * (1.0f - releaseCoeff);
            }

            left[i]  = delayedL * gainReduction;
            right[i] = delayedR * gainReduction;

            float grDB = (gainReduction < 0.999f) ? -(20.0f * std::log10(gainReduction)) : 0.0f;
            maxGR = std::max(maxGR, grDB);
        }

        currentGR.store(maxGR, std::memory_order_relaxed);
    }

    // Current gain reduction in dB (positive = amount of limiting). Thread-safe.
    float getGainReduction() const { return currentGR.load(std::memory_order_relaxed); }

    // Lookahead in samples (for latency reporting). 0 when disabled.
    int getLatencySamples() const { return enabled ? lookaheadSamples : 0; }

    // Maximum possible lookahead regardless of enable state (buffer sizing).
    int getMaxLookaheadSamples() const { return lookaheadSamples; }

private:
    void reset()
    {
        std::fill(delayL.begin(), delayL.end(), 0.0f);
        std::fill(delayR.begin(), delayR.end(), 0.0f);
        delayPos = 0;
        gainReduction = 1.0f;
        holdCounter = 0;
        currentGR.store(0.0f, std::memory_order_relaxed);
    }

    double sr = 44100.0;
    float ceiling = 1.0f;   // Linear ceiling (default 0 dBFS)
    bool  enabled = false;
    bool  needsReset = false;

    std::vector<float> delayL, delayR;
    int delayPos = 0;
    int lookaheadSamples = 44;

    float gainReduction = 1.0f;
    int   holdCounter = 0;
    int   holdSamples = 44;
    float releaseCoeff = 0.99f;

    std::atomic<float> currentGR{0.0f};   // UI readout (any thread)
};

} // namespace duskaudio

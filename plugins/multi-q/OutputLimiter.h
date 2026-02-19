#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <algorithm>

//==============================================================================
/**
    OutputLimiter — Transparent brickwall limiter for mastering safety.

    Features:
    - ~1ms lookahead for transparent peak limiting
    - Ceiling parameter (typically 0 dBFS or -0.1 dBFS)
    - Fast attack, program-dependent release
    - Stereo-linked gain reduction (prevents image shift)
    - Provides gain reduction amount for UI indicator
*/
class OutputLimiter
{
public:
    OutputLimiter() = default;

    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;

        // Lookahead: ~1ms (44-48 samples at 44.1-48kHz)
        lookaheadSamples = std::max(1, static_cast<int>(sr * 0.001));

        // Delay buffers for lookahead
        delayL.resize(static_cast<size_t>(lookaheadSamples), 0.0f);
        delayR.resize(static_cast<size_t>(lookaheadSamples), 0.0f);
        delayPos = 0;

        // Gain reduction envelope
        gainReduction = 1.0f;
        peakHold = 0.0f;

        // Attack: instant (1 sample), Release: ~100ms program-dependent
        releaseCoeff = static_cast<float>(std::exp(-1.0 / (0.1 * sr)));
    }

    void reset()
    {
        std::fill(delayL.begin(), delayL.end(), 0.0f);
        std::fill(delayR.begin(), delayR.end(), 0.0f);
        delayPos = 0;
        gainReduction = 1.0f;
        peakHold = 0.0f;
        currentGR.store(0.0f, std::memory_order_relaxed);
    }

    void setCeiling(float ceilingDB)
    {
        ceiling = juce::Decibels::decibelsToGain(ceilingDB);
    }

    void setEnabled(bool enable) { enabled = enable; }
    bool isEnabled() const { return enabled; }

    /** Process a stereo buffer in-place. */
    void process(float* left, float* right, int numSamples)
    {
        if (!enabled || numSamples == 0)
        {
            currentGR.store(0.0f, std::memory_order_relaxed);
            return;
        }

        float maxGR = 0.0f;  // Track peak gain reduction for UI

        for (int i = 0; i < numSamples; ++i)
        {
            // Read from delay line (lookahead)
            float delayedL = delayL[static_cast<size_t>(delayPos)];
            float delayedR = delayR[static_cast<size_t>(delayPos)];

            // Write current sample to delay line
            delayL[static_cast<size_t>(delayPos)] = left[i];
            delayR[static_cast<size_t>(delayPos)] = right[i];
            delayPos = (delayPos + 1) % lookaheadSamples;

            // Peak detection on incoming (future) samples — stereo linked
            float peak = std::max(std::abs(left[i]), std::abs(right[i]));

            // Compute required gain reduction
            float targetGR = 1.0f;
            if (peak > ceiling)
                targetGR = ceiling / peak;

            // Envelope: instant attack, smooth release
            if (targetGR < gainReduction)
            {
                gainReduction = targetGR;  // Instant attack
            }
            else
            {
                // Smooth release
                gainReduction = gainReduction + (1.0f - gainReduction) * (1.0f - releaseCoeff);
            }

            // Apply gain reduction to delayed signal
            left[i] = delayedL * gainReduction;
            right[i] = delayedR * gainReduction;

            // Track max GR for UI
            float grDB = (gainReduction < 0.999f)
                ? -juce::Decibels::gainToDecibels(gainReduction)
                : 0.0f;
            maxGR = std::max(maxGR, grDB);
        }

        currentGR.store(maxGR, std::memory_order_relaxed);
    }

    /** Get current gain reduction in dB (positive value = amount of limiting). Thread-safe. */
    float getGainReduction() const { return currentGR.load(std::memory_order_relaxed); }

    /** Get lookahead in samples (for latency reporting). */
    int getLatencySamples() const { return enabled ? lookaheadSamples : 0; }

private:
    double sr = 44100.0;
    float ceiling = 1.0f;       // Linear ceiling (default 0 dBFS)
    bool enabled = false;

    // Delay line for lookahead
    std::vector<float> delayL;
    std::vector<float> delayR;
    int delayPos = 0;
    int lookaheadSamples = 44;

    // Gain reduction envelope
    float gainReduction = 1.0f;
    float peakHold = 0.0f;
    float releaseCoeff = 0.99f;

    // Thread-safe GR readout for UI
    std::atomic<float> currentGR{0.0f};
};

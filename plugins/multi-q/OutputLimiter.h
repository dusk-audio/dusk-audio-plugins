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

    /** Called from prepareToPlay() — guaranteed by JUCE not to overlap with process(). */
    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;

        // Lookahead: ~1ms (44-48 samples at 44.1-48kHz)
        int la = std::max(1, static_cast<int>(sr * 0.001));
        lookaheadSamples.store(la, std::memory_order_relaxed);
        holdSamples = la;

        // Delay buffers for lookahead
        delayL.resize(static_cast<size_t>(la), 0.0f);
        delayR.resize(static_cast<size_t>(la), 0.0f);
        delayPos = 0;

        // Gain reduction envelope
        gainReduction = 1.0f;
        holdCounter = 0;

        // Attack: instant (1 sample), Release: ~100ms program-dependent
        releaseCoeff = static_cast<float>(std::exp(-1.0 / (0.1 * sr)));

        reset();
    }

    void setCeiling(float ceilingDB)
    {
        float linear = juce::Decibels::decibelsToGain(ceilingDB);
        ceiling.store(std::max(linear, 1e-6f), std::memory_order_release);
    }

    void setEnabled(bool enable)
    {
        bool expected = !enable;
        if (enabled.compare_exchange_strong(expected, enable, std::memory_order_acq_rel))
        {
            // Reset delay line on re-enable to avoid outputting stale samples
            if (enable)
                needsReset.store(true, std::memory_order_release);
        }
    }
    bool isEnabled() const { return enabled.load(std::memory_order_acquire); }

    /** Request a reset on the next process() call (thread-safe). */
    void requestReset() { needsReset.store(true, std::memory_order_release); }

    /** Process a stereo buffer in-place. */
    void process(float* left, float* right, int numSamples)
    {
        if (!enabled.load(std::memory_order_acquire) || numSamples == 0)
        {
            currentGR.store(0.0f, std::memory_order_relaxed);
            return;
        }

        int la = lookaheadSamples.load(std::memory_order_relaxed);
        if (la <= 0 || delayL.size() < static_cast<size_t>(la)
                     || delayR.size() < static_cast<size_t>(la))
        {
            currentGR.store(0.0f, std::memory_order_relaxed);
            return;
        }

        if (needsReset.exchange(false, std::memory_order_acquire))
            reset();

        // Ensure delayPos is within bounds (defensive — could be stale after resize)
        if (delayPos >= la)
            delayPos = 0;

        float ceilVal = ceiling.load(std::memory_order_acquire);
        float maxGR = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // Read from delay line (lookahead)
            float delayedL = delayL[static_cast<size_t>(delayPos)];
            float delayedR = delayR[static_cast<size_t>(delayPos)];

            // Write current sample to delay line
            delayL[static_cast<size_t>(delayPos)] = left[i];
            delayR[static_cast<size_t>(delayPos)] = right[i];
            delayPos = (delayPos + 1) % la;

            // Peak detection on incoming (future) samples — stereo linked
            float peak = std::max(std::abs(left[i]), std::abs(right[i]));

            // Compute required gain reduction
            float targetGR = 1.0f;
            if (peak > ceilVal)
                targetGR = ceilVal / peak;

            // Envelope: instant attack, hold, smooth release
            if (targetGR < gainReduction)
            {
                gainReduction = targetGR;      // Instant attack
                holdCounter = holdSamples + 1; // Reset hold timer (covers full lookahead window)
            }
            else if (holdCounter > 0)
            {
                --holdCounter;  // Hold at current GR level
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
    int getLatencySamples() const { return enabled.load(std::memory_order_acquire) ? lookaheadSamples.load(std::memory_order_relaxed) : 0; }

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
    std::atomic<float> ceiling{1.0f};  // Linear ceiling (default 0 dBFS)
    std::atomic<bool> enabled{false};
    std::atomic<bool> needsReset{false};

    // Delay line for lookahead
    std::vector<float> delayL;
    std::vector<float> delayR;
    int delayPos = 0;
    std::atomic<int> lookaheadSamples{44};

    // Gain reduction envelope
    float gainReduction = 1.0f;
    int holdCounter = 0;
    int holdSamples = 44;  // Hold for ~1ms (same as lookahead)
    float releaseCoeff = 0.99f;

    // Thread-safe GR readout for UI
    std::atomic<float> currentGR{0.0f};
};

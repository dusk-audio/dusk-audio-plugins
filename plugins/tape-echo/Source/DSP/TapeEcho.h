/*
  ==============================================================================

    TapeEcho.h
    Tape Echo - RE-201 Style Tape Delay Engine

    3 virtual playback heads with 12 mode selector:
    - Modes 1-3: Single heads
    - Modes 4-6: Head pairs
    - Modes 7-11: Triple combinations
    - Mode 12: Reverb only

    Variable tape speed, wow/flutter, and feedback saturation.

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include "TapeSaturation.h"
#include "WowFlutter.h"
#include <array>
#include <atomic>
#include <vector>

namespace TapeEchoDSP
{

// Simple thread-safe ring buffer with Hermite cubic interpolation
class SimpleDelayLine
{
public:
    SimpleDelayLine() = default;

    void prepare(int maxSamples)
    {
        bufferSize = maxSamples + 4;  // Extra margin for interpolation
        buffer.resize(static_cast<size_t>(bufferSize), 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    void pushSample(float sample)
    {
        if (buffer.empty()) return;
        buffer[static_cast<size_t>(writeIndex)] = sample;
        writeIndex = (writeIndex + 1) % bufferSize;
    }

    float popSample(float delaySamples) const
    {
        if (buffer.empty() || bufferSize <= 0) return 0.0f;

        // Clamp delay to valid range (need 2 samples on each side for cubic)
        delaySamples = juce::jlimit(2.0f, static_cast<float>(bufferSize - 3), delaySamples);

        int d = static_cast<int>(delaySamples);
        float f = delaySamples - static_cast<float>(d);

        // 4 samples for Hermite cubic interpolation
        float y0 = buffer[static_cast<size_t>((writeIndex - d - 1 + bufferSize) % bufferSize)];
        float y1 = buffer[static_cast<size_t>((writeIndex - d + bufferSize) % bufferSize)];
        float y2 = buffer[static_cast<size_t>((writeIndex - d + 1 + bufferSize) % bufferSize)];
        float y3 = buffer[static_cast<size_t>((writeIndex - d + 2 + bufferSize) % bufferSize)];

        // Hermite cubic interpolation - smoother than linear for wow/flutter
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * f + c2) * f + c1) * f + c0;
    }

private:
    std::vector<float> buffer;
    int bufferSize = 0;
    int writeIndex = 0;
};

class TapeEchoEngine
{
public:
    // Base delay times at standard speed (in ms)
    static constexpr float HEAD_1_BASE_MS = 50.0f;
    static constexpr float HEAD_2_BASE_MS = 100.0f;
    static constexpr float HEAD_3_BASE_MS = 150.0f;

    // Maximum delay time (accounts for tempo sync and wow/flutter modulation)
    // At 60 BPM, whole note = 4000ms, so we need more buffer
    static constexpr float MAX_DELAY_MS = 5000.0f;

    TapeEchoEngine() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        prepared.store(false, std::memory_order_release);  // Mark as not ready during preparation

        // Store sample rate atomically
        currentSampleRate.store(sampleRate, std::memory_order_release);
        maxSamplesPerBlock = maxBlockSize;

        // Calculate max delay in samples
        const int maxDelaySamples = static_cast<int>(MAX_DELAY_MS * sampleRate / 1000.0) + 512;

        // Initialize delay lines for each head (stereo)
        for (int head = 0; head < 3; ++head)
        {
            delayLineL[head].prepare(maxDelaySamples);
            delayLineR[head].prepare(maxDelaySamples);
        }

        // Store the configured max delay
        maxDelaySamplesConfigured.store(maxDelaySamples, std::memory_order_release);

        // Prepare sub-processors
        tapeSaturation.prepare(sampleRate, maxBlockSize);
        wowFlutter.prepare(sampleRate, maxBlockSize);

        // Smoothed parameters
        speedSmoothed.reset(sampleRate, 0.05);
        feedbackSmoothed.reset(sampleRate, 0.02);

        // Initialize feedback state
        feedbackL = 0.0f;
        feedbackR = 0.0f;

        updateDelayTimes();

        prepared.store(true, std::memory_order_release);  // Now ready for processing
    }

    bool isPrepared() const { return prepared.load(std::memory_order_acquire); }

    void reset()
    {
        for (int head = 0; head < 3; ++head)
        {
            delayLineL[head].reset();
            delayLineR[head].reset();
        }
        tapeSaturation.reset();
        wowFlutter.reset();
        feedbackL = 0.0f;
        feedbackR = 0.0f;
    }

    // Mode selector (1-12) with smooth transition
    void setMode(int mode)
    {
        int newMode = juce::jlimit(1, 12, mode);
        if (newMode != currentMode)
        {
            previousMode = currentMode;
            currentMode = newMode;
            modeTransitionSamples = static_cast<int>(currentSampleRate.load(std::memory_order_relaxed) * 0.01);  // 10ms crossfade
            modeTransitionCounter = modeTransitionSamples;
            updateHeadConfig();
        }
    }

    int getMode() const { return currentMode; }

    // Tape speed multiplier (0.5 to 2.0)
    void setSpeed(float speed)
    {
        speedSmoothed.setTargetValue(juce::jlimit(0.5f, 2.0f, speed));
    }

    // Feedback/Intensity (0.0 to 1.1, >1.0 = self-oscillation)
    void setFeedback(float feedback)
    {
        feedbackSmoothed.setTargetValue(juce::jlimit(0.0f, 1.1f, feedback));
    }

    // Wow/flutter amount
    void setWowFlutterAmount(float amount)
    {
        wowFlutter.setAmount(amount);
    }

    // Saturation drive
    void setSaturationDrive(float drive)
    {
        tapeSaturation.setDrive(drive);
    }

    // Tempo sync mode
    void setTempoSync(bool enabled)
    {
        tempoSyncEnabled.store(enabled, std::memory_order_relaxed);
    }

    // Set sync delay time in ms (used when tempo sync is enabled)
    // Head 1 uses this delay, heads 2 and 3 use 2x and 3x multiples
    void setSyncDelayTimeMs(float delayMs)
    {
        syncDelayTimeMs.store(juce::jlimit(10.0f, MAX_DELAY_MS / 3.0f, delayMs), std::memory_order_relaxed);
    }

    // Process stereo audio (returns wet signal only)
    void process(juce::AudioBuffer<float>& buffer)
    {
        // Safety check - don't process if not prepared
        if (!prepared.load(std::memory_order_acquire))
        {
            buffer.clear();
            return;
        }

        // Get cached values for thread safety
        const double sampleRate = currentSampleRate.load(std::memory_order_acquire);
        const int maxDelay = maxDelaySamplesConfigured.load(std::memory_order_acquire);

        // Additional safety check
        if (sampleRate <= 0.0 || maxDelay <= 0)
        {
            buffer.clear();
            return;
        }

        const int numSamples = buffer.getNumSamples();
        float* leftChannel = buffer.getWritePointer(0);
        float* rightChannel = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

        // If mode 12 (reverb only), output silence from echo section
        if (currentMode == 12)
        {
            buffer.clear();
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float speed = speedSmoothed.getNextValue();
            const float feedback = feedbackSmoothed.getNextValue();

            // Get wow/flutter modulation
            const float wfMod = wowFlutter.getNextDelayMultiplier();

            // Input samples
            const float inputL = leftChannel[i];
            const float inputR = rightChannel ? rightChannel[i] : inputL;

            // Calculate modulated delay times for each head
            float head1DelayMs, head2DelayMs, head3DelayMs;

            if (tempoSyncEnabled.load(std::memory_order_relaxed))
            {
                // Tempo sync mode: use sync delay time
                const float baseDelay = syncDelayTimeMs.load(std::memory_order_relaxed);
                head1DelayMs = baseDelay * wfMod;
                head2DelayMs = baseDelay * 2.0f * wfMod;
                head3DelayMs = baseDelay * 3.0f * wfMod;
            }
            else
            {
                // Speed mode: use base delays divided by speed
                head1DelayMs = HEAD_1_BASE_MS / speed * wfMod;
                head2DelayMs = HEAD_2_BASE_MS / speed * wfMod;
                head3DelayMs = HEAD_3_BASE_MS / speed * wfMod;
            }

            float head1DelaySamples = head1DelayMs * static_cast<float>(sampleRate) / 1000.0f;
            float head2DelaySamples = head2DelayMs * static_cast<float>(sampleRate) / 1000.0f;
            float head3DelaySamples = head3DelayMs * static_cast<float>(sampleRate) / 1000.0f;

            // Clamp delay times to valid range to prevent out-of-bounds access
            const float maxDelayFloat = static_cast<float>(maxDelay - 1);
            head1DelaySamples = juce::jlimit(1.0f, maxDelayFloat, head1DelaySamples);
            head2DelaySamples = juce::jlimit(1.0f, maxDelayFloat, head2DelaySamples);
            head3DelaySamples = juce::jlimit(1.0f, maxDelayFloat, head3DelaySamples);

            // Read from each head (with linear interpolation)
            float head1L = delayLineL[0].popSample(head1DelaySamples);
            float head1R = delayLineR[0].popSample(head1DelaySamples);
            float head2L = delayLineL[1].popSample(head2DelaySamples);
            float head2R = delayLineR[1].popSample(head2DelaySamples);
            float head3L = delayLineL[2].popSample(head3DelaySamples);
            float head3R = delayLineR[2].popSample(head3DelaySamples);

            // Mix heads according to mode configuration
            float outputL = 0.0f;
            float outputR = 0.0f;

            // Load head enabled states (thread-safe)
            const bool h1 = headEnabled[0].load(std::memory_order_relaxed);
            const bool h2 = headEnabled[1].load(std::memory_order_relaxed);
            const bool h3 = headEnabled[2].load(std::memory_order_relaxed);

            if (h1)
            {
                outputL += head1L;
                outputR += head1R;
            }
            if (h2)
            {
                outputL += head2L;
                outputR += head2R;
            }
            if (h3)
            {
                outputL += head3L;
                outputR += head3R;
            }

            // Normalize by number of active heads
            const float headCount = static_cast<float>(h1 + h2 + h3);
            if (headCount > 1.0f)
            {
                const float normFactor = 1.0f / std::sqrt(headCount);
                outputL *= normFactor;
                outputR *= normFactor;
            }

            // Get feedback signal based on mode routing
            float fbSourceL, fbSourceR;
            getFeedbackSignal(head1L, head1R, head2L, head2R, head3L, head3R, fbSourceL, fbSourceR);

            // Apply feedback amount
            float fbL = fbSourceL * feedback;
            float fbR = fbSourceR * feedback;

            // Soft saturation in feedback path
            fbL = tapeSaturation.processSampleMono(fbL, feedback * 0.3f);
            fbR = tapeSaturation.processSampleMono(fbR, feedback * 0.3f);

            // Write to delay lines (input + feedback)
            float toDelayL = inputL + fbL;
            float toDelayR = inputR + fbR;

            // Prevent runaway feedback
            toDelayL = juce::jlimit(-2.0f, 2.0f, toDelayL);
            toDelayR = juce::jlimit(-2.0f, 2.0f, toDelayR);

            // All heads share the same input (tape is continuous)
            for (int head = 0; head < 3; ++head)
            {
                delayLineL[head].pushSample(toDelayL);
                delayLineR[head].pushSample(toDelayR);
            }

            // Store output for next feedback cycle
            feedbackL = outputL;
            feedbackR = outputR;

            // Apply crossfade during mode transition to avoid clicks
            if (modeTransitionCounter > 0)
            {
                float fadeIn = 1.0f - static_cast<float>(modeTransitionCounter) / static_cast<float>(modeTransitionSamples);
                outputL *= fadeIn;
                outputR *= fadeIn;
                modeTransitionCounter--;
            }            // Output
            leftChannel[i] = outputL;
            if (rightChannel)
                rightChannel[i] = outputR;
        }
    }

    // Get active heads for visualization (thread-safe)
    bool isHeadActive(int headIndex) const
    {
        if (headIndex >= 0 && headIndex < 3)
            return headEnabled[static_cast<size_t>(headIndex)].load(std::memory_order_relaxed);
        return false;
    }

private:
    std::atomic<bool> prepared { false };
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> maxDelaySamplesConfigured { 20000 };  // Match initial delay line size
    int maxSamplesPerBlock = 512;

    // Simple delay lines for each head (mono, but we have L/R pairs)
    std::array<SimpleDelayLine, 3> delayLineL;
    std::array<SimpleDelayLine, 3> delayLineR;

    // Sub-processors
    TapeSaturation tapeSaturation;
    WowFlutter wowFlutter;

    // Mode and head configuration
    int currentMode = 1;
    int previousMode = 1;
    int modeTransitionSamples = 0;
    int modeTransitionCounter = 0;
    std::array<std::atomic<bool>, 3> headEnabled = { { true, false, false } };

    // Smoothed parameters
    juce::SmoothedValue<float> speedSmoothed { 1.0f };
    juce::SmoothedValue<float> feedbackSmoothed { 0.3f };

    // Tempo sync state
    std::atomic<bool> tempoSyncEnabled { false };
    std::atomic<float> syncDelayTimeMs { 250.0f };  // Default 1/8 note at 120 BPM

    // Feedback state
    float feedbackL = 0.0f;
    float feedbackR = 0.0f;

    // Feedback routing for modes 7-11
    // 0=head1, 1=head2, 2=head3, -1=mix 1+3, -2=cascade
    int feedbackSourceHead = 2;

    void updateDelayTimes()
    {
        // Base delay times are set when preparing
        // Actual times are calculated per-sample with speed and wow/flutter
    }

    void setHeadEnabled(bool h1, bool h2, bool h3)
    {
        headEnabled[0].store(h1, std::memory_order_relaxed);
        headEnabled[1].store(h2, std::memory_order_relaxed);
        headEnabled[2].store(h3, std::memory_order_relaxed);
    }

    void updateHeadConfig()
    {
        // RE-201 mode configurations:
        // Mode 1: Head 1 only
        // Mode 2: Head 2 only
        // Mode 3: Head 3 only
        // Mode 4: Heads 1+2
        // Mode 5: Heads 1+3
        // Mode 6: Heads 2+3
        // Mode 7: Heads 1+2+3, feedback from head 3 (longest delay - standard)
        // Mode 8: Heads 1+2+3, feedback from head 1 (shortest - tight echoes)
        // Mode 9: Heads 1+2+3, feedback from head 2 (medium - balanced)
        // Mode 10: Heads 1+2+3, feedback from mix of 1+3 (complex rhythm)
        // Mode 11: Heads 1+2+3, feedback cascade (dense buildup)
        // Mode 12: Reverb only (no echo)

        switch (currentMode)
        {
            case 1:
                setHeadEnabled(true, false, false);
                feedbackSourceHead = 0;
                break;
            case 2:
                setHeadEnabled(false, true, false);
                feedbackSourceHead = 1;
                break;
            case 3:
                setHeadEnabled(false, false, true);
                feedbackSourceHead = 2;
                break;
            case 4:
                setHeadEnabled(true, true, false);
                feedbackSourceHead = 1;
                break;
            case 5:
                setHeadEnabled(true, false, true);
                feedbackSourceHead = 2;
                break;
            case 6:
                setHeadEnabled(false, true, true);
                feedbackSourceHead = 2;
                break;
            case 7:
                setHeadEnabled(true, true, true);
                feedbackSourceHead = 2;   // Standard - feedback from head 3 (longest delay)
                break;
            case 8:
                setHeadEnabled(true, true, true);
                feedbackSourceHead = 0;   // Tight - feedback from head 1 (shortest delay)
                break;
            case 9:
                setHeadEnabled(true, true, true);
                feedbackSourceHead = 1;   // Balanced - feedback from head 2 (medium delay)
                break;
            case 10:
                setHeadEnabled(true, true, true);
                feedbackSourceHead = -1;  // Complex - mixed feedback from heads 1+3
                break;
            case 11:
                setHeadEnabled(true, true, true);
                feedbackSourceHead = -2;  // Dense - cascade feedback from all heads
                break;
            case 12:
                setHeadEnabled(false, false, false);  // Reverb only
                feedbackSourceHead = 2;
                break;
            default:
                setHeadEnabled(true, false, false);
                feedbackSourceHead = 0;
                break;
        }
    }

    // Get feedback signal based on mode configuration
    void getFeedbackSignal(float head1L, float head1R, float head2L, float head2R,
                           float head3L, float head3R, float& fbL, float& fbR)
    {
        switch (feedbackSourceHead)
        {
            case 0:  // Head 1 only
                fbL = head1L;
                fbR = head1R;
                break;
            case 1:  // Head 2 only
                fbL = head2L;
                fbR = head2R;
                break;
            case 2:  // Head 3 only (default)
                fbL = head3L;
                fbR = head3R;
                break;
            case -1: // Mixed 1+3 (complex rhythm)
                fbL = (head1L + head3L) * 0.5f;
                fbR = (head1R + head3R) * 0.5f;
                break;
            case -2: // Cascade (dense buildup)
                fbL = head1L * 0.5f + head2L * 0.3f + head3L * 0.2f;
                fbR = head1R * 0.5f + head2R * 0.3f + head3R * 0.2f;
                break;
            default:
                fbL = head3L;
                fbR = head3R;
                break;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoEngine)
};

} // namespace TapeEchoDSP

/*
  ==============================================================================

    DryWetMixer.h
    Phase-coherent dry/wet mixer for use with oversampled processing

    Prevents comb filtering artifacts that occur when mixing dry and wet signals
    that have different latencies due to FIR anti-aliasing filters in oversampling.

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <algorithm>

namespace LunaAudio
{

//==============================================================================
/**
    Phase-coherent dry/wet mixer for use with oversampled processing.

    When plugins use oversampling, the FIR anti-aliasing filters introduce latency
    (typically 48-63 samples for 2x, 94-126 samples for 4x). If dry and wet signals
    are mixed without compensation, this phase mismatch causes comb filtering with
    characteristic notches at regular frequency intervals.

    This class provides two mixing modes:

    1. Oversampled mixing (preferred): Capture dry AFTER upsampling, mix BEFORE
       downsampling. Both signals pass through the same anti-aliasing filter,
       eliminating phase mismatch entirely.

    2. Compensated mixing (fallback): Apply a delay to the dry signal to match
       the oversampling latency. Less ideal but still prevents comb filtering.

    Usage (oversampled mixing - recommended):
    @code
        // In processor header:
        LunaAudio::DryWetMixer dryWetMixer;

        // In prepareToPlay:
        dryWetMixer.prepare(sampleRate, samplesPerBlock, numChannels, 4);

        // In processBlock:
        auto oversampledBlock = oversampler.processSamplesUp(block);

        // Capture dry BEFORE processing
        float mixAmount = mixParam->load();
        bool canMix = dryWetMixer.captureDryAtOversampledRate(oversampledBlock);

        // ... process wet signal at oversampled rate ...

        // Mix BEFORE downsampling
        if (canMix)
            dryWetMixer.mixAtOversampledRate(oversampledBlock, mixAmount);

        oversampler.processSamplesDown(block);
    @endcode

    Usage (compensated mixing - fallback for 1x mode):
    @code
        dryWetMixer.captureDryAtNormalRate(buffer);
        // ... process ...
        dryWetMixer.mixAtNormalRate(buffer, mixAmount);
    @endcode
*/
class DryWetMixer
{
public:
    DryWetMixer() = default;
    ~DryWetMixer() = default;

    //==============================================================================
    // Configuration
    //==============================================================================

    /**
        Prepares the mixer for processing. Call from prepareToPlay().

        @param sampleRate               Base sample rate (before oversampling)
        @param maxBlockSize             Maximum expected block size
        @param numChannels              Number of audio channels (1 or 2)
        @param maxOversamplingFactor    Maximum oversampling factor (1, 2, or 4)
    */
    void prepare(double sampleRate, int maxBlockSize, int numChannels, int maxOversamplingFactor)
    {
        jassert(maxBlockSize > 0 && numChannels > 0 && numChannels <= MAX_CHANNELS);
        jassert(maxOversamplingFactor == 1 || maxOversamplingFactor == 2 || maxOversamplingFactor == 4);

        ready.store(false, std::memory_order_release);

        baseSampleRate = sampleRate;
        preparedChannels = numChannels;
        preparedBlockSize = maxBlockSize;
        preparedMaxOversamplingFactor = maxOversamplingFactor;

        // Allocate with 8x safety margin (DAWs may pass larger blocks during offline bounce)
        int safeBlockSize = maxBlockSize * 8;

        // Normal-rate dry buffer
        normalDryBuffer.setSize(numChannels, safeBlockSize, false, true, false);

        // Oversampled dry buffer: base * safety * max oversampling
        int oversampledSize = safeBlockSize * maxOversamplingFactor;
        oversampledDryBuffer.setSize(numChannels, oversampledSize, false, true, false);

        // Clear delay buffers
        for (auto& channelBuffer : delayBuffer)
            std::fill(channelBuffer.begin(), channelBuffer.end(), 0.0f);
        delayWritePos = 0;

        // Clear capture flags
        oversampledDryCaptured = false;
        normalDryCaptured = false;
        lastOversampledSamples = 0;

        ready.store(true, std::memory_order_release);
    }

    /**
        Resets all buffers and delay lines. Call when playback stops.
    */
    void reset()
    {
        normalDryBuffer.clear();
        oversampledDryBuffer.clear();

        for (auto& channelBuffer : delayBuffer)
            std::fill(channelBuffer.begin(), channelBuffer.end(), 0.0f);
        delayWritePos = 0;

        oversampledDryCaptured = false;
        normalDryCaptured = false;
        lastOversampledSamples = 0;
    }

    /**
        Sets the latency introduced by oversampling (in samples at base rate).
        Call this when the oversampling factor changes.

        @param samples  Oversampling latency in samples (from Oversampler::getLatencyInSamples())
    */
    void setOversamplingLatency(int samples)
    {
        oversamplingLatency = samples;
    }

    /**
        Sets additional latency for lookahead or other processing (in samples at base rate).

        @param samples  Additional latency in samples
    */
    void setAdditionalLatency(int samples)
    {
        additionalLatency = samples;
    }

    /**
        Gets the total latency for PDC reporting.

        @return Total latency in samples at base rate
    */
    int getTotalLatency() const
    {
        return oversamplingLatency + additionalLatency;
    }

    //==============================================================================
    // Oversampled Mixing (Preferred - Tier 1)
    //==============================================================================

    /**
        Captures the dry signal at oversampled rate BEFORE processing.
        Call this immediately after upsampling, before any wet processing.

        @param oversampledBlock  The audio block after upsampling
        @return true if capture succeeded, false if buffer was too small (wet-only output)
    */
    bool captureDryAtOversampledRate(const juce::dsp::AudioBlock<float>& oversampledBlock)
    {
        if (!ready.load(std::memory_order_acquire))
            return false;

        const int numChannels = static_cast<int>(oversampledBlock.getNumChannels());
        const int numSamples = static_cast<int>(oversampledBlock.getNumSamples());

        // Check buffer capacity
        if (oversampledDryBuffer.getNumChannels() < numChannels ||
            oversampledDryBuffer.getNumSamples() < numSamples)
        {
            // Buffer too small - fail safe, caller should output wet-only
            oversampledDryCaptured = false;
            jassertfalse;  // Alert during debug
            return false;
        }

        // Copy oversampled signal before processing
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* src = oversampledBlock.getChannelPointer(static_cast<size_t>(ch));
            float* dst = oversampledDryBuffer.getWritePointer(ch);
            std::memcpy(dst, src, static_cast<size_t>(numSamples) * sizeof(float));
        }

        lastOversampledSamples = numSamples;
        oversampledDryCaptured = true;
        return true;
    }

    /**
        Mixes dry and wet signals at oversampled rate BEFORE downsampling.
        Both signals will pass through the same anti-aliasing filter.

        @param oversampledBlock  The wet signal to mix into (modified in-place)
        @param mixAmount         Mix amount (0.0 = 100% dry, 1.0 = 100% wet)
    */
    void mixAtOversampledRate(juce::dsp::AudioBlock<float>& oversampledBlock, float mixAmount)
    {
        // Skip if mix is 100% wet or no dry captured
        if (mixAmount >= 0.999f || !oversampledDryCaptured)
        {
            oversampledDryCaptured = false;
            return;
        }

        const int numChannels = static_cast<int>(oversampledBlock.getNumChannels());
        const int numSamples = juce::jmin(static_cast<int>(oversampledBlock.getNumSamples()),
                                          lastOversampledSamples);

        // Skip if mix is 100% dry (just copy dry over wet)
        if (mixAmount <= 0.001f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* wet = oversampledBlock.getChannelPointer(static_cast<size_t>(ch));
                const float* dry = oversampledDryBuffer.getReadPointer(ch);
                std::memcpy(wet, dry, static_cast<size_t>(numSamples) * sizeof(float));
            }
            oversampledDryCaptured = false;
            return;
        }

        // Normal mixing: output = wet * mixAmount + dry * (1 - mixAmount)
        const float wetAmount = mixAmount;
        const float dryAmount = 1.0f - mixAmount;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* wet = oversampledBlock.getChannelPointer(static_cast<size_t>(ch));
            const float* dry = oversampledDryBuffer.getReadPointer(ch);

            for (int i = 0; i < numSamples; ++i)
            {
                wet[i] = wet[i] * wetAmount + dry[i] * dryAmount;
            }
        }

        // Clear flag after mixing (must capture again next block)
        oversampledDryCaptured = false;
    }

    //==============================================================================
    // Compensated Mixing (Fallback - Tier 2)
    //==============================================================================

    /**
        Captures the dry signal at normal rate BEFORE oversampling.
        Use this when oversampled mixing isn't practical.

        @param buffer  The input audio buffer
    */
    void captureDryAtNormalRate(const juce::AudioBuffer<float>& buffer)
    {
        if (!ready.load(std::memory_order_acquire))
            return;

        const int numChannels = juce::jmin(buffer.getNumChannels(), normalDryBuffer.getNumChannels());
        const int numSamples = juce::jmin(buffer.getNumSamples(), normalDryBuffer.getNumSamples());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* src = buffer.getReadPointer(ch);
            float* dst = normalDryBuffer.getWritePointer(ch);
            std::memcpy(dst, src, static_cast<size_t>(numSamples) * sizeof(float));
        }

        lastNormalSamples = numSamples;
        normalDryCaptured = true;
    }

    /**
        Mixes dry and wet signals at normal rate AFTER downsampling.
        Applies delay compensation to the dry signal to match oversampling latency.

        @param buffer      The wet signal to mix into (modified in-place)
        @param mixAmount   Mix amount (0.0 = 100% dry, 1.0 = 100% wet)
    */
    void mixAtNormalRate(juce::AudioBuffer<float>& buffer, float mixAmount)
    {
        if (!normalDryCaptured || mixAmount >= 0.999f)
        {
            normalDryCaptured = false;
            return;
        }

        const int numChannels = juce::jmin(buffer.getNumChannels(), normalDryBuffer.getNumChannels());
        const int numSamples = juce::jmin(buffer.getNumSamples(), lastNormalSamples);
        const int totalDelay = oversamplingLatency + additionalLatency;

        // Skip if mix is 100% dry (just copy dry over wet)
        if (mixAmount <= 0.001f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* wet = buffer.getWritePointer(ch);
                const float* dry = normalDryBuffer.getReadPointer(ch);
                std::memcpy(wet, dry, static_cast<size_t>(numSamples) * sizeof(float));
            }
            normalDryCaptured = false;
            return;
        }

        const float wetAmount = mixAmount;
        const float dryAmount = 1.0f - mixAmount;

        // If no delay needed, simple mix
        if (totalDelay <= 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* wet = buffer.getWritePointer(ch);
                const float* dry = normalDryBuffer.getReadPointer(ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    wet[i] = wet[i] * wetAmount + dry[i] * dryAmount;
                }
            }
        }
        else
        {
            // Apply delay compensation via ring buffer
            const int delayToApply = juce::jmin(totalDelay, MAX_DELAY_SAMPLES - 1);
            const int channelsToProcess = juce::jmin(numChannels, MAX_CHANNELS);

            for (int i = 0; i < numSamples; ++i)
            {
                // Calculate read position (circular) - use bitwise AND for efficient wraparound
                int readPos = (delayWritePos - delayToApply + MAX_DELAY_SAMPLES) & DELAY_MASK;

                for (int ch = 0; ch < channelsToProcess; ++ch)
                {
                    float* wet = buffer.getWritePointer(ch);
                    const float* dry = normalDryBuffer.getReadPointer(ch);

                    // Read delayed sample, write current to delay line
                    float delayedDry = delayBuffer[static_cast<size_t>(ch)][static_cast<size_t>(readPos)];
                    delayBuffer[static_cast<size_t>(ch)][static_cast<size_t>(delayWritePos)] = dry[i];

                    // Mix with delayed dry
                    wet[i] = wet[i] * wetAmount + delayedDry * dryAmount;
                }

                // Advance write position (bitwise AND for efficient wraparound)
                delayWritePos = (delayWritePos + 1) & DELAY_MASK;
            }
        }

        normalDryCaptured = false;
    }

    //==============================================================================
    // State Queries
    //==============================================================================

    /**
        Checks if the mixer is properly initialized.
    */
    bool isReady() const { return ready.load(std::memory_order_acquire); }

    /**
        Checks if oversampled dry buffer has valid data for mixing.
    */
    bool hasOversampledDry() const { return oversampledDryCaptured; }

    /**
        Checks if normal-rate dry buffer has valid data for mixing.
    */
    bool hasNormalDry() const { return normalDryCaptured; }

private:
    //==============================================================================
    // Buffer Management
    //==============================================================================

    // Maximum channels supported (stereo)
    static constexpr int MAX_CHANNELS = 2;

    // Maximum delay samples for compensation (enough for 4x oversampling + lookahead)
    // Must be power of 2 for efficient bitwise wraparound
    static constexpr int MAX_DELAY_SAMPLES = 256;
    static constexpr int DELAY_MASK = MAX_DELAY_SAMPLES - 1;  // 255 for bitwise AND

    // Oversampled dry buffer (for tier 1 mixing)
    juce::AudioBuffer<float> oversampledDryBuffer;
    bool oversampledDryCaptured{false};
    int lastOversampledSamples{0};

    // Normal-rate dry buffer (for tier 2 mixing)
    juce::AudioBuffer<float> normalDryBuffer;
    bool normalDryCaptured{false};
    int lastNormalSamples{0};

    // Ring buffer delay line for latency compensation (tier 2)
    std::array<std::array<float, MAX_DELAY_SAMPLES>, MAX_CHANNELS> delayBuffer{};
    int delayWritePos{0};

    //==============================================================================
    // State Variables
    //==============================================================================

    std::atomic<bool> ready{false};
    int preparedChannels{0};
    int preparedBlockSize{0};
    int preparedMaxOversamplingFactor{1};

    int oversamplingLatency{0};
    int additionalLatency{0};

    double baseSampleRate{44100.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DryWetMixer)
};

} // namespace LunaAudio

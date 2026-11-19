/*
  ==============================================================================

    DryWetMixer.h
    Phase-coherent dry/wet mixer for use with oversampled processing

    Prevents comb filtering artifacts that occur when mixing dry and wet signals
    that have different latencies due to FIR anti-aliasing filters in oversampling.

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <algorithm>

namespace DuskAudio
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
        DuskAudio::DryWetMixer dryWetMixer;

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

        // Clear oversampled processing delay buffers
        for (auto& channelBuffer : osDelayBuffer)
            std::fill(channelBuffer.begin(), channelBuffer.end(), 0.0f);
        osDelayWritePos = 0;

        // Clear capture flags
        oversampledDryCaptured = false;
        normalDryCaptured = false;
        lastOversampledSamples = 0;

        // No ramp across a prepare — start settled wherever the host left us.
        snapMixToTarget();

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

        for (auto& channelBuffer : osDelayBuffer)
            std::fill(channelBuffer.begin(), channelBuffer.end(), 0.0f);
        osDelayWritePos = 0;

        oversampledDryCaptured = false;
        normalDryCaptured = false;
        lastOversampledSamples = 0;
        snapMixToTarget();
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
        Sets the processing latency introduced by the wet signal chain
        (in samples at base rate). Used to delay the dry signal so it
        aligns with the wet signal's group delay, preventing comb filtering.

        This works for both Tier 1 (oversampled) and Tier 2 (normal rate) mixing:
        - Tier 1: delay is scaled by currentOversamplingFactor for ring buffer
        - Tier 2: delay is used directly at base rate

        Call this before mixing each block, or whenever the processing chain's
        group delay changes (e.g., wow/flutter toggle, oversampling change).

        @param samplesAtBaseRate  Processing latency in base-rate samples
    */
    void setProcessingLatency(int samplesAtBaseRate)
    {
        const int maxBase = (MAX_OS_DELAY_SAMPLES - 1) / juce::jmax(1, currentOversamplingFactor);
        jassert(samplesAtBaseRate <= maxBase);
        processingLatencyBase = juce::jlimit(0, maxBase, samplesAtBaseRate);
    }

    int getProcessingLatency() const { return processingLatencyBase; }

    /**
        Sets the current oversampling factor. Must be called whenever the
        oversampling factor changes so Tier 1 can correctly scale the
        processing latency for the ring buffer.

        @param factor  Current oversampling factor (1, 2, or 4)
    */
    void setCurrentOversamplingFactor(int factor)
    {
        currentOversamplingFactor = juce::jlimit(1, preparedMaxOversamplingFactor > 0 ? preparedMaxOversamplingFactor : 4, factor);
        // Re-clamp processing latency in case the new factor makes it exceed ring buffer
        const int maxBase = (MAX_OS_DELAY_SAMPLES - 1) / currentOversamplingFactor;
        processingLatencyBase = juce::jmin(processingLatencyBase, maxBase);
    }

    /**
        Gets the total latency for PDC reporting.

        @return Total latency in samples at base rate
    */
    int getTotalLatency() const
    {
        return oversamplingLatency + additionalLatency + processingLatencyBase;
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
        setTargetMix(mixAmount);

        if (!oversampledDryCaptured)
            return;

        const int numChannels = static_cast<int>(oversampledBlock.getNumChannels());
        const int numSamples = juce::jmin(static_cast<int>(oversampledBlock.getNumSamples()),
                                          lastOversampledSamples);

        // Scale base-rate processing latency to oversampled rate
        const int osProcessingLatency = processingLatencyBase * currentOversamplingFactor;
        const int delayToApply = juce::jlimit(0, MAX_OS_DELAY_SAMPLES - 1, osProcessingLatency);
        const int channelsToProcess = juce::jmin(numChannels, MAX_CHANNELS);
        const double osRate = baseSampleRate * currentOversamplingFactor;

        // ONE loop for every case. The endpoints are not special-cased: at a
        // settled mix of 1.0 the dry term multiplies by exactly 0 (bit-identical
        // wet passthrough) and at 0.0 the wet term does, so nothing jumps when
        // the knob crosses an endpoint. The ring is written unconditionally so
        // the delayed dry is never stale when the mix comes back off 100 % wet
        // — reading it back at delayToApply == 0 returns the sample just
        // written, which is the undelayed dry.
        for (int i = 0; i < numSamples; ++i)
        {
            const int readPos = (osDelayWritePos - delayToApply + MAX_OS_DELAY_SAMPLES) & OS_DELAY_MASK;
            const float wetAmount = advanceMix(osRate);
            const float dryAmount = 1.0f - wetAmount;

            for (int ch = 0; ch < channelsToProcess; ++ch)
            {
                float* wet = oversampledBlock.getChannelPointer(static_cast<size_t>(ch));
                const float* dry = oversampledDryBuffer.getReadPointer(ch);

                osDelayBuffer[static_cast<size_t>(ch)][static_cast<size_t>(osDelayWritePos)] = dry[i];
                const float delayedDry = osDelayBuffer[static_cast<size_t>(ch)][static_cast<size_t>(readPos)];

                wet[i] = wet[i] * wetAmount + delayedDry * dryAmount;
            }

            osDelayWritePos = (osDelayWritePos + 1) & OS_DELAY_MASK;
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
        setTargetMix(mixAmount);

        if (!normalDryCaptured)
            return;

        const int numChannels = juce::jmin(buffer.getNumChannels(), normalDryBuffer.getNumChannels());
        const int numSamples = juce::jmin(buffer.getNumSamples(), lastNormalSamples);
        const int totalDelay = oversamplingLatency + additionalLatency + processingLatencyBase;
        const int delayToApply = juce::jlimit(0, MAX_DELAY_SAMPLES - 1, totalDelay);
        const int channelsToProcess = juce::jmin(numChannels, MAX_CHANNELS);

        // Single loop, endpoints included — see mixAtOversampledRate for why the
        // fast paths are gone and why the ring is written unconditionally.
        for (int i = 0; i < numSamples; ++i)
        {
            const int readPos = (delayWritePos - delayToApply + MAX_DELAY_SAMPLES) & DELAY_MASK;
            const float wetAmount = advanceMix(baseSampleRate);
            const float dryAmount = 1.0f - wetAmount;

            for (int ch = 0; ch < channelsToProcess; ++ch)
            {
                float* wet = buffer.getWritePointer(ch);
                const float* dry = normalDryBuffer.getReadPointer(ch);

                delayBuffer[static_cast<size_t>(ch)][static_cast<size_t>(delayWritePos)] = dry[i];
                const float delayedDry = delayBuffer[static_cast<size_t>(ch)][static_cast<size_t>(readPos)];

                wet[i] = wet[i] * wetAmount + delayedDry * dryAmount;
            }

            delayWritePos = (delayWritePos + 1) & DELAY_MASK;
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

    /**
        Sets the mix the ramp is heading towards (0.0 = 100% dry, 1.0 = 100% wet).
        Called for you by the mix functions; call it directly only to retarget
        without processing a block.
    */
    void setTargetMix(float mixAmount)
    {
        mixTarget = juce::jlimit(0.0f, 1.0f, mixAmount);
    }

    /**
        Snaps the ramp to its target. Use on prepare/reset or any discontinuity
        (preset load, transport relocate) where a ramp would be pointless.
    */
    void snapMixToTarget() { mixCurrent = mixTarget; }

    /**
        True while the ramp is still travelling. The host processor must keep
        capturing dry while this is true, even at a target of 100% wet, or the
        remainder of the ramp has nothing to blend against.
    */
    bool isMixSmoothing() const { return mixCurrent != mixTarget; }

    /** Current (ramped) mix value — what the last processed sample actually used. */
    float getCurrentMix() const { return mixCurrent; }

    /** Ramp length in seconds. Default 20 ms: inaudible steps, still snappy. */
    void setMixRampSeconds(double seconds) { mixRampSeconds = juce::jmax(1.0e-4, seconds); }

private:
    //==============================================================================
    // Mix ramp
    //==============================================================================

    /**
        Advances the linear mix ramp by one sample at the given rate and returns
        the value to use. Rate is a parameter rather than state because the same
        ramp is advanced at base rate (tier 2) or oversampled rate (tier 1), and
        the ramp must last the same wall-clock time either way.
    */
    float advanceMix(double rate)
    {
        if (mixCurrent == mixTarget)
            return mixCurrent;

        const float maxStep = static_cast<float>(1.0 / (mixRampSeconds * juce::jmax(1.0, rate)));
        const float diff = mixTarget - mixCurrent;

        if (std::abs(diff) <= maxStep)
            mixCurrent = mixTarget;
        else
            mixCurrent += (diff > 0.0f ? maxStep : -maxStep);

        return mixCurrent;
    }

    float mixTarget{1.0f};
    float mixCurrent{1.0f};
    double mixRampSeconds{0.02};

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

    // Ring buffer for oversampled-rate processing delay compensation (tier 1)
    // Used to delay the dry signal to match the wet processing chain's group delay
    static constexpr int MAX_OS_DELAY_SAMPLES = 2048;  // Power of 2 (holds up to 512 base * 4x OS)
    static constexpr int OS_DELAY_MASK = MAX_OS_DELAY_SAMPLES - 1;
    std::array<std::array<float, MAX_OS_DELAY_SAMPLES>, MAX_CHANNELS> osDelayBuffer{};
    int osDelayWritePos{0};
    int processingLatencyBase{0};  // Processing latency in base-rate samples
    int currentOversamplingFactor{1};  // Current OS factor for Tier 1 scaling

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

} // namespace DuskAudio

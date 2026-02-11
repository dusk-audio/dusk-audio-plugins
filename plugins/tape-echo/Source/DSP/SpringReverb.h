/*
  ==============================================================================

    SpringReverb.h
    Tape Echo - RE-201 Style Spring Reverb

    Modeled as parallel chirped allpass network (6-8 allpass filters per spring)
    with characteristic metallic attack transient, 2-3s decay time.

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace TapeEchoDSP
{

// Simple thread-safe delay line for pre-delay
class SimplePreDelayLine
{
public:
    SimplePreDelayLine() = default;

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

        // Clamp delay to valid range
        delaySamples = juce::jlimit(1.0f, static_cast<float>(bufferSize - 2), delaySamples);

        // Linear interpolation
        int delayInt = static_cast<int>(delaySamples);
        float frac = delaySamples - static_cast<float>(delayInt);

        int readIndex1 = (writeIndex - delayInt + bufferSize) % bufferSize;
        int readIndex2 = (readIndex1 - 1 + bufferSize) % bufferSize;

        float sample1 = buffer[static_cast<size_t>(readIndex1)];
        float sample2 = buffer[static_cast<size_t>(readIndex2)];

        return sample1 + frac * (sample2 - sample1);
    }

private:
    std::vector<float> buffer;
    int bufferSize = 0;
    int writeIndex = 0;
};

// Simple allpass filter for spring reverb
class AllpassFilter
{
public:
    void prepare(double sampleRate, float delayMs, float feedback)
    {
        delaySamples = static_cast<int>(delayMs * sampleRate / 1000.0);
        delaySamples = juce::jlimit(1, static_cast<int>(bufferSize) - 1, delaySamples);
        this->feedback = feedback;
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    float process(float input)
    {
        int readIndex = writeIndex - delaySamples;
        if (readIndex < 0) readIndex += static_cast<int>(bufferSize);

        float delayed = buffer[readIndex];
        float output = -input + delayed;
        buffer[writeIndex] = input + feedback * delayed;

        writeIndex++;
        if (writeIndex >= static_cast<int>(bufferSize))
            writeIndex = 0;

        return output;
    }

private:
    static constexpr size_t bufferSize = 4096;
    std::array<float, bufferSize> buffer {};
    int delaySamples = 1;
    int writeIndex = 0;
    float feedback = 0.5f;
};

class SpringReverb
{
public:
    // Pre-delay time in ms
    static constexpr float PRE_DELAY_MS = 25.0f;

    // Decay time in seconds
    static constexpr float DECAY_TIME_S = 2.5f;

    SpringReverb() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        prepared.store(false, std::memory_order_release);
        currentSampleRate.store(sampleRate, std::memory_order_release);

        // Pre-delay
        const int preDelaySamples = static_cast<int>(PRE_DELAY_MS * sampleRate / 1000.0) + 16;
        preDelayL.prepare(preDelaySamples);
        preDelayR.prepare(preDelaySamples);

        // Store configured max delay
        maxPreDelaySamplesConfigured.store(preDelaySamples, std::memory_order_release);

        // Configure allpass chains for spring character
        // Use chirped delays (progressively longer) for metallic character
        // Left channel spring
        const float baseDelayL[] = { 3.1f, 5.3f, 8.7f, 13.1f, 19.7f, 28.9f };
        const float baseFeedbackL = 0.65f;
        for (int i = 0; i < 6; ++i)
        {
            allpassL[i].prepare(sampleRate, baseDelayL[i], baseFeedbackL);
        }

        // Right channel spring (slightly different for stereo)
        const float baseDelayR[] = { 3.3f, 5.7f, 9.1f, 13.7f, 20.3f, 29.7f };
        const float baseFeedbackR = 0.64f;
        for (int i = 0; i < 6; ++i)
        {
            allpassR[i].prepare(sampleRate, baseDelayR[i], baseFeedbackR);
        }

        // Bandpass filter (200Hz - 5kHz characteristic of springs)
        updateFilters();

        // Decay filter (lowpass in feedback)
        decayFilterL.reset();
        decayFilterR.reset();
        updateDecayFilter();

        // Smoothing
        mixSmoothed.reset(sampleRate, 0.05);

        juce::ignoreUnused(maxBlockSize);

        prepared.store(true, std::memory_order_release);
    }

    bool isPrepared() const { return prepared.load(std::memory_order_acquire); }

    void reset()
    {
        preDelayL.reset();
        preDelayR.reset();

        for (int i = 0; i < 6; ++i)
        {
            allpassL[i].reset();
            allpassR[i].reset();
        }

        highpassL.reset();
        highpassR.reset();
        lowpassL.reset();
        lowpassR.reset();
        decayFilterL.reset();
        decayFilterR.reset();

        feedbackL = 0.0f;
        feedbackR = 0.0f;
    }

    void setMix(float mix)
    {
        mixSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, mix));
    }

    // Process and return wet signal only
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
        const int maxPreDelay = maxPreDelaySamplesConfigured.load(std::memory_order_acquire);

        // Additional safety check
        if (sampleRate <= 0.0 || maxPreDelay <= 0)
        {
            buffer.clear();
            return;
        }

        const int numSamples = buffer.getNumSamples();
        float* leftChannel = buffer.getWritePointer(0);
        float* rightChannel = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

        float preDelaySamples = PRE_DELAY_MS * static_cast<float>(sampleRate) / 1000.0f;
        // Clamp to valid range
        preDelaySamples = juce::jlimit(1.0f, static_cast<float>(maxPreDelay - 1), preDelaySamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const float inputL = leftChannel[i];
            const float inputR = rightChannel ? rightChannel[i] : inputL;

            // Pre-delay
            preDelayL.pushSample(inputL);
            preDelayR.pushSample(inputR);
            float delayedL = preDelayL.popSample(preDelaySamples);
            float delayedR = preDelayR.popSample(preDelaySamples);

            // Add feedback
            float springInputL = delayedL + feedbackL * 0.35f;
            float springInputR = delayedR + feedbackR * 0.35f;

            // Process through allpass chain (spring simulation)
            float springL = springInputL;
            float springR = springInputR;

            for (int ap = 0; ap < 6; ++ap)
            {
                springL = allpassL[ap].process(springL);
                springR = allpassR[ap].process(springR);
            }

            // Bandpass filtering (spring character: 200Hz - 5kHz)
            springL = highpassL.processSample(springL);
            springL = lowpassL.processSample(springL);
            springR = highpassR.processSample(springR);
            springR = lowpassR.processSample(springR);

            // Decay filtering (lowpass feedback for natural decay)
            float fbL = decayFilterL.processSample(springL);
            float fbR = decayFilterR.processSample(springR);

            // Store for feedback
            feedbackL = juce::jlimit(-1.5f, 1.5f, fbL);
            feedbackR = juce::jlimit(-1.5f, 1.5f, fbR);

            // Add metallic attack transient (cross-coupling for stereo interest)
            float outputL = springL + springR * 0.15f;
            float outputR = springR + springL * 0.15f;

            // Soft limiting
            outputL = std::tanh(outputL * 0.8f);
            outputR = std::tanh(outputR * 0.8f);

            leftChannel[i] = outputL;
            if (rightChannel)
                rightChannel[i] = outputR;
        }
    }

private:
    std::atomic<bool> prepared { false };
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> maxPreDelaySamplesConfigured { 4096 };  // Match initial delay line size

    // Pre-delay using simple ring buffer
    SimplePreDelayLine preDelayL;
    SimplePreDelayLine preDelayR;

    // Allpass chains (6 stages each for stereo)
    std::array<AllpassFilter, 6> allpassL;
    std::array<AllpassFilter, 6> allpassR;

    // Bandpass filters (200Hz high-pass, 5kHz low-pass)
    juce::dsp::IIR::Filter<float> highpassL;
    juce::dsp::IIR::Filter<float> highpassR;
    juce::dsp::IIR::Filter<float> lowpassL;
    juce::dsp::IIR::Filter<float> lowpassR;

    // Decay filter
    juce::dsp::IIR::Filter<float> decayFilterL;
    juce::dsp::IIR::Filter<float> decayFilterR;

    // Feedback state
    float feedbackL = 0.0f;
    float feedbackR = 0.0f;

    // Smoothed mix
    juce::SmoothedValue<float> mixSmoothed { 0.0f };

    void updateFilters()
    {
        const double sampleRate = currentSampleRate.load(std::memory_order_acquire);
        if (sampleRate <= 0.0) return;

        // High-pass at 200Hz
        auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
            sampleRate, 200.0f, 0.707f);
        *highpassL.coefficients = *hpCoeffs;
        *highpassR.coefficients = *hpCoeffs;

        // Low-pass at 5kHz
        auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
            sampleRate, 5000.0f, 0.707f);
        *lowpassL.coefficients = *lpCoeffs;
        *lowpassR.coefficients = *lpCoeffs;
    }

    void updateDecayFilter()
    {
        const double sampleRate = currentSampleRate.load(std::memory_order_acquire);
        if (sampleRate <= 0.0) return;

        // Lowpass in feedback for natural decay
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
            sampleRate, 3500.0f, 0.707f);
        *decayFilterL.coefficients = *coeffs;
        *decayFilterR.coefficients = *coeffs;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpringReverb)
};

} // namespace TapeEchoDSP

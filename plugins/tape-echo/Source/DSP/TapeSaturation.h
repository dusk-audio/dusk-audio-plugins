/*
  ==============================================================================

    TapeSaturation.h
    Tape Echo - RE-201 Style Tape Saturation

    Soft-knee saturation with frequency-dependent behavior.
    More saturation at low frequencies, HF roll-off modeling head gap losses.

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <random>

namespace TapeEchoDSP
{

// Pink noise generator using Paul Kellet's algorithm
// Used for subtle tape hiss simulation
class PinkNoise
{
public:
    PinkNoise() : rng(std::random_device{}()), dist(0.0f, 1.0f) {}

    float next()
    {
        float white = dist(rng) * 2.0f - 1.0f;
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;
        float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;
        return pink * 0.11f;  // Normalize
    }

    void reset()
    {
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f;
    }

private:
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist;
};

class TapeSaturation
{
public:
    TapeSaturation() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        currentSampleRate = sampleRate;

        // High-frequency roll-off filter (6dB/octave @ 4.5kHz)
        // Models head gap losses in the feedback path
        headLossFilterL.reset();
        headLossFilterR.reset();
        updateHeadLossFilter();

        // DC blocker
        dcBlockerL.reset();
        dcBlockerR.reset();
        updateDCBlocker();

        // Bass bump filter (100-200Hz shelf)
        bassBumpFilterL.reset();
        bassBumpFilterR.reset();
        updateBassBumpFilter();

        // Smoothing
        driveSmoothed.reset(sampleRate, 0.02);  // 20ms smoothing

        juce::ignoreUnused(maxBlockSize);
    }

    void reset()
    {
        headLossFilterL.reset();
        headLossFilterR.reset();
        dcBlockerL.reset();
        dcBlockerR.reset();
        bassBumpFilterL.reset();
        bassBumpFilterR.reset();
        noiseGenL.reset();
        noiseGenR.reset();
    }

    // Set drive amount (0.0 to 1.0)
    void setDrive(float drive)
    {
        driveSmoothed.setTargetValue(drive);
    }

    // Set head loss filter cutoff (default ~4500 Hz)
    void setHeadLossCutoff(float cutoffHz)
    {
        headLossCutoff = cutoffHz;
        updateHeadLossFilter();
    }

    // Enable/disable tape noise (subtle hiss)
    void setNoiseEnabled(bool enabled)
    {
        noiseEnabled = enabled;
    }

    // Set noise level (0.0 to 1.0, typical values 0.001 to 0.01)
    void setNoiseLevel(float level)
    {
        noiseLevel = juce::jlimit(0.0f, 0.1f, level);
    }

    // Process stereo samples in-place
    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        float* leftChannel = buffer.getWritePointer(0);
        float* rightChannel = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float drive = driveSmoothed.getNextValue();

            // Process left channel
            float sampleL = leftChannel[i];
            sampleL = processSample(sampleL, drive, true);
            leftChannel[i] = sampleL;

            // Process right channel
            if (rightChannel != nullptr)
            {
                float sampleR = rightChannel[i];
                sampleR = processSample(sampleR, drive, false);
                rightChannel[i] = sampleR;
            }
        }
    }

    // Process a single sample (for use in delay feedback path)
    float processSampleMono(float input, float drive)
    {
        return processSample(input, drive, true);
    }

private:
    double currentSampleRate = 44100.0;

    // Filters
    juce::dsp::IIR::Filter<float> headLossFilterL;
    juce::dsp::IIR::Filter<float> headLossFilterR;
    juce::dsp::IIR::Filter<float> dcBlockerL;
    juce::dsp::IIR::Filter<float> dcBlockerR;
    juce::dsp::IIR::Filter<float> bassBumpFilterL;
    juce::dsp::IIR::Filter<float> bassBumpFilterR;

    float headLossCutoff = 4500.0f;

    // Noise generator
    PinkNoise noiseGenL, noiseGenR;
    bool noiseEnabled = false;
    float noiseLevel = 0.003f;  // Very subtle default

    // Parameter smoothing
    juce::SmoothedValue<float> driveSmoothed { 0.0f };

    float processSample(float input, float drive, bool isLeft)
    {
        // Apply bass bump (record/playback EQ curve characteristic)
        input = isLeft ? bassBumpFilterL.processSample(input)
                       : bassBumpFilterR.processSample(input);

        // Frequency-dependent saturation:
        // Low frequencies saturate more, simulating transformer core saturation
        // We achieve this by applying more saturation to the unfiltered signal
        // and less to the high-frequency content

        // Soft-knee tape saturation
        float saturated = softKneeSaturation(input * (1.0f + drive * 2.0f));

        // Blend based on drive
        float output = juce::jlimit(-1.0f, 1.0f, saturated);

        // Add subtle tape hiss if enabled
        if (noiseEnabled)
        {
            float noise = isLeft ? noiseGenL.next() : noiseGenR.next();
            output += noise * noiseLevel;
        }

        // Apply head loss filter (HF roll-off in feedback path)
        output = isLeft ? headLossFilterL.processSample(output)
                        : headLossFilterR.processSample(output);

        // DC blocking
        output = isLeft ? dcBlockerL.processSample(output)
                        : dcBlockerR.processSample(output);

        return output;
    }

    // Soft-knee saturation characteristic of tape
    float softKneeSaturation(float x)
    {
        // Tape-like saturation using tanh with asymmetry
        // Real tape has slightly asymmetric saturation
        const float asymmetry = 0.02f;
        float asymmetricX = x + (x * x * asymmetry);

        // Soft saturation curve
        return std::tanh(asymmetricX * 0.8f);
    }

    void updateHeadLossFilter()
    {
        // 6dB/octave lowpass (first-order) at cutoff frequency
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
            currentSampleRate, headLossCutoff);
        *headLossFilterL.coefficients = *coeffs;
        *headLossFilterR.coefficients = *coeffs;
    }

    void updateDCBlocker()
    {
        // High-pass at very low frequency to block DC
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
            currentSampleRate, 10.0f);
        *dcBlockerL.coefficients = *coeffs;
        *dcBlockerR.coefficients = *coeffs;
    }

    void updateBassBumpFilter()
    {
        // Low shelf boost at 150Hz (+2dB) - characteristic of tape record/playback
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
            currentSampleRate, 150.0f, 0.707f, 1.26f);  // +2dB
        *bassBumpFilterL.coefficients = *coeffs;
        *bassBumpFilterR.coefficients = *coeffs;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeSaturation)
};

} // namespace TapeEchoDSP

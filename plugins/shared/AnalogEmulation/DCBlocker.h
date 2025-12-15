/*
  ==============================================================================

    DCBlocker.h
    Simple DC blocking filter for analog emulation processing

    A first-order highpass filter to remove DC offset that can accumulate
    from asymmetric saturation and transformer coupling.

  ==============================================================================
*/

#pragma once

#include <cmath>

namespace AnalogEmulation {

/**
 * Simple DC blocking filter using a first-order highpass
 * Cutoff frequency is approximately 5-10Hz depending on sample rate
 *
 * Transfer function: H(z) = (1 - z^-1) / (1 - R*z^-1)
 * where R = 1 - (2*pi*fc/fs)
 */
class DCBlocker
{
public:
    DCBlocker() = default;

    /**
     * Prepare the filter for processing
     * @param sampleRate The audio sample rate
     * @param cutoffHz The cutoff frequency in Hz (default 5Hz)
     */
    void prepare(double sampleRate, float cutoffHz = 5.0f)
    {
        // Calculate coefficient for the given cutoff frequency
        // R = 1 - (2 * pi * fc / fs)
        // For 5Hz at 44.1kHz: R ≈ 0.9993
        coefficient = 1.0f - (2.0f * 3.14159265359f * cutoffHz / static_cast<float>(sampleRate));
        coefficient = std::max(0.9f, std::min(0.9999f, coefficient));  // Clamp to valid range
        reset();
    }

    /**
     * Reset the filter state
     */
    void reset()
    {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    /**
     * Process a single sample
     * @param input The input sample
     * @return The DC-blocked output sample
     */
    float processSample(float input)
    {
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        float output = input - x1 + coefficient * y1;
        x1 = input;
        y1 = output;
        return output;
    }

    /**
     * Process a block of samples in-place
     * @param buffer Pointer to the sample buffer
     * @param numSamples Number of samples to process
     */
    void processBlock(float* buffer, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            buffer[i] = processSample(buffer[i]);
        }
    }

private:
    float coefficient = 0.9993f;  // Default for ~5Hz at 44.1kHz
    float x1 = 0.0f;              // Previous input sample
    float y1 = 0.0f;              // Previous output sample
};

/**
 * Stereo DC blocker for processing two channels with identical filter states
 */
class StereoDCBlocker
{
public:
    StereoDCBlocker() = default;

    void prepare(double sampleRate, float cutoffHz = 5.0f)
    {
        left.prepare(sampleRate, cutoffHz);
        right.prepare(sampleRate, cutoffHz);
    }

    void reset()
    {
        left.reset();
        right.reset();
    }

    void processSample(float& inputL, float& inputR)
    {
        inputL = left.processSample(inputL);
        inputR = right.processSample(inputR);
    }

    void processBlock(float* bufferL, float* bufferR, int numSamples)
    {
        left.processBlock(bufferL, numSamples);
        right.processBlock(bufferR, numSamples);
    }

private:
    DCBlocker left;
    DCBlocker right;
};

} // namespace AnalogEmulation

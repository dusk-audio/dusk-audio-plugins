// DCBlocker.h — First-order highpass DC blocking filter

#pragma once

#include <cmath>

namespace AnalogEmulation {

// H(z) = (1 - z^-1) / (1 - R*z^-1), R = 1 - (2*pi*fc/fs)
class DCBlocker
{
public:
    DCBlocker() = default;

    void prepare(double sampleRate, float cutoffHz = 5.0f)
    {
        coefficient = 1.0f - (2.0f * 3.14159265359f * cutoffHz / static_cast<float>(sampleRate));
        coefficient = std::max(0.9f, std::min(0.9999f, coefficient));  // Clamp to valid range
        reset();
    }

    void reset()
    {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    float processSample(float input)
    {
        if (!std::isfinite(input))
        {
            x1 = 0.0f;
            y1 = 0.0f;
            return 0.0f;
        }
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        float output = input - x1 + coefficient * y1;
        x1 = input;
        y1 = output;
        return output;
    }

    void processBlock(float* buffer, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            buffer[i] = processSample(buffer[i]);
        }
    }

private:
    float coefficient = 0.9993f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

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

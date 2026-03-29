// HighFrequencyEstimator.h — Differentiator + LP smoothing for adaptive saturation

#pragma once

#include <cmath>
#include <algorithm>

namespace AnalogEmulation {

// Returns 0.0 (low freq) to 1.0 (high freq) estimate for anti-aliasing saturation reduction
class HighFrequencyEstimator
{
public:
    HighFrequencyEstimator() = default;

    void prepare(double sampleRate, float smoothingHz = 100.0f)
    {
        smoothingCoeff = 1.0f - std::exp(-2.0f * 3.14159265359f * smoothingHz /
                                          static_cast<float>(sampleRate));
        reset();
    }

    void reset()
    {
        previousSample = 0.0f;
        smoothedHF = 0.0f;
    }

    float processSample(float input)
    {
        if (!std::isfinite(input))
            return std::clamp(smoothedHF * 2.0f, 0.0f, 1.0f);

        float diff = input - previousSample;
        previousSample = input;

        float hfAmount = std::abs(diff);
        smoothedHF += smoothingCoeff * (hfAmount - smoothedHF);
        return std::clamp(smoothedHF * 2.0f, 0.0f, 1.0f);
    }

    float getCurrentEstimate() const
    {
        return std::clamp(smoothedHF * 2.0f, 0.0f, 1.0f);
    }

    float getSaturationReduction(float input, float maxReduction = 0.5f)
    {
        float hfContent = processSample(input);
        return 1.0f - (hfContent * maxReduction);
    }

private:
    float smoothingCoeff = 0.1f;
    float previousSample = 0.0f;
    float smoothedHF = 0.0f;
};

class StereoHighFrequencyEstimator
{
public:
    StereoHighFrequencyEstimator() = default;

    void prepare(double sampleRate, float smoothingHz = 100.0f)
    {
        left.prepare(sampleRate, smoothingHz);
        right.prepare(sampleRate, smoothingHz);
    }

    void reset()
    {
        left.reset();
        right.reset();
    }

    float getSaturationReduction(float inputL, float inputR, float maxReduction = 0.5f)
    {
        float hfL = left.processSample(inputL);
        float hfR = right.processSample(inputR);
        float maxHF = std::max(hfL, hfR);
        return 1.0f - (maxHF * maxReduction);
    }

private:
    HighFrequencyEstimator left;
    HighFrequencyEstimator right;
};

} // namespace AnalogEmulation

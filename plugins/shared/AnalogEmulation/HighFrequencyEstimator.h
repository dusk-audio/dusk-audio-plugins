/*
  ==============================================================================

    HighFrequencyEstimator.h
    Estimates high-frequency content in a signal for adaptive saturation

    Used to reduce saturation on high-frequency content to prevent aliasing.
    Based on a simple differentiator followed by one-pole lowpass smoothing.

  ==============================================================================
*/

#pragma once

#include <cmath>
#include <algorithm>

namespace AnalogEmulation {

/**
 * Estimates the amount of high-frequency content in a signal
 * Returns a value from 0.0 (mostly low frequencies) to 1.0 (mostly high frequencies)
 *
 * This is useful for reducing saturation on high-frequency content to prevent
 * aliasing artifacts, especially when not using oversampling.
 */
class HighFrequencyEstimator
{
public:
    HighFrequencyEstimator() = default;

    /**
     * Prepare the estimator for processing
     * @param sampleRate The audio sample rate
     * @param smoothingHz Time constant for smoothing (default 100Hz = 1.6ms)
     */
    void prepare(double sampleRate, float smoothingHz = 100.0f)
    {
        // One-pole lowpass coefficient for smoothing
        // alpha = 1 - exp(-2 * pi * fc / fs)
        smoothingCoeff = 1.0f - std::exp(-2.0f * 3.14159265359f * smoothingHz /
                                          static_cast<float>(sampleRate));
        reset();
    }

    /**
     * Reset the estimator state
     */
    void reset()
    {
        previousSample = 0.0f;
        smoothedHF = 0.0f;
    }

    /**
     * Estimate high-frequency content for a single sample
     * @param input The input sample
     * @return Estimated HF content (0.0 to 1.0)
     */
    float processSample(float input)
    {
        // Differentiator: output = current - previous
        // High-frequency content produces larger differences
        float diff = input - previousSample;
        previousSample = input;

        // Rectify and smooth
        float hfAmount = std::abs(diff);
        smoothedHF += smoothingCoeff * (hfAmount - smoothedHF);

        // Normalize to approximately 0-1 range
        // Typical differentiated signal peaks around 0.5-1.0 for full-scale HF content
        return std::clamp(smoothedHF * 2.0f, 0.0f, 1.0f);
    }

    /**
     * Get the current smoothed HF estimate without processing a new sample
     * @return Current HF estimate (0.0 to 1.0)
     */
    float getCurrentEstimate() const
    {
        return std::clamp(smoothedHF * 2.0f, 0.0f, 1.0f);
    }

    /**
     * Calculate saturation reduction factor based on HF content
     * Higher HF content = more reduction to prevent aliasing
     * @param input The input sample
     * @param maxReduction Maximum reduction factor (0.0 to 1.0, default 0.5 = 50% reduction)
     * @return Saturation amount multiplier (1.0 = full saturation, lower = reduced)
     */
    float getSaturationReduction(float input, float maxReduction = 0.5f)
    {
        float hfContent = processSample(input);
        // Linear reduction from 1.0 (no HF) to (1.0 - maxReduction) (max HF)
        return 1.0f - (hfContent * maxReduction);
    }

private:
    float smoothingCoeff = 0.1f;   // One-pole smoothing coefficient
    float previousSample = 0.0f;   // For differentiator
    float smoothedHF = 0.0f;       // Smoothed HF estimate
};

/**
 * Stereo high-frequency estimator
 */
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

    /**
     * Get saturation reduction for both channels
     * Uses the maximum HF content from either channel for consistent stereo behavior
     */
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

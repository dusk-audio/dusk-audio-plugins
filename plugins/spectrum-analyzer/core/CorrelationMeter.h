#pragma once

#include <cmath>
#include <algorithm>

//==============================================================================
/**
    Stereo correlation meter.

    Calculates Pearson correlation coefficient between L and R channels.
    Range: -1 (out of phase) to +1 (mono/in phase)
*/
class CorrelationMeter
{
public:
    CorrelationMeter() = default;

    void prepare(double sampleRate)
    {
        // Integration window of ~300ms
        int windowSamples = static_cast<int>(sampleRate * 0.3);
        decayCoeff = 1.0f - (1.0f / static_cast<float>(windowSamples));

        // Smoothing for display
        smoothingCoeff = 0.95f;

        reset();
    }

    void reset()
    {
        sumLR = 0.0f;
        sumL2 = 0.0f;
        sumR2 = 0.0f;
        smoothedCorrelation = 0.0f;
    }

    void process(const float* left, const float* right, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float L = left[i];
            float R = right[i];

            // Exponential decay of running sums
            sumLR = sumLR * decayCoeff + L * R;
            sumL2 = sumL2 * decayCoeff + L * L;
            sumR2 = sumR2 * decayCoeff + R * R;
        }

        // Calculate correlation
        float correlation = calculateCorrelation();

        // Smooth for display
        smoothedCorrelation = smoothedCorrelation * smoothingCoeff +
                              correlation * (1.0f - smoothingCoeff);
    }

    //==========================================================================
    // Get raw correlation (-1 to +1)
    float getCorrelation() const
    {
        return calculateCorrelation();
    }

    // Get smoothed correlation for display
    float getSmoothedCorrelation() const
    {
        return smoothedCorrelation;
    }

    //==========================================================================
    // Interpretation helpers
    static const char* getCorrelationLabel(float correlation)
    {
        if (correlation > 0.9f) return "Mono";
        if (correlation > 0.5f) return "Good";
        if (correlation > 0.0f) return "Wide";
        if (correlation > -0.5f) return "Very Wide";
        return "Out of Phase";
    }

private:
    float calculateCorrelation() const
    {
        // Pearson correlation: r = sum(L*R) / sqrt(sum(L^2) * sum(R^2))
        float denominator = std::sqrt(sumL2 * sumR2);

        if (denominator < 1e-10f)
            return 0.0f;  // No signal

        float correlation = sumLR / denominator;
        return std::clamp(correlation, -1.0f, 1.0f);
    }

    float sumLR = 0.0f;
    float sumL2 = 0.0f;
    float sumR2 = 0.0f;

    float decayCoeff = 0.999f;
    float smoothingCoeff = 0.95f;
    float smoothedCorrelation = 0.0f;
};

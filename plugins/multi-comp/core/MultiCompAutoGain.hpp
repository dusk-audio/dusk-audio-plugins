// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Framework-free level matcher used by the Multi-Comp core.
#pragma once

#include <algorithm>
#include <cmath>

namespace duskaudio
{

class MultiCompAutoGainMatcher
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        meanSquareIn = 0.0;
        meanSquareOut = 0.0;
        primed = false;
        currentGain = 1.0f;
    }

    float update(float inRms, float outRms, int numSamples) noexcept
    {
        if (numSamples <= 0 || inRms < 1.0e-5f || outRms < 1.0e-5f)
            return currentGain;

        const double msIn = static_cast<double>(inRms) * inRms;
        const double msOut = static_cast<double>(outRms) * outRms;
        if (!primed)
        {
            meanSquareIn = msIn;
            meanSquareOut = msOut;
            primed = true;
        }
        else
        {
            constexpr double windowSeconds = 2.0;
            const double blockSeconds = static_cast<double>(numSamples) / sampleRate;
            const double alpha = 1.0 - std::exp(-blockSeconds / windowSeconds);
            meanSquareIn += alpha * (msIn - meanSquareIn);
            meanSquareOut += alpha * (msOut - meanSquareOut);
        }

        if (meanSquareIn <= 0.0 || meanSquareOut <= 0.0)
            return currentGain;

        const float correctionDb = static_cast<float>(10.0 * std::log10(meanSquareIn / meanSquareOut));
        const float limited = std::clamp(correctionDb, -12.0f, 12.0f);
        currentGain = std::pow(10.0f, limited * 0.05f);
        return currentGain;
    }

private:
    double sampleRate = 44100.0;
    double meanSquareIn = 0.0;
    double meanSquareOut = 0.0;
    bool primed = false;
    float currentGain = 1.0f;
};

} // namespace duskaudio

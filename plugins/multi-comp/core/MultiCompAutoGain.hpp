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
    // The most the correction moves the output either way. Heavy compression takes 20 dB and more off the level, all
    // of which the makeup has to give back.
    static constexpr float kMaxCorrectionDb = 24.0f;
    // The estimator's window once it has measured long enough.
    static constexpr double kWindowSeconds = 2.0;
    // After a reset the window is the time measured since, divided by this, until it reaches kWindowSeconds. The
    // level from before the compressor settled then weighs (t0 / t) squared of the estimate, where a fixed two-second
    // window kept it for ten seconds and more under heavy reduction.
    static constexpr double kWindowGrowth = 2.0;
    // The first signal after a reset is the compressor catching hold (output at the input's level), not the level to
    // match, so it is not measured.
    static constexpr double kOnsetSkipSeconds = 0.05;

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
        skippedSeconds = 0.0;
        measuredSeconds = 0.0;
    }

    float update(float inRms, float outRms, int numSamples) noexcept
    {
        // Only measured blocks count towards the skip and the window, so silence after a reset leaves both waiting
        // for the signal.
        if (numSamples <= 0 || inRms < 1.0e-5f || outRms < 1.0e-5f)
            return currentGain;

        const double blockSeconds = static_cast<double>(numSamples) / sampleRate;
        if (skippedSeconds < kOnsetSkipSeconds)
        {
            skippedSeconds += blockSeconds;
            return currentGain;
        }
        measuredSeconds = std::min(measuredSeconds + blockSeconds, kWindowGrowth * kWindowSeconds);

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
            const double windowSeconds = std::min(measuredSeconds / kWindowGrowth, kWindowSeconds);
            const double alpha = 1.0 - std::exp(-blockSeconds / windowSeconds);
            meanSquareIn += alpha * (msIn - meanSquareIn);
            meanSquareOut += alpha * (msOut - meanSquareOut);
        }

        if (meanSquareIn <= 0.0 || meanSquareOut <= 0.0)
            return currentGain;

        const float correctionDb = static_cast<float>(10.0 * std::log10(meanSquareIn / meanSquareOut));
        const float limited = std::clamp(correctionDb, -kMaxCorrectionDb, kMaxCorrectionDb);
        currentGain = std::pow(10.0f, limited * 0.05f);
        return currentGain;
    }

private:
    double sampleRate = 44100.0;
    double meanSquareIn = 0.0;
    double meanSquareOut = 0.0;
    bool primed = false;
    float currentGain = 1.0f;
    double skippedSeconds = 0.0;
    double measuredSeconds = 0.0;
};

} // namespace duskaudio

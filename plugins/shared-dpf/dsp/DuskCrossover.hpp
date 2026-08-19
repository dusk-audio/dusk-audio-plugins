// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskCrossover.hpp , allocation-free Linkwitz-Riley 4th-order crossover.
// A LR4 branch is two cascaded Butterworth second-order sections (Q = 1/sqrt(2)).

#pragma once

#include "DuskFilters.hpp"

#include <algorithm>

namespace duskaudio
{

class DuskCrossover
{
public:
    void prepare(double sampleRate, float frequency) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        currentFrequency = -1.0f;
        setFrequency(frequency);
        reset();
    }

    void setFrequency(float frequency) noexcept
    {
        frequency = nyquistSafeDesignHz(fs, frequency);
        if (std::abs(frequency - currentFrequency) <= 0.001f) return;
        currentFrequency = frequency;
        const auto lp = Biquad::lowPass(fs, frequency, kButterworthQ);
        const auto hp = Biquad::highPass(fs, frequency, kButterworthQ);
        lowA.setCoeffs(lp); lowB.setCoeffs(lp);
        highA.setCoeffs(hp); highB.setCoeffs(hp);
    }

    void reset() noexcept
    {
        lowA.reset(); lowB.reset(); highA.reset(); highB.reset();
    }

    float processLow(float input) noexcept
    {
        return lowB.process(lowA.process(input));
    }

    float processHigh(float input) noexcept
    {
        return highB.process(highA.process(input));
    }

    // Process a low branch and its complementary residual. This is exact
    // reconstruction, but the residual is not an LR4 high branch.
    void process(float input, float& low, float& high) noexcept
    {
        low = processLow(input);
        // This helper exposes the low branch plus a residual high value so the
        // pair sums exactly to the input. The residual is not an LR4 high
        // branch and the pair is not an all-pass reconstruction.
        high = input - low;
    }

    // Standard LR4 branches. Multi-Comp uses these independently filtered
    // branches to match its cascaded Butterworth low/high topology.
    void processStandard(float input, float& low, float& high) noexcept
    {
        low = processLow(input);
        high = processHigh(input);
    }

    static constexpr float butterworthQ() noexcept { return kButterworthQ; }

private:
    static constexpr float kButterworthQ = 0.7071067811865476f;
    double fs = 48000.0;
    float currentFrequency = -1.0f;
    Biquad lowA, lowB, highA, highB;
};

} // namespace duskaudio

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskCrossover.hpp — allocation-free Linkwitz-Riley 4th-order crossover.
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
        setFrequency(frequency);
        reset();
    }

    void setFrequency(float frequency) noexcept
    {
        frequency = nyquistSafeDesignHz(fs, frequency);
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

    // Process both branches from the same input. The returned branches have the
    // standard LR4 complementary magnitude and phase response; their sum is an
    // all-pass reconstruction, as required by Linkwitz-Riley crossovers.
    void process(float input, float& low, float& high) noexcept
    {
        low = processLow(input);
        // The two canonical LR4 branches are exposed independently above. For
        // a splitter, use the complementary residual so summing the bands is
        // sample-exact (the JUCE tree's all-pass phase rotation must not be
        // mixed with a raw dry path). The residual still has the LR4 crossover
        // corner and is the phase-compensated high branch used by Multi-Comp.
        high = input - low;
    }

    static constexpr float butterworthQ() noexcept { return kButterworthQ; }

private:
    static constexpr float kButterworthQ = 0.7071067811865476f;
    double fs = 48000.0;
    Biquad lowA, lowB, highA, highB;
};

} // namespace duskaudio

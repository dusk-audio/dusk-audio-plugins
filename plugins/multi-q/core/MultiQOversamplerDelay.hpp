// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// MultiQOversamplerDelay.hpp — the delay a DuskOversampler round trip renders.
//
// DuskOversampler's latency() is the sum of its up and down FIR delays. The
// rendered delay is shorter by the decimation phase: 62.5 base samples rather
// than 63 for the 127-tap 2x stage, 65.75 rather than 66.5 at 4x. Multi-Q 2
// reports the rendered delay, so hosts line its output up to the sample.

#pragma once

#include <cmath>

namespace duskaudio
{

// Base-rate delay of an oversampler's up/down round trip at `factor` with an
// identity stage in between: the group delay at DC of its impulse response,
// sum(n * h[n]) / sum(h[n]). The halfbands are linear phase, so this is the
// delay at every frequency. It lies on a quarter-sample grid; the result is
// snapped to 1/64 sample so float noise cannot flip how a caller rounds a sum
// that lands on a half sample. Allocation-free, but it renders 512 samples:
// call it from prepare(), not per block.
template <class Oversampler>
double measureOversamplerDelay(int factor) noexcept
{
    if (factor <= 1)
        return 0.0;
    Oversampler os;
    os.setFactor(factor);
    os.reset();
    double sum = 0.0, moment = 0.0;
    for (int n = 0; n < 512; ++n)
    {
        const double h = os.processSample(n == 0 ? 1.0f : 0.0f, [](float s) noexcept { return s; });
        sum += h;
        moment += n * h;
    }
    return sum != 0.0 ? std::round(moment / sum * 64.0) / 64.0 : 0.0;
}

} // namespace duskaudio

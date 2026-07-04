// DuskSmoothed.hpp — one-pole exponential parameter smoother.
//
// Framework-free replacement for juce::LinearSmoothedValue (this one is
// exponential, not linear). Lifted from plugins/tape-echo/core/TapeEchoDSP.hpp.

#pragma once

#include <cmath>

namespace duskaudio
{

class SmoothedValue
{
public:
    // tauSeconds: time constant of the one-pole. Larger = slower glide.
    void prepare(double sampleRate, float tauSeconds) noexcept
    {
        coeff = 1.0f - std::exp(-1.0f / (float)(tauSeconds * sampleRate));
    }
    void  snap(float v) noexcept      { current = target = v; }
    void  setTarget(float v) noexcept { target = v; }
    float next() noexcept             { current += coeff * (target - current); return current; }
    float value() const noexcept      { return current; }

private:
    float current = 0.0f, target = 0.0f, coeff = 1.0f;
};

} // namespace duskaudio

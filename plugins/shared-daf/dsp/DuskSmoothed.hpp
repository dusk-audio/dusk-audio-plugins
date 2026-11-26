// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskSmoothed.hpp — one-pole exponential parameter smoother.
//
// Framework-free replacement for juce::LinearSmoothedValue (this one is
// exponential, not linear). Lifted from plugins/tape-echo/core/TapeEchoDSP.hpp.

#pragma once

#include <cmath>
#include <cstring>

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
    // True when next() would leave the value's bits unchanged: the step rounds
    // away and the value sits on its fixed point, so until the target moves
    // every next() returns value(). It runs next() on a copy and compares the
    // stored bits: == is not enough, since next() turns a -0 at a zero target
    // into +0, and under denormals-are-zero (ScopedFlushDenormals on x86 and
    // arm64) a subnormal value compares equal to the +0 next() turns it into.
    // A NaN value is never at rest.
    bool  atRest() const noexcept
    {
        SmoothedValue probe = *this;
        const float after = probe.next();
        return !std::isnan(after) && std::memcmp(&after, &current, sizeof(float)) == 0;
    }
    float targetValue() const noexcept { return target; }

private:
    float current = 0.0f, target = 0.0f, coeff = 1.0f;
};

} // namespace duskaudio

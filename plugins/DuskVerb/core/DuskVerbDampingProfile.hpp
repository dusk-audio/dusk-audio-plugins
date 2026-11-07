// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// The damping card's decay profile as pure arithmetic: what the five knobs (Bass, Mid and
// Treble Multiply, the Low and High Crossover) ask of a tail at each frequency. The damping
// scaler (DuskVerbDampingScaler.hpp) rescales the engines' decay tables by it, and the editor's
// TONE graph draws it, so the picture and the sound share one formula. No engine dependency.

#pragma once

#include <algorithm>
#include <cmath>

namespace duskverb
{

// The damping card in DSP units: three decay multipliers, then the two crossovers in Hz.
struct DampingKnobs
{
    float bass = 1.0f, mid = 1.0f, treble = 1.0f, lowX = 500.0f, highX = 4000.0f;

    bool operator== (const DampingKnobs& o) const noexcept
    {
        return bass == o.bass && mid == o.mid && treble == o.treble && lowX == o.lowX && highX == o.highX;
    }
    bool operator!= (const DampingKnobs& o) const noexcept { return ! (*this == o); }
};

// log M(f): the three-band decay-multiplier profile, a one-octave crossfade at each crossover.
inline float logDecayProfile (const DampingKnobs& k, float hz) noexcept
{
    const float wL = std::clamp (0.5f + std::log2 (hz / k.lowX),  0.0f, 1.0f);
    const float wH = std::clamp (0.5f + std::log2 (hz / k.highX), 0.0f, 1.0f);
    return (1.0f - wL) * std::log (k.bass)
         + wL * ((1.0f - wH) * std::log (k.mid) + wH * std::log (k.treble));
}

// r(f) = M(f; current) / M(f; stored): the factor a decay voiced at `stored` takes at
// `current`. Exactly 1 when the two are equal, and at any frequency no changed knob reaches.
inline float decayRatio (const DampingKnobs& current, const DampingKnobs& stored, float hz) noexcept
{
    if (current == stored)
        return 1.0f;
    return std::exp (logDecayProfile (current, hz) - logDecayProfile (stored, hz));
}

} // namespace duskverb

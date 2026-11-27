// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

#include <cmath>

namespace duskverb
{
// Each tank sample supplies two equal input phases (a zero-order hold).
// Average the two filtered phases to decimate. The two-tap FIRs have unity
// DC gain and a zero at the internal Nyquist. A unity feedback path therefore
// acquires no extra delay, including when Freeze removes all damping.
inline float tankPole2x (float cutoff, float hostRate) noexcept
{
    return -std::expm1 (-3.14159265359f * cutoff / hostRate);
}

// The factory voices retain a shelf above the output damping corner, measured
// at 48 kHz. For internal pole p that shelf needs (1-p)/(1+p) feedthrough
// alongside the two-phase low-pass. Anchor the voicing to that measurement
// rate so a different host rate does not move the shelf.
inline float tankOutputFeedthrough (float cutoff) noexcept
{
    const float coefficient = tankPole2x (cutoff, 48000.0f);
    return coefficient / (2.0f - coefficient);
}

inline float lowpassPhase (float input, float coefficient, float& state) noexcept
{
    state += coefficient * (input - state);
    return state;
}

}

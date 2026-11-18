// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// The Tone macro (-1..+1, 0 = the preset as voiced) as one definition. The DSP folds it into
// Treble Multiply and Hi Cut with these functions, and the editor's TONE graph draws the
// effective values with the same ones, so the picture cannot drift from the sound.

#pragma once

#include <algorithm>
#include <cmath>

namespace duskverb
{

// Treble Multiply factor: -1 -> 0.5x, 0 -> 1x, +1 -> 2x.
inline float toneTrebleFactor (float tone) noexcept { return std::pow (2.0f, tone); }

// Hi Cut factor, a gentler tilt of the wet low-pass: -1 -> 0.66x, +1 -> 1.52x.
inline float toneHiCutFactor (float tone) noexcept { return std::pow (2.0f, tone * 0.6f); }

// The Hi Cut every output stage is given: the knob scaled by the tilt, inside the filter's range.
inline float effectiveHiCut (float hiCutHz, float toneHiCut) noexcept
{
    return std::clamp (hiCutHz * toneHiCut, 200.0f, 20000.0f);
}

} // namespace duskverb

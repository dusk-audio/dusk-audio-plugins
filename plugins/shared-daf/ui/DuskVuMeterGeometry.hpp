// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Pure geometry used by the shared analogue VU widget and its logic tests.

#pragma once

#include <cmath>

namespace duskdaf
{

struct VuScreenOffset
{
    float x;
    float y;
};

inline VuScreenOffset vuScreenOffset(float designRadius, float angle,
                                     float panelScale) noexcept
{
    // The radius arrives in design coordinates. Convert it to screen pixels
    // exactly once; the caller adds this offset to an already-scaled pivot.
    const float screenRadius = designRadius * panelScale;
    return {screenRadius * std::cos(angle), screenRadius * std::sin(angle)};
}

} // namespace duskdaf

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <algorithm>
#include <cmath>

namespace multicompp::ui_detail
{
// A VU movement is linear in voltage, not decibels. Keep the printed scale
// and the GR needle on the same law; unity belongs at 0 VU, before the red +3.
inline float vintageVuDeflection(float db) noexcept
{
    if (!std::isfinite(db)) db = 0.0f;
    return std::clamp(std::pow(10.0f, (db - 3.0f) * 0.05f), 0.0f, 1.0f);
}

inline float vintageVuNeedleAngle(float db) noexcept
{
    return (-55.0f + 110.0f * vintageVuDeflection(db)) * 0.0174532925199433f;
}
}

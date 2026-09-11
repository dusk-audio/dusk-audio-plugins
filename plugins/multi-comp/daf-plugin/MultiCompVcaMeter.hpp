// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "DuskVuMeter.hpp"
#include <array>

namespace multicompp::ui_detail
{
inline duskdaf::VuScaleConfig vcaMeterScale(const char* sourceLabel)
{
    static constexpr std::array<duskdaf::VuTick, 13> ticks{{
        {-40.0f, "-40", true}, {-35.0f, nullptr, false},
        {-30.0f, "-30", true}, {-25.0f, nullptr, false},
        {-20.0f, "-20", true}, {-15.0f, nullptr, false},
        {-10.0f, "-10", true}, {-5.0f, nullptr, false},
        {0.0f, "0", true}, {5.0f, nullptr, false},
        {10.0f, "+10", true}, {15.0f, nullptr, false},
        {20.0f, "+20", true}}};
    duskdaf::VuScaleConfig scale;
    scale.ticks = ticks.data();
    scale.tickCount = static_cast<int>(ticks.size());
    scale.minDb = -40.0f;
    scale.maxDb = 20.0f;
    scale.redFromDb = 100.0f;
    scale.legend = "DECIBELS";
    scale.sublabel = sourceLabel;
    return scale;
}

inline duskdaf::VuStyle vcaMeterStyle()
{
    duskdaf::VuStyle style;
    style.bezelLight = IM_COL32(62, 62, 64, 255);
    style.bezelDark = IM_COL32(26, 26, 28, 255);
    style.bezelLine = IM_COL32(10, 10, 10, 255);
    style.bezelInnerLine = IM_COL32(177, 125, 67, 255);
    style.lip = IM_COL32(14, 14, 16, 255);
    style.faceBase = IM_COL32(244, 214, 155, 255);
    style.faceTopTint = IM_COL32(255, 241, 199, 110);
    style.faceBottomTint = IM_COL32(190, 132, 60, 75);
    style.faceGlow = IM_COL32(255, 250, 218, 150);
    style.lampGlow = IM_COL32(255, 253, 222, 185);
    style.faceEdgeShade = IM_COL32(105, 54, 19, 125);
    style.ink = IM_COL32(48, 156, 218, 255);
    style.hot = style.ink;
    style.minorTickColor = style.ink;
    style.sublabelColor = IM_COL32(120, 96, 60, 255);
    style.needle = IM_COL32(20, 18, 14, 255);
    style.overloadArc = false;
    style.cornerMarks = false;
    style.tickLabelSize = 15.0f;
    style.legendSize = 15.0f;
    style.sublabelSize = 8.0f;
    // A shallow arc and an off-window pivot reproduce the dbx scale. All
    // distances stay in design coordinates until drawVuMeter maps them once.
    style.pivotBelowFace = 126.0f;
    style.sweepHalfAngleDeg = 32.0f;
    style.radiusTopInset = 48.0f;
    style.majorTickLength = 13.0f;
    style.minorTickLength = 7.0f;
    style.tickLabelRadiusFrac = 1.03f;
    style.legendOffset = 0.82f;
    style.scaleArc = true;
    style.needleWidth = 2.0f;
    return style;
}
} // namespace multicompp::ui_detail

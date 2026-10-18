// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "DuskVuMeter.hpp"
#include <array>

namespace multicompp::ui_detail
{
// The VCA face's meter, in mode-canvas design coordinates: the same footprint
// and height on the face as the FET meter (234 x 141 from y 423), between the
// model line above it and the badge below. The UI and the render test both
// draw it from here.
struct VcaMeterRect
{
    static constexpr float x0 = 755.0f, y0 = 423.0f, x1 = 991.0f, y1 = 564.0f;
};

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
    // Lamp light kept below saturation: at full strength the glows washed the
    // label band to pure white and the blue scale read at 3.05:1.
    style.faceTopTint = IM_COL32(255, 241, 199, 50);
    style.faceBottomTint = IM_COL32(190, 132, 60, 75);
    style.faceGlow = IM_COL32(255, 240, 200, 45);
    style.lampGlow = IM_COL32(255, 244, 205, 70);
    style.faceEdgeShade = IM_COL32(105, 54, 19, 125);
    style.ink = IM_COL32(22, 96, 166, 255);
    style.hot = style.ink;
    style.minorTickColor = style.ink;
    style.sublabelColor = IM_COL32(120, 96, 60, 255);
    style.needle = IM_COL32(20, 18, 14, 255);
    style.overloadArc = false;
    style.cornerMarks = false;
    // Lengths below were drawn for a 346 x 188 meter and are scaled by the
    // face-width ratio (222 / 332) to VcaMeterRect; the sublabel and needle
    // match the Opto and FET meters.
    style.tickLabelSize = 10.0f;
    style.legendSize = 10.0f;
    style.sublabelSize = 6.8f;
    // A shallow arc and an off-window pivot reproduce the reference VCA compressor's scale. All
    // distances stay in design coordinates until drawVuMeter maps them once.
    style.pivotBelowFace = 84.3f;
    style.sweepHalfAngleDeg = 32.0f;
    style.radiusTopInset = 32.1f;
    style.majorTickLength = 8.7f;
    style.minorTickLength = 4.7f;
    style.tickLabelRadiusFrac = 1.03f;
    style.legendOffset = 0.82f;
    style.scaleArc = true;
    style.needleWidth = 1.9f;
    return style;
}
} // namespace multicompp::ui_detail

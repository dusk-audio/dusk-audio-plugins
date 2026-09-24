// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "MultiCompVintageMeterGeometry.hpp"
#include "../../shared-daf/ui/DuskVuMeter.hpp"

namespace multicompp::ui_detail
{
inline duskdaf::VuScaleConfig vintageVuScale(const char* sublabel = "GAIN REDUCTION") noexcept
{
    static constexpr duskdaf::VuTick ticks[] = {
        {-20, "-20", true}, {-15, nullptr, false}, {-10, "-10", true},
        {-7, "-7", true}, {-6, nullptr, false}, {-5, "-5", true},
        {-4, nullptr, false}, {-3, "-3", true}, {-2, nullptr, false},
        {-1, "-1", true}, {0, "0", true}, {1, "+1", true},
        {2, "+2", true}, {3, "+3", true}};
    duskdaf::VuScaleConfig cfg;
    cfg.ticks = ticks;
    cfg.tickCount = static_cast<int>(std::size(ticks));
    cfg.redFromDb = 0.1f;
    cfg.legend = "DUSK";
    cfg.sublabel = sublabel;
    cfg.deflection = [](float db, const duskdaf::VuScaleConfig&) { return vintageVuDeflection(db); };
    return cfg;
}

inline duskdaf::VuStyle vintageVuStyle(bool opto) noexcept
{
    duskdaf::VuStyle style;
    style.bezelLight = opto ? IM_COL32(130, 140, 141, 255) : IM_COL32(58, 59, 56, 255);
    style.bezelDark = opto ? IM_COL32(69, 78, 79, 255) : IM_COL32(16, 17, 16, 255);
    style.bezelLine = IM_COL32(36, 40, 39, 255);
    style.lip = IM_COL32(63, 56, 40, 255);
    style.faceBase = IM_COL32(239, 206, 132, 255);
    style.faceTopTint = IM_COL32(255, 232, 169, 195);
    style.faceBottomTint = IM_COL32(210, 155, 65, 110);
    style.faceGlow = IM_COL32(255, 249, 205, 80);
    style.faceEdgeShade = IM_COL32(86, 56, 18, 150);
    style.ink = IM_COL32(47, 43, 31, 255);
    style.hot = IM_COL32(193, 45, 24, 255);
    style.needle = IM_COL32(33, 31, 24, 255);
    style.sublabelColor = IM_COL32(86, 68, 37, 255);
    style.cornerMarks = false;
    style.scaleArc = true;
    style.pivotBelowFace = 24;
    style.sweepHalfAngleDeg = 55;
    style.radiusTopInset = opto ? 21.0f : 30.0f;
    style.tickLabelRadiusFrac = 1.02f;
    style.majorTickLength = -10;
    style.minorTickLength = -6;
    style.tickLabelSize = 8.0f;
    style.legendSize = 13;
    style.sublabelSize = 6.8f;
    style.legendOffset = 0.46f;
    style.needleWidth = 1.9f;
    return style;
}

inline void drawVintageMeter(duskdaf::DuskPanel& panel, ImDrawList* dl,
                                          float x, float y, float w, float h,
                                          float db, bool opto,
                                          const char* sublabel = "GAIN REDUCTION", bool powered = true)
{
    const float s = panel.scale();
    dl->AddRectFilled(panel.P(x + 3, y + 4), panel.P(x + w + 3, y + h + 4),
                       IM_COL32(0, 0, 0, 95), 2 * s);
    const float frame = opto ? 10.0f : 3.0f;
    auto style = vintageVuStyle(opto);
    if (!powered)
    {
        style.faceBase = IM_COL32(167, 155, 113, 255);
        style.faceTopTint = IM_COL32(191, 181, 141, 195);
        style.faceBottomTint = IM_COL32(135, 121, 84, 110);
        style.faceGlow = 0;
        db = -120.0f;
    }
    dl->AddRectFilledMultiColor(panel.P(x, y), panel.P(x + w, y + h),
                               style.bezelLight, style.bezelDark, style.bezelDark, style.bezelLight);
    dl->AddLine(panel.P(x + 1, y + 1), panel.P(x + w - 1, y + 1),
                 IM_COL32(188, 192, 184, 160), s);
    const float x0 = x + frame, y0 = y + frame, x1 = x + w - frame, y1 = y + h - frame;
    const auto cfg = vintageVuScale(sublabel);
    // Initialise to this frame's DSP value: the shared renderer then adds no
    // extra envelope/return delay to the measured Opto or FET gain reduction.
    float needle = vintageVuDeflection(db);
    duskdaf::drawVuMeter(panel, dl, x0, y0, x1, y1, db, needle, cfg,
                         IM_COL32(140, 141, 123, 150), style);

    const float fx0 = x0 + 7, fx1 = x1 - 7, fy0 = y0 + 7, fy1 = y1 - 7;
    const float cx = (fx0 + fx1) * 0.5f, py = fy1 + style.pivotBelowFace;
    const float half = style.sweepHalfAngleDeg * 0.0174532925199433f;
    const float radius = std::min(py - fy0 - style.radiusTopInset,
                                  (fx1 - fx0) * 0.5f / std::sin(half) - 6.0f);
    dl->PushClipRect(panel.P(fx0, fy0), panel.P(fx1, fy1), true);
    // The second printed scale is percentage of the 0 VU reference voltage.
    for (int percent : {20, 40, 60, 80, 100})
    {
        const float db = 20.0f * std::log10(percent * 0.01f);
        const float angle = vintageVuNeedleAngle(db);
        char label[8]; std::snprintf(label, sizeof(label), "%d", percent);
        panel.text(dl, cx + std::sin(angle) * radius * 0.71f,
                    py - std::cos(angle) * radius * 0.71f - 4,
                    6.5f, style.ink, label, 0);
    }
    panel.text(dl, fx0 + 7, fy0 + 8, 9.0f, style.ink, "VU", -1);
    panel.text(dl, fx1 - 7, fy0 + 8, 9.0f, style.hot, "VU", 1);
    dl->PopClipRect();
}
}

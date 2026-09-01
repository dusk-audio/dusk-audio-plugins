// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVuMeter.hpp — shared analogue needle meter for Dusk DAF UIs.
//
// Extracted from the TapeMachine 2 VU (plugins/TapeMachine/daf-plugin/
// TapeMachineUI.cpp drawVU) so every port stops hand-rolling its own needle
// meter: warm tan bezel, aged-cream face, tick ring with dark-below/red-above
// numerals, red overload arc, black needle with mound pivot and glass
// highlight. The scale is data, not code: a VuScaleConfig names the ticks,
// the deflection endpoints, where the red zone starts, and (optionally) a
// non-linear dB→deflection mapping, so the same widget draws a broadcast VU,
// a −40..+20 dB level scale, or a gain-change scale.
//
// The caller owns the needle state (a 0..1 deflection float) and the meaning
// of `valueDb`. The widget applies only a fast cosmetic anti-jitter pole
// (tau ≈ 25 ms) — any real meter ballistic belongs in the DSP feeding it, per
// the TapeMachine precedent.
//
// Requires imgui.h already included by the translation unit, and a
// duskdaf::DuskPanel for coordinate mapping / fonts (DuskImGuiWidgets.hpp).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "DuskImGuiWidgets.hpp"

namespace duskdaf
{

struct VuTick
{
    float db;
    const char* label;  // nullptr = tick only, no numeral
    bool major;
};

struct VuScaleConfig
{
    const VuTick* ticks = nullptr;
    int tickCount = 0;
    float minDb = -20.0f;         // deflection domain: full-left
    float maxDb = 3.0f;           // full-right
    float redFromDb = 0.0f;       // red numerals + overload arc from here up;
                                  // set above maxDb to disable the red zone
    const char* legend = "VU";
    const char* sublabel = nullptr;
    // 0..1 deflection for a dB value. Default is linear in dB across
    // [minDb, maxDb]; a broadcast VU wants the classic log-spread instead.
    float (*deflection)(float db, const VuScaleConfig& cfg) = nullptr;
};

// Colours and sizes. Defaults reproduce the TapeMachine face (tan bezel,
// cream face, dark ink, red overload zone); a panel matching a differently
// lit reference window overrides only what differs.
struct VuStyle
{
    ImU32 bezelLight = IM_COL32(170, 142, 100, 255);
    ImU32 bezelDark = IM_COL32(110, 88, 54, 255);
    ImU32 bezelLine = IM_COL32(92, 72, 44, 255);
    ImU32 lip = IM_COL32(56, 44, 30, 255);
    ImU32 faceBase = IM_COL32(240, 231, 205, 255);
    ImU32 faceTopTint = IM_COL32(252, 246, 226, 130);
    ImU32 faceBottomTint = IM_COL32(210, 194, 158, 150);
    ImU32 ink = IM_COL32(38, 32, 24, 255);        // ticks/numerals below the red zone
    ImU32 hot = IM_COL32(196, 42, 34, 255);       // ticks/numerals/arc in the red zone
    ImU32 plusMark = IM_COL32(196, 42, 34, 255);  // the "+" corner mark
    ImU32 sublabelColor = IM_COL32(90, 80, 64, 255);
    ImU32 needle = IM_COL32(28, 24, 18, 255);
    bool overloadArc = true;
    bool cornerMarks = true;
    float tickLabelSize = 10.0f;
    float legendSize = 11.0f;
    float sublabelSize = 9.0f;
    // Geometry. The defaults reproduce the TapeMachine face: the pivot sits
    // 4 design px above the face bottom and the needle sweeps +-64.7 degrees.
    // A meter like the dbx 160's has its pivot well below the visible face
    // and a flatter arc; both are expressed here so the widget stays shared.
    float pivotBelowFace = -4.0f;     // design px below the face's bottom edge (negative = above)
    float sweepHalfAngleDeg = 64.7f;
    float majorTickLength = 6.0f;
    float minorTickLength = 4.0f;
    ImU32 minorTickColor = 0;         // 0 = same as ink
    ImU32 bezelInnerLine = 0;         // 0 = none; a lighter bevel line inside the bezel
    ImU32 faceGlow = 0;               // 0 = none; a soft lit band across the top of the face
    float legendOffset = 0.46f;       // legend centre as a fraction of the radius above the pivot
};

inline float vuLinearDeflection(float db, const VuScaleConfig& cfg) noexcept
{
    const float span = cfg.maxDb - cfg.minDb;
    const float t = span > 0.0f ? (db - cfg.minDb) / span : 0.0f;
    return std::clamp(t, 0.0f, 1.0f);
}

// The classic VU law: deflection linear in signal LEVEL, not dB, referenced so
// cfg.maxDb lands at full scale. Marks bunch at the bottom and open up near
// the top — the TapeMachine face uses this with maxDb = +3.
inline float vuBroadcastDeflection(float db, const VuScaleConfig& cfg) noexcept
{
    const float t = std::pow(10.0f, (db - cfg.maxDb) / 20.0f);
    return std::clamp(t, 0.0f, 1.0f);
}

inline float vuDeflection(float db, const VuScaleConfig& cfg) noexcept
{
    return cfg.deflection != nullptr ? cfg.deflection(db, cfg)
                                     : vuLinearDeflection(db, cfg);
}

// Needle sweep of the default style (TapeMachine face): +-64.7 degrees about
// straight up. Kept for callers that position things on that arc.
inline constexpr float kVuAngle0 = -2.70f;
inline constexpr float kVuAngle1 = -0.44f;

inline void drawVuMeter(DuskPanel& panel, ImDrawList* dl,
                        float x0, float y0, float x1, float y1,
                        float valueDb, float& needle01,
                        const VuScaleConfig& cfg, ImU32 accent,
                        const VuStyle& style = VuStyle{})
{
    const float s = panel.scale();
    // A non-finite reading (a meter bridge that is not connected yet, a NaN
    // from a broken render) must not poison the persistent needle state; the
    // needle simply holds where it was.
    if (!std::isfinite(needle01)) needle01 = 0.0f;
    if (std::isfinite(valueDb))
    {
        const float target = vuDeflection(valueDb, cfg);
        // Cosmetic anti-jitter / frame-interpolation pole only (tau ~= 25 ms)
        // so the widget never adds a second, slower time constant to a DSP
        // ballistic.
        needle01 += (target - needle01)
            * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 40.0f));
        needle01 = std::clamp(needle01, 0.0f, 1.0f);
    }

    // bezel -> dark inner lip -> face
    dl->AddRectFilledMultiColor(panel.P(x0, y0), panel.P(x1, y1),
        style.bezelLight, style.bezelLight, style.bezelDark, style.bezelDark);
    dl->AddRect(panel.P(x0, y0), panel.P(x1, y1), style.bezelLine, 6.0f * s, 0, 1.4f * s);
    dl->AddLine(panel.P(x0 + 6, y0 + 3), panel.P(x1 - 6, y0 + 3), accent, 1.6f * s);
    dl->AddRectFilled(panel.P(x0 + 4, y0 + 4), panel.P(x1 - 4, y1 - 4), style.lip, 4.0f * s);
    const float fx0 = x0 + 7, fy0 = y0 + 7, fx1 = x1 - 7, fy1 = y1 - 7;
    dl->AddRectFilled(panel.P(fx0, fy0), panel.P(fx1, fy1), style.faceBase, 3.0f * s);
    dl->AddRectFilledMultiColor(panel.P(fx0, fy0), panel.P(fx1, fy1),
        style.faceTopTint, style.faceTopTint, style.faceBottomTint, style.faceBottomTint);

    if (style.bezelInnerLine != 0)
        dl->AddRect(panel.P(fx0 - 1, fy0 - 1), panel.P(fx1 + 1, fy1 + 1), style.bezelInnerLine,
                    3.0f * s, 0, 1.2f * s);
    if (style.faceGlow != 0)
        dl->AddRectFilledMultiColor(panel.P(fx0 + 2, fy0 + 2), panel.P(fx1 - 2, fy0 + (fy1 - fy0) * 0.45f),
                                    style.faceGlow, style.faceGlow,
                                    style.faceGlow & 0x00FFFFFF, style.faceGlow & 0x00FFFFFF);
    // Everything inside the face is clipped to it, so a low pivot can sit
    // below the visible window as it does on the reference.
    dl->PushClipRect(panel.P(fx0, fy0), panel.P(fx1, fy1), true);

    const float cx = 0.5f * (fx0 + fx1);
    const float pivotY = fy1 + style.pivotBelowFace;
    const float half = style.sweepHalfAngleDeg * 3.14159265f / 180.0f;
    const float angle0 = -1.5707963f - half, angle1 = -1.5707963f + half;
    // Radius: the arc must clear the top of the face at its highest point
    // (directly above the pivot) and its ends must stay inside the sides.
    const float radius = std::min((pivotY - fy0 - 12.0f), (fx1 - fx0) * 0.5f / std::max(std::sin(half), 0.2f) - 6.0f);
    const auto pt = [&](float r, float a) {
        return panel.P(cx + r * std::cos(a), pivotY + r * std::sin(a));
    };
    const ImU32 ink = style.ink;
    const ImU32 red = style.hot;
    const auto angleFor = [&](float db) {
        return angle0 + vuDeflection(db, cfg) * (angle1 - angle0);
    };

    // bold red arc across the overload zone
    if (style.overloadArc && cfg.redFromDb <= cfg.maxDb)
    {
        dl->PathClear();
        dl->PathArcTo(panel.P(cx, pivotY), radius * 0.90f * s,
                      angleFor(cfg.redFromDb), angle1, 26);
        dl->PathStroke(red, 0, 3.4f * s);
    }

    for (int i = 0; i < cfg.tickCount; ++i)
    {
        const VuTick& tick = cfg.ticks[i];
        const float a = angleFor(tick.db);
        const bool rz = tick.db >= cfg.redFromDb;
        const float rt = radius * 0.90f;
        const ImU32 minorInk = style.minorTickColor != 0 ? style.minorTickColor : ink;
        dl->AddLine(pt(rt * s, a), pt((rt + (tick.major ? style.majorTickLength : style.minorTickLength)) * s, a),
                    rz ? red : (tick.major ? ink : minorInk), (tick.major ? 1.6f : 1.0f) * s);
        if (tick.label != nullptr)
        {
            const float px = style.tickLabelSize * s;
            ImFont* nf = panel.pickFont(px);
            const ImVec2 ts = nf->CalcTextSizeA(px, FLT_MAX, 0, tick.label);
            const ImVec2 tp = pt(radius * 0.76f * s, a);
            dl->AddText(nf, px, ImVec2(tp.x - ts.x * 0.5f, tp.y - ts.y * 0.5f),
                        rz ? red : ink, tick.label);
        }
    }

    // - (left) and + (right) corner marks
    if (style.cornerMarks)
    {
        panel.text(dl, fx0 + 10, fy0 + 3, 15.0f, ink, "-", -1, true);
        panel.text(dl, fx1 - 10, fy0 + 3, 15.0f, style.plusMark, "+", 1, true);
    }

    if (cfg.legend != nullptr)
        panel.text(dl, cx, pivotY - radius * style.legendOffset, style.legendSize, ink, cfg.legend, 0, true);
    if (cfg.sublabel != nullptr)
        panel.text(dl, cx, pivotY - radius * style.legendOffset + style.legendSize + 3.0f,
                   style.sublabelSize, style.sublabelColor, cfg.sublabel, 0, true);

    // black needle with soft shadow + mound pivot
    const float na = angle0 + needle01 * (angle1 - angle0);
    dl->AddLine(panel.P(cx + 2, pivotY + 1), pt(radius * 0.95f * s, na),
                IM_COL32(60, 50, 36, 70), 4.0f * s);
    {
        const float perp = na + 1.5707963f, bw = 3.4f;
        dl->AddTriangleFilled(
            panel.P(cx + bw * 0.5f * std::cos(perp), pivotY + bw * 0.5f * std::sin(perp)),
            pt(radius * 0.95f * s, na),
            panel.P(cx - bw * 0.5f * std::cos(perp), pivotY - bw * 0.5f * std::sin(perp)),
            style.needle);
    }
    dl->AddCircleFilled(panel.P(cx, pivotY + 1), 8.0f * s, IM_COL32(40, 32, 22, 90), 20);
    dl->AddCircleFilled(panel.P(cx, pivotY), 4.6f * s, IM_COL32(24, 20, 14, 255), 18);
    dl->AddCircleFilled(panel.P(cx - 1.2f, pivotY - 1.4f), 1.5f * s,
                        IM_COL32(150, 140, 120, 150), 10);

    // glass top highlight
    dl->AddRectFilledMultiColor(
        panel.P(fx0 + 4, fy0 + 2), panel.P(fx1 - 4, fy0 + (fy1 - fy0) * 0.22f),
        IM_COL32(255, 255, 255, 30), IM_COL32(255, 255, 255, 30),
        IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    dl->PopClipRect();
}

} // namespace duskdaf

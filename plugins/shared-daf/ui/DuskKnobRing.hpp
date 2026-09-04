// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskKnobRing.hpp — the printed ring around a hardware knob.
//
// DuskPanel::knob owns gestures, the read-out and type-entry, but its only ring
// is an unlabelled 11-tick circle. Every hardware faceplate therefore escapes
// with bodyless=true and re-implements the labelled ring: 4K EQ 2 three times,
// Multi-Comp 2 three times (issue #246). The six copies differ in what they
// draw -- a dot or a radial tick, a plain or a letter-spaced label, aligned by
// direction or centred -- but the placement is identical everywhere:
//
//     t -> DuskPanel::knobAngle(t) -> direction (sin a, -cos a)
//     mark between radius+inner and radius+outer, label at radius+labelOffset
//
// So the geometry lives here and the body stays with the plugin, which is the
// split issue #246 asks for.
//
// The value -> t mapping is a callable the caller supplies rather than a
// min/max pair, because that is the part a face gets wrong. A skewed parameter
// whose ring is placed linearly prints marks that do not line up with the
// pointer; passing the parameter's own law makes the printed ring and the
// pointer agree by construction, which is what Multi-Comp's VCA knob already
// did locally.

#pragma once

#include <cmath>

#include "DuskImGuiWidgets.hpp"

namespace duskdaf
{

struct KnobRingStyle
{
    // Marks. A dot sits at markOuter; a tick spans markInner..markOuter. Both
    // are measured outward from the knob radius, in design pixels.
    bool dotMarks = false;
    float markInner = 7.0f;
    float markOuter = 15.0f;
    float markThickness = 1.7f;
    float dotRadius = 1.6f;
    int dotSegments = 8;
    ImU32 markColor = IM_COL32(150, 152, 156, 255);

    // Minor marks: the same shape with its own metrics. minorColor 0 follows
    // markColor.
    float minorInner = 9.0f;
    float minorOuter = 14.0f;
    float minorThickness = 1.0f;
    float minorDotRadius = 0.0f;   // 0 = dotRadius
    ImU32 minorColor = 0;

    // Labels.
    float labelOffset = 20.0f;
    float labelNudgeY = -5.0f;
    float labelSize = 10.5f;
    ImU32 labelColor = IM_COL32(206, 208, 212, 255);
    bool labelBold = true;
    // Centred, or aligned so labels on the left of the dial end at the dial and
    // labels on the right start at it. Hardware panels do both.
    bool labelCentred = false;
    // Letter spacing as a fraction of the pixel size; 0 draws ordinary text.
    float labelTracking = 0.0f;
};

// Letter-spaced text, which ImGui does not do: each glyph placed by its own
// advance plus a tracking gap. align: -1 left, 0 centred, 1 right. Shared
// because the silkscreen lettering on a faceplate needs it wherever it appears,
// not only on a ring.
inline void spacedText(const DuskPanel& panel, ImDrawList* dl, float x, float y,
                       float size, ImU32 col, const char* text, int align,
                       float tracking = 0.16f)
{
    const float s = panel.scale();
    const float px = size * s;
    ImFont* font = panel.pickFont(px);
    const float gap = tracking * px;

    float total = 0.0f;
    for (const char* c = text; *c; ++c)
        total += font->CalcTextSizeA(px, FLT_MAX, 0.0f, c, c + 1).x + (c[1] ? gap : 0.0f);

    ImVec2 pos = panel.P(x, y);
    if (align == 0) pos.x -= 0.5f * total;
    if (align == 1) pos.x -= total;
    pos.x = std::floor(pos.x + 0.5f);
    pos.y = std::floor(pos.y + 0.5f);

    for (const char* c = text; *c; ++c)
    {
        dl->AddText(font, px, pos, col, c, c + 1);
        pos.x += font->CalcTextSizeA(px, FLT_MAX, 0.0f, c, c + 1).x + gap;
    }
}

// One mark, plus its label if it has one. Exposed so a face with a mark that
// does not come from a value list (a centre detent, an INF stop) places it the
// same way as the rest of its ring instead of by hand.
inline void drawKnobRingMark(const DuskPanel& panel, ImDrawList* dl,
                             float cx, float cy, float radius, float t,
                             const char* label, const KnobRingStyle& style,
                             bool minor = false)
{
    const float s = panel.scale();
    const float angle = DuskPanel::knobAngle(t);
    const float dx = std::sin(angle), dy = -std::cos(angle);
    const ImVec2 c = panel.P(cx, cy);
    const float r = radius * s;

    const float inner = minor ? style.minorInner : style.markInner;
    const float outer = minor ? style.minorOuter : style.markOuter;
    const ImU32 color = minor ? (style.minorColor != 0 ? style.minorColor : style.markColor)
                              : style.markColor;

    if (style.dotMarks)
    {
        const float dotR = (minor && style.minorDotRadius > 0.0f) ? style.minorDotRadius
                                                                  : style.dotRadius;
        dl->AddCircleFilled(panel.P(cx + dx * (radius + outer), cy + dy * (radius + outer)),
                            dotR * s, color, style.dotSegments);
    }
    else
    {
        dl->AddLine(ImVec2(c.x + dx * (r + inner * s), c.y + dy * (r + inner * s)),
                    ImVec2(c.x + dx * (r + outer * s), c.y + dy * (r + outer * s)),
                    color, (minor ? style.minorThickness : style.markThickness) * s);
    }

    if (label == nullptr)
        return;

    const float lx = cx + dx * (radius + style.labelOffset);
    const float ly = cy + dy * (radius + style.labelOffset) + style.labelNudgeY;

    if (style.labelTracking > 0.0f)
    {
        spacedText(panel, dl, lx, ly, style.labelSize, style.labelColor, label,
                   style.labelCentred ? 0 : (dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0)),
                   style.labelTracking);
    }
    else
    {
        const int align = style.labelCentred ? 0 : (dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0));
        panel.text(dl, lx, ly, style.labelSize, style.labelColor, label, align, style.labelBold);
    }
}

// The whole ring. `valueToT` maps a printed value to its 0..1 position on the
// dial; pass the parameter's own law so the marks land where the pointer does.
template <typename ValueToT>
inline void drawKnobRing(const DuskPanel& panel, ImDrawList* dl,
                         float cx, float cy, float radius,
                         const float* majorValues, const char* const* majorLabels,
                         int majorCount,
                         const float* minorValues, int minorCount,
                         ValueToT valueToT, const KnobRingStyle& style)
{
    for (int i = 0; i < minorCount; ++i)
        drawKnobRingMark(panel, dl, cx, cy, radius, valueToT(minorValues[i]),
                         nullptr, style, true);

    for (int i = 0; i < majorCount; ++i)
        drawKnobRingMark(panel, dl, cx, cy, radius, valueToT(majorValues[i]),
                         majorLabels != nullptr ? majorLabels[i] : nullptr, style, false);
}

} // namespace duskdaf

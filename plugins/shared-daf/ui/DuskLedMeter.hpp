// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskLedMeter.hpp — the segmented LED level ladder, for DAF/Dear ImGui UIs.
//
// The framework-free counterpart of plugins/shared/LEDMeter.{h,cpp}, which is
// the meter every JUCE plugin in this repository draws. DuskVuMeter.hpp is the
// analogue needle VU and is a different instrument; this is the LED column.
//
// The parts that are not cosmetic, and are therefore transcribed rather than
// re-invented:
//
//   * dB -> lit segment count is (db - minDb) / (maxDb - minDb) scaled by the
//     segment count and truncated, over the same -60..+6 dB window.
//   * the colour zones are FRACTIONS of the ladder, not absolute indices: the
//     JUCE meter is 12 segments with green below 7, yellow 7..9 and red from
//     10, so 7/12 and 10/12 reproduce it at any segment count. A caller that
//     wants a taller rail can raise `segments` and keep the same thresholds.
//   * ballistics are the JUCE meter's symmetric one-pole with a 65 ms time
//     constant, driven here by the real frame delta instead of an assumed
//     refresh rate, so the rise/fall is the same at 30 and at 144 fps.
//   * peak hold: 1.5 s, then a 15 dB/s fall, never below the display level.
//
// Levels come in already in dB (that is what the Dusk DSP meter atomics hold).
// update() is message-thread only.
//
// Requires imgui.h and DuskImGuiWidgets.hpp's DuskPanel (for the design-space
// mapper) to be available to the translation unit.

#pragma once

#include <algorithm>
#include <cmath>

#include "DuskImGuiWidgets.hpp"

namespace duskdaf
{

struct LedLadderStyle
{
    int   segments = 12;          // JUCE LEDMeter uses 12; a taller rail can use more
    float gap      = 2.0f;        // design px between segments
    float minDb    = -60.0f;
    float maxDb    = 6.0f;
    float rounding = 1.5f;

    // Zone starts as a fraction of the ladder — see the header note.
    float yellowFrom = 7.0f / 12.0f;
    float redFrom    = 10.0f / 12.0f;

    float ballisticsTauMs  = 65.0f;
    bool  peakHold         = true;
    float peakHoldSeconds  = 1.5f;
    float peakFallDbPerSec = 15.0f;

    bool  glow = true;            // bloom under a lit segment
};

struct LedLadderColors
{
    ImU32 lit;
    ImU32 unlit;
    ImU32 glow;
};

// Transcribed from LEDMeter::getColorsForSegment.
inline LedLadderColors ledLadderColors(float fractionUpTheLadder,
                                       const LedLadderStyle& style) noexcept
{
    if (fractionUpTheLadder >= style.redFrom)
        return { IM_COL32(0xf8, 0x71, 0x71, 255), IM_COL32(0x2a, 0x0d, 0x0d, 255),
                 IM_COL32(0xef, 0x44, 0x44, 255) };
    if (fractionUpTheLadder >= style.yellowFrom)
        return { IM_COL32(0xfd, 0xe0, 0x47, 255), IM_COL32(0x2a, 0x22, 0x08, 255),
                 IM_COL32(0xea, 0xb3, 0x08, 255) };
    return { IM_COL32(0x4a, 0xde, 0x80, 255), IM_COL32(0x0d, 0x2a, 0x12, 255),
             IM_COL32(0x22, 0xc5, 0x5e, 255) };
}

// One two-channel ladder. Hold the instance next to the UI, feed it every frame.
class LedLadder
{
public:
    // dbL/dbR: current peak level in dB. dtSeconds: this frame's delta.
    void update(float dbL, float dbR, float dtSeconds,
                const LedLadderStyle& style = LedLadderStyle()) noexcept
    {
        // A paused or first frame must not be treated as an instant jump, and a
        // host that stalls the editor for a second must not fast-forward the
        // ballistics past the peak hold in one step.
        dtSeconds = std::clamp(dtSeconds, 0.0f, 0.1f);
        const float coeff = 1.0f - std::exp(-(dtSeconds * 1000.0f)
                                            / std::max(1.0f, style.ballisticsTauMs));
        const float in[2] = { dbL, dbR };
        for (int ch = 0; ch < 2; ++ch)
        {
            const float current = std::clamp(std::isfinite(in[ch]) ? in[ch] : style.minDb,
                                             style.minDb, style.maxDb);
            display_[ch] += coeff * (current - display_[ch]);
            display_[ch] = std::clamp(display_[ch], style.minDb, style.maxDb);

            if (!style.peakHold)
            {
                peak_[ch] = display_[ch];
                continue;
            }
            if (current > peak_[ch])
            {
                peak_[ch] = current;
                hold_[ch] = style.peakHoldSeconds;
            }
            else if (hold_[ch] > 0.0f)
            {
                hold_[ch] -= dtSeconds;
            }
            else
            {
                peak_[ch] = std::max(peak_[ch] - style.peakFallDbPerSec * dtSeconds,
                                     display_[ch]);
            }
        }
    }

    void reset(float db = -100.0f) noexcept
    {
        for (int ch = 0; ch < 2; ++ch) { display_[ch] = peak_[ch] = db; hold_[ch] = 0.0f; }
    }

    float displayDb(int channel) const noexcept { return display_[channel & 1]; }
    float peakDb(int channel) const noexcept    { return peak_[channel & 1]; }
    float peakDb() const noexcept               { return std::max(peak_[0], peak_[1]); }

    // Draws two columns filling the design-space rect. Nothing else: the caller
    // owns the surrounding card, the title and the numeric read-out, because
    // those differ per plugin.
    void draw(const DuskPanel& panel, ImDrawList* dl,
              float x0, float y0, float x1, float y1,
              const LedLadderStyle& style = LedLadderStyle()) const
    {
        const int n = std::max(1, style.segments);
        const float s = panel.scale();
        const float columnGap = 2.0f;
        const float columnW = 0.5f * ((x1 - x0) - columnGap);
        if (columnW <= 0.0f || y1 <= y0)
            return;

        const float segH = ((y1 - y0) - (float)(n - 1) * style.gap) / (float)n;
        if (segH <= 0.0f)
            return;

        const float span = std::max(1.0e-3f, style.maxDb - style.minDb);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float cx0 = x0 + (float)ch * (columnW + columnGap);
            const float cx1 = cx0 + columnW;

            const float norm = std::clamp((display_[ch] - style.minDb) / span, 0.0f, 1.0f);
            const int lit = (int)(norm * (float)n);
            const float peakNorm = std::clamp((peak_[ch] - style.minDb) / span, 0.0f, 1.0f);
            const int peakSeg = (int)(peakNorm * (float)n) - 1;

            for (int i = 0; i < n; ++i)
            {
                const float top = y1 - (float)(i + 1) * (segH + style.gap) + style.gap;
                const ImVec2 a = panel.P(cx0, top);
                const ImVec2 b = panel.P(cx1, top + segH);
                const LedLadderColors c =
                    ledLadderColors((float)i / (float)n, style);

                const bool isLit  = i < lit;
                const bool isPeak = style.peakHold && i == peakSeg && !isLit;

                if (isLit)
                {
                    if (style.glow)
                        dl->AddRectFilled(ImVec2(a.x - 2.0f * s, a.y - 1.5f * s),
                                          ImVec2(b.x + 2.0f * s, b.y + 1.5f * s),
                                          (c.glow & 0x00ffffffu) | (60u << 24),
                                          (style.rounding + 1.5f) * s);
                    dl->AddRectFilled(a, b, c.lit, style.rounding * s);
                    dl->AddRectFilled(ImVec2(a.x + 1.0f * s, a.y + 1.0f * s),
                                      ImVec2(b.x - 1.0f * s, a.y + 0.40f * (b.y - a.y)),
                                      IM_COL32(255, 255, 255, 70), style.rounding * s);
                }
                else if (isPeak)
                {
                    dl->AddRectFilled(a, b,
                                      (c.lit & 0x00ffffffu) | (150u << 24),
                                      style.rounding * s);
                }
                else
                {
                    // The unlit jewel is visible but recessed: a dim body with a
                    // black inner shadow, which is what stops a tall ladder from
                    // reading as one solid coloured bar (and is what the JUCE
                    // meter draws too).
                    dl->AddRectFilled(a, b, c.unlit, style.rounding * s);
                    dl->AddRect(a, b, IM_COL32(0, 0, 0, 120), style.rounding * s, 0, 1.0f * s);
                }
            }
        }
    }

private:
    float display_[2] = { -100.0f, -100.0f };
    float peak_[2]    = { -100.0f, -100.0f };
    float hold_[2]    = { 0.0f, 0.0f };
};

} // namespace duskdaf

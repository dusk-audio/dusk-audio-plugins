// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskStepperBox.hpp — a numeric read-out with [-] and [+] ends, for DAF/Dear
// ImGui UIs.
//
// The hardware-console "nudge" control: the value sits in a recessed window,
// a click on either end steps it, holding the end auto-repeats, the wheel steps
// it, a vertical drag on the window sweeps it and a double-click opens the
// shared inline text editor (DuskPanel::valueEdit). The knob in
// DuskImGuiWidgets.hpp is for continuous parameters; this is for values whose
// legal positions are a grid (1 Hz / 10 Hz / 100 Hz bands, 0.1 dB, 0.1 Q).
//
// Deliberately NOT bound to a host parameter: the first user is a filter table
// that is plugin STATE, not a parameter, so the box mutates `value` and returns
// true and the caller decides where the new value goes (setParameterValue,
// setState, ...). A parameter-bound caller wraps it in the usual
// beginEdit/setParam/endEdit on `changed`.
//
// The step rule is the caller's: `step(value, dir, ctx)` returns the next legal
// value in that direction (dir = +1 / -1) and is what keeps the box honest about
// the grid; the box never adds a float increment itself. `snap(value, ctx)`
// places a typed value onto the same grid.
#pragma once

#include <cmath>
#include <cstdio>

#include "DuskImGuiWidgets.hpp"

namespace duskdaf
{

struct StepperBoxStyle
{
    ImU32 windowBg   = IM_COL32(12, 12, 13, 255);
    ImU32 windowRim  = IM_COL32(72, 72, 76, 255);
    ImU32 text       = IM_COL32(236, 236, 230, 255);
    ImU32 textDim    = IM_COL32(120, 120, 118, 255);
    ImU32 endBg      = IM_COL32(44, 44, 47, 255);
    ImU32 endBgHot   = IM_COL32(66, 66, 70, 255);
    ImU32 endGlyph   = IM_COL32(222, 222, 216, 255);
    float endWidth   = 20.0f;       // design px, each end
    float textSize   = 13.0f;       // design px
    float rounding   = 3.0f;
    float repeatDelay = 0.40f;      // seconds before auto-repeat starts
    float repeatRate  = 0.06f;      // seconds between repeats
    float dragPixelsPerStep = 6.0f; // vertical drag: design px per step
};

struct StepperBoxRule
{
    // What typed entry accepts besides a bare number: the box's own unit, so
    // a "2k" meant for FREQ cannot land in CUT as 2000 dB clamped to 0.
    enum Unit { kPlain, kHertz, kDecibels };

    float (*step)(float value, int dir, void* ctx) = nullptr;
    float (*snap)(float value, void* ctx) = nullptr;
    void* ctx = nullptr;
    float minV = 0.0f;
    float maxV = 1.0f;
    Unit  unit = kPlain;
};

// Returns true when `value` changed this frame. `enabled == false` draws the box
// dimmed and ignores input. `fmt` formats the read-out. Typed entry is parsed
// with the classic-locale value_text parser so a comma-decimal host cannot break
// it; a trailing "dB", "Hz" or "k" (x1000) is accepted.
inline bool stepperBox(DuskPanel& panel, ImDrawList* dl, const char* id,
                       float x0, float y0, float x1, float y1,
                       float& value, const StepperBoxRule& rule,
                       const char* fmt, bool enabled,
                       const StepperBoxStyle& st = StepperBoxStyle())
{
    const float s = panel.scale();
    bool changed = false;
    const float ew = st.endWidth;
    const float cy = 0.5f * (y0 + y1);

    const auto clampV = [&](float v) { return v < rule.minV ? rule.minV : (v > rule.maxV ? rule.maxV : v); };
    const auto doStep = [&](int dir)
    {
        if (rule.step == nullptr) return;
        const float nv = clampV(rule.step(value, dir, rule.ctx));
        if (nv != value) { value = nv; changed = true; }
    };

    // --- the two ends: click steps, hold repeats -------------------------
    struct End { float ax0, ax1; int dir; const char* glyph; ImDrawFlags corners; };
    const End ends[2] = {
        { x0, x0 + ew, -1, "-", ImDrawFlags_RoundCornersLeft },
        { x1 - ew, x1, +1, "+", ImDrawFlags_RoundCornersRight },
    };
    for (const End& e : ends)
    {
        char bid[64];
        std::snprintf(bid, sizeof(bid), "%s##%s", id, e.dir < 0 ? "dec" : "inc");
        ImGui::SetCursorScreenPos(panel.P(e.ax0, y0));
        ImGui::InvisibleButton(bid, ImVec2((e.ax1 - e.ax0) * s, (y1 - y0) * s));
        const bool hot = enabled && (ImGui::IsItemHovered() || ImGui::IsItemActive());
        if (enabled)
        {
            if (ImGui::IsItemActivated())
                doStep(e.dir);
            else if (ImGui::IsItemActive())
            {
                // Auto-repeat while held, after a short delay. Count the repeat
                // boundaries crossed since last frame so a slow frame still
                // steps the right number of times.
                const float held = ImGui::GetIO().MouseDownDuration[0];
                if (held > st.repeatDelay)
                {
                    const float prev = held - ImGui::GetIO().DeltaTime;
                    const int before = prev > st.repeatDelay
                        ? (int)((prev - st.repeatDelay) / st.repeatRate) : 0;
                    const int now = (int)((held - st.repeatDelay) / st.repeatRate);
                    for (int i = before; i < now; ++i)
                        doStep(e.dir);
                }
            }
        }
        dl->AddRectFilled(panel.P(e.ax0, y0), panel.P(e.ax1, y1),
                          hot ? st.endBgHot : st.endBg, st.rounding * s, e.corners);
        panel.text(dl, 0.5f * (e.ax0 + e.ax1), cy - 0.60f * st.textSize, st.textSize + 2.0f,
                   enabled ? st.endGlyph : st.textDim, e.glyph, 0, true);
    }

    // --- the window: drag sweeps, wheel steps, double-click types --------
    const float wx0 = x0 + ew, wx1 = x1 - ew;
    char wid[64];
    std::snprintf(wid, sizeof(wid), "%s##win", id);
    ImGui::SetCursorScreenPos(panel.P(wx0, y0));
    ImGui::InvisibleButton(wid, ImVec2((wx1 - wx0) * s, (y1 - y0) * s));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    const bool editing = panel.isEditingValue(id);

    // One item is active at a time in ImGui, so a single accumulator serves
    // every stepper box in the UI.
    static float dragAccum = 0.0f;
    if (enabled && !editing)
    {
        if (ImGui::IsItemActivated())
            dragAccum = 0.0f;
        else if (active)
        {
            // Accumulate drag distance in design px; every dragPixelsPerStep
            // crossed emits one step. Shift makes it five times finer.
            const float fine = ImGui::GetIO().KeyShift ? 5.0f : 1.0f;
            dragAccum += -ImGui::GetIO().MouseDelta.y / s;
            const float per = st.dragPixelsPerStep * fine;
            while (dragAccum >= per)  { doStep(+1); dragAccum -= per; }
            while (dragAccum <= -per) { doStep(-1); dragAccum += per; }
        }
        if (hovered && !active)
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel > 0.0f) doStep(+1);
            else if (wheel < 0.0f) doStep(-1);
        }
        // The second press of a double-click makes the item active that same
        // frame, so the gesture must be read while active, as the knob does.
        if ((hovered || active) && ImGui::IsMouseDoubleClicked(0))
        {
            // Seed with the value's full precision, not the read-out's: a
            // 1234.6 Hz filter shown as "1235" must come back as 1234.6 when
            // the edit is confirmed untouched, or confirming would move it.
            char seed[32];
            std::snprintf(seed, sizeof(seed), "%.9g", (double)value);   // round-trips any float
            // Classic-locale form, which the parser below reads back.
            for (char* p = seed; *p; ++p) if (*p == ',') *p = '.';
            panel.openValueEdit(id, value, seed);
        }
    }

    dl->AddRectFilled(panel.P(wx0, y0), panel.P(wx1, y1), st.windowBg);
    dl->AddRect(panel.P(x0, y0), panel.P(x1, y1), st.windowRim, st.rounding * s, 0, 1.0f * s);

    if (editing)
    {
        float typed = 0.0f;
        // Accepts a bare number and, per the rule's unit, a trailing dB, or a
        // trailing Hz and the console form the axis labels print: "2k", "1k2",
        // "12k5" (digits after the k are tenths, hundredths ... of a thousand).
        const auto parse = [](const char* text, uint32_t unit, void*, float& out) -> bool
        {
            const char* end = nullptr;
            float v = 0.0f;
            if (!value_text::parseDecimal(text, end, v)) return false;
            if (unit == StepperBoxRule::kHertz && (*end == 'k' || *end == 'K'))
            {
                ++end;
                v *= 1000.0f;
                float place = 100.0f;
                while (*end >= '0' && *end <= '9')
                {
                    v += (float)(*end - '0') * place;
                    place *= 0.1f;
                    ++end;
                }
                if (!value_text::finish(end, "") && !value_text::suffix(end, "Hz", "hz"))
                    return false;
            }
            else if (unit == StepperBoxRule::kHertz)
            {
                if (!value_text::finish(end, "") && !value_text::suffix(end, "Hz", "hz")) return false;
            }
            else if (unit == StepperBoxRule::kDecibels)
            {
                if (!value_text::finish(end, "") && !value_text::suffix(end, "dB", "db")) return false;
            }
            else if (!value_text::finish(end, ""))
                return false;
            out = v;
            return true;
        };
        if (panel.valueEdit(id, 0.5f * (wx0 + wx1), cy, 0.0f, typed, parse, (uint32_t)rule.unit))
        {
            float v = clampV(typed);
            if (rule.snap != nullptr) v = clampV(rule.snap(v, rule.ctx));
            if (v != value) { value = v; changed = true; }
        }
    }
    else
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, (double)value);
        panel.text(dl, 0.5f * (wx0 + wx1), cy - 0.5f * st.textSize, st.textSize,
                   enabled ? st.text : st.textDim, buf, 0, false);
    }
    return changed;
}

} // namespace duskdaf

// TapeEchoUI.cpp — Dear ImGui UI for Tape Echo, modeled on the original
// classic tape-echo hardware front panel: black chassis, green MODE SELECTOR block with
// numbered dial, recessed metal-trimmed control panel, chrome knobs with
// triangle markers, VU meter and peak LED. Hardware I/O (jacks, switches
// for mic routing) is intentionally not reproduced.
// All rendering is custom ImDrawList work in a 900x340 design space.

#include "DistrhoUI.hpp"
#include "TapeEchoAccess.hpp"
#include "TapeEchoDSP.hpp"
#include "TapeEchoParams.hpp"
#include "TapeEchoVersion.hpp"
#include "DuskImGuiFont.hpp"      // shared crisp-bold loader (candidate search + DPI)
#include "DuskImGuiTextInput.hpp" // Windows host focus while typing values
#include "DuskImGuiWidgets.hpp"   // shared DuskPanel: chrome knob, LED, text, value bubble
#include "DuskSupportersOverlay.hpp" // shared DPF Patreon "Special Thanks" overlay
#include "TapeEchoFontRegular.inc"
#include "TapeEchoFontSemiBold.inc"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace
{
    constexpr float kDesignW = 900.0f;
    constexpr float kDesignH = 340.0f;

    // Restrained hardware palette: graphite chassis, aged olive paint, warm
    // nickel and a single red status color. Every highlight assumes a top-left
    // light source so separate components still read as one manufactured object.
    constexpr ImU32 kColHeader    = IM_COL32(14, 14, 15, 255);
    constexpr ImU32 kColGreen     = IM_COL32(76, 103, 48, 255);
    constexpr ImU32 kColGreenDk   = IM_COL32(39, 57, 24, 255);
    constexpr ImU32 kColWhite     = IM_COL32(241, 238, 226, 255);
    constexpr ImU32 kColWhiteDim  = IM_COL32(196, 193, 182, 255);
    constexpr ImU32 kColRailInk   = IM_COL32(31, 31, 30, 255);
    constexpr ImU32 kColLedOn     = IM_COL32(255, 60, 40, 255);
    constexpr ImU32 kColLedGlow   = IM_COL32(255, 70, 45, 80);

    constexpr float kPi = 3.14159265358979f;
}

class TapeEchoUI : public UI, public duskdpf::ParamHost
{
public:
    //--- duskdpf::ParamHost: forward the shared widgets' edits to the host ------
    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override { setParameterValue(idx, v); }

    TapeEchoUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kTeParams[i].def;
        setGeometryConstraints((uint32_t)kDesignW, (uint32_t)kDesignH, true);

        // Barlow Condensed is bundled under the SIL OFL. Unlike the previous
        // system-font search this produces identical metrics on every OS. Two
        // weights preserve a real hierarchy: semibold for controls/product marks,
        // regular for values, version copy and secondary annotations.
        // DPF coordinates are physical pixels. Cover the full resize range in
        // native atlas sizes instead of baking only the launch DPI: 7..24 px
        // design text remains near-native from the 1x minimum through 3x.
        static constexpr float kLabelSizes[] =
            { 7.0f, 9.0f, 11.0f, 14.0f, 18.0f, 24.0f, 32.0f, 48.0f, 72.0f };
        static constexpr float kRegularSizes[] =
            { 7.0f, 9.0f, 11.0f, 14.0f, 18.0f, 24.0f, 32.0f, 48.0f };
        const float dpi = getScaleFactor();
        labelFonts = duskdpf::loadEmbeddedCrispFontSet(
            kTeFontSemiBold, kTeFontSemiBold_len, kLabelSizes,
            (int)(sizeof(kLabelSizes) / sizeof(kLabelSizes[0])), 1.0f);
        regularFonts = duskdpf::loadEmbeddedCrispFontSet(
            kTeFontRegular, kTeFontRegular_len, kRegularSizes,
            (int)(sizeof(kRegularSizes) / sizeof(kRegularSizes[0])), 1.0f);
        labelFont = labelFonts.pick(12.5f * dpi);
        panel.setFontSet(labelFonts);

        scanUserPresets();
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount)
            return;
        values[index] = value;
        // Re-derive the active preset from the current values. Preset identity is
        // UI-only state (not a DPF parameter), so it is lost across a project
        // reload even though the host restores every parameter value; matching
        // the restored values back to a preset recovers the selection. It also
        // survives a POWER/bypass toggle (no sound parameter changes) and clears
        // once an edit diverges from every preset. Gated on the preset params so
        // the per-frame meter output never triggers a scan.
        if (teIsPresetParam(index))
            syncPresetSelection();
    }

    void programLoaded(uint32_t index) override
    {
        currentPreset = index < (uint32_t)kNumFactoryPresets
                      ? (int)index
                      : -1;
        currentUserName.clear();
    }

    void onImGuiDisplay() override
    {
        if (loopSpliceReleasePending)
        {
            setParameterValue(kParamLoopSplice, 0.0f);
            editParameter(kParamLoopSplice, false);
            loopSpliceReleasePending = false;
        }

        const float winW = (float)getWidth();
        const float winH = (float)getHeight();
        s   = std::min(winW / kDesignW, winH / kDesignH);
        org = ImVec2(0.5f * (winW - kDesignW * s), 0.5f * (winH - kDesignH * s));

        // Bind the shared widget panel to this frame's scale/origin/font. `this`
        // is the ParamHost the knob/LED gestures call back into.
        panel.begin(s, org, labelFont, this);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("TapeEcho", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(8, 8, 8, 255));
        drawChassis(dl);

        // Treat the supporters panel as a modal: background controls remain
        // visible beneath its scrim but cannot receive click-through gestures.
        // Snapshot before drawHeader(), where a title click can open it.
        const bool modalOpen = showSupporters;
        if (modalOpen)
            ImGui::BeginDisabled();
        drawHeader(dl);
        drawMeterBlock(dl);
        drawInputRow(dl);
        drawModeSelector(dl);
        drawControlPanel(dl);
        if (values[kParamBypass] >= 0.5f)
        {
            dl->AddRectFilled(P(2, 50), P(kDesignW - 2, 289),
                              IM_COL32(0, 0, 0, 92));
            text(dl, 450, 163, 18.0f, IM_COL32(220, 216, 204, 118),
                 "STANDBY", 0, true);
        }
        drawUtilityRail(dl);
        if (modalOpen)
            ImGui::EndDisabled();
        if (showSupporters)
            duskdpf::drawSupportersOverlay(
                panel, dl, kDesignW, kDesignH, showSupporters, "Tape Echo 2",
                TE2_VERSION_STRING);

        // Own resize grip, submitted LAST so it wins ImGui's hover race and
        // paints over everything. AUv2 hosts (Logic) never provide a window
        // grip of their own; on VST3/CLAP the host's grip stays available and
        // this is simply a second way to do the same thing.
        const duskdpf::ResizeGripState grip =
            panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH);

        ImGui::End();
        ImGui::PopStyleVar(2);
        textInputFocus.update(*this);

        // Cursor feedback. The DPF-Widgets ImGui backend never forwards
        // ImGui::SetMouseCursor() to the window, so drive DGL's cursor directly.
        // Edge-triggered: setCursor() is a window-level call, not a per-frame one.
        if (grip.hot != gripCursorSet)
        {
            gripCursorSet = grip.hot;
            setCursor(gripCursorSet ? DGL_NAMESPACE::kMouseCursorUpLeftDownRight
                                    : DGL_NAMESPACE::kMouseCursorArrow);
        }

        // Applied after End() so a host that services the resize synchronously
        // cannot re-enter the UI in the middle of this window's submission.
        if (grip.resized)
            setSize(grip.width, grip.height);
    }

private:
    //--- helpers -----------------------------------------------------------------
    ImVec2 P(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }

    static ImU32 fade(ImU32 c, float amount)
    {
        amount = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
        const ImU32 a = (c >> 24) & 0xffu;
        return (c & 0x00ffffffu) | ((ImU32)(a * amount + 0.5f) << 24);
    }

    static ImU32 blend(ImU32 a, ImU32 b, float t)
    {
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const auto ch = [t](ImU32 x, ImU32 y, int shift)
        {
            const float xv = (float)((x >> shift) & 0xffu);
            const float yv = (float)((y >> shift) & 0xffu);
            return (ImU32)(xv + (yv - xv) * t + 0.5f) << shift;
        };
        return ch(a, b, 0) | ch(a, b, 8) | ch(a, b, 16) | ch(a, b, 24);
    }

    void text(ImDrawList* dl, float x, float y, float size, ImU32 col,
              const char* txt, int align /* -1 left, 0 center, 1 right */,
              bool bold = false) const
    {
        panel.text(dl, x, y, size, col, txt, align, bold);
    }

    void regularText(ImDrawList* dl, float x, float y, float size, ImU32 col,
                     const char* txt, int align = -1) const
    {
        const float px = size * s;
        ImFont* font = regularFonts.pick(px);
        if (font == nullptr)
        {
            text(dl, x, y, size, col, txt, align);
            return;
        }
        const ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, 0.0f, txt);
        ImVec2 pos = P(x, y);
        if (align == 0) pos.x -= 0.5f * ts.x;
        if (align == 1) pos.x -= ts.x;
        pos.x = std::floor(pos.x + 0.5f);
        pos.y = std::floor(pos.y + 0.5f);
        dl->AddText(font, px, pos, col, txt);
    }

    void led(ImDrawList* dl, float x, float y, bool on, float r = 5.0f) const
    {
        panel.led(dl, x, y, on, r);
    }

    static float knobAngle(float t) { return duskdpf::DuskPanel::knobAngle(t); }

    void brushedRect(ImDrawList* dl, float x0, float y0, float x1, float y1,
                     ImU32 top, ImU32 bottom, float rounding = 0.0f) const
    {
        dl->AddRectFilledMultiColor(P(x0, y0), P(x1, y1),
                                    top, top, bottom, bottom);
        // Low-contrast deterministic horizontal brushing. Geometry instead of a
        // bitmap keeps the surface sharp at every supported UI scale.
        for (int y = (int)y0 + 2; y < (int)y1 - 1; y += 3)
        {
            const int n = (y * 37 + 11) & 31;
            const float inset = (float)n * 0.31f;
            dl->AddLine(P(x0 + inset, (float)y),
                        P(x1 - 5.0f - 0.4f * inset, (float)y),
                        (y & 1) ? IM_COL32(255, 255, 255, 10)
                                : IM_COL32(0, 0, 0, 12),
                        0.55f * s);
        }
        if (rounding > 0.0f)
            dl->AddRect(P(x0, y0), P(x1, y1), IM_COL32(255, 255, 255, 18),
                        rounding * s, 0, 0.8f * s);
    }

    void drawChassis(ImDrawList* dl) const
    {
        dl->AddRectFilledMultiColor(
            P(0, 0), P(kDesignW, kDesignH),
            IM_COL32(34, 34, 35, 255), IM_COL32(30, 30, 31, 255),
            IM_COL32(20, 20, 21, 255), IM_COL32(22, 22, 23, 255));
        for (int y = 52; y < 289; y += 4)
            dl->AddLine(P(0, (float)y), P(kDesignW, (float)y),
                        (y & 4) ? IM_COL32(255, 255, 255, 5)
                                : IM_COL32(0, 0, 0, 8),
                        0.6f * s);
        // Deep outer lip and a fine top-left catch-light sell the chassis as a
        // single object instead of a collection of floating rectangles.
        dl->AddRect(P(1, 1), P(kDesignW - 1, kDesignH - 1),
                    IM_COL32(7, 7, 8, 255), 7.0f * s, 0, 2.0f * s);
        dl->AddLine(P(8, 4), P(kDesignW - 8, 4),
                    IM_COL32(225, 222, 211, 90), 1.0f * s);
    }

    void drawDuskMark(ImDrawList* dl, float cx, float cy) const
    {
        const ImVec2 c = P(cx, cy);
        const float r = 9.0f * s;
        dl->AddCircle(c, r, kColWhiteDim, 32, 1.25f * s);
        dl->AddLine(P(cx - 7.0f, cy + 1.0f), P(cx + 7.0f, cy + 1.0f),
                    kColWhiteDim, 1.1f * s);
        dl->PathClear();
        dl->PathArcTo(P(cx, cy + 1.0f), 4.8f * s, kPi, 2.0f * kPi, 18);
        dl->PathStroke(IM_COL32(184, 205, 151, 255), 0, 1.4f * s);
        dl->AddCircleFilled(P(cx, cy + 1.0f), 1.4f * s,
                            IM_COL32(196, 71, 49, 255), 12);
    }

    void drawKnobBody(ImDrawList* dl, float cx, float cy, float radius,
                      float t, bool enabled = true, bool ticks = true) const
    {
        const ImVec2 c = P(cx, cy);
        const float R = radius * s;
        const float dim = enabled ? 1.0f : 0.38f;

        if (ticks)
            for (int i = 0; i <= 10; ++i)
            {
                const float a = knobAngle((float)i / 10.0f);
                const ImVec2 d(std::sin(a), -std::cos(a));
                const float inner = R + 3.0f * s;
                const float outer = R + (i == 5 ? 8.0f : 6.5f) * s;
                dl->AddLine(ImVec2(c.x + d.x * inner, c.y + d.y * inner),
                            ImVec2(c.x + d.x * outer, c.y + d.y * outer),
                            fade(kColWhiteDim, dim), (i == 5 ? 1.5f : 1.1f) * s);
            }

        // Contact shadow, seating washer and ridged skirt.
        dl->AddCircleFilled(P(cx + 1.3f, cy + 3.2f), R * 1.08f,
                            IM_COL32(0, 0, 0, (int)(110.0f * dim)), 56);
        dl->AddCircleFilled(c, R * 1.03f, fade(IM_COL32(16, 16, 17, 255), dim), 56);
        dl->AddCircleFilled(P(cx, cy - 0.5f), R, fade(IM_COL32(88, 88, 89, 255), dim), 56);
        for (int i = 0; i < 36; ++i)
        {
            const float a = 2.0f * kPi * (float)i / 36.0f;
            const ImVec2 d(std::sin(a), -std::cos(a));
            const ImU32 ridge = (i % 3 == 0)
                              ? IM_COL32(208, 207, 202, (int)(90.0f * dim))
                              : IM_COL32(22, 22, 23, (int)(130.0f * dim));
            dl->AddLine(ImVec2(c.x + d.x * R * 0.82f, c.y + d.y * R * 0.82f),
                        ImVec2(c.x + d.x * R * 0.98f, c.y + d.y * R * 0.98f),
                        ridge, 1.0f * s);
        }

        // Warm nickel cap. Horizontal strips are analytically clipped to the
        // circle, producing a smooth top-to-bottom metal response without the
        // concentric "bullseye" look of stacked circles.
        const float capR = R * 0.73f;
        dl->AddCircleFilled(P(cx, cy + 0.8f), capR * 1.04f,
                            fade(IM_COL32(24, 24, 25, 255), dim), 52);
        dl->AddCircleFilled(c, capR, fade(IM_COL32(100, 101, 102, 255), dim), 52);
        // One strip per physical pixel avoids visible scanlines at either 1x or
        // Retina scale while retaining the deterministic gradient.
        const int kMetalBands = std::max(16, (int)(2.0f * capR + 0.5f));
        for (int i = 0; i <= kMetalBands; ++i)
        {
            const float u = -0.96f + 1.92f * (float)i / (float)kMetalBands;
            const float half = std::sqrt(std::max(0.0f, 1.0f - u * u)) * capR;
            const float shade = 0.5f * (u + 1.0f);
            const ImU32 col = blend(IM_COL32(218, 217, 210, 255),
                                    IM_COL32(91, 92, 94, 255), shade);
            const float yy = c.y + u * capR;
            dl->AddLine(ImVec2(c.x - half, yy), ImVec2(c.x + half, yy),
                        fade(col, dim), 1.15f);
        }
        dl->AddCircle(c, capR, fade(IM_COL32(17, 17, 18, 255), dim),
                      52, 1.1f * s);
        dl->AddCircleFilled(P(cx - radius * 0.20f, cy - radius * 0.24f),
                            capR * 0.17f,
                            IM_COL32(255, 255, 250, (int)(22.0f * dim)), 24);

        const float a = knobAngle(t);
        const ImVec2 d(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + d.x * capR * 0.10f + 0.8f * s,
                           c.y + d.y * capR * 0.10f + 1.0f * s),
                    ImVec2(c.x + d.x * capR * 0.91f + 0.8f * s,
                           c.y + d.y * capR * 0.91f + 1.0f * s),
                    IM_COL32(255, 255, 255, (int)(30.0f * dim)), 3.1f * s);
        dl->AddLine(ImVec2(c.x + d.x * capR * 0.10f, c.y + d.y * capR * 0.10f),
                    ImVec2(c.x + d.x * capR * 0.91f, c.y + d.y * capR * 0.91f),
                    fade(IM_COL32(18, 18, 19, 255), dim), 2.8f * s);
    }

    // Hardware-specific visual body with shared DuskPanel gestures/readouts.
    // Double-click reset and a right-click Reset/Type value menu follow common
    // commercial plugin conventions without changing the rest of the Dusk fleet.
    void knob(const char* id, uint32_t param, float minV, float maxV,
              float cx, float cy, float radius, bool stepped = false,
              bool panelTicks = true, const char* fmt = nullptr,
              const char* suffix = "", float dispMul = 1.0f, float dispAdd = 0.0f,
              bool persistent = false, bool enabled = true,
              const char* overrideText = nullptr, const char* /*tooltip*/ = nullptr)
    {
        const float range = maxV - minV;
        float t = range > 0.0f ? (values[param] - minV) / range : 0.0f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        drawKnobBody(ImGui::GetWindowDrawList(), cx, cy, radius, t, enabled, panelTicks);

        const char* bubbleText = overrideText;
        if (!enabled && bubbleText == nullptr)
            bubbleText = "INACTIVE";
        panel.knob(id, param, minV, maxV, cx, cy, radius,
                   values[param], kTeParams[param].def, stepped, panelTicks,
                   fmt != nullptr ? fmt : (stepped ? "%.0f" : "%.2f"), suffix,
                   /*faceColor*/ 0, /*bodyless*/ true, persistent,
                   /*tooltip*/ nullptr, /*rightClickReset*/ false,
                   dispMul, dispAdd, /*name*/ nullptr,
                   /*contextMenu*/ true, bubbleText,
                   /*hasExternalReadout*/ false,
                   /*dispMin*/ 0.0f, /*dispMax*/ 0.0f,
                   /*nameOnHover*/ false,
                   /*doubleClickReset*/ true,
                   /*persistentTextSize*/ 11.5f);
    }

    void knobLabel(ImDrawList* dl, float cx, float topY, const char* l1,
                   const char* l2 = nullptr, bool enabled = true) const
    {
        const ImU32 ink = enabled ? kColWhite : IM_COL32(104, 103, 98, 255);
        constexpr float kLabelSize = 12.0f;
        constexpr float kLineStep = 12.5f;
        text(dl, cx, topY, kLabelSize, ink, l1, 0, true);
        if (l2 != nullptr)
            text(dl, cx, topY + kLineStep, kLabelSize, ink, l2, 0, true);
        const float ty = topY + (l2 != nullptr ? 27.0f : 15.0f);
        dl->AddTriangleFilled(P(cx - 3.7f, ty), P(cx + 3.7f, ty),
                              P(cx, ty + 5.5f), ink);
    }

    // Small chevron button ("<" / ">") for stepping the preset combo. Styled to
    // the dark header instead of the silver TapeMachine treatment: it has to read
    // as part of the combo it flanks, so fill/hover match ImGuiCol_FrameBg.
    bool chevron(ImDrawList* dl, const char* id, float cx, float cy,
                 float halfH, bool left)
    {
        const ImVec2 b0 = P(cx - 9.5f, cy - halfH);
        const ImVec2 b1 = P(cx + 9.5f, cy + halfH);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hov ? IM_COL32(54, 54, 58, 255)
                                      : IM_COL32(38, 38, 41, 255), 2.0f * s);
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 2.0f * s, 0, 1.0f * s);
        const ImVec2 c = P(cx, cy);
        const float  d = 4.3f * s;
        const ImU32 ink = hov ? kColWhite : kColWhiteDim;
        if (left)
            dl->AddTriangleFilled(ImVec2(c.x + d * 0.5f, c.y - d),
                                  ImVec2(c.x + d * 0.5f, c.y + d),
                                  ImVec2(c.x - d * 0.7f, c.y), ink);
        else
            dl->AddTriangleFilled(ImVec2(c.x - d * 0.5f, c.y - d),
                                  ImVec2(c.x - d * 0.5f, c.y + d),
                                  ImVec2(c.x + d * 0.7f, c.y), ink);
        return ImGui::IsItemClicked();
    }

    // Small header text button (INIT / SAVE), styled to match the chevrons that
    // flank the preset combo rather than TapeMachine's silver caps: this header
    // is a black chassis strip.
    bool textButton(ImDrawList* dl, const char* id, float x0, float y0,
                    float x1, float y1, const char* label)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hov ? IM_COL32(54, 54, 58, 255)
                                      : IM_COL32(38, 38, 41, 255), 2.0f * s);
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 2.0f * s, 0, 1.0f * s);
        // text() takes the top edge, so centre it explicitly rather than with a
        // fixed fraction of the height (the band is 30 px tall now, not 20).
        constexpr float kTxt = 11.0f;
        text(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1 - kTxt), kTxt,
             hov ? kColWhite : kColWhiteDim, label, 0, true);
        return ImGui::IsItemClicked();
    }

    //--- sections ---------------------------------------------------------------------
    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(P(0, 0), P(kDesignW, 48), kColHeader);
        brushedRect(dl, 0, 0, kDesignW, 4,
                    IM_COL32(205, 203, 197, 255),
                    IM_COL32(91, 91, 91, 255));
        dl->AddLine(P(0, 48), P(kDesignW, 48),
                    IM_COL32(91, 90, 88, 255), 1.2f * s);
        dl->AddLine(P(0, 49), P(kDesignW, 49),
                    IM_COL32(0, 0, 0, 160), 1.0f * s);

        constexpr float kHdrCy = 24.0f;
        constexpr float kPlateX0 = 28.0f, kPlateX1 = 318.0f;
        constexpr float kPlateY0 = 8.0f, kPlateY1 = 40.0f;
        dl->AddRectFilledMultiColor(
            P(kPlateX0, kPlateY0), P(kPlateX1, kPlateY1),
            IM_COL32(37, 37, 38, 255), IM_COL32(29, 29, 30, 255),
            IM_COL32(16, 16, 17, 255), IM_COL32(19, 19, 20, 255));
        dl->AddRect(P(kPlateX0, kPlateY0), P(kPlateX1, kPlateY1),
                    IM_COL32(185, 184, 180, 220), 3.5f * s, 0, 1.2f * s);
        dl->AddLine(P(40, 37), P(230, 37),
                    IM_COL32(116, 145, 75, 210), 1.2f * s);

        // Product wordmark: a single strong name plus a discrete model cartouche
        // creates identity without impersonating any reference hardware.
        text(dl, 42, 10.0f, 24.0f, kColWhite, "TAPE ECHO", -1, true);
        dl->AddRectFilled(P(236, 12), P(263, 36),
                          IM_COL32(66, 91, 41, 255), 2.5f * s);
        dl->AddRect(P(236, 12), P(263, 36),
                    IM_COL32(133, 163, 94, 180), 2.5f * s, 0, 0.9f * s);
        text(dl, 249.5f, 13.0f, 18.0f, kColWhite, "2", 0, true);
        regularText(dl, 286, 13.0f, 9.0f, IM_COL32(151, 149, 143, 255),
                    "MODEL", 0);
        text(dl, 286, 23.0f, 10.5f, kColWhiteDim, "TE-2", 0, true);
        regularText(dl, 309, 30.5f, 7.5f, IM_COL32(128, 126, 121, 255),
                    "v" TE2_VERSION_STRING, 1);

        drawDuskMark(dl, 782, kHdrCy);
        text(dl, 799, 15.0f, 15.0f, kColWhite, "DUSK AUDIO", -1, true);
        regularText(dl, 799, 28.5f, 8.0f, IM_COL32(143, 142, 136, 255),
                    "ANALOGUE SIGNAL TOOLS", -1);

        // Match the other DPF plugins: clicking the title nameplate opens the
        // generated Patreon "Special Thanks" panel.
        ImGui::SetCursorScreenPos(P(kPlateX0, kPlateY0));
        if (ImGui::InvisibleButton(
                "##titlecredits",
                ImVec2((kPlateX1 - kPlateX0) * s, (kPlateY1 - kPlateY0) * s)))
            showSupporters = true;

        // Preset browser: < [combo] > INIT SAVE, all centred on the nameplate's
        // centre line. The band is 26 px, not the plate's full 30: these are solid
        // edge-to-edge fills where the plate is only a hairline outline, so at
        // equal height they read heavier than the plate they sit beside.
        // Chevron half-width and arrow size were scaled by the same 26/30.
        // Horizontal rhythm: 4 px inside the < combo > group, 8 px before INIT
        // (group break), 6 px between INIT and SAVE.
        constexpr float kBandY0 = kHdrCy - 12.5f, kBandY1 = kHdrCy + 12.5f;
        constexpr float kBandH  = kBandY1 - kBandY0;
        if (chevron(dl, "##presetprev", 350.5f, kHdrCy, 0.5f * kBandH, true))
            stepPreset(-1);
        if (chevron(dl, "##presetnext", 569.5f, kHdrCy, 0.5f * kBandH, false))
            stepPreset(1);

        ImGui::SetCursorScreenPos(P(364, kBandY0));
        ImGui::SetNextItemWidth(192.0f * s);
        // Lock the combo frame to the same 26 px height as the hand-drawn controls
        // (ImGui's default is font-size driven and would drift). Vertical padding
        // is the leftover half-height, which also keeps the preview text centred.
        // No PushFont here: unlike TapeMachine this UI bakes a single large face,
        // so the combo keeps ImGui's own atlas font for its preview/rows.
        ImFont* presetFont = regularFonts.pick(12.0f * s);
        if (presetFont != nullptr)
            ImGui::PushFont(presetFont);
        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(6.0f * s,
                   std::max(0.0f, 0.5f * (kBandH * s - ImGui::GetFontSize()))));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(48, 48, 51, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(55, 55, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(110, 45, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(132, 57, 48, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(92, 39, 34, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, kColGreenDk);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColGreen);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 58, 27, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColWhite);
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(122, 160, 84, 255));
        const char* preview = (currentPreset >= 0 && currentPreset < kNumFactoryPresets)
                                  ? kFactoryPresets[currentPreset].name
                                  : (!currentUserName.empty() ? currentUserName.c_str()
                                                              : "Presets...");
        // BeginCombo otherwise caps its popup at eight entries. An explicit
        // identity constraint makes it auto-fit all 13 factory presets, so the
        // complete list is visible without a scrollbar.
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::BeginCombo("##presets", preview))
        {
            for (int i = 0; i < kNumFactoryPresets; ++i)
            {
                if (ImGui::Selectable(kFactoryPresets[i].name, i == currentPreset))
                {
                    applyPreset(i);
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!userPresets.empty())
            {
                ImGui::SeparatorText("User");
                for (const auto& up : userPresets)
                    if (ImGui::Selectable(up.name.c_str(),
                                          currentPreset < 0 && up.name == currentUserName))
                    {
                        loadUserPreset(up.path, up.name);
                        ImGui::CloseCurrentPopup();
                    }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(12);
        ImGui::PopStyleVar();
        if (presetFont != nullptr)
            ImGui::PopFont();

        // INIT (back to factory defaults) and SAVE (name + write a user preset),
        // on the same band as the combo, clear of the top-right brand text.
        if (textButton(dl, "##init", 587, kBandY0, 635, kBandY1, "INIT"))
            initDefaults();
        if (textButton(dl, "##save", 642, kBandY0, 690, kBandY1, "SAVE"))
        {
            std::snprintf(saveBuf, sizeof(saveBuf), "%s", currentUserName.c_str());
            ImGui::OpenPopup("Save Preset");
        }
        drawSaveModal();
    }

    // Name-entry modal for SAVE. Dark styling to match this chassis; Enter or the
    // Save button commits, Escape / Cancel dismisses.
    void drawSaveModal()
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(26, 26, 28, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColWhite);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(90, 90, 94, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(40, 40, 43, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(50, 50, 54, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(56, 56, 60, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, kColGreenDk);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColGreen);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 58, 27, 255));
        if (ImGui::BeginPopupModal("Save Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Preset name");
            ImGui::SetNextItemWidth(240.0f * s);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##savename", saveBuf, sizeof(saveBuf),
                                                ImGuiInputTextFlags_EnterReturnsTrue
                                                | ImGuiInputTextFlags_AutoSelectAll);
            const bool doSave = ImGui::Button("Save") || enter;
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel");
            if (doSave && saveBuf[0] != '\0')
            {
                saveUserPreset(saveBuf);
                ImGui::CloseCurrentPopup();
            }
            if (cancel)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(9);
    }

    // Clamped, never wrapping: stepping off either end parks on the end preset.
    // With no preset loaded (currentPreset < 0) either direction lands on the first.
    void stepPreset(int dir)
    {
        int i = currentPreset < 0 ? (dir < 0 ? 0 : -1) : currentPreset;
        i += dir;
        if (i < 0) i = 0;
        if (i >= kNumFactoryPresets) i = kNumFactoryPresets - 1;
        applyPreset(i);
    }

    // Single write path for a UI-driven parameter change: keeps the local cache
    // in step and brackets the host write with the edit-gesture markers.
    void setP(uint32_t param, float value)
    {
        values[param] = value;
        editParameter(param, true);
        setParameterValue(param, value);
        editParameter(param, false);
    }

    // Walk a factory preset's parameter/value pairs. Shared by applyPreset() and
    // the identity matcher so the two can never disagree about what a preset sets.
    // BYPASS is deliberately absent: a preset recall must never fight the host's
    // own bypass state (see teIsPresetParam).
    template <typename Fn>
    static void forEachPresetParam(int idx, Fn&& fn)
    {
        const TapeEchoPreset& preset = kFactoryPresets[idx];
        for (uint32_t i = 0; i <= (uint32_t)kParamTapeAge; ++i)
            fn(i, preset.v[i]);
        fn((uint32_t)kParamOutputVolume, preset.outputVolume);
        fn((uint32_t)kParamEchoPan, preset.echoPan);
        fn((uint32_t)kParamReverbPan, preset.reverbPan);
        fn((uint32_t)kParamInputSend, preset.inputSend);
        fn((uint32_t)kParamWetSolo, preset.wetSolo);
    }

    void applyPreset(int idx)
    {
        if (idx < 0 || idx >= kNumFactoryPresets)
            return;
        currentPreset = idx;
        currentUserName.clear();
        forEachPresetParam(idx, [this](uint32_t param, float value)
                                { setP(param, value); });
    }

    // Reset every control to its factory default. BYPASS is left alone so INIT
    // never fights the host's own bypass state, and the momentary splice trigger
    // is not a state to restore.
    void initDefaults()
    {
        currentPreset = -1;
        currentUserName.clear();
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (teIsPresetParam(i))
                setP(i, kTeParams[i].def);
        // The defaults ARE the "Default" factory preset here, so re-derive rather
        // than leaving the combo reading "Presets..." over a recognised state.
        syncPresetSelection();
    }

    //--- user preset file library (~/.config/DuskAudio/TapeEcho2/presets) -------
    // Base dir: %APPDATA% (or %LOCALAPPDATA%) on Windows, else $XDG_CONFIG_HOME,
    // else $HOME/.config. macOS deliberately stays on the ~/.config layout the
    // shipped builds already write, so an update never orphans a user's library.
    // "." is the last resort only: never write relative to the host's CWD while
    // any supported variable is set.
    std::string configDir() const
    {
        std::string base;
       #if defined(_WIN32)
        for (const char* var : { "APPDATA", "LOCALAPPDATA" })
            if (const char* v = std::getenv(var); v != nullptr && *v != '\0')
            { base = v; break; }
       #elif defined(__APPLE__)
        // XDG_CONFIG_HOME is deliberately NOT read here: a mac with it set in a
        // dotfile would otherwise stop seeing the library shipped builds wrote.
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
            base = std::string(home) + "/.config";
       #else
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0')
            base = xdg;
        else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
            base = std::string(home) + "/.config";
       #endif
        if (base.empty())
            base = ".";
        return base + "/DuskAudio/TapeEcho2/presets";
    }

    // Strict field parse, shared by the library scan and the loader so a file can
    // never mean two things. atof() reports failure as 0.0 and happily yields
    // NaN/inf for "nan"/"1e999", all of which would reach the DSP through setP();
    // require the whole field to be one finite number and clamp it into the
    // parameter's declared range (mode and sync division included, before either
    // is folded to an index). Returns false for a line to skip.
    //
    // Locale-independent on purpose, in both directions (saveUserPreset() imbues
    // the same classic locale): plugin hosts do call setlocale(), and a
    // comma-decimal locale makes strtod() stop at the '.' in "0.5" — every value
    // in every preset file would silently read as its default.
    static bool parsePresetValue(const std::string& line, std::size_t valueStart,
                                 uint32_t param, float& out)
    {
        std::istringstream field(line.substr(valueStart));
        field.imbue(std::locale::classic());
        double d = 0.0;
        field >> d;
        if (field.fail() || !std::isfinite(d))   // unparsable, or overflowed to inf
            return false;
        char trailing = '\0';
        if (field >> trailing)                   // trailing junk: not a number
            return false;
        const TeParam& p = kTeParams[param];
        out = (float)(d < (double)p.min ? (double)p.min
                                        : (d > (double)p.max ? (double)p.max : d));
        return true;
    }

    void scanUserPresets()
    {
        userPresets.clear();
        namespace fs = std::filesystem;
        std::error_code ec;
        for (fs::directory_iterator it(configDir(), ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->path().extension() != ".tepreset")
                continue;
            UserPreset up;
            up.path = it->path().string();
            up.name = it->path().stem().string();
            for (uint32_t i = 0; i < kParamCount; ++i)
                up.vals[i] = kTeParams[i].def;
            std::ifstream f(it->path());
            std::string line;
            while (std::getline(f, line))
            {
                const auto eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                const std::string key = line.substr(0, eq);
                if (key == "name") { up.name = line.substr(eq + 1); continue; }
                for (uint32_t i = 0; i < kParamCount; ++i)
                    if (teIsPresetParam(i) && key == kTeParams[i].id)
                    {
                        float v = 0.0f;
                        if (parsePresetValue(line, eq + 1, i, v))
                            up.vals[i] = v;   // else keep the default already in place
                        break;
                    }
            }
            userPresets.push_back(std::move(up));
        }
        std::sort(userPresets.begin(), userPresets.end(),
                  [](const UserPreset& a, const UserPreset& b) { return a.name < b.name; });
    }

    // Display name stored inside a preset file, or "" when the file is missing or
    // carries no name= line (a foreign or truncated file in our own directory).
    static std::string storedPresetName(const std::string& path)
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
            if (line.compare(0, 5, "name=") == 0)
                return line.substr(5);
        return {};
    }

    void saveUserPreset(const char* rawName)
    {
        std::string name(rawName);
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        if (name.empty())
            return;
        const std::string dir = configDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
            return;   // no usable library directory (permissions, file in the way)
        // Filename stem: every non-alphanumeric collapses to '_', so distinct
        // display names can share a stem ("A B" and "A-B" both give "A_B").
        // Re-saving the SAME name still overwrites its own file; a stem clash
        // with a different name takes the next free suffix instead of silently
        // clobbering that preset.
        std::string stem;
        for (char c : name)
            stem += std::isalnum((unsigned char)c) ? c : '_';
        std::string path;
        bool usable = false;
        for (int n = 1; n <= 99 && !usable; ++n)
        {
            path = dir + "/" + stem
                 + (n == 1 ? std::string() : "_" + std::to_string(n)) + ".tepreset";
            // A free path, or one whose file already stores THIS display name.
            // An existing file without a name= line is not free: the library
            // lists it under its stem, so overwriting it would drop a preset
            // the player can see. A path that cannot be probed is never assumed
            // free either - exists() reports an error as false.
            ec.clear();
            const bool present = std::filesystem::exists(path, ec);
            if (ec)
                continue;
            usable = !present || storedPresetName(path) == name;
        }
        if (!usable)
            return;   // 99 colliding stems: refuse rather than overwrite one
        std::ofstream f(path, std::ios::trunc);
        if (!f)
            return;
        // Classic locale so the values are written with '.' whatever locale the
        // host installed, matching parsePresetValue() on the way back in.
        f.imbue(std::locale::classic());
        f << "name=" << name << "\n";
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (teIsPresetParam(i))
                f << kTeParams[i].id << "=" << values[i] << "\n";
        f.close();
        scanUserPresets();
        currentPreset = -1;
        currentUserName = name;
    }

    void loadUserPreset(const std::string& path, const std::string& name)
    {
        std::ifstream f(path);
        if (!f)
            return;
        // Default every parameter first, then overlay the file's values. This
        // matches the cache built in scanUserPresets() (missing keys ->
        // kTeParams[i].def), so an incomplete or legacy file loads to the exact
        // values its cached identity records - otherwise a missing key would keep
        // the current value and deriveUserPreset() could never match. BYPASS is
        // left alone (never saved, and not part of preset identity).
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (teIsPresetParam(i))
                setP(i, kTeParams[i].def);
        std::string line;
        while (std::getline(f, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = line.substr(0, eq);
            if (key == "name")
                continue;
            for (uint32_t i = 0; i < kParamCount; ++i)
                if (teIsPresetParam(i) && key == kTeParams[i].id)
                {
                    float v = 0.0f;
                    if (parsePresetValue(line, eq + 1, i, v))
                        setP(i, v);   // else keep the default written above
                    break;
                }
        }
        currentPreset = -1;
        currentUserName = name;
    }

    //--- preset identity recovery ----------------------------------------------
    // The active preset is UI-only state; these re-derive it from the current
    // parameter values so a project reload (which restores parameters but not the
    // selection) shows the right preset again. Compares with a range-relative
    // tolerance to absorb host parameter quantisation, and never compares BYPASS,
    // the splice trigger or the meter output.
    bool paramMatches(uint32_t id, float v) const
    {
        const TeParam& d = kTeParams[id];
        const float tol = std::max(1.0e-3f, (d.max - d.min) * 1.0e-4f);
        return std::fabs(values[id] - v) <= tol;
    }

    int deriveFactoryPreset() const
    {
        for (int idx = 0; idx < kNumFactoryPresets; ++idx)
        {
            bool ok = true;
            forEachPresetParam(idx, [&](uint32_t id, float v)
            {
                if (ok && teIsPresetParam(id) && !paramMatches(id, v))
                    ok = false;
            });
            if (ok)
                return idx;
        }
        return -1;
    }

    int deriveUserPreset() const
    {
        for (size_t i = 0; i < userPresets.size(); ++i)
        {
            bool ok = true;
            for (uint32_t id = 0; id < kParamCount && ok; ++id)
                if (teIsPresetParam(id) && !paramMatches(id, userPresets[i].vals[id]))
                    ok = false;
            if (ok)
                return (int)i;
        }
        return -1;
    }

    // Recompute the active selection from the current values (factory wins).
    void syncPresetSelection()
    {
        const int f = deriveFactoryPreset();
        if (f >= 0) { currentPreset = f; currentUserName.clear(); return; }
        const int u = deriveUserPreset();
        if (u >= 0) { currentPreset = -1; currentUserName = userPresets[(size_t)u].name; return; }
        currentPreset = -1;
        currentUserName.clear();
    }

    void drawMeterBlock(ImDrawList* dl)
    {
        led(dl, 34, 93, meterLevel > 0.89f, 5.2f);
        text(dl, 34, 105, 10.0f, kColWhite, "PEAK", 0, true);
        regularText(dl, 34, 116.5f, 8.0f, kColWhiteDim, "LEVEL", 0);

        constexpr float x0 = 70.0f, y0 = 60.0f, x1 = 274.0f, y1 = 153.0f;
        dl->AddRectFilled(P(x0 - 5, y0 - 5), P(x1 + 5, y1 + 5),
                          IM_COL32(5, 5, 6, 210), 5.5f * s);
        brushedRect(dl, x0 - 3, y0 - 3, x1 + 3, y1 + 3,
                    IM_COL32(189, 187, 181, 255),
                    IM_COL32(77, 77, 79, 255), 4.5f);
        dl->AddRectFilledMultiColor(
            P(x0, y0), P(x1, y1),
            IM_COL32(23, 25, 20, 255), IM_COL32(14, 16, 13, 255),
            IM_COL32(7, 9, 7, 255), IM_COL32(10, 11, 9, 255));

        // needle ballistics at frame rate; read the DSP meter directly when
        // the plugin runs in-process (CLAP/LV2 hosts do not forward output
        // parameters to the UI), else fall back to the output parameter.
        const float dt   = ImGui::GetIO().DeltaTime;
        float lvl = values[kParamOutLevel];
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (tapeEchoGetOutputLevel != nullptr) // weak: null in the split LV2 UI
            if (void* const inst = getPluginInstancePointer())
                lvl = tapeEchoGetOutputLevel(inst);
       #endif
        meterLevel = lvl;
        const float dB   = 20.0f * std::log10(lvl > 1e-5f ? lvl : 1e-5f);
        float target     = (dB + 20.0f) / 23.0f; // -20 dB .. +3 dB across the arc
        target = target < 0.0f ? 0.0f : (target > 1.0f ? 1.0f : target);
        needlePos += (target - needlePos) * (1.0f - std::exp(-dt * 7.0f));

        dl->PushClipRect(P(x0, y0), P(x1, y1), true);

        const ImVec2 pivot = P(172, 239);
        const float rArc   = 155.0f * s;
        const float aMin   = -0.62f, aMax = 0.62f; // radians from vertical

        // Minor scale marks plus calibrated major dB labels. The non-linear
        // crowding toward zero is characteristic of a real VU face and makes the
        // meter useful instead of merely animated.
        for (int i = 0; i <= 24; ++i)
        {
            const float a = aMin + (aMax - aMin) * (float)i / 24.0f;
            const ImVec2 dir(std::sin(a), -std::cos(a));
            dl->AddLine(
                ImVec2(pivot.x + dir.x * (rArc - 3.8f * s),
                       pivot.y + dir.y * (rArc - 3.8f * s)),
                ImVec2(pivot.x + dir.x * (rArc + 0.7f * s),
                       pivot.y + dir.y * (rArc + 0.7f * s)),
                i >= 21 ? IM_COL32(217, 71, 50, 225)
                        : IM_COL32(218, 216, 204, 190),
                0.85f * s);
        }

        struct MeterMark { float db; const char* label; };
        static constexpr MeterMark kMarks[] =
        {
            { -20.0f, "-20" }, { -10.0f, "-10" }, { -7.0f, "-7" },
            { -5.0f, "-5" }, { -3.0f, "-3" }, { 0.0f, "0" }, { 3.0f, "+3" },
        };
        for (const MeterMark& mark : kMarks)
        {
            const float t = (mark.db + 20.0f) / 23.0f;
            const float a = aMin + (aMax - aMin) * t;
            const ImVec2 dir(std::sin(a), -std::cos(a));
            const ImU32 ink = mark.db >= 0.0f
                            ? IM_COL32(235, 75, 54, 255)
                            : IM_COL32(229, 226, 213, 255);
            dl->AddLine(
                ImVec2(pivot.x + dir.x * (rArc - 7.0f * s),
                       pivot.y + dir.y * (rArc - 7.0f * s)),
                ImVec2(pivot.x + dir.x * (rArc + 3.0f * s),
                       pivot.y + dir.y * (rArc + 3.0f * s)),
                ink, 1.25f * s);
            regularText(dl, 172.0f + dir.x * 137.0f,
                        239.0f + dir.y * 137.0f - 3.8f,
                        7.8f, ink, mark.label, 0);
        }

        // red zone arc band
        dl->PathClear();
        for (int i = 0; i <= 8; ++i)
        {
            const float a = aMin + (aMax - aMin) * (0.8f + 0.2f * (float)i / 8.0f);
            dl->PathLineTo(ImVec2(pivot.x + std::sin(a) * (rArc + 5.0f * s),
                                  pivot.y - std::cos(a) * (rArc + 5.0f * s)));
        }
        dl->PathStroke(IM_COL32(226, 66, 46, 235), 0, 1.8f * s);

        text(dl, 172, 119.5f, 13.5f, IM_COL32(151, 224, 117, 255), "VU", 0, true);
        regularText(dl, 172, 135, 8.0f, IM_COL32(173, 175, 160, 255),
                    "OUTPUT LEVEL", 0);

        // needle
        {
            const float a = aMin + (aMax - aMin) * needlePos;
            const ImVec2 dir(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(pivot.x + dir.x * 38.0f * s + 1.0f * s,
                               pivot.y + dir.y * 38.0f * s + 1.2f * s),
                        ImVec2(pivot.x + dir.x * (rArc + 5.0f * s) + 1.0f * s,
                               pivot.y + dir.y * (rArc + 5.0f * s) + 1.2f * s),
                        IM_COL32(0, 0, 0, 150), 2.4f * s);
            dl->AddLine(ImVec2(pivot.x + dir.x * 38.0f * s,
                               pivot.y + dir.y * 38.0f * s),
                        ImVec2(pivot.x + dir.x * (rArc + 5.0f * s),
                               pivot.y + dir.y * (rArc + 5.0f * s)),
                        IM_COL32(239, 234, 215, 255), 1.45f * s);
        }

        dl->AddRectFilledMultiColor(P(x0, y0), P(x1, y0 + 34),
                                    IM_COL32(255, 255, 255, 34), IM_COL32(255, 255, 255, 9),
                                    IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        dl->AddLine(P(x0 + 8, y0 + 5), P(x1 - 28, y0 + 5),
                    IM_COL32(255, 255, 255, 24), 0.8f * s);
        dl->PopClipRect();
    }

    void drawInputRow(ImDrawList* dl)
    {
        constexpr float kLabelY = 167.0f, kKnobY = 234.0f;
        constexpr float x[4] = { 38.0f, 106.0f, 174.0f, 242.0f };

        knobLabel(dl, x[0], kLabelY, "INPUT", "DRIVE");
        knob("input", kParamInputGain, 0.0f, 1.0f, x[0], kKnobY, 23.0f,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, true, nullptr,
             "Tape preamp drive");

        knobLabel(dl, x[1], kLabelY, "DRY", "LEVEL");
        knob("dry", kParamDryLevel, 0.0f, 1.0f, x[1], kKnobY, 22.0f,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, true, nullptr,
             "Direct signal level");

        knobLabel(dl, x[2], kLabelY, "WOW &", "FLUTTER");
        knob("wow", kParamWowFlutter, 0.0f, 1.0f, x[2], kKnobY, 22.0f,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, true, nullptr,
             "Transport modulation depth");

        knobLabel(dl, x[3], kLabelY, "OUTPUT", "TRIM");
        knob("output", kParamOutputVolume, 0.0f, 1.0f, x[3], kKnobY, 23.0f,
             false, true, "%+.1f", " dB", 40.0f, -20.0f, true, true,
             nullptr, "Post-effect output trim");
    }

    void drawModeSelector(ImDrawList* dl)
    {
        constexpr float x0 = 290.0f, y0 = 58.0f, x1 = 460.0f, y1 = 282.0f;
        dl->AddRectFilled(P(x0 + 2, y0 + 4), P(x1 + 3, y1 + 4),
                          IM_COL32(0, 0, 0, 90), 7.0f * s);
        brushedRect(dl, x0, y0, x1, y1,
                    IM_COL32(91, 122, 60, 255),
                    IM_COL32(61, 85, 37, 255), 7.0f);
        dl->AddRect(P(x0, y0), P(x1, y1), kColGreenDk,
                    7.0f * s, 0, 2.0f * s);
        dl->AddLine(P(x0 + 8, y0 + 4), P(x1 - 8, y0 + 4),
                    IM_COL32(191, 207, 163, 54), 0.9f * s);

        text(dl, 375, 68, 14.0f, kColWhite,
             "HEAD SELECT", 0, true);

        const float cx = 375, cy = 157, R = 36;
        const int mode = modeIndex();

        dl->AddCircleFilled(P(cx + 1.5f, cy + 3.5f), (R + 12.0f) * s,
                            IM_COL32(0, 0, 0, 95), 56);
        dl->AddCircleFilled(P(cx, cy), (R + 9.0f) * s,
                            IM_COL32(28, 37, 19, 255), 56);
        dl->AddCircleFilled(P(cx, cy - 1.2f), (R + 7.4f) * s,
                            IM_COL32(188, 187, 181, 255), 56);
        dl->AddCircleFilled(P(cx, cy + 0.5f), (R + 5.7f) * s,
                            IM_COL32(76, 77, 79, 255), 56);
        dl->AddCircleFilled(P(cx, cy + 1.0f), (R + 3.8f) * s,
                            IM_COL32(29, 38, 20, 255), 56);

        knob("mode", kParamMode, 1.0f, 12.0f, cx, cy, R, true, false,
             "%.0f", "", 1.0f, 0.0f, false, true, kModeShort[mode],
             "Playback-head routing");

        // Detent ticks + numbered arc. The tick ring sits in the gap between the
        // bezel and the numbers; the selected detent goes white along with its number.
        for (int i = 0; i < 12; ++i)
        {
            const float a = knobAngle((float)i / 11.0f);
            const ImVec2 dir(std::sin(a), -std::cos(a));
            const bool cur = (i == mode);
            const float t0 = cur ? 48.0f : 49.0f;
            const float t1 = cur ? 54.0f : 53.0f;
            dl->AddLine(P(cx + dir.x * t0, cy + dir.y * t0),
                        P(cx + dir.x * t1, cy + dir.y * t1),
                        cur ? kColWhite : IM_COL32(28, 41, 16, 255),
                        (cur ? 2.2f : 1.5f) * s);

            char num[4];
            std::snprintf(num, sizeof(num), "%d", i + 1);
            text(dl, cx + dir.x * 59.0f, cy + dir.y * 59.0f - 4.5f,
                 cur ? 12.0f : 10.0f,
                 cur ? kColWhite : IM_COL32(209, 220, 191, 255),
                 num, 0, cur);
        }

        dl->AddRectFilled(P(298, 213), P(452, 237),
                          IM_COL32(31, 46, 21, 210), 3.0f * s);
        dl->AddRect(P(298, 213), P(452, 237),
                    IM_COL32(126, 151, 94, 170), 3.0f * s, 0, 0.9f * s);
        text(dl, 375, 217.0f, 13.0f, kColWhite, kModeShort[mode], 0, true);

        drawHeadTimingStrip(dl, values[kParamTempoSync] > 0.5f);
    }

    void drawControlPanel(ImDrawList* dl)
    {
        constexpr float x0 = 470.0f, y0 = 58.0f, x1 = 888.0f, y1 = 282.0f;
        dl->AddRectFilled(P(x0 - 4, y0 - 3), P(x1 + 3, y1 + 4),
                          IM_COL32(0, 0, 0, 125), 7.0f * s);
        brushedRect(dl, x0 - 2, y0 - 2, x1 + 2, y1 + 2,
                    IM_COL32(190, 188, 183, 255),
                    IM_COL32(86, 86, 87, 255), 7.0f);
        dl->AddRectFilledMultiColor(
            P(x0 + 2, y0 + 2), P(x1 - 2, y1 - 2),
            IM_COL32(25, 25, 26, 255), IM_COL32(20, 20, 21, 255),
            IM_COL32(13, 13, 14, 255), IM_COL32(16, 16, 17, 255));
        dl->AddRect(P(x0 + 2, y0 + 2), P(x1 - 2, y1 - 2),
                    IM_COL32(4, 4, 5, 220), 5.0f * s, 0, 1.2f * s);

        const int mode = modeIndex();
        const bool reverbActive = mode >= 4;
        const bool echoActive = mode != 11;
        const char* const inactiveEcho = "Inactive in Reverb Only mode; value is preserved";
        const char* const inactiveReverb = "Inactive in this head mode; value is preserved";

        knobLabel(dl, 520, 64, "BASS");
        knob("bass", kParamBass, -1.0f, 1.0f, 520, 121, 25,
             false, true, "%+.1f", " dB", 17.0f, 0.0f, false, true, nullptr,
             "Echo-path bass shelf");
        knobLabel(dl, 620, 64, "TREBLE");
        knob("treble", kParamTreble, -1.0f, 1.0f, 620, 121, 25,
             false, true, "%+.1f", " dB", 17.0f, 0.0f, false, true, nullptr,
             "Echo-path treble shelf");
        knobLabel(dl, 720, 64, "REVERB LEVEL", nullptr, reverbActive);
        knob("reverbvol", kParamReverbLevel, 0.0f, 1.0f, 720, 121, 25,
             false, true, "%.0f", "%", 100.0f, 0.0f, false, reverbActive,
             nullptr, reverbActive ? "Spring reverb level" : inactiveReverb);
        knobLabel(dl, 830, 64, "REVERB PAN", nullptr, reverbActive);
        knob("reverbpan", kParamReverbPan, 0.0f, 1.0f, 830, 121, 25,
             false, true, "%+.0f", "", 200.0f, -100.0f, false, reverbActive,
             nullptr, reverbActive ? "-100 left / +100 right" : inactiveReverb);

        knobLabel(dl, 520, 169, "REPEAT RATE", nullptr, echoActive);
        const bool sync = values[kParamTempoSync] > 0.5f;
        if (sync) // knob steps through note divisions while synced
            knob("rate", kParamSyncDivision, 0.0f, (float)(kNumSyncDivisions - 1),
                 520, 229, 25, true, true, "%.0f", "", 1.0f, 0.0f, true,
                 echoActive, kSyncDivisions[divIndex()].name,
                 echoActive ? "Host-synced note division" : inactiveEcho);
        else
            knob("rate", kParamRepeatRate, 0.0f, 1.0f, 520, 229, 25,
                 false, true, "%.0f", "%", 100.0f, 0.0f, true, echoActive,
                 nullptr, echoActive ? "Tape motor speed" : inactiveEcho);

        knobLabel(dl, 620, 169, "INTENSITY", nullptr, echoActive);
        knob("intensity", kParamIntensity, 0.0f, 1.0f, 620, 229, 25,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, echoActive,
             nullptr, echoActive ? "Feedback; self-oscillates at high settings"
                                 : inactiveEcho);
        knobLabel(dl, 720, 169, "ECHO LEVEL", nullptr, echoActive);
        knob("echovol", kParamEchoLevel, 0.0f, 1.0f, 720, 229, 25,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, echoActive,
             nullptr, echoActive ? "Tape echo return level" : inactiveEcho);
        knobLabel(dl, 830, 169, "ECHO PAN", nullptr, echoActive);
        knob("echopan", kParamEchoPan, 0.0f, 1.0f, 830, 229, 25,
             false, true, "%+.0f", "", 200.0f, -100.0f, true, echoActive,
             nullptr, echoActive ? "-100 left / +100 right" : inactiveEcho);
    }

    void drawHeadTimingStrip(ImDrawList* dl, bool sync)
    {
        float motorMs = duskaudio::TapeEchoDSP::delayMsForRepeatRate(
            values[kParamRepeatRate]);
        bool timingAvailable = true;

        // Tempo sync octave-folds the selected division on the audio thread,
        // so the manual Repeat Rate parameter no longer describes the motor.
        // Read the effective target directly when the format is same-process.
        if (sync)
        {
            timingAvailable = false;
           #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
            if (tapeEchoGetHead1DelayMs != nullptr)
                if (void* const inst = getPluginInstancePointer())
                {
                    const float syncedMs = tapeEchoGetHead1DelayMs(inst);
                    if (syncedMs > 0.0f)
                    {
                        motorMs = syncedMs;
                        timingAvailable = true;
                    }
                }
           #endif
        }

        float slow01 =
            (motorMs - duskaudio::TapeEchoDSP::kMinDelayMs)
            / (duskaudio::TapeEchoDSP::kMaxDelayMs
               - duskaudio::TapeEchoDSP::kMinDelayMs);
        slow01 = slow01 < 0.0f ? 0.0f : (slow01 > 1.0f ? 1.0f : slow01);

        // Front-panel nominal ranges. The first-head fast label is intentionally
        // independent of the fixed mechanical head ratios, matching the visible
        // timing convention while the calibrated DSP retains its measured
        // audible arrival times.
        constexpr float kFastMs[3] = { 60.0f, 131.0f, 189.0f };
        constexpr float kSlowMs[3] = { 177.0f, 336.0f, 487.0f };
        // Bottom of the green head-select panel, three equal timing cells.
        constexpr float kCellX[4] =
            { 298.0f, 349.33333f, 400.66667f, 452.0f };
        constexpr float kStripY0 = 244.0f, kStripY1 = 276.0f;
        constexpr const char* kLabels[3] = { "HEAD 1", "HEAD 2", "HEAD 3" };
        constexpr uint8_t kHeadMask[12] =
            { 1, 2, 4, 6, 1, 2, 4, 3, 6, 5, 7, 0 };
        const uint8_t activeMask = kHeadMask[modeIndex()];

        dl->AddRectFilled(P(kCellX[0], kStripY0), P(kCellX[3], kStripY1),
                          IM_COL32(30, 42, 22, 255), 4.0f * s);
        dl->AddRect(P(kCellX[0], kStripY0), P(kCellX[3], kStripY1),
                    IM_COL32(111, 143, 85, 180), 4.0f * s, 0, 1.0f * s);

        for (int i = 0; i < 3; ++i)
        {
            const bool active = (activeMask & (1u << i)) != 0;
            const float cx = 0.5f * (kCellX[i] + kCellX[i + 1]);
            if (active)
                dl->AddRectFilled(
                    P(kCellX[i] + 1.0f, kStripY0 + 1.0f),
                    P(kCellX[i + 1] - 1.0f, kStripY1 - 1.0f),
                    IM_COL32(45, 62, 32, 255), 2.0f * s);
            if (i > 0)
                dl->AddLine(P(kCellX[i], kStripY0 + 2.0f), P(kCellX[i], kStripY1 - 2.0f),
                            IM_COL32(78, 101, 58, 255), 1.0f * s);

            text(dl, cx, 248.5f, 8.5f,
                 active ? kColWhiteDim : IM_COL32(140, 154, 126, 255),
                 kLabels[i], 0, active);

            char valueText[16];
            if (!active)
            {
                std::snprintf(valueText, sizeof(valueText), "---");
            }
            else if (timingAvailable)
            {
                const int delayMs = (int)std::lround(
                    kFastMs[i] + slow01 * (kSlowMs[i] - kFastMs[i]));
                std::snprintf(
                    valueText, sizeof(valueText), "%d ms", delayMs);
            }
            else
            {
                std::snprintf(valueText, sizeof(valueText), "-- ms");
            }
            regularText(dl, cx, 260, 10.0f,
                 active ? kColWhite : IM_COL32(150, 164, 136, 255),
                 valueText, 0);
        }
    }

    bool railToggle(ImDrawList* dl, const char* id, uint32_t param,
                    float cx, const char* label, bool invert = false,
                    bool stateLamp = true)
    {
        const bool on = invert ? values[param] < 0.5f : values[param] >= 0.5f;
        const ImVec2 hit0 = P(cx - 39, 292);
        const ImVec2 hit1 = P(cx + 39, 339);
        ImGui::SetCursorScreenPos(hit0);
        ImGui::InvisibleButton(id, ImVec2(hit1.x - hit0.x, hit1.y - hit0.y));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
            const float nv = invert ? (on ? 1.0f : 0.0f) : (on ? 0.0f : 1.0f);
            setP(param, nv);
        }

        text(dl, cx, 293.5f, 10.5f, kColRailInk, label, 0, true);
        const float baseX = cx, baseY = 321.0f;
        dl->AddCircleFilled(P(baseX + 1.0f, baseY + 1.8f), 9.0f * s,
                            IM_COL32(0, 0, 0, 85), 28);
        dl->AddCircleFilled(P(baseX, baseY), 8.0f * s,
                            IM_COL32(70, 69, 66, 255), 28);
        dl->AddCircleFilled(P(baseX, baseY - 0.7f), 6.3f * s,
                            hovered ? IM_COL32(211, 209, 201, 255)
                                    : IM_COL32(176, 174, 168, 255), 28);
        dl->AddCircleFilled(P(baseX, baseY), 3.5f * s,
                            IM_COL32(28, 28, 28, 255), 20);
        const float ex = baseX + (on ? 8.5f : -8.5f);
        const float ey = baseY + (on ? -6.0f : 5.0f);
        dl->AddLine(P(baseX + 0.8f, baseY + 1.0f), P(ex + 0.8f, ey + 1.2f),
                    IM_COL32(0, 0, 0, 100), 4.2f * s);
        dl->AddLine(P(baseX, baseY), P(ex, ey),
                    IM_COL32(205, 204, 199, 255), 3.2f * s);
        dl->AddCircleFilled(P(ex, ey), 3.2f * s,
                            IM_COL32(225, 224, 219, 255), 18);
        regularText(dl, cx - 20, 326, 7.5f,
                    on ? IM_COL32(88, 86, 82, 255) : kColRailInk, "OFF", 0);
        regularText(dl, cx + 20, 326, 7.5f,
                    on ? kColRailInk : IM_COL32(88, 86, 82, 255), "ON", 0);
        if (on && stateLamp)
        {
            dl->AddCircleFilled(P(cx + 29, 301), 4.0f * s, kColLedGlow, 16);
            dl->AddCircleFilled(P(cx + 29, 301), 2.0f * s, kColLedOn, 12);
        }
        return ImGui::IsItemClicked();
    }

    void drawUtilityRail(ImDrawList* dl)
    {
        dl->AddRectFilled(P(0, 287), P(kDesignW, 340), IM_COL32(4, 4, 5, 190));
        brushedRect(dl, 0, 291, kDesignW, 340,
                    IM_COL32(211, 209, 203, 255),
                    IM_COL32(126, 124, 120, 255));
        dl->AddLine(P(0, 291), P(kDesignW, 291),
                    IM_COL32(246, 244, 235, 100), 1.0f * s);
        dl->AddLine(P(0, 339), P(kDesignW, 339),
                    IM_COL32(20, 20, 20, 180), 1.0f * s);
        for (float x : { 172.0f, 328.0f, 490.0f, 646.0f, 775.0f })
            dl->AddLine(P(x, 295), P(x, 336),
                        IM_COL32(48, 47, 45, 110), 1.0f * s);

        railToggle(dl, "##rail_input", kParamInputSend, 92, "INPUT SEND");
        railToggle(dl, "##rail_solo", kParamWetSolo, 250, "WET SOLO");
        railToggle(dl, "##rail_sync", kParamTempoSync, 397, "TEMPO SYNC");

        // A tiny electromechanical-style division window gives sync state an
        // always-visible musical value while leaving the rate knob as the editor.
        dl->AddRectFilled(P(443, 306), P(482, 331),
                          IM_COL32(12, 20, 10, 255), 2.0f * s);
        dl->AddRect(P(443, 306), P(482, 331),
                    IM_COL32(53, 58, 49, 255), 2.0f * s, 0, 1.1f * s);
        regularText(dl, 462.5f, 312.5f, 10.0f,
                    values[kParamTempoSync] > 0.5f
                        ? IM_COL32(136, 230, 102, 255)
                        : IM_COL32(65, 82, 57, 255),
                    values[kParamTempoSync] > 0.5f
                        ? kSyncDivisions[divIndex()].name : "FREE", 0);

        // Loop splice remains momentary and is deliberately a pushbutton, not a
        // latching toggle, so its physical form communicates its behavior.
        const ImVec2 sp0 = P(520, 294), sp1 = P(616, 338);
        ImGui::SetCursorScreenPos(sp0);
        ImGui::InvisibleButton("##rail_splice", ImVec2(sp1.x - sp0.x, sp1.y - sp0.y));
        const bool pressed = ImGui::IsItemActive();
        if (ImGui::IsItemClicked())
        {
            editParameter(kParamLoopSplice, true);
            setParameterValue(kParamLoopSplice, 1.0f);
            loopSpliceReleasePending = true;
        }
        text(dl, 568, 293.5f, 10.5f, kColRailInk, "LOOP SPLICE", 0, true);
        dl->AddCircleFilled(P(568, 321.5f), 10.0f * s,
                            IM_COL32(55, 54, 52, 255), 28);
        dl->AddCircleFilled(P(568, pressed ? 322.5f : 320.5f), 7.2f * s,
                            pressed ? IM_COL32(127, 46, 35, 255)
                                    : IM_COL32(33, 33, 32, 255), 26);
        dl->AddCircle(P(568, 320.5f), 7.2f * s,
                      IM_COL32(211, 208, 198, 100), 26, 0.9f * s);

        // Tape Age stays a sound control, but its smaller rail treatment correctly
        // ranks it below Repeat/Intensity/Echo while keeping it always available.
        text(dl, 710, 293.5f, 10.5f, kColRailInk, "TAPE AGE", 0, true);
        knob("rail_age", kParamTapeAge, 0.0f, 1.0f, 710, 321, 13.5f,
             false, false, "%.0f", "%", 100.0f, 0.0f, false, true, nullptr,
             "Tape wear, hiss and transport instability");

        const bool on = values[kParamBypass] < 0.5f;
        railToggle(dl, "##rail_power", kParamBypass, 828, "POWER", true, false);
        led(dl, 862, 319, on, 5.0f);
    }

    int divIndex() const
    {
        int d = (int)(values[kParamSyncDivision] + 0.5f);
        return d < 0 ? 0 : (d >= kNumSyncDivisions ? kNumSyncDivisions - 1 : d);
    }

    int modeIndex() const
    {
        int m = (int)(values[kParamMode] + 0.5f) - 1;
        return m < 0 ? 0 : (m > 11 ? 11 : m);
    }

    static constexpr const char* kModeShort[12] =
    {
        "HEAD 1", "HEAD 2", "HEAD 3", "HEADS 2+3",
        "HEAD 1 + REV", "HEAD 2 + REV", "HEAD 3 + REV", "HEADS 1+2 + REV",
        "HEADS 2+3 + REV", "HEADS 1+3 + REV", "ALL HEADS + REV", "REVERB ONLY",
    };

    duskdpf::DuskPanel panel;
    duskdpf::DuskImGuiTextInputFocus textInputFocus;
    duskdpf::CrispFontSet labelFonts;
    duskdpf::CrispFontSet regularFonts;
    ImFont* labelFont = nullptr;
    float  values[kParamCount] = {};
    float  needlePos = 0.0f;
    float  meterLevel = 0.0f;
    int    currentPreset = -1;
    std::string currentUserName;   // non-empty when a user preset is active

    // Cached user preset library (file name + display name + every preset param).
    struct UserPreset { std::string name, path; float vals[kParamCount]; };
    std::vector<UserPreset> userPresets;
    char   saveBuf[64] = {};

    bool   showSupporters = false;
    bool   gripCursorSet = false;   // NWSE cursor currently pushed to the window
    bool   loopSpliceReleasePending = false;
    float  s = 1.0f;
    ImVec2 org = ImVec2(0, 0);

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoUI)
};

constexpr const char* TapeEchoUI::kModeShort[12];

UI* createUI()
{
    return new TapeEchoUI();
}

END_NAMESPACE_DISTRHO

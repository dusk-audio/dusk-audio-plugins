// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// Pre35UI.cpp — Dear ImGui UI for PRE-35, drawn in a fixed 640x380 design space
// and uniformly scaled (letterboxed) to whatever size the host gives the window.
//
// The panel follows the photographed Tascam M-35 channel: MIC ATT above the red
// TRIM control, a pair of warm horizontal meters, and the auxiliary controls in
// a separate bay. Every gesture, value readout and type-in editor still comes
// from the shared duskdpf::DuskPanel; only the hardware-style art is local.

#include "DistrhoUI.hpp"
#include "DistrhoPluginUtils.hpp"

#include "Pre35Access.hpp"
#include "Pre35Params.hpp"
#include "Pre35Version.hpp"

#include "DuskImGuiFont.hpp"
#include "DuskImGuiTextInput.hpp"
#include "DuskImGuiWidgets.hpp"
#include "DuskSupportersOverlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace
{
    constexpr float kDesignW = 640.0f;
    constexpr float kDesignH = 380.0f;

    // --- chassis ---------------------------------------------------------------
    constexpr float kHeaderH = 66.0f;

    // --- photographed channel module ------------------------------------------
    constexpr float PADX0 = 42.0f, PADX1 = 212.0f;
    constexpr float PADY0 = 108.0f, PADY1 = 142.0f;

    // --- knobs -----------------------------------------------------------------
    constexpr float TRIM_CX = 124.0f, TRIM_CY = 224.0f, TRIM_R = 56.0f;
    constexpr float IRON_CX = 436.0f, IRON_CY = 244.0f, IRON_R = 40.0f;

    // --- stereo meter pair -----------------------------------------------------
    constexpr float VULX0 = 250.0f, VULX1 = 430.0f;
    constexpr float VURX0 = 442.0f, VURX1 = 622.0f;
    constexpr float VUY0  = 82.0f,  VUY1  = 176.0f;

    // --- bottom rail -----------------------------------------------------------
    constexpr float RAILY0 = 308.0f, RAILY1 = 338.0f;
    constexpr float BYPX0 = 26.0f,  BYPX1 = 132.0f;
    constexpr float NOISEX0 = 242.0f, NOISEX1 = 358.0f;
    constexpr float MATCHX0 = 372.0f, MATCHX1 = 512.0f;

    // --- palette ---------------------------------------------------------------
    constexpr ImU32 kChassis   = IM_COL32(28, 28, 27, 255);
    constexpr ImU32 kPanelBg   = IM_COL32(60, 60, 54, 255);
    constexpr ImU32 kHeaderBg  = IM_COL32(16, 16, 16, 255);
    constexpr ImU32 kHairline  = IM_COL32(92, 92, 84, 255);
    constexpr ImU32 kInk       = IM_COL32(231, 224, 198, 255);
    constexpr ImU32 kInkDim    = IM_COL32(164, 159, 140, 255);
    constexpr ImU32 kFaceTrim  = IM_COL32(241, 71, 38, 255);
    constexpr ImU32 kFaceIron  = IM_COL32(116, 96, 70, 255);
    constexpr ImU32 kVuInk     = IM_COL32(33, 27, 23, 255);
    constexpr ImU32 kVuRed     = IM_COL32(157, 41, 40, 255);

    constexpr float kVuReferenceDbfs = -18.0f;
    constexpr float kVuScaleDb[] = { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, 0.0f, 3.0f };
    const char* const kVuScaleLbl[] = { "20", "10", "7", "5", "3", "0", "3" };
    constexpr int kVuScaleCount = (int)(sizeof(kVuScaleDb) / sizeof(kVuScaleDb[0]));
}

class Pre35UI : public UI, public duskdpf::ParamHost
{
public:
    Pre35UI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        // Ardour and other LV2 hosts expose their own container resize handle and
        // may omit the optional ui:resize feature. Do not show a grip that those
        // hosts cannot service; their outer window corner remains fully usable.
        const char* const format = getPluginFormatName();
        useInternalResizeGrip = format == nullptr || std::strcmp(format, "LV2") != 0;

        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kParamDefaults[i];

        setGeometryConstraints(kMinWidth,
                               (uint)std::lround((double)kMinWidth * kDesignH / kDesignW),
                               /*keepAspectRatio*/ true);

        // Multi-size atlas: the bold face at several native sizes spanning the
        // on-screen text range, so each label is drawn near-native (crisp).
        static const float kFontSizes[] = { 9.0f, 11.0f, 13.0f, 16.0f, 21.0f, 27.0f };
        fontSet = duskdpf::loadCrispFontSet(kFontSizes, 6, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
    }

    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override { setParameterValue(idx, v); }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount || ! std::isfinite(value))
            return;
        values[index] = value;
    }

    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth(), winH = (float)getHeight();

        // Uniform scale + letterbox: scale the whole design by the SMALLER of the
        // two ratios and centre it, so knobs and spacing can never grow apart.
        const float s = std::min(winW / kDesignW, winH / kDesignH);
        const ImVec2 org(0.5f * (winW - kDesignW * s), 0.5f * (winH - kDesignH * s));
        panel.begin(s, org, labelFont, this);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("PRE35", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), kChassis);

        // The overlay is modal: the controls under its scrim stay visible but must
        // not take click-through. Snapshot before drawHeader(), where a title
        // click can open it.
        const bool modalOpen = showSupporters;
        if (modalOpen)
            ImGui::BeginDisabled();

        drawChassis(dl);
        drawHeader(dl);
        drawPadSwitch(dl);
        drawKnobs(dl);
        drawMeters(dl);
        drawRail(dl);
        if (values[kBypass] > 0.5f)
        {
            dl->AddRectFilled(panel.P(8.0f, kHeaderH + 4.0f), panel.P(kDesignW - 8.0f, kDesignH - 8.0f),
                              IM_COL32(0, 0, 0, 96));
            panel.text(dl, 0.5f * kDesignW, 24.0f, 14.0f, IM_COL32(224, 220, 208, 210),
                       "BYPASSED", 0, true);
        }

        if (modalOpen)
            ImGui::EndDisabled();
        if (showSupporters)
            duskdpf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH, showSupporters,
                                           "PRE-35", PRE35_VERSION_STRING);

        // Own resize grip, submitted LAST so it wins ImGui's hover race and paints
        // over everything. AUv2 hosts (Logic) never provide a window grip of their
        // own; on VST3/CLAP the host's grip stays available alongside it. LV2 uses
        // the host frame because ui:resize is optional and Ardour does not offer it.
        duskdpf::ResizeGripState grip;
        if (useInternalResizeGrip)
            grip = panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH, kMinScale);

        ImGui::End();
        ImGui::PopStyleVar(2);
        textInputFocus.update(*this);

        // Cursor feedback: the DPF-Widgets ImGui backend never forwards
        // ImGui::SetMouseCursor() to the window, so drive DGL's cursor directly.
        // Edge-triggered — setCursor() is a window call, not a per-frame one.
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
    // Minimum window width in device pixels, and the matching scale floor the grip
    // is clamped to. These two MUST agree or the grip and the host constraint fight.
    static constexpr uint kMinWidth = 480;
    static constexpr float kMinScale = (float)kMinWidth / kDesignW;

    float sc() const { return panel.scale(); }

    //==========================================================================
    void drawChassis(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, kHeaderH), panel.P(kDesignW, kDesignH), kPanelBg);

        // Fixed, low-contrast speckle gives the powder-coated panel some depth
        // without a bitmap or frame-to-frame randomness.
        for (int i = 0; i < 190; ++i)
        {
            const float x = 8.0f + (float)((i * 83) % 624);
            const float y = 70.0f + (float)((i * 47) % 226);
            const ImU32 c = (i & 1) ? IM_COL32(255, 255, 236, 11)
                                    : IM_COL32(0, 0, 0, 14);
            dl->AddCircleFilled(panel.P(x, y), (0.35f + 0.18f * (float)(i % 3)) * sc(), c, 6);
        }

        // The photographed input strip is a separate, silver-edged module.
        dl->AddRectFilled(panel.P(16, 76), panel.P(232, 298), IM_COL32(49, 49, 45, 255), 3.0f * sc());
        dl->AddRect(panel.P(16, 76), panel.P(232, 298), IM_COL32(168, 167, 154, 210),
                    3.0f * sc(), 0, 1.2f * sc());
        dl->AddLine(panel.P(21, 78), panel.P(21, 296), IM_COL32(201, 199, 184, 145), 1.0f * sc());
        dl->AddLine(panel.P(227, 78), panel.P(227, 296), IM_COL32(12, 12, 12, 190), 1.0f * sc());

        // Auxiliary controls sit in the larger right-hand service bay.
        dl->AddRectFilled(panel.P(242, 76), panel.P(628, 298), IM_COL32(48, 48, 44, 225), 4.0f * sc());
        dl->AddRect(panel.P(242, 76), panel.P(628, 298), kHairline, 4.0f * sc(), 0, 1.0f * sc());
    }

    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, kHeaderH), kHeaderBg);
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 3), IM_COL32(150, 150, 152, 255));
        dl->AddLine(panel.P(0, kHeaderH), panel.P(kDesignW, kHeaderH), kHairline, 1.5f * sc());

        panel.text(dl, 24, 16, 26.0f, kInk, "PRE-35", -1, true);
        panel.text(dl, 26, 46, 10.5f, kInkDim, "M-35 CONSOLE MICROPHONE PREAMPLIFIER", -1);
        panel.text(dl, kDesignW - 24, 26, 10.5f, kInkDim, "DUSK AUDIO", 1, true);
        panel.text(dl, kDesignW - 24, 42, 9.5f, IM_COL32(108, 110, 114, 255),
                   "v" PRE35_VERSION_STRING, 1);

        // Clickable title -> Patreon supporters overlay (fleet convention).
        const ImVec2 t0 = panel.P(20, 10), t1 = panel.P(160, 42);
        ImGui::SetCursorScreenPos(t0);
        ImGui::InvisibleButton("titlecredits", ImVec2(t1.x - t0.x, t1.y - t0.y));
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) showSupporters = true;
    }

    //==========================================================================
    // Three-position MIC ATT switch. The slot and ridged cap mirror the hardware,
    // but each position remains its own latching hit target so no in-between host
    // value can ever be emitted.
    void drawPadSwitch(ImDrawList* dl)
    {
        panel.text(dl, 31.0f, 87.0f, 12.0f, kInk, "MIC ATT", -1, true);
        panel.text(dl, 214.0f, 96.0f, 8.0f, kInkDim, "dB", 1);

        static constexpr float kPosX[kNumPads] = { 61.0f, 127.0f, 193.0f };
        static constexpr const char* kLabels[kNumPads] = { "0", "20", "40" };

        // NOT a control any more. The pad is a tombstone forced to 0 dB
        // (Pre35Params.hpp has the measurements): engaging it costs +20 or +40 dB
        // of hiss and pushes the amp's clip point out of reach, which disables
        // Trim as a drive control. The switch stays on the faceplate because the
        // M-35 has one and drawing it at its real position is truthful, but it no
        // longer takes clicks and no longer writes the parameter.
        const int sel = 0;
        for (int i = 0; i < kNumPads; ++i)
            panel.text(dl, kPosX[i], 98.0f, 10.0f,
                       i == sel ? kInk : kInkDim, kLabels[i], 0, i == sel);

        dl->AddRectFilled(panel.P(PADX0, PADY0), panel.P(PADX1, PADY1), IM_COL32(19, 19, 18, 255),
                          2.0f * sc());
        dl->AddRect(panel.P(PADX0, PADY0), panel.P(PADX1, PADY1), IM_COL32(8, 8, 8, 255),
                    2.0f * sc(), 0, 2.0f * sc());

        const float capX = kPosX[sel];
        dl->AddRectFilled(panel.P(capX - 23.0f, PADY0 + 3.0f), panel.P(capX + 23.0f, PADY1 - 3.0f),
                          IM_COL32(8, 8, 8, 255), 3.0f * sc());
        dl->AddRect(panel.P(capX - 23.0f, PADY0 + 3.0f), panel.P(capX + 23.0f, PADY1 - 3.0f),
                    IM_COL32(79, 78, 72, 210), 3.0f * sc(), 0, 1.0f * sc());
        for (int ridge = -2; ridge <= 2; ++ridge)
        {
            const float x = capX + (float)ridge * 7.0f;
            dl->AddLine(panel.P(x, PADY0 + 6.0f), panel.P(x, PADY1 - 6.0f),
                        ridge < 0 ? IM_COL32(65, 65, 61, 180) : IM_COL32(0, 0, 0, 190),
                        1.2f * sc());
        }
    }

    //==========================================================================
    static float knobAngle(float value, float minValue, float maxValue)
    {
        const float range = maxValue - minValue;
        float t = range > 0.0f ? (value - minValue) / range : 0.0f;
        t = std::max(0.0f, std::min(1.0f, t));
        return (-135.0f + 270.0f * t) * duskdpf::DuskPanel::kPi / 180.0f;
    }

    void drawKnobScale(ImDrawList* dl, float cx, float cy, float radius, bool numbered)
    {
        for (int i = 0; i <= 10; ++i)
        {
            const float a = (-135.0f + 27.0f * (float)i) * duskdpf::DuskPanel::kPi / 180.0f;
            const float dx = std::sin(a), dy = -std::cos(a);
            dl->AddLine(panel.P(cx + dx * (radius + 3.0f), cy + dy * (radius + 3.0f)),
                        panel.P(cx + dx * (radius + 7.0f), cy + dy * (radius + 7.0f)),
                        kInkDim, 1.1f * sc());
            if (numbered)
            {
                char label[4];
                std::snprintf(label, sizeof(label), "%d", i);
                panel.text(dl, cx + dx * (radius + 16.0f), cy + dy * (radius + 16.0f) - 4.0f,
                           8.0f, kInk, label, 0, i == 0 || i == 5 || i == 10);
            }
        }
    }

    void drawHardwareKnob(ImDrawList* dl, float cx, float cy, float radius,
                          float value, float minValue, float maxValue, ImU32 face)
    {
        const ImVec2 c = panel.P(cx, cy);
        const float r = radius * sc();

        dl->AddCircleFilled(panel.P(cx + 3.0f, cy + 4.0f), r * 1.04f, IM_COL32(0, 0, 0, 125), 48);
        dl->AddCircleFilled(c, r, IM_COL32(13, 13, 13, 255), 48);
        dl->AddCircleFilled(c, r * 0.88f, IM_COL32(45, 44, 41, 255), 48);
        dl->AddCircleFilled(c, r * 0.70f, face, 48);
        dl->AddCircleFilled(panel.P(cx - radius * 0.16f, cy - radius * 0.20f),
                            r * 0.45f, IM_COL32(255, 245, 224, 24), 32);
        dl->AddCircle(c, r * 0.70f, IM_COL32(0, 0, 0, 145), 48, 1.2f * sc());
        dl->AddCircle(c, r, IM_COL32(3, 3, 3, 255), 48, 1.4f * sc());

        const float a = knobAngle(value, minValue, maxValue);
        const float dx = std::sin(a), dy = -std::cos(a);
        const float px = -dy, py = dx;
        dl->AddLine(panel.P(cx + dx * radius * 0.10f + px * 2.0f,
                            cy + dy * radius * 0.10f + py * 2.0f),
                    panel.P(cx + dx * radius * 0.80f + px * 2.0f,
                            cy + dy * radius * 0.80f + py * 2.0f),
                    IM_COL32(24, 20, 18, 230), 7.0f * sc());
        dl->AddLine(panel.P(cx + dx * radius * 0.12f - px * 1.0f,
                            cy + dy * radius * 0.12f - py * 1.0f),
                    panel.P(cx + dx * radius * 0.78f - px * 1.0f,
                            cy + dy * radius * 0.78f - py * 1.0f),
                    IM_COL32(255, 231, 207, 175), 2.2f * sc());
        dl->AddCircleFilled(c, r * 0.08f, IM_COL32(24, 20, 18, 235), 16);
    }

    void drawKnobs(ImDrawList* dl)
    {
        panel.text(dl, 31.0f, 150.0f, 12.0f, kInk, "TRIM", -1, true);
        drawKnobScale(dl, TRIM_CX, TRIM_CY, TRIM_R, true);
        drawHardwareKnob(dl, TRIM_CX, TRIM_CY, TRIM_R,
                         values[kTrim], kParamMin[kTrim], kParamMax[kTrim], kFaceTrim);
        char trimReadout[32];
        std::snprintf(trimReadout, sizeof(trimReadout), "%.1f  (%.0f%%)",
                      values[kTrim] * 0.1f, values[kTrim]);
        panel.knob("trim", kTrim, kParamMin[kTrim], kParamMax[kTrim],
                   TRIM_CX, TRIM_CY, TRIM_R, values[kTrim], kParamDefaults[kTrim],
                   /*stepped*/ false, /*panelTicks*/ false, "%.1f", "", 0,
                   /*bodyless*/ true, /*persistent*/ true,
                   "Preamp trim. Increases downstream amplifier drive; output level is matched automatically.",
                   /*rightClickReset*/ false, 0.1f, 0.0f, "Trim", /*contextMenu*/ true,
                   trimReadout);

        panel.knobLabel(dl, IRON_CX, 188.0f, "IRON");
        drawKnobScale(dl, IRON_CX, IRON_CY, IRON_R, false);
        drawHardwareKnob(dl, IRON_CX, IRON_CY, IRON_R,
                         values[kIron], kParamMin[kIron], kParamMax[kIron], kFaceIron);
        panel.knob("iron", kIron, kParamMin[kIron], kParamMax[kIron],
                   IRON_CX, IRON_CY, IRON_R, values[kIron], kParamDefaults[kIron],
                   false, false, "%.0f", " %", 0, true, true,
                   "Input transformer amount. MIC ATT sets its input level; 100 % is the measured device.",
                   false, 1.0f, 0.0f, "Iron", true);
    }

    //==========================================================================
    static float vuAngle(float db)
    {
        // A real VU face is voltage-linear, so its dB marks bunch toward the
        // negative end instead of being evenly spaced around the arc.
        db = std::max(-20.0f, std::min(3.0f, db));
        const float low = std::pow(10.0f, -20.0f / 20.0f);
        const float high = std::pow(10.0f, 3.0f / 20.0f);
        const float t = (std::pow(10.0f, db / 20.0f) - low) / (high - low);
        return -2.35f + t * 1.70f;
    }

    void drawVuFace(ImDrawList* dl, float x0, float x1, float linear, const char* channel)
    {
        dl->AddRectFilled(panel.P(x0, VUY0), panel.P(x1, VUY1), IM_COL32(7, 7, 7, 255),
                          4.0f * sc());
        dl->AddRect(panel.P(x0, VUY0), panel.P(x1, VUY1), IM_COL32(112, 110, 99, 180),
                    4.0f * sc(), 0, 1.0f * sc());

        const float ix0 = x0 + 7.0f, ix1 = x1 - 7.0f;
        const float iy0 = VUY0 + 7.0f, iy1 = VUY1 - 7.0f;
        dl->AddRectFilled(panel.P(ix0, iy0), panel.P(ix1, iy1), IM_COL32(209, 176, 130, 255),
                          2.0f * sc());
        dl->AddRectFilledMultiColor(panel.P(ix0 + 1.0f, iy0 + 1.0f), panel.P(ix1 - 1.0f, iy1 - 1.0f),
                                    IM_COL32(133, 112, 91, 255), IM_COL32(133, 112, 91, 255),
                                    IM_COL32(239, 203, 150, 255), IM_COL32(239, 203, 150, 255));

        const float pivotX = 0.5f * (x0 + x1);
        const float pivotY = VUY1 - 8.0f;
        const float radius = 0.42f * (x1 - x0);
        const ImVec2 pivot = panel.P(pivotX, pivotY);

        dl->PathArcTo(pivot, radius * sc(), vuAngle(-20.0f), vuAngle(0.0f), 30);
        dl->PathStroke(kVuInk, 0, 1.6f * sc());
        dl->PathArcTo(pivot, radius * sc(), vuAngle(0.0f), vuAngle(3.0f), 14);
        dl->PathStroke(kVuRed, 0, 2.3f * sc());

        for (int i = 0; i < kVuScaleCount; ++i)
        {
            const float a = vuAngle(kVuScaleDb[i]);
            const float dx = std::cos(a), dy = std::sin(a);
            const ImU32 col = kVuScaleDb[i] >= 0.0f ? kVuRed : kVuInk;
            dl->AddLine(panel.P(pivotX + dx * (radius - 8.0f), pivotY + dy * (radius - 8.0f)),
                        panel.P(pivotX + dx * (radius + 1.0f), pivotY + dy * (radius + 1.0f)),
                        col, 1.4f * sc());
            panel.text(dl, pivotX + dx * (radius - 18.0f), pivotY + dy * (radius - 18.0f) - 4.0f,
                       8.5f, col, kVuScaleLbl[i], 0, i == 5);

            if (i + 1 < kVuScaleCount)
            {
                const float nextA = vuAngle(kVuScaleDb[i + 1]);
                for (int minor = 1; minor <= 2; ++minor)
                {
                    const float ma = a + (nextA - a) * (float)minor / 3.0f;
                    const float mdx = std::cos(ma), mdy = std::sin(ma);
                    const ImU32 mcol = i >= 5 ? kVuRed : kVuInk;
                    dl->AddLine(panel.P(pivotX + mdx * (radius - 4.0f), pivotY + mdy * (radius - 4.0f)),
                                panel.P(pivotX + mdx * (radius + 1.0f), pivotY + mdy * (radius + 1.0f)),
                                mcol, 0.9f * sc());
                }
            }
        }

        panel.text(dl, x0 + 19.0f, VUY0 + 10.0f, 9.0f, kVuInk, "-", 0, true);
        panel.text(dl, x1 - 19.0f, VUY0 + 10.0f, 9.0f, kVuRed, "+", 0, true);
        panel.text(dl, pivotX, VUY1 - 39.0f, 13.0f, kVuInk, "VU", 0, true);
        panel.text(dl, x0 + 18.0f, VUY1 - 18.0f, 8.5f, kVuInk, channel, 0, true);

        const float dbfs = 20.0f * std::log10(std::max(linear, 1.0e-6f));
        const float needleA = vuAngle(dbfs - kVuReferenceDbfs);
        const float ndx = std::cos(needleA), ndy = std::sin(needleA);
        dl->AddLine(panel.P(pivotX + 1.0f, pivotY + 1.0f),
                    panel.P(pivotX + ndx * (radius - 2.0f) + 1.0f,
                            pivotY + ndy * (radius - 2.0f) + 1.0f),
                    IM_COL32(0, 0, 0, 90), 2.2f * sc());
        dl->AddLine(pivot, panel.P(pivotX + ndx * (radius - 2.0f),
                                   pivotY + ndy * (radius - 2.0f)),
                    IM_COL32(31, 28, 25, 255), 1.5f * sc());
        dl->AddCircleFilled(pivot, 5.0f * sc(), IM_COL32(67, 55, 45, 255), 20);
        dl->AddCircleFilled(panel.P(pivotX - 1.0f, pivotY - 1.0f), 2.7f * sc(),
                            IM_COL32(206, 176, 134, 255), 16);

        const bool peak = linear >= 1.0f;
        const float peakX = x1 - 18.0f, peakY = VUY1 - 19.0f;
        if (peak)
            dl->AddCircleFilled(panel.P(peakX, peakY), 7.0f * sc(), IM_COL32(255, 40, 25, 60), 18);
        dl->AddCircleFilled(panel.P(peakX, peakY), 4.0f * sc(),
                            peak ? IM_COL32(211, 40, 30, 255) : IM_COL32(81, 26, 23, 255), 18);
        panel.text(dl, peakX, VUY1 - 34.0f, 7.5f, kVuRed, "PEAK", 0, true);
    }

    // The DSP publishes linear peak with a ~300 ms release. The native meter art
    // maps that signal onto a -18 dBFS DAW reference without adding a
    // second UI smoother, so the telemetry and host fallback remain unchanged.
    void drawMeters(ImDrawList* dl)
    {
        float l = values[kOutPeakL];
        float r = values[kOutPeakR];
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        // Weak symbols: null in a split LV2 UI, which keeps the output parameters.
        if (pre35GetOutPeakL != nullptr)
            if (void* const inst = getPluginInstancePointer())
                l = pre35GetOutPeakL(inst);
        if (pre35GetOutPeakR != nullptr)
            if (void* const inst = getPluginInstancePointer())
                r = pre35GetOutPeakR(inst);
       #endif

        drawVuFace(dl, VULX0, VULX1, l, "L");
        drawVuFace(dl, VURX0, VURX1, r, "R");
        panel.text(dl, 0.5f * (VULX0 + VURX1), VUY1 + 2.0f, 9.0f, kInkDim,
                   "PEAK RESPONSE | 0 VU = -18 dBFS", 0, true);
    }

    //==========================================================================
    void drawRail(ImDrawList* dl)
    {
        panel.toggle("bypass", kBypass, BYPX0, RAILY0, BYPX1, RAILY1, values[kBypass], "BYPASS");
        panel.toggle("noise", kNoise, NOISEX0, RAILY0, NOISEX1, RAILY1, values[kNoise], "NOISE");
        dl->AddRectFilled(panel.P(MATCHX0, RAILY0), panel.P(MATCHX1, RAILY1),
                          IM_COL32(36, 37, 35, 255), 3.0f * sc());
        dl->AddRect(panel.P(MATCHX0, RAILY0), panel.P(MATCHX1, RAILY1), kHairline,
                    3.0f * sc(), 0, 1.0f * sc());
        panel.text(dl, 0.5f * (MATCHX0 + MATCHX1), RAILY0 + 8.0f, 9.0f, kInkDim,
                   "LEVEL MATCHED", 0, true);
        panel.text(dl, kDesignW - 24.0f, RAILY0 + 9.0f, 9.0f, IM_COL32(104, 106, 110, 255),
                   "TASCAM M-35 MODEL", 1);
    }

    //==========================================================================
    duskdpf::DuskPanel  panel;
    duskdpf::CrispFontSet fontSet;
    duskdpf::DuskImGuiTextInputFocus textInputFocus;
    ImFont* labelFont = nullptr;

    float values[kParamCount] = {};
    bool  showSupporters = false;   // Patreon supporters overlay (title click)
    bool  useInternalResizeGrip = true;
    bool  gripCursorSet  = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pre35UI)
};

UI* createUI() { return new Pre35UI(); }

END_NAMESPACE_DISTRHO

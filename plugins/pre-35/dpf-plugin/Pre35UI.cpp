// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// Pre35UI.cpp — Dear ImGui UI for PRE-35, drawn in a fixed 640x380 design space
// and uniformly scaled (letterboxed) to whatever size the host gives the window.
//
// Layout, left to right: a three-position PAD switch, the large TRIM knob, the
// IRON and OUTPUT knobs, a segmented stereo output meter, and a bottom rail with
// BYPASS / NOISE / AUTO GAIN. Chrome only — every gesture, the value bubble and
// the type-in editor come from the shared duskdpf::DuskPanel, and the supporters
// overlay from the shared DuskSupportersOverlay.
//
// PHASE 3 (polish) NOTE: this is a functional layout, not a finished front panel.
// The geometry constants below are all in one block for exactly that reason.

#include "DistrhoUI.hpp"

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

START_NAMESPACE_DISTRHO

namespace
{
    constexpr float kDesignW = 640.0f;
    constexpr float kDesignH = 380.0f;

    // --- chassis ---------------------------------------------------------------
    constexpr float kHeaderH = 66.0f;

    // --- pad switch ------------------------------------------------------------
    constexpr float PADX0 = 26.0f, PADX1 = 132.0f;
    constexpr float PADY0 = 108.0f, PADH = 38.0f, PADGAP = 6.0f;

    // --- knobs -----------------------------------------------------------------
    constexpr float TRIM_CX = 242.0f, TRIM_CY = 178.0f, TRIM_R = 56.0f;
    constexpr float IRON_CX = 378.0f, IRON_CY = 170.0f, IRON_R = 40.0f;
    constexpr float OUT_CX  = 486.0f, OUT_CY  = 170.0f, OUT_R  = 40.0f;

    // --- meter -----------------------------------------------------------------
    constexpr float METX0 = 560.0f, METY0 = 100.0f, METX1 = 624.0f, METY1 = 296.0f;
    constexpr float kMeterMinDb = -48.0f, kMeterMaxDb = 6.0f;

    // --- bottom rail -----------------------------------------------------------
    constexpr float RAILY0 = 308.0f, RAILY1 = 338.0f;
    constexpr float BYPX0 = 26.0f,  BYPX1 = 132.0f;
    constexpr float NOISEX0 = 242.0f, NOISEX1 = 358.0f;
    constexpr float AGX0 = 372.0f,  AGX1 = 512.0f;

    // --- palette ---------------------------------------------------------------
    constexpr ImU32 kChassis  = IM_COL32(30, 30, 33, 255);
    constexpr ImU32 kPanelBg  = IM_COL32(38, 37, 40, 255);
    constexpr ImU32 kHeaderBg = IM_COL32(18, 18, 20, 255);
    constexpr ImU32 kHairline = IM_COL32(62, 62, 66, 255);
    constexpr ImU32 kInk      = IM_COL32(238, 236, 228, 255);
    constexpr ImU32 kInkDim   = IM_COL32(150, 152, 156, 255);
    // Knob faces: warm oxblood for the gain stage, steel for the tone/level pair.
    constexpr ImU32 kFaceTrim = IM_COL32(126, 58, 48, 255);
    constexpr ImU32 kFaceIron = IM_COL32(92, 78, 56, 255);
    constexpr ImU32 kFaceOut  = IM_COL32(58, 64, 76, 255);

    constexpr ImU32 kMetGreen = IM_COL32(74, 182, 102, 255);
    constexpr ImU32 kMetAmber = IM_COL32(214, 168, 56, 255);
    constexpr ImU32 kMetRed   = IM_COL32(226, 66, 46, 255);
    constexpr ImU32 kMetOff   = IM_COL32(46, 48, 50, 255);

    constexpr int   kMeterSegments = 24;
    constexpr float kMeterScaleDb[] = { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f };
    const     char* kMeterScaleLbl[] = { "+6", "0", "-6", "-12", "-24", "-36", "-48" };
    constexpr int   kMeterScaleCount = (int)(sizeof(kMeterScaleDb) / sizeof(kMeterScaleDb[0]));
}

class Pre35UI : public UI, public duskdpf::ParamHost
{
public:
    Pre35UI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
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
        drawMeter(dl);
        drawRail(dl);
        if (values[kBypass] > 0.5f)
        {
            dl->AddRectFilled(panel.P(8.0f, kHeaderH + 4.0f), panel.P(kDesignW - 8.0f, kDesignH - 8.0f),
                              IM_COL32(0, 0, 0, 96));
            panel.text(dl, 0.5f * kDesignW, 186.0f, 20.0f, IM_COL32(224, 220, 208, 180),
                       "BYPASSED", 0, true);
        }

        if (modalOpen)
            ImGui::EndDisabled();
        if (showSupporters)
            duskdpf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH, showSupporters,
                                           "PRE-35", PRE35_VERSION_STRING);

        // Own resize grip, submitted LAST so it wins ImGui's hover race and paints
        // over everything. AUv2 hosts (Logic) never provide a window grip of their
        // own; on VST3/CLAP the host's grip stays available alongside it.
        const duskdpf::ResizeGripState grip =
            panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH, kMinScale);

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
    static constexpr uint kMinWidth = 420;
    static constexpr float kMinScale = (float)kMinWidth / kDesignW;

    float sc() const { return panel.scale(); }

    //==========================================================================
    void drawChassis(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, kHeaderH), panel.P(kDesignW, kDesignH), kPanelBg);
        // Section wells: the control field and the meter bay.
        dl->AddRectFilled(panel.P(12, kHeaderH + 12), panel.P(544, 296), IM_COL32(33, 33, 36, 255), 6.0f * sc());
        dl->AddRect      (panel.P(12, kHeaderH + 12), panel.P(544, 296), kHairline, 6.0f * sc(), 0, 1.2f * sc());
        dl->AddRectFilled(panel.P(552, kHeaderH + 12), panel.P(kDesignW - 12, 296), IM_COL32(26, 26, 28, 255), 6.0f * sc());
        dl->AddRect      (panel.P(552, kHeaderH + 12), panel.P(kDesignW - 12, 296), kHairline, 6.0f * sc(), 0, 1.2f * sc());
    }

    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, kHeaderH), kHeaderBg);
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 3), IM_COL32(150, 150, 152, 255));
        dl->AddLine(panel.P(0, kHeaderH), panel.P(kDesignW, kHeaderH), kHairline, 1.5f * sc());

        panel.text(dl, 24, 16, 26.0f, kInk, "PRE-35", -1, true);
        panel.text(dl, 26, 46, 10.5f, kInkDim, "Console Mic Preamp", -1);
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
    // Three-position PAD switch. Written as three latching cells rather than a
    // knob: it is a switch on the hardware and a restricted enumeration in the
    // host, and a rotary would invite drags to values that do not exist.
    void drawPadSwitch(ImDrawList* dl)
    {
        panel.text(dl, 0.5f * (PADX0 + PADX1), 86.0f, 11.0f, kInk, "PAD", 0, true);

        const int sel = (int)std::lround(values[kPad]);
        for (int i = 0; i < kNumPads; ++i)
        {
            const float y0 = PADY0 + (float)i * (PADH + PADGAP);
            const float y1 = y0 + PADH;
            const bool on = (i == sel);

            char id[24];
            std::snprintf(id, sizeof(id), "pad%d", i);
            const ImVec2 b0 = panel.P(PADX0, y0), b1 = panel.P(PADX1, y1);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            const bool hov = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked() && ! on)
            {
                editParameter(kPad, true);
                values[kPad] = (float)i;
                setParameterValue(kPad, values[kPad]);
                editParameter(kPad, false);
            }

            dl->AddRectFilled(b0, b1, on ? IM_COL32(56, 52, 50, 255) : IM_COL32(40, 40, 43, 255), 4.0f * sc());
            dl->AddRect(b0, b1,
                        on  ? IM_COL32(200, 92, 62, 235)
                            : (hov ? IM_COL32(150, 150, 155, 200) : IM_COL32(84, 84, 88, 200)),
                        4.0f * sc(), 0, 1.4f * sc());
            panel.led(dl, PADX0 + 15.0f, 0.5f * (y0 + y1), on, 4.2f);
            panel.text(dl, PADX0 + 32.0f, 0.5f * (y0 + y1) - 6.5f, 12.5f,
                       on ? kInk : kInkDim, kPadLabels[i], -1, on);
        }

        panel.text(dl, 0.5f * (PADX0 + PADX1), 232.0f, 9.0f, IM_COL32(112, 114, 118, 255),
                   "AHEAD OF THE IRON", 0);
    }

    //==========================================================================
    void drawKnobs(ImDrawList* dl)
    {
        panel.knobLabel(dl, TRIM_CX, 94.0f, "TRIM");
        panel.knob("trim", kTrim, kParamMin[kTrim], kParamMax[kTrim],
                   TRIM_CX, TRIM_CY, TRIM_R, values[kTrim], kParamDefaults[kTrim],
                   /*stepped*/ false, /*panelTicks*/ true, "%.0f", " %", kFaceTrim,
                   /*bodyless*/ false, /*persistent*/ true,
                   "Preamp trim. Sets the amp gain and the transformer drive together.",
                   /*rightClickReset*/ false, 1.0f, 0.0f, "Trim", /*contextMenu*/ true);

        panel.knobLabel(dl, IRON_CX, 102.0f, "IRON");
        panel.knob("iron", kIron, kParamMin[kIron], kParamMax[kIron],
                   IRON_CX, IRON_CY, IRON_R, values[kIron], kParamDefaults[kIron],
                   false, true, "%.0f", " %", kFaceIron, false, true,
                   "Input transformer amount. 0 % bypasses it, 100 % is the measured device.",
                   false, 1.0f, 0.0f, "Iron", true);

        panel.knobLabel(dl, OUT_CX, 102.0f, "OUTPUT");
        panel.knob("output", kOutput, kParamMin[kOutput], kParamMax[kOutput],
                   OUT_CX, OUT_CY, OUT_R, values[kOutput], kParamDefaults[kOutput],
                   false, true, "%+.1f", " dB", kFaceOut, false, true,
                   "Output trim, applied after the whole chain.",
                   false, 1.0f, 0.0f, "Output", true);
    }

    //==========================================================================
    // Segmented stereo peak meter. The DSP publishes a linear peak with a ~300 ms
    // release, so nothing is smoothed again here — a second visual smoother would
    // make the display lag the meter and vary with the frame rate.
    void drawMeter(ImDrawList* dl)
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

        panel.text(dl, 0.5f * (METX0 + METX1), 86.0f, 11.0f, kInk, "OUTPUT", 0, true);

        dl->AddRectFilled(panel.P(METX0 - 4, METY0 - 4), panel.P(METX1 + 4, METY1 + 4),
                          IM_COL32(12, 12, 14, 255), 4.0f * sc());

        // dB scale, right-aligned just left of the bars.
        for (int i = 0; i < kMeterScaleCount; ++i)
        {
            const float y = dbToY(kMeterScaleDb[i]);
            panel.text(dl, METX0 - 8.0f, y - 5.0f, 8.5f, IM_COL32(120, 122, 126, 255),
                       kMeterScaleLbl[i], 1);
            dl->AddLine(panel.P(METX0 - 5.0f, y), panel.P(METX0 - 1.0f, y),
                        IM_COL32(96, 98, 102, 255), 1.0f * sc());
        }

        const float barW = 0.5f * (METX1 - METX0) - 5.0f;
        drawBar(dl, METX0 + 2.0f, METX0 + 2.0f + barW, l);
        drawBar(dl, METX1 - 2.0f - barW, METX1 - 2.0f, r);
        panel.text(dl, METX0 + 2.0f + 0.5f * barW, METY1 + 6.0f, 9.0f, kInkDim, "L", 0);
        panel.text(dl, METX1 - 2.0f - 0.5f * barW, METY1 + 6.0f, 9.0f, kInkDim, "R", 0);
    }

    float dbToY(float db) const
    {
        const float t = (db - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb);
        return METY1 - std::min(std::max(t, 0.0f), 1.0f) * (METY1 - METY0);
    }

    void drawBar(ImDrawList* dl, float x0, float x1, float linear)
    {
        const float db = 20.0f * std::log10(std::max(linear, 1.0e-6f));
        const float segH = (METY1 - METY0) / (float)kMeterSegments;

        for (int i = 0; i < kMeterSegments; ++i)
        {
            // Segment i spans the dB band it occupies; lit once the level reaches
            // its lower edge, so the topmost lit segment IS the current reading.
            const float t0 = (float)i / (float)kMeterSegments;
            const float segDb = kMeterMinDb + t0 * (kMeterMaxDb - kMeterMinDb);
            const bool on = db >= segDb;

            const float y1 = METY1 - (float)i * segH - 1.0f;
            const float y0 = y1 - segH + 1.0f;
            const ImU32 col = ! on ? kMetOff
                            : (segDb >= 0.0f ? kMetRed : (segDb >= -6.0f ? kMetAmber : kMetGreen));
            dl->AddRectFilled(panel.P(x0, y0), panel.P(x1, y1), col, 1.0f * sc());
        }
    }

    //==========================================================================
    void drawRail(ImDrawList* dl)
    {
        panel.toggle("bypass", kBypass, BYPX0, RAILY0, BYPX1, RAILY1, values[kBypass], "BYPASS");
        panel.toggle("noise", kNoise, NOISEX0, RAILY0, NOISEX1, RAILY1, values[kNoise], "NOISE");
        panel.toggle("autogain", kAutoGain, AGX0, RAILY0, AGX1, RAILY1, values[kAutoGain], "AUTO GAIN");
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
    bool  gripCursorSet  = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pre35UI)
};

UI* createUI() { return new Pre35UI(); }

END_NAMESPACE_DISTRHO

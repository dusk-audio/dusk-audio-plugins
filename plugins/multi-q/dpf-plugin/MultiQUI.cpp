// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQUI.cpp — Dear ImGui UI for Multi-Q 2.
//
// Multi-Q's "British" EQ character routes through the SAME FourKEQDSP core as the
// standalone 4K EQ 2, so its British-mode UI is a carbon copy of FourKEQUI: a
// full-width response graph over six console channel-strip columns
// (FILTERS | LF | LMF | HMF | HF | MASTER) with per-band colour coding, INPUT/
// OUTPUT edge meters, preset + oversample selectors, a Brown/Black voicing
// toggle and Hide Graph — all remapped from FourKEQ's kParamId to Multi-Q's
// kParamBritish* indices. An EQ Type character selector (Digital/Match/British/
// Tube) lives in the header; Digital/Match/Tube draw a placeholder for now (the
// curve editor lands in a later phase).
//
// Everything is drawn in a 960x680 design space, uniformly scaled and letterboxed
// inside the 1040x680 window (side chassis margins), exactly as FourKEQUI does.

#include "DistrhoUI.hpp"
#include "MultiQParams.hpp"
#include "MultiQProgramPresets.hpp"  // Digital factory presets (header dropdown)
#include "MultiQAccess.hpp"   // same-process meter/analyzer bridge (weak accessors)
#include "FourKEQDSP.hpp"
#include "MultiQFilters.hpp"  // amb:: analog-matched designers + MqBiquadCoeffs (Digital curve)

#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"
#include "PatreonBackersDpf.hpp"

#include <cmath>
#include <complex>
#include <string>
#include <vector>
#include <algorithm>

START_NAMESPACE_DISTRHO

namespace
{
    constexpr float kDesignW = 960.0f;
    constexpr float kDesignH = 680.0f;             // graph shown
    constexpr float kDesignHCollapsed = 556.0f;    // graph hidden (band removed)
    constexpr float kFMin = 20.0f, kFMax = 20000.0f;

    // graph + meter rects
    constexpr float GX0 = 9, GY0 = 98, GX1 = 951, GY1 = 214; // outer frame lines up with IN/OUT meter outer edges
    constexpr float INX0 = 8,   INX1 = 34,  MET_Y0 = 246, MET_Y1 = 656;
    constexpr float MET_LBL_Y = 228;  // INPUT/OUTPUT caption, inside the control band
    constexpr float OUTX0 = 926, OUTX1 = 952;
    // column dividers
    constexpr float COL[7] = { 40, 186, 331, 477, 622, 768, 920 };

    // preset dropdown box (design coords) — shared by the header box + popup.
    constexpr float kPresetX0 = 200.f, kPresetY0 = 26.f, kPresetX1 = 396.f, kPresetY1 = 56.f;
    constexpr float kPresetRowH = 22.f;
    constexpr float kHdrY0 = 26.f, kHdrY1 = 56.f;   // header button row

    // band face colours
    constexpr ImU32 C_LF_BROWN  = IM_COL32(96, 56, 48, 255);   // SSL LF maroon console knob
    constexpr ImU32 C_LMF_BLUE  = IM_COL32(56, 100, 156, 255); // SSL LMF blue console knob
    constexpr ImU32 C_HMF_GREEN = IM_COL32(58, 108, 58, 255);  // SSL HMF green console knob
    constexpr ImU32 C_HF_RED    = IM_COL32(158, 52, 46, 255);  // SSL HF red console knob

    // Master-knob tick rings (continuous knobs: value + label per tick).
    constexpr float  MK_GAIN_V[]  = { -12.f, -6.f, 0.f, 6.f, 12.f };
    const     char*  MK_GAIN_L[]  = { "12", "6", "0", "6", "12" };
    constexpr float  MK_DRIVE_V[] = { 0.f, 25.f, 50.f, 75.f, 100.f };
    const     char*  MK_DRIVE_L[] = { "0", "25", "50", "75", "100" };

    constexpr ImU32 kPanel   = IM_COL32(34, 34, 37, 255);
    constexpr ImU32 kHeader  = IM_COL32(18, 18, 20, 255);
    constexpr ImU32 kAmber   = IM_COL32(150, 96, 32, 255);
    constexpr ImU32 kGreenBtn = IM_COL32(48, 108, 56, 255);

    const int   kGridF[]  = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    const char* kGridFL[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };

    // ---- FourKEQ kParamId -> Multi-Q MqParamId remap ------------------------
    // The ported drawing code addresses parameters by these short FourKEQ names;
    // here they alias Multi-Q's British-block (and a few global) indices, so the
    // body of the console UI is a verbatim copy of FourKEQUI.
    constexpr uint32_t kHpfFreq       = kParamBritishHpfFreq;
    constexpr uint32_t kHpfEnabled    = kParamBritishHpfEnabled;
    constexpr uint32_t kLpfFreq       = kParamBritishLpfFreq;
    constexpr uint32_t kLpfEnabled    = kParamBritishLpfEnabled;
    constexpr uint32_t kLfGain        = kParamBritishLfGain;
    constexpr uint32_t kLfFreq        = kParamBritishLfFreq;
    constexpr uint32_t kLfBell        = kParamBritishLfBell;
    constexpr uint32_t kLmGain        = kParamBritishLmGain;
    constexpr uint32_t kLmFreq        = kParamBritishLmFreq;
    constexpr uint32_t kLmQ           = kParamBritishLmQ;
    constexpr uint32_t kHmGain        = kParamBritishHmGain;
    constexpr uint32_t kHmFreq        = kParamBritishHmFreq;
    constexpr uint32_t kHmQ           = kParamBritishHmQ;
    constexpr uint32_t kHfGain        = kParamBritishHfGain;
    constexpr uint32_t kHfFreq        = kParamBritishHfFreq;
    constexpr uint32_t kHfBell        = kParamBritishHfBell;
    constexpr uint32_t kEqType        = kParamBritishMode;      // Brown(0)/Black(1) voicing
    constexpr uint32_t kBypass        = kParamBypass;
    constexpr uint32_t kInputGain     = kParamBritishInputGain;
    constexpr uint32_t kOutputGain    = kParamBritishOutputGain;
    constexpr uint32_t kSaturation    = kParamBritishSaturation;
    constexpr uint32_t kOversampling  = kParamHqEnabled;        // 0=Off,1=2x,2=4x
    constexpr uint32_t kSpectrumPrePost = kParamAnalyzerPrePost;
    constexpr uint32_t kAutoGain      = kParamAutoGainEnabled;
    // NOTE: FourKEQ's kMsMode (M/S) has no clean Multi-Q equivalent — Multi-Q's
    // processing_mode is a 5-way choice (Stereo/L/R/Mid/Side), not a boolean, so
    // wiring a M/S toggle onto it would corrupt its semantics. The M/S toggle is
    // OMITTED (see drawColumns MASTER section).
    // NOTE: kShowGraph is a UI-local bool (Multi-Q has no persisted show-graph param).
    // NOTE: FourKEQ meters/spectrum accessors don't exist on the Phase-2 Multi-Q
    // core, so the edge meters read 0.0f stubs and the FFT overlay is a no-op.

    inline float mqDef(uint32_t p) { return kMqParams[p].def; }

    // British factory presets now come from the shared, generated Multi-Q table
    // (mqprog::kBritishPrograms in MultiQProgramPresets.hpp) — the same sparse
    // (paramIndex,value) rows the DPF shell exposes as host programs. The old
    // hand-rolled 4K-EQ list has been retired so the dropdown, the host programs
    // and MultiQPresets.h all stay in lockstep.

    // Character selector (EQ Type: Digital / Match / British / Tube).
    const char* kCharLabels[4] = { "DIGITAL", "MATCH", "BRITISH", "TUBE" };
    constexpr int kBritishModeIndex = 2; // kMqParams eq_type: 0 Digital,1 Match,2 British,3 Tube
    constexpr int kDigitalModeIndex = 0;
    constexpr int kTubeModeIndex    = 3;

    //========================================================================
    // Digital curve-editor constants
    //========================================================================
    // Per-band colours ported from JUCE EQBand.h BandColors (band 0..7 ==
    // JUCE band 1..8). HPF red / LowShelf orange / 4x parametric / HighShelf
    // purple / LPF pink.
    constexpr ImU32 kDigitalBandCol[8] = {
        IM_COL32(0xff, 0x55, 0x55, 255), // 0 HPF   red
        IM_COL32(0xff, 0xaa, 0x00, 255), // 1 LowSh  orange
        IM_COL32(0xff, 0xee, 0x00, 255), // 2 Para   yellow
        IM_COL32(0x88, 0xee, 0x44, 255), // 3 Para   lime
        IM_COL32(0x00, 0xcc, 0xff, 255), // 4 Para   cyan
        IM_COL32(0x55, 0x88, 0xff, 255), // 5 Para   blue
        IM_COL32(0xaa, 0x66, 0xff, 255), // 6 HighSh purple
        IM_COL32(0xff, 0x66, 0xcc, 255), // 7 LPF    pink
    };
    // Band names EXACTLY as the JUCE original (EQBand.h kBandConfigs .name).
    const char* kDigitalBandShort[8] = { "HPF", "Low Shelf", "Low", "Low Mid", "Mid", "High Mid", "High Shelf", "LPF" };

    // Digital response plot rect + one-row band strip + bottom detail panel
    // (design coords). Graph ~top 45%, strips one row, detail panel bottom ~27%.
    // IN/OUT meters flank the graph (thin vertical strips just inside the chassis
    // margins); strips + detail span nearly full width (edge-to-edge cells).
    // NB: DGY0 sits below the Digital second toolbar row (y 90..116); the graph top
    // was lowered from 104 to 120 to seat that row cleanly under the header.
    constexpr float DGX0 = 44.f, DGY0 = 120.f, DGX1 = 916.f, DGY1 = 408.f;
    constexpr float DINX0 = 16.f, DINX1 = 34.f, DOUTX0 = 926.f, DOUTX1 = 944.f;
    constexpr float DSTRIP_Y0 = 416.f, DSTRIP_Y1 = 470.f, DSTRIP_X0 = 10.f, DSTRIP_X1 = 950.f;
    constexpr float DPX0 = 10.f, DPY0 = 476.f, DPX1 = 950.f, DPY1 = 662.f;

    // Butterworth per-stage Q tables (inlined from core ButterworthQ so the UI
    // graph matches the DSP edge-filter cascade exactly, without pulling in the
    // core header's clashing symbols).
    constexpr float kButterQ = 0.7071067811865476f;
    constexpr float kBq2[]  = { 0.7071f };
    constexpr float kBq4[]  = { 0.5412f, 1.3066f };
    constexpr float kBq6[]  = { 0.5176f, 0.7071f, 1.9319f };
    constexpr float kBq8[]  = { 0.5098f, 0.6013f, 0.9000f, 2.5629f };
    constexpr float kBq12[] = { 0.5024f, 0.5412f, 0.6313f, 0.7071f, 1.0000f, 1.9319f };
    constexpr float kBq16[] = { 0.5006f, 0.5176f, 0.5612f, 0.6013f, 0.7071f, 0.9000f, 1.3066f, 2.5629f };
    inline float butterStageQ(int totalSecondOrder, int stageIdx, float userQ)
    {
        const float* q = nullptr;
        switch (totalSecondOrder)
        {
            case 1: q = kBq2;  break; case 2: q = kBq4;  break; case 3: q = kBq6;  break;
            case 4: q = kBq8;  break; case 6: q = kBq12; break; case 8: q = kBq16; break;
            default: return userQ;
        }
        if (stageIdx < 0 || stageIdx >= totalSecondOrder) return userQ;
        return q[stageIdx] * (userQ / kButterQ);
    }
    // slope choice index -> cascade shape (stages, secondOrderStages, firstStageFirstOrder)
    inline void slopeToCascade(int slope, int& stages, int& secondOrder, bool& firstFirst)
    {
        switch (slope)
        {
            case 0: stages = 1; firstFirst = true;  secondOrder = 0; break; // 6
            case 1: stages = 1; firstFirst = false; secondOrder = 1; break; // 12
            case 2: stages = 2; firstFirst = true;  secondOrder = 1; break; // 18
            case 3: stages = 2; firstFirst = false; secondOrder = 2; break; // 24
            case 4: stages = 3; firstFirst = false; secondOrder = 3; break; // 36
            case 5: stages = 4; firstFirst = false; secondOrder = 4; break; // 48
            case 6: stages = 6; firstFirst = false; secondOrder = 6; break; // 72
            default:stages = 8; firstFirst = false; secondOrder = 8; break; // 96
        }
    }

    // Tube (Pultec) column x-dividers + warm face colour.
    constexpr ImU32 C_TUBE_FACE = IM_COL32(120, 78, 52, 255);   // bronze knob face
    constexpr ImU32 C_TUBE_LBL  = IM_COL32(214, 196, 168, 255); // warm caption
    constexpr float TGY0 = 104.f, TGY1 = 300.f;                 // tube response plot
}

class MultiQUI : public UI, public duskdpf::ParamHost
{
public:
    MultiQUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kMqParams[i].def;
        // Free resize with a sane minimum; onImGuiDisplay uniformly scales +
        // letterboxes the fixed 960x680 design into any window size (no distortion).
        setGeometryConstraints(624, 408, false);
        static const float kFontSizes[] = { 9.f, 11.f, 13.f, 16.f, 20.f, 26.f };
        fontSet = duskdpf::loadCrispFontSet(kFontSizes, 6, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
        fft.prepare(kFftSize);
        specDb.assign(kFftSize / 2 + 1, -120.0f);
        // Digital analyzer FFT — default Medium (4096); resized on resolution change.
        digFft_.prepare(digFftSize_);
        digSpecDb_.assign(digFftSize_ / 2 + 1, -120.0f);
        digPeakDb_.assign(digFftSize_ / 2 + 1, -120.0f);
        digFrozenDb_.assign(digFftSize_ / 2 + 1, -120.0f);
    }

    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override { setParameterValue(idx, v); }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount) return;
        values[index] = value;
    }

    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth(), winH = (float)getHeight();

        const bool british = (int)std::lround(values[kParamEqType]) == kBritishModeIndex;

        // Uniform scale + letterbox: scale the whole design by the SMALLER of the
        // width/height ratios and centre it; leftover area is a chassis margin.
        const float designH = (british && !showGraph) ? kDesignHCollapsed : kDesignH;
        const float s = std::min(winW / kDesignW, winH / designH);
        const ImVec2 org((winW - kDesignW * s) * 0.5f, (winH - designH * s) * 0.5f);
        panel.begin(s, org, labelFont, this);

        // Control rows keep their design spacing (no vertical compression); shift
        // up when the graph is hidden to fill the reclaimed band.
        ctlDstTop_ = showGraph ? 220.0f : 96.0f;
        ctlScaleY_ = 1.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("MultiQ2", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(30, 30, 33, 255)); // chassis fills window

        if (british)
        {
            drawHeader(dl);
            drawCharSelector(dl);
            if (showGraph)
                drawGraph(dl);
            // Absorb input under a modal (preset list / credits) BEFORE controls.
            if (presetOpen || showCredits)
            {
                ImGui::SetCursorScreenPos(ImVec2(0, 0));
                ImGui::InvisibleButton("modalblock", ImVec2(winW, winH));
            }
            drawColumns(dl);
            drawMeters(dl);
            if (presetOpen)
                drawPresetPopup(dl, winW, winH);
        }
        else
        {
            const int mode = (int)std::lround(values[kParamEqType]);
            drawSimpleHeader(dl, mode == kDigitalModeIndex ? "Dusk Audio" : "DUSK AUDIO");
            if (showCredits)
            {
                ImGui::SetCursorScreenPos(ImVec2(0, 0));
                ImGui::InvisibleButton("modalblock", ImVec2(winW, winH));
            }
            // Digital gets the full JUCE-style header (A/B, character dropdown,
            // preset, Save, Auto Gain, mode, range, Oversample, Bypass); Match/Tube
            // keep the shared 4-segment EQ-TYPE character selector.
            if (mode == kDigitalModeIndex)   { drawDigitalHeader(dl); drawDigital(dl); }
            else if (mode == kTubeModeIndex) { drawCharSelector(dl); drawTubePresetCombo(dl); drawTube(dl); }
            else                             { drawCharSelector(dl); drawMatch(dl); } // Match spectrum-learn UI
        }

        if (showCredits)
            drawCredits(dl, winW, winH);

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    // Control-area vertical remap (see FourKEQUI): maps a design Y in the
    // [220..662] band to the repositioned band so the strip fills the height.
    float cY(float y) const { return ctlDstTop_ + (y - 220.0f) * ctlScaleY_; }

    void toggleGraph() { showGraph = !showGraph; }

private:
    static constexpr int kFftSize = 2048;
    const auto& pal() const { return panel.palette(); }
    float sc() const { return panel.scale(); }

    //========================================================================
    // character selector (Digital / Match / British / Tube) — always visible
    //========================================================================
    void drawCharSelector(ImDrawList* dl)
    {
        panel.text(dl, 352, 72, 10.5f, IM_COL32(150, 152, 156, 255), "EQ TYPE", 1, true);
        const int cur = (int)std::lround(values[kParamEqType]);
        // Right edge kept clear of the "DUSK AUDIO" label (right-aligned at x~934).
        const float x0 = 360.f, x1 = 852.f, segGap = 6.f;
        const float segW = (x1 - x0 - 3.f * segGap) / 4.f;
        for (int i = 0; i < 4; ++i)
        {
            const float sx0 = x0 + i * (segW + segGap);
            const float sx1 = sx0 + segW;
            const bool on = (i == cur);
            const ImVec2 b0 = panel.P(sx0, 61), b1 = panel.P(sx1, 83);
            char id[16]; std::snprintf(id, sizeof(id), "char%d", i);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            const bool hov = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked() && !on)
            {
                editParameter(kParamEqType, true);
                values[kParamEqType] = (float)i;
                setParameterValue(kParamEqType, (float)i);
                editParameter(kParamEqType, false);
            }
            dl->AddRectFilled(b0, b1, on ? IM_COL32(60, 92, 130, 255) : IM_COL32(44, 44, 48, 255), 4.f * sc());
            dl->AddRect(b0, b1, on ? IM_COL32(120, 160, 210, 240)
                                   : (hov ? IM_COL32(150, 152, 156, 220) : IM_COL32(84, 84, 90, 200)),
                        4.f * sc(), 0, 1.2f * sc());
            panel.text(dl, 0.5f * (sx0 + sx1), 66, 10.5f,
                       on ? pal().white : IM_COL32(168, 170, 174, 255), kCharLabels[i], 0, true);
        }
    }

    //========================================================================
    // header (British console) — title + preset + control-row buttons
    //========================================================================
    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 88), kHeader);
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 3), IM_COL32(150, 150, 152, 255));
        dl->AddLine(panel.P(0, 88), panel.P(kDesignW, 88), IM_COL32(60, 60, 63, 255), 1.5f * sc());

        drawTitle(dl);

        // A/B compare — real two-bank swap (shares toggleAB() with the Digital
        // header). Sits in the gap between the title and the preset dropdown.
        headerButton(dl, "brab", 172, kHdrY0, 196, kHdrY1, abIsA_ ? "A" : "B",
                     abIsA_ ? kGreenBtn : IM_COL32(60, 60, 64, 255), pal().white,
                     [&]{ toggleAB(); });

        // preset dropdown — custom crisp widget; the expanded list draws later, on top.
        {
            const ImVec2 p0 = panel.P(kPresetX0, kPresetY0), p1 = panel.P(kPresetX1, kPresetY1);
            dl->AddRectFilled(p0, p1, IM_COL32(46, 46, 50, 255), 3.f * sc());
            dl->AddRect(p0, p1, presetOpen ? IM_COL32(120, 150, 200, 240) : IM_COL32(90, 90, 96, 220), 3.f * sc(), 0, 1.f * sc());
            const char* preview = (currentPreset >= 0 && currentPreset < mqprog::kNumBritishPrograms)
                                      ? mqprog::kBritishPrograms[currentPreset].name : "Default";
            panel.text(dl, kPresetX0 + 9.f, 0.5f * (kPresetY0 + kPresetY1) - 6.5f, 13.f, IM_COL32(228, 228, 224, 255), preview, -1);
            const ImVec2 ac = panel.P(kPresetX1 - 13.f, 0.5f * (kPresetY0 + kPresetY1));
            dl->AddTriangleFilled(ImVec2(ac.x - 4.f * sc(), ac.y - 2.5f * sc()),
                                  ImVec2(ac.x + 4.f * sc(), ac.y - 2.5f * sc()),
                                  ImVec2(ac.x, ac.y + 3.5f * sc()), IM_COL32(180, 182, 186, 255));
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton("presetbox", ImVec2(p1.x - p0.x, p1.y - p0.y));
            if (ImGui::IsItemClicked()) presetOpen = !presetOpen;
        }

        const ImU32 btnBg = IM_COL32(46, 46, 50, 255);

        // Oversample (cycles 1x/2x/4x — maps to Multi-Q hq_enabled Off/2x/4x).
        static const char* kOsBtn[3] = { "Oversample: 1x", "Oversample: 2x", "Oversample: 4x" };
        int osi = (int)(values[kOversampling] + 0.5f); osi = osi < 0 ? 0 : (osi > 2 ? 2 : osi);
        headerButton(dl, "os", 412, kHdrY0, 536, kHdrY1, kOsBtn[osi], btnBg, pal().white,
                     [&]{ cycleParam(kOversampling, 3); });

        // Brown / Black voicing.
        const bool brown = values[kEqType] < 0.5f;
        headerButton(dl, "eqtype", 548, kHdrY0, 640, kHdrY1, brown ? "BROWN" : "BLACK",
                     brown ? kAmber : IM_COL32(60, 74, 96, 255), IM_COL32(245, 240, 232, 255),
                     [&]{ cycleParam(kEqType, 2); });

        headerButton(dl, "hidegraph", 652, kHdrY0, 740, kHdrY1, showGraph ? "Hide Graph" : "Show Graph",
                     btnBg, pal().white, [&]{ toggleGraph(); });

        headerButton(dl, "fft", 752, kHdrY0, 822, kHdrY1, showFft ? "FFT: On" : "FFT: Off",
                     showFft ? IM_COL32(52, 70, 92, 255) : btnBg,
                     showFft ? IM_COL32(210, 224, 240, 255) : IM_COL32(150, 152, 156, 255),
                     [&]{ showFft = !showFft; });

        headerButton(dl, "prepost", 834, kHdrY0, 920, kHdrY1,
                     values[kSpectrumPrePost] > 0.5f ? "FFT PRE" : "FFT POST",
                     btnBg, IM_COL32(180, 182, 186, 255),
                     [&]{ toggleParam(kSpectrumPrePost); });
    }

    // Simple header for the non-British character skins (title only). `brand` lets
    // Digital use title-case "Dusk Audio" while British/Tube keep "DUSK AUDIO".
    void drawSimpleHeader(ImDrawList* dl, const char* brand = "DUSK AUDIO")
    {
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 88), kHeader);
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 3), IM_COL32(150, 150, 152, 255));
        dl->AddLine(panel.P(0, 88), panel.P(kDesignW, 88), IM_COL32(60, 60, 63, 255), 1.5f * sc());
        drawTitle(dl, brand);
    }

    void drawTitle(ImDrawList* dl, const char* brand = "DUSK AUDIO")
    {
        panel.text(dl, 28, 28, 25, pal().white, "Multi-Q 2", -1, true);
        static const char* kSub[4] = {
            "Universal EQ", "Universal Equalizer - Match",
            "Console-Style Equalizer - British", "Passive Program Equalizer - Tube" };
        const int m = (int)std::lround(values[kParamEqType]);
        panel.text(dl, 30, 58, 10.5f, IM_COL32(150, 152, 156, 255),
                   kSub[(m >= 0 && m < 4) ? m : 0], -1);
        // Clickable title -> Patreon supporters overlay.
        {
            const ImVec2 t0 = panel.P(26, 20), t1 = panel.P(170, 48);
            ImGui::SetCursorScreenPos(t0);
            ImGui::InvisibleButton("titlecredits", ImVec2(t1.x - t0.x, t1.y - t0.y));
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) { showCredits = true; creditsArmed = false; }
        }
        panel.text(dl, kDesignW - 26, 68, 10.5f, IM_COL32(140, 142, 146, 255), brand, 1, true);
    }

    void drawPlaceholder(ImDrawList* dl, int mode)
    {
        const float s = sc();
        const ImVec2 c0 = panel.P(160, 200), c1 = panel.P(800, 480);
        dl->AddRectFilled(c0, c1, IM_COL32(34, 34, 37, 255), 8.f * s);
        dl->AddRect(c0, c1, IM_COL32(80, 82, 88, 220), 8.f * s, 0, 1.4f * s);
        const char* name = (mode >= 0 && mode < 4) ? kCharLabels[mode] : "DIGITAL";
        char title[48]; std::snprintf(title, sizeof(title), "%s EQ", name);
        panel.text(dl, kDesignW * 0.5f, 300, 26.f, pal().white, title, 0, true);
        panel.text(dl, kDesignW * 0.5f, 344, 13.f, IM_COL32(160, 162, 166, 255),
                   "Curve editor UI — coming soon", 0);
        panel.text(dl, kDesignW * 0.5f, 372, 11.f, IM_COL32(130, 132, 136, 255),
                   "Select DIGITAL, BRITISH or TUBE above.", 0);
    }

    //========================================================================
    // shared helpers
    //========================================================================
    ImVec2 dOrg() const { return panel.P(0.f, 0.f); }
    ImVec2 invP(ImVec2 screen) const
    {
        const ImVec2 o = dOrg(); const float s = sc();
        return ImVec2((screen.x - o.x) / s, (screen.y - o.y) / s);
    }
    static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
    double digitalSampleRate() const { return getSampleRate() > 0.0 ? getSampleRate() : 48000.0; }
    int choiceIdx(uint32_t p) const
    {
        int i = (int)std::lround(values[p]);
        const int n = kMqParams[p].numChoices;
        return i < 0 ? 0 : (i >= n ? n - 1 : i);
    }
    void cycleChoice(uint32_t id)
    {
        int n = kMqParams[id].numChoices; if (n < 1) n = 1;
        float nv = values[id] + 1.0f; if (nv > n - 1 + 0.5f) nv = 0.0f;
        editParameter(id, true); values[id] = nv; setParameterValue(id, nv); editParameter(id, false);
    }

    // Stepped choice box: caption above, current label inside, click cycles.
    void stepSelector(ImDrawList* dl, const char* id, float cx, float cyTop, float w, float h,
                      const char* caption, uint32_t pid, bool enabled = true)
    {
        const float s = sc();
        if (caption) panel.text(dl, cx, cyTop - 14.f, 10.f, C_TUBE_LBL, caption, 0, true);
        const ImVec2 b0 = panel.P(cx - 0.5f * w, cyTop), b1 = panel.P(cx + 0.5f * w, cyTop + h);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (enabled && ImGui::IsItemClicked()) cycleChoice(pid);
        dl->AddRectFilled(b0, b1, IM_COL32(30, 26, 22, 255), 3.f * s);
        dl->AddRect(b0, b1, hov && enabled ? IM_COL32(200, 170, 130, 220) : IM_COL32(96, 84, 70, 200), 3.f * s, 0, 1.2f * s);
        const int i = choiceIdx(pid);
        const char* lbl = kMqParams[pid].choices ? kMqParams[pid].choices[i] : "";
        panel.text(dl, cx - 5.f, cyTop + 0.5f * h - 6.f, 11.f,
                   enabled ? IM_COL32(232, 224, 210, 255) : IM_COL32(120, 112, 100, 255), lbl, 0, true);
        const ImVec2 ac = panel.P(cx + 0.5f * w - 9.f, cyTop + 0.5f * h);
        dl->AddTriangleFilled(ImVec2(ac.x - 3.f * s, ac.y - 2.f * s), ImVec2(ac.x + 3.f * s, ac.y - 2.f * s),
                              ImVec2(ac.x, ac.y + 2.5f * s), IM_COL32(180, 160, 130, 255));
    }

    //========================================================================
    // DIGITAL — interactive log-frequency curve editor + 8 band strips
    //========================================================================
    // Digital dB range set — matches JUCE DisplayScaleMode {±12, ±24, ±30, ±60, Warped}.
    // Index 4 = Warped: sqrt-warped axis with an effective ±24 dB span (JUCE Warped
    // sets minDisplayDB/maxDisplayDB to ∓24 and warps via getYForDB). digRangeDb()
    // returns the linear/effective span; digGridStep() the dB ladder step (JUCE: 6 by
    // default, 10 for ±30, 20 for ±60); digWarped() gates the nonlinear mapping.
    static float digRangeDb(int idx) { static const float R[5] = { 12.f, 24.f, 30.f, 60.f, 24.f }; return R[idx < 0 ? 0 : (idx > 4 ? 4 : idx)]; }
    bool  digWarped()  const { return digRangeIdx == 4; }
    int   digGridStep() const { return digRangeIdx == 3 ? 20 : (digRangeIdx == 2 ? 10 : 6); }
    float digY(float db) const
    {
        if (digWarped())
        {
            // sqrt-warped axis (verbatim mapping of British dbToNy warped branch /
            // JUCE getYForDB): more vertical resolution around 0 dB. m = warped span.
            const float m = 24.f;
            float d = db < -m ? -m : (db > m ? m : db);
            const float tt = (d >= 0.f ? 1.f : -1.f) * std::sqrt(std::abs(d) / m);
            return DGY0 + (0.5f - 0.5f * tt) * (DGY1 - DGY0);
        }
        const float r = digRangeDb(digRangeIdx);
        float d = db < -r ? -r : (db > r ? r : db);
        return DGY0 + (0.5f - 0.5f * d / r) * (DGY1 - DGY0);
    }
    // Inverse of digY — MUST stay the exact inverse (incl. Warped) so dragged nodes
    // track the cursor. For the linear scales this is the original formula; for
    // Warped: norm→tt (=sign·√(|d|/m)) → db = sign(tt)·tt²·m (mirrors JUCE getDBAtY).
    float digDbFromY(float y) const
    {
        const float norm = (y - DGY0) / (DGY1 - DGY0);
        if (digWarped())
        {
            const float m = 24.f;
            const float tt = (0.5f - norm) * 2.f;   // = sign·√(|d|/m)
            return (tt >= 0.f ? 1.f : -1.f) * tt * tt * m;
        }
        const float r = digRangeDb(digRangeIdx);
        return (0.5f - norm) * 2.f * r;
    }

    void digTiltShelf(duskaudio::MqBiquadCoeffs& c, double sr, double freq, float gainDB) const
    {
        // 1st-order tilt shelf — verbatim from MultiQDSP::computeTiltShelf.
        const double w0 = 2.0 * duskaudio::kMultiQPi * freq;
        const double T = 1.0 / sr;
        const double wc = (2.0 / T) * std::tan(w0 * T / 2.0);
        const double A = std::pow(10.0, gainDB / 40.0);
        const double sqrtA = std::sqrt(A);
        const double twoOverT = 2.0 / T;
        const double b0 = twoOverT + wc * sqrtA, b1 = wc * sqrtA - twoOverT;
        const double a0 = twoOverT + wc / sqrtA, a1 = wc / sqrtA - twoOverT;
        c.coeffs[0] = float(b0 / a0); c.coeffs[1] = float(b1 / a0); c.coeffs[2] = 0.f;
        c.coeffs[3] = 1.f; c.coeffs[4] = float(a1 / a0); c.coeffs[5] = 0.f;
    }

    double digitalEdgeMag(int b, double freq, double sr, bool hp) const
    {
        const double fc = std::max(20.0, std::min((double)values[mqidx::freq(b)], sr * 0.45));
        const float userQ = values[mqidx::q(b)];
        int stages, secondOrder; bool firstFirst;
        slopeToCascade((int)std::lround(values[mqidx::slope(b)]), stages, secondOrder, firstFirst);
        double mag = 1.0; int soIdx = 0;
        for (int stage = 0; stage < stages; ++stage)
        {
            duskaudio::MqBiquadCoeffs c;
            if (firstFirst && stage == 0)
            {
                if (hp) duskaudio::amb::computeFirstOrderHighPass(c, fc, sr);
                else    duskaudio::amb::computeFirstOrderLowPass(c, fc, sr);
            }
            else
            {
                const float sq = butterStageQ(secondOrder, soIdx, userQ);
                if (hp) duskaudio::amb::computeHighPass(c, fc, sr, sq);
                else    duskaudio::amb::computeLowPass(c, fc, sr, sq);
                ++soIdx;
            }
            mag *= c.getMagnitudeForFrequency(freq, sr);
        }
        return mag;
    }

    // Linear magnitude of one enabled Digital band (matches computeBandCoeffs).
    // gainOffset shifts the band's gain (dB) — used to draw the live dynamic curve
    // (each dyn-enabled band offset by its current dynamic-EQ gain).
    double digitalBandMag(int b, double freq, double sr, float gainOffset = 0.f) const
    {
        if (values[mqidx::enabled(b)] < 0.5f) return 1.0;
        if (b == 0) return digitalEdgeMag(0, freq, sr, true);
        if (b == 7) return digitalEdgeMag(7, freq, sr, false);
        duskaudio::MqBiquadCoeffs c;
        const float gain = values[mqidx::gain(b)] + gainOffset;
        const float q    = values[mqidx::q(b)];
        const float fc   = values[mqidx::freq(b)];
        const int shape  = (int)std::lround(values[mqidx::shape(b)]);
        if (b == 1)
        {
            if (shape == 1)      duskaudio::amb::computePeaking(c, fc, sr, gain, q);
            else if (shape == 2) duskaudio::amb::computeHighPass(c, fc, sr, q);
            else                 duskaudio::amb::computeLowShelf(c, fc, sr, gain, q);
        }
        else if (b == 6)
        {
            if (shape == 1)      duskaudio::amb::computePeaking(c, fc, sr, gain, q);
            else if (shape == 2) duskaudio::amb::computeLowPass(c, fc, sr, q);
            else                 duskaudio::amb::computeHighShelf(c, fc, sr, gain, q);
        }
        else
        {
            if (shape == 1)      duskaudio::amb::computeNotch(c, fc, sr, q);
            else if (shape == 2) duskaudio::amb::computeBandPass(c, fc, sr, q);
            else if (shape == 3) digTiltShelf(c, sr, fc, gain);
            else                 duskaudio::amb::computePeaking(c, fc, sr, gain, q);
        }
        return c.getMagnitudeForFrequency(freq, sr);
    }

    float digitalResponseDb(float freq) const
    {
        const double sr = digitalSampleRate();
        double mag = 1.0;
        for (int b = 0; b < 8; ++b) mag *= digitalBandMag(b, freq, sr);
        double db = 20.0 * std::log10(std::max(mag, 1e-6));
        db += values[kParamMasterGain];
        return (float)db;
    }

    // Composite response with each dyn-enabled band offset by its live dynamic-EQ
    // gain (the moving "dynamic" curve). Matches JUCE getFrequencyResponseWithDynamics.
    float digitalResponseDbDyn(float freq) const
    {
        const double sr = digitalSampleRate();
        double mag = 1.0;
        for (int b = 0; b < 8; ++b)
        {
            const float dg = values[mqidx::dynEnabled(b)] > 0.5f ? smoothedDynGain_[b] : 0.f;
            mag *= digitalBandMag(b, freq, sr, dg);
        }
        double db = 20.0 * std::log10(std::max(mag, 1e-6));
        db += values[kParamMasterGain];
        return (float)db;
    }

    // Per-frame read + smoothing of the DSP's live per-band dynamic-EQ gain
    // (read-only meter tap through the same-process bridge). Mirrors JUCE
    // EQGraphicDisplay::smoothedDynamicGains.
    void updateDynGains()
    {
        float raw[8] = {};
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        bool have = false;
        if (multiQGetBandDynGain != nullptr) // weak: null in the split LV2 UI
            if (void* inst = getPluginInstancePointer())
            {
                have = true;
                for (int b = 0; b < 8; ++b) raw[b] = multiQGetBandDynGain(inst, b);
            }
        const float dt = std::min(std::max(ImGui::GetIO().DeltaTime, 0.f), 0.1f);
        const float sm = 1.0f - std::exp(-dt / 0.05f); // ~50 ms UI smoothing
        for (int b = 0; b < 8; ++b)
            smoothedDynGain_[b] += ((have ? raw[b] : 0.f) - smoothedDynGain_[b]) * sm;
       #else
        for (int b = 0; b < 8; ++b) smoothedDynGain_[b] = 0.f;
       #endif
    }

    // Does band b's control node carry gain (draggable in Y)? Matches JUCE
    // getControlPointPosition: only peaking-shaped nodes sit off the 0 dB line.
    bool digBandCarriesGain(int b) const
    {
        if (b == 0 || b == 7) return false;               // HPF/LPF at 0 dB
        const int shape = (int)std::lround(values[mqidx::shape(b)]);
        if (b == 1 || b == 6) return shape == 1;          // shelf bands: only Peaking
        return shape == 0;                                // parametric: only Peaking
    }

    // Does a *drag* on band b's node change its gain? Matches JUCE setBandGain
    // (EQGraphicDisplay :1819-1838): every band 1..6 is gain-draggable EXCEPT
    // parametric bands (2..5) currently in Notch(1)/BandPass(2). Both shelves
    // (bands 1/6 in shelf, peaking or — for tilt — the parametric Tilt shape 3)
    // therefore drag their gain even though their node sits on the 0 dB line
    // (getControlPointPosition only lifts the node for peaking). HPF/LPF: never.
    bool digBandDragGain(int b) const
    {
        if (b <= 0 || b >= 7) return false;               // HPF (0) / LPF (7)
        if (b >= 2 && b <= 5)
        {
            const int shape = (int)std::lround(values[mqidx::shape(b)]);
            if (shape == 1 || shape == 2) return false;   // Notch / BandPass: no gain
        }
        return true;                                      // shelves, peaking, tilt
    }

    // Shape-glyph drawn INSIDE a control node (JUCE drawBandControlPoint :1008-1101).
    // Primitives only — no unicode: HPF "/¯", LPF "¯\", low/high shelf steps, notch
    // "V", bandpass "^", else the band number. cx/cy are the (design-space) node
    // centre, iconR the glyph half-extent, w the stroke width, col the ink colour.
    // Coordinate offsets are copied verbatim from the JUCE Path builders (y grows
    // down in both frameworks, so +y is below centre).
    void digNodeGlyph(ImDrawList* dl, int b, float cx, float cy,
                      float iconR, float w, ImU32 col) const
    {
        const int shape = (mqidx::shape(b) >= 0) ? (int)std::lround(values[mqidx::shape(b)]) : 0;
        // Resolve the glyph kind from band + shape (mirrors digitalBandMag routing).
        // 0=number 1=HPF 2=LPF 3=lowShelf 4=highShelf 5=notch 6=bandpass
        int g = 0;
        if      (b == 0) g = 1;                                   // HPF
        else if (b == 7) g = 2;                                   // LPF
        else if (b == 1) g = (shape == 1) ? 0 : (shape == 2 ? 1 : 3); // peak / HPF / lowShelf
        else if (b == 6) g = (shape == 1) ? 0 : (shape == 2 ? 2 : 4); // peak / LPF / highShelf
        else             g = (shape == 1) ? 5 : (shape == 2 ? 6 : 0); // notch / bandpass / (peak,tilt)

        if (g == 0)   // band number
        {
            panel.text(dl, cx, cy - 5.f, 9.f, col, std::to_string(b + 1).c_str(), 0, true);
            return;
        }
        auto pt = [&](float ux, float uy) { return panel.P(cx + ux * iconR, cy + uy * iconR); };
        ImVec2 v[5]; int n = 0;
        switch (g)
        {
            case 1: v[0]=pt(-0.6f,0.4f); v[1]=pt(-0.1f,0.4f); v[2]=pt(0.3f,-0.4f); v[3]=pt(0.6f,-0.4f); n=4; break; // HPF /¯
            case 2: v[0]=pt(-0.6f,-0.4f);v[1]=pt(-0.3f,-0.4f);v[2]=pt(0.1f,0.4f);  v[3]=pt(0.6f,0.4f);  n=4; break; // LPF ¯\
            case 3: v[0]=pt(-0.6f,0.3f); v[1]=pt(-0.15f,0.3f);v[2]=pt(0.15f,-0.3f);v[3]=pt(0.6f,-0.3f); n=4; break; // low shelf
            case 4: v[0]=pt(-0.6f,-0.3f);v[1]=pt(-0.15f,-0.3f);v[2]=pt(0.15f,0.3f);v[3]=pt(0.6f,0.3f);  n=4; break; // high shelf
            case 5: v[0]=pt(-0.6f,-0.3f);v[1]=pt(-0.15f,-0.3f);v[2]=pt(0.f,0.5f);  v[3]=pt(0.15f,-0.3f);v[4]=pt(0.6f,-0.3f); n=5; break; // notch V
            case 6: v[0]=pt(-0.6f,0.3f); v[1]=pt(-0.15f,0.3f);v[2]=pt(0.f,-0.5f);  v[3]=pt(0.15f,0.3f); v[4]=pt(0.6f,0.3f);  n=5; break; // bandpass ^
        }
        dl->AddPolyline(v, n, col, 0, w);
    }

    // Design-space x -> frequency (inverse of flog over the plot rect).
    float digFreqFromX(float x) const
    {
        const float t = clamp01((x - DGX0) / (DGX1 - DGX0));
        return std::pow(10.f, std::log10(kFMin) + t * (std::log10(kFMax) - std::log10(kFMin)));
    }

    // Musical note name for a frequency, ported verbatim from JUCE
    // EQGraphicDisplay.cpp frequencyToNoteName (A4 = 440 Hz, +/- cents suffix).
    static void freqToNoteName(char* buf, size_t n, float hz)
    {
        if (hz <= 0.f) { if (n) buf[0] = '\0'; return; }
        static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const float midi   = 12.f * std::log2(hz / 440.f) + 69.f;
        const int   rounded = (int)std::lround(midi);
        const int   cents   = (int)std::lround((midi - (float)rounded) * 100.f);
        const int   idx     = ((rounded % 12) + 12) % 12;
        const int   octave  = (rounded / 12) - 1;
        if (cents != 0) std::snprintf(buf, n, "%s%d %s%dc", names[idx], octave, cents > 0 ? "+" : "", cents);
        else            std::snprintf(buf, n, "%s%d", names[idx], octave);
    }

    // Context-menu band type name (matches JUCE showBandContextMenu :1871-1898).
    const char* digBandTypeName(int b) const
    {
        if (b == 0) return "High-Pass";
        if (b == 7) return "Low-Pass";
        const int shape = (int)std::lround(values[mqidx::shape(b)]);
        if (b == 1) { static const char* n[3] = { "Low Shelf", "Peaking", "High-Pass" }; return n[shape < 0 ? 0 : (shape > 2 ? 2 : shape)]; }
        if (b == 6) { static const char* n[3] = { "High Shelf", "Peaking", "Low-Pass" }; return n[shape < 0 ? 0 : (shape > 2 ? 2 : shape)]; }
        static const char* n[4] = { "Parametric", "Notch", "Band Pass", "Tilt Shelf" };
        return n[shape < 0 ? 0 : (shape > 3 ? 3 : shape)];
    }

    // Write + notify one param, wrapped in a begin/end edit gesture.
    void setParamNotify(uint32_t id, float v)
    {
        editParameter(id, true); values[id] = v; setParameterValue(id, v); editParameter(id, false);
    }

    // Params that a real A/B compare must NOT swap. The character/skin selector
    // (eq_type) is excluded so an A/B click always compares WITHIN the current
    // view instead of yanking the user into another skin. Multi-Q's DPF param
    // table has no output-only/meter params (metering is via the spectrum
    // bridge, not automation), so nothing else needs skipping.
    static bool abSkipParam(uint32_t i) { return i == (uint32_t)kParamEqType; }

    // Real A/B compare (JUCE MultiQEditor::toggleDigitalAB / toggleBritishAB):
    // hold two full parameter snapshots and swap the LIVE params to the other
    // bank on click. Shared by the Digital AND British headers.
    //   - First use: seed BOTH banks from the current state, so A==B until the
    //     user edits something (exactly like JUCE, where B starts as a copy of A).
    //   - Click: save the current live values[] into the active bank, flip the
    //     active bank, then LOAD the other bank by pushing every param through
    //     the host (begin/set/end). setParamNotify writes values[] too, so the
    //     graph + every control reflect the loaded bank on the same frame.
    void toggleAB()
    {
        if (!abInit_)
        {
            for (uint32_t i = 0; i < kParamCount; ++i)
                abBankA_[i] = abBankB_[i] = values[i];
            abInit_ = true;
        }
        // 1) snapshot the CURRENT live state into the active bank
        float* active = abIsA_ ? abBankA_ : abBankB_;
        for (uint32_t i = 0; i < kParamCount; ++i)
            active[i] = values[i];
        // 2) flip, then 3) load the other bank into the live params
        abIsA_ = !abIsA_;
        const float* load = abIsA_ ? abBankA_ : abBankB_;
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            if (abSkipParam(i)) continue;
            if (load[i] != values[i])
                setParamNotify(i, load[i]);   // pushes host + updates values[] now
        }
    }

    // Reset a band to defaults. Alt-drag reset (JUCE :1315-1318) keeps the shape;
    // double-click / "Reset to Default" (JUCE :1484-1496) also zeroes the shape.
    void digResetBand(int b, bool resetShape)
    {
        setParamNotify((uint32_t)mqidx::freq(b), mqDef(mqidx::freq(b)));
        if (mqidx::gain(b) >= 0) setParamNotify((uint32_t)mqidx::gain(b), 0.f);
        setParamNotify((uint32_t)mqidx::q(b), 0.71f);
        if (resetShape && mqidx::shape(b) >= 0) setParamNotify((uint32_t)mqidx::shape(b), 0.f);
        selectedBand_ = b;
    }

    // Double-click on empty graph = JUCE "spectrum grab" (:1500-1563): enable the
    // DISABLED parametric band (index 2..5) whose DEFAULT freq is nearest the click
    // (log2 distance), force it to peaking, and drop it at the clicked freq/gain.
    void digSpectrumGrab(ImVec2 mp)
    {
        if (mp.x < DGX0 || mp.x > DGX1 || mp.y < DGY0 || mp.y > DGY1) return;
        const float clickFreq = digFreqFromX(mp.x);
        const float clickGain = digDbFromY(mp.y);
        int   band = -1;
        float best = 1e30f;
        for (int i = 2; i <= 5; ++i)
        {
            if (values[mqidx::enabled(i)] < 0.5f)
            {
                const float d = std::fabs(std::log2(clickFreq) - std::log2(mqDef(mqidx::freq(i))));
                if (d < best) { best = d; band = i; }
            }
        }
        if (band < 0) return;                             // all parametric bands live
        setParamNotify((uint32_t)mqidx::enabled(band), 1.f);
        setParamNotify((uint32_t)mqidx::shape(band), 0.f);   // peaking first (else gain no-ops)
        float f = clickFreq < kFMin ? kFMin : (clickFreq > kFMax ? kFMax : clickFreq);
        setParamNotify((uint32_t)mqidx::freq(band), f);
        float g = clickGain < -24.f ? -24.f : (clickGain > 24.f ? 24.f : clickGain);
        setParamNotify((uint32_t)mqidx::gain(band), g);
        setParamNotify((uint32_t)mqidx::q(band), 0.71f);
        selectedBand_ = band;
    }

    //---- Solo write/read bridge (weak-guarded like the meter/dyn-gain bridge) ----
    int digSoloBand()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQGetSoloBand != nullptr)
            if (void* inst = getPluginInstancePointer()) return multiQGetSoloBand(inst);
       #endif
        return -1;
    }
    bool digSoloDelta()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQGetSoloDelta != nullptr)
            if (void* inst = getPluginInstancePointer()) return multiQGetSoloDelta(inst);
       #endif
        return false;
    }
    void digSetSolo(int band, bool delta)
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQSetSolo != nullptr)
            if (void* inst = getPluginInstancePointer()) multiQSetSolo(inst, band, delta);
       #else
        (void)band; (void)delta;
       #endif
    }

    //========================================================================
    // MATCH — spectrum-learn bridge (weak-guarded like the meter/solo bridge)
    //========================================================================
    // True only when the same-process DSP bridge is reachable (single binary).
    // In the split LV2 UI the weak symbols resolve to null -> controls grey out.
    bool matchBridge()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        return multiQMatchClear != nullptr && getPluginInstancePointer() != nullptr;
       #endif
        return false;
    }
    bool matchLearningCurrent()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchIsLearningCurrent != nullptr)
            if (void* i = getPluginInstancePointer()) return multiQMatchIsLearningCurrent(i);
       #endif
        return false;
    }
    bool matchLearningReference()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchIsLearningReference != nullptr)
            if (void* i = getPluginInstancePointer()) return multiQMatchIsLearningReference(i);
       #endif
        return false;
    }
    bool matchHasCurrent()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchHasCurrent != nullptr)
            if (void* i = getPluginInstancePointer()) return multiQMatchHasCurrent(i);
       #endif
        return false;
    }
    bool matchHasReference()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchHasReference != nullptr)
            if (void* i = getPluginInstancePointer()) return multiQMatchHasReference(i);
       #endif
        return false;
    }
    bool matchHasCorrection()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchHasCorrection != nullptr)
            if (void* i = getPluginInstancePointer()) return multiQMatchHasCorrection(i);
       #endif
        return false;
    }
    int matchFrames()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchFrameCount != nullptr)
            if (void* i = getPluginInstancePointer()) return multiQMatchFrameCount(i);
       #endif
        return 0;
    }
    void matchStartCurrent(bool on)
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchStartLearnCurrent != nullptr)
            if (void* i = getPluginInstancePointer()) multiQMatchStartLearnCurrent(i, on);
       #else
        (void)on;
       #endif
    }
    void matchStartReference(bool on)
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchStartLearnReference != nullptr)
            if (void* i = getPluginInstancePointer()) multiQMatchStartLearnReference(i, on);
       #else
        (void)on;
       #endif
    }
    void matchCompute()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchCompute != nullptr)
            if (void* i = getPluginInstancePointer()) multiQMatchCompute(i);
       #endif
    }
    void matchClear()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQMatchClear != nullptr)
            if (void* i = getPluginInstancePointer()) multiQMatchClear(i);
       #endif
    }
    // Fill `out[0..n-1]` with dB values for which: 0 current, 1 reference, 2 correction.
    // out[k] is FFT bin k (n == kMatchBins == full spectrum). Returns false if the
    // bridge is unavailable (caller skips the curve).
    bool matchGetCurve(int which, float* out, int n)
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (void* i = getPluginInstancePointer())
        {
            if (which == 0 && multiQMatchGetCurrentDb    != nullptr) { multiQMatchGetCurrentDb(i, out, n);    return true; }
            if (which == 1 && multiQMatchGetReferenceDb  != nullptr) { multiQMatchGetReferenceDb(i, out, n);  return true; }
            if (which == 2 && multiQMatchGetCorrectionDb != nullptr) { multiQMatchGetCorrectionDb(i, out, n); return true; }
        }
       #else
        (void)which; (void)out; (void)n;
       #endif
        return false;
    }

    // Momentary action button with an explicit enabled state (greyed when the
    // bridge is null or a precondition fails, e.g. MATCH before both spectra).
    template <class Fn>
    void matchActionButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                           const char* label, ImU32 bg, bool enabled, Fn onClick)
    {
        const float s = sc();
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = enabled && ImGui::IsItemHovered();
        if (enabled && ImGui::IsItemClicked()) onClick();
        dl->AddRectFilled(b0, b1, enabled ? bg : IM_COL32(38, 38, 41, 255), 4.f * s);
        dl->AddRect(b0, b1, hov ? IM_COL32(200, 200, 205, 220) : IM_COL32(90, 90, 96, 200), 4.f * s, 0, 1.2f * s);
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.5f * (y1 - y0) - 6.f, 11.f,
                   enabled ? pal().white : IM_COL32(110, 110, 114, 255), label, 0, true);
    }

    //========================================================================
    // MATCH — spectrum-match learn workflow (replaces the dynamics detail panel)
    //========================================================================
    // Uses the same plot rect + log-freq/dB axes as Digital so CURRENT (blue),
    // REFERENCE (green) and CORRECTION (amber) sit on shared axes. Learn/Match/
    // Clear + Limit +/- + Apply + Smoothing wire straight to the committed bridge
    // and the kParamMatch* params. Mirrors JUCE BandDetailPanel::setupMatchControls
    // + EQGraphicDisplay::drawMatchOverlays.
    static constexpr int kMatchBins = 2049;   // == MultiQMatch::NUM_BINS (4096-pt FFT)
    void drawMatch(ImDrawList* dl)
    {
        const float s = sc();
        const bool bridge  = matchBridge();
        const bool learnC  = matchLearningCurrent();
        const bool learnR  = matchLearningReference();
        const bool hasC    = matchHasCurrent();
        const bool hasR    = matchHasReference();
        const bool hasCorr = matchHasCorrection();

        // curve colours (ported from EQGraphicDisplay::drawMatchOverlays)
        const ImU32 curLine = IM_COL32(0x44, 0x88, 0xcc, 0x9a), curFill = IM_COL32(0x44, 0x88, 0xcc, 0x22);
        const ImU32 refLine = IM_COL32(0x44, 0xcc, 0x88, 0x9a), refFill = IM_COL32(0x44, 0xcc, 0x88, 0x22);
        const ImU32 corLine = IM_COL32(0xff, 0xaa, 0x44, 0xe6), corFill = IM_COL32(0xff, 0xaa, 0x44, 0x40);

        // ---- plot frame + background (shared with Digital) ----
        dl->AddRectFilled(panel.P(DGX0 - 3, DGY0 - 3), panel.P(DGX1 + 3, DGY1 + 3), IM_COL32(60, 60, 63, 255), 3.f * s);
        dl->AddRectFilled(panel.P(DGX0, DGY0), panel.P(DGX1, DGY1), IM_COL32(12, 13, 15, 255));
        dl->PushClipRect(panel.P(DGX0, DGY0), panel.P(DGX1, DGY1), true);

        // frequency grid (verbatim from drawDigital)
        for (int i = 0; i < (int)(sizeof(kGridF) / sizeof(kGridF[0])); ++i)
        {
            const float x = DGX0 + flog((float)kGridF[i]) * (DGX1 - DGX0);
            dl->AddLine(panel.P(x, DGY0), panel.P(x, DGY1), IM_COL32(38, 41, 45, 255), 1.f * s);
            panel.text(dl, x, DGY1 - 13, 8.5f, IM_COL32(120, 124, 130, 255), kGridFL[i], 0);
        }
        // dB grid (uses the shared digY scale so 0 dB is centred)
        {
            const int R = (int)digRangeDb(digRangeIdx);
            const int step = digGridStep();
            for (int db = -R; db <= R; db += step)
            {
                const float y = digY((float)db);
                dl->AddLine(panel.P(DGX0, y), panel.P(DGX1, y),
                            db == 0 ? IM_COL32(64, 68, 74, 255) : IM_COL32(30, 33, 37, 255), 1.f * s);
                char b[8]; std::snprintf(b, sizeof(b), "%+d", db);
                float ly = y - 6.f; ly = ly < DGY0 + 1.f ? DGY0 + 1.f : (ly > DGY1 - 13.f ? DGY1 - 13.f : ly);
                panel.text(dl, DGX0 + 5, ly, 9.5f, IM_COL32(150, 154, 160, 255), db == 0 ? "0" : b, -1);
            }
        }

        // ---- the three learned curves ----
        float buf[kMatchBins];
        const double sr = digitalSampleRate();
        float nyq = (float)(sr * 0.5); if (nyq < 1.f) nyq = 22050.f;
        const float binW = nyq / (float)(kMatchBins - 1);
        auto plotCurve = [&](int which, ImU32 lineCol, ImU32 fillCol, bool fromZero)
        {
            if (!matchGetCurve(which, buf, kMatchBins)) return;
            const int N = 320;
            std::vector<ImVec2> line; line.reserve(N);
            for (int i = 0; i < N; ++i)
            {
                const float lx = (float)i / (N - 1);
                const float x  = DGX0 + lx * (DGX1 - DGX0);
                const float f  = std::pow(10.f, std::log10(kFMin) + lx * (std::log10(kFMax) - std::log10(kFMin)));
                int bin = (int)(f / binW); bin = bin < 0 ? 0 : (bin > kMatchBins - 1 ? kMatchBins - 1 : bin);
                line.push_back(panel.P(x, digY(buf[bin])));
            }
            const float baseY = panel.P(0.f, fromZero ? digY(0.f) : DGY1).y;
            for (size_t i = 0; i + 1 < line.size(); ++i)
                dl->AddQuadFilled(line[i], line[i + 1],
                                  ImVec2(line[i + 1].x, baseY), ImVec2(line[i].x, baseY), fillCol);
            dl->AddPolyline(line.data(), (int)line.size(), lineCol, 0, (fromZero ? 2.2f : 1.5f) * s);
        };
        if (hasC)    plotCurve(0, curLine, curFill, false);
        if (hasR)    plotCurve(1, refLine, refFill, false);
        if (hasCorr) plotCurve(2, corLine, corFill, true);
        dl->PopClipRect();

        // ---- legend (top-left of plot) ----
        auto legend = [&](float ly, ImU32 c, const char* t, bool lit)
        {
            dl->AddRectFilled(panel.P(DGX0 + 12, ly), panel.P(DGX0 + 26, ly + 8), lit ? c : IM_COL32(70, 72, 76, 255), 1.5f * s);
            panel.text(dl, DGX0 + 32, ly - 1, 9.5f, lit ? IM_COL32(200, 202, 206, 255) : IM_COL32(120, 122, 126, 255), t, -1);
        };
        legend(DGY0 + 10, curLine, "CURRENT",    hasC);
        legend(DGY0 + 24, refLine, "REFERENCE",  hasR);
        legend(DGY0 + 38, corLine, "CORRECTION", hasCorr);

        // ============ control chassis (replaces the dynamics detail panel) ============
        const float PY0 = 424.f, PY1 = 662.f, DIVX = 624.f;
        dl->AddRectFilled(panel.P(DGX0, PY0), panel.P(DGX1, PY1), IM_COL32(28, 30, 34, 255), 5.f * s);
        dl->AddRect(panel.P(DGX0, PY0), panel.P(DGX1, PY1), IM_COL32(64, 66, 72, 255), 5.f * s, 0, 1.4f * s);
        dl->AddLine(panel.P(DIVX, PY0 + 12), panel.P(DIVX, PY1 - 12), IM_COL32(50, 52, 58, 255), 1.4f * s);
        panel.text(dl, 0.5f * (DGX0 + DIVX), PY0 + 12, 11.f, IM_COL32(150, 152, 156, 255), "LEARN & MATCH", 0, true);
        panel.text(dl, 0.5f * (DIVX + DGX1), PY0 + 12, 11.f, IM_COL32(150, 152, 156, 255), "CORRECTION", 0, true);

        // ---- learn / match / clear buttons ----
        matchActionButton(dl, "mqlc", 70, 452, 330, 488,
                           learnC ? "STOP" : (hasC ? "CURRENT *" : "LEARN CURRENT"),
                           learnC ? IM_COL32(0xcc, 0x44, 0x44, 255)
                                  : (hasC ? IM_COL32(0x2f, 0x52, 0x70, 255) : IM_COL32(0x2a, 0x3a, 0x4a, 255)),
                           bridge, [this, learnC] { matchStartCurrent(!learnC); });
        matchActionButton(dl, "mqlr", 344, 452, 604, 488,
                           learnR ? "STOP" : (hasR ? "REFERENCE *" : "LEARN REFERENCE"),
                           learnR ? IM_COL32(0xcc, 0x44, 0x44, 255)
                                  : (hasR ? IM_COL32(0x2e, 0x66, 0x48, 255) : IM_COL32(0x2a, 0x4a, 0x3a, 255)),
                           bridge, [this, learnR] { matchStartReference(!learnR); });

        const bool canMatch = bridge && hasC && hasR;
        matchActionButton(dl, "mqcompute", 70, 500, 330, 536, "MATCH",
                           hasCorr ? IM_COL32(0xcc, 0x88, 0x44, 255) : IM_COL32(0x5a, 0x40, 0x30, 255),
                           canMatch, [this] { matchCompute(); });
        matchActionButton(dl, "mqclear", 344, 500, 604, 536, "CLEAR",
                           IM_COL32(0x4a, 0x4a, 0x4a, 255), bridge, [this] { matchClear(); });

        // ---- live learning status ----
        char sb[48];
        if (learnC || learnR)
        {
            std::snprintf(sb, sizeof(sb), "%d frames", matchFrames());
            panel.text(dl, 74, 556, 12.f, IM_COL32(232, 224, 120, 255), sb, -1, true);
        }
        else if (!bridge)
            panel.text(dl, 74, 556, 11.f, IM_COL32(150, 152, 156, 255), "Match bridge unavailable (split UI)", -1);
        else
            panel.text(dl, 74, 556, 11.f, hasCorr ? IM_COL32(255, 170, 80, 255) : IM_COL32(140, 142, 146, 255),
                       hasCorr ? "Correction ready" : "Play audio, then Learn", -1);

        panel.text(dl, 74, 578, 10.5f, hasC ? IM_COL32(90, 150, 210, 255) : IM_COL32(112, 114, 118, 255),
                   hasC ? "Current: captured" : "Current: --", -1);
        panel.text(dl, 320, 578, 10.5f, hasR ? IM_COL32(90, 200, 150, 255) : IM_COL32(112, 114, 118, 255),
                   hasR ? "Reference: captured" : "Reference: --", -1);

        // ---- correction settings (right column) ----
        const ImU32 faceCol = IM_COL32(58, 78, 104, 255);
        panel.text(dl, 700, 452, 10.f, IM_COL32(150, 152, 156, 255), "APPLY", 0, true);
        if (panel.knob("mqapply", kParamMatchApply, -100.f, 100.f, 700, 512, 30,
                       values[kParamMatchApply], mqDef(kParamMatchApply), false, true,
                       "%.0f", "%", faceCol, false, true,
                       "Apply amount: 100% full correction, 0% bypass, negative inverts", false, 1.f, 0.f, "APPLY")
            && hasCorr)
            matchCompute();

        panel.text(dl, 840, 452, 10.f, IM_COL32(150, 152, 156, 255), "SMOOTHING", 0, true);
        if (panel.knob("mqsmooth", kParamMatchSmoothing, 1.f, 24.f, 840, 512, 30,
                       values[kParamMatchSmoothing], mqDef(kParamMatchSmoothing), false, true,
                       "%.0f", " st", faceCol, false, true,
                       "Smoothing width in semitones (12 = 1 octave)", false, 1.f, 0.f, "SMOOTHING")
            && hasCorr)
            matchCompute();

        if (panel.toggle("mqlimb", kParamMatchLimitBoost, 646, 576, 762, 600,
                         values[kParamMatchLimitBoost], "LIMIT +") && hasCorr)
            matchCompute();
        if (panel.toggle("mqlimc", kParamMatchLimitCut, 778, 576, 894, 600,
                         values[kParamMatchLimitCut], "LIMIT -") && hasCorr)
            matchCompute();
    }

    // Right-click band context menu (JUCE showBandContextMenu :1860-2012). Opened
    // with band=digCtxBand_ (>=0 over a node, -1 over empty graph). Undo/Redo are
    // omitted: DPF param edits are host-undoable, so there is no internal stack.
    void digDrawContextMenu()
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(222, 224, 228, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(70, 90, 120, 255));
        if (ImGui::BeginPopup("digbandctx"))
        {
            const int  b     = digCtxBand_;
            const int  soloB = digSoloBand();
            const bool delta = digSoloDelta();
            if (b >= 0 && b < 8)
            {
                char hdr[48]; std::snprintf(hdr, sizeof(hdr), "Band %d: %s", b + 1, digBandTypeName(b));
                ImGui::TextDisabled("%s", hdr);
                ImGui::Separator();
                const bool en = values[mqidx::enabled(b)] > 0.5f;
                if (ImGui::MenuItem(en ? "Disable Band" : "Enable Band")) toggleParam(mqidx::enabled(b));
                if (ImGui::MenuItem("Reset to Default", nullptr, false, en)) digResetBand(b, true);
                ImGui::Separator();
                const bool soloOn  = (soloB == b && !delta);
                const bool deltaOn = (soloB == b && delta);
                if (ImGui::MenuItem("Solo This Band", nullptr, soloOn, en))       digSetSolo(soloOn  ? -1 : b, false);
                if (ImGui::MenuItem("Delta Solo (Listen)", nullptr, deltaOn, en)) digSetSolo(deltaOn ? -1 : b, true);
                if (soloB >= 0 && ImGui::MenuItem("Un-solo"))                      digSetSolo(-1, false);
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Enable All Bands"))  for (int i = 0; i < 8; ++i) setParamNotify((uint32_t)mqidx::enabled(i), 1.f);
            if (ImGui::MenuItem("Disable All Bands")) for (int i = 0; i < 8; ++i) setParamNotify((uint32_t)mqidx::enabled(i), 0.f);
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(3);
    }

    // Free-hover readout + crosshair (JUCE :327-386): shown whenever the cursor is
    // inside the plot and no node is being dragged. Four lines: frequency, musical
    // note, cursor dB, live composite EQ response dB at that frequency.
    void digDrawHoverReadout(ImDrawList* dl)
    {
        if (dragBand_ >= 0 || ImGui::IsPopupOpen("digbandctx")) return;
        const ImVec2 mp = invP(ImGui::GetMousePos());
        if (mp.x < DGX0 || mp.x > DGX1 || mp.y < DGY0 || mp.y > DGY1) return;

        const float s = sc();
        const float freq  = digFreqFromX(mp.x);
        const float curDb = digDbFromY(mp.y);
        const float eqDb  = digitalResponseDb(freq);

        // faint crosshair
        dl->PushClipRect(panel.P(DGX0, DGY0), panel.P(DGX1, DGY1), true);
        dl->AddLine(panel.P(mp.x, DGY0), panel.P(mp.x, DGY1), IM_COL32(255, 255, 255, 32), 1.f * s);
        dl->AddLine(panel.P(DGX0, mp.y), panel.P(DGX1, mp.y), IM_COL32(255, 255, 255, 32), 1.f * s);
        dl->PopClipRect();

        char l1[24], l2[24], l3[24], l4[24];
        if (freq >= 1000.f) std::snprintf(l1, sizeof(l1), "%.2f kHz", freq / 1000.f);
        else                std::snprintf(l1, sizeof(l1), "%d Hz", (int)freq);
        freqToNoteName(l2, sizeof(l2), freq);
        std::snprintf(l3, sizeof(l3), "%s%.1f dB", curDb >= 0.f ? "+" : "", curDb);
        std::snprintf(l4, sizeof(l4), "EQ: %s%.1f dB", eqDb >= 0.f ? "+" : "", eqDb);

        const float boxW = 82.f, boxH = 58.f, padX = 7.f;
        float bx = mp.x + 14.f, by = mp.y - boxH - 6.f;
        if (bx + boxW > DGX1) bx = mp.x - boxW - 6.f;
        if (by < DGY0)        by = mp.y + 14.f;
        const ImVec2 p0 = panel.P(bx, by), p1 = panel.P(bx + boxW, by + boxH);
        dl->AddRectFilled(p0, p1, IM_COL32(16, 16, 20, 221), 4.f * s);
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 80), 4.f * s, 0, 1.f * s);
        panel.text(dl, bx + padX, by + 5.f,  9.5f, IM_COL32(221, 221, 221, 255), l1, -1);
        panel.text(dl, bx + padX, by + 18.f, 9.5f, IM_COL32(204, 170, 119, 255), l2, -1);
        panel.text(dl, bx + padX, by + 31.f, 9.5f, IM_COL32(170, 170, 170, 255), l3, -1);
        panel.text(dl, bx + padX, by + 44.f, 9.5f, IM_COL32(136, 204, 255, 255), l4, -1);
    }

    void drawDigital(ImDrawList* dl)
    {
        const float s = sc();
        // ---- plot frame + background (spectrum tap not exposed by core -> dark) ----
        dl->AddRectFilled(panel.P(DGX0 - 3, DGY0 - 3), panel.P(DGX1 + 3, DGY1 + 3), IM_COL32(60, 60, 63, 255), 3.f * s);
        dl->AddRectFilled(panel.P(DGX0, DGY0), panel.P(DGX1, DGY1), IM_COL32(12, 13, 15, 255));
        dl->PushClipRect(panel.P(DGX0, DGY0), panel.P(DGX1, DGY1), true);

        // frequency grid — split into MINOR (~7% white) and MAJOR (~12% white)
        // tiers at different opacities (JUCE drawGrid :462-486). Decade lines
        // (100 / 1k / 10k / 20k) are the brighter major tier + carry the label.
        for (int i = 0; i < (int)(sizeof(kGridF) / sizeof(kGridF[0])); ++i)
        {
            const int f = kGridF[i];
            const bool major = (f == 100 || f == 1000 || f == 10000 || f == 20000);
            const float x = DGX0 + flog((float)f) * (DGX1 - DGX0);
            dl->AddLine(panel.P(x, DGY0), panel.P(x, DGY1),
                        major ? IM_COL32(255, 255, 255, 30) : IM_COL32(255, 255, 255, 16), 1.f * s);
            panel.text(dl, x, DGY1 - 13, 8.5f,
                       major ? IM_COL32(138, 138, 138, 255) : IM_COL32(96, 96, 96, 255), kGridFL[i], 0);
        }
        // piano strip along the bottom of the plot: white/black key ticks with C
        // labels (JUCE drawPianoOverlay :569-636). MIDI 24 (C1) .. 108 (C8); black
        // keys short/dim, white keys medium, C keys full height + label. Sits just
        // above the frequency-axis labels; kept subtle so it reads behind curves.
        {
            static const bool  kBlack[12] = { false,true,false,true,false,false,true,false,true,false,true,false };
            const float stripH = 11.f, stripY = DGY1 - 24.f;
            dl->AddRectFilled(panel.P(DGX0, stripY), panel.P(DGX1, stripY + stripH), IM_COL32(0, 0, 0, 32));
            dl->AddLine(panel.P(DGX0, stripY), panel.P(DGX1, stripY), IM_COL32(255, 255, 255, 32), 0.5f * s);
            for (int midi = 24; midi <= 108; ++midi)
            {
                const float freq = 440.f * std::pow(2.f, (midi - 69) / 12.f);
                if (freq < kFMin || freq > kFMax) continue;
                const float x = DGX0 + flog(freq) * (DGX1 - DGX0);
                const int   noteInOct = midi % 12;
                if (noteInOct == 0)   // C — full-height tick + label
                {
                    dl->AddLine(panel.P(x, stripY), panel.P(x, stripY + stripH), IM_COL32(255, 255, 255, 96), 1.f * s);
                    char lb[8]; std::snprintf(lb, sizeof(lb), "C%d", (midi / 12) - 1);
                    panel.text(dl, x + 2.f, stripY + 1.f, 7.5f, IM_COL32(153, 153, 153, 210), lb, -1);
                }
                else if (kBlack[noteInOct])
                    dl->AddLine(panel.P(x, stripY + stripH * 0.5f), panel.P(x, stripY + stripH), IM_COL32(255, 255, 255, 32), 0.5f * s);
                else
                    dl->AddLine(panel.P(x, stripY + stripH * 0.3f), panel.P(x, stripY + stripH), IM_COL32(255, 255, 255, 48), 0.5f * s);
            }
        }
        // dB grid — horizontal ladder (step per scale). The 0 dB line gets a soft
        // white glow underlay + brighter core (JUCE drawGrid :495-504).
        {
            const int R = (int)digRangeDb(digRangeIdx);
            const int step = digGridStep();
            for (int db = -R; db <= R; db += step)
            {
                const float y = digY((float)db);
                if (db == 0)
                {
                    dl->AddLine(panel.P(DGX0, y), panel.P(DGX1, y), IM_COL32(255, 255, 255, 14), 2.5f * s); // glow
                    dl->AddLine(panel.P(DGX0, y), panel.P(DGX1, y), IM_COL32(255, 255, 255, 64), 1.f * s);  // core
                }
                else
                    dl->AddLine(panel.P(DGX0, y), panel.P(DGX1, y), IM_COL32(255, 255, 255, 16), 1.f * s);
                char b[8]; std::snprintf(b, sizeof(b), "%+d", db);
                float ly = y - 6.f; ly = ly < DGY0 + 1.f ? DGY0 + 1.f : (ly > DGY1 - 13.f ? DGY1 - 13.f : ly);
                panel.text(dl, DGX0 + 5, ly, 9.5f, IM_COL32(150, 154, 160, 255), db == 0 ? "0" : b, -1);
            }
        }

        // live spectrum analyzer behind the response curves (dim, subtle). Gated by
        // the Analyzer ENABLE param (JUCE analyzerEnabled); pre/post, resolution,
        // smoothing, decay, peak-hold + freeze are all driven from the toolbar.
        if (values[kParamAnalyzerEnabled] > 0.5f)
            drawDigitalSpectrum(dl, DGX0, DGY0, DGX1, DGY1);

        // read + smooth the live per-band dynamic-EQ gains for the moving overlay
        updateDynGains();

        // per-band ghost curves (dim) then composite (bright)
        const double sr = digitalSampleRate();
        const int N = 300;
        auto freqAt = [](float lx) { return std::pow(10.f, std::log10(kFMin) + lx * (std::log10(kFMax) - std::log10(kFMin))); };
        for (int b = 0; b < 8; ++b)
        {
            if (values[mqidx::enabled(b)] < 0.5f) continue;
            std::vector<ImVec2> gp; gp.reserve(N);
            for (int i = 0; i < N; ++i)
            {
                const float lx = (float)i / (N - 1);
                const float db = 20.f * std::log10((float)std::max(digitalBandMag(b, freqAt(lx), sr), 1e-6));
                float y = digY(db);
                gp.push_back(panel.P(DGX0 + lx * (DGX1 - DGX0), y));
            }
            const ImU32 col = kDigitalBandCol[b];
            dl->AddPolyline(gp.data(), (int)gp.size(), (col & 0x00FFFFFF) | 0x50000000, 0, 1.2f * s);
        }
        std::vector<ImVec2> pts; pts.reserve(N);
        for (int i = 0; i < N; ++i)
        {
            const float lx = (float)i / (N - 1);
            float y = digY(digitalResponseDb(freqAt(lx)));
            pts.push_back(panel.P(DGX0 + lx * (DGX1 - DGX0), y));
        }
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(236, 236, 236, 255), 0, 2.2f * s);

        // animated DYNAMIC response overlay: recompute the composite with each
        // dyn-enabled band shifted by its live dyn-gain, and draw it (orange, with
        // a translucent fill between static and dynamic) so the band visibly moves
        // as it compresses. Mirrors JUCE EQGraphicDisplay's dynamic curve.
        {
            bool anyDyn = false;
            for (int b = 0; b < 8; ++b)
                if (values[mqidx::enabled(b)] > 0.5f && values[mqidx::dynEnabled(b)] > 0.5f
                    && std::abs(smoothedDynGain_[b]) > 0.05f) { anyDyn = true; break; }
            if (anyDyn)
            {
                std::vector<ImVec2> dp; dp.reserve(N);
                for (int i = 0; i < N; ++i)
                {
                    const float lx = (float)i / (N - 1);
                    dp.push_back(panel.P(DGX0 + lx * (DGX1 - DGX0), digY(digitalResponseDbDyn(freqAt(lx)))));
                }
                for (size_t i = 0; i + 1 < dp.size(); ++i)
                    dl->AddQuadFilled(pts[i], pts[i + 1], dp[i + 1], dp[i], IM_COL32(255, 136, 68, 45));
                dl->AddPolyline(dp.data(), (int)dp.size(), IM_COL32(255, 150, 90, 235), 0, 2.0f * s);
            }
        }

        // DYN threshold: dashed horizontal line + "T: xx dB" for the selected band
        // when its dynamics are enabled (matches JUCE EQGraphicDisplay).
        if (values[mqidx::dynEnabled(selectedBand_)] > 0.5f)
        {
            const float thr = values[mqidx::dynThreshold(selectedBand_)];
            const float ty = digY(thr);
            if (ty > DGY0 + 1.f && ty < DGY1 - 1.f)
            {
                const ImU32 tc = IM_COL32(255, 136, 68, 210);
                for (float x = DGX0; x < DGX1; x += 14.f)
                    dl->AddLine(panel.P(x, ty), panel.P(std::min(x + 8.f, DGX1), ty), tc, 1.4f * s);
                char tb[24]; std::snprintf(tb, sizeof(tb), "T: %d dB", (int)std::lround(thr));
                panel.text(dl, DGX1 - 6, ty - 13, 9.5f, tc, tb, 1, true);
            }
        }
        dl->PopClipRect();

        // ---- draggable band handles ----
        const int soloB = digSoloBand();   // >=0 while a band is soloed (dim others)

        // Full-plot catch-all UNDER the handles: empty-area double-click spectrum-
        // grab + empty right-click menu. AllowOverlap so the handles (submitted
        // after) keep pointer priority; this button only fires on bare graph.
        {
            const ImVec2 bp0 = panel.P(DGX0, DGY0), bp1 = panel.P(DGX1, DGY1);
            ImGui::SetNextItemAllowOverlap();
            ImGui::SetCursorScreenPos(bp0);
            ImGui::InvisibleButton("digplotbg", ImVec2(bp1.x - bp0.x, bp1.y - bp0.y));
            if (ImGui::IsItemHovered())
            {
                if (ImGui::IsMouseDoubleClicked(0)) digSpectrumGrab(invP(ImGui::GetMousePos()));
                else if (ImGui::IsMouseClicked(1)) { digCtxBand_ = -1; ImGui::OpenPopup("digbandctx"); }
            }
        }

        for (int b = 0; b < 8; ++b)
        {
            if (values[mqidx::enabled(b)] < 0.5f) continue;
            // Node Y reflects gain only for peaking shapes (JUCE getControlPoint-
            // Position); shelves/tilt sit on 0 dB even though their gain is drag-
            // able. The DRAG, however, uses digBandDragGain (shelves + tilt too).
            const bool  showGain = digBandCarriesGain(b);
            const float gainVal  = showGain ? values[mqidx::gain(b)] : 0.f;
            const float freqVal  = values[mqidx::freq(b)];
            const bool  dynOn    = values[mqidx::dynEnabled(b)] > 0.5f;
            // Live dynamic offset (JUCE getControlPointPosition adds smoothedDynamic-
            // Gains). The node + its hit target follow the MOVED position so grabbing
            // the visible node works; the drag is relative, so the dyn bounce never
            // fights an active drag. hyStatic marks where the node rests (ghost).
            const float dynOff  = dynOn ? smoothedDynGain_[b] : 0.f;
            const float hx = DGX0 + flog(freqVal) * (DGX1 - DGX0);
            const float hyStatic = digY(gainVal);
            const float hy = digY(gainVal + dynOff);
            const ImVec2 hc = panel.P(hx, hy);
            const float hit = 11.f * s;
            char id[16]; std::snprintf(id, sizeof(id), "dh%d", b);
            ImGui::SetCursorScreenPos(ImVec2(hc.x - hit, hc.y - hit));
            ImGui::InvisibleButton(id, ImVec2(2.f * hit, 2.f * hit));
            const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

            // ---- drag begin: latch mode + start values at mouse-down (JUCE
            // mouseDown :1311-1350). Modifiers are captured ONCE here so releasing
            // a key mid-drag never switches modes. Double-click / Alt reset early.
            if (ImGui::IsItemActivated())
            {
                selectedBand_ = b;   // selecting a handle drives the detail panel
                const ImGuiIO& io = ImGui::GetIO();
                const bool cmd = io.KeyCtrl || io.KeySuper;   // Ctrl(Win/Lin) == Cmd(Mac)
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    digResetBand(b, /*resetShape=*/true);     // JUCE mouseDoubleClick on node
                    digDragMode_ = DigDrag::None;
                }
                else if (io.KeyAlt && !cmd)
                {
                    digResetBand(b, /*resetShape=*/false);    // JUCE Alt reset :1311-1325
                    digDragMode_ = DigDrag::None;
                }
                else
                {
                    if (io.KeyAlt && cmd)  digDragMode_ = DigDrag::QOnly;
                    else if (cmd)          digDragMode_ = DigDrag::GainOnly;
                    else if (io.KeyShift)  digDragMode_ = DigDrag::FreqOnly;
                    else                   digDragMode_ = DigDrag::FreqGain;
                    digDragStartFreq_  = values[mqidx::freq(b)];
                    digDragStartGain_  = (mqidx::gain(b) >= 0) ? values[mqidx::gain(b)] : 0.f;
                    digDragStartQ_     = values[mqidx::q(b)];
                    digDragStartMouse_ = invP(ImGui::GetMousePos());
                    editParameter(mqidx::freq(b), true);
                    if (mqidx::gain(b) >= 0) editParameter(mqidx::gain(b), true);
                    editParameter(mqidx::q(b), true);
                    dragBand_ = b;
                }
            }
            // ---- drag move: relative deltas from mouse-down (JUCE mouseDrag
            // :1370-1435). Ctrl/Cmd held = 10x finer (checked live like JUCE).
            if (act && dragBand_ == b && digDragMode_ != DigDrag::None)
            {
                const ImGuiIO& io = ImGui::GetIO();
                const ImVec2 md = invP(ImGui::GetMousePos());
                const bool  fine = io.KeyCtrl || io.KeySuper;
                float dX = md.x - digDragStartMouse_.x;
                float dY = md.y - digDragStartMouse_.y;
                if (fine) { dX *= 0.1f; dY *= 0.1f; }
                const float plotW = DGX1 - DGX0;
                // Gain delta comes from the (possibly Warped/±60) inverse mapping so
                // the node tracks the cursor exactly. For the linear scales this is
                // identical to the old -(dY/plotH)*span; for Warped it follows the
                // sqrt axis. Fine mode scales the dB delta by 0.1 like freq/Q.
                float gainDelta = digDbFromY(md.y) - digDbFromY(digDragStartMouse_.y);
                if (fine) gainDelta *= 0.1f;
                auto setF = [&](float f) { f = f < kFMin ? kFMin : (f > kFMax ? kFMax : f);
                                           values[mqidx::freq(b)] = f; setParameterValue(mqidx::freq(b), f); };
                auto setG = [&](float g) { if (!digBandDragGain(b)) return;
                                           g = g < -24.f ? -24.f : (g > 24.f ? 24.f : g);
                                           values[mqidx::gain(b)] = g; setParameterValue(mqidx::gain(b), g); };
                auto setQ = [&](float q) { q = q < 0.1f ? 0.1f : (q > 100.f ? 100.f : q);
                                           values[mqidx::q(b)] = q; setParameterValue(mqidx::q(b), q); };
                switch (digDragMode_)
                {
                    case DigDrag::FreqGain:
                        setF(digDragStartFreq_ * std::pow(kFMax / kFMin, dX / plotW));
                        setG(digDragStartGain_ + gainDelta);
                        break;
                    case DigDrag::GainOnly:
                        setG(digDragStartGain_ + gainDelta);
                        break;
                    case DigDrag::FreqOnly:
                        setF(digDragStartFreq_ * std::pow(kFMax / kFMin, dX / plotW));
                        break;
                    case DigDrag::QOnly:
                        setQ(digDragStartQ_ * std::pow(2.f, -dY / 50.f));
                        break;
                    default: break;
                }
            }
            if (ImGui::IsItemDeactivated() && dragBand_ == b)
            {
                editParameter(mqidx::freq(b), false);
                if (mqidx::gain(b) >= 0) editParameter(mqidx::gain(b), false);
                editParameter(mqidx::q(b), false);
                dragBand_ = -1;
                digDragMode_ = DigDrag::None;
            }
            // Right-click a node -> that band's context menu (JUCE :1301-1309).
            if (hov && ImGui::IsMouseClicked(1)) { digCtxBand_ = b; selectedBand_ = b; ImGui::OpenPopup("digbandctx"); }
            // Wheel over a band handle adjusts that band's Q (JUCE
            // EQGraphicDisplay::mouseWheelMove: newQ = Q * 1.15^(wheel*3)).
            if (hov && !act)
            {
                const float wh = ImGui::GetIO().MouseWheel;
                if (wh != 0.f)
                {
                    float qv = values[mqidx::q(b)] * std::pow(1.15f, wh * 3.0f);
                    qv = qv < 0.1f ? 0.1f : (qv > 100.f ? 100.f : qv);
                    editParameter(mqidx::q(b), true);
                    values[mqidx::q(b)] = qv; setParameterValue(mqidx::q(b), qv);
                    editParameter(mqidx::q(b), false);
                    selectedBand_ = b;
                }
            }
            ImU32 col = kDigitalBandCol[b];
            const bool soloDim = (soloB >= 0 && b != soloB);
            const bool soloOn  = (soloB == b);
            if (soloDim) col = (col & 0x00FFFFFF) | 0x55000000;   // dim non-soloed nodes
            const bool sel = (b == selectedBand_);
            const ImU32 colRGB = col & 0x00FFFFFF;
            const float rr     = (hov || act || sel) ? 8.5f * s : 7.0f * s;
            const float ringW  = (sel ? 3.0f : (hov ? 2.5f : 2.0f)) * s;
            const float innerR = rr - ringW;

            // ---- STALK: node -> 0 dB line (behind the node; JUCE :819-844) ----
            {
                const ImVec2 z = panel.P(hx, digY(0.f));
                const ImU32 sa = (ImU32)(sel ? 150 : (hov ? 100 : 64)) << 24;
                dl->AddLine(hc, z, colRGB | sa, (sel ? 2.4f : (hov ? 2.0f : 1.5f)) * s);
            }
            // ---- GHOST ring + tether at the static rest position while dynamics
            //      move the node (JUCE :918-934) ----
            if (dynOn && std::abs(dynOff) > 0.5f)
            {
                const ImVec2 gc = panel.P(hx, hyStatic);
                dl->AddLine(gc, hc, colRGB | 0x28000000, 1.0f * s);
                dl->AddCircle(gc, rr * 0.72f, colRGB | 0x40000000, 20, 1.5f * s);
            }
            // ---- soft outer glow (selected / hovered / has-gain) ----
            if (sel)          dl->AddCircleFilled(hc, rr + 5.f * s, colRGB | 0x55000000, 24);
            else if (hov)     dl->AddCircleFilled(hc, rr + 3.f * s, colRGB | 0x33000000, 24);
            // ---- body: drop shadow, coloured ring, dark centre, rim highlights ----
            dl->AddCircleFilled(ImVec2(hc.x + 1.5f * s, hc.y + 1.5f * s), rr, IM_COL32(0, 0, 0, 110), 24);
            dl->AddCircleFilled(hc, rr, col, 24);
            dl->AddCircleFilled(hc, innerR, IM_COL32(18, 19, 23, 235), 24);
            dl->AddCircle(hc, innerR - 0.5f * s, colRGB | 0x66000000, 24, 0.9f * s);
            dl->AddCircle(hc, rr, IM_COL32(255, 255, 255, (hov || act || sel) ? 235 : 120), 24, sel ? 2.0f * s : 1.5f * s);
            if (sel)    dl->AddCircle(hc, rr + 1.0f * s, IM_COL32(255, 255, 255, 150), 24, 1.2f * s);
            if (soloOn) dl->AddCircle(hc, rr + 4.f * s, IM_COL32(255, 230, 120, 235), 24, 2.0f * s); // solo ring
            // ---- SHAPE GLYPH (or band number) inside the node (JUCE :1008-1101) ----
            digNodeGlyph(dl, b, hx, hy, innerR * 1.1f, (sel ? 2.0f : 1.5f) * s,
                         IM_COL32(236, 238, 242, sel ? 255 : 225));
            // ---- green->yellow DYNAMIC-ACTIVITY ARC around the node (JUCE :1104-1132) ----
            if (dynOn && std::abs(dynOff) > 0.5f)
            {
                const float ng = std::min(std::abs(dynOff) / 24.f, 1.f);
                const float ar = rr + 4.f * s;
                const float a0 = -1.57079633f;                    // top
                const float a1 = a0 + ng * 6.2831853f * 0.8f;     // clockwise sweep
                auto lerpCol = [](ImU32 a, ImU32 bb, float t) {
                    auto ch = [](ImU32 c, int sh) { return (float)((c >> sh) & 0xFF); };
                    const int r  = (int)(ch(a, 0)  + (ch(bb, 0)  - ch(a, 0))  * t);
                    const int gg = (int)(ch(a, 8)  + (ch(bb, 8)  - ch(a, 8))  * t);
                    const int bl = (int)(ch(a, 16) + (ch(bb, 16) - ch(a, 16)) * t);
                    return IM_COL32(r, gg, bl, 255);
                };
                const ImU32 ac = lerpCol(IM_COL32(0, 204, 102, 255), IM_COL32(255, 204, 0, 255), ng * 0.7f);
                dl->PathArcTo(hc, ar, a0, a1, 32); dl->PathStroke((ac & 0x00FFFFFF) | 0x4D000000, 0, 4.5f * s);
                dl->PathArcTo(hc, ar, a0, a1, 32); dl->PathStroke((ac & 0x00FFFFFF) | 0xE6000000, 0, 2.5f * s);
            }
            // Per-node bubble ONLY while dragging (JUCE drag tooltip); free-hover
            // over a node instead shows the 4-line readout below.
            if (act)
            {
                char bub[40];
                if (digBandDragGain(b))
                {
                    const float gv = values[mqidx::gain(b)];
                    std::snprintf(bub, sizeof(bub), values[mqidx::freq(b)] >= 1000.f ? "%.2fk  %+.1f dB" : "%.0f Hz  %+.1f dB",
                                  values[mqidx::freq(b)] >= 1000.f ? values[mqidx::freq(b)] / 1000.f : values[mqidx::freq(b)], gv);
                }
                else
                {
                    std::snprintf(bub, sizeof(bub), values[mqidx::freq(b)] >= 1000.f ? "%.2f kHz  Q %.2f" : "%.0f Hz  Q %.2f",
                                  values[mqidx::freq(b)] >= 1000.f ? values[mqidx::freq(b)] / 1000.f : values[mqidx::freq(b)], values[mqidx::q(b)]);
                }
                panel.valueBubble(dl, hx, hy, 8.f, bub);
            }
        }

        // ---- processing-mode badge: a LEFT/RIGHT/MID/SIDE pill at the plot's
        //      top-right when processing_mode != Stereo (JUCE :259-285). ----
        {
            const int pm = (int)std::lround(values[kParamProcessingMode]);
            if (pm > 0)
            {
                static const char* kPm[5] = { "", "LEFT", "RIGHT", "MID", "SIDE" };
                const char* txt = kPm[pm < 0 ? 0 : (pm > 4 ? 4 : pm)];
                ImFont* f = panel.pickFont(11.f * s);
                const float tw = f->CalcTextSizeA(11.f * s, FLT_MAX, 0.f, txt).x / s + 12.f;
                const float bx = DGX1 - tw - 6.f, by = DGY0 + 6.f, bh = 18.f;
                const ImVec2 p0 = panel.P(bx, by), p1 = panel.P(bx + tw, by + bh);
                dl->AddRectFilled(p0, p1, IM_COL32(26, 26, 46, 204), 4.f * s);
                dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 96), 4.f * s, 0, 1.f * s);
                panel.text(dl, bx + 0.5f * tw, by + 0.5f * bh - 5.5f, 11.f, IM_COL32(255, 255, 255, 221), txt, 0, true);
            }
        }

        digDrawContextMenu();
        digDrawHoverReadout(dl);

        drawDigitalMeters(dl);
        drawDigitalStrips(dl);
        drawDetailPanel(dl);
    }

    //========================================================================
    // IN/OUT meters flanking the Digital graph (thin vertical strips). Reuses the
    // same same-process bridge accessors + green->yellow->red look as British.
    //========================================================================
    void drawDigitalMeters(ImDrawList* dl)
    {
        float inL = 0, inR = 0, outL = 0, outR = 0;
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQGetInputPeakL != nullptr) // weak: null in the split LV2 UI
            if (void* inst = getPluginInstancePointer())
            {
                inL  = multiQGetInputPeakL(inst);  inR  = multiQGetInputPeakR(inst);
                outL = multiQGetOutputPeakL(inst); outR = multiQGetOutputPeakR(inst);
            }
       #endif
        panel.text(dl, 0.5f * (DINX0 + DINX1),  DGY0 - 13, 9.f, IM_COL32(160, 162, 166, 255), "IN",  0, true);
        panel.text(dl, 0.5f * (DOUTX0 + DOUTX1), DGY0 - 13, 9.f, IM_COL32(160, 162, 166, 255), "OUT", 0, true);
        digMeterPair(dl, DINX0, DINX1, inL, inR, 0);
        digMeterPair(dl, DOUTX0, DOUTX1, outL, outR, 2);

        // small numeric dB readout under each meter (British already has one). Held
        // ~0.15 s so it stays legible. Sits in the gap between plot and band strips.
        digMtRdIn_  = std::max(digMtRdIn_,  std::max(inL, inR));
        digMtRdOut_ = std::max(digMtRdOut_, std::max(outL, outR));
        digMtRdTimer_ += ImGui::GetIO().DeltaTime;
        if (digMtRdTimer_ >= 0.15f)
        {
            digMtRdTimer_ = 0.f;
            digMtDbIn_  = 20.f * std::log10(digMtRdIn_  > 1e-5f ? digMtRdIn_  : 1e-5f);
            digMtDbOut_ = 20.f * std::log10(digMtRdOut_ > 1e-5f ? digMtRdOut_ : 1e-5f);
            digMtRdIn_ = digMtRdOut_ = 0.f;
        }
        char rb[12];
        std::snprintf(rb, sizeof(rb), "%d", (int)std::lround(digMtDbIn_));
        panel.text(dl, 0.5f * (DINX0 + DINX1), DGY1 + 1.f, 8.f, IM_COL32(150, 152, 156, 255), rb, 0);
        std::snprintf(rb, sizeof(rb), "%d", (int)std::lround(digMtDbOut_));
        panel.text(dl, 0.5f * (DOUTX0 + DOUTX1), DGY1 + 1.f, 8.f, IM_COL32(150, 152, 156, 255), rb, 0);
    }

    void digMeterPair(ImDrawList* dl, float x0, float x1, float l, float r, int slotBase)
    {
        dl->AddRectFilled(panel.P(x0 - 2, DGY0 - 2), panel.P(x1 + 2, DGY1 + 2), IM_COL32(60, 60, 63, 255), 2.f * sc());
        dl->AddRectFilled(panel.P(x0, DGY0), panel.P(x1, DGY1), IM_COL32(10, 11, 13, 255));
        const float mid = 0.5f * (x0 + x1);
        ledMeterBar(dl, x0 + 1, mid - 0.5f, DGY0, DGY1, l, slotBase);
        ledMeterBar(dl, mid + 0.5f, x1 - 1, DGY0, DGY1, r, slotBase + 1);
    }

    // Discrete LED-segment meter (JUCE shared LEDMeter look): stacked segments —
    // green to ~-12 dB, yellow to ~-3 dB, red near 0 — with a peak-hold cap segment
    // and a clip box at the very top that latches red. `slot` indexes the per-channel
    // peak/clip hold state (0-3 Digital in/out L/R, 4-7 British in/out L/R).
    void ledMeterBar(ImDrawList* dl, float x0, float x1, float y0, float y1, float lin, int slot)
    {
        const float s  = sc();
        const float db = 20.f * std::log10(lin > 1e-5f ? lin : 1e-5f);
        const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
        if (db > ledPk_[slot]) ledPk_[slot] = db; else ledPk_[slot] -= 18.f * dt; // ~18 dB/s fall
        if (ledPk_[slot] < -60.f) ledPk_[slot] = -60.f;
        if (db >= -0.3f) ledClip_[slot] = 1.5f; else ledClip_[slot] = std::max(0.f, ledClip_[slot] - dt);

        const float capH = 5.f;
        dl->AddRectFilled(panel.P(x0, y0), panel.P(x1, y0 + capH),
                          ledClip_[slot] > 0.f ? IM_COL32(255, 64, 48, 255) : IM_COL32(48, 20, 20, 255));
        const float sTop = y0 + capH + 1.f, sBot = y1;
        auto frac = [](float d) { float t = (d + 60.f) / 60.f; return t < 0.f ? 0.f : (t > 1.f ? 1.f : t); };
        const float tv = frac(db), tp = frac(ledPk_[slot]);
        const int n = 20;
        for (int i = 0; i < n; ++i)
        {
            const float f0 = (float)i / n, f1 = (float)(i + 1) / n;
            const float segDbTop = -60.f + f1 * 60.f;
            const float ry1 = sBot - f0 * (sBot - sTop);
            const float ry0 = sBot - f1 * (sBot - sTop);
            const ImU32 on = segDbTop > -3.f  ? IM_COL32(226, 70, 55, 255)
                           : segDbTop > -12.f ? IM_COL32(224, 196, 72, 255)
                           :                    IM_COL32(96, 196, 112, 255);
            const bool lit = tv >= f0 + 1e-4f;
            const bool pk  = (ledPk_[slot] > -59.f) && tp >= f0 && tp < f1;
            const ImU32 c  = (lit || pk) ? on : IM_COL32(28, 30, 33, 255);
            dl->AddRectFilled(panel.P(x0, ry0 + 0.8f * s), panel.P(x1, ry1), c);
        }
    }

    // Format a frequency the JUCE way (BandDetailPanel::formatFreq): int Hz below
    // 1 kHz, 2-dec kHz to 10 kHz, 1-dec kHz above (e.g. "80 Hz", "2.00 kHz", "12.0 kHz").
    static void formatFreqShort(char* buf, size_t n, float f)
    {
        if (f >= 10000.f)     std::snprintf(buf, n, "%.1f kHz", f / 1000.f);
        else if (f >= 1000.f) std::snprintf(buf, n, "%.2f kHz", f / 1000.f);
        else                  std::snprintf(buf, n, "%.0f Hz", f);
    }

    // Editable strip field: a small boxed value that types-to-edit on double-click
    // (parse via the shared inline editor, then clamp to the param range + notify the
    // host), and selects the band on a single click. Mirrors JUCE BandStripComponent's
    // double-click-to-type freq/gain/Q labels. Plain numeric entry (no "1.2k" suffix
    // — the shared editor parses with atof; values pre-fill from the current setting).
    void stripField(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                    int b, uint32_t pid, int fmt, bool en)
    {
        const float s = sc();
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetNextItemAllowOverlap();
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        const bool editing = panel.isEditingValue(id);
        if (!editing)
        {
            if (ImGui::IsItemClicked()) selectedBand_ = b;
            if (hov && ImGui::IsMouseDoubleClicked(0)) { selectedBand_ = b; panel.openValueEdit(id, values[pid]); }
        }
        dl->AddRectFilled(b0, b1, hov ? IM_COL32(40, 40, 46, 255) : IM_COL32(22, 22, 26, 255), 2.f * s);
        dl->AddRect(b0, b1, hov ? IM_COL32(130, 132, 136, 210) : IM_COL32(58, 58, 64, 180), 2.f * s, 0, 1.f * s);
        const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
        float typed;
        if (panel.valueEdit(id, cx, cy, 0.f, typed))
        {
            const float mn = kMqParams[pid].min, mx = kMqParams[pid].max;
            typed = typed < mn ? mn : (typed > mx ? mx : typed);
            editParameter(pid, true); values[pid] = typed; setParameterValue(pid, typed); editParameter(pid, false);
        }
        else
        {
            char vb[24]; fmtDetail(vb, sizeof(vb), fmt, values[pid]);
            panel.text(dl, cx, cy - 5.f, 9.f, en ? IM_COL32(214, 216, 220, 255) : IM_COL32(120, 122, 126, 255), vb, 0, true);
        }
    }

    // One-row band strip: full-width edge-to-edge cells. Each cell has a band-colour
    // ACCENT BAR on top, "N:Name", a small enable dot (top-right, click toggles), and
    // editable FREQ + Q fields plus a GAIN field (gain bands) or SLOPE selector (HPF/
    // LPF) — all double-click-to-type / click-to-select. Selected cell = brighter bg.
    void drawDigitalStrips(ImDrawList* dl)
    {
        const float s = sc();
        const float cw = (DSTRIP_X1 - DSTRIP_X0) / 8.f;
        const int soloB = digSoloBand();   // >=0 while a band is soloed
        for (int b = 0; b < 8; ++b)
        {
            const float x0 = DSTRIP_X0 + b * cw, x1 = x0 + cw;
            const float cx = 0.5f * (x0 + x1);
            const bool en = values[mqidx::enabled(b)] > 0.5f;
            const bool selCell = (b == selectedBand_);
            const bool edge = mqidx::isEdge(b);
            const ImU32 col = kDigitalBandCol[b];

            // full-cell background (edge-to-edge; selected brighter)
            const ImVec2 c0 = panel.P(x0, DSTRIP_Y0), c1 = panel.P(x1, DSTRIP_Y1);
            dl->AddRectFilled(c0, c1, selCell ? IM_COL32(50, 52, 58, 255) : IM_COL32(28, 28, 32, 255));
            if (b > 0) dl->AddLine(panel.P(x0, DSTRIP_Y0), panel.P(x0, DSTRIP_Y1), IM_COL32(16, 16, 18, 255), 1.f * s);

            // band-colour accent bar along the TOP edge (dim when disabled)
            dl->AddRectFilled(panel.P(x0, DSTRIP_Y0), panel.P(x1, DSTRIP_Y0 + 3),
                              en ? col : (col & 0x00FFFFFF) | 0x40000000);

            // whole-cell catch-all UNDER the fields: click selects, enable-dot toggles.
            char cid[16]; std::snprintf(cid, sizeof(cid), "strip%d", b);
            ImGui::SetNextItemAllowOverlap();
            ImGui::SetCursorScreenPos(c0);
            ImGui::InvisibleButton(cid, ImVec2(c1.x - c0.x, c1.y - c0.y));
            if (ImGui::IsItemClicked())
            {
                const ImVec2 md = invP(ImGui::GetMousePos());
                if (md.x >= x1 - 18 && md.y <= DSTRIP_Y0 + 18)
                    toggleParam(mqidx::enabled(b));
                else
                    selectedBand_ = b;
            }

            // "N:Name"
            char nm[24]; std::snprintf(nm, sizeof(nm), "%d:%s", b + 1, kDigitalBandShort[b]);
            panel.text(dl, cx - 6.f, DSTRIP_Y0 + 8, 9.5f,
                       en ? IM_COL32(230, 230, 232, 255) : IM_COL32(128, 130, 134, 255), nm, 0, true);

            // small enable indicator (top-right) — sits over the catch-all, so it needs
            // its own hit region priority handled by the md.x test above.
            const ImVec2 dc = panel.P(x1 - 10, DSTRIP_Y0 + 9);
            dl->AddCircleFilled(dc, 3.6f * s, en ? col : IM_COL32(58, 58, 62, 255), 14);
            dl->AddCircle(dc, 3.6f * s, IM_COL32(0, 0, 0, 160), 14, 1.f * s);

            // editable fields: FREQ | Q (row 1), GAIN or SLOPE (row 2)
            char fid[24];
            const float mid = cx;
            std::snprintf(fid, sizeof(fid), "sf%d", b);
            stripField(dl, fid, x0 + 4, DSTRIP_Y0 + 20, mid - 1, DSTRIP_Y0 + 34, b, (uint32_t)mqidx::freq(b), FMT_FREQ, en);
            std::snprintf(fid, sizeof(fid), "sq%d", b);
            stripField(dl, fid, mid + 1, DSTRIP_Y0 + 20, x1 - 4, DSTRIP_Y0 + 34, b, (uint32_t)mqidx::q(b), FMT_Q, en);
            if (edge)
            {
                std::snprintf(fid, sizeof(fid), "ssl%d", b);
                detailSelector(dl, fid, x0 + 4, DSTRIP_Y0 + 37, x1 - 4, DSTRIP_Y0 + 51, (uint32_t)mqidx::slope(b), false);
            }
            else
            {
                std::snprintf(fid, sizeof(fid), "sg%d", b);
                stripField(dl, fid, x0 + 4, DSTRIP_Y0 + 37, x1 - 4, DSTRIP_Y0 + 51, b, (uint32_t)mqidx::gain(b), FMT_GAIN, en);
            }

            // Solo reflection: dim non-soloed cells, ring the soloed one gold.
            if (soloB >= 0 && b != soloB) dl->AddRectFilled(c0, c1, IM_COL32(0, 0, 0, 90));
            if (soloB == b)               dl->AddRect(c0, c1, IM_COL32(255, 230, 120, 235), 0, 0, 2.f * s);
        }
    }

    //========================================================================
    // DIGITAL — bottom band-detail panel for the SELECTED band. Mirrors the JUCE
    // BandDetailPanel: band badge + FREQ/Q/GAIN(or SLOPE) + INV/PH/SOLO/routing
    // + PAN + DYNAMICS (THRESH/ATTACK/RELEASE/RANGE/RATIO + DYN). Knob value-arcs
    // + badge use the band colour; the dynamics section dims when DYN is off.
    //========================================================================
    enum DetFmt { FMT_FREQ, FMT_GAIN, FMT_Q, FMT_MS, FMT_DB, FMT_RATIO, FMT_PAN };

    void fmtDetail(char* buf, size_t n, int fmt, float v) const
    {
        switch (fmt)
        {
            case FMT_FREQ:  formatFreqShort(buf, n, v); break;
            case FMT_GAIN:  std::snprintf(buf, n, "%+.1f dB", v); break;
            case FMT_Q:     std::snprintf(buf, n, "%.2f", v); break;
            case FMT_MS:    if (v >= 1000.f) std::snprintf(buf, n, "%.1f s", v / 1000.f);
                            else             std::snprintf(buf, n, "%.0f ms", v); break;
            case FMT_DB:    std::snprintf(buf, n, "%d dB", (int)std::lround(v)); break;
            case FMT_RATIO: std::snprintf(buf, n, "%.1f:1", v); break;
            case FMT_PAN:   { const int p = (int)std::lround(std::fabs(v) * 100.f);
                              if (p == 0) std::snprintf(buf, n, "C");
                              else std::snprintf(buf, n, "%d%s", p, v < 0.f ? "L" : "R"); } break;
            default:        std::snprintf(buf, n, "%.1f", v); break;
        }
    }

    // Band-coloured rotary. logScale drags in log space (freq/Q/attack/release/
    // ratio); linear otherwise (gain/threshold/range/pan). Value drawn inside the
    // cap (JUCE F6 style); label above; dim greys the whole knob (DYN off).
    void detailKnob(ImDrawList* dl, const char* id, float cx, float cy, float R,
                    uint32_t pid, float minV, float maxV, bool logScale,
                    ImU32 capCol, const char* label, int fmt, bool dim)
    {
        const float s = sc();
        const ImVec2 c = panel.P(cx, cy);
        const float RR = R * s;
        const float lmin = std::log10(std::max(1e-4f, minV)), lmax = std::log10(std::max(1e-4f, maxV));
        auto toT = [&](float v) {
            if (logScale) return clamp01((std::log10(std::max(minV, v)) - lmin) / (lmax - lmin));
            return clamp01((v - minV) / (maxV - minV));
        };
        auto toV = [&](float t) {
            if (logScale) return std::pow(10.f, lmin + t * (lmax - lmin));
            return minV + t * (maxV - minV);
        };
        float t = toT(values[pid]);

        ImGui::SetCursorScreenPos(ImVec2(c.x - RR, c.y - RR));
        ImGui::InvisibleButton(id, ImVec2(2.f * RR, 2.f * RR));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        const bool editing = panel.isEditingValue(id);
        const bool modKey = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        auto apply = [&](float tt) { const float v = toV(tt); values[pid] = v; setParameterValue(pid, v); };
        if (!editing)
        {
            if (ImGui::IsItemActivated())
            {
                if (modKey) { editParameter(pid, true); values[pid] = mqDef(pid); setParameterValue(pid, mqDef(pid)); editParameter(pid, false); t = toT(values[pid]); stepModReset_ = true; }
                else { editParameter(pid, true); stepDragT = t; stepModReset_ = false; }
            }
            if (act && !stepModReset_)
            {
                const float sp = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
                stepDragT = clamp01(stepDragT - ImGui::GetIO().MouseDelta.y * sp);
                t = stepDragT; apply(t);
            }
            if (ImGui::IsItemDeactivated()) { if (!stepModReset_) editParameter(pid, false); stepModReset_ = false; }
            if (!modKey && (hov || act) && ImGui::IsMouseDoubleClicked(0)) { panel.openValueEdit(id, values[pid]); editParameter(pid, false); }
            // Wheel is its OWN branch (never gated by the double-click check) so a
            // scroll frame is never skipped; works even when the knob is dimmed.
            if (hov && !act) { const float wh = ImGui::GetIO().MouseWheel; if (wh != 0.f) { t = clamp01(t + wh * 0.02f); editParameter(pid, true); apply(t); editParameter(pid, false); } }
        }

        // label above
        panel.text(dl, cx, cy - R - 16.f, 10.5f,
                   dim ? IM_COL32(120, 122, 126, 255) : IM_COL32(176, 178, 182, 255), label, 0, true);

        // band-coloured value arc around the knob
        {
            const float aS = duskdpf::DuskPanel::knobAngle(0.f);
            const float aE = duskdpf::DuskPanel::knobAngle(t);
            const float ar = RR + 5.f * s;
            const int seg = 34;
            ImVec2 prev(c.x + std::sin(aS) * ar, c.y - std::cos(aS) * ar);
            const ImU32 arcCol = dim ? IM_COL32(96, 98, 102, 255) : capCol;
            for (int i = 1; i <= seg; ++i)
            {
                const float a = aS + (aE - aS) * (float)i / seg;
                const ImVec2 p(c.x + std::sin(a) * ar, c.y - std::cos(a) * ar);
                dl->AddLine(prev, p, arcCol, 3.2f * s);
                prev = p;
            }
        }
        drawMetalKnobBody(dl, c, RR, t, IM_COL32(46, 48, 52, 255),
                          dim ? IM_COL32(150, 152, 156, 255) : capCol);

        float typed;
        if (panel.valueEdit(id, cx, cy, R, typed))
        {
            typed = typed < minV ? minV : (typed > maxV ? maxV : typed);
            editParameter(pid, true); values[pid] = typed; setParameterValue(pid, typed); editParameter(pid, false);
        }
        else
        {
            char b[24]; fmtDetail(b, sizeof(b), fmt, values[pid]);
            panel.text(dl, cx, cy - 6.f, 11.f,
                       dim ? IM_COL32(150, 148, 142, 200) : IM_COL32(232, 224, 216, 255), b, 0, true);
        }
    }

    // Compact click-to-cycle choice box (routing / slope) for the detail panel.
    void detailSelector(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                        uint32_t pid, bool dim)
    {
        const float s = sc();
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (!dim && ImGui::IsItemClicked()) cycleChoice(pid);
        dl->AddRectFilled(b0, b1, IM_COL32(30, 30, 34, 255), 3.f * s);
        dl->AddRect(b0, b1, hov && !dim ? IM_COL32(150, 152, 156, 220) : IM_COL32(84, 84, 90, 200), 3.f * s, 0, 1.2f * s);
        const int i = choiceIdx(pid);
        const char* lbl = kMqParams[pid].choices ? kMqParams[pid].choices[i] : "";
        panel.text(dl, 0.5f * (x0 + x1) - 3.f, y0 + 0.30f * (y1 - y0), 9.5f,
                   dim ? IM_COL32(120, 122, 126, 255) : IM_COL32(210, 212, 216, 255), lbl, 0, true);
        const ImVec2 ac = panel.P(x1 - 8.f, 0.5f * (y0 + y1));
        dl->AddTriangleFilled(ImVec2(ac.x - 3.f * s, ac.y - 2.f * s), ImVec2(ac.x + 3.f * s, ac.y - 2.f * s),
                              ImVec2(ac.x, ac.y + 2.5f * s), IM_COL32(170, 172, 176, 255));
    }

    // Phase-invert button drawing a circle-slash icon (font atlas lacks "Ø").
    template <class Fn>
    void phaseButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                     bool on, Fn onClick)
    {
        const float s = sc();
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) onClick();
        dl->AddRectFilled(b0, b1, on ? IM_COL32(60, 74, 96, 255) : IM_COL32(46, 46, 50, 255), 4.f * s);
        dl->AddRect(b0, b1, hov ? IM_COL32(200, 200, 205, 220) : IM_COL32(90, 90, 96, 200), 4.f * s, 0, 1.2f * s);
        const ImVec2 c(0.5f * (b0.x + b1.x), 0.5f * (b0.y + b1.y));
        const float r = 4.6f * s;
        const ImU32 gc = on ? pal().white : IM_COL32(180, 182, 186, 255);
        dl->AddCircle(c, r, gc, 16, 1.4f * s);
        dl->AddLine(ImVec2(c.x - r * 0.8f, c.y + r * 0.8f), ImVec2(c.x + r * 0.8f, c.y - r * 0.8f), gc, 1.4f * s);
    }

    void drawBandBadge(ImDrawList* dl, float x0, float y0, float x1, float y1, int b)
    {
        const float s = sc();
        const ImVec2 p0 = panel.P(x0, y0), p1 = panel.P(x1, y1);
        const bool en = values[mqidx::enabled(b)] > 0.5f;
        const ImU32 col = kDigitalBandCol[b];
        dl->AddRectFilled(p0, p1, en ? ((col & 0x00FFFFFF) | 0x55000000) : IM_COL32(40, 40, 44, 255), 8.f * s);
        dl->AddRect(p0, p1, en ? col : IM_COL32(70, 70, 74, 255), 8.f * s, 0, 2.f * s);
        panel.text(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1) - 15.f, 30.f,
                   en ? pal().white : IM_COL32(120, 122, 126, 255), std::to_string(b + 1).c_str(), 0, true);
    }

    void drawDetailPanel(ImDrawList* dl)
    {
        const float s = sc();
        const int b = selectedBand_;
        const ImU32 col = kDigitalBandCol[b];
        const bool edge = mqidx::isEdge(b);
        const bool dyn = values[mqidx::dynEnabled(b)] > 0.5f;

        // panel background + top divider
        dl->AddRectFilled(panel.P(DPX0 - 3, DPY0 - 3), panel.P(DPX1 + 3, DPY1 + 3), IM_COL32(26, 26, 28, 255), 4.f * s);
        dl->AddLine(panel.P(DPX0, DPY0 - 3), panel.P(DPX1, DPY0 - 3), IM_COL32(58, 58, 62, 255), 1.2f * s);

        // section backgrounds (EQ | PAN | DYNAMICS), spread across the full width
        dl->AddRectFilled(panel.P(92, DPY0 + 4), panel.P(392, DPY1 - 4), IM_COL32(34, 34, 37, 255), 4.f * s);
        dl->AddRectFilled(panel.P(398, DPY0 + 4), panel.P(486, DPY1 - 4), IM_COL32(30, 34, 40, 255), 4.f * s);
        dl->AddRectFilled(panel.P(494, DPY0 + 4), panel.P(DPX1 - 4, DPY1 - 4),
                          dyn ? IM_COL32(40, 35, 30, 255) : IM_COL32(30, 30, 33, 255), 4.f * s);
        panel.text(dl, 98,  DPY0 + 8, 8.5f, IM_COL32(120, 122, 126, 255), "EQ", -1, true);
        panel.text(dl, 500, DPY0 + 8, 8.5f, dyn ? IM_COL32(255, 136, 68, 255) : IM_COL32(96, 96, 100, 255), "DYNAMICS", -1, true);

        const float cy = DPY0 + 92.f;   // knob-row centre
        const float R  = 32.f;          // big F6-style knobs

        // band badge (left)
        drawBandBadge(dl, 14, cy - 34, 82, cy + 34, b);

        // ---- EQ knobs ----
        detailKnob(dl, "dpfreq", 128, cy, R, mqidx::freq(b), 20.f, 20000.f, true, col, "FREQ", FMT_FREQ, false);
        detailKnob(dl, "dpq",    206, cy, R, mqidx::q(b),    0.1f, 100.f,   true, col, "Q",    FMT_Q,    false);
        if (edge)
        {
            panel.text(dl, 284, cy - R - 15.f, 10.f, IM_COL32(176, 178, 182, 255), "SLOPE", 0, true);
            detailSelector(dl, "dpslope", 252, cy - 13, 316, cy + 13, (uint32_t)mqidx::slope(b), false);
        }
        else
        {
            const bool inv = values[mqidx::invert(b)] > 0.5f;
            detailKnob(dl, "dpgain", 284, cy, R, mqidx::gain(b), -24.f, 24.f, false, col,
                       inv ? "GAIN (INV)" : "GAIN", FMT_GAIN, false);
        }

        // ---- INV / PH / SOLO / routing column ----
        {
            const float bx0 = 322.f, bx1 = 390.f;
            const float bhw = 0.5f * (bx1 - bx0) - 2.f;
            const bool inv = values[mqidx::invert(b)] > 0.5f;
            const bool ph  = values[mqidx::phaseInvert(b)] > 0.5f;
            panelButton(dl, "dpinv", bx0, cy - 33, bx0 + bhw, cy - 12, "INV",
                        inv ? IM_COL32(150, 90, 40, 255) : IM_COL32(46, 46, 50, 255),
                        [this, b]{ toggleParam(mqidx::invert(b)); });
            phaseButton(dl, "dpph", bx0 + bhw + 4, cy - 33, bx1, cy - 12, ph,
                        [this, b]{ toggleParam(mqidx::phaseInvert(b)); });
            const int soloB = digSoloBand();   // real exclusive solo via the DSP bridge
            panelButton(dl, "dpsolo", bx0, cy - 8, bx1, cy + 13, "SOLO",
                        soloB == b ? IM_COL32(150, 140, 40, 255) : IM_COL32(46, 46, 50, 255),
                        [this, b, soloB]{ digSetSolo(soloB == b ? -1 : b, false); });
            detailSelector(dl, "dproute", bx0, cy + 17, bx1, cy + 34, (uint32_t)mqidx::routing(b), false);
        }

        // ---- PAN (boxed area) ----
        detailKnob(dl, "dppan", 442, cy, R, mqidx::pan(b), -1.f, 1.f, false, col, "PAN", FMT_PAN, false);

        // ---- DYNAMICS ----
        detailKnob(dl, "dpth", 548, cy, R, mqidx::dynThreshold(b), -48.f, 0.f,   false, col, "THRESH",  FMT_DB,    !dyn);
        detailKnob(dl, "dpat", 626, cy, R, mqidx::dynAttack(b),    0.1f, 500.f,  true,  col, "ATTACK",  FMT_MS,    !dyn);
        detailKnob(dl, "dprl", 704, cy, R, mqidx::dynRelease(b),   10.f, 5000.f, true,  col, "RELEASE", FMT_MS,    !dyn);
        detailKnob(dl, "dprg", 782, cy, R, mqidx::dynRange(b),     0.f, 24.f,    false, col, "RANGE",   FMT_DB,    !dyn);
        detailKnob(dl, "dprt", 860, cy, R, mqidx::dynRatio(b),     1.f, 100.f,   true,  col, "RATIO",   FMT_RATIO, !dyn);
        panelButton(dl, "dpdyn", 898, cy - 14, 944, cy + 14, dyn ? "DYN ON" : "DYN",
                    dyn ? IM_COL32(52, 90, 150, 255) : IM_COL32(46, 46, 50, 255),
                    [this, b]{ toggleParam(mqidx::dynEnabled(b)); });
    }

    // Apply a Digital factory preset (pi<0 = Init/defaults). Resets the Digital-
    // relevant params to layout defaults, then applies the preset's sparse
    // (paramIndex,value) overrides — matching the shell's loadProgram semantics.
    void applyDigitalPreset(int pi)
    {
        auto setP = [&](uint32_t id, float v) {
            editParameter(id, true); values[id] = v; setParameterValue(id, v); editParameter(id, false); };
        for (uint32_t i = 0; i <= 81; ++i)    setP(i, kMqParams[i].def);   // 8 EQ bands
        for (uint32_t i = 138; i <= 185; ++i) setP(i, kMqParams[i].def);   // per-band dynamics
        setP(kParamMasterGain,     mqDef(kParamMasterGain));
        setP(kParamHqEnabled,      mqDef(kParamHqEnabled));
        setP(kParamProcessingMode, mqDef(kParamProcessingMode));
        setP(kParamQCoupleMode,    mqDef(kParamQCoupleMode));
        if (pi >= 0 && pi < mqprog::kNumDigitalPrograms)
        {
            const mqprog::Program& prog = mqprog::kDigitalPrograms[pi];
            for (int i = 0; i < prog.count; ++i) setP(prog.pairs[i].idx, prog.pairs[i].val);
        }
        digPreset_ = pi;
    }

    // Apply a Tube (Pultec) factory preset (pi<0 = Init/defaults). Resets the Tube
    // param group to layout defaults, then applies the preset's sparse overrides
    // (which include eq_type=Tube) — matching the shell's loadProgram semantics.
    void applyTubePreset(int pi)
    {
        auto setP = [&](uint32_t id, float v) {
            editParameter(id, true); values[id] = v; setParameterValue(id, v); editParameter(id, false); };
        for (uint32_t i = kParamPultecLfBoostGain; i <= kParamPultecMidHighPeak; ++i)
            setP(i, kMqParams[i].def);                          // reset Tube group
        setP(kParamEqType, (float)kTubeModeIndex);              // keep Tube character
        if (pi >= 0 && pi < mqprog::kNumTubePrograms)
        {
            const mqprog::Program& prog = mqprog::kTubePrograms[pi];
            for (int i = 0; i < prog.count; ++i) setP(prog.pairs[i].idx, prog.pairs[i].val);
        }
        tubePreset_ = pi;
    }

    // Tube preset dropdown — factory Tube presets (mqprog::kTubePrograms). Sits in
    // the header gap between the title and the shared EQ-TYPE character selector.
    void drawTubePresetCombo(ImDrawList* dl)
    {
        const float s = sc();
        const float y0 = 61.f, y1 = 83.f;
        panel.text(dl, 180.f, 52.f, 9.5f, IM_COL32(150, 152, 156, 255), "PRESET", -1, true);
        const char* cur = (tubePreset_ >= 0 && tubePreset_ < mqprog::kNumTubePrograms)
                              ? mqprog::kTubePrograms[tubePreset_].name : "Init";
        ImGui::SetCursorScreenPos(panel.P(180.f, y0));
        ImGui::SetNextItemWidth((348.f - 180.f) * s);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(50, 42, 34, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(28, 22, 17, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(96, 70, 44, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(214, 196, 168, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * s, 4.f * s));
        if (ImGui::BeginCombo("##tbpreset", cur))
        {
            if (ImGui::Selectable("Init", tubePreset_ < 0)) applyTubePreset(-1);
            for (int i = 0; i < mqprog::kNumTubePrograms; ++i)
                if (ImGui::Selectable(mqprog::kTubePrograms[i].name, i == tubePreset_))
                    applyTubePreset(i);
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        (void)y1;
    }

    //========================================================================
    // DIGITAL header — A/B, character dropdown, preset, Save, Auto Gain, mode,
    // graph range, Oversample, Bypass (JUCE MultiQEditor header layout).
    //========================================================================
    void drawDigitalHeader(ImDrawList* dl)
    {
        const float s = sc();
        const ImU32 btnBg = IM_COL32(46, 46, 50, 255);
        const float y0 = 30.f, y1 = 52.f;

        // A/B compare — real two-bank parameter swap (shared toggleAB()).
        {
            const ImVec2 b0 = panel.P(178, y0), b1 = panel.P(202, y1);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton("abbtn", ImVec2(b1.x - b0.x, b1.y - b0.y));
            if (ImGui::IsItemClicked()) toggleAB();
            dl->AddRectFilled(b0, b1, abIsA_ ? kGreenBtn : IM_COL32(60, 60, 64, 255), 4.f * s);
            dl->AddRect(b0, b1, IM_COL32(90, 90, 96, 220), 4.f * s, 0, 1.2f * s);
            panel.text(dl, 190, y0 + 5.f, 11.f, pal().white, abIsA_ ? "A" : "B", 0, true);
        }

        // Character dropdown (Digital/Match/British/Tube) -> kParamEqType.
        headerCombo(dl, "##charsel", 208, y0, 300, y1, kParamEqType, mqp::kEqType, 4);

        // Preset dropdown — Digital factory presets (mqprog::kDigitalPrograms).
        {
            const char* cur = (digPreset_ >= 0 && digPreset_ < mqprog::kNumDigitalPrograms)
                                  ? mqprog::kDigitalPrograms[digPreset_].name : "Init";
            ImGui::SetCursorScreenPos(panel.P(306, y0));
            ImGui::SetNextItemWidth(134.f * s);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, btnBg);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(70, 90, 120, 255));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(210, 212, 216, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * s, 4.f * s));
            if (ImGui::BeginCombo("##dppreset", cur))
            {
                if (ImGui::Selectable("Init", digPreset_ < 0)) applyDigitalPreset(-1);
                for (int i = 0; i < mqprog::kNumDigitalPrograms; ++i)
                    if (ImGui::Selectable(mqprog::kDigitalPrograms[i].name, i == digPreset_))
                        applyDigitalPreset(i);
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
        }

        // Save — VISUAL ONLY (no user-preset store wired in the DPF shell yet).
        headerButton(dl, "dpsave", 446, y0, 492, y1, "Save", IM_COL32(52, 78, 120, 255), pal().white, []{});

        // ---- right group ----
        headerButton(dl, "dpautog", 500, y0, 574, y1, "Auto Gain",
                     values[kAutoGain] > 0.5f ? kGreenBtn : btnBg, pal().white,
                     [&]{ toggleParam(kAutoGain); });
        // Processing mode (Stereo/Left/Right/Mid/Side)
        headerCombo(dl, "##pmode", 580, y0, 648, y1, kParamProcessingMode, mqp::kProcMode, 5);
        // Graph range — JUCE DisplayScaleMode set {±12, ±24, ±30, ±60, Warped}
        {
            static const char* kDR[5] = { "+/-12 dB", "+/-24 dB", "+/-30 dB", "+/-60 dB", "Warped" };
            ImGui::SetCursorScreenPos(panel.P(652, y0));
            ImGui::SetNextItemWidth(96.f * s);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, btnBg);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(70, 90, 120, 255));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(210, 212, 216, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * s, 4.f * s));
            if (ImGui::BeginCombo("##digrange", kDR[digRangeIdx]))
            {
                for (int i = 0; i < 5; ++i) if (ImGui::Selectable(kDR[i], i == digRangeIdx)) digRangeIdx = i;
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
        }
        // Oversample (Off/2x/4x) — cycling button keeps the "Oversample: xx" caption.
        {
            char osb[24]; std::snprintf(osb, sizeof(osb), "Oversample: %s", mqp::kOversampling[choiceIdx(kOversampling)]);
            headerButton(dl, "dpos", 752, y0, 846, y1, osb, btnBg, pal().white, [&]{ cycleChoice(kOversampling); });
        }
        // Bypass
        headerButton(dl, "dpbyp", 852, y0, 916, y1, values[kBypass] > 0.5f ? "BYPASSED" : "BYPASS",
                     values[kBypass] > 0.5f ? IM_COL32(150, 60, 48, 255) : btnBg, pal().white,
                     [&]{ toggleParam(kBypass); });

        // second toolbar row (Q-couple, master, analyzer cluster, limiter)
        drawDigitalToolbar(dl);
    }

    //========================================================================
    // DIGITAL — second toolbar row under the header (JUCE MultiQEditor toolbar
    // density). Left→right: Q-Couple | MASTER | Analyzer(enable/pre-post/mode/
    // resolution/smoothing/decay/freeze) | Limiter(enable/ceiling/GR). Auto-Gain
    // stays on the first header row. Sits at y 92..114 on the chassis, just above
    // the response graph (DGY0 was lowered to 120 to make room).
    //========================================================================
    void drawDigitalToolbar(ImDrawList* dl)
    {
        const float s = sc();
        const float y0 = 92.f, y1 = 114.f;
        const ImU32 btnBg = IM_COL32(46, 46, 50, 255);

        // seat strip + faint separators between the three groups
        dl->AddRectFilled(panel.P(8, 90), panel.P(950, 116), IM_COL32(24, 24, 27, 255), 3.f * s);

        // ---- Q-Couple (9 modes) ----
        panel.text(dl, 14, y0 - 10.f, 8.f, IM_COL32(140, 142, 146, 255), "Q-COUPLE", -1, true);
        headerCombo(dl, "##qcouple", 76, y0, 214, y1, kParamQCoupleMode, mqp::kQCouple, 9);

        // ---- MASTER gain (-24..+24 dB) ----
        headerValueBox(dl, "dpmaster", 220, y0, 300, y1, "MASTER", kParamMasterGain,
                       -24.f, 24.f, false, "%+.1f", " dB", IM_COL32(96, 118, 150, 255));

        dl->AddLine(panel.P(308, 92), panel.P(308, 114), IM_COL32(52, 52, 56, 255), 1.2f * s);
        panel.text(dl, 494, y0 - 10.f, 8.f, IM_COL32(140, 142, 146, 255), "ANALYZER", 0, true);
        panel.text(dl, 883, y0 - 10.f, 8.f, IM_COL32(140, 142, 146, 255), "LIMITER", 0, true);

        // ---- Analyzer cluster ----
        const bool anaOn = values[kParamAnalyzerEnabled] > 0.5f;
        headerButton(dl, "dpana", 314, y0, 366, y1, "ANLZ",
                     anaOn ? kGreenBtn : btnBg, pal().white, [&]{ toggleParam(kParamAnalyzerEnabled); });
        // Pre/Post — JUCE toggle checked == Pre.
        const bool pre = values[kParamAnalyzerPrePost] > 0.5f;
        headerButton(dl, "dpprepost", 370, y0, 424, y1, pre ? "PRE" : "POST",
                     pre ? IM_COL32(52, 78, 120, 255) : btnBg, pal().white,
                     [&]{ toggleParam(kParamAnalyzerPrePost); });
        headerCombo(dl, "##anamode",   428, y0, 494, y1, kParamAnalyzerMode,       mqp::kPeakRms,   2);
        headerCombo(dl, "##anares",    498, y0, 588, y1, kParamAnalyzerResolution, mqp::kAnaRes,    3);
        headerCombo(dl, "##anasmooth", 592, y0, 674, y1, kParamAnalyzerSmoothing,  mqp::kAnaSmooth, 4);
        headerValueBox(dl, "dpdecay", 678, y0, 750, y1, "DEC", kParamAnalyzerDecay,
                       3.f, 60.f, false, "%.0f", "", IM_COL32(150, 130, 90, 255));
        headerButton(dl, "dpfreeze", 754, y0, 810, y1, digFrozen_ ? "FROZEN" : "FREEZE",
                     digFrozen_ ? IM_COL32(40, 110, 150, 255) : btnBg, pal().white,
                     [&]{ toggleFreeze(); });

        dl->AddLine(panel.P(818, 92), panel.P(818, 114), IM_COL32(52, 52, 56, 255), 1.2f * s);

        // ---- Limiter (enable + ceiling + GR readout) ----
        const bool lim = values[kParamLimiterEnabled] > 0.5f;
        headerButton(dl, "dplim", 824, y0, 868, y1, "LIMIT",
                     lim ? IM_COL32(150, 90, 40, 255) : btnBg, pal().white,
                     [&]{ toggleParam(kParamLimiterEnabled); });
        headerValueBox(dl, "dpceil", 872, y0, 920, y1, "CEIL", kParamLimiterCeiling,
                       -1.f, 0.f, false, "%.2f", "", IM_COL32(150, 90, 40, 255));
        drawLimiterGr(dl, 924, y0, 946, y1);
    }

    // Toggle UI-local spectrum freeze; snapshots the current live trace on engage
    // (JUCE FFTAnalyzer::toggleFreeze copies smoothedMagnitudes -> frozenMagnitudes).
    void toggleFreeze()
    {
        digFrozen_ = !digFrozen_;
        if (digFrozen_) digFrozenDb_ = digSpecDb_;
    }

    // Compact toolbar numeric box: caption (left) + value (right); vertical drag to
    // adjust (Shift = fine, Ctrl/Cmd = reset-to-default), wheel steps, double-click
    // types a value (shared inline editor). Used for MASTER / DECAY / CEILING.
    void headerValueBox(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                        const char* caption, uint32_t pid, float minV, float maxV, bool logScale,
                        const char* fmt, const char* suffix, ImU32 accent)
    {
        const float s = sc();
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        const float lmin = std::log10(std::max(1e-4f, minV)), lmax = std::log10(std::max(1e-4f, maxV));
        auto toT = [&](float v) { return logScale ? clamp01((std::log10(std::max(minV, v)) - lmin) / (lmax - lmin))
                                                  : clamp01((v - minV) / (maxV - minV)); };
        auto toV = [&](float t) { return logScale ? std::pow(10.f, lmin + t * (lmax - lmin)) : minV + t * (maxV - minV); };

        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        const bool editing = panel.isEditingValue(id);
        const bool modKey = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        float t = toT(values[pid]);
        auto apply = [&](float tt) { const float v = toV(tt); values[pid] = v; setParameterValue(pid, v); };
        if (!editing)
        {
            if (ImGui::IsItemActivated())
            {
                if (modKey) { setParamNotify(pid, mqDef(pid)); stepModReset_ = true; }
                else { editParameter(pid, true); stepDragT = t; stepModReset_ = false; }
            }
            if (act && !stepModReset_)
            {
                const float sp = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
                stepDragT = clamp01(stepDragT - ImGui::GetIO().MouseDelta.y * sp);
                t = stepDragT; apply(t);
            }
            if (ImGui::IsItemDeactivated()) { if (!stepModReset_) editParameter(pid, false); stepModReset_ = false; }
            if (!modKey && (hov || act) && ImGui::IsMouseDoubleClicked(0)) { panel.openValueEdit(id, values[pid]); editParameter(pid, false); }
            if (hov && !act) { const float wh = ImGui::GetIO().MouseWheel; if (wh != 0.f) { t = clamp01(t + wh * 0.02f); editParameter(pid, true); apply(t); editParameter(pid, false); } }
        }

        dl->AddRectFilled(b0, b1, IM_COL32(30, 30, 34, 255), 3.f * s);
        dl->AddRect(b0, b1, (hov || act) ? IM_COL32(150, 152, 156, 220) : IM_COL32(84, 84, 90, 200), 3.f * s, 0, 1.2f * s);
        // accent tick on the left edge
        dl->AddRectFilled(panel.P(x0, y0), panel.P(x0 + 3.f, y1), accent, 3.f * s);
        panel.text(dl, x0 + 8.f, y0 + 0.5f * (y1 - y0) - 5.f, 8.5f, IM_COL32(150, 152, 156, 255), caption, -1, true);

        float typed;
        const float vcx = 0.5f * (x0 + x1) + 12.f, vcy = 0.5f * (y0 + y1);
        if (panel.valueEdit(id, vcx, vcy, 0.f, typed))
        {
            typed = typed < minV ? minV : (typed > maxV ? maxV : typed);
            editParameter(pid, true); values[pid] = typed; setParameterValue(pid, typed); editParameter(pid, false);
        }
        else
        {
            char vb[24]; std::snprintf(vb, sizeof(vb), fmt, values[pid]);
            char full[32]; std::snprintf(full, sizeof(full), "%s%s", vb, suffix);
            panel.text(dl, x1 - 6.f, y0 + 0.5f * (y1 - y0) - 6.f, 10.f, IM_COL32(228, 224, 216, 255), full, 1, true);
        }
    }

    // Read (weak bridge) + smooth the limiter gain reduction, then draw a tiny
    // vertical GR bar (grows DOWN from the top as GR increases) with a "GR" cap.
    // Surfaces the limiter's live gain reduction beside the OUT meter / master area.
    void drawLimiterGr(ImDrawList* dl, float x0, float y0, float x1, float y1)
    {
        float gr = 0.f;
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQGetLimiterGR != nullptr)
            if (void* inst = getPluginInstancePointer()) gr = multiQGetLimiterGR(inst);
       #endif
        const float dt = std::min(std::max(ImGui::GetIO().DeltaTime, 0.f), 0.1f);
        const float sm = 1.0f - std::exp(-dt / 0.08f);
        digLimGrDb_ += (gr - digLimGrDb_) * sm;
        const float s = sc();
        const ImVec2 b0 = panel.P(x0, y0 + 8.f), b1 = panel.P(x1, y1);
        dl->AddRectFilled(b0, b1, IM_COL32(20, 20, 22, 255), 2.f * s);
        dl->AddRect(b0, b1, IM_COL32(70, 70, 74, 200), 2.f * s, 0, 1.f * s);
        const float t = std::min(digLimGrDb_ / 12.f, 1.f);   // full bar at 12 dB GR
        if (t > 0.001f)
            dl->AddRectFilled(b0, panel.P(x1, (y0 + 8.f) + t * (y1 - (y0 + 8.f))), IM_COL32(210, 140, 60, 255), 2.f * s);
        panel.text(dl, 0.5f * (x0 + x1), y0 - 2.f, 7.5f, IM_COL32(150, 130, 100, 255), "GR", 0, true);
    }

    // Small styled ImGui combo bound to a choice param (character / mode).
    void headerCombo(ImDrawList* /*dl*/, const char* id, float x0, float y0, float x1, float y1,
                     uint32_t pid, const char* const* labels, int n)
    {
        const float s = sc();
        ImGui::SetCursorScreenPos(panel.P(x0, y0));
        ImGui::SetNextItemWidth((x1 - x0) * s);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(46, 46, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(70, 90, 120, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(210, 212, 216, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * s, 4.f * s));
        const int cur = choiceIdx(pid);
        if (ImGui::BeginCombo(id, labels[cur]))
        {
            for (int i = 0; i < n; ++i)
                if (ImGui::Selectable(labels[i], i == cur))
                {
                    editParameter(pid, true); values[pid] = (float)i; setParameterValue(pid, (float)i); editParameter(pid, false);
                }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        (void)y1;
    }

    //========================================================================
    // TUBE — Pultec-style passive program EQ
    //========================================================================
    // Read-only response, ported verbatim from JUCE TubeEQCurveDisplay so the
    // curve matches the DSP's voiced behaviour (analytic approximation; the real
    // core adds inductor Q modulation + tube nonlinearity that no static curve
    // can show). Frequencies resolved from the choice params via the mqp:: LUTs.
    double tubeBiquadMag(double b0, double b1, double b2, double a1, double a2,
                         double cosw, double sinw, double cos2w, double sin2w) const
    {
        double numR = b0 + b1 * cosw + b2 * cos2w;
        double numI = -(b1 * sinw + b2 * sin2w);
        double denR = 1.0 + a1 * cosw + a2 * cos2w;
        double denI = -(a1 * sinw + a2 * sin2w);
        double nn = numR * numR + numI * numI, dd = denR * denR + denI * denI;
        return dd > 1e-20 ? std::sqrt(nn / dd) : 1.0;
    }

    float tubeLfCombinedDb(float freq, double sr) const
    {
        const float boostGain = values[kParamPultecLfBoostGain];
        const float attenGain = values[kParamPultecLfAttenGain];
        if (boostGain < 0.1f && attenGain < 0.1f) return 0.f;
        float frequency = mqp::kLfBoostHz[choiceIdx(kParamPultecLfBoostFreq)];
        const float maxFreq = (float)sr * 0.45f;
        frequency = std::max(10.f, std::min(frequency, maxFreq));
        const float twoPi = 2.f * (float)duskaudio::kMultiQPi;
        const double omega = 2.0 * duskaudio::kMultiQPi * freq / sr;
        const double cosw = std::cos(omega), sinw = std::sin(omega), cos2w = std::cos(2 * omega), sin2w = std::sin(2 * omega);
        double peakMag = 1.0, dipMag = 1.0;
        if (boostGain > 0.01f)
        {
            const float peakGainDB = boostGain * 1.4f + attenGain * boostGain * 0.08f;
            float effQ = 0.55f * (1.0f + attenGain * 0.015f); effQ = std::max(effQ, 0.2f);
            const float A = std::pow(10.f, peakGainDB / 40.f);
            const float w0 = twoPi * frequency / (float)sr, cw = std::cos(w0), sw = std::sin(w0), al = sw / (2.f * effQ);
            const float pb0 = 1 + al * A, pb1 = -2 * cw, pb2 = 1 - al * A, pa0 = 1 + al / A, pa1 = -2 * cw, pa2 = 1 - al / A;
            peakMag = tubeBiquadMag(pb0 / pa0, pb1 / pa0, pb2 / pa0, pa1 / pa0, pa2 / pa0, cosw, sinw, cos2w, sin2w);
        }
        if (attenGain > 0.01f)
        {
            float dipFreq = std::max(10.f, std::min(frequency, maxFreq));
            const float dipGainDB = -(attenGain * 1.75f + boostGain * attenGain * 0.06f);
            const float dipQ = 0.65f + attenGain * 0.03f;
            const float A = std::pow(10.f, dipGainDB / 40.f);
            const float w0 = twoPi * dipFreq / (float)sr, cw = std::cos(w0), sw = std::sin(w0), al = sw / (2.f * dipQ), sqA = std::sqrt(A);
            const float db0 = A * ((A + 1) - (A - 1) * cw + 2 * sqA * al);
            const float db1 = 2 * A * ((A - 1) - (A + 1) * cw);
            const float db2 = A * ((A + 1) - (A - 1) * cw - 2 * sqA * al);
            const float da0 = (A + 1) + (A - 1) * cw + 2 * sqA * al;
            const float da1 = -2 * ((A - 1) + (A + 1) * cw);
            const float da2 = (A + 1) + (A - 1) * cw - 2 * sqA * al;
            dipMag = tubeBiquadMag(db0 / da0, db1 / da0, db2 / da0, da1 / da0, da2 / da0, cosw, sinw, cos2w, sin2w);
        }
        return (float)(20.0 * std::log10(peakMag * dipMag + 1e-10));
    }

    float tubeHfBoostDb(float freq) const
    {
        const float g = values[kParamPultecHfBoostGain];
        if (g < 0.1f) return 0.f;
        const float fc = mqp::kHfBoostHz[choiceIdx(kParamPultecHfBoostFreq)];
        const float gain = g * 1.8f;
        const float q = 2.0f + (0.3f - 2.0f) * values[kParamPultecHfBoostBandwidth]; // jmap bw 0..1 -> Q 2..0.3
        const float bandwidth = 1.0f / q;
        const float logRatio = std::log(freq / fc);
        return gain * std::exp(-0.5f * std::pow(logRatio / (bandwidth * 0.6f), 2.0f));
    }

    float tubeHfAttenDb(float freq) const
    {
        const float g = values[kParamPultecHfAttenGain];
        if (g < 0.1f) return 0.f;
        const float fc = mqp::kHfAttenHz[choiceIdx(kParamPultecHfAttenFreq)];
        const float gain = -g * 1.6f;
        const float logRatio = std::log10(freq / fc);
        const float normalized = 0.5f * (1.0f + std::tanh(logRatio / 0.5f));
        return gain * normalized;
    }

    float tubeMidDb(float freq, double sr) const
    {
        if (values[kParamPultecMidEnabled] < 0.5f) return 0.f;
        const float lowPk = values[kParamPultecMidLowPeak], dip = values[kParamPultecMidDip], hiPk = values[kParamPultecMidHighPeak];
        if (lowPk < 0.01f && dip < 0.01f && hiPk < 0.01f) return 0.f;
        const double omega = 2.0 * duskaudio::kMultiQPi * freq / sr;
        const double cosw = std::cos(omega), sinw = std::sin(omega), cos2w = std::cos(2 * omega), sin2w = std::sin(2 * omega);
        auto peakMag = [&](float fc, float q, float gainDB) -> double {
            const double tubeQ = std::max(0.01, (double)q * 0.85);
            const double fcD = std::max(1.0, std::min((double)fc, sr * 0.4998));
            const double bw = fcD / tubeQ;
            const double kbw = std::tan(duskaudio::kMultiQPi * std::min(bw, sr * 0.4998) / sr);
            const double A = std::pow(10.0, (double)gainDB / 40.0);
            const double cW = std::cos(2.0 * duskaudio::kMultiQPi * fcD / sr);
            const double b0 = (1 + kbw * A) / (1 + kbw / A), b1 = (-2 * cW) / (1 + kbw / A), b2 = (1 - kbw * A) / (1 + kbw / A);
            const double a1 = (-2 * cW) / (1 + kbw / A), a2 = (1 - kbw / A) / (1 + kbw / A);
            return tubeBiquadMag(b0, b1, b2, a1, a2, cosw, sinw, cos2w, sin2w);
        };
        double combined = 1.0;
        if (lowPk > 0.01f) combined *= peakMag(mqp::kMidLowHz[choiceIdx(kParamPultecMidLowFreq)], 1.2f, lowPk * 1.2f);
        if (dip   > 0.01f) combined *= peakMag(mqp::kMidDipHz[choiceIdx(kParamPultecMidDipFreq)], 0.8f, -dip * 1.0f);
        if (hiPk  > 0.01f) combined *= peakMag(mqp::kMidHighHz[choiceIdx(kParamPultecMidHighFreq)], 1.4f, hiPk * 1.2f);
        return (float)(20.0 * std::log10(combined + 1e-10));
    }

    float tubeResponseDb(float freq) const
    {
        const double sr = digitalSampleRate();
        return tubeLfCombinedDb(freq, sr) + tubeHfBoostDb(freq) + tubeHfAttenDb(freq) + tubeMidDb(freq, sr);
    }

    void drawTube(ImDrawList* dl)
    {
        const float s = sc();
        const double sr = digitalSampleRate();
        // ---- response curve area ----
        // dB scale is driven by the shared display_scale param (JUCE ties the Tube
        // graph to the same scale selector); default index 1 == +/-24 dB (was a
        // fixed +/-18). {0:+/-12, 1:+/-24, 2:+/-30, 3:+/-60, 4:Warped->+/-24}.
        static const float kScaleR[5] = { 12.f, 24.f, 30.f, 60.f, 24.f };
        int scIdx = choiceIdx(kParamDisplayScaleMode);
        const float R = kScaleR[(scIdx < 0 || scIdx > 4) ? 1 : scIdx];
        dl->AddRectFilled(panel.P(DGX0 - 3, TGY0 - 3), panel.P(DGX1 + 3, TGY1 + 3), IM_COL32(60, 52, 44, 255), 3.f * s);
        dl->AddRectFilled(panel.P(DGX0, TGY0), panel.P(DGX1, TGY1), IM_COL32(20, 16, 13, 255));
        dl->PushClipRect(panel.P(DGX0, TGY0), panel.P(DGX1, TGY1), true);
        auto ty = [&](float db) { float d = db < -R ? -R : (db > R ? R : db); return TGY0 + (0.5f - 0.5f * d / R) * (TGY1 - TGY0); };

        // vertical freq grid
        for (int i = 0; i < (int)(sizeof(kGridF) / sizeof(kGridF[0])); ++i)
        {
            const float x = DGX0 + flog((float)kGridF[i]) * (DGX1 - DGX0);
            dl->AddLine(panel.P(x, TGY0), panel.P(x, TGY1), IM_COL32(48, 40, 32, 255), 1.f * s);
            panel.text(dl, x, TGY1 - 13, 8.5f, IM_COL32(150, 130, 104, 255), kGridFL[i], 0);
        }
        // horizontal dB grid — step chosen from the range (JUCE drawVintageGrid).
        const float dbStep = R <= 12.f ? 3.f : (R <= 24.f ? 6.f : (R <= 30.f ? 10.f : 20.f));
        for (float db = -R; db <= R + 0.1f; db += dbStep)
        {
            const float y = ty(db);
            const bool zero = std::abs(db) < 0.1f;
            dl->AddLine(panel.P(DGX0, y), panel.P(DGX1, y), zero ? IM_COL32(80, 68, 54, 255) : IM_COL32(44, 37, 30, 255), 1.f * s);
            char b[8]; std::snprintf(b, sizeof(b), "%+d", (int)std::lround(db));
            panel.text(dl, DGX0 + 5, y - 6.f, 10.f, IM_COL32(170, 150, 122, 255), zero ? "0" : b, -1);
        }

        // live FFT analyzer overlay — reuses the committed spectrum bridge +
        // digital smoothing/freeze (pre/post via analyzer_pre_post). Drawn BEHIND
        // the EQ curves, dim. UI-local ANALYZER on/off; FREEZE shares digFrozen_.
        if (tubeAnalyzer_)
            drawDigitalSpectrum(dl, DGX0, TGY0, DGX1, TGY1);

        // ---- per-section colored sub-curves + combined white curve ----
        // Split the tube response into its section contributions (matching JUCE
        // TubeEQCurveDisplay's per-band curves): LF boost/atten in blue, HF
        // boost/atten in light-blue, MID in amber; combined in white on top.
        const int N = 300;
        auto section = [&](ImU32 col, float thick, auto&& fn)
        {
            std::vector<ImVec2> pts; pts.reserve(N);
            for (int i = 0; i < N; ++i)
            {
                const float lx = (float)i / (N - 1);
                const float freq = std::pow(10.f, std::log10(kFMin) + lx * (std::log10(kFMax) - std::log10(kFMin)));
                pts.push_back(panel.P(DGX0 + lx * (DGX1 - DGX0), ty(fn(freq))));
            }
            dl->AddPolyline(pts.data(), (int)pts.size(), col, 0, thick * s);
        };
        const float lfB = values[kParamPultecLfBoostGain], lfA = values[kParamPultecLfAttenGain];
        const float hfB = values[kParamPultecHfBoostGain], hfA = values[kParamPultecHfAttenGain];
        const bool  midOn = values[kParamPultecMidEnabled] > 0.5f;
        const bool  midAny = values[kParamPultecMidLowPeak] > 0.01f || values[kParamPultecMidDip] > 0.01f
                          || values[kParamPultecMidHighPeak] > 0.01f;
        if (lfB > 0.1f || lfA > 0.1f)
            section(IM_COL32(96, 160, 200, 205), 1.6f, [&](float f) { return tubeLfCombinedDb(f, sr); });
        if (hfB > 0.1f || hfA > 0.1f)
            section(IM_COL32(130, 196, 224, 205), 1.6f, [&](float f) { return tubeHfBoostDb(f) + tubeHfAttenDb(f); });
        if (midOn && midAny)
            section(IM_COL32(216, 168, 110, 205), 1.6f, [&](float f) { return tubeMidDb(f, sr); });
        section(IM_COL32(232, 228, 222, 255), 2.4f, [&](float f) { return tubeResponseDb(f); });

        dl->PopClipRect();

        // small READ-ONLY note (curve is DSP-driven; not draggable). Kept small,
        // top-centre so it clears the HUD toggles and the frozen badge.
        panel.text(dl, 0.5f * (DGX0 + DGX1), TGY0 + 4.f, 8.5f, IM_COL32(120, 104, 84, 255), "READ-ONLY", 0, true);

        // analyzer HUD toggles (top-left interior, clear of the dB label column).
        const ImU32 hudDim = IM_COL32(48, 40, 32, 235);
        headerButton(dl, "tbanlz", DGX0 + 34, TGY0 + 4, DGX0 + 96, TGY0 + 20, "ANLZ",
                     tubeAnalyzer_ ? kGreenBtn : hudDim, pal().white, [&]{ tubeAnalyzer_ = !tubeAnalyzer_; });
        headerButton(dl, "tbfrz", DGX0 + 100, TGY0 + 4, DGX0 + 168, TGY0 + 20, digFrozen_ ? "FROZEN" : "FREEZE",
                     digFrozen_ ? IM_COL32(40, 110, 150, 255) : hudDim, pal().white, [&]{ toggleFreeze(); });

        // ---- control chassis ----
        const float PY0 = 316.f, PY1 = 662.f;
        dl->AddRectFilled(panel.P(DGX0, PY0), panel.P(DGX1, PY1), IM_COL32(46, 38, 31, 255), 5.f * s);
        dl->AddRect(panel.P(DGX0, PY0), panel.P(DGX1, PY1), IM_COL32(80, 66, 52, 255), 5.f * s, 0, 1.4f * s);

        // column dividers: LF | MID | HF | I/O
        const float CX[5] = { 44.f, 262.f, 566.f, 806.f, 916.f };
        const char* CN[4] = { "LOW FREQUENCY", "MID", "HIGH FREQUENCY", "GAIN" };
        for (int i = 0; i < 4; ++i)
        {
            const float cx = 0.5f * (CX[i] + CX[i + 1]);
            if (i > 0) dl->AddLine(panel.P(CX[i], PY0 + 8), panel.P(CX[i], PY1 - 8), IM_COL32(30, 24, 19, 255), 1.4f * s);
            panel.text(dl, cx, PY0 + 10, 11.f, C_TUBE_LBL, CN[i], 0, true);
        }

        // --- LF section ---
        {
            const float cx = 0.5f * (CX[0] + CX[1]);
            stepSelector(dl, "tlff", cx, PY0 + 44, 92.f, 20.f, "BOOST/ATTEN FREQ", kParamPultecLfBoostFreq);
            tubeKnob(dl, "tlfb", kParamPultecLfBoostGain, 0.f, 10.f, cx - 46, PY0 + 130, 26.f, "BOOST", "%.1f", "");
            tubeKnob(dl, "tlfa", kParamPultecLfAttenGain, 0.f, 10.f, cx + 46, PY0 + 130, 26.f, "ATTEN", "%.1f", "");
        }
        // --- MID section ---
        {
            const bool midOn = values[kParamPultecMidEnabled] > 0.5f;
            const float cx = 0.5f * (CX[1] + CX[2]);
            panelButton(dl, "tmiden", cx - 34, PY0 + 30, cx + 34, PY0 + 50, midOn ? "MID: ON" : "MID: OFF",
                        midOn ? kGreenBtn : IM_COL32(50, 44, 38, 255), [this] { toggleParam(kParamPultecMidEnabled); });
            const float col1 = CX[1] + 52, col2 = 0.5f * (CX[1] + CX[2]), col3 = CX[2] - 52;
            stepSelector(dl, "tmlf", col1, PY0 + 78, 74.f, 18.f, "LOW PK FREQ", kParamPultecMidLowFreq, midOn);
            stepSelector(dl, "tmdf", col2, PY0 + 78, 74.f, 18.f, "DIP FREQ", kParamPultecMidDipFreq, midOn);
            stepSelector(dl, "tmhf", col3, PY0 + 78, 74.f, 18.f, "HIGH PK FREQ", kParamPultecMidHighFreq, midOn);
            tubeKnob(dl, "tmlp", kParamPultecMidLowPeak, 0.f, 10.f, col1, PY0 + 158, 24.f, "PEAK", "%.1f", "");
            tubeKnob(dl, "tmd",  kParamPultecMidDip,     0.f, 10.f, col2, PY0 + 158, 24.f, "DIP", "%.1f", "");
            tubeKnob(dl, "tmhp", kParamPultecMidHighPeak,0.f, 10.f, col3, PY0 + 158, 24.f, "PEAK", "%.1f", "");
        }
        // --- HF section ---
        {
            const float cxa = CX[2] + 60, cxb = CX[3] - 60;
            stepSelector(dl, "thbf", cxa, PY0 + 44, 82.f, 20.f, "BOOST FREQ", kParamPultecHfBoostFreq);
            stepSelector(dl, "thaf", cxb, PY0 + 44, 82.f, 20.f, "ATTEN FREQ", kParamPultecHfAttenFreq);
            tubeKnob(dl, "thb",  kParamPultecHfBoostGain, 0.f, 10.f, cxa, PY0 + 130, 26.f, "BOOST", "%.1f", "");
            tubeKnob(dl, "thbw", kParamPultecHfBoostBandwidth, 0.f, 1.f, 0.5f * (cxa + cxb), PY0 + 220, 22.f, "BANDWIDTH", "%.2f", "");
            tubeKnob(dl, "tha",  kParamPultecHfAttenGain, 0.f, 10.f, cxb, PY0 + 130, 26.f, "ATTEN", "%.1f", "");
        }
        // --- I/O + drive ---
        {
            const float cx = 0.5f * (CX[3] + CX[4]);
            tubeKnob(dl, "tin",  kParamPultecInputGain,  -12.f, 12.f, cx, PY0 + 58,  22.f, "INPUT", "%.1f", " dB");
            tubeKnob(dl, "tdrv", kParamPultecTubeDrive,   0.f,  1.f,  cx, PY0 + 150, 22.f, "DRIVE", "%.0f", "%", 100.f);
            tubeKnob(dl, "tout", kParamPultecOutputGain, -12.f, 12.f, cx, PY0 + 242, 22.f, "OUTPUT", "%.1f", " dB");
        }
    }

    void tubeKnob(ImDrawList* dl, const char* id, uint32_t pid, float minV, float maxV,
                  float cx, float cy, float r, const char* caption, const char* fmt, const char* suffix,
                  float dispMul = 1.0f)
    {
        panel.text(dl, cx, cy - r - 15.f, 10.f, C_TUBE_LBL, caption, 0, true);
        panel.knob(id, pid, minV, maxV, cx, cy, r, values[pid], mqDef(pid),
                   false, true, fmt, suffix, C_TUBE_FACE, false, true, nullptr, false, dispMul, 0.f);
    }

    template <class Fn>
    void headerButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                      const char* label, ImU32 bg, ImU32 fg, Fn onClick)
    {
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) onClick();
        dl->AddRectFilled(b0, b1, bg, 4.0f * sc());
        dl->AddRect(b0, b1, hov ? IM_COL32(200, 200, 205, 200) : IM_COL32(90, 90, 96, 200), 4.0f * sc(), 0, 1.2f * sc());
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 10.5f, fg, label, 0, true);
    }

    void cycleParam(uint32_t id, int n)
    {
        float nv = values[id] + 1.0f; if (nv > n - 1 + 0.5f) nv = 0.0f;
        editParameter(id, true); values[id] = nv; setParameterValue(id, nv); editParameter(id, false);
    }
    void toggleParam(uint32_t id)
    {
        float nv = values[id] > 0.5f ? 0.0f : 1.0f;
        editParameter(id, true); values[id] = nv; setParameterValue(id, nv); editParameter(id, false);
    }

    // Apply a British factory preset (mqprog::kBritishPrograms). Mirrors the shell's
    // loadProgram + applyDigitalPreset: reset the British param group to layout
    // defaults, then apply the preset's sparse (paramIndex,value) overrides (which
    // include eq_type=British, so the character stays selected).
    void applyPreset(int idx)
    {
        if (idx < 0 || idx >= mqprog::kNumBritishPrograms) return;
        auto setP = [&](uint32_t id, float v) {
            editParameter(id, true); values[id] = v; setParameterValue(id, v); editParameter(id, false); };
        for (uint32_t i = kParamBritishHpfFreq; i <= kParamBritishOutputGain; ++i)
            setP(i, kMqParams[i].def);                              // reset British group
        setP(kParamEqType, (float)kBritishModeIndex);              // keep British character
        const mqprog::Program& prog = mqprog::kBritishPrograms[idx];
        for (int i = 0; i < prog.count; ++i) setP(prog.pairs[i].idx, prog.pairs[i].val);
        currentPreset = idx;
    }

    //========================================================================
    // response graph + spectrum
    //========================================================================
    float responseDb(float freq) const
    {
        using duskaudio::Biquad; using duskaudio::BiquadCoeffs;
        using duskaudio::FourKEQDSP;
        const double base = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        const double fs = base * (double) FourKEQDSP::chooseFactor(base, (int)(values[kOversampling] + 0.5f));
        const double w = 2.0 * 3.14159265358979323846 * (double)freq / fs;
        const bool black = values[kEqType] > 0.5f;
        double filtMag = 1.0;
        if (values[kHpfEnabled] > 0.5f)
        {
            Biquad h1; h1.setCoeffs(Biquad::firstOrderHighPass(fs, values[kHpfFreq]));
            Biquad h2; h2.setCoeffs(Biquad::highPass(fs, values[kHpfFreq], 1.0f));
            filtMag *= h1.magnitude(w) * h2.magnitude(w);
        }
        if (values[kLpfEnabled] > 0.5f)
        {
            const float f = (float)std::max(1.0, std::min((double)values[kLpfFreq], fs * 0.4998));
            Biquad lp; lp.setCoeffs(Biquad::lowPass(fs, f, black ? 0.8f : 0.707f));
            filtMag *= lp.magnitude(w);
        }
        auto blockResp = [&](const BiquadCoeffs& c) {
            Biquad b; b.setCoeffs(c); return b.response(w);
        };
        const float bellQ = black ? 0.9f : 0.6f;
        std::complex<double> Heq(1.0, 0.0);
        Heq += (double)FourKEQDSP::bandK(values[kLfGain]) * blockResp(values[kLfBell] > 0.5f
            ? Biquad::bandPassConstantPeak(fs, values[kLfFreq], bellQ)
            : (black ? Biquad::lowPass(fs, values[kLfFreq], 0.9f)
                     : Biquad::firstOrderLowPass(fs, values[kLfFreq])));
        Heq += (double)FourKEQDSP::bandK(values[kLmGain]) * blockResp(
            Biquad::bandPassConstantPeak(fs, values[kLmFreq],
                FourKEQDSP::voicedMidQ(values[kLmGain], values[kLmQ], black)));
        { float hmFreq = values[kHmFreq]; if (!black && hmFreq > 7000.f) hmFreq = 7000.f;
          Heq += (double)FourKEQDSP::bandK(values[kHmGain]) * blockResp(
              Biquad::bandPassConstantPeak(fs, hmFreq,
                  FourKEQDSP::voicedMidQ(values[kHmGain], values[kHmQ], black))); }
        Heq += (double)FourKEQDSP::bandK(values[kHfGain]) * blockResp(values[kHfBell] > 0.5f
            ? Biquad::bandPassConstantPeak(fs, values[kHfFreq], bellQ)
            : (black ? Biquad::highPass(fs, values[kHfFreq], 0.9f)
                     : Biquad::firstOrderHighPass(fs, values[kHfFreq])));

        const double magLin = std::abs(Heq) * filtMag;
        return 20.0f * std::log10((float)std::max(magLin, 1e-6));
    }

    static constexpr const char* kRangeLabels[5] = { "+/-6 dB", "+/-12 dB", "+/-18 dB", "+/-30 dB", "Warped" };
    float graphRangeDb() const { static const float R[4] = { 6.f, 12.f, 18.f, 30.f }; return graphRangeIdx < 4 ? R[graphRangeIdx] : 18.f; }
    float dbToNy(float db) const
    {
        if (graphRangeIdx == 4)
        {
            const float m = 24.0f;
            float d = db < -m ? -m : (db > m ? m : db);
            const float tt = (d >= 0.f ? 1.f : -1.f) * std::sqrt(std::abs(d) / m);
            return 0.5f - 0.5f * tt;
        }
        const float r = graphRangeDb();
        float d = db < -r ? -r : (db > r ? r : db);
        return 0.5f - 0.5f * (d / r);
    }

    void drawPresetPopup(ImDrawList* dl, float winW, float winH)
    {
        const int n = mqprog::kNumBritishPrograms;
        const float top = kPresetY1 + 2.f;
        const float bot = top + n * kPresetRowH + 6.f;
        const ImVec2 p0 = panel.P(kPresetX0, top), p1 = panel.P(kPresetX1, bot);
        dl->AddRectFilled(p0, p1, IM_COL32(24, 24, 26, 255), 3.f * sc());
        dl->AddRect(p0, p1, IM_COL32(96, 100, 108, 235), 3.f * sc(), 0, 1.f * sc());

        const ImVec2 m = ImGui::GetMousePos();
        for (int i = 0; i < n; ++i)
        {
            const float ry0 = top + 3.f + i * kPresetRowH;
            const ImVec2 r0 = panel.P(kPresetX0 + 2.f, ry0), r1 = panel.P(kPresetX1 - 2.f, ry0 + kPresetRowH);
            const bool hov = m.x >= r0.x && m.x <= r1.x && m.y >= r0.y && m.y <= r1.y;
            if (hov)              dl->AddRectFilled(r0, r1, IM_COL32(70, 90, 120, 255), 2.f * sc());
            else if (i == currentPreset) dl->AddRectFilled(r0, r1, IM_COL32(48, 52, 58, 255), 2.f * sc());
            panel.text(dl, kPresetX0 + 11.f, ry0 + 0.5f * kPresetRowH - 6.f, 12.f, IM_COL32(224, 224, 220, 255), mqprog::kBritishPrograms[i].name, -1);
            if (hov && ImGui::IsMouseClicked(0)) { applyPreset(i); presetOpen = false; }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { presetOpen = false; return; }
        if (ImGui::IsMouseClicked(0))
        {
            const ImVec2 mm = ImGui::GetMousePos();
            const ImVec2 b0 = panel.P(kPresetX0, kPresetY0), b1 = panel.P(kPresetX1, kPresetY1);
            const bool inPopup = mm.x >= p0.x && mm.x <= p1.x && mm.y >= p0.y && mm.y <= p1.y;
            const bool inBox   = mm.x >= b0.x && mm.x <= b1.x && mm.y >= b0.y && mm.y <= b1.y;
            if (!inPopup && !inBox) presetOpen = false;
            (void)winW; (void)winH;
        }
    }

    void drawCredits(ImDrawList* dl, float winW, float winH)
    {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(0, 0, 0, 205));

        const float s = sc();
        const float LN = 21.f * s, LH = 26.f * s, GAP = 12.f * s;
        const float titleH = 40.f * s, subH = 30.f * s, footH = 34.f * s, padY = 30.f * s;
        const auto& tiers = duskdpf::patreonTiers();
        float contentH = titleH + subH + footH;
        for (const auto& t : tiers)
            if (!t.names.empty()) contentH += LH + (float)t.names.size() * LN + GAP;

        const float cardW = 480.f * s;
        float cardH = contentH + 2.f * padY;
        if (cardH > winH * 0.95f) cardH = winH * 0.95f;
        const ImVec2 cc(winW * 0.5f, winH * 0.5f);
        const ImVec2 c0(cc.x - cardW * 0.5f, cc.y - cardH * 0.5f), c1(cc.x + cardW * 0.5f, cc.y + cardH * 0.5f);
        dl->AddRectFilled(c0, c1, IM_COL32(26, 27, 30, 255), 8.f * s);
        dl->AddRect(c0, c1, IM_COL32(90, 92, 98, 255), 8.f * s, 0, 1.5f * s);

        auto ctext = [&](float yPx, float sz, ImU32 col, const char* txt, bool bold) {
            const float fsz = sz * s;
            ImFont* f = panel.pickFont(fsz);
            const ImVec2 ts = f->CalcTextSizeA(fsz, FLT_MAX, 0.f, txt);
            const float px = std::floor(cc.x - ts.x * 0.5f + 0.5f), py = std::floor(yPx + 0.5f);
            dl->AddText(f, fsz, ImVec2(px, py), col, txt);
            (void)bold;
        };

        float y = c0.y + padY;
        ctext(y, 24.f, IM_COL32(238, 236, 228, 255), "SUPPORTERS", true); y += titleH;
        ctext(y, 13.f, IM_COL32(160, 162, 166, 255), "Thank you to our Patreon supporters", false); y += subH;

        for (const auto& tier : tiers)
        {
            if (tier.names.empty()) continue;
            ctext(y, 15.f, IM_COL32(150, 172, 214, 255), tier.title, true); y += LH;
            for (const char* nm : tier.names) { ctext(y, 15.f, IM_COL32(220, 220, 216, 255), nm, false); y += LN; }
            y += GAP;
        }
        ctext(c1.y - 24.f * s, 11.f, IM_COL32(140, 142, 148, 255), "click anywhere to close", false);

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showCredits = false;
        else if (!creditsArmed) { if (!ImGui::IsMouseDown(0)) creditsArmed = true; }
        else if (ImGui::IsMouseClicked(0)) showCredits = false;
    }

    void drawGraph(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(GX0 - 3, GY0 - 3), panel.P(GX1 + 3, GY1 + 3), IM_COL32(60, 60, 63, 255), 3.0f * sc());
        dl->AddRectFilled(panel.P(GX0, GY0), panel.P(GX1, GY1), IM_COL32(14, 16, 18, 255));
        dl->PushClipRect(panel.P(GX0, GY0), panel.P(GX1, GY1), true);

        for (int i = 0; i < (int)(sizeof(kGridF) / sizeof(kGridF[0])); ++i)
        {
            const float lx = flog(kGridF[i]);
            const float x = GX0 + lx * (GX1 - GX0);
            dl->AddLine(panel.P(x, GY0), panel.P(x, GY1), IM_COL32(40, 43, 47, 255), 1.0f * sc());
            if (kGridF[i] == 100 || kGridF[i] == 1000 || kGridF[i] == 10000)
                panel.text(dl, x, GY1 - 13, 8.5f, IM_COL32(120, 124, 130, 255), kGridFL[i], 0);
        }
        {
            const float R = graphRangeDb();
            const float ticks[5] = { -R, -0.5f * R, 0.f, 0.5f * R, R };
            for (int i = 0; i < 5; ++i)
            {
                const float db = ticks[i];
                const float y = GY0 + dbToNy(db) * (GY1 - GY0);
                dl->AddLine(panel.P(GX0, y), panel.P(GX1, y),
                            db == 0.f ? IM_COL32(64, 68, 74, 255) : IM_COL32(34, 36, 40, 255), 1.0f * sc());
                char b[8]; std::snprintf(b, sizeof(b), "%+d", (int)db);
                float ly = y - 6.f;
                ly = ly < GY0 + 1.f ? GY0 + 1.f : (ly > GY1 - 13.f ? GY1 - 13.f : ly);
                panel.text(dl, GX0 + 5, ly, 11.f, IM_COL32(150, 154, 160, 255), db == 0.f ? "0" : b, -1);
            }
        }

        if (showFft)
            drawSpectrum(dl, GX0, GY0, GX1, GY1);
        const int N = 240;
        std::vector<ImVec2> pts; pts.reserve(N);
        for (int i = 0; i < N; ++i)
        {
            const float lx = (float)i / (N - 1);
            const float freq = std::pow(10.0f, std::log10(kFMin) + lx * (std::log10(kFMax) - std::log10(kFMin)));
            float ny = dbToNy(responseDb(freq));
            ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny);
            pts.push_back(panel.P(GX0 + lx * (GX1 - GX0), GY0 + ny * (GY1 - GY0)));
        }
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(236, 236, 236, 255), 0, 2.0f * sc());

        dl->PopClipRect();

        // Vertical-scale selector, top-right of the graph.
        ImGui::SetCursorScreenPos(panel.P(GX1 - 78, GY0 + 4));
        ImGui::SetNextItemWidth(74.0f * sc());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 32, 36, 210));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(70, 90, 120, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(206, 208, 212, 255));
        if (ImGui::BeginCombo("##graphrange", kRangeLabels[graphRangeIdx], ImGuiComboFlags_NoArrowButton))
        {
            for (int i = 0; i < 5; ++i)
                if (ImGui::Selectable(kRangeLabels[i], i == graphRangeIdx))
                    graphRangeIdx = i;
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(4);
    }

    // Live post-processing spectrum, drawn as a dim filled polyline BEHIND the
    // response curve. Snapshots the DSP's lock-free analyzer ring (SpectrumRing:
    // ACQUIRE write-index, copy N newest slots, torn frames self-drop), windows +
    // FFTs on the UI thread (shared duskdpf::RealFFT), smooths in dB across frames,
    // and maps log-freq X to the plot rect. Mirrors FourKEQUI::drawSpectrum.
    void drawSpectrum(ImDrawList* dl, float x0, float y0, float x1, float y1)
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        const duskaudio::SpectrumRing* ring = nullptr;
        if (multiQGetOutputSpectrum != nullptr)
            if (void* inst = getPluginInstancePointer())
                ring = multiQGetOutputSpectrum(inst);
        if (ring == nullptr) return;

        float buf[kFftSize]; ring->snapshot(buf, kFftSize);
        float mag[kFftSize / 2 + 1]; fft.magnitude(buf, mag);
        // Analyzer ballistics: FAST attack (rise almost instantly to a new peak),
        // SLOW release (fall gently). This is what kills the per-bin jitter that a
        // symmetric time-constant produces on broadband material. Mirrors the JUCE
        // FFTAnalyzer asymmetric temporal smoothing (attack coeff << release coeff).
        const float dt = std::min(std::max(ImGui::GetIO().DeltaTime, 0.f), 0.1f);
        const float atkCoeff = 1.0f - std::exp(-dt * 45.0f);   // ~22 ms attack
        const float relCoeff = 1.0f - std::exp(-dt / 0.40f);   // ~400 ms release
        // Rings fill at the DSP base rate; map bins with the host rate so the
        // spectrum lines up with the log-frequency response curve at any rate.
        const double sr = getSampleRate();
        const float binHz = (float)((sr > 1.0 ? sr : 48000.0) / kFftSize);
        const int half = kFftSize / 2;
        constexpr float kSpecTop = -6.0f, kSpecBot = -84.0f; // dBFS window shown
        // SPATIAL smoothing: triangular-weighted average across +/-2 neighbour bins
        // (weight = 1 - |j|/(W+1)), the same kernel JUCE FFTAnalyzer uses. This is
        // what tames broadband per-bin variance so the trace doesn't crawl.
        constexpr int kW = 2;
        float raw[kFftSize / 2 + 1];
        for (int k = 0; k <= half; ++k) raw[k] = 20.0f * std::log10(mag[k] > 1e-7f ? mag[k] : 1e-7f);
        std::vector<ImVec2> curve; curve.reserve((size_t)half);
        for (int k = 1; k <= half; ++k)
        {
            const float freq = (float)k * binHz;
            float sum = 0.0f, wsum = 0.0f;
            for (int j = -kW; j <= kW; ++j)
            {
                const int idx = k + j;
                if (idx >= 0 && idx <= half)
                {
                    const float w = 1.0f - std::abs((float)j) / (float)(kW + 1);
                    sum += raw[idx] * w; wsum += w;
                }
            }
            const float db = wsum > 0.0f ? sum / wsum : raw[k];
            float& sdb = specDb[(size_t)k];
            sdb += (db - sdb) * (db > sdb ? atkCoeff : relCoeff);
            if (freq < kFMin || freq > kFMax) continue;
            float ny = (kSpecTop - specDb[(size_t)k]) / (kSpecTop - kSpecBot);
            ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny);
            curve.push_back(panel.P(x0 + flog(freq) * (x1 - x0), y0 + ny * (y1 - y0)));
        }
        if (curve.size() >= 2)
        {
            const float baseY = panel.P(x0, y1).y;
            for (size_t i = 0; i + 1 < curve.size(); ++i)
                dl->AddQuadFilled(curve[i], curve[i + 1],
                                  ImVec2(curve[i + 1].x, baseY), ImVec2(curve[i].x, baseY),
                                  IM_COL32(70, 110, 140, 40));
            dl->AddPolyline(curve.data(), (int)curve.size(), IM_COL32(96, 150, 190, 115), 0, 1.1f * sc());
        }
       #else
        (void)dl; (void)x0; (void)y0; (void)x1; (void)y1;
       #endif
    }

    // Digital analyzer FFT size from the resolution param. High (8192) is CAPPED at
    // 4096 because the core SpectrumRing holds 4096 samples (shared class; not worth
    // enlarging just for this). Low=2048, Medium/High=4096.
    int digResolutionFftSize() const
    {
        switch (choiceIdx(kParamAnalyzerResolution))
        {
            case 0:  return 2048;   // Low
            default: return 4096;   // Medium / High (High capped at ring size)
        }
    }

    // Live analyzer for the Digital graph. Selects the PRE-EQ or POST ring by the
    // pre/post param, FFTs on the UI thread at the resolution FFT size, applies the
    // param-driven spatial + temporal smoothing and decay ballistic (Peak vs RMS),
    // maintains a slow-decay peak-hold line, and overlays a frozen snapshot + badge.
    // Separate from British's drawSpectrum so that skin stays byte-for-byte unchanged.
    void drawDigitalSpectrum(ImDrawList* dl, float x0, float y0, float x1, float y1)
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        const bool pre = values[kParamAnalyzerPrePost] > 0.5f;
        const duskaudio::SpectrumRing* ring = nullptr;
        if (void* inst = getPluginInstancePointer())
        {
            if (pre) { if (multiQGetInputSpectrum  != nullptr) ring = multiQGetInputSpectrum(inst); }
            else     { if (multiQGetOutputSpectrum != nullptr) ring = multiQGetOutputSpectrum(inst); }
        }
        if (ring == nullptr) return;

        // (re)size the analyzer FFT + buffers when the resolution changes.
        const int fftN = digResolutionFftSize();
        if (fftN != digFftSize_)
        {
            digFftSize_ = fftN;
            digFft_.prepare(fftN);
            digSpecDb_.assign((size_t)(fftN / 2 + 1), -120.0f);
            digPeakDb_.assign((size_t)(fftN / 2 + 1), -120.0f);
            digFrozenDb_.assign((size_t)(fftN / 2 + 1), -120.0f);
        }

        // fixed stack buffers (max 4096-pt FFT) — no per-frame heap churn.
        float buf[4096]; float mag[2049];
        ring->snapshot(buf, fftN);
        digFft_.magnitude(buf, mag);

        const double sr = getSampleRate();
        const float binHz = (float)((sr > 1.0 ? sr : 48000.0) / fftN);
        const int half = fftN / 2;
        constexpr float kSpecTop = -6.0f, kSpecBot = -84.0f;

        // smoothing param -> temporal coeff + spatial ±width (FFTAnalyzer tables).
        const int smoothIdx = choiceIdx(kParamAnalyzerSmoothing);
        static const float kTemporal[4] = { 0.0f, 0.7f, 0.85f, 0.93f };
        static const int   kSpatial[4]  = { 0, 1, 2, 4 };
        const float cT = kTemporal[smoothIdx < 0 ? 0 : (smoothIdx > 3 ? 3 : smoothIdx)];
        const int   kW = kSpatial[smoothIdx < 0 ? 0 : (smoothIdx > 3 ? 3 : smoothIdx)];
        const bool  rms   = choiceIdx(kParamAnalyzerMode) == 1;
        const float decay = values[kParamAnalyzerDecay];                       // dB/s
        const float dt    = std::min(std::max(ImGui::GetIO().DeltaTime, 0.f), 0.1f);
        const float dDb   = decay * dt;                                        // max fall this frame

        float raw[2049];
        for (int k = 0; k <= half; ++k) raw[k] = 20.0f * std::log10(mag[k] > 1e-7f ? mag[k] : 1e-7f);

        std::vector<ImVec2> curve, peak, frozen;
        curve.reserve((size_t)half);
        for (int k = 1; k <= half; ++k)
        {
            // spatial triangular smoothing over ±kW bins (weight = 1 - |j|/(kW+1)).
            float target;
            if (kW > 0)
            {
                float sum = 0.f, wsum = 0.f;
                for (int j = -kW; j <= kW; ++j)
                {
                    const int idx = k + j;
                    if (idx >= 0 && idx <= half)
                    {
                        const float w = 1.0f - std::abs((float)j) / (float)(kW + 1);
                        sum += raw[(size_t)idx] * w; wsum += w;
                    }
                }
                target = wsum > 0.f ? sum / wsum : raw[(size_t)k];
            }
            else target = raw[(size_t)k];

            // temporal ballistic. Peak: fast attack, release limited to `decay` dB/s.
            // RMS: symmetric exponential average (slower, steadier) — matches JUCE.
            float& cur = digSpecDb_[(size_t)k];
            const float prev = cur;
            if (rms)
                cur = 0.9f * cur + 0.1f * target;
            else if (target > cur)
                cur += (target - cur) * (1.0f - cT * 0.5f);
            else
            {
                const float smoothed = cur + (target - cur) * (1.0f - cT);
                cur = std::max(smoothed, prev - dDb);   // don't fall faster than `decay`
            }

            // peak-hold: instant rise, slow (decay dB/s) fall.
            float& ph = digPeakDb_[(size_t)k];
            if (cur > ph) ph = cur; else ph = std::max(cur, ph - dDb);

            const float freq = (float)k * binHz;
            if (freq < kFMin || freq > kFMax) continue;
            const float px = x0 + flog(freq) * (x1 - x0);
            auto toY = [&](float db) { float ny = (kSpecTop - db) / (kSpecTop - kSpecBot); ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny); return y0 + ny * (y1 - y0); };
            curve.push_back(panel.P(px, toY(cur)));
            peak.push_back(panel.P(px, toY(ph)));
            if (digFrozen_) frozen.push_back(panel.P(px, toY(digFrozenDb_[(size_t)k])));
        }

        // main trace (filled) — pre in a warmer tint, post in the familiar blue.
        if (curve.size() >= 2)
        {
            const ImU32 fill = pre ? IM_COL32(150, 110, 70, 40) : IM_COL32(70, 110, 140, 40);
            const ImU32 line = pre ? IM_COL32(190, 150, 96, 130) : IM_COL32(96, 150, 190, 130);
            const float baseY = panel.P(x0, y1).y;
            for (size_t i = 0; i + 1 < curve.size(); ++i)
                dl->AddQuadFilled(curve[i], curve[i + 1], ImVec2(curve[i + 1].x, baseY), ImVec2(curve[i].x, baseY), fill);
            dl->AddPolyline(curve.data(), (int)curve.size(), line, 0, 1.1f * sc());
        }
        // peak-hold line (thin yellow).
        if (peak.size() >= 2)
            dl->AddPolyline(peak.data(), (int)peak.size(), IM_COL32(230, 220, 110, 150), 0, 1.0f * sc());
        // frozen overlay (cyan) + FROZEN badge.
        if (digFrozen_ && frozen.size() >= 2)
        {
            dl->AddPolyline(frozen.data(), (int)frozen.size(), IM_COL32(102, 204, 255, 180), 0, 1.5f * sc());
            const ImVec2 p0 = panel.P(x1 - 66.f, y0 + 6.f), p1 = panel.P(x1 - 6.f, y0 + 22.f);
            dl->AddRectFilled(p0, p1, IM_COL32(20, 60, 90, 220), 3.f * sc());
            dl->AddRect(p0, p1, IM_COL32(102, 204, 255, 220), 3.f * sc(), 0, 1.f * sc());
            panel.text(dl, x1 - 36.f, y0 + 8.f, 9.f, IM_COL32(180, 230, 255, 255), "FROZEN", 0, true);
        }
       #else
        (void)dl; (void)x0; (void)y0; (void)x1; (void)y1;
       #endif
    }

    static float flog(float f) { return (std::log10(f) - std::log10(kFMin)) / (std::log10(kFMax) - std::log10(kFMin)); }

    //========================================================================
    // channel-strip columns
    //========================================================================
    void drawColumns(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(COL[0], cY(220)), panel.P(COL[6], cY(662)), kPanel, 4.0f * sc());
        const char* names[6] = { "FILTERS", "LF", "LMF", "HMF", "HF", "MASTER" };
        for (int i = 0; i < 6; ++i)
        {
            const float cx = 0.5f * (COL[i] + COL[i + 1]);
            if (i > 0) dl->AddLine(panel.P(COL[i], cY(224)), panel.P(COL[i], cY(658)), IM_COL32(20, 20, 22, 255), 1.4f * sc());
            panel.text(dl, cx, cY(232), 12, IM_COL32(210, 210, 214, 255), names[i], 0, true);
        }

        // FILTERS — SSL-style stepped HPF & LPF, then input trim.
        static const char* const HPFL[7] = { "OUT", "20", "70", "120", "200", "300", "350" };
        static const float        HPFF[7] = { 20.f, 20.f, 70.f, 120.f, 200.f, 300.f, 350.f };
        static const char* const LPFL[7] = { "OUT", "12", "8", "5", "4", "3.5", "3" };
        static const float        LPFF[7] = { 12000.f, 12000.f, 8000.f, 5000.f, 4000.f, 3500.f, 3000.f };
        const float fcx = 0.5f * (COL[0] + COL[1]);
        steppedFilterKnob(dl, "hpfknob", fcx, cY(314), 28.f, kHpfEnabled, kHpfFreq, HPFL, HPFF, false, "Hz");
        steppedFilterKnob(dl, "lpfknob", fcx, cY(452), 28.f, kLpfEnabled, kLpfFreq, LPFL, LPFF, true,  "kHz");
        colMetalKnob(dl, "input", kInputGain, -12.f, 12.f, fcx, cY(568), 26, "INPUT", MK_GAIN_V, MK_GAIN_L, 5, "%.1f", " dB");

        // Shared GAIN and Q detent tables.
        static const float GT[11] = { 0.f, .1f, .2f, .3f, .4f, .5f, .6f, .7f, .8f, .9f, 1.f };
        static const float GV[11] = { -15.f, -12.f, -9.f, -6.f, -3.f, 0.f, 3.f, 6.f, 9.f, 12.f, 15.f };
        static const char* const GL[11] = { "-15", "12", "9", "6", "3", "0", "3", "6", "9", "12", "+15" };
        static const float QT[5] = { 0.f, .25f, .5f, .75f, 1.f };
        static const float QV[5] = { 3.f, 2.f, 1.5f, 1.f, .5f };
        static const char* const QL[5] = { "3", "2", "1.5", "1", ".5" };
        static const float FT7[7] = { 0.f, 1.f/6, 2.f/6, 3.f/6, 4.f/6, 5.f/6, 1.f };

        // LF band
        {
            const float lcx = 0.5f * (COL[1] + COL[2]);
            static const float FT[7] = { 0.f, 1.f/6, 2.f/6, 3.f/6, 4.f/6, 5.f/6, 1.f };
            static const float FV[7] = { 30.f, 50.f, 100.f, 200.f, 300.f, 400.f, 450.f };
            static const char* const FL[7] = { "30", "50", "100", "200", "300", "400", "450" };
            consoleDetentKnob(dl, "lfg", lcx, cY(314), 28.f, kLfGain, GT, GV, GL, 11, C_LF_BROWN, "dB", "%.1f dB");
            consoleDetentKnob(dl, "lff", lcx, cY(452), 28.f, kLfFreq, FT, FV, FL, 7, C_LF_BROWN, "Hz", "%.0f Hz");
            metalButton(dl, "lfbell", lcx - 32.f, cY(560), lcx + 32.f, cY(584), kLfBell, "BELL", "SHELF");
        }
        // LMF band
        {
            const float mcx = 0.5f * (COL[2] + COL[3]);
            static const float MFV[7] = { 200.f, 300.f, 800.f, 1000.f, 1500.f, 2000.f, 2500.f };
            static const char* const MFL[7] = { ".2", ".3", ".8", "1", "1.5", "2", "2.5" };
            consoleDetentKnob(dl, "lmg", mcx, cY(314), 28.f, kLmGain, GT, GV, GL, 11, C_LMF_BLUE, "dB", "%.1f dB");
            consoleDetentKnob(dl, "lmf", mcx, cY(452), 28.f, kLmFreq, FT7, MFV, MFL, 7, C_LMF_BLUE, "kHz", "%.0f Hz");
            consoleDetentKnob(dl, "lmq", mcx, cY(590), 28.f, kLmQ, QT, QV, QL, 5, C_LMF_BLUE, "", "Q %.2f");
            bandwidthIcons(dl, mcx, cY(590) + 40.f);
        }
        // HMF band
        {
            const float hcx = 0.5f * (COL[3] + COL[4]);
            static const float HFV[7] = { 600.f, 800.f, 1500.f, 3000.f, 4500.f, 6000.f, 7000.f };
            static const char* const HFL[7] = { ".6", ".8", "1.5", "3", "4.5", "6", "7" };
            consoleDetentKnob(dl, "hmg", hcx, cY(314), 28.f, kHmGain, GT, GV, GL, 11, C_HMF_GREEN, "dB", "%.1f dB");
            consoleDetentKnob(dl, "hmf", hcx, cY(452), 28.f, kHmFreq, FT7, HFV, HFL, 7, C_HMF_GREEN, "kHz", "%.0f Hz");
            consoleDetentKnob(dl, "hmq", hcx, cY(590), 28.f, kHmQ, QT, QV, QL, 5, C_HMF_GREEN, "", "Q %.2f");
            bandwidthIcons(dl, hcx, cY(590) + 40.f);
        }
        // HF band
        {
            const float hcx = 0.5f * (COL[4] + COL[5]);
            static const float XFV[7] = { 1500.f, 2000.f, 5000.f, 8000.f, 10000.f, 14000.f, 16000.f };
            static const char* const XFL[7] = { "1.5", "2", "5", "8", "10", "14", "16" };
            consoleDetentKnob(dl, "hfg", hcx, cY(314), 28.f, kHfGain, GT, GV, GL, 11, C_HF_RED, "dB", "%.1f dB");
            consoleDetentKnob(dl, "hff", hcx, cY(452), 28.f, kHfFreq, FT7, XFV, XFL, 7, C_HF_RED, "kHz", "%.0f Hz");
            metalButton(dl, "hfbell", hcx - 32.f, cY(560), hcx + 32.f, cY(584), kHfBell, "BELL", "SHELF");
        }

        // MASTER
        const float mcx = 0.5f * (COL[5] + COL[6]);
        panelButton(dl, "bypass", mcx - 40, cY(268), mcx + 40, cY(292),
                    values[kBypass] > 0.5f ? "BYPASSED" : "BYPASS",
                    values[kBypass] > 0.5f ? IM_COL32(150, 60, 48, 255) : IM_COL32(50, 50, 54, 255),
                    [&]{ toggleParam(kBypass); });
        panelButton(dl, "autogain", mcx - 40, cY(300), mcx + 40, cY(324), "AUTO GAIN",
                    values[kAutoGain] > 0.5f ? kGreenBtn : IM_COL32(50, 50, 54, 255),
                    [&]{ toggleParam(kAutoGain); });
        colMetalKnob(dl, "drive", kSaturation, 0.f, 100.f, mcx, cY(430), 26, "DRIVE", MK_DRIVE_V, MK_DRIVE_L, 5, "%.0f", "");
        colMetalKnob(dl, "outg", kOutputGain, -12.f, 12.f, mcx, cY(556), 26, "OUTPUT", MK_GAIN_V, MK_GAIN_L, 5, "%.1f", " dB");
        // NOTE: FourKEQ's M/S toggle is omitted here (no clean Multi-Q equivalent).
    }

    // knob + name label below + min/max tick labels at the dial ends.
    void colKnob(ImDrawList* dl, const char* id, uint32_t param, float minV, float maxV,
                 float cx, float cy, float r, ImU32 face, const char* name,
                 const char* lmin, const char* lmax, const char* fmt, const char* suffix)
    {
        panel.knob(id, param, minV, maxV, cx, cy, r, values[param], mqDef(param),
                   false, false, fmt, suffix, face);
        const float a0 = duskdpf::DuskPanel::knobAngle(0.0f), a1 = duskdpf::DuskPanel::knobAngle(1.0f);
        panel.text(dl, cx + std::sin(a0) * (r + 14), cy - std::cos(a0) * (r + 14) - 5, 9.5f, IM_COL32(170, 172, 176, 255), lmin, 1);
        panel.text(dl, cx + std::sin(a1) * (r + 14), cy - std::cos(a1) * (r + 14) - 5, 9.5f, IM_COL32(170, 172, 176, 255), lmax, -1);
        panel.text(dl, cx, cy + r + 8, 11.0f, IM_COL32(206, 208, 212, 255), name, 0, true);
    }

    void colMetalKnob(ImDrawList* dl, const char* id, uint32_t param, float minV, float maxV,
                      float cx, float cy, float r, const char* name,
                      const float* TV, const char* const* TL, int nT,
                      const char* fmt, const char* suffix)
    {
        const float range = maxV - minV;
        float t = range > 0.f ? (values[param] - minV) / range : 0.f;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        drawMetalKnobBody(dl, panel.P(cx, cy), r * sc(), t,
                          IM_COL32(150, 152, 156, 255), IM_COL32(30, 30, 33, 255));
        for (int i = 0; i < nT; ++i)
        {
            const float tt = range > 0.f ? (TV[i] - minV) / range : 0.f;
            const float a = duskdpf::DuskPanel::knobAngle(tt);
            const float dx = std::sin(a), dy = -std::cos(a);
            dl->AddCircleFilled(panel.P(cx + dx * (r + 9.f), cy + dy * (r + 9.f)), 1.6f * sc(), IM_COL32(150, 152, 156, 255), 8);
            const int align = dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0);
            panel.text(dl, cx + dx * (r + 20.f), cy + dy * (r + 20.f) - 5.f, 10.5f, IM_COL32(206, 208, 212, 255), TL[i], align, true);
        }
        panel.text(dl, cx, cy + r + 10.f, 11.0f, IM_COL32(206, 208, 212, 255), name, 0, true);
        panel.knob(id, param, minV, maxV, cx, cy, r, values[param], mqDef(param),
                   false, false, fmt, suffix, 0, /*bodyless*/true);
    }

    template <class Fn>
    void panelButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                     const char* label, ImU32 bg, Fn onClick)
    {
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) onClick();
        dl->AddRectFilled(b0, b1, bg, 4.0f * sc());
        dl->AddRect(b0, b1, hov ? IM_COL32(200, 200, 205, 220) : IM_COL32(90, 90, 96, 200), 4.0f * sc(), 0, 1.2f * sc());
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 10.0f, pal().white, label, 0, true);
    }

    //========================================================================
    // SSL-style stepped filter rotary (HPF & LPF)
    //========================================================================
    static float stepLT(int i) { static const float t[7] = {0.f, 1.f/6, 2.f/6, 3.f/6, 4.f/6, 5.f/6, 1.f}; return t[i]; }

    void stepPosToState(const float* F, float t, bool& en, float& f) const
    {
        if (t < 0.06f) { en = false; f = 0.f; return; }
        en = true;
        if (t <= stepLT(1)) { f = F[1]; return; }
        for (int i = 1; i <= 5; ++i)
            if (t <= stepLT(i + 1)) { const float a = (t - stepLT(i)) / (stepLT(i + 1) - stepLT(i)); f = F[i] + (F[i + 1] - F[i]) * a; return; }
        f = F[6];
    }
    float stepStateToPos(const float* F, bool en, float freq) const
    {
        if (!en) return 0.f;
        float lo = F[1], hi = F[1];
        for (int i = 2; i <= 6; ++i) { lo = std::min(lo, F[i]); hi = std::max(hi, F[i]); }
        const float fr = freq < lo ? lo : (freq > hi ? hi : freq);
        for (int i = 1; i <= 5; ++i)
            if ((fr - F[i]) * (fr - F[i + 1]) <= 0.f)
            {
                const float d = F[i + 1] - F[i];
                const float a = d != 0.f ? (fr - F[i]) / d : 0.f;
                return stepLT(i) + (stepLT(i + 1) - stepLT(i)) * a;
            }
        return 1.f;
    }
    void stepApply(uint32_t enId, uint32_t freqId, const float* F, float t)
    {
        bool en; float f; stepPosToState(F, t, en, f);
        values[enId] = en ? 1.f : 0.f; setParameterValue(enId, values[enId]);
        if (en) { values[freqId] = f; setParameterValue(freqId, f); }
    }

    void steppedFilterKnob(ImDrawList* dl, const char* id, float cx, float cy, float R,
                           uint32_t enId, uint32_t freqId, const char* const* labels,
                           const float* F, bool lowpass, const char* unit)
    {
        const float s = sc();
        const ImVec2 c = panel.P(cx, cy);
        const float RR = R * s;
        auto c01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };

        const bool en = values[enId] > 0.5f;
        float t = stepStateToPos(F, en, values[freqId]);

        ImGui::SetCursorScreenPos(ImVec2(c.x - RR, c.y - RR));
        ImGui::InvisibleButton(id, ImVec2(2.f * RR, 2.f * RR));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        const bool editing = panel.isEditingValue(id);
        const bool modKey = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (!editing)
        {
            if (ImGui::IsItemActivated())
            {
                if (modKey)
                { editParameter(enId, true); stepApply(enId, freqId, F, 0.f); editParameter(enId, false); t = 0.f; stepModReset_ = true; }
                else { editParameter(enId, true); editParameter(freqId, true); stepDragT = t; stepModReset_ = false; }
            }
            if (act && !stepModReset_)
            {
                const float sp = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
                stepDragT = c01(stepDragT - ImGui::GetIO().MouseDelta.y * sp);
                t = stepDragT; stepApply(enId, freqId, F, t);
            }
            if (ImGui::IsItemDeactivated()) { if (!stepModReset_) { editParameter(enId, false); editParameter(freqId, false); } stepModReset_ = false; }
            if (!modKey && (hov || act) && ImGui::IsMouseDoubleClicked(0))
            { panel.openValueEdit(id, values[freqId]); editParameter(enId, false); editParameter(freqId, false); }
            if (hov && !act)   // independent wheel branch (never skipped)
            {
                const float wh = ImGui::GetIO().MouseWheel;
                if (wh != 0.f)
                {
                    t = c01(t + wh * 0.02f);
                    editParameter(enId, true); editParameter(freqId, true);
                    stepApply(enId, freqId, F, t);
                    editParameter(freqId, false); editParameter(enId, false);
                }
            }
        }

        for (int i = 0; i < 7; ++i)
        {
            const float a = duskdpf::DuskPanel::knobAngle(stepLT(i));
            const float dx = std::sin(a), dy = -std::cos(a);
            dl->AddCircleFilled(panel.P(cx + dx * (R + 9.f), cy + dy * (R + 9.f)), 1.7f * s, IM_COL32(150, 152, 156, 255), 8);
            const int align = dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0);
            panel.text(dl, cx + dx * (R + 20.f), cy + dy * (R + 20.f) - 5.f, 11.f, IM_COL32(206, 208, 212, 255), labels[i], align, true);
        }

        drawMetalKnobBody(dl, c, RR, t, IM_COL32(150, 152, 156, 255), IM_COL32(30, 30, 33, 255));

        const float iy = cy + R + 22.f;
        if (lowpass)
        {
            ImVec2 ic[4] = { panel.P(cx + 4.f, iy - 3.f), panel.P(cx + 11.f, iy - 3.f), panel.P(cx + 18.f, iy), panel.P(cx + 23.f, iy + 6.f) };
            dl->AddPolyline(ic, 4, IM_COL32(196, 198, 202, 255), 0, 1.4f * s);
            panel.text(dl, cx - 4.f, iy - 9.f, 12.f, IM_COL32(210, 212, 216, 255), unit, 1, true);
        }
        else
        {
            ImVec2 ic[4] = { panel.P(cx - 22.f, iy + 6.f), panel.P(cx - 17.f, iy), panel.P(cx - 10.f, iy - 3.f), panel.P(cx + 4.f, iy - 3.f) };
            dl->AddPolyline(ic, 4, IM_COL32(196, 198, 202, 255), 0, 1.4f * s);
            panel.text(dl, cx + 8.f, iy - 9.f, 12.f, IM_COL32(210, 212, 216, 255), unit, -1, true);
        }

        float typed;
        if (panel.valueEdit(id, cx, cy, R, typed))
        {
            float lo = F[1], hi = F[1];
            for (int i = 2; i <= 6; ++i) { lo = std::min(lo, F[i]); hi = std::max(hi, F[i]); }
            typed = typed < lo ? lo : (typed > hi ? hi : typed);
            editParameter(enId, true); editParameter(freqId, true);
            values[enId] = 1.f;    setParameterValue(enId, 1.f);
            values[freqId] = typed; setParameterValue(freqId, typed);
            editParameter(freqId, false); editParameter(enId, false);
        }
        else if ((hov || act) && !editing)
        {
            char buf[24];
            if (!en) std::snprintf(buf, sizeof(buf), "OUT");
            else if (values[freqId] >= 1000.f) std::snprintf(buf, sizeof(buf), "%.1f kHz", values[freqId] / 1000.f);
            else std::snprintf(buf, sizeof(buf), "%.0f Hz", values[freqId]);
            panel.valueBubble(dl, cx, cy, R, buf);
        }
    }

    void drawMetalKnobBody(ImDrawList* dl, ImVec2 c, float RR, float t, ImU32 capCol, ImU32 pointerCol)
    {
        const float s = sc();
        dl->AddCircleFilled(c, RR, IM_COL32(18, 18, 20, 255), 48);
        dl->AddCircleFilled(c, RR * 0.95f, IM_COL32(88, 90, 94, 255), 48);
        for (int i = 0; i < 20; ++i)
        {
            const float a = (float)i / 20.f * 2.f * duskdpf::DuskPanel::kPi;
            const ImVec2 d(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(c.x + d.x * RR * 0.80f, c.y + d.y * RR * 0.80f),
                        ImVec2(c.x + d.x * RR * 0.93f, c.y + d.y * RR * 0.93f), IM_COL32(40, 40, 43, 160), 1.3f * s);
        }
        const float capR = RR * 0.74f;
        dl->AddCircleFilled(c, capR, capCol, 44);
        dl->PushClipRect(ImVec2(c.x - capR, c.y - capR), ImVec2(c.x + capR, c.y + capR), true);
        dl->AddCircleFilled(ImVec2(c.x, c.y + capR * 0.64f), capR * 0.82f, IM_COL32(0, 0, 0, 20), 40);
        dl->AddCircleFilled(ImVec2(c.x, c.y - capR * 0.60f), capR * 0.85f, IM_COL32(255, 255, 255, 13), 40);
        dl->AddCircleFilled(ImVec2(c.x, c.y - capR * 0.72f), capR * 0.55f, IM_COL32(255, 255, 255, 17), 40);
        dl->PopClipRect();
        dl->AddCircle(c, capR, IM_COL32(20, 20, 22, 255), 44, 1.4f * s);
        const float a = duskdpf::DuskPanel::knobAngle(t);
        const ImVec2 pd(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + pd.x * capR * 0.12f, c.y + pd.y * capR * 0.12f),
                    ImVec2(c.x + pd.x * capR * 0.92f, c.y + pd.y * capR * 0.92f), pointerCol, 3.4f * s);
    }

    //========================================================================
    // SSL-style console band knob (continuous over (t,value) breakpoints)
    //========================================================================
    static float detentPosToVal(const float* T, const float* V, int n, float t)
    {
        if (t <= T[0]) return V[0];
        if (t >= T[n - 1]) return V[n - 1];
        for (int i = 0; i < n - 1; ++i)
            if (t <= T[i + 1]) { const float a = (t - T[i]) / (T[i + 1] - T[i]); return V[i] + (V[i + 1] - V[i]) * a; }
        return V[n - 1];
    }
    static float detentValToPos(const float* T, const float* V, int n, float v)
    {
        float lo = V[0], hi = V[0];
        for (int i = 1; i < n; ++i) { lo = std::min(lo, V[i]); hi = std::max(hi, V[i]); }
        v = v < lo ? lo : (v > hi ? hi : v);
        for (int i = 0; i < n - 1; ++i)
            if ((v - V[i]) * (v - V[i + 1]) <= 0.f)
            {
                const float d = V[i + 1] - V[i];
                const float a = d != 0.f ? (v - V[i]) / d : 0.f;
                return T[i] + (T[i + 1] - T[i]) * a;
            }
        return T[n - 1];
    }

    void consoleDetentKnob(ImDrawList* dl, const char* id, float cx, float cy, float R,
                           uint32_t paramId, const float* T, const float* V,
                           const char* const* labels, int n, ImU32 capCol,
                           const char* unit, const char* fmt)
    {
        const float s = sc();
        const ImVec2 c = panel.P(cx, cy);
        const float RR = R * s;
        auto c01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };

        float t = detentValToPos(T, V, n, values[paramId]);

        ImGui::SetCursorScreenPos(ImVec2(c.x - RR, c.y - RR));
        ImGui::InvisibleButton(id, ImVec2(2.f * RR, 2.f * RR));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        const bool editing = panel.isEditingValue(id);
        const bool modKey = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        auto setFromT = [&](float tt) { const float nv = detentPosToVal(T, V, n, tt); values[paramId] = nv; setParameterValue(paramId, nv); };
        auto resetDefault = [&] { editParameter(paramId, true); values[paramId] = mqDef(paramId); setParameterValue(paramId, mqDef(paramId)); editParameter(paramId, false); t = detentValToPos(T, V, n, values[paramId]); };
        if (!editing)
        {
            if (ImGui::IsItemActivated())
            {
                if (modKey) { resetDefault(); stepModReset_ = true; }
                else { editParameter(paramId, true); stepDragT = t; stepModReset_ = false; }
            }
            if (act && !stepModReset_)
            {
                const float sp = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
                stepDragT = c01(stepDragT - ImGui::GetIO().MouseDelta.y * sp);
                t = stepDragT; setFromT(t);
            }
            if (ImGui::IsItemDeactivated()) { if (!stepModReset_) editParameter(paramId, false); stepModReset_ = false; }
            if (!modKey && (hov || act) && ImGui::IsMouseDoubleClicked(0))
            { panel.openValueEdit(id, values[paramId]); editParameter(paramId, false); }
            if (hov && !act)   // independent wheel branch (never skipped)
            {
                const float wh = ImGui::GetIO().MouseWheel;
                if (wh != 0.f) { t = c01(t + wh * 0.02f); editParameter(paramId, true); setFromT(t); editParameter(paramId, false); }
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const float a = duskdpf::DuskPanel::knobAngle(T[i]);
            const float dx = std::sin(a), dy = -std::cos(a);
            dl->AddCircleFilled(panel.P(cx + dx * (R + 9.f), cy + dy * (R + 9.f)), 1.6f * s, IM_COL32(150, 152, 156, 255), 8);
            const int align = dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0);
            panel.text(dl, cx + dx * (R + 20.f), cy + dy * (R + 20.f) - 5.f, 10.5f, IM_COL32(206, 208, 212, 255), labels[i], align, true);
        }

        drawMetalKnobBody(dl, c, RR, t, capCol, IM_COL32(245, 245, 245, 255));
        panel.text(dl, cx, cy + R + 16.f, 12.f, IM_COL32(210, 212, 216, 255), unit, 0, true);

        float typed;
        if (panel.valueEdit(id, cx, cy, R, typed))
        {
            float lo = V[0], hi = V[0];
            for (int i = 1; i < n; ++i) { lo = std::min(lo, V[i]); hi = std::max(hi, V[i]); }
            typed = typed < lo ? lo : (typed > hi ? hi : typed);
            editParameter(paramId, true); values[paramId] = typed; setParameterValue(paramId, typed); editParameter(paramId, false);
        }
        else if ((hov || act) && !editing)
        {
            char buf[24]; std::snprintf(buf, sizeof(buf), fmt, values[paramId]);
            panel.valueBubble(dl, cx, cy, R, buf);
        }
    }

    void bandwidthIcons(ImDrawList* dl, float cx, float iy)
    {
        const float s = sc();
        const ImU32 col = IM_COL32(150, 152, 156, 255);
        ImVec2 nar[5] = { panel.P(cx - 24.f, iy + 5.f), panel.P(cx - 20.f, iy + 5.f), panel.P(cx - 16.f, iy - 6.f), panel.P(cx - 12.f, iy + 5.f), panel.P(cx - 8.f, iy + 5.f) };
        dl->AddPolyline(nar, 5, col, 0, 1.4f * s);
        ImVec2 wid[6] = { panel.P(cx + 8.f, iy + 5.f), panel.P(cx + 12.f, iy + 5.f), panel.P(cx + 15.f, iy - 4.f), panel.P(cx + 19.f, iy - 4.f), panel.P(cx + 22.f, iy + 5.f), panel.P(cx + 26.f, iy + 5.f) };
        dl->AddPolyline(wid, 6, col, 0, 1.4f * s);
    }

    void metalButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                     uint32_t paramId, const char* onLabel, const char* offLabel)
    {
        const bool on = values[paramId] > 0.5f;
        const float s = sc();
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked()) toggleParam(paramId);
        dl->AddRectFilled(b0, b1, IM_COL32(18, 18, 20, 255), 5.f * s);
        const ImVec2 f0(b0.x + 1.6f * s, b0.y + 1.6f * s), f1(b1.x - 1.6f * s, b1.y - 1.6f * s);
        const ImU32 top = on ? IM_COL32(120, 122, 126, 255) : IM_COL32(182, 184, 188, 255);
        const ImU32 bot = on ? IM_COL32(150, 152, 156, 255) : IM_COL32(138, 140, 144, 255);
        dl->AddRectFilledMultiColor(f0, f1, top, top, bot, bot);
        dl->AddLine(ImVec2(f0.x, f0.y), ImVec2(f1.x, f0.y), IM_COL32(255, 255, 255, on ? 40 : 150), 1.2f * s);
        dl->AddLine(ImVec2(f0.x, f1.y), ImVec2(f1.x, f1.y), IM_COL32(0, 0, 0, on ? 120 : 60), 1.2f * s);
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 11.f, IM_COL32(34, 34, 38, 255), on ? onLabel : offLabel, 0, true);
    }

    //========================================================================
    // edge meters (INPUT left, OUTPUT right) — real in/out peak levels read off
    // the live DSP instance through the same-process bridge (MultiQAccess.hpp).
    //========================================================================
    void drawMeters(ImDrawList* dl)
    {
        float inL = 0, inR = 0, outL = 0, outR = 0;
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiQGetInputPeakL != nullptr) // weak: null in the split LV2 UI
            if (void* inst = getPluginInstancePointer())
            {
                inL  = multiQGetInputPeakL(inst);  inR  = multiQGetInputPeakR(inst);
                outL = multiQGetOutputPeakL(inst); outR = multiQGetOutputPeakR(inst);
            }
       #endif
        panel.text(dl, 0.5f * (INX0 + INX1), cY(MET_LBL_Y), 10.f, IM_COL32(160, 162, 166, 255), "IN", 0, true);
        panel.text(dl, 0.5f * (OUTX0 + OUTX1), cY(MET_LBL_Y), 10.f, IM_COL32(160, 162, 166, 255), "OUT", 0, true);
        meterPair(dl, INX0, INX1, inL, inR, 4);
        meterPair(dl, OUTX0, OUTX1, outL, outR, 6);

        meterPkIn_  = std::max(meterPkIn_,  std::max(inL, inR));
        meterPkOut_ = std::max(meterPkOut_, std::max(outL, outR));
        meterTimer_ += ImGui::GetIO().DeltaTime;
        if (meterTimer_ >= 0.15f)
        {
            meterTimer_ = 0.f;
            meterDbIn_  = 20.0f * std::log10(meterPkIn_  > 1e-5f ? meterPkIn_  : 1e-5f);
            meterDbOut_ = 20.0f * std::log10(meterPkOut_ > 1e-5f ? meterPkOut_ : 1e-5f);
            meterPkIn_ = meterPkOut_ = 0.f;
        }
        char b[16];
        std::snprintf(b, sizeof(b), "%.1f", meterDbIn_);
        panel.text(dl, 0.5f * (INX0 + INX1), cY(MET_Y1) + 5, 11.f, IM_COL32(160, 162, 166, 255), b, 0);
        std::snprintf(b, sizeof(b), "%.1f", meterDbOut_);
        panel.text(dl, 0.5f * (OUTX0 + OUTX1), cY(MET_Y1) + 5, 11.f, IM_COL32(160, 162, 166, 255), b, 0);
    }

    void meterPair(ImDrawList* dl, float x0, float x1, float l, float r, int slotBase)
    {
        const float y0 = cY(MET_Y0), y1 = cY(MET_Y1);
        dl->AddRectFilled(panel.P(x0 - 2, y0 - 2), panel.P(x1 + 2, y1 + 2), IM_COL32(60, 60, 63, 255), 2.0f * sc());
        dl->AddRectFilled(panel.P(x0, y0), panel.P(x1, y1), IM_COL32(10, 11, 13, 255));
        const float mid = 0.5f * (x0 + x1);
        ledMeterBar(dl, x0 + 1, mid - 0.5f, y0, y1, l, slotBase);       // British: slots 4-7
        ledMeterBar(dl, mid + 0.5f, x1 - 1, y0, y1, r, slotBase + 1);
    }

    duskdpf::DuskPanel panel;
    duskdpf::RealFFT fft;
    std::vector<float> specDb;
    duskdpf::CrispFontSet fontSet;
    ImFont* labelFont = nullptr;
    float meterPkIn_ = 0.f, meterPkOut_ = 0.f;
    float meterDbIn_ = -60.f, meterDbOut_ = -60.f;
    float meterTimer_ = 0.f;
    float values[kParamCount] = {};
    int currentPreset = -1;
    bool showGraph = true;
    bool showCredits = false;
    bool creditsArmed = false;
    bool presetOpen = false;
    bool showFft = true;
    int  graphRangeIdx = 2;
    int  digRangeIdx = 1;      // Digital plot range idx into {±12,±24,±30,±60,Warped}; default ±24
    int  dragBand_ = -1;       // Digital: band whose handle is being dragged
    int  selectedBand_ = 4;    // Digital: selected band for the detail panel (default Mid)
    bool abIsA_ = true;        // A/B compare: which bank is live (shared Digital+British)
    bool abInit_ = false;      // banks seeded from current state on first A/B use
    float abBankA_[kParamCount] = {};   // snapshot A (all automatable params)
    float abBankB_[kParamCount] = {};   // snapshot B
    bool tubeAnalyzer_ = true; // Tube view: FFT analyzer overlay on/off (UI-local)
    int  digPreset_ = -1;      // Digital: selected factory preset index (-1 = Init/none)
    int  tubePreset_ = -1;     // Tube: selected factory preset index (-1 = Init/none)

    // Digital analyzer state (own FFT + buffers, kept separate from British `fft`/
    // `specDb` so the two skins never fight over the FFT size). digFrozen_ + snapshot
    // are UI-local (JUCE freezes on the 'F' key; here it's a FREEZE toggle button).
    duskdpf::RealFFT   digFft_;
    int                digFftSize_ = 4096;   // current analyzer FFT size (2048/4096; High capped at 4096 = ring size)
    std::vector<float> digSpecDb_, digPeakDb_, digFrozenDb_;
    bool               digFrozen_ = false;   // UI-local spectrum freeze
    float              digLimGrDb_ = 0.f;     // smoothed limiter gain-reduction readout (dB)

    // Digital graph-interaction drag latch (JUCE EQGraphicDisplay drag model): the
    // modifier-selected mode + drag-start values are captured at mouse-down so
    // releasing a modifier mid-drag never switches modes.
    enum class DigDrag { None, FreqGain, GainOnly, FreqOnly, QOnly };
    DigDrag digDragMode_ = DigDrag::None;
    float   digDragStartFreq_ = 0.f, digDragStartGain_ = 0.f, digDragStartQ_ = 0.71f;
    ImVec2  digDragStartMouse_ { 0.f, 0.f };   // design coords at mouse-down
    int     digCtxBand_ = -1;  // band whose right-click context menu is open (-1 = empty)
    float smoothedDynGain_[8] = {}; // Digital: smoothed live per-band dyn-EQ gain (dB)
    // LED-segment meter hold state: peak-hold dB + clip-hold timer per channel.
    // Slots 0-3 = Digital in L/R + out L/R; 4-7 = British in L/R + out L/R.
    float ledPk_[8]   = { -60.f, -60.f, -60.f, -60.f, -60.f, -60.f, -60.f, -60.f };
    float ledClip_[8] = {};
    // Digital meter numeric readout (0.15 s peak-hold, like British drawMeters).
    float digMtRdIn_ = 0.f, digMtRdOut_ = 0.f, digMtRdTimer_ = 0.f;
    float digMtDbIn_ = -60.f, digMtDbOut_ = -60.f;
    float ctlDstTop_ = 220.0f, ctlScaleY_ = 1.0f;
    float stepDragT = 0.0f;
    bool  stepModReset_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiQUI)
};

UI* createUI() { return new MultiQUI(); }

END_NAMESPACE_DISTRHO

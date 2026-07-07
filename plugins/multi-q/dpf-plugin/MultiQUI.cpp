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
#include "FourKEQDSP.hpp"

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

    // ---- British factory presets (ported from FourKEQParams.hpp kFactoryPresets;
    // self-contained here to avoid the FourKEQ header's clashing kParamCount) ----
    struct BritishPreset
    {
        const char* name;
        float lfGain, lfFreq, lfBell;
        float lmGain, lmFreq, lmQ;
        float hmGain, hmFreq, hmQ;
        float hfGain, hfFreq, hfBell;
        float hpfFreq, lpfFreq;
        float saturation, outputGain, inputGain, eqType;
    };
    constexpr BritishPreset kBritishPresets[] =
    {
        { "Vocal Presence",        3.0f,100.f,0.f, -3.0f,300.f,1.3f,  4.0f,3500.f,0.7f,  2.0f,8000.f,0.f,   80.f,20000.f, 0.f,0.f,0.f,0.f },
        { "Kick Punch",            4.0f,50.f,0.f,  -2.5f,200.f,0.8f,  3.0f,2000.f,1.5f,  0.0f,8000.f,0.f,   30.f,20000.f, 0.f,0.f,0.f,0.f },
        { "Snare Crack",           0.0f,100.f,0.f,  4.0f,250.f,0.7f,  5.0f,5000.f,1.2f,  3.0f,8000.f,1.f,  150.f,20000.f, 0.f,0.f,0.f,0.f },
        { "Drum Bus Punch",        4.0f,70.f,0.f,  -3.0f,350.f,0.6f,  3.0f,3500.f,1.0f,  2.5f,10000.f,0.f,  20.f,20000.f, 25.f,0.f,0.f,1.f },
        { "Bass Warmth",           4.0f,80.f,0.f,  -3.0f,400.f,0.7f,  2.0f,1500.f,0.7f,  0.0f,8000.f,0.f,   20.f,10000.f, 0.f,0.f,0.f,0.f },
        { "Bass Guitar Polish",    5.0f,60.f,0.f,  -2.0f,250.f,1.0f,  3.0f,1200.f,0.8f,  2.0f,4500.f,1.f,   35.f,20000.f, 0.f,0.f,0.f,0.f },
        { "Acoustic Guitar",      -2.0f,100.f,0.f,  2.0f,200.f,0.7f,  3.0f,2500.f,0.9f,  4.0f,12000.f,0.f,  80.f,20000.f, 0.f,0.f,0.f,0.f },
        { "Piano Brilliance",      2.0f,80.f,0.f,  -2.5f,500.f,0.8f,  3.0f,2000.f,0.7f,  3.5f,8000.f,0.f,   30.f,20000.f, 0.f,0.f,0.f,0.f },
        { "Bright Mix",            2.0f,60.f,0.f,   0.0f,600.f,0.7f, -2.0f,2500.f,0.8f,  2.5f,10000.f,0.f,  20.f,20000.f, 20.f,0.f,0.f,0.f },
        { "Glue Bus",              2.0f,100.f,0.f,  0.0f,600.f,0.7f, -1.5f,3000.f,0.7f,  2.0f,10000.f,0.f,  20.f,20000.f, 20.f,0.f,0.f,0.f },
        { "Telephone EQ",          0.0f,100.f,0.f,  6.0f,1000.f,1.5f, 0.0f,2000.f,0.7f,  0.0f,8000.f,0.f,  300.f,3000.f,  0.f,0.f,0.f,0.f },
        { "Air & Silk",            0.0f,100.f,0.f,  0.0f,600.f,0.7f,  3.0f,7000.f,0.7f,  4.0f,15000.f,0.f,  20.f,20000.f, 0.f,0.f,0.f,0.f },
        { "Master Sheen",          0.0f,100.f,0.f,  0.0f,600.f,0.7f,  1.0f,5000.f,0.7f,  1.5f,16000.f,0.f,  20.f,20000.f, 10.f,0.f,0.f,0.f },
        { "Master Bus Sweetening", 1.0f,50.f,0.f,  -1.0f,600.f,0.5f,  0.5f,4000.f,0.6f,  1.5f,15000.f,0.f,  20.f,20000.f, 15.f,-0.5f,0.f,0.f },
    };
    constexpr int kNumBritishPresets = (int)(sizeof(kBritishPresets) / sizeof(kBritishPresets[0]));

    // Character selector (EQ Type: Digital / Match / British / Tube).
    const char* kCharLabels[4] = { "DIGITAL", "MATCH", "BRITISH", "TUBE" };
    constexpr int kBritishModeIndex = 2; // kMqParams eq_type: 0 Digital,1 Match,2 British,3 Tube
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
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

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
            drawSimpleHeader(dl);
            drawCharSelector(dl);
            if (showCredits)
            {
                ImGui::SetCursorScreenPos(ImVec2(0, 0));
                ImGui::InvisibleButton("modalblock", ImVec2(winW, winH));
            }
            drawPlaceholder(dl, (int)std::lround(values[kParamEqType]));
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

        // preset dropdown — custom crisp widget; the expanded list draws later, on top.
        {
            const ImVec2 p0 = panel.P(kPresetX0, kPresetY0), p1 = panel.P(kPresetX1, kPresetY1);
            dl->AddRectFilled(p0, p1, IM_COL32(46, 46, 50, 255), 3.f * sc());
            dl->AddRect(p0, p1, presetOpen ? IM_COL32(120, 150, 200, 240) : IM_COL32(90, 90, 96, 220), 3.f * sc(), 0, 1.f * sc());
            const char* preview = (currentPreset >= 0 && currentPreset < kNumBritishPresets)
                                      ? kBritishPresets[currentPreset].name : "Default";
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

    // Simple header for the non-British character placeholders (title only).
    void drawSimpleHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 88), kHeader);
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 3), IM_COL32(150, 150, 152, 255));
        dl->AddLine(panel.P(0, 88), panel.P(kDesignW, 88), IM_COL32(60, 60, 63, 255), 1.5f * sc());
        drawTitle(dl);
    }

    void drawTitle(ImDrawList* dl)
    {
        panel.text(dl, 28, 28, 25, pal().white, "Multi-Q 2", -1, true);
        panel.text(dl, 30, 58, 10.5f, IM_COL32(150, 152, 156, 255), "Console-Style Equalizer - British", -1);
        // Clickable title -> Patreon supporters overlay.
        {
            const ImVec2 t0 = panel.P(26, 20), t1 = panel.P(170, 48);
            ImGui::SetCursorScreenPos(t0);
            ImGui::InvisibleButton("titlecredits", ImVec2(t1.x - t0.x, t1.y - t0.y));
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) { showCredits = true; creditsArmed = false; }
        }
        panel.text(dl, kDesignW - 26, 68, 10.5f, IM_COL32(140, 142, 146, 255), "DUSK AUDIO", 1, true);
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
                   "Select BRITISH above for the console EQ.", 0);
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

    void applyPreset(int idx)
    {
        if (idx < 0 || idx >= kNumBritishPresets) return;
        currentPreset = idx;
        const BritishPreset& p = kBritishPresets[idx];
        struct KV { uint32_t id; float v; };
        const KV kv[] = {
            {kLfGain,p.lfGain},{kLfFreq,p.lfFreq},{kLfBell,p.lfBell},
            {kLmGain,p.lmGain},{kLmFreq,p.lmFreq},{kLmQ,p.lmQ},
            {kHmGain,p.hmGain},{kHmFreq,p.hmFreq},{kHmQ,p.hmQ},
            {kHfGain,p.hfGain},{kHfFreq,p.hfFreq},{kHfBell,p.hfBell},
            {kHpfFreq,p.hpfFreq},{kLpfFreq,p.lpfFreq},
            {kSaturation,p.saturation},{kOutputGain,p.outputGain},
            {kInputGain,p.inputGain},{kEqType,p.eqType},
            {kHpfEnabled, p.hpfFreq > 20.5f ? 1.0f : 0.0f},
            {kLpfEnabled, p.lpfFreq < 19999.0f ? 1.0f : 0.0f},
        };
        for (const KV& e : kv) { editParameter(e.id, true); values[e.id] = e.v; setParameterValue(e.id, e.v); editParameter(e.id, false); }
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
        const int n = kNumBritishPresets;
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
            panel.text(dl, kPresetX0 + 11.f, ry0 + 0.5f * kPresetRowH - 6.f, 12.f, IM_COL32(224, 224, 220, 255), kBritishPresets[i].name, -1);
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
            drawSpectrum(dl);
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

    // Multi-Q's Phase-2 core exposes no pre/post spectrum ring yet, so the FFT
    // overlay is a no-op (the toggle + button are kept for UI parity).
    void drawSpectrum(ImDrawList* dl) { (void)dl; }

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
            else if (hov && !act)
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
            else if (hov && !act)
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
    // edge meters (INPUT left, OUTPUT right) — Phase-2 core has no meters, so
    // levels read 0.0f stubs (bars empty). Kept for visual parity with 4K EQ.
    //========================================================================
    void drawMeters(ImDrawList* dl)
    {
        const float inL = 0, inR = 0, outL = 0, outR = 0;
        panel.text(dl, 0.5f * (INX0 + INX1), cY(MET_LBL_Y), 10.f, IM_COL32(160, 162, 166, 255), "IN", 0, true);
        panel.text(dl, 0.5f * (OUTX0 + OUTX1), cY(MET_LBL_Y), 10.f, IM_COL32(160, 162, 166, 255), "OUT", 0, true);
        meterPair(dl, INX0, INX1, inL, inR);
        meterPair(dl, OUTX0, OUTX1, outL, outR);

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

    void meterPair(ImDrawList* dl, float x0, float x1, float l, float r)
    {
        const float y0 = cY(MET_Y0), y1 = cY(MET_Y1);
        dl->AddRectFilled(panel.P(x0 - 2, y0 - 2), panel.P(x1 + 2, y1 + 2), IM_COL32(60, 60, 63, 255), 2.0f * sc());
        dl->AddRectFilled(panel.P(x0, y0), panel.P(x1, y1), IM_COL32(14, 16, 18, 255));
        const float mid = 0.5f * (x0 + x1);
        meterBar(dl, x0 + 1, mid - 0.5f, l);
        meterBar(dl, mid + 0.5f, x1 - 1, r);
    }

    void meterBar(ImDrawList* dl, float x0, float x1, float lin)
    {
        const float y0 = cY(MET_Y0), y1 = cY(MET_Y1);
        float db = 20.0f * std::log10(lin > 1e-5f ? lin : 1e-5f);
        float t = (db + 60.0f) / 60.0f; t = t < 0 ? 0 : (t > 1 ? 1 : t);
        const float yFill = y1 - t * (y1 - y0);
        ImU32 col = db > -1.5f ? IM_COL32(226, 70, 55, 255)
                  : db > -10.f ? IM_COL32(224, 196, 72, 255) : IM_COL32(96, 196, 112, 255);
        dl->AddRectFilled(panel.P(x0, yFill), panel.P(x1, y1), col);
        for (int i = 1; i < 12; ++i)
        {
            const float y = y0 + (float)i / 12.0f * (y1 - y0);
            dl->AddLine(panel.P(x0, y), panel.P(x1, y), IM_COL32(14, 16, 18, 200), 1.0f * sc());
        }
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
    float ctlDstTop_ = 220.0f, ctlScaleY_ = 1.0f;
    float stepDragT = 0.0f;
    bool  stepModReset_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiQUI)
};

UI* createUI() { return new MultiQUI(); }

END_NAMESPACE_DISTRHO

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachineUI.cpp — Dear ImGui UI for TapeMachine 2. Brushed-aluminium panel
// in the classic Swiss multichannel-recorder style: two spinning reels (one
// silver, one gold) flanking an amber dual VU bridge, a row of tape/EQ
// selectors, a knob row, and light Studer-style function buttons. Continuous
// controls render through the shared duskdpf::DuskPanel.

#include "DistrhoUI.hpp"
#include "TapeMachineAccess.hpp"
#include "TapeMachineParams.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"

#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {
    constexpr float kDesignW = 920.0f;
    constexpr float kDesignH = 480.0f;
    constexpr float kPi = 3.14159265358979f;

    // Brushed-aluminium palette (Studer-style silver panel, dark ink).
    constexpr ImU32 kColPanel   = IM_COL32(188, 189, 191, 255);
    constexpr ImU32 kColPanelHi = IM_COL32(206, 207, 209, 255);
    constexpr ImU32 kColPanelLo = IM_COL32(150, 151, 153, 255);
    constexpr ImU32 kColFrame   = IM_COL32(58, 58, 60, 255);   // dark chassis edge
    constexpr ImU32 kColInk     = IM_COL32(34, 34, 37, 255);   // dark text
    constexpr ImU32 kColInkDim  = IM_COL32(96, 97, 100, 255);
    constexpr ImU32 kColScrew    = IM_COL32(120, 121, 123, 255);
    constexpr ImU32 kColReelGold = IM_COL32(198, 166, 98, 255);
    constexpr ImU32 kColReelSilv = IM_COL32(158, 159, 162, 255);
    constexpr ImU32 kColRed      = IM_COL32(190, 55, 40, 255);
}

class TapeMachineUI : public UI, public duskdpf::ParamHost
{
public:
    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override { setParameterValue(idx, v); }

    TapeMachineUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kTmParams[i].def;
        setGeometryConstraints(460, 240, true);
        labelFont = duskdpf::loadCrispFont(30.0f * getScaleFactor());

        // Dark ink for labels/ticks/markers on the silver panel.
        duskdpf::Palette pal;
        pal.white    = kColInk;
        pal.whiteDim = kColInkDim;
        panel.setPalette(pal);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < kParamCount)
            values[index] = value;
    }

    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth();
        const float winH = (float)getHeight();
        s   = std::min(winW / kDesignW, winH / kDesignH);
        org = ImVec2(0.5f * (winW - kDesignW * s), 0.5f * (winH - kDesignH * s));
        panel.begin(s, org, labelFont, this);

        reelPhase += ImGui::GetIO().DeltaTime * reelSpeed();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("TapeMachine2", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        drawPanel(dl, winW, winH);
        drawHeader(dl);
        drawSelectorRow(dl);
        drawReel(dl, 108, 250, 68, +1.0f, kColReelSilv);
        drawReel(dl, 812, 250, 68, -1.0f, kColReelGold);
        drawMeterBridge(dl);
        drawKnobRow(dl);
        drawToggles(dl);

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    ImVec2 P(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }

    void text(ImDrawList* dl, float x, float y, float sz, ImU32 c, const char* t, int align, bool bold = false)
    { panel.text(dl, x, y, sz, c, t, align, bold); }

    float reelSpeed() const
    {
        if (values[kParamBypass] > 0.5f) return 0.0f;
        const int sp = (int)(values[kParamTapeSpeed] + 0.5f);
        return (sp == 2 ? 4.4f : sp == 1 ? 2.2f : 1.1f);
    }

    // Brushed-aluminium fill: base + faint horizontal grain + screws.
    void drawPanel(ImDrawList* dl, float winW, float winH)
    {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), kColFrame);
        dl->AddRectFilledMultiColor(P(0, 0), P(kDesignW, kDesignH),
                                    kColPanelHi, kColPanelHi, kColPanelLo, kColPanelLo);
        dl->AddRectFilled(P(6, 6), P(kDesignW - 6, kDesignH - 6), kColPanel);
        for (float y = 8.0f; y < kDesignH - 6.0f; y += 3.0f) // brushed grain
            dl->AddLine(P(8, y), P(kDesignW - 8, y), IM_COL32(255, 255, 255, 10), 1.0f);
        const float sx[6] = { 16, kDesignW - 16, 16, kDesignW - 16, kDesignW * 0.5f, kDesignW * 0.5f };
        const float sy[6] = { 16, 16, kDesignH - 16, kDesignH - 16, 16, kDesignH - 16 };
        for (int i = 0; i < 6; ++i) screw(dl, sx[i], sy[i]);
    }

    void screw(ImDrawList* dl, float x, float y) const
    {
        const ImVec2 c = P(x, y);
        dl->AddCircleFilled(c, 5.0f * s, kColScrew, 16);
        dl->AddCircle(c, 5.0f * s, IM_COL32(80, 80, 82, 255), 16, 1.0f * s);
        dl->AddLine(ImVec2(c.x - 3 * s, c.y - 3 * s), ImVec2(c.x + 3 * s, c.y + 3 * s),
                    IM_COL32(70, 70, 72, 255), 1.2f * s);
    }

    void drawHeader(ImDrawList* dl)
    {
        // recessed dark nameplate (Studer-style: dark plate, light text)
        dl->AddRectFilled(P(30, 10), P(300, 38), IM_COL32(30, 30, 32, 255), 3.0f * s);
        dl->AddRect(P(30, 10), P(300, 38), IM_COL32(120, 120, 122, 255), 3.0f * s, 0, 1.4f * s);
        text(dl, 44, 15, 17, IM_COL32(232, 232, 228, 255), "TapeMachine 2", -1, true);
        text(dl, kDesignW - 30, 15, 14, kColInk, "Dusk Audio", 1, true);
    }

    void selector(ImDrawList* dl, const char* id, uint32_t param, float x, float y,
                  float w, const char* title)
    {
        const TmParam& d = kTmParams[param];
        text(dl, x + 0.5f * w, y, 9.5f, kColInk, title, 0, true);

        int cur = (int)(values[param] + 0.5f);
        if (cur < 0) cur = 0; if (cur >= d.numChoices) cur = d.numChoices - 1;

        ImGui::SetCursorScreenPos(P(x, y + 13.0f));
        ImGui::SetNextItemWidth(w * s);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(232, 232, 232, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(244, 244, 244, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(238, 238, 238, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(180, 150, 90, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColInk);
        if (ImGui::BeginCombo(id, d.choices[cur], ImGuiComboFlags_NoArrowButton))
        {
            for (int i = 0; i < d.numChoices; ++i)
                if (ImGui::Selectable(d.choices[i], i == cur))
                {
                    values[param] = (float)i;
                    editParameter(param, true);
                    setParameterValue(param, (float)i);
                    editParameter(param, false);
                }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(5);
    }

    void drawSelectorRow(ImDrawList* dl)
    {
        const float w = 112.0f, y = 54.0f;
        const float xs[7] = { 18, 145, 272, 399, 526, 653, 780 };
        selector(dl, "##machine", kParamTapeMachine, xs[0], y, w, "MACHINE");
        selector(dl, "##speed",   kParamTapeSpeed,   xs[1], y, w, "TAPE SPEED");
        selector(dl, "##type",    kParamTapeType,    xs[2], y, w, "TAPE TYPE");
        selector(dl, "##path",    kParamSignalPath,  xs[3], y, w, "SIGNAL PATH");
        selector(dl, "##eq",      kParamEqStandard,  xs[4], y, w, "EQ");
        selector(dl, "##cal",     kParamCalibration, xs[5], y, w, "CALIBRATION");
        selector(dl, "##os",      kParamOversampling,xs[6], y, w, "OVERSAMPLING");
    }

    void drawReel(ImDrawList* dl, float cx, float cy, float r, float dir, ImU32 flangeCol)
    {
        const ImVec2 c = P(cx, cy);
        dl->AddCircleFilled(c, r * s, flangeCol, 56);
        dl->AddCircle(c, r * s, IM_COL32(70, 70, 72, 255), 56, 2.0f * s);
        dl->AddCircleFilled(c, r * 0.90f * s, IM_COL32(74, 58, 40, 255), 56); // exposed tape pack
        dl->AddCircleFilled(c, r * 0.58f * s, flangeCol, 48);
        dl->AddCircle(c, r * 0.58f * s, IM_COL32(90, 90, 92, 255), 48, 1.4f * s);
        // three large flange cutouts, rotating
        const float a0 = reelPhase * dir;
        for (int i = 0; i < 3; ++i)
        {
            const float a = a0 + i * (2.0f * kPi / 3.0f);
            const ImVec2 h(c.x + std::sin(a) * r * 0.72f * s, c.y - std::cos(a) * r * 0.72f * s);
            dl->AddCircleFilled(h, r * 0.15f * s, IM_COL32(74, 58, 40, 255), 24);
            dl->AddCircle(h, r * 0.15f * s, IM_COL32(90, 90, 92, 200), 24, 1.0f * s);
        }
        // hub
        dl->AddCircleFilled(c, r * 0.22f * s, IM_COL32(210, 210, 212, 255), 28);
        dl->AddCircleFilled(c, r * 0.22f * s, IM_COL32(150, 150, 152, 255), 28);
        dl->AddCircle(c, r * 0.22f * s, IM_COL32(80, 80, 82, 255), 28, 1.4f * s);
        for (int i = 0; i < 3; ++i) // hub spokes
        {
            const float a = a0 * 1.0f + i * (2.0f * kPi / 3.0f);
            dl->AddLine(c, ImVec2(c.x + std::sin(a) * r * 0.20f * s, c.y - std::cos(a) * r * 0.20f * s),
                        IM_COL32(90, 90, 92, 255), 1.4f * s);
        }
        dl->AddCircleFilled(c, r * 0.07f * s, IM_COL32(40, 40, 42, 255), 12);
    }

    void drawMeterBridge(ImDrawList* dl)
    {
        float lvlL = values[kParamVuL], lvlR = values[kParamVuR];
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (tapeMachineGetVuL != nullptr && tapeMachineGetVuR != nullptr)
            if (void* const inst = getPluginInstancePointer())
            { lvlL = tapeMachineGetVuL(inst); lvlR = tapeMachineGetVuR(inst); }
       #endif
        // two framed cream VU meters side by side (ported from the JUCE
        // AnalogVUMeter: silver frame, dark bezel, cream face, red needle,
        // -20..+3 VU scale with red zone, deco screws, glass highlight).
        drawVU(dl, 258, 148, 454, 296, lvlL, needleL, "L");
        drawVU(dl, 466, 148, 662, 296, lvlR, needleR, "R");
    }

    // JUCE AnalogVUMeter palette.
    static constexpr float kVuA0 = -2.70f, kVuA1 = -0.44f; // -20 .. +3 VU angles

    void drawVU(ImDrawList* dl, float x0, float y0, float x1, float y1,
                float level, float& needle, const char* label)
    {
        // ballistics: 0 VU == -18 dBFS; needle 0..1 over -20..+3 VU.
        const float dB = 20.0f * std::log10(level > 1e-4f ? level : 1e-4f) + 18.0f;
        float target = (dB + 20.0f) / 23.0f;
        target = target < 0.0f ? 0.0f : (target > 1.0f ? 1.0f : target);
        needle += (target - needle) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 11.0f));

        // frame: silver bezel -> dark inner -> cream face
        dl->AddRectFilledMultiColor(P(x0, y0), P(x1, y1),
            IM_COL32(206, 199, 184, 255), IM_COL32(196, 189, 174, 255),
            IM_COL32(168, 161, 146, 255), IM_COL32(160, 153, 138, 255));
        dl->AddRectFilled(P(x0 + 3, y0 + 3), P(x1 - 3, y1 - 3), IM_COL32(58, 58, 58, 255), 3.0f * s);
        const float fx0 = x0 + 6, fy0 = y0 + 6, fx1 = x1 - 6, fy1 = y1 - 6;
        dl->AddRectFilled(P(fx0, fy0), P(fx1, fy1), IM_COL32(245, 240, 230, 255), 2.0f * s);
        dl->AddRectFilledMultiColor(P(fx0, fy0), P(fx1, fy1),
            IM_COL32(255, 252, 244, 90), IM_COL32(255, 252, 244, 90),
            IM_COL32(236, 228, 212, 90), IM_COL32(236, 228, 212, 90));

        const float cx = 0.5f * (fx0 + fx1);
        const float pivotY = fy1 - 5.0f;
        const float faceW = fx1 - fx0, faceH = fy1 - fy0;
        const float L = std::min(faceW * 0.48f, faceH * 0.90f);

        auto pt = [&](float r, float a) { return P(cx + r * std::cos(a), pivotY + r * std::sin(a)); };

        // red zone arc (0..+3 VU)
        {
            const float a0 = kVuA0 + (20.0f / 23.0f) * (kVuA1 - kVuA0);
            const float rr = L * 0.86f;
            dl->PathClear();
            dl->PathArcTo(P(cx, pivotY), rr * s, a0, kVuA1, 24);
            dl->PathStroke(IM_COL32(212, 44, 44, 150), 0, 3.0f * s);
        }
        // scale ticks + numbers
        const float dbv[11] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
        for (int i = 0; i < 11; ++i)
        {
            const float db = dbv[i];
            const float a = kVuA0 + ((db + 20.0f) / 23.0f) * (kVuA1 - kVuA0);
            const bool major = !(db == -2 || db == 2);
            const bool red = db >= 0.0f;
            const float tick = major ? 8.0f : 5.0f;
            const float rt = L * 0.94f;
            dl->AddLine(pt(rt, a), pt(rt + tick, a), red ? IM_COL32(212, 44, 44, 255) : IM_COL32(42, 42, 42, 255),
                        (major ? 1.7f : 1.0f) * s);
            const bool showNum = (db == -20 || db == -10 || db == -5 || db == 0 || db == 3);
            if (showNum)
            {
                char b[6]; std::snprintf(b, sizeof(b), db == 0 ? "0" : db > 0 ? "+%d" : "%d", (int)db);
                const ImVec2 tp = pt(L * 0.72f, a);
                const char* fnt = b; ImFont* f = labelFont ? labelFont : ImGui::GetFont();
                const float fs2 = 10.0f * s; ImVec2 ts = f->CalcTextSizeA(fs2, FLT_MAX, 0, fnt);
                dl->AddText(f, fs2, ImVec2(tp.x - ts.x * 0.5f, tp.y - ts.y * 0.5f),
                            red ? IM_COL32(190, 40, 40, 255) : IM_COL32(42, 42, 42, 255), b);
            }
        }
        // "VU" legend
        text(dl, cx, pivotY - L * 0.46f, 11, IM_COL32(42, 42, 42, 255), "VU", 0, true);
        text(dl, cx, pivotY - L * 0.46f + 13.0f, 8.5f, IM_COL32(90, 90, 90, 255), label, 0, true);

        // needle (red, with shadow) + pivot cap
        const float na = kVuA0 + needle * (kVuA1 - kVuA0);
        dl->AddLine(P(cx + 2, pivotY + 2), P(cx + 2 + L * 0.95f * std::cos(na), pivotY + 2 + L * 0.95f * std::sin(na)),
                    IM_COL32(0, 0, 0, 60), 3.0f * s);
        {
            const float perp = na + 1.5707963f, bw = 3.5f;
            const ImVec2 tip = pt(L * 0.95f, na);
            dl->AddTriangleFilled(P(cx + bw * 0.5f * std::cos(perp), pivotY + bw * 0.5f * std::sin(perp)),
                                  tip,
                                  P(cx - bw * 0.5f * std::cos(perp), pivotY - bw * 0.5f * std::sin(perp)),
                                  IM_COL32(204, 51, 51, 255));
        }
        dl->AddCircleFilled(P(cx, pivotY), 4.5f * s, IM_COL32(20, 20, 20, 255), 18);
        dl->AddCircleFilled(P(cx - 1.2f * s, pivotY - 1.4f * s), 1.6f * s, IM_COL32(120, 120, 120, 160), 10);

        // glass highlight (top) + deco screws
        dl->AddRectFilledMultiColor(P(fx0 + 4, fy0 + 2), P(fx1 - 4, fy0 + faceH * 0.22f),
            IM_COL32(255, 255, 255, 34), IM_COL32(255, 255, 255, 34),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        const float m = 9.0f;
        for (float sx : { x0 + m, x1 - m })
            for (float sy : { y0 + m, y1 - m })
            {
                dl->AddCircleFilled(P(sx, sy), 3.0f * s, IM_COL32(176, 168, 152, 255), 14);
                dl->AddLine(P(sx - 2, sy), P(sx + 2, sy), IM_COL32(26, 26, 24, 255), 1.2f * s);
            }
    }

    void knob(ImDrawList* dl, const char* id, uint32_t param, float cx, float cy,
              const char* l1, const char* l2, const char* fmt, const char* suffix)
    {
        const TmParam& d = kTmParams[param];
        panel.knobLabel(dl, cx, cy - 46.0f, l1, l2);
        panel.knob(id, param, d.min, d.max, cx, cy, 26.0f, values[param], d.def,
                   false, true, fmt, suffix);
    }

    void drawKnobRow(ImDrawList* dl)
    {
        const float cy = 372.0f;
        knob(dl, "input",  kParamInputGain,   80,  cy, "INPUT",   nullptr, "%.1f", " dB");
        knob(dl, "bias",   kParamBias,        183, cy, "BIAS",    nullptr, "%.0f", "%");
        knob(dl, "hp",     kParamHighpassFreq,286, cy, "HP",      nullptr, "%.0f", " Hz");
        knob(dl, "lp",     kParamLowpassFreq, 389, cy, "LP",      nullptr, "%.0f", " Hz");
        knob(dl, "noise",  kParamNoiseAmount, 492, cy, "NOISE",   nullptr, "%.0f", "%");
        knob(dl, "wow",    kParamWow,         595, cy, "WOW",     nullptr, "%.0f", "%");
        knob(dl, "flut",   kParamFlutter,     698, cy, "FLUTTER", nullptr, "%.0f", "%");
        knob(dl, "output", kParamOutputGain,  825, cy, "OUTPUT",  nullptr, "%.1f", " dB");
    }

    // Light Studer-style function button: raised silver cap, dark label, red
    // legend + LED when engaged. Flips the (0/1) parameter.
    void tmButton(ImDrawList* dl, const char* id, uint32_t param, float x0, float y0,
                  float x1, float y1, const char* label)
    {
        const bool on = values[param] > 0.5f;
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked())
        {
            const float nv = on ? 0.0f : 1.0f;
            editParameter(param, true); values[param] = nv;
            setParameterValue(param, nv); editParameter(param, false);
        }
        dl->AddRectFilledMultiColor(b0, b1, IM_COL32(226, 226, 228, 255), IM_COL32(226, 226, 228, 255),
                                    IM_COL32(188, 188, 190, 255), IM_COL32(188, 188, 190, 255));
        dl->AddRect(b0, b1, IM_COL32(90, 90, 92, 255), 2.0f * s, 0, 1.4f * s);
        if (on)
            dl->AddCircleFilled(P(x0 + 8.0f, 0.5f * (y0 + y1)), 2.6f * s, kColRed, 12);
        text(dl, 0.5f * (x0 + x1) + 5.0f, y0 + 0.30f * (y1 - y0), 9.5f,
             on ? kColRed : kColInk, label, 0, true);
    }

    void drawToggles(ImDrawList* dl)
    {
        tmButton(dl, "autocal",  kParamAutoCal,      140, 430, 250, 452, "AUTO CAL");
        tmButton(dl, "noiseen",  kParamNoiseEnabled, 452, 430, 552, 452, "NOISE");
        tmButton(dl, "autocomp", kParamAutoComp,     660, 430, 790, 452, "AUTO COMP");
        tmButton(dl, "bypass",   kParamBypass,       690, 12,  770, 34,  "BYPASS");
    }

    //--- state -----------------------------------------------------------------
    duskdpf::DuskPanel panel;
    ImFont* labelFont = nullptr;
    float   values[kParamCount] = {};
    float   s = 1.0f;
    ImVec2  org = ImVec2(0, 0);
    float   reelPhase = 0.0f;
    float   needleL = 0.0f, needleR = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeMachineUI)
};

UI* createUI()
{
    return new TapeMachineUI();
}

END_NAMESPACE_DISTRHO

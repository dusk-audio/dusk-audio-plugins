// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachineUI.cpp — Dear ImGui UI for TapeMachine 2. Layout mirrors the JUCE
// TapeMachine editor (800x568): header, a transport row with two reels flanking
// a stereo VU bridge over two rows of three selectors, then a main knob row and
// a character knob row. Recolored to a brushed-silver panel; the chrome knobs
// (shared duskdpf::DuskPanel) and the cream JUCE VU meters are kept.

#include "DistrhoUI.hpp"
#include "TapeMachineAccess.hpp"
#include "TapeMachineParams.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"

#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {
    constexpr float kDesignW = 800.0f;
    constexpr float kDesignH = 568.0f;

    constexpr ImU32 kColPanel   = IM_COL32(188, 189, 191, 255);
    constexpr ImU32 kColPanelHi = IM_COL32(206, 207, 209, 255);
    constexpr ImU32 kColPanelLo = IM_COL32(150, 151, 153, 255);
    constexpr ImU32 kColFrame   = IM_COL32(58, 58, 60, 255);
    constexpr ImU32 kColInk     = IM_COL32(34, 34, 37, 255);
    constexpr ImU32 kColInkDim  = IM_COL32(96, 97, 100, 255);
    constexpr ImU32 kColScrew    = IM_COL32(120, 121, 123, 255);
    constexpr ImU32 kColReel     = IM_COL32(158, 159, 162, 255);
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
        setGeometryConstraints(400, 284, true);
        labelFont = duskdpf::loadCrispFont(30.0f * getScaleFactor());

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

        // transport: reels flank the centre column (VU + two selector rows)
        drawReel(dl, 75, 167, 52);
        drawReel(dl, 725, 167, 52);
        drawVU(dl, 140, 68, 396, 184, meterLevel(0), needleL, "L");
        drawVU(dl, 404, 68, 660, 184, meterLevel(1), needleR, "R");
        drawSelectors(dl);
        drawKnobs(dl);

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    ImVec2 P(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }
    void text(ImDrawList* dl, float x, float y, float sz, ImU32 c, const char* t, int align, bool bold = false)
    { panel.text(dl, x, y, sz, c, t, align, bold); }

    float meterLevel(int ch)
    {
        float v = values[ch == 0 ? kParamVuL : kParamVuR];
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (tapeMachineGetVuL != nullptr && tapeMachineGetVuR != nullptr)
            if (void* const inst = getPluginInstancePointer())
                v = ch == 0 ? tapeMachineGetVuL(inst) : tapeMachineGetVuR(inst);
       #endif
        return v;
    }

    float reelSpeed() const
    {
        if (values[kParamBypass] > 0.5f) return 0.0f;
        const int sp = (int)(values[kParamTapeSpeed] + 0.5f);
        return (sp == 2 ? 4.4f : sp == 1 ? 2.2f : 1.1f);
    }

    void drawPanel(ImDrawList* dl, float winW, float winH)
    {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), kColFrame);
        dl->AddRectFilledMultiColor(P(0, 0), P(kDesignW, kDesignH),
                                    kColPanelHi, kColPanelHi, kColPanelLo, kColPanelLo);
        dl->AddRectFilled(P(6, 6), P(kDesignW - 6, kDesignH - 6), kColPanel);
        for (float y = 8.0f; y < kDesignH - 6.0f; y += 3.0f)
            dl->AddLine(P(8, y), P(kDesignW - 8, y), IM_COL32(255, 255, 255, 10), 1.0f);
        const float sx[4] = { 16, kDesignW - 16, 16, kDesignW - 16 };
        const float sy[4] = { 16, 16, kDesignH - 16, kDesignH - 16 };
        for (int i = 0; i < 4; ++i) screw(dl, sx[i], sy[i]);
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
        dl->AddRectFilled(P(15, 12), P(300, 40), IM_COL32(30, 30, 32, 255), 3.0f * s);
        dl->AddRect(P(15, 12), P(300, 40), IM_COL32(120, 120, 122, 255), 3.0f * s, 0, 1.4f * s);
        text(dl, 28, 17, 17, IM_COL32(232, 232, 228, 255), "TapeMachine 2", -1, true);
        text(dl, 690, 18, 13, kColInk, "Dusk Audio", 1, true);
        tmButton(dl, "bypass", kParamBypass, 704, 14, 785, 38, "BYPASS");
    }

    //--- transport -------------------------------------------------------------
    void drawReel(ImDrawList* dl, float cx, float cy, float r)
    {
        const ImVec2 c = P(cx, cy);
        dl->AddCircleFilled(c, r * s, kColReel, 56);
        dl->AddCircle(c, r * s, IM_COL32(70, 70, 72, 255), 56, 2.0f * s);
        dl->AddCircleFilled(c, r * 0.90f * s, IM_COL32(120, 120, 123, 255), 56);
        dl->AddCircleFilled(c, r * 0.58f * s, kColReel, 48);
        dl->AddCircle(c, r * 0.58f * s, IM_COL32(90, 90, 92, 255), 48, 1.4f * s);
        const float a0 = reelPhase;
        for (int i = 0; i < 3; ++i)
        {
            const float a = a0 + i * (2.0944f);
            const ImVec2 h(c.x + std::sin(a) * r * 0.72f * s, c.y - std::cos(a) * r * 0.72f * s);
            dl->AddCircleFilled(h, r * 0.15f * s, IM_COL32(96, 96, 99, 255), 24);
            dl->AddCircle(h, r * 0.15f * s, IM_COL32(70, 70, 72, 200), 24, 1.0f * s);
        }
        dl->AddCircleFilled(c, r * 0.22f * s, IM_COL32(205, 205, 207, 255), 28);
        dl->AddCircle(c, r * 0.22f * s, IM_COL32(80, 80, 82, 255), 28, 1.4f * s);
        for (int i = 0; i < 3; ++i)
        {
            const float a = a0 + i * 2.0944f;
            dl->AddLine(c, ImVec2(c.x + std::sin(a) * r * 0.20f * s, c.y - std::cos(a) * r * 0.20f * s),
                        IM_COL32(90, 90, 92, 255), 1.4f * s);
        }
        dl->AddCircleFilled(c, r * 0.07f * s, IM_COL32(40, 40, 42, 255), 12);
    }

    static constexpr float kVuA0 = -2.70f, kVuA1 = -0.44f;

    void drawVU(ImDrawList* dl, float x0, float y0, float x1, float y1,
                float level, float& needle, const char* label)
    {
        const float dB = 20.0f * std::log10(level > 1e-4f ? level : 1e-4f) + 18.0f;
        // deflection is linear in signal level (%), not dB: gives the classic
        // log-spread VU scale (marks bunch at the bottom, open up near 0..+3).
        float target = std::pow(10.0f, (dB - 3.0f) / 20.0f);
        target = target < 0.0f ? 0.0f : (target > 1.0f ? 1.0f : target);
        needle += (target - needle) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 11.0f));

        // warm tan bezel -> dark inner lip -> aged-cream face
        dl->AddRectFilledMultiColor(P(x0, y0), P(x1, y1),
            IM_COL32(170, 142, 100, 255), IM_COL32(158, 130, 88, 255),
            IM_COL32(120, 96, 60, 255),  IM_COL32(110, 88, 54, 255));
        dl->AddRect(P(x0, y0), P(x1, y1), IM_COL32(92, 72, 44, 255), 6.0f * s, 0, 1.4f * s);
        dl->AddRectFilled(P(x0 + 4, y0 + 4), P(x1 - 4, y1 - 4), IM_COL32(56, 44, 30, 255), 4.0f * s);
        const float fx0 = x0 + 7, fy0 = y0 + 7, fx1 = x1 - 7, fy1 = y1 - 7;
        dl->AddRectFilled(P(fx0, fy0), P(fx1, fy1), IM_COL32(240, 231, 205, 255), 3.0f * s);
        dl->AddRectFilledMultiColor(P(fx0, fy0), P(fx1, fy1),
            IM_COL32(252, 246, 226, 130), IM_COL32(252, 246, 226, 130),
            IM_COL32(210, 194, 158, 150), IM_COL32(210, 194, 158, 150));
        // corner vignette
        for (float vx : { fx0, fx1 })
            for (float vy : { fy0, fy1 })
                dl->AddCircleFilled(P(vx, vy), (fy1 - fy0) * 0.45f * s, IM_COL32(120, 96, 62, 26), 20);

        const float cx = 0.5f * (fx0 + fx1);
        const float pivotY = fy1 - 4.0f;
        const float L = std::min((fx1 - fx0) * 0.48f, (fy1 - fy0) * 0.92f);
        auto pt = [&](float r, float a) { return P(cx + r * std::cos(a), pivotY + r * std::sin(a)); };
        ImFont* f = labelFont ? labelFont : ImGui::GetFont();
        const ImU32 ink = IM_COL32(38, 32, 24, 255), red = IM_COL32(196, 42, 34, 255);
        auto num = [&](float r, float a, const char* b, ImU32 c, float sz) {
            ImVec2 ts = f->CalcTextSizeA(sz * s, FLT_MAX, 0, b);
            ImVec2 tp = pt(r, a);
            dl->AddText(f, sz * s, ImVec2(tp.x - ts.x * 0.5f, tp.y - ts.y * 0.5f), c, b);
        };
        auto ang = [&](float db) { float n = std::pow(10.0f, (db - 3.0f) / 20.0f);
                                   n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
                                   return kVuA0 + n * (kVuA1 - kVuA0); };

        // bold red arc over the 0..+3 zone
        dl->PathClear();
        dl->PathArcTo(P(cx, pivotY), L * 0.90f * s, ang(0.0f), kVuA1, 26);
        dl->PathStroke(red, 0, 3.4f * s);

        // dB scale: ticks + numbers (black below 0, red above)
        const float dbv[11] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
        for (int i = 0; i < 11; ++i)
        {
            const float db = dbv[i], a = ang(db);
            const bool major = !(db == -2 || db == 2);
            const bool rz = db > 0.0f;
            const float rt = L * 0.90f;
            dl->AddLine(pt(rt, a), pt(rt + (major ? 6.0f : 4.0f), a),
                        rz ? red : ink, (major ? 1.6f : 1.0f) * s);
            char b[6]; std::snprintf(b, sizeof(b), "%d", (int)std::abs(db));
            num(L * 0.80f, a, b, rz ? red : ink, 9.5f);
        }
        // percentage row (0..100) across the black zone, closer to the pivot
        const float pct0 = std::pow(10.0f, -3.0f / 20.0f); // 0 VU == 100%
        for (int k = 0; k <= 5; ++k)
        {
            const float aP = kVuA0 + (k / 5.0f) * pct0 * (kVuA1 - kVuA0);
            char b[6]; std::snprintf(b, sizeof(b), "%d", k * 20);
            num(L * 0.60f, aP, b, IM_COL32(70, 60, 46, 255), 7.5f);
        }
        // - (left) and + (red, right) marks in the top corners
        text(dl, fx0 + 10, fy0 + 3, 15.0f, ink, "-", -1, true);
        text(dl, fx1 - 10, fy0 + 3, 15.0f, red, "+", 1, true);

        // peak LED (right)
        dl->AddCircleFilled(P(fx1 - 15, pivotY - L * 0.20f), 4.6f * s, IM_COL32(112, 40, 30, 255), 16);
        dl->AddCircleFilled(P(fx1 - 16, pivotY - L * 0.20f - 1), 1.5f * s, IM_COL32(210, 130, 110, 120), 10);

        // VU legend (kept at its current position) + channel tag
        text(dl, cx, pivotY - L * 0.46f, 11, ink, "VU", 0, true);
        text(dl, cx, pivotY - L * 0.46f + 13.0f, 8.5f, IM_COL32(90, 80, 64, 255), label, 0, true);

        // black needle with soft shadow + mound pivot
        const float na = kVuA0 + needle * (kVuA1 - kVuA0);
        dl->AddLine(P(cx + 2, pivotY + 1), pt(L * 0.95f, na), IM_COL32(60, 50, 36, 70), 4.0f * s);
        {
            const float perp = na + 1.5707963f, bw = 3.4f;
            dl->AddTriangleFilled(P(cx + bw * 0.5f * std::cos(perp), pivotY + bw * 0.5f * std::sin(perp)),
                                  pt(L * 0.95f, na),
                                  P(cx - bw * 0.5f * std::cos(perp), pivotY - bw * 0.5f * std::sin(perp)),
                                  IM_COL32(28, 24, 18, 255));
        }
        dl->AddCircleFilled(P(cx, pivotY + 1), 8.0f * s, IM_COL32(40, 32, 22, 90), 20); // mound shadow
        dl->AddCircleFilled(P(cx, pivotY), 4.6f * s, IM_COL32(24, 20, 14, 255), 18);
        dl->AddCircleFilled(P(cx - 1.2f * s, pivotY - 1.4f * s), 1.5f * s, IM_COL32(150, 140, 120, 150), 10);

        // glass top highlight
        dl->AddRectFilledMultiColor(P(fx0 + 4, fy0 + 2), P(fx1 - 4, fy0 + (fy1 - fy0) * 0.22f),
            IM_COL32(255, 255, 255, 30), IM_COL32(255, 255, 255, 30),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    }

    //--- selectors (two rows of three, in the centre column) -------------------
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
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(150, 152, 156, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColInk);
        if (ImGui::BeginCombo(id, d.choices[cur], ImGuiComboFlags_NoArrowButton))
        {
            for (int i = 0; i < d.numChoices; ++i)
                if (ImGui::Selectable(d.choices[i], i == cur))
                {
                    values[param] = (float)i;
                    editParameter(param, true); setParameterValue(param, (float)i); editParameter(param, false);
                }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(5);
    }

    void drawSelectors(ImDrawList* dl)
    {
        const float W = 176.67f, x0 = 135.0f, w = W - 10.0f;
        const float col[3] = { x0 + 5, x0 + W + 5, x0 + 2 * W + 5 };
        selector(dl, "##machine", kParamTapeMachine, col[0], 190, w, "TAPE MACHINE");
        selector(dl, "##speed",   kParamTapeSpeed,   col[1], 190, w, "TAPE SPEED");
        selector(dl, "##type",    kParamTapeType,    col[2], 190, w, "TAPE TYPE");
        selector(dl, "##path",    kParamSignalPath,  col[0], 238, w, "SIGNAL PATH");
        selector(dl, "##eq",      kParamEqStandard,  col[1], 238, w, "EQ STANDARD");
        selector(dl, "##os",      kParamOversampling,col[2], 238, w, "OVERSAMPLING");
    }

    //--- knob rows -------------------------------------------------------------
    void knob(ImDrawList* dl, const char* id, uint32_t param, float cx, float cy,
              const char* l1, const char* fmt, const char* suffix)
    {
        const TmParam& d = kTmParams[param];
        panel.knobLabel(dl, cx, cy - 50.0f, l1);
        panel.knob(id, param, d.min, d.max, cx, cy, 32.0f, values[param], d.def, false, true, fmt, suffix);
    }

    void drawKnobs(ImDrawList* dl)
    {
        // main row: input, bias, wow, flutter, output
        const float cy1 = 356.0f;
        knob(dl, "input",  kParamInputGain, 116.67f, cy1, "INPUT",   "%.1f", " dB");
        knob(dl, "bias",   kParamBias,      258.34f, cy1, "BIAS",    "%.0f", "%");
        knob(dl, "wow",    kParamWow,       400.00f, cy1, "WOW",     "%.0f", "%");
        knob(dl, "flut",   kParamFlutter,   541.67f, cy1, "FLUTTER", "%.0f", "%");
        knob(dl, "output", kParamOutputGain,683.34f, cy1, "OUTPUT",  "%.1f", " dB");

        // character row: hp, lp, noise, + auto-comp / auto-cal buttons
        const float cy2 = 482.0f;
        knob(dl, "hp",    kParamHighpassFreq, 110.0f, cy2, "HIGHPASS", "%.0f", " Hz");
        knob(dl, "lp",    kParamLowpassFreq,  245.0f, cy2, "LOWPASS",  "%.0f", " Hz");
        knob(dl, "noise", kParamNoiseAmount,  380.0f, cy2, "NOISE",    "%.0f", "%");
        text(dl, 525, cy2 - 50.0f, 10, kColInk, "AUTO COMP", 0, true);
        tmButton(dl, "autocomp", kParamAutoComp, 480, cy2 - 18, 570, cy2 + 18, "LINK");
        text(dl, 680, cy2 - 50.0f, 10, kColInk, "AUTO CAL", 0, true);
        tmButton(dl, "autocal",  kParamAutoCal,  630, cy2 - 18, 730, cy2 + 18, "AUTO");
    }

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
                                    IM_COL32(186, 186, 188, 255), IM_COL32(186, 186, 188, 255));
        dl->AddRect(b0, b1, IM_COL32(90, 90, 92, 255), 2.0f * s, 0, 1.4f * s);
        if (on) dl->AddCircleFilled(P(x0 + 8.0f, 0.5f * (y0 + y1)), 2.6f * s, kColRed, 12);
        text(dl, 0.5f * (x0 + x1) + 5.0f, y0 + 0.32f * (y1 - y0), 10.0f,
             on ? kColRed : kColInk, label, 0, true);
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

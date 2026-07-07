// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachineUI.cpp — Dear ImGui UI for TapeMachine 2 (800x520). Brushed-silver
// panel: header (title, preset selector, bypass); two large Sifam-style VU
// meters; a row of six tape/EQ selectors; a main knob row and a character knob
// row. Chrome knobs via the shared duskdpf::DuskPanel. All drawn strings are
// ASCII (the atlas font lacks some Unicode glyphs).

#include "DistrhoUI.hpp"
#include "TapeMachineAccess.hpp"
#include "TapeMachineParams.hpp"
#include "TapeMachinePresets.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {
    constexpr float kDesignW = 800.0f;
    constexpr float kDesignH = 470.0f;

    constexpr ImU32 kColPanel   = IM_COL32(188, 189, 191, 255);
    constexpr ImU32 kColPanelHi = IM_COL32(206, 207, 209, 255);
    constexpr ImU32 kColPanelLo = IM_COL32(150, 151, 153, 255);
    constexpr ImU32 kColFrame   = IM_COL32(58, 58, 60, 255);
    constexpr ImU32 kColInk     = IM_COL32(34, 34, 37, 255);
    constexpr ImU32 kColInkDim  = IM_COL32(96, 97, 100, 255);
    constexpr ImU32 kColScrew   = IM_COL32(120, 121, 123, 255);
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
        setGeometryConstraints(400, 260, true);
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
        {
            values[index] = value;
            if (index < kParamVuL) currentPreset = -1; // an edit deselects the preset
        }
    }

    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth();
        const float winH = (float)getHeight();
        s   = std::min(winW / kDesignW, winH / kDesignH);
        org = ImVec2(0.5f * (winW - kDesignW * s), 0.5f * (winH - kDesignH * s));
        panel.begin(s, org, labelFont, this);

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
        drawVU(dl, 68,  62, 388, 198, meterLevel(0), needleL, "L");
        drawVU(dl, 412, 62, 732, 198, meterLevel(1), needleR, "R");
        drawSelectors(dl);
        drawControls(dl);

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

    void applyPreset(int idx)
    {
        currentPreset = idx;
        tmApplyPreset(idx, [this](uint32_t id, float v) {
            values[id] = v;
            editParameter(id, true); setParameterValue(id, v); editParameter(id, false);
        });
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
        dl->AddRectFilled(P(15, 12), P(232, 40), IM_COL32(30, 30, 32, 255), 3.0f * s);
        dl->AddRect(P(15, 12), P(232, 40), IM_COL32(120, 120, 122, 255), 3.0f * s, 0, 1.4f * s);
        text(dl, 27, 17, 16, IM_COL32(232, 232, 228, 255), "TapeMachine 2", -1, true);

        // preset selector (centre of header)
        const char* cur = (currentPreset >= 0 && currentPreset < kNumTmPresets)
                              ? kTmPresets[currentPreset].name : "Presets...";
        text(dl, 262, 8, 8.5f, kColInkDim, "PRESET", -1, true);
        ImGui::SetCursorScreenPos(P(262, 18));
        ImGui::SetNextItemWidth(300.0f * s);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(232, 232, 232, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(244, 244, 244, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(238, 238, 238, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(150, 152, 156, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(198, 199, 201, 255));        // arrow bg
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(212, 213, 215, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(178, 179, 181, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColInk);
        if (ImGui::BeginCombo("##preset", cur))
        {
            const char* lastCat = nullptr;
            for (int i = 0; i < kNumTmPresets; ++i)
            {
                if (lastCat == nullptr || std::strcmp(lastCat, kTmPresets[i].category) != 0)
                {
                    lastCat = kTmPresets[i].category;
                    ImGui::TextDisabled("%s", lastCat);
                }
                if (ImGui::Selectable(kTmPresets[i].name, i == currentPreset))
                    applyPreset(i);
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(8);

        // bypass, kept clear of the top-right corner screw (~x784)
        tmButton(dl, "bypass", kParamBypass, 648, 14, 736, 38, "BYPASS");
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

        // VU legend + channel tag
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
        dl->AddCircleFilled(P(cx, pivotY + 1), 8.0f * s, IM_COL32(40, 32, 22, 90), 20);
        dl->AddCircleFilled(P(cx, pivotY), 4.6f * s, IM_COL32(24, 20, 14, 255), 18);
        dl->AddCircleFilled(P(cx - 1.2f * s, pivotY - 1.4f * s), 1.5f * s, IM_COL32(150, 140, 120, 150), 10);

        // glass top highlight
        dl->AddRectFilledMultiColor(P(fx0 + 4, fy0 + 2), P(fx1 - 4, fy0 + (fy1 - fy0) * 0.22f),
            IM_COL32(255, 255, 255, 30), IM_COL32(255, 255, 255, 30),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    }

    //--- selectors (single row of six) -----------------------------------------
    // cx = column centre; w = combo width (sized to fit its widest option +
    // the dropdown arrow). The combo is centred under its (centred) label.
    void selector(ImDrawList* dl, const char* id, uint32_t param, float cx, float y,
                  float w, const char* title)
    {
        const TmParam& d = kTmParams[param];
        text(dl, cx, y, 9.0f, kColInk, title, 0, true);
        int cur = (int)(values[param] + 0.5f);
        if (cur < 0) cur = 0; if (cur >= d.numChoices) cur = d.numChoices - 1;

        ImGui::SetCursorScreenPos(P(cx - 0.5f * w, y + 13.0f));
        ImGui::SetNextItemWidth(w * s);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(232, 232, 232, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(244, 244, 244, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(238, 238, 238, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(150, 152, 156, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(198, 199, 201, 255));        // arrow bg
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(212, 213, 215, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(178, 179, 181, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColInk);
        if (ImGui::BeginCombo(id, d.choices[cur]))   // arrow shown -> reads as a dropdown
        {
            for (int i = 0; i < d.numChoices; ++i)
                if (ImGui::Selectable(d.choices[i], i == cur))
                {
                    values[param] = (float)i;
                    editParameter(param, true); setParameterValue(param, (float)i); editParameter(param, false);
                }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(8);
    }

    void drawSelectors(ImDrawList* dl)
    {
        const float y = 216.0f;
        // id, param, column-centre, width (fits widest option + arrow), title
        struct Sel { const char* id; uint32_t p; float cx, w; const char* t; };
        static const Sel ss[6] = {
            { "##machine", kParamTapeMachine,  83.f, 100.f, "MACHINE"      },
            { "##speed",   kParamTapeSpeed,   210.f,  82.f, "TAPE SPEED"   },
            { "##type",    kParamTapeType,    337.f,  90.f, "TAPE TYPE"    },
            { "##path",    kParamSignalPath,  463.f,  78.f, "SIGNAL PATH"  },
            { "##eq",      kParamEqStandard,  590.f,  70.f, "EQ STANDARD"  },
            { "##os",      kParamOversampling,717.f,  62.f, "OVERSAMPLING" },
        };
        for (const Sel& e : ss)
            selector(dl, e.id, e.p, e.cx, y, e.w, e.t);
    }

    //--- knob rows -------------------------------------------------------------
    // Persistent value readout, right-click reset + tooltip inherited from the
    // shared knob. dispAdd shifts the read-out (BIAS shows relative over/under).
    void knob(ImDrawList* dl, const char* id, uint32_t param, float cx, float cy,
              const char* l1, const char* fmt, const char* suffix,
              const char* tip, float dispAdd = 0.0f)
    {
        const TmParam& d = kTmParams[param];
        panel.knobLabel(dl, cx, cy - 50.0f, l1);
        panel.knob(id, param, d.min, d.max, cx, cy, 32.0f, values[param], d.def,
                   false, true, fmt, suffix, 0, false,
                   /*persistent*/ true, tip, /*rightClickReset*/ true, 1.0f, dispAdd);
    }

    // Centred small-caps section header with an underline.
    void sectionHeader(ImDrawList* dl, float cx, float y, const char* txt)
    {
        text(dl, cx, y, 9.5f, kColInkDim, txt, 0, true);
        ImFont* f = labelFont ? labelFont : ImGui::GetFont();
        const float w = f->CalcTextSizeA(9.5f * s, FLT_MAX, 0, txt).x;
        const ImVec2 c = P(cx, y + 13.0f);
        dl->AddLine(ImVec2(c.x - w * 0.5f, c.y), ImVec2(c.x + w * 0.5f, c.y),
                    IM_COL32(120, 121, 123, 220), 1.2f * s);
    }

    // Four functional groups: GAIN STAGING | TAPE CHARACTER | TRANSPORT | FILTERS.
    // Auto-comp (gain link) sits with gain staging; auto-cal (auto bias) with tape
    // character, matching how each toggle couples to its knobs.
    void drawControls(ImDrawList* dl)
    {
        const float hy = 264.0f, cy = 336.0f, ty = 396.0f;

        sectionHeader(dl, 115, hy, "GAIN STAGING");
        sectionHeader(dl, 305, hy, "TAPE CHARACTER");
        sectionHeader(dl, 495, hy, "TRANSPORT");
        sectionHeader(dl, 685, hy, "FILTERS");
        for (float dx : { 210.0f, 400.0f, 590.0f })
            dl->AddLine(P(dx, hy - 2), P(dx, ty + 28), IM_COL32(150, 151, 153, 200), 1.0f * s);

        // GAIN STAGING
        knob(dl, "input",  kParamInputGain,  67.0f, cy, "INPUT",  "%.1f", " dB",
             "Drive into the tape stage. Higher = more saturation.");
        knob(dl, "output", kParamOutputGain,163.0f, cy, "OUTPUT", "%.1f", " dB",
             "Make-up gain after the tape stage.");
        text(dl, 115, ty, 9.0f, kColInk, "GAIN LINK", 0, true);
        tmButton(dl, "autocomp", kParamAutoComp, 82, ty + 10, 148, ty + 30, "ON",
                 "Auto gain compensation: output tracks input so level stays matched.");

        // TAPE CHARACTER
        knob(dl, "bias",  kParamBias,       257.0f, cy, "BIAS",  "%+.0f", "%",
             "Tape bias vs. optimal calibration. Under-bias = grittier, more distortion.", -50.0f);
        knob(dl, "noise", kParamNoiseAmount,353.0f, cy, "NOISE", "%.0f", "%",
             "Tape hiss level. 0% = clean/silent.");
        text(dl, 305, ty, 9.0f, kColInk, "AUTO BIAS", 0, true);
        tmButton(dl, "autocal", kParamAutoCal, 272, ty + 10, 338, ty + 30, "ON",
                 "Auto bias calibration for the selected tape and speed (disables the BIAS knob).");

        // TRANSPORT
        knob(dl, "wow",  kParamWow,     447.0f, cy, "WOW",     "%.0f", "%",
             "Slow pitch drift (~0.5 Hz) from the transport.");
        knob(dl, "flut", kParamFlutter, 543.0f, cy, "FLUTTER", "%.0f", "%",
             "Faster pitch modulation (tape/motor flutter).");

        // FILTERS
        knob(dl, "hp", kParamHighpassFreq, 637.0f, cy, "HIGHPASS", "%.0f", " Hz",
             "High-pass filter on the output.");
        knob(dl, "lp", kParamLowpassFreq,  733.0f, cy, "LOWPASS",  "%.0f", " Hz",
             "Low-pass filter on the output (tape HF roll-off).");
    }

    void tmButton(ImDrawList* dl, const char* id, uint32_t param, float x0, float y0,
                  float x1, float y1, const char* label, const char* tip = nullptr)
    {
        const bool on = values[param] > 0.5f;
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (tip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
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
    float   needleL = 0.0f, needleR = 0.0f;
    int     currentPreset = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeMachineUI)
};

UI* createUI()
{
    return new TapeMachineUI();
}

END_NAMESPACE_DISTRHO

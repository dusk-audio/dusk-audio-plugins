// TapeEchoUI.cpp — Dear ImGui UI for Tape Echo, modeled on the original
// classic tape-echo hardware front panel: black chassis, green MODE SELECTOR block with
// numbered dial, recessed metal-trimmed control panel, chrome knobs with
// triangle markers, VU meter and peak LED. Hardware I/O (jacks, switches
// for mic routing) is intentionally not reproduced.
// All rendering is custom ImDrawList work in a 900x320 design space.

#include "DistrhoUI.hpp"
#include "TapeEchoAccess.hpp"
#include "TapeEchoDSP.hpp"
#include "TapeEchoParams.hpp"
#include "DuskImGuiFont.hpp"      // shared crisp-bold loader (candidate search + DPI)
#include "DuskImGuiWidgets.hpp"   // shared DuskPanel: chrome knob, LED, text, value bubble
#include "DuskSupportersOverlay.hpp" // shared DPF Patreon "Special Thanks" overlay

#include <cfloat>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace
{
    constexpr float kDesignW = 900.0f;
    constexpr float kDesignH = 320.0f;

    // palette (sampled from the hardware)
    constexpr ImU32 kColChassis   = IM_COL32(28, 28, 30, 255);
    constexpr ImU32 kColHeader    = IM_COL32(16, 16, 17, 255);
    constexpr ImU32 kColRecess    = IM_COL32(20, 20, 22, 255);
    constexpr ImU32 kColMetal     = IM_COL32(150, 150, 152, 255);
    constexpr ImU32 kColGreen     = IM_COL32(88, 118, 58, 255);
    constexpr ImU32 kColGreenDk   = IM_COL32(48, 68, 30, 255);
    constexpr ImU32 kColWhite     = IM_COL32(238, 236, 228, 255);
    constexpr ImU32 kColWhiteDim  = IM_COL32(202, 200, 191, 255);
    constexpr ImU32 kColLedOn     = IM_COL32(255, 60, 40, 255);
    constexpr ImU32 kColLedGlow   = IM_COL32(255, 70, 45, 90);
    constexpr ImU32 kColLedOff    = IM_COL32(70, 20, 15, 255);

    constexpr float kParamDefaults[kParamCount] =
    {
        1.0f,   // mode
        0.0f,   // repeat rate
        0.0f,   // intensity
        0.5f,   // echo level
        0.0f,   // reverb level
        0.0f,   // bass
        0.0f,   // treble
        0.5f,   // input gain
        0.0f,   // wow & flutter
        1.0f,   // dry level
        0.0f,   // tempo sync off
        2.0f,   // sync division: 1/16
        0.5f,   // tape age: used
        0.5f,   // output volume: unity
        0.5f,   // echo pan: center
        0.5f,   // reverb pan: center
        1.0f,   // input send on
        0.0f,   // wet solo off
        0.0f,   // loop splice (momentary)
        0.0f,   // bypass (power ON)
        0.0f,   // out level (meter, output-only)
    };

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
            values[i] = kParamDefaults[i];
        // The 13-row preset popup is intentionally non-scrolling; keep the
        // minimum canvas at the design size so every row remains visible.
        setGeometryConstraints(900, 320, true);

        // Crisp bold label font via the shared loader (candidate search, DPI
        // scale + atlas build live in DuskImGuiFont.hpp so every Dusk UI matches).
        // Null -> text() falls back to the ImGui default face (softer, never gone).
        labelFont = duskdpf::loadCrispFont(30.0f * getScaleFactor());
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < kParamCount)
            values[index] = value;
    }

    void programLoaded(uint32_t index) override
    {
        currentPreset = index < (uint32_t)kNumFactoryPresets
                      ? (int)index
                      : -1;
    }

    void onImGuiDisplay() override
    {
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
        dl->AddRectFilled(P(0, 0), P(kDesignW, kDesignH), kColChassis);

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
        drawPowerBlock(dl);
        if (modalOpen)
            ImGui::EndDisabled();
        if (showSupporters)
            duskdpf::drawSupportersOverlay(
                panel, dl, kDesignW, kDesignH, showSupporters, "Tape Echo 2");

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    //--- helpers -----------------------------------------------------------------
    ImVec2 P(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }

    // Thin adapters onto the shared duskdpf::DuskPanel (all the chrome-knob
    // drawing, LED, text crisping, gesture handling + value bubble live there
    // now; every Dusk DPF plugin renders identical controls through it).
    void text(ImDrawList* dl, float x, float y, float size, ImU32 col,
              const char* txt, int align /* -1 left, 0 center, 1 right */,
              bool bold = false) const
    {
        panel.text(dl, x, y, size, col, txt, align, bold);
    }

    void led(ImDrawList* dl, float x, float y, bool on, float r = 5.0f) const
    {
        panel.led(dl, x, y, on, r);
    }

    static float knobAngle(float t) { return duskdpf::DuskPanel::knobAngle(t); }

    void knob(const char* id, uint32_t param, float minV, float maxV,
              float cx, float cy, float radius, bool stepped = false,
              bool panelTicks = true)
    {
        panel.knob(id, param, minV, maxV, cx, cy, radius,
                   values[param], kParamDefaults[param], stepped, panelTicks,
                   stepped ? "%.0f" : "%.2f");
    }

    void knobLabel(ImDrawList* dl, float cx, float topY, const char* l1,
                   const char* l2 = nullptr) const
    {
        panel.knobLabel(dl, cx, topY, l1, l2);
    }

    //--- sections ---------------------------------------------------------------------
    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(P(0, 0), P(kDesignW, 46), kColHeader);
        dl->AddRectFilled(P(0, 0), P(kDesignW, 3), kColMetal);
        dl->AddLine(P(0, 46), P(kDesignW, 46), IM_COL32(70, 70, 72, 255), 1.5f * s);

        // name plate
        dl->AddRect(P(38, 8), P(360, 38), IM_COL32(210, 210, 210, 200), 4.0f * s,
                    0, 1.6f * s);
        text(dl, 52, 12, 20, kColWhite, "TAPE ECHO 2", -1, true);
        text(dl, 235, 15, 15, kColWhiteDim, "TE-3", -1, true);
        text(dl, kDesignW - 30, 14, 15, kColWhite, "Dusk Audio", 1, true);

        // Match the other DPF plugins: clicking the title nameplate opens the
        // generated Patreon "Special Thanks" panel.
        ImGui::SetCursorScreenPos(P(38, 8));
        if (ImGui::InvisibleButton(
                "##titlecredits", ImVec2(322.0f * s, 30.0f * s)))
            showSupporters = true;

        // preset dropdown
        ImGui::SetCursorScreenPos(P(392, 10));
        ImGui::SetNextItemWidth(210.0f * s);
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
                                  ? kFactoryPresets[currentPreset].name : "Presets...";
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
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(12);
    }

    void applyPreset(int idx)
    {
        if (idx < 0 || idx >= kNumFactoryPresets)
            return;
        currentPreset = idx;
        const auto apply = [this](uint32_t param, float value)
        {
            editParameter(param, true);
            values[param] = value;
            setParameterValue(param, value);
            editParameter(param, false);
        };
        const TapeEchoPreset& preset = kFactoryPresets[idx];
        for (uint32_t i = 0; i <= (uint32_t)kParamTapeAge; ++i)
            apply(i, preset.v[i]);
        apply(kParamOutputVolume, preset.outputVolume);
        apply(kParamEchoPan, preset.echoPan);
        apply(kParamReverbPan, preset.reverbPan);
        apply(kParamInputSend, preset.inputSend);
        apply(kParamWetSolo, preset.wetSolo);
        apply(kParamBypass, preset.bypass);
    }

    void drawMeterBlock(ImDrawList* dl)
    {
        // peak LED (meterLevel is refreshed each frame in the VU block below)
        led(dl, 52, 92, meterLevel > 0.89f, 6.0f);
        text(dl, 52, 106, 9.5f, kColWhite, "PEAK", 0);
        text(dl, 52, 117, 9.5f, kColWhite, "LEVEL", 0);

        // VU meter housing
        const float x0 = 92, y0 = 60, x1 = 232, y1 = 142;
        dl->AddRectFilled(P(x0 - 3, y0 - 3), P(x1 + 3, y1 + 3), IM_COL32(70, 70, 72, 255), 4.0f * s);
        dl->AddRectFilled(P(x0, y0), P(x1, y1), IM_COL32(12, 14, 10, 255), 3.0f * s);

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

        const ImVec2 pivot = P(162, 210);         // below the face
        const float rArc   = 118.0f * s;
        const float aMin   = -0.62f, aMax = 0.62f; // radians from vertical

        // scale ticks + red zone
        for (int i = 0; i <= 10; ++i)
        {
            const float a = aMin + (aMax - aMin) * (float)i / 10.0f;
            const ImVec2 dir(std::sin(a), -std::cos(a));
            const bool redZone = i >= 8;
            dl->AddLine(ImVec2(pivot.x + dir.x * (rArc - 6.0f * s), pivot.y + dir.y * (rArc - 6.0f * s)),
                        ImVec2(pivot.x + dir.x * (rArc + (i % 5 == 0 ? 4.0f : 1.0f) * s),
                               pivot.y + dir.y * (rArc + (i % 5 == 0 ? 4.0f : 1.0f) * s)),
                        redZone ? IM_COL32(230, 60, 45, 255) : IM_COL32(225, 223, 210, 255),
                        (i % 5 == 0 ? 1.8f : 1.2f) * s);
        }
        // red zone arc band
        dl->PathClear();
        for (int i = 0; i <= 8; ++i)
        {
            const float a = aMin + (aMax - aMin) * (0.8f + 0.2f * (float)i / 8.0f);
            dl->PathLineTo(ImVec2(pivot.x + std::sin(a) * (rArc + 2.0f * s),
                                  pivot.y - std::cos(a) * (rArc + 2.0f * s)));
        }
        dl->PathStroke(IM_COL32(230, 60, 45, 255), 0, 2.4f * s);

        // legend
        text(dl, 162, 108, 13, IM_COL32(140, 225, 120, 255), "VU", 0, true);
        text(dl, 162, 124, 9, IM_COL32(230, 70, 55, 255), "Dusk Audio", 0);

        // needle
        {
            const float a = aMin + (aMax - aMin) * needlePos;
            const ImVec2 dir(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(pivot.x + dir.x * 30.0f * s, pivot.y + dir.y * 30.0f * s),
                        ImVec2(pivot.x + dir.x * (rArc + 4.0f * s), pivot.y + dir.y * (rArc + 4.0f * s)),
                        IM_COL32(240, 238, 225, 255), 1.8f * s);
        }

        // glass highlight
        dl->AddRectFilledMultiColor(P(x0, y0), P(x1, y0 + 26),
                                    IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 10),
                                    IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        dl->PopClipRect();
    }

    void drawInputRow(ImDrawList* dl)
    {
        // Four evenly-spaced controls retain the original DIRECT path while
        // keeping the newer output trim. Their tick rings remain clear of the
        // mode-selector panel at x=288.
        knobLabel(dl, 42, 165, "INPUT", "VOLUME");
        knob("input", kParamInputGain, 0.0f, 1.0f, 42, 240, 25);

        knobLabel(dl, 112, 165, "DRY", "LEVEL");
        knob("dry", kParamDryLevel, 0.0f, 1.0f, 112, 240, 25);

        knobLabel(dl, 182, 165, "WOW /", "FLUTTER");
        knob("wow", kParamWowFlutter, 0.0f, 1.0f, 182, 240, 25);

        knobLabel(dl, 252, 165, "OUTPUT", "VOLUME");
        knob("output", kParamOutputVolume, 0.0f, 1.0f, 252, 240, 25);
    }

    void drawModeSelector(ImDrawList* dl)
    {
        const float x0 = 288, y0 = 58, x1 = 462, y1 = 302;
        dl->AddRectFilled(P(x0, y0), P(x1, y1), kColGreen, 8.0f * s);
        dl->AddRect(P(x0, y0), P(x1, y1), kColGreenDk, 8.0f * s, 0, 2.0f * s);

        text(dl, 375, 68, 13, IM_COL32(20, 30, 12, 255), "MODE SELECTOR", 0, true);

        const float cx = 375, cy = 190, R = 42;

        // dial ring
        ImDrawList* d = dl;
        d->AddCircleFilled(P(cx, cy), (R + 24.0f) * s, kColGreenDk, 48);
        d->AddCircleFilled(P(cx, cy), (R + 22.0f) * s, kColGreen, 48);

        knob("mode", kParamMode, 1.0f, 12.0f, cx, cy, R, true, false);

        // numbered arc
        const int mode = modeIndex();
        for (int i = 0; i < 12; ++i)
        {
            const float a = knobAngle((float)i / 11.0f);
            char num[4];
            std::snprintf(num, sizeof(num), "%d", i + 1);
            const bool cur = (i == mode);
            text(dl, cx + std::sin(a) * 58.0f, cy - std::cos(a) * 58.0f - 4.5f,
                 cur ? 12.0f : 10.0f, cur ? kColWhite : IM_COL32(215, 228, 200, 255),
                 num, 0, cur);
        }

        text(dl, 310, 96, 9.5f, IM_COL32(20, 30, 12, 255), "REPEAT", -1, true);
        text(dl, 440, 96, 9.5f, IM_COL32(20, 30, 12, 255), "REVERB", 1, true);
        text(dl, 440, 107, 9.5f, IM_COL32(20, 30, 12, 255), "ECHO", 1, true);

        // Keep the selected routing legible at a glance, including the longer
        // combined-head names, without competing with the numbered dial.
        dl->AddRectFilled(P(304, 262), P(446, 294),
                          IM_COL32(35, 52, 25, 150), 5.0f * s);
        dl->AddRect(P(304, 262), P(446, 294),
                    IM_COL32(111, 143, 85, 180), 5.0f * s, 0, 1.0f * s);
        text(dl, 375, 270, 14.5f, kColWhite, kModeShort[mode], 0, true);
    }

    void drawControlPanel(ImDrawList* dl)
    {
        const float x0 = 472, y0 = 58, x1 = 832, y1 = 302;
        dl->AddRectFilled(P(x0 - 3, y0 - 3), P(x1 + 3, y1 + 3), kColMetal, 8.0f * s);
        dl->AddRectFilled(P(x0, y0), P(x1, y1), kColRecess, 6.0f * s);

        knobLabel(dl, 516, 70, "BASS");
        knob("bass", kParamBass, -1.0f, 1.0f, 516, 124, 21);
        knobLabel(dl, 596, 70, "TREBLE");
        knob("treble", kParamTreble, -1.0f, 1.0f, 596, 124, 21);
        knobLabel(dl, 688, 70, "REVERB", "VOLUME");
        knob("reverbvol", kParamReverbLevel, 0.0f, 1.0f, 688, 124, 21);
        knobLabel(dl, 786, 76, "REVERB PAN");
        knob("reverbpan", kParamReverbPan, 0.0f, 1.0f, 786, 124, 16);

        // Digital routing utilities. The first two latch; SPLICE is a
        // momentary trigger that immediately returns to its off state.
        const auto toggle = [&](const char* id, uint32_t param,
                                float bx0, float bx1, const char* label)
        {
            const bool on = values[param] >= 0.5f;
            const ImVec2 b0 = P(bx0, 158), b1 = P(bx1, 178);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            if (ImGui::IsItemClicked())
            {
                const float next = on ? 0.0f : 1.0f;
                editParameter(param, true);
                values[param] = next;
                setParameterValue(param, next);
                editParameter(param, false);
            }
            dl->AddRectFilled(b0, b1, IM_COL32(38, 38, 41, 255), 3.0f * s);
            dl->AddRect(b0, b1, on ? kColLedOn : IM_COL32(88, 88, 92, 255),
                        3.0f * s, 0, 1.2f * s);
            led(dl, bx0 + 8.0f, 168, on, 2.4f);
            text(dl, 0.5f * (bx0 + bx1) + 4.0f, 162, 8.5f,
                 on ? kColWhite : kColWhiteDim, label, 0, on);
        };
        toggle("inputsend", kParamInputSend, 486, 588, "INPUT SEND");
        toggle("wetsolo", kParamWetSolo, 596, 684, "WET SOLO");

        {
            const ImVec2 b0 = P(692, 158), b1 = P(816, 178);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton(
                "loopsplice", ImVec2(b1.x - b0.x, b1.y - b0.y));
            const bool pressed = ImGui::IsItemActive();
            if (ImGui::IsItemClicked())
            {
                editParameter(kParamLoopSplice, true);
                setParameterValue(kParamLoopSplice, 1.0f);
                setParameterValue(kParamLoopSplice, 0.0f);
                editParameter(kParamLoopSplice, false);
            }
            dl->AddRectFilled(b0, b1,
                pressed ? IM_COL32(75, 38, 34, 255)
                        : IM_COL32(38, 38, 41, 255), 3.0f * s);
            dl->AddRect(b0, b1,
                pressed ? kColLedOn : IM_COL32(88, 88, 92, 255),
                3.0f * s, 0, 1.2f * s);
            text(dl, 754, 162, 8.5f,
                 pressed ? kColWhite : kColWhiteDim,
                 "LOOP SPLICE", 0, pressed);
        }

        knobLabel(dl, 516, 188, "REPEAT RATE");
        const bool sync = values[kParamTempoSync] > 0.5f;
        if (sync) // knob steps through note divisions while synced
            knob("rate", kParamSyncDivision, 0.0f, (float)(kNumSyncDivisions - 1),
                 516, 240, 21, true);
        else
            knob("rate", kParamRepeatRate, 0.0f, 1.0f, 516, 240, 21);

        // Tempo sync sits beneath the rate control.
        {
            const ImVec2 b0 = P(482, 274), b1 = P(550, 294);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton("syncbtn", ImVec2(b1.x - b0.x, b1.y - b0.y));
            if (ImGui::IsItemClicked())
            {
                const float nv = sync ? 0.0f : 1.0f;
                editParameter(kParamTempoSync, true);
                values[kParamTempoSync] = nv;
                setParameterValue(kParamTempoSync, nv);
                editParameter(kParamTempoSync, false);
            }
            dl->AddRectFilled(b0, b1, IM_COL32(40, 40, 43, 255), 3.0f * s);
            dl->AddRect(b0, b1, sync ? IM_COL32(200, 60, 45, 255)
                                     : IM_COL32(90, 90, 94, 255), 3.0f * s, 0, 1.4f * s);
            if (sync)
                dl->AddCircleFilled(P(490, 284), 2.6f * s, kColLedOn, 12);
            text(dl, 519, 278, 9.0f, sync ? kColWhite : kColWhiteDim,
                 sync ? kSyncDivisions[divIndex()].name : "SYNC", 0, sync);
        }
        knobLabel(dl, 596, 188, "INTENSITY");
        knob("intensity", kParamIntensity, 0.0f, 1.0f, 596, 240, 21);
        knobLabel(dl, 688, 188, "ECHO", "VOLUME");
        knob("echovol", kParamEchoLevel, 0.0f, 1.0f, 688, 240, 21);
        knobLabel(dl, 786, 194, "ECHO PAN");
        knob("echopan", kParamEchoPan, 0.0f, 1.0f, 786, 240, 16);

        drawHeadTimingStrip(dl, sync);
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
        constexpr float kCellX[4] = { 558.0f, 644.0f, 730.0f, 816.0f };
        constexpr const char* kLabels[3] = { "HEAD 1", "HEAD 2", "HEAD 3" };
        constexpr uint8_t kHeadMask[12] =
            { 1, 2, 4, 6, 1, 2, 4, 3, 6, 5, 7, 0 };
        const uint8_t activeMask = kHeadMask[modeIndex()];

        dl->AddRectFilled(P(kCellX[0], 270), P(kCellX[3], 298),
                          IM_COL32(35, 35, 38, 255), 3.0f * s);
        dl->AddRect(P(kCellX[0], 270), P(kCellX[3], 298),
                    IM_COL32(82, 82, 86, 255), 3.0f * s, 0, 1.0f * s);

        for (int i = 0; i < 3; ++i)
        {
            const bool active = (activeMask & (1u << i)) != 0;
            const float cx = 0.5f * (kCellX[i] + kCellX[i + 1]);
            if (active)
                dl->AddRectFilled(
                    P(kCellX[i] + 1.0f, 271),
                    P(kCellX[i + 1] - 1.0f, 297),
                    IM_COL32(45, 57, 39, 255), 2.0f * s);
            if (i > 0)
                dl->AddLine(P(kCellX[i], 272), P(kCellX[i], 296),
                            IM_COL32(74, 74, 78, 255), 1.0f * s);

            text(dl, cx, 272, 7.5f,
                 active ? kColWhiteDim : IM_COL32(135, 134, 129, 255),
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
            text(dl, cx, 283, 9.5f,
                 active ? kColWhite : kColWhiteDim,
                 valueText, 0, true);
        }
    }

    void drawPowerBlock(ImDrawList* dl)
    {
        // worn-transport character, above the power switch
        knobLabel(dl, 866, 58, "TAPE AGE");
        knob("tapeage", kParamTapeAge, 0.0f, 1.0f, 866, 110, 18);

        const bool on = values[kParamBypass] < 0.5f;

        text(dl, 866, 150, 9.5f, kColWhite, "POWER", 0, true);
        led(dl, 866, 180, on, 7.0f);

        // clickable toggle: flips the host-designated bypass parameter
        const ImVec2 hit0 = P(850, 200);
        const ImVec2 hit1 = P(882, 276);
        ImGui::SetCursorScreenPos(hit0);
        ImGui::InvisibleButton("power", ImVec2(hit1.x - hit0.x, hit1.y - hit0.y));
        if (ImGui::IsItemClicked())
        {
            const float nv = on ? 1.0f : 0.0f; // toggle bypass
            editParameter(kParamBypass, true);
            values[kParamBypass] = nv;
            setParameterValue(kParamBypass, nv);
            editParameter(kParamBypass, false);
        }

        text(dl, 866, 207, 9, on ? kColWhite : kColWhiteDim, "ON", 0);
        dl->AddRectFilled(P(858, 222), P(874, 262), IM_COL32(55, 55, 58, 255), 3.0f * s);
        if (on) // lever up
        {
            dl->AddCircleFilled(P(866, 252), 7.0f * s, IM_COL32(160, 160, 164, 255), 24);
            dl->AddRectFilled(P(863, 226), P(869, 252), IM_COL32(190, 190, 194, 255), 2.0f * s);
            dl->AddCircleFilled(P(866, 228), 4.0f * s, IM_COL32(210, 210, 214, 255), 16);
        }
        else    // lever down
        {
            dl->AddCircleFilled(P(866, 232), 7.0f * s, IM_COL32(160, 160, 164, 255), 24);
            dl->AddRectFilled(P(863, 232), P(869, 258), IM_COL32(190, 190, 194, 255), 2.0f * s);
            dl->AddCircleFilled(P(866, 256), 4.0f * s, IM_COL32(210, 210, 214, 255), 16);
        }
        text(dl, 866, 268, 9, on ? kColWhiteDim : kColWhite, "OFF", 0);
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
    ImFont* labelFont = nullptr;
    float  values[kParamCount] = {};
    float  needlePos = 0.0f;
    float  meterLevel = 0.0f;
    int    currentPreset = -1;
    bool   showSupporters = false;
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

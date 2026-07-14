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
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

START_NAMESPACE_DISTRHO

namespace {
    constexpr float kDesignW = 800.0f;
    constexpr float kDesignH = 470.0f;

    // Warmed painted-steel greys (a touch of warmth vs. the old cool neutral).
    constexpr ImU32 kColPanel   = IM_COL32(190, 188, 183, 255);
    constexpr ImU32 kColPanelHi = IM_COL32(214, 212, 206, 255);
    constexpr ImU32 kColPanelLo = IM_COL32(150, 148, 143, 255);
    constexpr ImU32 kColFrame   = IM_COL32(60, 58, 55, 255);
    constexpr ImU32 kColInk     = IM_COL32(34, 33, 31, 255);
    constexpr ImU32 kColInkDim  = IM_COL32(98, 96, 92, 255);
    constexpr ImU32 kColScrew   = IM_COL32(126, 123, 118, 255);
    constexpr ImU32 kColRed      = IM_COL32(190, 55, 40, 255);
}

class TapeMachineUI : public UI, public duskdpf::ParamHost
{
public:
    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override
    {
        // GAIN LINK: while the OUTPUT knob is driven in the EFFECTIVE domain
        // (needle/readout show -input + trim), every host write it makes must be
        // converted back to the stored TRIM (host param = clamp(effective -
        // linkOffset)). Doing it here — the single point every gesture funnels
        // through — means the host/DSP NEVER momentarily sees the effective value,
        // so there is no double-write and no audible glitch, and it needs no change
        // to the shared knob widget. Only active around the OUTPUT knob call.
        if (outLinkActive_ && idx == kParamOutputGain)
        {
            const TmParam& d = kTmParams[kParamOutputGain];
            v -= outLinkOffset_;
            v = v < d.min ? d.min : (v > d.max ? d.max : v);
        }
        setParameterValue(idx, v);
    }

    TapeMachineUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kTmParams[i].def;
        setGeometryConstraints(400, 260, true);   // aspect-locked

        // Bake the bold face at several native sizes (design px * DPI scaleFactor)
        // so body text and headers each render from a near-1x face (crisp, not an
        // upscaled single size). pickFont() chooses the nearest per draw.
        static const float kFontSizes[] = { 8.5f, 10.f, 11.f, 13.f, 16.f, 22.f };
        fontSet   = duskdpf::loadCrispFontSet(kFontSizes, 6, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
        // DECISION (not an oversight): the atlas is baked once, here, at the ctor-time
        // scaleFactor. A runtime DPI-scale change (dragging the window between a Retina
        // and non-Retina display mid-session) is intentionally NOT rebaked - it is rare
        // and fails soft (text blurry until the editor is reopened; nothing strands).
        // Bounded fix if ever needed: hook the wrapper's scale-change callback and
        // rebuild the CrispFontSet.

        duskdpf::Palette pal;
        pal.white    = kColInk;
        pal.whiteDim = IM_COL32(70, 68, 64, 255);   // darker than kColInkDim so knob readouts stay legible on the gray panel
        panel.setPalette(pal);

        scanUserPresets();

        // Start the PEAK-lamp hold timers cleared: a host that hides (not destroys)
        // the editor would otherwise freeze a mid-hold value and show a stale lit lamp
        // on reopen. No DPF show/visibility hook exists here, so ctor-init is the guard.
        clipHoldL = clipHoldR = 0.0f;
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < kParamCount)
        {
            values[index] = value;
            if (index < kParamVuL) { currentPreset = -1; currentUserName.clear(); } // edit deselects preset
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
        // When the Advanced panel is open it acts as a modal: disable ALL background widgets so
        // a click outside the card lands on the scrim (which dismisses) and never falls through
        // to a preset combo / bypass / selector / knob. BeginDisabled only suppresses ImGui item
        // input — the manual dl-> draws (knobs, meters, text use explicit colours) are unaffected,
        // so the background still renders normally under the scrim's dim. drawAdvanced runs OUTSIDE
        // the disabled scope so the card, scrim and EQ knobs stay live.
        if (showAdvanced) ImGui::BeginDisabled();
        drawHeader(dl);
        // The PEAK lamp is a digital-CLIP indicator: peakLevel(ch) reads the DSP's final-OUTPUT
        // sample-peak hold, and the lamp lights when the output crosses 0 dBFS (see drawVU),
        // auto-holds 1.5 s after the last over, and click-clears. Tape soft-saturates, so
        // driving it for crunch does not trip it — only a genuine output over does.
        const bool clipEnabled = true;
        const ImU32 accent = accentCol();
        drawVU(dl, 68,  62, 388, 198, meterLevel(0), peakLevel(0), needleL, clipHoldL, clipEnabled, accent, "L");
        drawVU(dl, 412, 62, 732, 198, meterLevel(1), peakLevel(1), needleR, clipHoldR, clipEnabled, accent, "R");
        drawSelectors(dl);
        drawControls(dl);
        if (showAdvanced) ImGui::EndDisabled();
        if (showAdvanced) drawAdvanced(dl);

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    ImVec2 P(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }
    void text(ImDrawList* dl, float x, float y, float sz, ImU32 c, const char* t, int align, bool bold = false)
    { panel.text(dl, x, y, sz, c, t, align, bold); }

    float meterLevel(int ch)
    {
        const bool out = meterSource != 0;
        float v = values[ch == 0 ? kParamVuL : kParamVuR]; // output params (generic-UI fallback)
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (void* const inst = getPluginInstancePointer())
        {
            if (out) { if (tapeMachineGetVuL)   v = ch == 0 ? tapeMachineGetVuL(inst)   : tapeMachineGetVuR(inst); }
            else     { if (tapeMachineGetInVuL) v = ch == 0 ? tapeMachineGetInVuL(inst) : tapeMachineGetInVuR(inst); }
        }
       #endif
        return v;
    }

    // OUTPUT-node true sample-peak for the PEAK lamp — the final signal the host receives.
    // The lamp is a genuine digital-clip indicator (lights when the OUTPUT crosses 0 dBFS in
    // drawVU). Tape soft-saturates, so hitting the tape harder for crunch does NOT trip it;
    // only a real output over does. (Previously read the INPUT record node, which lit on any
    // hot source + positive input gain even though nothing was clipping.)
    float peakLevel(int ch)
    {
        float v = 0.0f;
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (void* const inst = getPluginInstancePointer())
            if (tapeMachineGetOutPeakL)
                v = ch == 0 ? tapeMachineGetOutPeakL(inst) : tapeMachineGetOutPeakR(inst);
       #endif
        return v;
    }

    void setP(uint32_t id, float v)
    {
        values[id] = v;
        editParameter(id, true); setParameterValue(id, v); editParameter(id, false);
    }

    void applyPreset(int idx)
    {
        currentPreset = idx;
        currentUserName.clear();
        tmApplyPreset(idx, [this](uint32_t id, float v) { setP(id, v); });
    }

    //--- user preset file library (~/.config/DuskAudio/TapeMachine2/presets) ---
    std::string configDir() const
    {
        const char* home = std::getenv("HOME");
        return std::string(home ? home : ".") + "/.config/DuskAudio/TapeMachine2/presets";
    }

    void scanUserPresets()
    {
        userPresets.clear();
        namespace fs = std::filesystem;
        std::error_code ec;
        for (fs::directory_iterator it(configDir(), ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->path().extension() != ".tmpreset") continue;
            std::ifstream f(it->path());
            std::string line, name;
            if (std::getline(f, line) && line.rfind("name=", 0) == 0) name = line.substr(5);
            else name = it->path().stem().string();
            userPresets.push_back({ name, it->path().string() });
        }
        std::sort(userPresets.begin(), userPresets.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    void saveUserPreset(const char* rawName)
    {
        std::string name(rawName);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (name.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(configDir(), ec);
        std::string fn;
        for (char c : name) fn += std::isalnum((unsigned char)c) ? c : '_';
        std::ofstream f(configDir() + "/" + fn + ".tmpreset", std::ios::trunc);
        if (!f) return;
        f << "name=" << name << "\n";
        for (uint32_t i = 0; i < kParamVuL; ++i)
            if (i != kParamBypass) f << kTmParams[i].id << "=" << values[i] << "\n";
        f.close();
        scanUserPresets();
        currentPreset = -1;
        currentUserName = name;
    }

    void loadUserPreset(const std::string& path, const std::string& name)
    {
        std::ifstream f(path);
        if (!f) return;
        std::string line;
        while (std::getline(f, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            if (key == "name") continue;
            const float v = (float) std::atof(line.c_str() + eq + 1);
            for (uint32_t i = 0; i < kParamVuL; ++i)
                if (key == kTmParams[i].id) { setP(i, v); break; }
        }
        currentPreset = -1;
        currentUserName = name;
    }

    void drawPanel(ImDrawList* dl, float winW, float winH)
    {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), kColFrame);
        // main panel: top-down painted-steel gradient (light top -> darker bottom)
        dl->AddRectFilledMultiColor(P(6, 6), P(kDesignW - 6, kDesignH - 6),
                                    kColPanelHi, kColPanelHi, kColPanelLo, kColPanelLo);
        // fine horizontal brushed grain: alternating light/dark hairlines
        for (float y = 8.0f; y < kDesignH - 6.0f; y += 2.0f)
        {
            dl->AddLine(P(8, y),        P(kDesignW - 8, y),        IM_COL32(255, 255, 255, 9), 1.0f);
            dl->AddLine(P(8, y + 1.0f), P(kDesignW - 8, y + 1.0f), IM_COL32(0, 0, 0, 7), 1.0f);
        }
        // bevel: bright top-left edge, dark bottom-right edge
        dl->AddLine(P(6, 7), P(kDesignW - 6, 7), IM_COL32(235, 234, 228, 130), 1.4f * s);
        dl->AddLine(P(6, kDesignH - 7), P(kDesignW - 6, kDesignH - 7), IM_COL32(40, 39, 37, 90), 1.4f * s);
        const float sx[4] = { 16, kDesignW - 16, 16, kDesignW - 16 };
        const float sy[4] = { 16, 16, kDesignH - 16, kDesignH - 16 };
        for (int i = 0; i < 4; ++i) screw(dl, sx[i], sy[i]);
    }

    void screw(ImDrawList* dl, float x, float y) const
    {
        const ImVec2 c = P(x, y);
        dl->AddCircleFilled(c, 6.2f * s, IM_COL32(70, 68, 64, 90), 20);            // recessed shadow
        dl->AddCircleFilled(c, 5.0f * s, kColScrew, 20);                           // head
        dl->AddCircleFilled(ImVec2(c.x - 1.2f * s, c.y - 1.4f * s), 4.3f * s,
                            IM_COL32(198, 195, 188, 150), 20);                     // bevel highlight
        dl->AddCircleFilled(c, 3.6f * s, kColScrew, 20);
        dl->AddCircle(c, 5.0f * s, IM_COL32(72, 70, 66, 255), 20, 1.0f * s);       // rim
        const float a = (x < kDesignW * 0.5f) ? 0.5f : -0.5f;                      // slot angle
        const ImVec2 d(std::cos(a), std::sin(a));
        dl->AddLine(ImVec2(c.x - d.x * 3.2f * s, c.y - d.y * 3.2f * s),
                    ImVec2(c.x + d.x * 3.2f * s, c.y + d.y * 3.2f * s),
                    IM_COL32(56, 54, 50, 255), 1.3f * s);
    }

    // small silver chevron button (< or >) for preset stepping
    bool chevron(ImDrawList* dl, const char* id, float cx, float cy, bool left)
    {
        const ImVec2 b0 = P(cx - 9, cy - 10), b1 = P(cx + 9, cy + 10);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hov ? IM_COL32(214, 214, 216, 255) : IM_COL32(224, 224, 226, 255), 2.0f * s);
        dl->AddRect(b0, b1, IM_COL32(120, 121, 123, 255), 2.0f * s, 0, 1.0f * s);
        const ImVec2 c = P(cx, cy); const float d = 4.0f * s;
        if (left) dl->AddTriangleFilled(ImVec2(c.x + d * 0.5f, c.y - d), ImVec2(c.x + d * 0.5f, c.y + d), ImVec2(c.x - d * 0.7f, c.y), kColInk);
        else      dl->AddTriangleFilled(ImVec2(c.x - d * 0.5f, c.y - d), ImVec2(c.x - d * 0.5f, c.y + d), ImVec2(c.x + d * 0.7f, c.y), kColInk);
        return ImGui::IsItemClicked();
    }

    // small silver text button
    bool textButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                    const char* label, bool active = false)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        // Latched/active reuses the engaged tmButton cap look (recessed fill, warm
        // border, red ink) so an open toggle reads identically to an engaged BYPASS.
        if (active)
            dl->AddRectFilledMultiColor(b0, b1, IM_COL32(188, 186, 188, 255), IM_COL32(188, 186, 188, 255),
                                        IM_COL32(214, 212, 212, 255), IM_COL32(214, 212, 212, 255));
        else
            dl->AddRectFilledMultiColor(b0, b1, IM_COL32(226, 226, 228, 255), IM_COL32(226, 226, 228, 255),
                                        IM_COL32(186, 186, 188, 255), IM_COL32(186, 186, 188, 255));
        dl->AddRect(b0, b1, active ? IM_COL32(150, 84, 74, 255) : IM_COL32(90, 90, 92, 255),
                    2.0f * s, 0, active ? 1.4f * s : 1.2f * s);
        text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 9.5f, active ? kColRed : kColInk, label, 0, true);
        return ImGui::IsItemClicked();
    }

    void initDefaults()
    {
        currentPreset = -1;
        for (uint32_t i = 0; i < kParamVuL; ++i)
        {
            if (i == kParamBypass) continue;   // don't fight host bypass on Init
            const float d = kTmParams[i].def;
            values[i] = d;
            editParameter(i, true); setParameterValue(i, d); editParameter(i, false);
        }
    }

    void stepPreset(int dir)
    {
        int i = currentPreset < 0 ? (dir < 0 ? 0 : -1) : currentPreset;
        i += dir;
        if (i < 0) i = 0; if (i >= kNumTmPresets) i = kNumTmPresets - 1;
        applyPreset(i);
    }

    // Category label with the deck-name prefix stripped, so the in-column sub-headers read
    // as the purpose only ("Master"/"Color"/"Mix"/"Lo-Fi") beneath the deck heading.
    static const char* presetPurpose(const char* category)
    {
        if (std::strncmp(category, "Classic 102 ", 12) == 0) return category + 12;
        if (std::strncmp(category, "Swiss 800 ",   10) == 0) return category + 10;
        return category;   // "Lo-Fi"
    }

    // One deck's column inside the preset popup: an accent-tinted deck heading, then every
    // factory preset whose tapeMachine == machine (table order preserved), grouped by purpose
    // sub-header. Fixed-width AutoResizeY child so SeparatorText spans a stable width and the
    // parent popup auto-fits height (no scrollbar). Selectable click loads + closes the popup.
    void presetColumn(int machine, const char* title, ImU32 accent, float colW)
    {
        ImGui::BeginChild(title, ImVec2(colW, 0.0f), ImGuiChildFlags_AutoResizeY);
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::SeparatorText(title);
        ImGui::PopStyleColor();
        const char* lastPurpose = nullptr;
        for (int i = 0; i < kNumTmPresets; ++i)
        {
            if (kTmPresets[i].tapeMachine != machine) continue;
            const char* purpose = presetPurpose(kTmPresets[i].category);
            if (lastPurpose == nullptr || std::strcmp(lastPurpose, purpose) != 0)
            {
                lastPurpose = purpose;
                ImGui::SeparatorText(purpose);
            }
            if (ImGui::Selectable(kTmPresets[i].name, i == currentPreset))
            {
                applyPreset(i);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
    }

    void drawHeader(ImDrawList* dl)
    {
        // Title nameplate + machine badge (left-anchored, clear of the top-left screw ~x22).
        dl->AddRectFilled(P(30, 12), P(247, 40), IM_COL32(30, 30, 32, 255), 3.0f * s);
        dl->AddRect(P(30, 12), P(247, 40), IM_COL32(120, 120, 122, 255), 3.0f * s, 0, 1.4f * s);
        text(dl, 42, 17, 15, IM_COL32(232, 232, 228, 255), "TapeMachine 2", -1, true);
        {   // machine badge (accent-tinted). Options are hard-gated per machine, so
            // no non-standard state exists to flag.
            const char* badge = isA800() ? "Swiss 800" : "Classic 102";
            dl->AddRectFilled(P(193, 16), P(243, 34), accentCol(), 3.0f * s);
            dl->AddRect(P(193, 16), P(243, 34), IM_COL32(0, 0, 0, 90), 3.0f * s, 0, 1.0f * s);
            text(dl, 218, 20, 8.5f, IM_COL32(18, 18, 20, 255), badge, 0, true);
        }

        // preset browser, centred between the title and the (right-anchored) bypass:
        //   < [combo] >  INIT  SAVE          (all on the 18..38 band)
        const char* cur = currentPreset >= 0 ? kTmPresets[currentPreset].name
                        : (!currentUserName.empty() ? currentUserName.c_str() : "Presets...");
        if (chevron(dl, "##pv", 298, 28, true)) stepPreset(-1);
        ImGui::SetCursorScreenPos(P(311, 18));
        ImGui::SetNextItemWidth(200.0f * s);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(232, 232, 232, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(244, 244, 244, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(238, 238, 238, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(150, 152, 156, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(176, 178, 182, 255)); // silver hover (matches Header, not ImGui default blue)
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(150, 152, 156, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(198, 199, 201, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(212, 213, 215, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(178, 179, 181, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColInk);
        // Uncapping the combo popup height (BeginComboPopup defaults to an 8-item cap UNLESS
        // a size constraint is already set — imgui_widgets.cpp BeginComboPopup: its
        // HasSizeConstraint branch skips the CalcMaxPopupHeightFromItemCount cap) makes ImGui
        // auto-fit the popup to ALL its content with no scrollbar; the two fixed-width machine
        // columns below then show every factory preset at once. The identity range
        // (0..FLT_MAX) imposes NO real size limit — it exists solely to raise the
        // HasSizeConstraint flag so that cap branch is taken.
        //
        // SAFE, NO LEAK — verified against Dear ImGui 1.91.9b (imgui_widgets.cpp ~1854-1924).
        // This is unconditional, before BeginCombo, and cannot bleed into the next Begin()-ed
        // window whether or not the popup opens: BeginCombo consumes NextWindowData exactly
        // like Begin() — it snapshots HasFlags, calls ClearFlags() at its very top, and only
        // RESTORES the snapshot when the popup is open (right before BeginComboPopup). When the
        // popup is closed BeginCombo returns with the flags already cleared, so the constraint
        // is discarded, not inherited. A pre-BeginCombo IsPopupOpen() gate would be both
        // unnecessary and worse here: the combo popup id is the internal
        // ImHashStr("##ComboPopup", id) (needs imgui_internal.h), and on the click frame the
        // popup opens INSIDE BeginCombo, so a pre-check would miss it and flash the 8-item cap
        // for one frame. So this stays unconditional by design, not by accident.
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::BeginCombo("##preset", cur))
        {
            // Column width sized to the widest factory preset name so both columns align
            // and nothing truncates.
            const ImGuiStyle& st = ImGui::GetStyle();
            float nameW = 0.0f;
            for (int i = 0; i < kNumTmPresets; ++i)
                nameW = std::max(nameW, ImGui::CalcTextSize(kTmPresets[i].name).x);
            const float colW = nameW + st.WindowPadding.x * 2.0f + st.FramePadding.x * 2.0f + 8.0f * s;

            // Two columns, one per deck (table order preserved within each), accent-tinted
            // to match each machine's badge. 10 factory presets per deck -> all 20 visible.
            presetColumn(1, "CLASSIC 102", IM_COL32(176, 116, 70, 255), colW);   // warm copper deck
            ImGui::SameLine(0.0f, 14.0f * s);
            presetColumn(0, "SWISS 800",   IM_COL32(96, 120, 150, 255), colW);   // cool blue deck

            if (!userPresets.empty())
            {
                ImGui::SeparatorText("User");
                for (const auto& up : userPresets)
                    if (ImGui::Selectable(up.first.c_str(), currentPreset < 0 && up.first == currentUserName))
                    { loadUserPreset(up.second, up.first); ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(10);
        if (chevron(dl, "##nx", 524, 28, false)) stepPreset(+1);
        if (textButton(dl, "##init", 545, 18, 581, 38, "INIT")) initDefaults();
        if (textButton(dl, "##save", 587, 18, 623, 38, "SAVE"))
        {
            std::snprintf(saveBuf, sizeof(saveBuf), "%s", currentUserName.c_str());
            ImGui::OpenPopup("Save Preset");
        }
        drawSaveModal();

        // bypass, tucked closer to INIT/SAVE (SAVE ends ~623) while staying clear
        // of the top-right corner screw (~x784).
        tmButton(dl, "bypass", kParamBypass, 628, 18, 704, 38, "BYPASS");
        // ADVANCED: reveals the hidden back panel — Repro EQ + (Classic 102) transport
        // electronics. Wider cell than the old "EQ" for the longer label; still clears the
        // top-right corner screw (centre ~x784, head starts ~x779).
        // Toggle: opens the modal, latches active (engaged-cap styling) while open,
        // and closes it when clicked again. The scrim also dismisses on an outside click.
        if (textButton(dl, "##adv", 710, 18, 776, 38, "ADVANCED", showAdvanced)) showAdvanced = !showAdvanced;
    }

    // Meter source IN|OUT switch. Removed from the UI by request; kept intact (the
    // meterSource state + input-meter DSP path stay wired) so it can be reinstated.
    void drawMeterSwitch(ImDrawList* dl)
    {
        const float mx0 = 576, mid = 610, mx1 = 644, my0 = 18, my1 = 38;
        dl->AddRectFilled(P(mx0, my0), P(mx1, my1), IM_COL32(210, 211, 213, 255), 3.0f * s);
        ImGui::SetCursorScreenPos(P(mx0, my0));
        ImGui::InvisibleButton("mIn", ImVec2((mid - mx0) * s, (my1 - my0) * s));
        if (ImGui::IsItemClicked()) meterSource = 0;
        const bool inAct = meterSource == 0;
        if (inAct) dl->AddRectFilled(P(mx0, my0), P(mid, my1), IM_COL32(150, 152, 156, 255), 3.0f * s);
        text(dl, 0.5f * (mx0 + mid), 22, 9.0f, inAct ? IM_COL32(22, 22, 24, 255) : kColInkDim, "IN", 0, inAct);
        ImGui::SetCursorScreenPos(P(mid, my0));
        ImGui::InvisibleButton("mOut", ImVec2((mx1 - mid) * s, (my1 - my0) * s));
        if (ImGui::IsItemClicked()) meterSource = 1;
        const bool outAct = meterSource != 0;
        if (outAct) dl->AddRectFilled(P(mid, my0), P(mx1, my1), IM_COL32(150, 152, 156, 255), 3.0f * s);
        text(dl, 0.5f * (mid + mx1), 22, 9.0f, outAct ? IM_COL32(22, 22, 24, 255) : kColInkDim, "OUT", 0, outAct);
        dl->AddLine(P(mid, my0 + 1), P(mid, my1 - 1), IM_COL32(120, 121, 123, 255), 1.0f * s);
        dl->AddRect(P(mx0, my0), P(mx1, my1), IM_COL32(90, 90, 92, 255), 3.0f * s, 0, 1.2f * s);
    }

    // Hidden advanced panel (UAD-style back panel): the Repro-head EQ trims plus (on the
    // Classic 102) the transport-electronics switches, kept off the main face. A full-canvas
    // scrim swallows clicks to the controls behind and closes on an OUTSIDE click.
    void drawAdvanced(ImDrawList* dl)
    {
        // Classic 102 hosts the transport-electronics toggles below the EQ, so its card
        // grows downward; the Swiss 800 has no such switches and keeps the shorter
        // EQ-only card with the EQ centred as before.
        const bool classic = !isA800();
        // Card ends just below its last content: the EQ readouts (Swiss 800) or the
        // CROSSTALK/XFMR row (Classic 102). No CLOSE button — the scrim dismisses.
        const float cardBot = classic ? 426.0f : 356.0f;

        // scrim: dim the main panel + catch OUTSIDE clicks to dismiss. It covers the whole
        // canvas and is submitted FIRST, so it must opt into overlap: SetNextItemAllowOverlap()
        // sets HoveredIdAllowOverlap, which lets the later-submitted card / knobs / toggles win
        // the hit-test in their own rects (without it, ImGui's ItemHoverable rejects any later
        // item once the full-canvas scrim has claimed HoveredId — so EVERY press, even on a
        // knob, was landing on the scrim and closing the modal: the reported bug). A drag that
        // STARTS on a control keeps that control as ActiveId, so releasing over the scrim never
        // fires it. And to keep an accidental drag that starts on the scrim from closing, only a
        // clean press+release with negligible travel (< 4 px) dismisses.
        dl->AddRectFilled(P(0, 0), P(kDesignW, kDesignH), IM_COL32(8, 8, 10, 170));
        ImGui::SetCursorScreenPos(P(0, 0));
        ImGui::SetNextItemAllowOverlap();
        const bool scrimClicked = ImGui::InvisibleButton("##advscrim", ImVec2(kDesignW * s, kDesignH * s));
        if (ImGui::IsItemActivated())
            advScrimPressPos = ImGui::GetIO().MousePos;   // remember where the scrim press began
        if (scrimClicked)
        {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const float dx = mp.x - advScrimPressPos.x, dy = mp.y - advScrimPressPos.y;
            const float tol = 4.0f * s;
            if (dx * dx + dy * dy <= tol * tol)
                showAdvanced = false;                     // clean outside click -> dismiss
        }

        // card click-catcher: consumes clicks on the card body (titles, labels, gaps between
        // knobs) so they DON'T fall through to the scrim and dismiss the panel. Also opts into
        // overlap so the knobs / toggles submitted AFTER it (and inside its rect) still win.
        ImGui::SetCursorScreenPos(P(212, 150));
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##advcard", ImVec2((588 - 212) * s, (cardBot - 150) * s));

        // card
        dl->AddRectFilled(P(212, 150), P(588, cardBot), IM_COL32(34, 34, 37, 255), 5.0f * s);
        dl->AddRect(P(212, 150), P(588, cardBot), accentCol(), 5.0f * s, 0, 1.6f * s);

        // Modal title + REPRO EQ section heading. Both drawn bright by hand: sectionHeader()
        // uses dark chassis ink meant for the grey main panel and would vanish on this card.
        text(dl, 400, 162, 13,    IM_COL32(240, 240, 236, 255), "ADVANCED", 0, true);
        text(dl, 400, 196, 10.5f, IM_COL32(228, 228, 231, 255), "REPRO EQ", 0, true);
        text(dl, 400, 211, 8.5f,  IM_COL32(176, 176, 180, 255), "playback-head EQ", 0);

        // knob names + freq drawn bright (the built-in knobLabel reads too dim on the
        // dark card), then the rotary via panel.knob. Its persistent read-out uses
        // pal.whiteDim — a dark grey tuned for the grey main panel, invisible here — so
        // it is disabled (persistent=false) and the value redrawn bright below the knob.
        struct EqK { const char* id; uint32_t p; float cx; const char* name; const char* hz; };
        static const EqK eqk[4] = {
            { "##rlf",  kParamReproLF,  262, "LOW",    "80 Hz"  },
            { "##rlmf", kParamReproLMF, 354, "LO-MID", "160 Hz" },
            { "##rhmf", kParamReproHMF, 446, "HI-MID", "5 kHz"  },
            { "##rhf",  kParamReproHF,  538, "HIGH",   "9 kHz"  },
        };
        for (const EqK& k : eqk)
        {
            text(dl, k.cx, 236, 11.5f, IM_COL32(255, 255, 255, 255), k.name, 0, true);
            text(dl, k.cx, 250, 8.5f,  IM_COL32(205, 205, 208, 255), k.hz,   0);
            const TmParam& d = kTmParams[k.p];
            panel.knob(k.id, k.p, d.min, d.max, k.cx, 296, 30.0f, values[k.p], d.def,
                       false, true, "%+.1f", " dB", 0, false, /*persistent*/ false, nullptr,
                       false, 1.0f, 0.0f, k.name, /*contextMenu*/ true,
                       /*overrideText*/ nullptr, /*hasExternalReadout*/ true);
            char rb[32];
            std::snprintf(rb, sizeof(rb), "%+.1f dB", values[k.p]);
            text(dl, k.cx, 334, 9.5f, IM_COL32(206, 206, 210, 255), rb, 0);   // bright readout
        }

        // Transport electronics — Classic 102 only (the Swiss 800 A800 has none of these
        // output-stage switches, mirroring the main-panel gating). Relocated here from the
        // main toggle row; wired via tmButton exactly as before so look + param handling
        // (edit / setParameterValue / values[] sync) are unchanged.
        if (classic)
        {
            text(dl, 400, 360, 9.5f, IM_COL32(214, 214, 218, 255), "ELECTRONICS", 0, true);
            text(dl, 330, 382, 9.0f, IM_COL32(228, 228, 231, 255), "CROSSTALK", 0, true);
            tmButton(dl, "crosstalk",   kParamCrosstalk,   297, 392, 363, 412, "", true);
            text(dl, 470, 382, 9.0f, IM_COL32(228, 228, 231, 255), "XFMR", 0, true);
            tmButton(dl, "transformer", kParamTransformer, 437, 392, 503, 412, "", true);
        }
    }

    void drawSaveModal()
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(234, 234, 236, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColInk);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(248, 249, 251, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(206, 207, 209, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(218, 219, 221, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(190, 191, 193, 255));
        if (ImGui::BeginPopupModal("Save Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Preset name");
            ImGui::SetNextItemWidth(240.0f * s);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##savename", saveBuf, sizeof(saveBuf),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool doSave = ImGui::Button("Save") || enter;
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel");
            if (doSave && saveBuf[0] != '\0') { saveUserPreset(saveBuf); ImGui::CloseCurrentPopup(); }
            if (cancel) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(6);
    }

    static constexpr float kVuA0 = -2.70f, kVuA1 = -0.44f;

    void drawVU(ImDrawList* dl, float x0, float y0, float x1, float y1,
                float level, float peak, float& needle, float& clipHold, bool clipEnabled,
                ImU32 accent, const char* label)
    {
        // 0 VU = -12 dBFS: the UAD/Swiss 800/Classic 102 spec ("plug-in operates at an internal
        // level of -12 dBFS ... a -12 dBFS input equates to 0 dB on the meters").
        // The DSP now feeds a mean-abs (rectified) value, which reads 2/pi (-3.92 dB) below
        // peak for a sine. So a -12 dBFS sine arrives here as 0.15992 lin, and the offset is
        // 12 + 20*log10(pi/2) = 15.92 dB so that sine still lands exactly on 0 VU.
        const float dB = 20.0f * std::log10(level > 1e-4f ? level : 1e-4f) + 15.92f;
        // deflection is linear in signal level (%), not dB: gives the classic
        // log-spread VU scale (marks bunch at the bottom, open up near 0..+3).
        float target = std::pow(10.0f, (dB - 3.0f) / 20.0f);
        target = target < 0.0f ? 0.0f : (target > 1.0f ? 1.0f : target);
        // The 300 ms ANSI ballistic lives entirely in the DSP integrator now; this UI filter
        // is only a fast cosmetic anti-jitter / frame-interpolation pole (tau ~= 25 ms, well
        // under 30 ms) so it does not add a second, slower time constant to the needle.
        needle += (target - needle) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 40.0f));

        // warm tan bezel -> dark inner lip -> aged-cream face
        dl->AddRectFilledMultiColor(P(x0, y0), P(x1, y1),
            IM_COL32(170, 142, 100, 255), IM_COL32(158, 130, 88, 255),
            IM_COL32(120, 96, 60, 255),  IM_COL32(110, 88, 54, 255));
        dl->AddRect(P(x0, y0), P(x1, y1), IM_COL32(92, 72, 44, 255), 6.0f * s, 0, 1.4f * s);
        dl->AddLine(P(x0 + 6, y0 + 3), P(x1 - 6, y0 + 3), accent, 1.6f * s); // machine accent stripe
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
            ImFont* nf = panel.pickFont(sz * s); if (nf == nullptr) nf = f;   // nearest baked size -> crisp
            ImVec2 ts = nf->CalcTextSizeA(sz * s, FLT_MAX, 0, b);
            ImVec2 tp = pt(r, a);
            dl->AddText(nf, sz * s, ImVec2(tp.x - ts.x * 0.5f, tp.y - ts.y * 0.5f), c, b);
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

        // over lamp (right). A digital-CLIP indicator (peak = the DSP's final-OUTPUT sample-peak
        // hold — the buffer the host receives). Lights when the OUTPUT crosses 0 dBFS (true
        // digital over). Because the tape soft-saturates and the gain-link holds output ~unity,
        // pushing the input for crunch does NOT clip the output, so the lamp stays dark under
        // normal drive and lights only on a genuine over. (Earlier it read the INPUT record node
        // and lit on any hot source + positive input gain, a false alarm the user reported.)
        // `clipHold` is a HOLD TIMER in seconds: every over (re)arms it to 1.5 s, and
        // between overs it counts down by the frame dt, so a brief transient stays lit ~1.5 s
        // after it passes and a sustained hot signal keeps re-arming (stays lit). A click clears
        // it immediately. (Replaces the old indefinite output-clip latch.)
        static constexpr float kPeakThreshLin = 1.0f;          // 0 dBFS in linear amplitude
        static constexpr float kPeakHoldSec   = 1.5f;          // auto-hold after the last over
        if (clipEnabled && peak >= kPeakThreshLin)
            clipHold = kPeakHoldSec;                           // (re)arm on every over
        else if (clipHold > 0.0f)
            clipHold -= ImGui::GetIO().DeltaTime;              // count the hold down between overs
        const bool over = clipEnabled && clipHold > 0.0f;
        const ImVec2 lp = P(fx1 - 15, pivotY - L * 0.20f);
        if (clipEnabled)
        {
            char cid[8]; std::snprintf(cid, sizeof(cid), "clip%s", label);
            ImGui::SetCursorScreenPos(ImVec2(lp.x - 8.0f * s, lp.y - 8.0f * s));
            ImGui::InvisibleButton(cid, ImVec2(16.0f * s, 16.0f * s));
            if (ImGui::IsItemClicked()) clipHold = 0.0f;
        }
        // Lit vs unlit must be unmistakable: LIT = hot red core + bright ring + glow
        // halo; UNLIT = a clearly dark drilled recess. Click the lamp to clear the hold.
        dl->AddCircleFilled(lp, 7.2f * s, IM_COL32(26, 20, 18, 255), 22);              // drilled socket shadow
        if (over) dl->AddCircleFilled(lp, 10.5f * s, IM_COL32(232, 60, 40, 95), 24);   // glow halo
        dl->AddCircleFilled(lp, 5.0f * s,
            over ? IM_COL32(242, 68, 46, 255) : IM_COL32(44, 33, 31, 255), 18);        // lit core / dark recess
        if (over)
            dl->AddCircle(lp, 5.7f * s, IM_COL32(255, 152, 132, 225), 20, 1.2f * s);   // hot rim when lit
        else
            dl->AddCircle(lp, 5.0f * s, IM_COL32(10, 6, 6, 255), 18, 1.2f * s);        // recessed dark rim
        dl->AddCircleFilled(ImVec2(lp.x - 1.4f * s, lp.y - 1.6f * s), 1.6f * s,
                            IM_COL32(255, 205, 185, over ? 235 : 38), 10);             // specular dot
        // small screened label so the click-to-reset lamp reads as PEAK without a tooltip
        text(dl, fx1 - 15, pivotY - L * 0.20f + 8.5f, 6.5f,
             over ? red : IM_COL32(118, 98, 78, 210), "PEAK", 0, true);

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
    // nVisible = how many of the param's choices to expose for the CURRENT machine
    // (hard per-machine gating: only the deck's real options are selectable — no
    // amber "non-standard" state; the dropdown simply omits unavailable options).
    void selector(ImDrawList* dl, const char* id, uint32_t param, float cx, float y,
                  float w, const char* title, int nVisible)
    {
        const TmParam& d = kTmParams[param];
        if (nVisible <= 0 || nVisible > d.numChoices) nVisible = d.numChoices;
        text(dl, cx, y, 9.0f, kColInk, title, 0, true);
        int cur = coerceChoice(param, (int)(values[param] + 0.5f));
        if (cur < 0) cur = 0; if (cur >= nVisible) cur = nVisible - 1;

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
        const bool open = ImGui::BeginCombo(id, d.choices[cur]);   // arrow -> reads as a dropdown
        if (open)
        {
            for (int i = 0; i < nVisible; ++i)
                if (ImGui::Selectable(d.choices[i], i == cur))
                {
                    values[param] = (float)i;
                    editParameter(param, true); setParameterValue(param, (float)i); editParameter(param, false);
                }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(8);
    }

    // Map an option the current machine doesn't offer to its valid effective index
    // for DISPLAY ONLY, mirroring the DSP-side coercion (TapeMachineDSP: Swiss 800 has no
    // 3.75 IPS -> plays as 15). Returns a local value; the stored parameter is left
    // untouched so automating Swiss 800 + 3.75 IPS is preserved, not overwritten with 15
    // on every render / editor reopen. Currently only 3.75 IPS (Swiss 800).
    int coerceChoice(uint32_t param, int idx) const
    {
        if (param == kParamTapeSpeed && isA800() && idx == 3)   // 3.75 IPS: Classic 102 only
            return 1;                                           // -> 15 IPS
        return idx;
    }

    void drawSelectors(ImDrawList* dl)
    {
        const float y = 216.0f;
        const bool atr = !isA800();
        // The selector row reflows per machine: the Classic 102 adds a HEAD WIDTH cell (6
        // cells); the Swiss 800 omits it (5). TAPE SPEED exposes 3.75 IPS on the Classic 102 only.
        // No OVERSAMPLING cell: the OS param is pinned at the tuned 2x core (DSP ignores it),
        // matching the UAD decks which have no OS control (see TapeMachineDSP::factorFromChoice).
        struct Sel { const char* id; uint32_t p; float w; const char* t; int n; };
        Sel cells[6];
        int nc = 0;
        cells[nc++] = { "##machine", kParamTapeMachine,  96.f, "MACHINE",      2 };
        cells[nc++] = { "##speed",   kParamTapeSpeed,    80.f, "TAPE SPEED",   atr ? 4 : 3 };
        cells[nc++] = { "##type",    kParamTapeType,     84.f, "TAPE TYPE",    4 };
        cells[nc++] = { "##path",    kParamSignalPath,   74.f, "SIGNAL PATH",  4 };
        cells[nc++] = { "##eq",      kParamEqStandard,   60.f, "EQ STANDARD",  2 };
        if (atr)
            cells[nc++] = { "##hw",  kParamHeadWidth,    62.f, "HEAD WIDTH",   3 };

        const float x0 = 48.f, x1 = 752.f;   // even spacing across the panel width
        for (int i = 0; i < nc; ++i)
        {
            const float cx = x0 + (x1 - x0) * ((float)i + 0.5f) / (float)nc;
            selector(dl, cells[i].id, cells[i].p, cx, y, cells[i].w, cells[i].t, cells[i].n);
        }
    }

    //--- knob rows -------------------------------------------------------------
    // Persistent value readout, right-click reset. dispAdd shifts the read-out
    // (BIAS shows relative over/under).
    bool knob(ImDrawList* dl, const char* id, uint32_t param, float cx, float cy,
              const char* l1, const char* fmt, const char* suffix,
              float dispAdd = 0.0f, const char* overrideText = nullptr,
              bool linked = false, float linkOffset = 0.0f)
    {
        const TmParam& d = kTmParams[param];
        panel.knobLabel(dl, cx, cy - 50.0f, l1);
        // GAIN LINK path (OUTPUT knob only): the knob shows the EFFECTIVE output
        // (-input + trim) while the STORED param stays the trim. We feed the shared
        // knob a SYNTHETIC effective value; the host adapter (setParam override,
        // gated by outLink*) converts every write it makes back to the trim, so the
        // host/DSP never sees the effective value. As INPUT moves the pointer must
        // physically ROTATE down to show the auto-compensation the DSP applies.
        if (linked)
        {
            // TWO ranges, deliberately different (this is the fix for "output knob doesn't
            // move when I turn input"):
            //  - DRAG range [effLo,effHi] = reachable span = [d.min+linkOffset, d.max+linkOffset].
            //    Clamps the drag domain to what the ±12 trim can actually reach at this input,
            //    so a direct output-knob drag has no rubber-band. This span SLIDES with input.
            //  - DISPLAY range [effDispLo,effDispHi] = FIXED full effective span, independent of
            //    input. The pointer angle maps the effective value against THIS, so turning input
            //    rotates the pointer (a sliding display range would cancel the slide — the old bug).
            //    input at max -> most-negative eff (d.min - inMax); input at min -> most-positive
            //    (d.max - inMin). With both gains ±12 dB this is a fixed [-24,+24].
            const float effLo = d.min + linkOffset;
            const float effHi = d.max + linkOffset;
            const float inMin = kTmParams[kParamInputGain].min;
            const float inMax = kTmParams[kParamInputGain].max;
            const float effDispLo = d.min - inMax;   // input at max  -> lowest effective output
            const float effDispHi = d.max - inMin;   // input at min  -> highest effective output
            float eff = values[param] + linkOffset;                     // effective output
            eff = eff < effLo ? effLo : (eff > effHi ? effHi : eff);    // stays in the reachable span
            outLinkOffset_ = linkOffset;
            outLinkActive_ = true;
            // defaultVal is passed in the EFFECTIVE domain as linkOffset (= -input), so a
            // reset stores trim = clamp(linkOffset - linkOffset) = 0 (the param table
            // default) and the pointer jumps to -input. linkOffset always lies within
            // [effLo,effHi] (trim 0 is always reachable), so the reset target is inside the drag domain.
            const bool ch = panel.knob(id, param, effLo, effHi, cx, cy, 32.0f, eff, linkOffset,
                       false, true, fmt, suffix, 0, false,
                       /*persistent*/ true, nullptr, /*rightClickReset*/ false, 1.0f,
                       /*dispAdd*/ 0.0f, l1, /*contextMenu*/ true, overrideText,
                       /*hasExternalReadout*/ false, /*dispMin*/ effDispLo, /*dispMax*/ effDispHi);
            outLinkActive_ = false;
            // Mirror the trim the adapter just stored, but ONLY on a real edit (ch) — when
            // merely displaying a clamped effective we must not destructively re-clamp the
            // preserved stored trim. ch is airtight: the shared knob reports changed only on
            // an actual gesture (drag delta, wheel, reset, type-in), never on an idle frame,
            // so no held-but-still frame re-fires the mirror.
            if (ch)
            {
                float trim = eff - linkOffset;
                trim = trim < d.min ? d.min : (trim > d.max ? d.max : trim);
                values[param] = trim;
            }
            return ch;
        }
        // Interaction is owned entirely by the shared knob widget so every knob
        // behaves identically: drag / shift-fine / wheel (shift-finer) / double-click
        // type-in / Alt- or Cmd-click reset / right-click context menu. No tooltip.
        return panel.knob(id, param, d.min, d.max, cx, cy, 32.0f, values[param], d.def,
                   false, true, fmt, suffix, 0, false,
                   /*persistent*/ true, nullptr, /*rightClickReset*/ false, 1.0f, dispAdd,
                   /*hover name*/ l1, /*contextMenu*/ true, overrideText);
    }

    // Machine-aware accent: Swiss 800 (Swiss 800) cooler blue-grey, Classic 102 (Classic
    // 102) warmer copper. Derived live from the machine parameter.
    bool isA800() const { return (int)(values[kParamTapeMachine] + 0.5f) == 0; }
    ImU32 accentCol() const
    {
        return isA800() ? IM_COL32(96, 120, 150, 255)    // cool blue-grey
                        : IM_COL32(176, 116, 70, 255);    // warm copper
    }

    // Authentic per-machine option matrix (HARD-gated — only real options selectable,
    // enforced by the selector nVisible + coerceChoice + the DSP-side coercion):
    //   Swiss 800    : speed 7.5/15/30,      EQ NAB/CCIR, type 250/456/900/GP9, no Head Width
    //   Classic 102 : speed 3.75/7.5/15/30, EQ NAB/CCIR, type 250/456/900/GP9, Head Width 1/4-1
    // Only TAPE SPEED (3.75 Swiss 800-hidden) and Head Width (Swiss 800-hidden) differ; EQ/tape/cal
    // are identical on both decks, so there is no "non-standard" state to flag.

    // Centred small-caps section header with an accent underline. Engraved
    // (light highlight below + dark ink on top) so it reads clearly on the
    // mid-gray panel instead of the old low-contrast dim ink.
    void sectionHeader(ImDrawList* dl, float cx, float y, const char* txt)
    {
        text(dl, cx, y + 1.0f, 9.5f, kColPanelHi, txt, 0, true);   // engrave highlight
        text(dl, cx, y, 9.5f, kColInk, txt, 0, true);
        ImFont* f = labelFont ? labelFont : ImGui::GetFont();
        const float w = f->CalcTextSizeA(9.5f * s, FLT_MAX, 0, txt).x;
        const ImVec2 c = P(cx, y + 13.0f);
        dl->AddLine(ImVec2(c.x - w * 0.5f, c.y), ImVec2(c.x + w * 0.5f, c.y), accentCol(), 1.4f * s);
    }

    // Vertical group divider drawn as an ENGRAVED GROOVE: a darker shadow score with a
    // lighter metal highlight one pixel to its side, so it reads as a channel cut into
    // the brushed panel instead of a flat border. Neutral chassis tones only (the accent
    // stays on header underlines / badge). Thickness + offset derive from the scale
    // factor and the X is snapped to the device pixel grid so the hairlines stay crisp
    // at every HiDPI scale. To trial an accent-tinted groove, flip TM_DIVIDER_ACCENT.
    #ifndef TM_DIVIDER_ACCENT
    #define TM_DIVIDER_ACCENT 0
    #endif
    void engravedDivider(ImDrawList* dl, float x, float y0, float y1) const
    {
        const float th = std::max(1.0f, std::floor(s + 0.5f));      // ~1 physical px, integer
        const float dx = std::floor(org.x + x * s) + 0.5f * th;     // pixel-centred -> crisp AA
        const float ay0 = org.y + y0 * s, ay1 = org.y + y1 * s;
       #if TM_DIVIDER_ACCENT
        const ImU32 shadow = accentCol();
        const ImU32 hilite = IM_COL32(224, 223, 218, 150);
       #else
        const ImU32 shadow = IM_COL32(120, 119, 116, 205);          // dark score
        const ImU32 hilite = IM_COL32(226, 224, 219, 175);          // metal highlight catching light
       #endif
        dl->AddLine(ImVec2(dx, ay0), ImVec2(dx, ay1), shadow, th);
        dl->AddLine(ImVec2(dx + th, ay0), ImVec2(dx + th, ay1), hilite, th);
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
        // Engraved grooves separate the four groups. They run from just under the
        // header row to the shared bottom baseline (identical extents in both machine
        // modes) so the toggle rows below stay framed inside their group's cell.
        const float cellTop = hy + 16.0f, cellBot = 452.0f;
        for (float dx : { 210.0f, 400.0f, 590.0f })
            engravedDivider(dl, dx, cellTop, cellBot);
        dl->AddLine(P(26, cellBot), P(774, cellBot), IM_COL32(150, 151, 153, 140), 1.0f * s);

        // GAIN STAGING — with GAIN LINK (autoComp) on, the DSP holds the output at the
        // inverse of the input (drive the tape harder without the level rising) and the
        // OUTPUT knob is an ADDITIVE makeup trim on top of that inverse (effective output
        // gain = -input + trim; factory presets ship a fitted trim to match each UAD preset's
        // own output loudness). The link lives entirely in the DSP so it works under host
        // automation. The OUTPUT knob's STORED value is the trim (what presets carry and what
        // a drag edits); to make the link VISIBLE — the user watches the needle, not only the
        // number — we drive the whole knob in the EFFECTIVE domain when link is on: its needle
        // AND readout show -input + trim, so moving INPUT rotates the OUTPUT needle live to
        // compensate. The stored value stays the trim (converted back in the setParam adapter),
        // so presets and the DSP are untouched. Link off -> plain output gain / trim.
        const bool gainLink = values[kParamAutoComp] > 0.5f;
        knob(dl, "input",  kParamInputGain,  67.0f, cy, "INPUT",  "%.1f", " dB");
        knob(dl, "output", kParamOutputGain,163.0f, cy, "OUTPUT", "%.1f", " dB",
             /*dispAdd*/ 0.0f, /*overrideText*/ nullptr,
             /*linked*/ gainLink, /*linkOffset*/ -values[kParamInputGain]);
        text(dl, 115, ty, 9.0f, kColInk, "GAIN LINK", 0, true);
        tmButton(dl, "autocomp", kParamAutoComp, 82, ty + 10, 148, ty + 30, "", true);

        // TAPE CHARACTER — BIAS fades into the panel when AUTO BIAS (autoCal) is
        // on (the DSP then computes bias from tape+speed and ignores the knob). A
        // soft panel-toned disc veil reads as "inactive" without a hard grey box.
        const bool biasAuto = values[kParamAutoCal] > 0.5f;
        // With AUTO BIAS on the DSP ignores the knob, so the readout shows "AUTO"
        // (display only — the knob value is unchanged) in addition to the dim veil.
        knob(dl, "bias",  kParamBias,       257.0f, cy, "BIAS",  "%+.0f", "%", -50.0f,
             biasAuto ? "AUTO" : nullptr);
        if (biasAuto)
            dl->AddCircleFilled(P(257.0f, cy), 33.0f * s, IM_COL32(190, 188, 183, 140), 48); // panel-toned veil = inactive
        knob(dl, "noise", kParamNoiseAmount,353.0f, cy, "NOISE", "%.0f", "%");
        text(dl, 305, ty, 9.0f, kColInk, "AUTO BIAS", 0, true);
        tmButton(dl, "autocal", kParamAutoCal, 272, ty + 10, 338, ty + 30, "", true);

        // TRANSPORT
        knob(dl, "wow",  kParamWow,     447.0f, cy, "WOW",     "%.0f", "%");
        knob(dl, "flut", kParamFlutter, 543.0f, cy, "FLUTTER", "%.0f", "%");

        // FILTERS — at their inactive extreme (HP at 20 Hz min, LP at 20 kHz max) the
        // filter does nothing, so the readout reads "OFF" instead of the frequency.
        // Display only: no parameter/DSP change (the knob value is unchanged).
        const TmParam& hpP = kTmParams[kParamHighpassFreq];
        const TmParam& lpP = kTmParams[kParamLowpassFreq];
        const bool hpOff = values[kParamHighpassFreq] <= hpP.min + 0.5f;
        const bool lpOff = values[kParamLowpassFreq]  >= lpP.max - 0.5f;
        knob(dl, "hp", kParamHighpassFreq, 637.0f, cy, "HIGHPASS", "%.0f", " Hz", 0.0f, hpOff ? "OFF" : nullptr);
        knob(dl, "lp", kParamLowpassFreq,  733.0f, cy, "LOWPASS",  "%.0f", " Hz", 0.0f, lpOff ? "OFF" : nullptr);

        // ATR-102 front-panel toggle — Classic 102 only (the Studer A800 has no such
        // switch, so it is hidden on the Swiss 800). W&F stays on the main face as a
        // performance control under TRANSPORT (it gates the Wow/Flutter knobs). Crosstalk
        // and Transformer — calibration/output-electronics switches — now live in the
        // ADVANCED modal (see drawAdvanced). W&F's cell is column-anchored under TRANSPORT,
        // so no reflow is needed for the relocated buttons: FILTERS simply has no toggle
        // row, exactly as the Swiss 800 already renders every column here.
        if (!isA800())
        {
            text(dl, 495, ty, 9.0f, kColInk, "W&F", 0, true);
            tmButton(dl, "wfenable", kParamWowFlutterOn, 462, ty + 10, 528, ty + 30, "", true);
        }
    }

    void tmButton(ImDrawList* dl, const char* id, uint32_t param, float x0, float y0,
                  float x1, float y1, const char* label, bool stateLabel = false)
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
        // ON reads as a recessed, lit cap (warm border + red pilot + red text);
        // OFF as a raised light cap with dim "OFF" text — the two are now
        // unmistakable instead of both showing a static "ON".
        if (on)
            dl->AddRectFilledMultiColor(b0, b1, IM_COL32(188, 186, 188, 255), IM_COL32(188, 186, 188, 255),
                                        IM_COL32(214, 212, 212, 255), IM_COL32(214, 212, 212, 255));
        else
            dl->AddRectFilledMultiColor(b0, b1, IM_COL32(226, 226, 228, 255), IM_COL32(226, 226, 228, 255),
                                        IM_COL32(186, 186, 188, 255), IM_COL32(186, 186, 188, 255));
        dl->AddRect(b0, b1, on ? IM_COL32(150, 84, 74, 255) : IM_COL32(90, 90, 92, 255), 2.0f * s, 0, 1.4f * s);
        if (on) dl->AddCircleFilled(P(x0 + 8.0f, 0.5f * (y0 + y1)), 2.6f * s, kColRed, 12);
        const char* lbl = stateLabel ? (on ? "ON" : "OFF") : label;
        text(dl, 0.5f * (x0 + x1) + 5.0f, y0 + 0.32f * (y1 - y0), 10.0f,
             on ? kColRed : kColInkDim, lbl, 0, true);
    }

    //--- state -----------------------------------------------------------------
    duskdpf::DuskPanel panel;
    duskdpf::CrispFontSet fontSet;
    ImFont* labelFont = nullptr;
    float   values[kParamCount] = {};
    float   s = 1.0f;
    ImVec2  org = ImVec2(0, 0);
    float   needleL = 0.0f, needleR = 0.0f;
    float   clipHoldL = 0.0f, clipHoldR = 0.0f;   // PEAK-lamp hold timers (seconds); 0 = unlit
    bool    outLinkActive_ = false;   // set only while the OUTPUT knob is drawn under GAIN LINK
    float   outLinkOffset_ = 0.0f;    // = -input; effective = trim + offset (see setParam / knob)
    ImVec2  advScrimPressPos = ImVec2(0, 0);       // where an Advanced-scrim press began (travel guard)
    int     meterSource = 0;      // 0 = input (record/tape-drive level, like the hardware VU), 1 = output
    int     currentPreset = -1;
    std::string currentUserName;  // non-empty when a user preset is active
    std::vector<std::pair<std::string, std::string>> userPresets; // (name, path)
    char    saveBuf[64] = {};
    bool    openSaveModal = false;
    bool    showAdvanced = false;   // hidden advanced (Repro EQ) panel toggle

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeMachineUI)
};

UI* createUI()
{
    return new TapeMachineUI();
}

END_NAMESPACE_DISTRHO

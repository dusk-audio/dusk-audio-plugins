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

        scanUserPresets();
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
        drawHeader(dl);
        const bool clipEnabled = meterSource != 0;   // OUTPUT source only
        const ImU32 accent = accentCol();
        drawVU(dl, 68,  62, 388, 198, meterLevel(0), needleL, clipHoldL, clipEnabled, accent, "L");
        drawVU(dl, 412, 62, 732, 198, meterLevel(1), needleR, clipHoldR, clipEnabled, accent, "R");
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
    bool chevron(ImDrawList* dl, const char* id, float cx, float cy, bool left, const char* tip)
    {
        const ImVec2 b0 = P(cx - 9, cy - 10), b1 = P(cx + 9, cy + 10);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (tip != nullptr && hov) ImGui::SetTooltip("%s", tip);
        dl->AddRectFilled(b0, b1, hov ? IM_COL32(214, 214, 216, 255) : IM_COL32(224, 224, 226, 255), 2.0f * s);
        dl->AddRect(b0, b1, IM_COL32(120, 121, 123, 255), 2.0f * s, 0, 1.0f * s);
        const ImVec2 c = P(cx, cy); const float d = 4.0f * s;
        if (left) dl->AddTriangleFilled(ImVec2(c.x + d * 0.5f, c.y - d), ImVec2(c.x + d * 0.5f, c.y + d), ImVec2(c.x - d * 0.7f, c.y), kColInk);
        else      dl->AddTriangleFilled(ImVec2(c.x - d * 0.5f, c.y - d), ImVec2(c.x - d * 0.5f, c.y + d), ImVec2(c.x + d * 0.7f, c.y), kColInk);
        return ImGui::IsItemClicked();
    }

    // small silver text button
    bool textButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                    const char* label, const char* tip)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (tip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        dl->AddRectFilledMultiColor(b0, b1, IM_COL32(226, 226, 228, 255), IM_COL32(226, 226, 228, 255),
                                    IM_COL32(186, 186, 188, 255), IM_COL32(186, 186, 188, 255));
        dl->AddRect(b0, b1, IM_COL32(90, 90, 92, 255), 2.0f * s, 0, 1.2f * s);
        text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 9.5f, kColInk, label, 0, true);
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

    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(P(15, 12), P(232, 40), IM_COL32(30, 30, 32, 255), 3.0f * s);
        dl->AddRect(P(15, 12), P(232, 40), IM_COL32(120, 120, 122, 255), 3.0f * s, 0, 1.4f * s);
        text(dl, 27, 17, 15, IM_COL32(232, 232, 228, 255), "TapeMachine 2", -1, true);
        {   // machine badge (accent-tinted, right side of the nameplate)
            const char* badge = isA800() ? "A800" : "ATR-102";
            dl->AddRectFilled(P(178, 16), P(228, 34), accentCol(), 3.0f * s);
            dl->AddRect(P(178, 16), P(228, 34), IM_COL32(0, 0, 0, 90), 3.0f * s, 0, 1.0f * s);
            text(dl, 203, 20, 8.5f, IM_COL32(18, 18, 20, 255), badge, 0, true);
        }

        // preset browser: < [combo] >  INIT  SAVE
        const char* cur = currentPreset >= 0 ? kTmPresets[currentPreset].name
                        : (!currentUserName.empty() ? currentUserName.c_str() : "Presets...");
        text(dl, 364, 6, 8.0f, kColInkDim, "PRESET", 0, true);
        if (chevron(dl, "##pv", 250, 28, true, "Previous preset")) stepPreset(-1);
        ImGui::SetCursorScreenPos(P(264, 18));
        ImGui::SetNextItemWidth(200.0f * s);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(232, 232, 232, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(244, 244, 244, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(238, 238, 238, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(150, 152, 156, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(198, 199, 201, 255));
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
            if (!userPresets.empty())
            {
                ImGui::TextDisabled("User");
                for (const auto& up : userPresets)
                    if (ImGui::Selectable(up.first.c_str(), currentPreset < 0 && up.first == currentUserName))
                        loadUserPreset(up.second, up.first);
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(8);
        if (chevron(dl, "##nx", 478, 28, false, "Next preset")) stepPreset(+1);
        if (textButton(dl, "##init", 492, 18, 528, 38, "INIT", "Reset all controls to their defaults")) initDefaults();
        if (textButton(dl, "##save", 532, 18, 568, 38, "SAVE", "Save the current settings as a user preset"))
        {
            std::snprintf(saveBuf, sizeof(saveBuf), "%s", currentUserName.c_str());
            ImGui::OpenPopup("Save Preset");
        }
        drawSaveModal();

        // segmented IN | OUT meter source switch (both cells visible)
        {
            const float mx0 = 576, mid = 610, mx1 = 644, my0 = 18, my1 = 38;
            text(dl, mid, 6, 8.0f, kColInkDim, "METER", 0, true);
            dl->AddRectFilled(P(mx0, my0), P(mx1, my1), IM_COL32(210, 211, 213, 255), 3.0f * s);

            ImGui::SetCursorScreenPos(P(mx0, my0));
            ImGui::InvisibleButton("mIn", ImVec2((mid - mx0) * s, (my1 - my0) * s));
            if (ImGui::IsItemClicked()) meterSource = 0;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Meter the INPUT (record / tape-drive) level.");
            const bool inAct = meterSource == 0;
            if (inAct) dl->AddRectFilled(P(mx0, my0), P(mid, my1), IM_COL32(150, 152, 156, 255), 3.0f * s);
            text(dl, 0.5f * (mx0 + mid), 22, 9.0f, inAct ? IM_COL32(22, 22, 24, 255) : kColInkDim, "IN", 0, inAct);

            ImGui::SetCursorScreenPos(P(mid, my0));
            ImGui::InvisibleButton("mOut", ImVec2((mx1 - mid) * s, (my1 - my0) * s));
            if (ImGui::IsItemClicked()) meterSource = 1;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Meter the OUTPUT level (digital clip lamp active).");
            const bool outAct = meterSource != 0;
            if (outAct) dl->AddRectFilled(P(mid, my0), P(mx1, my1), IM_COL32(150, 152, 156, 255), 3.0f * s);
            text(dl, 0.5f * (mid + mx1), 22, 9.0f, outAct ? IM_COL32(22, 22, 24, 255) : kColInkDim, "OUT", 0, outAct);

            dl->AddLine(P(mid, my0 + 1), P(mid, my1 - 1), IM_COL32(120, 121, 123, 255), 1.0f * s);
            dl->AddRect(P(mx0, my0), P(mx1, my1), IM_COL32(90, 90, 92, 255), 3.0f * s, 0, 1.2f * s);
        }

        // bypass, kept clear of the top-right corner screw (~x784)
        tmButton(dl, "bypass", kParamBypass, 648, 14, 736, 38, "BYPASS",
                 "Bypass the plugin (host-integrated).");
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
                float level, float& needle, float& clipHold, bool clipEnabled,
                ImU32 accent, const char* label)
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

        // over lamp (right). Only meaningful on the OUTPUT source, where it means a
        // true >= 0 dBFS digital clip. On INPUT, being in the red is desirable tape
        // drive, so the lamp is disabled (clipEnabled == false).
        if (clipEnabled && level > 1.0f) clipHold = 1.2f;
        else if (clipHold > 0.0f) clipHold -= ImGui::GetIO().DeltaTime;
        const bool over = clipEnabled && clipHold > 0.0f;
        const ImVec2 lp = P(fx1 - 15, pivotY - L * 0.20f);
        if (over) dl->AddCircleFilled(lp, 8.0f * s, IM_COL32(230, 60, 40, 70), 20);   // glow
        dl->AddCircleFilled(lp, 5.0f * s, over ? IM_COL32(232, 60, 40, 255) : IM_COL32(96, 40, 32, 255), 16);
        dl->AddCircleFilled(ImVec2(lp.x - 1.4f * s, lp.y - 1.6f * s), 1.6f * s,
                            IM_COL32(255, 200, 180, over ? 210 : 90), 10);

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
                  float w, const char* title, const char* tip)
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
        const bool open = ImGui::BeginCombo(id, d.choices[cur]);   // arrow -> reads as a dropdown
        if (tip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        if (open)
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
        // id, param, column-centre, width (fits widest option + arrow), title, tooltip
        struct Sel { const char* id; uint32_t p; float cx, w; const char* t; const char* tip; };
        static const Sel ss[6] = {
            { "##machine", kParamTapeMachine,  83.f, 100.f, "MACHINE",
              "Tape machine model: Swiss 800 (tighter/cleaner) or Classic 102 (warmer)." },
            { "##speed",   kParamTapeSpeed,   210.f,  82.f, "TAPE SPEED",
              "Tape speed. Faster = extended lows/highs, less head bump and noise." },
            { "##type",    kParamTapeType,    337.f,  90.f, "TAPE TYPE",
              "Tape formulation: affects saturation, headroom and noise floor." },
            { "##path",    kParamSignalPath,  463.f,  78.f, "SIGNAL PATH",
              "Repro = full tape path; Sync = repro w/ extra HF loss; Input = electronics only; Thru = bypass." },
            { "##eq",      kParamEqStandard,  590.f,  70.f, "EQ STANDARD",
              "Record/repro EQ curve: NAB (US), CCIR/IEC (EU), or AES (30 IPS)." },
            { "##os",      kParamOversampling,717.f,  62.f, "OVERSAMPLING",
              "Anti-alias oversampling. Higher = cleaner saturation, more CPU." },
        };
        for (const Sel& e : ss)
            selector(dl, e.id, e.p, e.cx, y, e.w, e.t, e.tip);
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
                   /*persistent*/ true, tip, /*rightClickReset*/ true, 1.0f, dispAdd,
                   /*hover name*/ l1);
    }

    // Machine-aware accent: A800 (Swiss 800) cooler blue-grey, ATR-102 (Classic
    // 102) warmer copper. Derived live from the machine parameter.
    bool isA800() const { return (int)(values[kParamTapeMachine] + 0.5f) == 0; }
    ImU32 accentCol() const
    {
        return isA800() ? IM_COL32(96, 120, 150, 255)    // cool blue-grey
                        : IM_COL32(176, 116, 70, 255);    // warm copper
    }

    // Centred small-caps section header with an accent underline.
    void sectionHeader(ImDrawList* dl, float cx, float y, const char* txt)
    {
        text(dl, cx, y, 9.5f, kColInkDim, txt, 0, true);
        ImFont* f = labelFont ? labelFont : ImGui::GetFont();
        const float w = f->CalcTextSizeA(9.5f * s, FLT_MAX, 0, txt).x;
        const ImVec2 c = P(cx, y + 13.0f);
        dl->AddLine(ImVec2(c.x - w * 0.5f, c.y), ImVec2(c.x + w * 0.5f, c.y), accentCol(), 1.4f * s);
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
    float   clipHoldL = 0.0f, clipHoldR = 0.0f;
    int     meterSource = 1;      // 0 = input, 1 = output
    int     currentPreset = -1;
    std::string currentUserName;  // non-empty when a user preset is active
    std::vector<std::pair<std::string, std::string>> userPresets; // (name, path)
    char    saveBuf[64] = {};
    bool    openSaveModal = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeMachineUI)
};

UI* createUI()
{
    return new TapeMachineUI();
}

END_NAMESPACE_DISTRHO

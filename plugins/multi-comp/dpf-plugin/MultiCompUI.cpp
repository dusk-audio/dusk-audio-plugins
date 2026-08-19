// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Multi-Comp 2 Dear ImGui UI.  The controls mirror EnhancedCompressorEditor and
// ModernCompressorPanels; the DSP and parameter ownership remain in the shell/core.

#include "DistrhoUI.hpp"
#include "MultiCompAccess.hpp"
#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"
#include "MultiCompVersion.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"
#include "DuskSupportersOverlay.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

START_NAMESPACE_DISTRHO

namespace
{
constexpr float kDesignW = 1120.0f;
constexpr float kDesignH = 760.0f;
constexpr float kHeaderH = 64.0f;
constexpr float kGlobalH = 132.0f;
constexpr float kSidechainH = 126.0f;
constexpr float kPanelTop = kHeaderH + kGlobalH + kSidechainH;

constexpr ImU32 kBg = IM_COL32(27, 28, 32, 255);
constexpr ImU32 kPanel = IM_COL32(35, 37, 43, 255);
constexpr ImU32 kPanelRaised = IM_COL32(43, 46, 54, 255);
constexpr ImU32 kLine = IM_COL32(75, 79, 90, 255);
constexpr ImU32 kText = IM_COL32(232, 234, 238, 255);
constexpr ImU32 kDim = IM_COL32(162, 168, 180, 255);
constexpr ImU32 kAccent = IM_COL32(65, 194, 220, 255);
constexpr ImU32 kGreen = IM_COL32(56, 145, 88, 255);
constexpr ImU32 kRed = IM_COL32(164, 65, 60, 255);
constexpr ImU32 kBandColors[4] = {
    IM_COL32(92, 165, 235, 255), IM_COL32(92, 205, 150, 255),
    IM_COL32(232, 185, 74, 255), IM_COL32(224, 100, 93, 255)
};

// Parameter indices are aliases of the single table order in MultiCompParams.hpp.
using ParamId = multicompp::ParamId;
constexpr uint32_t P_MODE = static_cast<uint32_t>(ParamId::Mode), P_BYPASS = static_cast<uint32_t>(ParamId::Bypass), P_LINK = static_cast<uint32_t>(ParamId::StereoLink), P_MIX = static_cast<uint32_t>(ParamId::Mix);
constexpr uint32_t P_SC_HP = static_cast<uint32_t>(ParamId::SidechainHP), P_TRUE_PEAK = static_cast<uint32_t>(ParamId::TruePeakEnable), P_TP_QUALITY = static_cast<uint32_t>(ParamId::TruePeakQuality), P_EXT_SC = static_cast<uint32_t>(ParamId::ExternalSidechain);
#define MC_PID(name) static_cast<uint32_t>(ParamId::name)
constexpr uint32_t P_AUTO = MC_PID(AutoMakeup), P_DIST = MC_PID(Distortion), P_DIST_AMT = MC_PID(DistortionAmount), P_OS = MC_PID(Oversampling), P_LOOK = MC_PID(GlobalLookahead);
constexpr uint32_t P_OPTO_PEAK = MC_PID(OptoPeakReduction), P_OPTO_GAIN = MC_PID(OptoGain), P_OPTO_LIMIT = MC_PID(OptoLimit);
constexpr uint32_t P_FET_IN = MC_PID(FetInput), P_FET_OUT = MC_PID(FetOutput), P_FET_ATTACK = MC_PID(FetAttack), P_FET_RELEASE = MC_PID(FetRelease);
constexpr uint32_t P_FET_RATIO = MC_PID(FetRatio), P_FET_CURVE = MC_PID(FetCurve), P_FET_TRANSIENT = MC_PID(FetTransient), P_FET_THRESHOLD = MC_PID(FetThreshold);
constexpr uint32_t P_VCA_THRESHOLD = MC_PID(VcaThreshold), P_VCA_RATIO = MC_PID(VcaRatio), P_VCA_ATTACK = MC_PID(VcaAttack), P_VCA_RELEASE = MC_PID(VcaRelease);
constexpr uint32_t P_VCA_OUT = MC_PID(VcaOutput), P_VCA_OVER_EASY = MC_PID(VcaOverEasy), P_VCA_DETECTOR = MC_PID(VcaClassicDetector);
constexpr uint32_t P_BUS_THRESHOLD = MC_PID(BusThreshold), P_BUS_RATIO = MC_PID(BusRatio), P_BUS_ATTACK = MC_PID(BusAttack), P_BUS_RELEASE = MC_PID(BusRelease);
constexpr uint32_t P_BUS_MAKEUP = MC_PID(BusMakeup), P_BUS_MIX = MC_PID(BusMix);
constexpr uint32_t P_SVCA_THRESHOLD = MC_PID(StudioVcaThreshold), P_SVCA_RATIO = MC_PID(StudioVcaRatio), P_SVCA_ATTACK = MC_PID(StudioVcaAttack);
constexpr uint32_t P_SVCA_RELEASE = MC_PID(StudioVcaRelease), P_SVCA_OUT = MC_PID(StudioVcaOutput);
constexpr uint32_t P_DIG_THRESHOLD = MC_PID(DigitalThreshold), P_DIG_RATIO = MC_PID(DigitalRatio), P_DIG_KNEE = MC_PID(DigitalKnee), P_DIG_ATTACK = MC_PID(DigitalAttack);
constexpr uint32_t P_DIG_RELEASE = MC_PID(DigitalRelease), P_DIG_LOOK = MC_PID(DigitalLookahead), P_DIG_MIX = MC_PID(DigitalMix), P_DIG_OUT = MC_PID(DigitalOutput);
constexpr uint32_t P_DIG_ADAPT = MC_PID(DigitalAdaptive), P_X1 = MC_PID(Crossover1), P_X2 = MC_PID(Crossover2), P_X3 = MC_PID(Crossover3);
constexpr uint32_t P_ENV = MC_PID(EnvelopeCurve), P_SC_LISTEN = MC_PID(GlobalSidechainListen), P_MB_MIX = MC_PID(MbMix), P_MB_OUT = MC_PID(MbOutput);
constexpr uint32_t P_NOISE = MC_PID(NoiseEnable), P_SAT_MODE = MC_PID(SaturationMode), P_SC_LOW_FREQ = MC_PID(ScLowFreq), P_SC_LOW_GAIN = MC_PID(ScLowGain);
constexpr uint32_t P_SC_HIGH_FREQ = MC_PID(ScHighFreq), P_SC_HIGH_GAIN = MC_PID(ScHighGain), P_LINK_MODE = MC_PID(StereoLinkMode);
#undef MC_PID

constexpr uint32_t kMeterMaster = static_cast<uint32_t>(multicompp::kMeterMaster);
constexpr uint32_t kMeterBand0 = static_cast<uint32_t>(multicompp::kMeterBand0);

const char* modeName(int mode)
{
    return mode >= 0 && mode < 8 ? multicompp::kModes[mode] : "Opto";
}

const char* bandName(int band)
{
    static const char* names[] = {"LOW", "LO-MID", "HI-MID", "HIGH"};
    return names[band < 0 ? 0 : band > 3 ? 3 : band];
}
}

class MultiCompUI final : public UI, public duskdpf::ParamHost
{
public:
    MultiCompUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < multicompp::kTotalParamCount; ++i)
        {
            if (i < static_cast<uint32_t>(multicompp::kParamCount))
                values[i] = multicompp::kParams[i].def;
            else
                values[i] = 0.0f;
        }
        for (int b = 0; b < duskaudio::kMultiCompBands; ++b)
            for (int f = 0; f < 8; ++f)
                values[multicompp::kBandBase + b * 8 + f] = multicompp::bandParam(f, b).def;

        // DPF reports the actual window size.  The UI draws a fixed design space
        // with one uniform scale and letterboxes any extra width or height.
        setGeometryConstraints(760, 520, false);
        static const float kFontSizes[] = {9.f, 11.f, 13.f, 16.f, 20.f, 26.f, 30.f};
        fontSet = duskdpf::loadCrispFontSet(kFontSizes, 7, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
    }

    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override { editParameter(idx, false); }
    void setParam(uint32_t idx, float value) override { currentPreset = -1; setParameterValue(idx, value); }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values.size()) values[index] = value;
    }

    void stateChanged(const char* key, const char* state) override
    {
        if (key == nullptr || state == nullptr || std::strcmp(key, "parameters") != 0) return;
        currentPreset = -1;
        const std::string_view serialized(state);
        size_t begin = 0;
        while (begin < serialized.size())
        {
            const size_t end = serialized.find(';', begin);
            const std::string_view token = serialized.substr(begin,
                end == std::string_view::npos ? serialized.size() - begin : end - begin);
            const size_t equal = token.find('=');
            if (equal != std::string_view::npos && token.substr(0, equal) != "v")
            {
                std::uint32_t bits = 0;
                const std::string_view encoded = token.substr(equal + 1);
                const auto parsed = std::from_chars(encoded.data(), encoded.data() + encoded.size(), bits, 16);
                if (parsed.ec == std::errc() && parsed.ptr == encoded.data() + encoded.size())
                {
                    float decoded = 0.0f;
                    std::memcpy(&decoded, &bits, sizeof(decoded));
                    if (std::isfinite(decoded))
                    {
                        const std::string_view id = token.substr(0, equal);
                        for (int i = 0; i < multicompp::kParamCount; ++i)
                            if (id == multicompp::kParams[static_cast<size_t>(i)].id)
                                values[static_cast<size_t>(i)] = decoded;
                        for (int i = multicompp::kBandBase; i < multicompp::kMeterMaster; ++i)
                        {
                            const int band = (i - multicompp::kBandBase) / 8;
                            const int field = (i - multicompp::kBandBase) % 8;
                            char bandId[64];
                            std::snprintf(bandId, sizeof(bandId), "%s_%d",
                                          multicompp::bandParam(field, band).id, band);
                            if (id == bandId)
                            {
                                values[static_cast<size_t>(i)] = decoded;
                                break;
                            }
                        }
                    }
                }
            }
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        repaint();
    }

    void programLoaded(uint32_t index) override
    {
        currentPreset = index < multicompp::kFactoryPresets.size() ? static_cast<int>(index) : -1;
    }

    void uiIdle() override { repaint(); }

    void onImGuiDisplay() override
    {
        const float winW = static_cast<float>(getWidth());
        const float winH = static_cast<float>(getHeight());
        const float scale = std::min(winW / kDesignW, winH / kDesignH);
        const ImVec2 origin((winW - kDesignW * scale) * 0.5f,
                            (winH - kDesignH * scale) * 0.5f);
        panel.begin(scale, origin, labelFont, this);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("##MultiComp2", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(21, 22, 25, 255));
        drawHeader(dl);
        drawGlobal(dl);
        drawSidechain(dl);
        drawModePanel(dl);

        if (showSupporters)
            duskdpf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH, showSupporters,
                                           "Multi-Comp 2", MULTICOMP2_VERSION_STRING,
                                           &supporters);

        const duskdpf::ResizeGripState grip =
            panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH);
        ImGui::End();
        ImGui::PopStyleVar(2);
        if (grip.resized) setSize(grip.width, grip.height);
    }

private:
    std::array<float, multicompp::kTotalParamCount> values{};
    duskdpf::DuskPanel panel;
    duskdpf::CrispFontSet fontSet;
    ImFont* labelFont = nullptr;
    duskdpf::SupportersOverlay supporters;
    bool showSupporters = false;
    int currentPreset = -1;

    float value(uint32_t p) const { return values[p]; }

    void setValue(uint32_t p, float v)
    {
        currentPreset = -1;
        editParameter(p, true);
        values[p] = v;
        setParameterValue(p, v);
        editParameter(p, false);
    }

    void drawSection(ImDrawList* dl, float y0, float y1, const char* title)
    {
        dl->AddRectFilled(panel.P(8, y0), panel.P(kDesignW - 8, y1), kPanel, 5.0f * panel.scale());
        dl->AddRect(panel.P(8, y0), panel.P(kDesignW - 8, y1), kLine, 5.0f * panel.scale(), 0, panel.scale());
        panel.text(dl, 22, y0 + 10, 11, kAccent, title, -1, true);
    }

    void titleHit(ImDrawList* dl)
    {
        panel.text(dl, 22, 11, 24, kText, "Multi-Comp 2", -1, true);
        panel.text(dl, 24, 40, 10, kDim, modeName(static_cast<int>(std::lround(value(P_MODE)))), -1);
        panel.text(dl, kDesignW - 20, 20, 11, kDim, "Dusk Audio", 1);
        ImGui::SetCursorScreenPos(panel.P(12, 7));
        ImGui::InvisibleButton("##mc_title", ImVec2(190 * panel.scale(), 44 * panel.scale()));
        if (ImGui::IsItemClicked()) showSupporters = true;
    }

    bool combo(const char* id, uint32_t p, const char* const* labels, int count,
               float x, float y, float w, const char* caption)
    {
        panel.text(ImGui::GetWindowDrawList(), x, y, 9.5f, kDim, caption, 0, true);
        const int selected = std::clamp(static_cast<int>(std::lround(value(p))), 0, count - 1);
        const ImVec2 p0 = panel.P(x - w * 0.5f, y + 16);
        const ImVec2 p1 = panel.P(x + w * 0.5f, y + 43);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, IM_COL32(24, 25, 29, 255), 3 * panel.scale());
        dl->AddRect(p0, p1, kLine, 3 * panel.scale(), 0, panel.scale());
        panel.text(dl, x, y + 23, 10, kText, labels[selected], 0);
        ImGui::SetCursorScreenPos(p0);
        ImGui::SetNextItemWidth(p1.x - p0.x);
        char hiddenId[64];
        std::snprintf(hiddenId, sizeof(hiddenId), "##%s", id);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        bool changed = false;
        const bool open = ImGui::BeginCombo(hiddenId, labels[selected], ImGuiComboFlags_NoArrowButton);
        ImGui::PopStyleColor(4);
        if (open)
        {
            for (int i = 0; i < count; ++i)
            {
                const bool isSelected = i == selected;
                if (ImGui::Selectable(labels[i], isSelected))
                {
                    setValue(p, static_cast<float>(i));
                    changed = true;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, kHeaderH), IM_COL32(18, 19, 22, 255));
        dl->AddLine(panel.P(0, kHeaderH), panel.P(kDesignW, kHeaderH), kLine, panel.scale());
        titleHit(dl);
        combo("mc_mode", P_MODE, multicompp::kModes, 8, 310, 10, 180, "MODE");
        // Presets use a custom popup because they are programs, not a parameter enum.
        const ImVec2 p0 = panel.P(505, 26), p1 = panel.P(750, 54);
        dl->AddRectFilled(p0, p1, IM_COL32(30, 31, 35, 255), 3 * panel.scale());
        dl->AddRect(p0, p1, kLine, 3 * panel.scale(), 0, panel.scale());
        const char* presetName = currentPreset >= 0
            ? multicompp::kFactoryPresets[static_cast<size_t>(currentPreset)].name : "Factory Preset";
        panel.text(dl, 518, 35, 10.5f, kText, presetName, -1);
        ImGui::SetCursorScreenPos(p0);
        ImGui::SetNextItemWidth(p1.x - p0.x);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        const bool presetOpen = ImGui::BeginCombo("##mc_factory", presetName, ImGuiComboFlags_NoArrowButton);
        ImGui::PopStyleColor(4);
        if (presetOpen)
        {
            for (size_t i = 0; i < multicompp::kFactoryPresets.size(); ++i)
            {
                const bool selected = static_cast<int>(i) == currentPreset;
                if (ImGui::Selectable(multicompp::kFactoryPresets[i].name, selected))
                {
                    applyPreset(static_cast<int>(i));
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        panel.toggle("mc_bypass", P_BYPASS, 780, 20, 875, 50, values[P_BYPASS], "BYPASS");
    }

    void drawGlobal(ImDrawList* dl)
    {
        drawSection(dl, kHeaderH + 4, kHeaderH + kGlobalH, "GLOBAL");
        knob(dl, "mc_mix", P_MIX, 78, 125, 0, 100, "MIX", "%.0f", "%");
        knob(dl, "mc_link", P_LINK, 166, 125, 0, 100, "STEREO LINK", "%.0f", "%");
        combo("mc_linkmode", P_LINK_MODE, multicompp::kLinkMode, 3, 270, 103, 112, "LINK MODE");
        combo("mc_os", P_OS, multicompp::kOversampling, 3, 394, 103, 96, "OVERSAMPLE");
        knob(dl, "mc_look", P_LOOK, 510, 125, 0, 10, "LOOKAHEAD", "%.1f", " ms");
        panel.toggle("mc_auto", P_AUTO, 568, 111, 680, 136, values[P_AUTO], "AUTO MAKEUP");
        panel.toggle("mc_tp", P_TRUE_PEAK, 688, 111, 790, 136, values[P_TRUE_PEAK], "TRUE PEAK");
        combo("mc_tpq", P_TP_QUALITY, multicompp::kTruePeakQuality, 2, 845, 103, 110, "QUALITY");
        combo("mc_sat", P_SAT_MODE, multicompp::kSaturationMode, 3, 970, 103, 125, "SATURATION");
        knob(dl, "mc_dist", P_DIST_AMT, 1080, 125, 0, 100, "DRIVE", "%.0f", "%");
    }

    void drawSidechain(ImDrawList* dl)
    {
        drawSection(dl, kHeaderH + kGlobalH + 4, kPanelTop - 4, "SIDECHAIN");
        panel.toggle("mc_ext", P_EXT_SC, 24, 223, 142, 248, values[P_EXT_SC], "EXTERNAL SC");
        panel.toggle("mc_listen", P_SC_LISTEN, 24, 255, 142, 280, values[P_SC_LISTEN], "LISTEN");
        knob(dl, "mc_schp", P_SC_HP, 190, 251, 0, 500, "HP FILTER", "%.0f", " Hz");
        knob(dl, "mc_sclf", P_SC_LOW_FREQ, 300, 251, 60, 500, "LOW FREQ", "%.0f", " Hz");
        knob(dl, "mc_sclg", P_SC_LOW_GAIN, 410, 251, -12, 12, "LOW GAIN", "%.1f", " dB");
        knob(dl, "mc_schf", P_SC_HIGH_FREQ, 520, 251, 2000, 16000, "HIGH FREQ", "%.0f", " Hz");
        knob(dl, "mc_schg", P_SC_HIGH_GAIN, 630, 251, -12, 12, "HIGH GAIN", "%.1f", " dB");
        combo("mc_env", P_ENV, multicompp::kEnvelopeCurve, 2, 770, 230, 170, "ENVELOPE CURVE");
        combo("mc_disttype", P_DIST, multicompp::kDistortion, 4, 954, 230, 130, "DISTORTION");
        neutralToggle("##mc_noise", P_NOISE, 1000, 270, 1095, 295, "NOISE");
    }

    // Noise is enabled by default, but it is a utility switch rather than a
    // compression-state indicator. Keep the same geometry and typography as the
    // fleet toggles while avoiding the red alarm treatment used for bypass/GR.
    void neutralToggle(const char* id, uint32_t p, float x0, float y0, float x1, float y1,
                       const char* label)
    {
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked())
        {
            const float next = values[p] > 0.5f ? 0.0f : 1.0f;
            setValue(p, next);
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool on = values[p] > 0.5f;
        dl->AddRectFilled(b0, b1, IM_COL32(40, 40, 43, 255), 3.0f * panel.scale());
        dl->AddRect(b0, b1, kLine, 3.0f * panel.scale(), 0, 1.4f * panel.scale());
        if (on)
            dl->AddCircleFilled(panel.P(x0 + 7.0f, 0.5f * (y0 + y1)),
                                2.6f * panel.scale(), kDim, 12);
        panel.text(dl, 0.5f * (x0 + x1) + 6.0f, y0 + 0.30f * (y1 - y0), 10.0f,
                   kText, label, 0, on);
    }

    void knob(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
              float min, float max, const char* label, const char* fmt, const char* suffix)
    {
        panel.knob(id, p, min, max, x, y, 25, values[p], defaultValue(p), false, true,
                   fmt, suffix, kPanelRaised, false, true);
        panel.text(dl, x, y + 49, 9.5f, kText, label, 0, true);
    }

    float defaultValue(uint32_t p) const
    {
        if (p < static_cast<uint32_t>(multicompp::kParamCount)) return multicompp::kParams[p].def;
        const int rel = static_cast<int>(p) - multicompp::kBandBase;
        return multicompp::bandParam(rel % 8, rel / 8).def;
    }

    void drawModePanel(ImDrawList* dl)
    {
        drawSection(dl, kPanelTop, kDesignH - 8, modeName(static_cast<int>(std::lround(value(P_MODE)))));
        const int mode = std::clamp(static_cast<int>(std::lround(value(P_MODE))), 0, 7);
        if (mode == 7)
            drawMeter(dl, 1062, kPanelTop + 24, 42, 92, meter(kMeterMaster), kAccent, "MASTER GR");
        else
            drawMeter(dl, 28, kPanelTop + 42, 48, 300, meter(kMeterMaster), kAccent, "MASTER GR");
        // Deliberately exhaustive: adding a ninth mode must force a UI decision.
        switch (mode)
        {
            case 0: drawOpto(dl); break;
            case 1: drawFet(dl, false); break;
            case 2: drawVca(dl); break;
            case 3: drawBus(dl); break;
            case 4: drawFet(dl, true); break;
            case 5: drawStudioVca(dl); break;
            case 6: drawDigital(dl); break;
            case 7: drawMultiband(dl); break;
        }
    }

    void drawOpto(ImDrawList* dl)
    {
        knob(dl, "opto_peak", P_OPTO_PEAK, 250, 380, 0, 100, "PEAK REDUCTION", "%.0f", "%");
        knob(dl, "opto_gain", P_OPTO_GAIN, 420, 380, 0, 100, "GAIN", "%.0f", "%");
        knob(dl, "opto_mix", P_MIX, 590, 380, 0, 100, "MIX", "%.0f", "%");
        panel.toggle("opto_limit", P_OPTO_LIMIT, 690, 366, 820, 392, values[P_OPTO_LIMIT], "LIMIT");
        panel.text(dl, 560, 500, 12, kDim, "Program-dependent optical envelope", 0);
    }

    void drawFet(ImDrawList* dl, bool studio)
    {
        knob(dl, studio ? "sf_in" : "fet_in", P_FET_IN, 120, 380, -20, 40, "INPUT", "%.1f", " dB");
        knob(dl, studio ? "sf_out" : "fet_out", P_FET_OUT, 250, 380, -20, 20, "OUTPUT", "%.1f", " dB");
        knob(dl, studio ? "sf_att" : "fet_att", P_FET_ATTACK, 380, 380, 0.02f, 80, "ATTACK", "%.2f", " ms");
        knob(dl, studio ? "sf_rel" : "fet_rel", P_FET_RELEASE, 510, 380, 50, 1100, "RELEASE", "%.0f", " ms");
        knob(dl, studio ? "sf_mix" : "fet_mix", P_MIX, 640, 380, 0, 100, "MIX", "%.0f", "%");
        combo(studio ? "sf_ratio" : "fet_ratio", P_FET_RATIO, multicompp::kRatios, 5, 775, 352, 118, "RATIO");
        combo(studio ? "sf_curve" : "fet_curve", P_FET_CURVE, multicompp::kFetCurve, 2, 905, 352, 142, "CURVE");
        knob(dl, studio ? "sf_trans" : "fet_trans", P_FET_TRANSIENT, 1020, 380, 0, 100, "TRANSIENT", "%.0f", "%");
        panel.text(dl, 560, 500, 12, studio ? IM_COL32(80, 215, 205, 255) : IM_COL32(235, 175, 80, 255),
                   studio ? "Clean FET response with controlled harmonics" : "Fast FET response with program-dependent release", 0);
    }

    void drawVca(ImDrawList* dl)
    {
        knob(dl, "vca_thr", P_VCA_THRESHOLD, 170, 380, -38, 12, "THRESHOLD", "%.1f", " dB");
        knob(dl, "vca_ratio", P_VCA_RATIO, 320, 380, 1, 120, "RATIO", "%.1f", ":1");
        knob(dl, "vca_att", P_VCA_ATTACK, 470, 380, 0.1f, 50, "ATTACK", "%.1f", " ms");
        knob(dl, "vca_rel", P_VCA_RELEASE, 620, 380, 10, 5000, "RELEASE", "%.0f", " ms");
        knob(dl, "vca_out", P_VCA_OUT, 770, 380, -20, 20, "OUTPUT", "%.1f", " dB");
        knob(dl, "vca_mix", P_MIX, 920, 380, 0, 100, "MIX", "%.0f", "%");
        panel.toggle("vca_over", P_VCA_OVER_EASY, 860, 450, 1000, 476, values[P_VCA_OVER_EASY], "OVER EASY");
        combo("vca_detector", P_VCA_DETECTOR, multicompp::kVcaDetector, 2, 670, 450, 170, "DETECTOR");
    }

    void drawBus(ImDrawList* dl)
    {
        knob(dl, "bus_thr", P_BUS_THRESHOLD, 160, 380, -30, 15, "THRESHOLD", "%.1f", " dB");
        combo("bus_ratio", P_BUS_RATIO, multicompp::kBusRatios, 3, 300, 352, 100, "RATIO");
        combo("bus_attack", P_BUS_ATTACK, multicompp::kBusAttack, 6, 440, 352, 108, "ATTACK");
        combo("bus_release", P_BUS_RELEASE, multicompp::kBusRelease, 5, 580, 352, 108, "RELEASE");
        knob(dl, "bus_makeup", P_BUS_MAKEUP, 720, 380, 0, 20, "MAKEUP", "%.1f", " dB");
        knob(dl, "bus_mix", P_BUS_MIX, 860, 380, 0, 100, "BUS MIX", "%.0f", "%");
        panel.text(dl, 560, 500, 12, IM_COL32(152, 190, 228, 255), "Gentle bus glue with linked stereo detection", 0);
    }

    void drawStudioVca(ImDrawList* dl)
    {
        knob(dl, "sv_thr", P_SVCA_THRESHOLD, 150, 380, -40, 20, "THRESHOLD", "%.1f", " dB");
        knob(dl, "sv_ratio", P_SVCA_RATIO, 300, 380, 1, 10, "RATIO", "%.1f", ":1");
        knob(dl, "sv_att", P_SVCA_ATTACK, 450, 380, 0.3f, 75, "ATTACK", "%.1f", " ms");
        knob(dl, "sv_rel", P_SVCA_RELEASE, 600, 380, 100, 4000, "RELEASE", "%.0f", " ms");
        knob(dl, "sv_out", P_SVCA_OUT, 750, 380, -20, 20, "OUTPUT", "%.1f", " dB");
        knob(dl, "sv_mix", P_MIX, 900, 380, 0, 100, "MIX", "%.0f", "%");
        panel.text(dl, 560, 500, 12, IM_COL32(220, 120, 125, 255), "RMS detection, soft knee, precision VCA control", 0);
    }

    void drawDigital(ImDrawList* dl)
    {
        knob(dl, "dig_thr", P_DIG_THRESHOLD, 120, 380, -60, 0, "THRESHOLD", "%.1f", " dB");
        knob(dl, "dig_ratio", P_DIG_RATIO, 250, 380, 1, 100, "RATIO", "%.1f", ":1");
        knob(dl, "dig_knee", P_DIG_KNEE, 380, 380, 0, 20, "KNEE", "%.1f", " dB");
        knob(dl, "dig_att", P_DIG_ATTACK, 510, 380, 0.01f, 500, "ATTACK", "%.2f", " ms");
        knob(dl, "dig_rel", P_DIG_RELEASE, 640, 380, 1, 5000, "RELEASE", "%.0f", " ms");
        knob(dl, "dig_look", P_DIG_LOOK, 770, 380, 0, 10, "LOOKAHEAD", "%.1f", " ms");
        knob(dl, "dig_mix", P_DIG_MIX, 900, 380, 0, 100, "MIX", "%.0f", "%");
        knob(dl, "dig_out", P_DIG_OUT, 1030, 380, -24, 24, "OUTPUT", "%.1f", " dB");
        panel.toggle("dig_adapt", P_DIG_ADAPT, 475, 448, 645, 475, values[P_DIG_ADAPT], "ADAPTIVE RELEASE");
    }

    void drawMultiband(ImDrawList* dl)
    {
        const float left = 28, right = kDesignW - 28, top = 342, bottom = 445;
        dl->AddRectFilled(panel.P(left, top), panel.P(right, bottom), IM_COL32(22, 24, 29, 255), 5 * panel.scale());
        panel.text(dl, 40, top + 10, 10, kDim, "CROSSOVERS / GAIN REDUCTION", -1, true);
        crossover(dl, P_X1, 180, 20, 500, "XOVER 1");
        crossover(dl, P_X2, 430, 200, 5000, "XOVER 2");
        crossover(dl, P_X3, 680, 2000, 16000, "XOVER 3");
        for (int b = 0; b < 4; ++b)
        {
            char id[16];
            auto bandId = [&id, b](const char* prefix) { std::snprintf(id, sizeof(id), "%s%d", prefix, b); return id; };
            const float x = 46 + b * 264.0f;
            const uint32_t base = static_cast<uint32_t>(multicompp::kBandBase + b * 8);
            dl->AddRectFilled(panel.P(x, 462), panel.P(x + 250, 730), IM_COL32(30, 32, 38, 255), 5 * panel.scale());
            dl->AddRect(panel.P(x, 462), panel.P(x + 250, 730), kBandColors[b], 5 * panel.scale(), 0, panel.scale());
            panel.text(dl, x + 12, 475, 12, kBandColors[b], bandName(b), -1, true);
            panel.toggle(bandId("mb_en"), base + 7,
                         x + 112, 470, x + 178, 494, values[base + 7], "ENABLE");
            panel.toggle(bandId("mb_bp"), base + 5,
                         x + 182, 470, x + 238, 494, values[base + 5], "BYP");
            drawMeter(dl, x + 22, 510, 26, 190, meter(kMeterBand0 + static_cast<uint32_t>(b)), kBandColors[b], "GR");
            knob(dl, bandId("mb_t"), base, x + 75, 550, -60, 0, "THRESH", "%.1f", " dB");
            knob(dl, bandId("mb_r"), base + 1, x + 145, 550, 1, 20, "RATIO", "%.1f", ":1");
            knob(dl, bandId("mb_a"), base + 2, x + 215, 550, 0.1f, 100, "ATTACK", "%.1f", " ms");
            knob(dl, bandId("mb_rel"), base + 3, x + 75, 650, 10, 1000, "RELEASE", "%.0f", " ms");
            knob(dl, bandId("mb_m"), base + 4, x + 145, 650, -12, 12, "MAKEUP", "%.1f", " dB");
            panel.toggle(bandId("mb_s"), base + 6,
                         x + 180, 640, x + 238, 666, values[base + 6], "SOLO");
        }
        knob(dl, "mb_mix", P_MB_MIX, 900, 395, 0, 100, "MB MIX", "%.0f", "%");
        knob(dl, "mb_out", P_MB_OUT, 1005, 395, -24, 24, "MB OUTPUT", "%.1f", " dB");
    }

    static constexpr const char* busAttackLabels[6] = {"0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms"};
    static constexpr const char* busReleaseLabels[5] = {"0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto"};

    void crossover(ImDrawList* dl, uint32_t p, float x, float min, float max, const char* label)
    {
        const float trackStart = x - 100.0f;
        const float trackEnd = x + 100.0f;
        const float t = std::clamp((value(p) - min) / (max - min), 0.0f, 1.0f);
        const float handleX = trackStart + t * (trackEnd - trackStart);
        dl->AddLine(panel.P(trackStart, 392), panel.P(trackEnd, 392), IM_COL32(70, 75, 84, 255), 4 * panel.scale());
        dl->AddLine(panel.P(handleX, 376), panel.P(handleX, 408), kAccent, 3 * panel.scale());
        panel.text(dl, handleX, 414, 9, kText, label, 0, true);
        char text[32]; std::snprintf(text, sizeof(text), "%.0f Hz", static_cast<double>(value(p)));
        panel.text(dl, handleX, 428, 9, kDim, text, 0);
        ImGui::SetCursorScreenPos(panel.P(handleX - 12, 365));
        char handleId[48];
        std::snprintf(handleId, sizeof(handleId), "##mc_%s", label);
        ImGui::InvisibleButton(handleId, ImVec2(24 * panel.scale(), 54 * panel.scale()));
        if (ImGui::IsItemActivated()) editParameter(p, true);
        if (ImGui::IsItemActive())
        {
            const float mouseX = (ImGui::GetMousePos().x - panel.P(0, 0).x) / panel.scale();
            const float next = std::clamp(min + (mouseX - trackStart) / (trackEnd - trackStart) * (max - min), min, max);
            currentPreset = -1;
            values[p] = next;
            setParameterValue(p, next);
        }
        if (ImGui::IsItemDeactivated()) editParameter(p, false);
    }

    float meter(uint32_t p) const
    {
        if (p >= values.size()) return 0.0f;
        void* instance = getPluginInstancePointer();
        if (instance != nullptr)
        {
            if (p == kMeterMaster && multiCompGetGainReduction != nullptr) return multiCompGetGainReduction(instance);
            if (p >= kMeterBand0 && p <= kMeterBand0 + 3 && multiCompGetBandGainReduction != nullptr)
                return multiCompGetBandGainReduction(instance, static_cast<int>(p - kMeterBand0));
        }
        return values[p];
    }

    void drawMeter(ImDrawList* dl, float x, float y, float w, float h, float gr, ImU32 color, const char* label)
    {
        dl->AddRectFilled(panel.P(x, y), panel.P(x + w, y + h), IM_COL32(12, 13, 16, 255), 3 * panel.scale());
        const float amount = std::clamp(-gr / 20.0f, 0.0f, 1.0f);
        constexpr int segments = 16;
        const float gap = 2.0f;
        const float sh = (h - gap * (segments - 1) - 8) / segments;
        for (int i = 0; i < segments; ++i)
        {
            const float sy = y + h - 5 - (i + 1) * sh - i * gap;
            const bool lit = static_cast<float>(i) < amount * segments;
            const ImU32 col = lit ? (i > 12 ? IM_COL32(236, 72, 64, 255) : i > 8 ? IM_COL32(241, 184, 66, 255) : color)
                                  : IM_COL32(43, 45, 51, 255);
            dl->AddRectFilled(panel.P(x + 4, sy), panel.P(x + w - 4, sy + sh), col, 1.5f * panel.scale());
        }
        panel.text(dl, x + w * 0.5f, y + h + 5, 9, kDim, label, 0, true);
        char text[24]; std::snprintf(text, sizeof(text), "%.1f dB", static_cast<double>(gr));
        panel.text(dl, x + w * 0.5f, y + h + 18, 9, kText, text, 0);
    }

    void applyPreset(int index)
    {
        if (index < 0 || index >= static_cast<int>(multicompp::kFactoryPresets.size())) return;
        const auto& q = multicompp::kFactoryPresets[static_cast<size_t>(index)];
        multicompp::forEachPresetParam(q,
            [this](multicompp::CoreParameter parameter, float value)
            {
                const int index = multicompp::coreParamIndex(parameter);
                if (index >= 0) setValue(static_cast<uint32_t>(index), value);
            },
            [this](int band, int field, float value)
            {
                setValue(static_cast<uint32_t>(multicompp::kBandBase + band * 8 + field), value);
            });
        currentPreset = index;
    }
};

UI* createUI() { return new MultiCompUI(); }

END_NAMESPACE_DISTRHO

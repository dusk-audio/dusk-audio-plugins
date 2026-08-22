// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Multi-Comp 2 Dear ImGui UI.  The controls mirror EnhancedCompressorEditor and
// ModernCompressorPanels; the DSP and parameter ownership remain in the shell/core.

#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace multicompp::ui_detail
{
inline int choiceIndex(float hostValue, int count) noexcept
{
    return std::clamp(static_cast<int>(std::round(hostValue)), 0, count - 1);
}

struct OptoFaceplateLayout
{
    static constexpr float left = 34.0f;
    static constexpr float right = 1086.0f;
    static constexpr float top = 362.0f;
    static constexpr float bottom = 653.0f;
};

inline constexpr float optoFaceplateAspect() noexcept
{
    return (OptoFaceplateLayout::right - OptoFaceplateLayout::left)
        / (OptoFaceplateLayout::bottom - OptoFaceplateLayout::top);
}

inline float optoMeterNeedleAngle(float gainReductionDb) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    const float amount = std::clamp(-gainReductionDb / 20.0f, 0.0f, 1.0f);
    return (55.0f - 110.0f * amount) * pi / 180.0f;
}

inline const char* optoModeLabel(float hostValue) noexcept
{
    return choiceIndex(hostValue, 2) == 0 ? "COMP" : "LIMIT";
}

inline constexpr const char* optoMeterLabel() noexcept { return "GR"; }

template <size_t N>
inline int loadProgramIntoMirror(uint32_t index, std::array<float, N>& values)
{
    if (index >= kFactoryPresets.size()) return -1;
    applyPresetToHostParameters(kFactoryPresets[index],
        [&values](int parameterIndex, float hostValue)
        {
            if (parameterIndex >= 0 && static_cast<size_t>(parameterIndex) < values.size())
                values[static_cast<size_t>(parameterIndex)] = hostValue;
        });
    return static_cast<int>(index);
}

// Mirrors the plugin's ordered crossover set, so raising one handle also shows
// the DSP pushing its neighbours. skipIndex excludes the handle the user is
// dragging: only the AU wrapper applies a UI parameter write synchronously
// (CLAP queues it to the next process() call, LV2 to an atom port), so reading
// that one back mid-gesture returns the pre-edit value and drags the handle
// backwards under the mouse. Its neighbours have no such pending write.
template <size_t N, typename ReadParameter>
inline void refreshCrossoverMirror(std::array<float, N>& values, ReadParameter readParameter,
                                   uint32_t skipIndex = ~uint32_t(0))
{
    const auto x1 = static_cast<uint32_t>(ParamId::Crossover1);
    const auto x3 = static_cast<uint32_t>(ParamId::Crossover3);
    if (x3 >= N) return;
    for (uint32_t index = x1; index <= x3; ++index)
        if (index != skipIndex)
            values[index] = readParameter(index);
}
} // namespace multicompp::ui_detail

#ifndef MULTICOMP_UI_LOGIC_TEST

#include "DafUI.hpp"
#include "MultiCompAccess.hpp"
#include "MultiCompVersion.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"
#include "DuskSupportersOverlay.hpp"

#include <cstdio>
#include <cstring>

DUSK_WEAK float multiCompGetParameterValue(void* pluginInstancePointer,
                                            uint32_t index) noexcept;

START_NAMESPACE_DAF

namespace
{
constexpr float kDesignW = 1120.0f;
constexpr float kDesignH = 760.0f;
constexpr float kHeaderH = 64.0f;
constexpr float kGlobalH = 132.0f;
constexpr float kSidechainH = 126.0f;
constexpr float kPanelTop = kHeaderH + kGlobalH + kSidechainH;
constexpr float kUiPi = duskdaf::DuskPanel::kPi;
// Set to true only while collecting a host geometry measurement.
constexpr bool kGeometryDiagnosticEnabled = false;

constexpr ImU32 kPanel = IM_COL32(35, 37, 43, 255);
constexpr ImU32 kPanelRaised = IM_COL32(43, 46, 54, 255);
constexpr ImU32 kLine = IM_COL32(75, 79, 90, 255);
constexpr ImU32 kText = IM_COL32(232, 234, 238, 255);
constexpr ImU32 kDim = IM_COL32(162, 168, 180, 255);
constexpr ImU32 kAccent = IM_COL32(65, 194, 220, 255);
constexpr ImU32 kOptoFace = IM_COL32(211, 204, 184, 255);
constexpr ImU32 kOptoFaceLight = IM_COL32(235, 229, 208, 255);
constexpr ImU32 kOptoInk = IM_COL32(43, 41, 37, 255);
constexpr ImU32 kOptoTrim = IM_COL32(92, 72, 54, 255);
constexpr ImU32 kOptoRed = IM_COL32(159, 48, 39, 255);
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
constexpr uint32_t P_SC_LISTEN = MC_PID(GlobalSidechainListen), P_MB_MIX = MC_PID(MbMix), P_MB_OUT = MC_PID(MbOutput);
constexpr uint32_t P_NOISE = MC_PID(NoiseEnable), P_SC_LOW_FREQ = MC_PID(ScLowFreq), P_SC_LOW_GAIN = MC_PID(ScLowGain);
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

class MultiCompUI final : public UI, public duskdaf::ParamHost
{
public:
    MultiCompUI()
        : UI(DAF_UI_DEFAULT_WIDTH, DAF_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < multicompp::kTotalParamCount; ++i)
        {
            if (i < static_cast<uint32_t>(multicompp::kParamCount))
                values[i] = multicompp::hostDefault(multicompp::kParams[i]);
            else
                values[i] = 0.0f;
        }
        for (int b = 0; b < duskaudio::kMultiCompBands; ++b)
            for (int f = 0; f < 8; ++f)
                values[multicompp::kBandBase + b * 8 + f] =
                    multicompp::hostDefault(multicompp::bandParam(f, b));

        // Match the fixed design space and keep host-driven resizes on its aspect.
        setGeometryConstraints(static_cast<uint32_t>(kDesignW * 0.5f),
                               static_cast<uint32_t>(kDesignH * 0.5f), true);
        static const float kFontSizes[] = {9.f, 11.f, 13.f, 16.f, 20.f, 26.f, 30.f};
        fontSet = duskdaf::loadCrispFontSet(kFontSizes, 7, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
    }

    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override { editParameter(idx, false); }
    void setParam(uint32_t idx, float value) override
    {
        if (idx >= static_cast<uint32_t>(multicompp::kMeterMaster)) return;
        currentPreset = -1;
        values[idx] = value;
        setParameterValue(idx, value);
        if (idx >= P_X1 && idx <= P_X3) refreshCrossoverMirror();
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values.size()) values[index] = value;
        if (index >= P_X1 && index <= P_X3) refreshCrossoverMirror();
    }

    void stateChanged(const char* key, const char* state) override
    {
        if (key == nullptr || state == nullptr || std::strcmp(key, "parameters") != 0) return;
        multicompp::StateValues decoded{};
        if (!multicompp::decodeState(state, decoded)) return;
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
            values[static_cast<size_t>(i)] = decoded[static_cast<size_t>(i)];
        currentPreset = -1;
        repaint();
    }

    void programLoaded(uint32_t index) override
    {
        currentPreset = multicompp::ui_detail::loadProgramIntoMirror(index, values);
    }

    void uiIdle() override
    {
        refreshCrossoverMirror();
        repaint();
    }

    void onImGuiDisplay() override
    {
        // The crossovers are only submitted in Multiband mode. If the host
        // switches Mode away mid-drag the widget stops being drawn, so ImGui
        // never reports IsItemDeactivated for it: the gesture would stay open on
        // the host and the mirror would skip that crossover for the rest of the
        // session. Close it here instead, from the fact that nothing re-armed
        // the flag on the previous frame.
        if (draggedCrossover != kNoCrossover && !draggedCrossoverStillDrawn)
        {
            editParameter(draggedCrossover, false);
            draggedCrossover = kNoCrossover;
        }
        draggedCrossoverStillDrawn = false;

        const float winW = static_cast<float>(getWidth());
        const float winH = static_cast<float>(getHeight());
        const float scale = std::min(winW / kDesignW, winH / kDesignH);
        const ImVec2 origin((winW - kDesignW * scale) * 0.5f,
                            (winH - kDesignH * scale) * 0.5f);
        if (kGeometryDiagnosticEnabled && !geometryDiagnosticLogged)
        {
            GLint viewport[4]{};
            glGetIntegerv(GL_VIEWPORT, viewport);
            std::fprintf(stderr,
                         "[MultiCompUI geometry] width=%u height=%u scaleFactor=%.3f "
                         "design=%.0fx%.0f scale=%.6f origin=(%.3f,%.3f) "
                         "glViewport=(%d,%d %dx%d)\n",
                         static_cast<unsigned>(getWidth()),
                         static_cast<unsigned>(getHeight()),
                         static_cast<double>(getScaleFactor()),
                         static_cast<double>(kDesignW), static_cast<double>(kDesignH),
                         static_cast<double>(scale), static_cast<double>(origin.x),
                         static_cast<double>(origin.y), viewport[0], viewport[1],
                         viewport[2], viewport[3]);
            std::fflush(stderr);
            geometryDiagnosticLogged = true;
        }
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
            duskdaf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH, showSupporters,
                                           "Multi-Comp 2", MULTICOMP2_VERSION_STRING,
                                           &supporters);

        const duskdaf::ResizeGripState grip =
            panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH, 0.5f);
        ImGui::End();
        ImGui::PopStyleVar(2);
        if (grip.resized) setSize(grip.width, grip.height);
    }

private:
    std::array<float, multicompp::kTotalParamCount> values{};
    duskdaf::DuskPanel panel;
    ImDrawListSplitter optoKnobSplitter;
    duskdaf::CrispFontSet fontSet;
    ImFont* labelFont = nullptr;
    duskdaf::SupportersOverlay supporters;
    bool showSupporters = false;
    bool geometryDiagnosticLogged = false;
    static constexpr uint32_t kNoCrossover = ~uint32_t(0);
    // The crossover handle under an active drag. Owns both the open automation
    // gesture and the mirror's skip, so the two cannot disagree.
    uint32_t draggedCrossover = kNoCrossover;
    // Set by crossover() on every frame it submits the held handle, and read
    // once per frame to notice a handle that stopped being drawn mid-drag.
    bool draggedCrossoverStillDrawn = false;
    int currentPreset = -1;

    float value(uint32_t p) const { return values[p]; }

    void refreshCrossoverMirror()
    {
        if (multiCompGetParameterValue == nullptr) return;
        void* const instance = getPluginInstancePointer();
        if (instance == nullptr) return;
        multicompp::ui_detail::refreshCrossoverMirror(values,
            [instance](uint32_t index) { return multiCompGetParameterValue(instance, index); },
            draggedCrossover);
    }

    void setValue(uint32_t p, float v)
    {
        currentPreset = -1;
        editParameter(p, true);
        const float hostValue = hostValueForPlain(p, v);
        values[p] = hostValue;
        setParameterValue(p, hostValue);
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
        panel.text(dl, 24, 40, 10, kDim,
                   modeName(multicompp::ui_detail::choiceIndex(value(P_MODE), 8)), -1);
        panel.text(dl, kDesignW - 20, 20, 11, kDim, "Dusk Audio", 1);
        ImGui::SetCursorScreenPos(panel.P(12, 7));
        ImGui::InvisibleButton("##mc_title", ImVec2(190 * panel.scale(), 44 * panel.scale()));
        if (ImGui::IsItemClicked()) showSupporters = true;
    }

    bool combo(const char* id, uint32_t p, const char* const* labels, int count,
               float x, float y, float w, const char* caption)
    {
        panel.text(ImGui::GetWindowDrawList(), x, y, 9.5f, kDim, caption, 0, true);
        const int selected = multicompp::ui_detail::choiceIndex(value(p), count);
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
        knob(dl, "mc_mix", P_MIX, 78, 125, "MIX", "%.0f", "%");
        knob(dl, "mc_link", P_LINK, 166, 125, "STEREO LINK", "%.0f", "%");
        combo("mc_linkmode", P_LINK_MODE, multicompp::kLinkMode, 3, 270, 103, 112, "LINK MODE");
        combo("mc_os", P_OS, multicompp::kOversampling, 3, 394, 103, 96, "OVERSAMPLE");
        knob(dl, "mc_look", P_LOOK, 510, 125, "LOOKAHEAD", "%.1f", " ms");
        panel.toggle("mc_auto", P_AUTO, 568, 111, 680, 136, values[P_AUTO], "AUTO MAKEUP");
        panel.toggle("mc_tp", P_TRUE_PEAK, 688, 111, 790, 136, values[P_TRUE_PEAK], "TRUE PEAK");
        combo("mc_tpq", P_TP_QUALITY, multicompp::kTruePeakQuality, 2, 845, 103, 110, "QUALITY");
        const bool driveApplicable = multicompp::ui_detail::choiceIndex(value(P_DIST), 4) != 0;
        knob(dl, "mc_dist", P_DIST_AMT, 1080, 125, "DRIVE", "%.0f", "%",
             driveApplicable);
    }

    void drawSidechain(ImDrawList* dl)
    {
        drawSection(dl, kHeaderH + kGlobalH + 4, kPanelTop - 4, "SIDECHAIN");
        panel.toggle("mc_ext", P_EXT_SC, 24, 223, 142, 248, values[P_EXT_SC], "EXTERNAL SC");
        panel.toggle("mc_listen", P_SC_LISTEN, 24, 255, 142, 280, values[P_SC_LISTEN], "LISTEN");
        knob(dl, "mc_schp", P_SC_HP, 190, 251, "HP FILTER", "%.0f", " Hz");
        knob(dl, "mc_sclf", P_SC_LOW_FREQ, 300, 251, "LOW FREQ", "%.0f", " Hz");
        knob(dl, "mc_sclg", P_SC_LOW_GAIN, 410, 251, "LOW GAIN", "%.1f", " dB");
        knob(dl, "mc_schf", P_SC_HIGH_FREQ, 520, 251, "HIGH FREQ", "%.0f", " Hz");
        knob(dl, "mc_schg", P_SC_HIGH_GAIN, 630, 251, "HIGH GAIN", "%.1f", " dB");
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
              const char* label, const char* fmt, const char* suffix,
              bool applicable = true)
    {
        if (!applicable) ImGui::BeginDisabled();
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, 25, values[p],
                   hostDefaultValue(p), false, true,
                   fmt, suffix, kPanelRaised, false, true, nullptr, false, 1.0f, 0.0f,
                   nullptr, false, nullptr, false, 0.0f, 0.0f, false, false, 9.5f,
                   false, false, &knobHostToPlain, &knobPlainToHost, this);
        if (!applicable)
        {
            ImGui::EndDisabled();
            dl->AddCircleFilled(panel.P(x, y), 33.0f * panel.scale(),
                                IM_COL32(21, 22, 25, 145), 48);
        }
        panel.text(dl, x, y + 49, 9.5f, applicable ? kText : kDim, label, 0, true);
    }

    float hostDefaultValue(uint32_t p) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
            [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });
    }

    float hostMinimum(uint32_t p) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [](const multicompp::Param& d) { return multicompp::hostMin(d); },
            [](const multicompp::BandParam& d, int) { return multicompp::hostMin(d); });
    }

    float hostMaximum(uint32_t p) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [](const multicompp::Param& d) { return multicompp::hostMax(d); },
            [](const multicompp::BandParam& d, int) { return multicompp::hostMax(d); });
    }

    float plainValueForHost(uint32_t p, float host) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [host](const multicompp::Param& d) { return multicompp::hostToPlain(d, host); },
            [host](const multicompp::BandParam& d, int) { return multicompp::hostToPlain(d, host); });
    }

    float hostValueForPlain(uint32_t p, float plain) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [plain](const multicompp::Param& d) { return multicompp::plainToHost(d, plain); },
            [plain](const multicompp::BandParam& d, int) { return multicompp::plainToHost(d, plain); });
    }

    static float knobHostToPlain(float host, uint32_t p, void* context)
    {
        return static_cast<MultiCompUI*>(context)->plainValueForHost(p, host);
    }

    static float knobPlainToHost(float plain, uint32_t p, void* context)
    {
        return static_cast<MultiCompUI*>(context)->hostValueForPlain(p, plain);
    }

    void drawModePanel(ImDrawList* dl)
    {
        const int mode = multicompp::ui_detail::choiceIndex(value(P_MODE), 8);
        drawSection(dl, kPanelTop, kDesignH - 8, modeName(mode));
        if (mode == 0)
        {
            // Opto owns a panel-integrated analogue GR meter.
        }
        else if (mode == 7)
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
        using Layout = multicompp::ui_detail::OptoFaceplateLayout;
        const float left = Layout::left;
        const float right = Layout::right;
        const float top = Layout::top;
        const float bottom = Layout::bottom;

        dl->AddRectFilled(panel.P(left + 4, top + 6), panel.P(right + 4, bottom + 6),
                          IM_COL32(0, 0, 0, 90), 7.0f * panel.scale());
        dl->AddRectFilled(panel.P(left, top), panel.P(right, bottom),
                          kOptoTrim, 7.0f * panel.scale());
        dl->AddRectFilled(panel.P(left + 5, top + 5), panel.P(right - 5, bottom - 5),
                          kOptoFace, 4.0f * panel.scale());
        dl->AddLine(panel.P(left + 8, top + 8), panel.P(right - 8, top + 8),
                    IM_COL32(255, 252, 235, 145), panel.scale());
        dl->AddLine(panel.P(left + 8, bottom - 8), panel.P(right - 8, bottom - 8),
                    IM_COL32(74, 60, 48, 150), panel.scale());

        for (const ImVec2 screw : {ImVec2(left + 18, top + 18),
                                   ImVec2(right - 18, top + 18),
                                   ImVec2(left + 18, bottom - 18),
                                   ImVec2(right - 18, bottom - 18)})
        {
            dl->AddCircleFilled(panel.P(screw.x, screw.y), 6.0f * panel.scale(),
                                IM_COL32(75, 73, 68, 255), 20);
            dl->AddCircleFilled(panel.P(screw.x - 1.0f, screw.y - 1.0f),
                                4.2f * panel.scale(), IM_COL32(177, 174, 163, 255), 20);
            dl->AddLine(panel.P(screw.x - 2.5f, screw.y + 1.0f),
                        panel.P(screw.x + 2.5f, screw.y - 1.0f),
                        IM_COL32(72, 69, 63, 255), panel.scale());
        }

        panel.text(dl, 560, top + 13, 13.0f, kOptoInk, "DUSK OPTO LEVELER", 0, true);
        panel.text(dl, 560, top + 31, 8.5f, IM_COL32(91, 84, 72, 255),
                   "PROGRAM-DEPENDENT LEVELING AMPLIFIER", 0, true);
        optoModeSwitch(dl, 112, 492);
        optoKnob(dl, "opto_gain", P_OPTO_GAIN, 264, 505, 58.0f, "GAIN");
        drawOptoMeter(dl, 389, 407, 324, 160, meter(kMeterMaster));
        optoKnob(dl, "opto_peak", P_OPTO_PEAK, 829, 505, 58.0f,
                 "PEAK REDUCTION");
        optoKnob(dl, "opto_mix", P_MIX, 1000, 505, 39.0f, "MIX", false);
    }

    void optoKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
                  float radius, const char* label, bool numberedScale = true)
    {
        // Put the shared gesture/editor layer above the custom body while
        // letting it mutate the value first, so the pointer and readout update
        // in the same frame. The active-only bubble exposes shift-fine values.
        optoKnobSplitter.Split(dl, 2);
        optoKnobSplitter.SetCurrentChannel(dl, 1);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, "%.0f", "%",
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this);
        optoKnobSplitter.SetCurrentChannel(dl, 0);

        const float plain = plainValueForHost(p, values[p]);
        const float minimum = plainValueForHost(p, hostMinimum(p));
        const float maximum = plainValueForHost(p, hostMaximum(p));
        const float t = std::clamp((plain - minimum)
            / std::max(maximum - minimum, 1.0e-6f), 0.0f, 1.0f);
        const ImVec2 center = panel.P(x, y);
        const float scaledRadius = radius * panel.scale();

        for (int tick = 0; tick <= 10; ++tick)
        {
            const float angle = duskdaf::DuskPanel::knobAngle(
                static_cast<float>(tick) / 10.0f);
            const ImVec2 direction(std::sin(angle), -std::cos(angle));
            const float inner = scaledRadius + 5.0f * panel.scale();
            const float outer = scaledRadius
                + (tick % 5 == 0 ? 13.0f : 9.0f) * panel.scale();
            dl->AddLine(ImVec2(center.x + direction.x * inner,
                               center.y + direction.y * inner),
                        ImVec2(center.x + direction.x * outer,
                               center.y + direction.y * outer),
                        kOptoInk, (tick % 5 == 0 ? 1.8f : 1.0f) * panel.scale());
        }

        dl->AddCircleFilled(ImVec2(center.x + 2.0f * panel.scale(),
                                   center.y + 3.0f * panel.scale()),
                            scaledRadius, IM_COL32(0, 0, 0, 85), 56);
        dl->AddCircleFilled(center, scaledRadius, IM_COL32(28, 28, 27, 255), 56);
        for (int ridge = 0; ridge < 24; ++ridge)
        {
            const float angle = 2.0f * kUiPi
                * static_cast<float>(ridge) / 24.0f;
            const ImVec2 direction(std::sin(angle), -std::cos(angle));
            dl->AddLine(ImVec2(center.x + direction.x * scaledRadius * 0.82f,
                               center.y + direction.y * scaledRadius * 0.82f),
                        ImVec2(center.x + direction.x * scaledRadius * 0.97f,
                               center.y + direction.y * scaledRadius * 0.97f),
                        IM_COL32(116, 112, 102, 150), 1.4f * panel.scale());
        }
        dl->AddCircleFilled(center, scaledRadius * 0.72f,
                            IM_COL32(56, 55, 52, 255), 48);
        dl->AddCircleFilled(ImVec2(center.x - scaledRadius * 0.12f,
                                   center.y - scaledRadius * 0.14f),
                            scaledRadius * 0.57f, IM_COL32(78, 76, 70, 255), 42);
        dl->AddCircle(center, scaledRadius, IM_COL32(12, 12, 11, 255), 56,
                      1.5f * panel.scale());
        const float pointerAngle = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 pointer(std::sin(pointerAngle), -std::cos(pointerAngle));
        dl->AddLine(ImVec2(center.x + pointer.x * scaledRadius * 0.18f,
                           center.y + pointer.y * scaledRadius * 0.18f),
                    ImVec2(center.x + pointer.x * scaledRadius * 0.88f,
                           center.y + pointer.y * scaledRadius * 0.88f),
                    kOptoFaceLight, 3.2f * panel.scale());

        if (numberedScale)
        {
            panel.text(dl, x - radius - 16.0f, y + radius * 0.70f, 8.5f,
                       kOptoInk, "0", 0, true);
            panel.text(dl, x, y - radius - 24.0f, 8.5f, kOptoInk, "50", 0, true);
            panel.text(dl, x + radius + 16.0f, y + radius * 0.70f, 8.5f,
                       kOptoInk, "100", 0, true);
        }
        char readout[16];
        std::snprintf(readout, sizeof(readout), "%.0f%%", static_cast<double>(plain));
        dl->AddRectFilled(panel.P(x - 24.0f, y + radius + 10.0f),
                          panel.P(x + 24.0f, y + radius + 29.0f),
                          IM_COL32(43, 41, 37, 255), 2.0f * panel.scale());
        panel.text(dl, x, y + radius + 14.0f, 9.5f, kOptoFaceLight,
                   readout, 0, true);
        panel.text(dl, x, y + radius + 35.0f, numberedScale ? 10.0f : 9.0f,
                   kOptoInk, label, 0, true);
        optoKnobSplitter.Merge(dl);
    }

    void optoModeSwitch(ImDrawList* dl, float x, float y)
    {
        const ImVec2 p0 = panel.P(x - 38.0f, y - 82.0f);
        const ImVec2 p1 = panel.P(x + 38.0f, y + 78.0f);
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##opto_comp_limit", ImVec2(p1.x - p0.x, p1.y - p0.y));
        if (ImGui::IsItemClicked())
            setValue(P_OPTO_LIMIT, values[P_OPTO_LIMIT] > 0.5f ? 0.0f : 1.0f);

        const bool limit = multicompp::ui_detail::choiceIndex(
            values[P_OPTO_LIMIT], 2) == 1;
        constexpr ImU32 inactiveMode = IM_COL32(111, 103, 88, 255);
        panel.text(dl, x, y - 74.0f, 9.5f, limit ? kOptoRed : inactiveMode,
                   multicompp::ui_detail::optoModeLabel(1.0f), 0, true);
        panel.text(dl, x, y + 65.0f, 9.5f, !limit ? kOptoRed : inactiveMode,
                   multicompp::ui_detail::optoModeLabel(0.0f), 0, true);
        dl->AddRectFilled(panel.P(x - 14.0f, y - 42.0f),
                          panel.P(x + 14.0f, y + 42.0f),
                          IM_COL32(44, 42, 38, 255), 5.0f * panel.scale());
        dl->AddRect(panel.P(x - 14.0f, y - 42.0f),
                    panel.P(x + 14.0f, y + 42.0f),
                    IM_COL32(18, 18, 17, 255), 5.0f * panel.scale(), 0,
                    1.5f * panel.scale());
        const float leverY = y + (limit ? -24.0f : 24.0f);
        dl->AddLine(panel.P(x, y), panel.P(x, leverY),
                    IM_COL32(168, 164, 151, 255), 5.0f * panel.scale());
        dl->AddCircleFilled(panel.P(x, leverY), 8.0f * panel.scale(),
                            IM_COL32(203, 198, 183, 255), 24);
        dl->AddCircle(panel.P(x, leverY), 8.0f * panel.scale(),
                      IM_COL32(34, 33, 30, 255), 24, 1.2f * panel.scale());
        panel.text(dl, x, y + 93.0f, 8.5f, IM_COL32(91, 84, 72, 255),
                   multicompp::ui_detail::optoModeLabel(values[P_OPTO_LIMIT]),
                   0, true);
    }

    void drawOptoMeter(ImDrawList* dl, float x, float y, float w, float h, float gr)
    {
        dl->AddRectFilled(panel.P(x + 3.0f, y + 4.0f),
                          panel.P(x + w + 3.0f, y + h + 4.0f),
                          IM_COL32(0, 0, 0, 90), 5.0f * panel.scale());
        dl->AddRectFilled(panel.P(x, y), panel.P(x + w, y + h),
                          IM_COL32(47, 45, 41, 255), 5.0f * panel.scale());
        dl->AddRectFilled(panel.P(x + 8.0f, y + 8.0f),
                          panel.P(x + w - 8.0f, y + h - 8.0f),
                          kOptoFaceLight, 2.0f * panel.scale());
        const ImVec2 pivot = panel.P(x + w * 0.5f, y + h - 13.0f);
        const float radius = 118.0f * panel.scale();
        constexpr std::array<const char*, 5> labels{{"20", "15", "10", "5", "0"}};
        for (size_t tick = 0; tick < labels.size(); ++tick)
        {
            const float amount = static_cast<float>(tick)
                / static_cast<float>(labels.size() - 1);
            const float angle = (-55.0f + 110.0f * amount)
                * kUiPi / 180.0f;
            const ImVec2 direction(std::sin(angle), -std::cos(angle));
            dl->AddLine(ImVec2(pivot.x + direction.x * radius * 0.76f,
                               pivot.y + direction.y * radius * 0.76f),
                        ImVec2(pivot.x + direction.x * radius * 0.88f,
                               pivot.y + direction.y * radius * 0.88f),
                        kOptoInk, 1.4f * panel.scale());
            panel.text(dl,
                       x + w * 0.5f + direction.x * 76.0f,
                       y + h - 17.0f - std::cos(angle) * 76.0f,
                       8.0f, kOptoInk, labels[tick], 0, true);
        }
        panel.text(dl, x + w * 0.5f, y + 16.0f, 11.0f, kOptoRed,
                   multicompp::ui_detail::optoMeterLabel(), 0, true);
        const float needleAngle = multicompp::ui_detail::optoMeterNeedleAngle(gr);
        const ImVec2 needle(std::sin(needleAngle), -std::cos(needleAngle));
        dl->AddLine(pivot,
                    ImVec2(pivot.x + needle.x * radius * 0.87f,
                           pivot.y + needle.y * radius * 0.87f),
                    kOptoRed, 2.2f * panel.scale());
        dl->AddCircleFilled(pivot, 7.0f * panel.scale(), kOptoInk, 24);
        char reading[24];
        std::snprintf(reading, sizeof(reading), "%s %.1f dB",
                      multicompp::ui_detail::optoMeterLabel(),
                      static_cast<double>(std::clamp(-gr, 0.0f, 99.9f)));
        panel.text(dl, x + w * 0.5f, y + h + 10.0f, 8.5f, kOptoInk,
                   reading, 0, true);
    }

    void drawFet(ImDrawList* dl, bool studio)
    {
        knob(dl, studio ? "sf_in" : "fet_in", P_FET_IN, 120, 380, "INPUT", "%.1f", " dB");
        knob(dl, studio ? "sf_out" : "fet_out", P_FET_OUT, 250, 380, "OUTPUT", "%.1f", " dB");
        knob(dl, studio ? "sf_att" : "fet_att", P_FET_ATTACK, 380, 380, "ATTACK", "%.2f", " ms");
        knob(dl, studio ? "sf_rel" : "fet_rel", P_FET_RELEASE, 510, 380, "RELEASE", "%.0f", " ms");
        knob(dl, studio ? "sf_mix" : "fet_mix", P_MIX, 640, 380, "MIX", "%.0f", "%");
        combo(studio ? "sf_ratio" : "fet_ratio", P_FET_RATIO, multicompp::kRatios, 5, 775, 352, 118, "RATIO");
        combo(studio ? "sf_curve" : "fet_curve", P_FET_CURVE, multicompp::kFetCurve, 2, 905, 352, 142, "CURVE");
        knob(dl, studio ? "sf_trans" : "fet_trans", P_FET_TRANSIENT, 1020, 380, "TRANSIENT", "%.0f", "%");
        panel.text(dl, 560, 500, 12, studio ? IM_COL32(80, 215, 205, 255) : IM_COL32(235, 175, 80, 255),
                   studio ? "Clean FET response with controlled harmonics" : "Fast FET response with program-dependent release", 0);
    }

    void drawVca(ImDrawList* dl)
    {
        knob(dl, "vca_thr", P_VCA_THRESHOLD, 170, 380, "THRESHOLD", "%.1f", " dB");
        knob(dl, "vca_ratio", P_VCA_RATIO, 320, 380, "RATIO", "%.1f", ":1");
        knob(dl, "vca_att", P_VCA_ATTACK, 470, 380, "ATTACK", "%.1f", " ms");
        knob(dl, "vca_rel", P_VCA_RELEASE, 620, 380, "RELEASE", "%.0f", " ms");
        knob(dl, "vca_out", P_VCA_OUT, 770, 380, "OUTPUT", "%.1f", " dB");
        knob(dl, "vca_mix", P_MIX, 920, 380, "MIX", "%.0f", "%");
        panel.toggle("vca_over", P_VCA_OVER_EASY, 860, 450, 1000, 476, values[P_VCA_OVER_EASY], "OVER EASY");
        combo("vca_detector", P_VCA_DETECTOR, multicompp::kVcaDetector, 2, 670, 450, 170, "DETECTOR");
    }

    void drawBus(ImDrawList* dl)
    {
        knob(dl, "bus_thr", P_BUS_THRESHOLD, 160, 380, "THRESHOLD", "%.1f", " dB");
        combo("bus_ratio", P_BUS_RATIO, multicompp::kBusRatios, 3, 300, 352, 100, "RATIO");
        combo("bus_attack", P_BUS_ATTACK, multicompp::kBusAttack, 6, 440, 352, 108, "ATTACK");
        combo("bus_release", P_BUS_RELEASE, multicompp::kBusRelease, 5, 580, 352, 108, "RELEASE");
        knob(dl, "bus_makeup", P_BUS_MAKEUP, 720, 380, "MAKEUP", "%.1f", " dB");
        knob(dl, "bus_mix", P_BUS_MIX, 860, 380, "BUS MIX", "%.0f", "%");
        panel.text(dl, 560, 500, 12, IM_COL32(152, 190, 228, 255), "Gentle bus glue with linked stereo detection", 0);
    }

    void drawStudioVca(ImDrawList* dl)
    {
        knob(dl, "sv_thr", P_SVCA_THRESHOLD, 150, 380, "THRESHOLD", "%.1f", " dB");
        knob(dl, "sv_ratio", P_SVCA_RATIO, 300, 380, "RATIO", "%.1f", ":1");
        knob(dl, "sv_att", P_SVCA_ATTACK, 450, 380, "ATTACK", "%.1f", " ms");
        knob(dl, "sv_rel", P_SVCA_RELEASE, 600, 380, "RELEASE", "%.0f", " ms");
        knob(dl, "sv_out", P_SVCA_OUT, 750, 380, "OUTPUT", "%.1f", " dB");
        knob(dl, "sv_mix", P_MIX, 900, 380, "MIX", "%.0f", "%");
        panel.text(dl, 560, 500, 12, IM_COL32(220, 120, 125, 255), "RMS detection, soft knee, precision VCA control", 0);
    }

    void drawDigital(ImDrawList* dl)
    {
        knob(dl, "dig_thr", P_DIG_THRESHOLD, 120, 380, "THRESHOLD", "%.1f", " dB");
        knob(dl, "dig_ratio", P_DIG_RATIO, 250, 380, "RATIO", "%.1f", ":1");
        knob(dl, "dig_knee", P_DIG_KNEE, 380, 380, "KNEE", "%.1f", " dB");
        knob(dl, "dig_att", P_DIG_ATTACK, 510, 380, "ATTACK", "%.2f", " ms");
        knob(dl, "dig_rel", P_DIG_RELEASE, 640, 380, "RELEASE", "%.0f", " ms");
        knob(dl, "dig_look", P_DIG_LOOK, 770, 380, "LOOKAHEAD", "%.1f", " ms");
        knob(dl, "dig_mix", P_DIG_MIX, 900, 380, "MIX", "%.0f", "%");
        knob(dl, "dig_out", P_DIG_OUT, 1030, 380, "OUTPUT", "%.1f", " dB");
        panel.toggle("dig_adapt", P_DIG_ADAPT, 475, 448, 645, 475, values[P_DIG_ADAPT], "ADAPTIVE RELEASE");
    }

    void drawMultiband(ImDrawList* dl)
    {
        const float left = 28, right = kDesignW - 28, top = 342, bottom = 445;
        dl->AddRectFilled(panel.P(left, top), panel.P(right, bottom), IM_COL32(22, 24, 29, 255), 5 * panel.scale());
        panel.text(dl, 40, top + 10, 10, kDim, "CROSSOVERS / GAIN REDUCTION", -1, true);
        crossover(dl, P_X1, 180, "XOVER 1");
        crossover(dl, P_X2, 430, "XOVER 2");
        crossover(dl, P_X3, 680, "XOVER 3");
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
            knob(dl, bandId("mb_t"), base, x + 75, 550, "THRESH", "%.1f", " dB");
            knob(dl, bandId("mb_r"), base + 1, x + 145, 550, "RATIO", "%.1f", ":1");
            knob(dl, bandId("mb_a"), base + 2, x + 215, 550, "ATTACK", "%.1f", " ms");
            knob(dl, bandId("mb_rel"), base + 3, x + 75, 650, "RELEASE", "%.0f", " ms");
            knob(dl, bandId("mb_m"), base + 4, x + 145, 650, "MAKEUP", "%.1f", " dB");
            panel.toggle(bandId("mb_s"), base + 6,
                         x + 180, 640, x + 238, 666, values[base + 6], "SOLO");
        }
        knob(dl, "mb_mix", P_MB_MIX, 900, 395, "MB MIX", "%.0f", "%");
        knob(dl, "mb_out", P_MB_OUT, 1005, 395, "MB OUTPUT", "%.1f", " dB");
    }

    void crossover(ImDrawList* dl, uint32_t p, float x, const char* label)
    {
        const float trackStart = x - 100.0f;
        const float trackEnd = x + 100.0f;
        const float physicalValue = multicompp::hostToPlain(multicompp::kParams[p], values[p]);
        const float t = std::clamp(values[p], 0.0f, 1.0f);
        const float handleX = trackStart + t * (trackEnd - trackStart);
        dl->AddLine(panel.P(trackStart, 392), panel.P(trackEnd, 392), IM_COL32(70, 75, 84, 255), 4 * panel.scale());
        dl->AddLine(panel.P(handleX, 376), panel.P(handleX, 408), kAccent, 3 * panel.scale());
        panel.text(dl, handleX, 414, 9, kText, label, 0, true);
        char text[32]; std::snprintf(text, sizeof(text), "%.0f Hz", static_cast<double>(physicalValue));
        panel.text(dl, handleX, 428, 9, kDim, text, 0);
        ImGui::SetCursorScreenPos(panel.P(handleX - 12, 365));
        char handleId[48];
        std::snprintf(handleId, sizeof(handleId), "##mc_%s", label);
        ImGui::InvisibleButton(handleId, ImVec2(24 * panel.scale(), 54 * panel.scale()));
        if (ImGui::IsItemActivated()) { editParameter(p, true); draggedCrossover = p; }
        if (draggedCrossover == p) draggedCrossoverStillDrawn = true;
        if (ImGui::IsItemActive())
        {
            const float mouseX = (ImGui::GetMousePos().x - panel.P(0, 0).x) / panel.scale();
            const float normalized = std::clamp(
                (mouseX - trackStart) / (trackEnd - trackStart), 0.0f, 1.0f);
            setParam(p, normalized);
        }
        if (ImGui::IsItemDeactivated())
        {
            editParameter(p, false);
            // Released: the pending write has had the gesture to land, and the
            // next refresh adopts whatever ordering the DSP settled on.
            if (draggedCrossover == p) draggedCrossover = kNoCrossover;
        }
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
                setValue(static_cast<uint32_t>(multicompp::kBandBase + band * 8 + field),
                         value);
            });
        currentPreset = index;
    }
};

UI* createUI() { return new MultiCompUI(); }

END_NAMESPACE_DAF

#endif // MULTICOMP_UI_LOGIC_TEST

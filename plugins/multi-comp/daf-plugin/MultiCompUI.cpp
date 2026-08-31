// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Multi-Comp 2 Dear ImGui UI.  The controls mirror EnhancedCompressorEditor and
// ModernCompressorPanels; the DSP and parameter ownership remain in the shell/core.

#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
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
    static constexpr float left = 0.0f;
    static constexpr float right = 1120.0f;
    static constexpr float top = 344.0f;
    static constexpr float bottom = 654.0f;
};

inline constexpr float optoFaceplateAspect() noexcept
{
    return (OptoFaceplateLayout::right - OptoFaceplateLayout::left)
        / (OptoFaceplateLayout::bottom - OptoFaceplateLayout::top);
}

inline float designHeightForMode(float hostValue) noexcept
{
    // Opto and vintage FET are single rack faces and use the same compact
    // canvas. The modern modes still need the original tall control surface;
    // keeping the decision here gives fractional host automation the same
    // rounding rule as the DSP and mode picker.
    return choiceIndex(hostValue, 8) <= 1 ? 380.0f : 486.0f;
}

inline float optoMeterNeedleAngle(float gainReductionDb) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    const float amount = std::clamp(-gainReductionDb / 20.0f, 0.0f, 1.0f);
    return (55.0f - 110.0f * amount) * pi / 180.0f;
}

inline float optoMeterDisplayValue(float gainReductionDb) noexcept
{
    // The Opto gain cell already carries the measured LA-2A attack and release.
    // The separate display decay was not measured from the reference and made
    // the needle return materially later than the audio. Keep only a finite guard.
    return std::isfinite(gainReductionDb) ? gainReductionDb : 0.0f;
}

inline constexpr const char* optoKnobValueSuffix() noexcept { return ""; }

inline bool fetTimingUsesDialReadout(uint32_t parameter) noexcept
{
    return parameter == static_cast<uint32_t>(ParamId::FetAttack)
        || parameter == static_cast<uint32_t>(ParamId::FetRelease);
}

// Vintage FET timing follows the reference unit's numbered 1-7 controls. Keep
// the legacy host ranges intact for automation/state compatibility and convert
// only the faceplate readout, gesture domain, and pointer position.
inline float fetTimingDialValue(float hostValue, uint32_t parameter) noexcept
{
    if (!fetTimingUsesDialReadout(parameter)) return hostValue;
    const auto& descriptor = kParams[parameter];
    const float minimum = hostMin(descriptor);
    const float range = hostMax(descriptor) - minimum;
    const float position = std::clamp((hostValue - minimum) / range, 0.0f, 1.0f);
    return 1.0f + 6.0f * position;
}

inline float fetTimingHostValue(float dialValue, uint32_t parameter) noexcept
{
    if (!fetTimingUsesDialReadout(parameter)) return dialValue;
    const auto& descriptor = kParams[parameter];
    const float position = std::clamp((dialValue - 1.0f) / 6.0f, 0.0f, 1.0f);
    return hostMin(descriptor)
        + position * (hostMax(descriptor) - hostMin(descriptor));
}

inline bool optoKnobUsesPlainDomainDrag(uint32_t parameter) noexcept
{
    return parameter == static_cast<uint32_t>(ParamId::SidechainHP);
}

inline const char* optoModeLabel(float hostValue) noexcept
{
    return choiceIndex(hostValue, 2) == 0 ? "COMPRESS" : "LIMIT";
}

inline constexpr const char* optoMeterLabel() noexcept { return "GR"; }

inline float optoMeterReadoutAmount(float gainReductionDb) noexcept
{
    return std::clamp(std::max(0.0f, -gainReductionDb), 0.0f, 99.9f);
}

template <size_t N>
inline int loadProgramIntoMirror(uint32_t index, std::array<float, N>& values)
{
    if (index >= kFactoryPresets.size()) return -1;
    applyFactoryPresetToHostParameters(index,
        [&values](int parameterIndex, float hostValue)
        {
            if (parameterIndex >= 0 && static_cast<size_t>(parameterIndex) < values.size())
                values[static_cast<size_t>(parameterIndex)] = hostValue;
        });
    return static_cast<int>(index);
}

inline bool selectionOwnsParam(int currentFactoryPreset, bool userPresetActive,
                               bool defaultsActive, uint32_t parameterIndex) noexcept
{
    if (currentFactoryPreset >= 0)
        return presetOwnsParam(currentFactoryPreset, parameterIndex);
    if (userPresetActive || defaultsActive)
        return parameterIndex != static_cast<uint32_t>(ParamId::Bypass);
    return false;
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

// A host may set parameters before it creates the editor, and DPF does not
// replay those earlier parameterChanged() calls to a newly constructed UI.
// Seed the mirror once from the live plugin so the first frame represents the
// state the host actually opened rather than the compile-time defaults.
template <size_t N, typename ReadParameter>
inline void refreshParameterMirror(std::array<float, N>& values,
                                   ReadParameter readParameter,
                                   uint32_t parameterCount)
{
    const uint32_t count = std::min(parameterCount, static_cast<uint32_t>(N));
    for (uint32_t index = 0; index < count; ++index)
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
#include "DuskUserPresetStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

DUSK_WEAK float multiCompGetParameterValue(void* pluginInstancePointer,
                                            uint32_t index) noexcept;

START_NAMESPACE_DAF

namespace
{
constexpr float kDesignW = 1120.0f;
constexpr float kHeaderH = 48.0f;
// Mode panels were authored in the original 760 px canvas. Translate that
// canvas upward so every mode keeps its internal proportions while the retired
// Global and Sidechain bands disappear completely from the visible layout.
constexpr float kModeCanvasTop = 322.0f;
constexpr float kModeCanvasBottom = 760.0f;
constexpr float kModeCanvasShiftY = kModeCanvasTop - kHeaderH;
constexpr float kUiPi = duskdaf::DuskPanel::kPi;
// Set to true only while collecting a host geometry measurement.
constexpr bool kGeometryDiagnosticEnabled = false;

constexpr ImU32 kPanel = IM_COL32(35, 37, 43, 255);
constexpr ImU32 kPanelRaised = IM_COL32(43, 46, 54, 255);
constexpr ImU32 kLine = IM_COL32(75, 79, 90, 255);
constexpr ImU32 kText = IM_COL32(232, 234, 238, 255);
constexpr ImU32 kDim = IM_COL32(162, 168, 180, 255);
constexpr ImU32 kAccent = IM_COL32(65, 194, 220, 255);
constexpr ImU32 kHeaderGreen = IM_COL32(76, 103, 48, 255);
constexpr ImU32 kHeaderGreenDark = IM_COL32(39, 57, 24, 255);
constexpr ImU32 kOptoInk = IM_COL32(31, 32, 31, 255);
constexpr ImU32 kOptoRed = IM_COL32(151, 25, 31, 255);
constexpr ImU32 kBandColors[4] = {
    IM_COL32(92, 165, 235, 255), IM_COL32(92, 205, 150, 255),
    IM_COL32(232, 185, 74, 255), IM_COL32(224, 100, 93, 255)
};

// Parameter indices are aliases of the single table order in MultiCompParams.hpp.
using ParamId = multicompp::ParamId;
constexpr uint32_t P_MODE = static_cast<uint32_t>(ParamId::Mode), P_BYPASS = static_cast<uint32_t>(ParamId::Bypass), P_MIX = static_cast<uint32_t>(ParamId::Mix);
constexpr uint32_t P_SC_HP = static_cast<uint32_t>(ParamId::SidechainHP);
#define MC_PID(name) static_cast<uint32_t>(ParamId::name)
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
constexpr uint32_t P_MB_MIX = MC_PID(MbMix), P_MB_OUT = MC_PID(MbOutput);
#undef MC_PID

constexpr uint32_t kMeterMaster = static_cast<uint32_t>(multicompp::kMeterMaster);
constexpr uint32_t kMeterBand0 = static_cast<uint32_t>(multicompp::kMeterBand0);

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

        // Hardware modes open as shallow rack faces. Modern modes request their
        // taller authored canvas when selected, preserving the user's scale.
        constrainedDesignH = multicompp::ui_detail::designHeightForMode(values[P_MODE]);
        pendingDesignH = constrainedDesignH;
        setGeometryConstraints(static_cast<uint32_t>(kDesignW),
                               static_cast<uint32_t>(constrainedDesignH), true);
        static const float kFontSizes[] = {9.f, 11.f, 13.f, 16.f, 20.f, 26.f, 30.f};
        fontSet = duskdaf::loadCrispFontSet(kFontSizes, 7, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
        scanUserPresets();
    }

    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override { editParameter(idx, false); }
    void setParam(uint32_t idx, float value) override
    {
        if (idx >= static_cast<uint32_t>(multicompp::kMeterMaster)) return;
        clearPresetSelectionForEdit(idx);
        values[idx] = value;
        if (idx == P_MODE) scheduleModeGeometry(value);
        setParameterValue(idx, value);
        if (idx >= P_X1 && idx <= P_X3) refreshCrossoverMirror();
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values.size()) values[index] = value;
        if (index == P_MODE) scheduleModeGeometry(value);
        if (index >= P_X1 && index <= P_X3) refreshCrossoverMirror();
        if (index < static_cast<uint32_t>(multicompp::kMeterMaster)
            && index != P_BYPASS)
            syncPresetSelection();
    }

    void stateChanged(const char* key, const char* state) override
    {
        if (key == nullptr || state == nullptr || std::strcmp(key, "parameters") != 0) return;
        multicompp::StateValues decoded{};
        if (!multicompp::decodeState(state, decoded)) return;
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
            values[static_cast<size_t>(i)] = decoded[static_cast<size_t>(i)];
        scheduleModeGeometry(values[P_MODE]);
        syncPresetSelection();
        repaint();
    }

    void programLoaded(uint32_t index) override
    {
        currentPreset = multicompp::ui_detail::loadProgramIntoMirror(index, values);
        scheduleModeGeometry(values[P_MODE]);
        currentUserName.clear();
        currentUserPath.clear();
        defaultsActive = currentPreset < 0 && matchesDefaults();
    }

    void uiIdle() override
    {
        seedParameterMirror();
        refreshCrossoverMirror();
        repaint();
    }

    void onImGuiDisplay() override
    {
        seedParameterMirror();
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
        const float designH = multicompp::ui_detail::designHeightForMode(value(P_MODE));
        const float scale = std::min(winW / kDesignW, winH / designH);
        const ImVec2 origin((winW - kDesignW * scale) * 0.5f,
                            (winH - designH * scale) * 0.5f);
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
                         static_cast<double>(kDesignW), static_cast<double>(designH),
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

        // The mode-panel drawing code retains its original internal coordinates;
        // translate only that canvas so removing the two utility bands does not
        // perturb the carefully tuned per-mode layouts or hit targets.
        panel.begin(scale, ImVec2(origin.x, origin.y - kModeCanvasShiftY * scale),
                    labelFont, this);
        drawModePanel(dl);

        // Overlays and the resize grip live in the compact visible canvas.
        panel.begin(scale, origin, labelFont, this);

        if (showSupporters)
            duskdaf::drawSupportersOverlay(panel, dl, kDesignW, designH, showSupporters,
                                           "Multi-Comp 2", MULTICOMP2_VERSION_STRING,
                                           &supporters);

        const duskdaf::ResizeGripState grip =
            panel.resizeGrip(dl, winW, winH, kDesignW, designH, 0.5f);
        ImGui::End();
        ImGui::PopStyleVar(2);
        // A host can service either request synchronously, so never resize from
        // inside the active ImGui window. A mode change takes priority over a
        // resize-grip request made against the previous aspect ratio.
        if (modeGeometryPending)
            applyModeGeometry();
        else if (grip.resized)
            setSize(grip.width, grip.height);
    }

private:
    std::array<float, multicompp::kTotalParamCount> values{};
    duskdaf::DuskPanel panel;
    ImDrawListSplitter optoKnobSplitter;
    ImDrawListSplitter fetKnobSplitter;
    duskdaf::CrispFontSet fontSet;
    ImFont* labelFont = nullptr;
    duskdaf::SupportersOverlay supporters;
    bool showSupporters = false;
    bool geometryDiagnosticLogged = false;
    float constrainedDesignH = 380.0f;
    float pendingDesignH = 380.0f;
    bool modeGeometryPending = false;
    bool parameterMirrorSeeded = false;
    static constexpr uint32_t kNoCrossover = ~uint32_t(0);
    // The crossover handle under an active drag. Owns both the open automation
    // gesture and the mirror's skip, so the two cannot disagree.
    uint32_t draggedCrossover = kNoCrossover;
    // Set by crossover() on every frame it submits the held handle, and read
    // once per frame to notice a handle that stopped being drawn mid-drag.
    bool draggedCrossoverStillDrawn = false;
    int currentPreset = -1;
    bool defaultsActive = true;
    std::string currentUserName;
    std::string currentUserPath;
    struct UserPreset
    {
        std::string name;
        std::string path;
        multicompp::StateValues values{};
    };
    std::vector<UserPreset> userPresets;
    char saveBuf[64] = {};
    bool saveFailed = false;

    float value(uint32_t p) const { return values[p]; }

    void scheduleModeGeometry(float hostMode)
    {
        pendingDesignH = multicompp::ui_detail::designHeightForMode(hostMode);
        modeGeometryPending = pendingDesignH != constrainedDesignH;
    }

    void applyModeGeometry()
    {
        modeGeometryPending = false;
        const uint32_t width = getWidth();
        const uint32_t targetHeight = static_cast<uint32_t>(std::lround(
            static_cast<double>(width) * pendingDesignH / kDesignW));
        setGeometryConstraints(static_cast<uint32_t>(kDesignW),
                               static_cast<uint32_t>(pendingDesignH), true);
        constrainedDesignH = pendingDesignH;
        geometryDiagnosticLogged = false;
        if (getHeight() != targetHeight)
            setSize(width, targetHeight);
    }

    void clearPresetSelection(bool atDefaults)
    {
        currentPreset = -1;
        currentUserName.clear();
        currentUserPath.clear();
        defaultsActive = atDefaults;
    }

    void clearPresetSelectionForEdit(uint32_t parameterIndex)
    {
        if (multicompp::ui_detail::selectionOwnsParam(
                currentPreset, !currentUserName.empty(), defaultsActive,
                parameterIndex))
            clearPresetSelection(false);
    }

    bool parameterMatches(int index, float expected) const
    {
        const float range = multicompp::resolveParameter(index,
            [](const multicompp::Param& d) {
                return multicompp::hostMax(d) - multicompp::hostMin(d);
            },
            [](const multicompp::BandParam& d, int) {
                return multicompp::hostMax(d) - multicompp::hostMin(d);
            });
        return std::fabs(values[static_cast<size_t>(index)] - expected)
            <= std::max(1.0e-6f, range * 1.0e-5f);
    }

    bool matchesDefaults() const
    {
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
        {
            if (i == static_cast<int>(P_BYPASS)) continue;
            const float expected = multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
                [](const multicompp::BandParam& d, int) {
                    return multicompp::hostDefault(d);
                });
            if (!parameterMatches(i, expected)) return false;
        }
        return true;
    }

    int deriveFactoryPreset() const
    {
        for (size_t presetIndex = 0;
             presetIndex < multicompp::kFactoryPresets.size(); ++presetIndex)
        {
            bool matches = true;
            const auto& expanded = multicompp::kExpandedFactoryPresets[presetIndex];
            for (int parameterIndex = 0;
                 parameterIndex < multicompp::kMeterMaster && matches;
                 ++parameterIndex)
                if (multicompp::presetOwnsParam(
                        static_cast<int>(presetIndex),
                        static_cast<uint32_t>(parameterIndex))
                    && !parameterMatches(
                        parameterIndex,
                        expanded.hostValues[static_cast<size_t>(parameterIndex)]))
                    matches = false;
            if (matches) return static_cast<int>(presetIndex);
        }
        return -1;
    }

    int deriveUserPreset() const
    {
        for (size_t presetIndex = 0; presetIndex < userPresets.size(); ++presetIndex)
        {
            bool matches = true;
            for (int i = 0; i < multicompp::kMeterMaster && matches; ++i)
                if (i != static_cast<int>(P_BYPASS)
                    && !parameterMatches(
                        i, userPresets[presetIndex].values[static_cast<size_t>(i)]))
                    matches = false;
            if (matches) return static_cast<int>(presetIndex);
        }
        return -1;
    }

    void syncPresetSelection()
    {
        if (matchesDefaults())
        {
            clearPresetSelection(true);
            return;
        }
        const int factory = deriveFactoryPreset();
        if (factory >= 0)
        {
            currentPreset = factory;
            currentUserName.clear();
            currentUserPath.clear();
            defaultsActive = false;
            return;
        }
        const int user = deriveUserPreset();
        if (user >= 0)
        {
            currentPreset = -1;
            currentUserName = userPresets[static_cast<size_t>(user)].name;
            currentUserPath = userPresets[static_cast<size_t>(user)].path;
            defaultsActive = false;
            return;
        }
        clearPresetSelection(false);
    }

    void refreshCrossoverMirror()
    {
        if (multiCompGetParameterValue == nullptr) return;
        void* const instance = getPluginInstancePointer();
        if (instance == nullptr) return;
        multicompp::ui_detail::refreshCrossoverMirror(values,
            [instance](uint32_t index) { return multiCompGetParameterValue(instance, index); },
            draggedCrossover);
    }

    void seedParameterMirror()
    {
        if (parameterMirrorSeeded || multiCompGetParameterValue == nullptr) return;
        void* const instance = getPluginInstancePointer();
        if (instance == nullptr) return;
        multicompp::ui_detail::refreshParameterMirror(values,
            [instance](uint32_t index) { return multiCompGetParameterValue(instance, index); },
            static_cast<uint32_t>(multicompp::kMeterMaster));
        parameterMirrorSeeded = true;
        scheduleModeGeometry(values[P_MODE]);
        syncPresetSelection();
    }

    void setValue(uint32_t p, float v)
    {
        clearPresetSelectionForEdit(p);
        editParameter(p, true);
        const float hostValue = hostValueForPlain(p, v);
        values[p] = hostValue;
        if (p == P_MODE) scheduleModeGeometry(hostValue);
        setParameterValue(p, hostValue);
        editParameter(p, false);
    }

    void setHostValue(uint32_t p, float hostValue)
    {
        clearPresetSelectionForEdit(p);
        editParameter(p, true);
        values[p] = hostValue;
        if (p == P_MODE) scheduleModeGeometry(hostValue);
        setParameterValue(p, hostValue);
        editParameter(p, false);
        if (p >= P_X1 && p <= P_X3) refreshCrossoverMirror();
    }

    void drawSection(ImDrawList* dl, float y0, float y1, const char* title)
    {
        dl->AddRectFilled(panel.P(8, y0), panel.P(kDesignW - 8, y1), kPanel, 5.0f * panel.scale());
        dl->AddRect(panel.P(8, y0), panel.P(kDesignW - 8, y1), kLine, 5.0f * panel.scale(), 0, panel.scale());
        if (title != nullptr && title[0] != '\0')
            panel.text(dl, 22, y0 + 10, 11, kAccent, title, -1, true);
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

    bool headerChevron(ImDrawList* dl, const char* id, float cx, bool left)
    {
        constexpr float cy = 24.0f;
        constexpr float halfH = 12.5f;
        const ImVec2 b0 = panel.P(cx - 9.5f, cy - halfH);
        const ImVec2 b1 = panel.P(cx + 9.5f, cy + halfH);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hovered ? IM_COL32(54, 54, 58, 255)
                                          : IM_COL32(38, 38, 41, 255),
                          2.0f * panel.scale());
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 2.0f * panel.scale(), 0,
                    panel.scale());
        const ImVec2 center = panel.P(cx, cy);
        const float d = 4.3f * panel.scale();
        const ImU32 ink = hovered ? kText : kDim;
        if (left)
            dl->AddTriangleFilled(ImVec2(center.x + d * 0.5f, center.y - d),
                                  ImVec2(center.x + d * 0.5f, center.y + d),
                                  ImVec2(center.x - d * 0.7f, center.y), ink);
        else
            dl->AddTriangleFilled(ImVec2(center.x - d * 0.5f, center.y - d),
                                  ImVec2(center.x - d * 0.5f, center.y + d),
                                  ImVec2(center.x + d * 0.7f, center.y), ink);
        return ImGui::IsItemClicked();
    }

    bool headerTextButton(ImDrawList* dl, const char* id, float x0, float x1,
                          const char* label)
    {
        constexpr float y0 = 11.5f, y1 = 36.5f;
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hovered ? IM_COL32(54, 54, 58, 255)
                                          : IM_COL32(38, 38, 41, 255),
                          2.0f * panel.scale());
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 2.0f * panel.scale(), 0,
                    panel.scale());
        panel.text(dl, 0.5f * (x0 + x1), 18.5f, 11.0f,
                   hovered ? kText : kDim, label, 0, true);
        return ImGui::IsItemClicked();
    }

    const char* presetPreview() const
    {
        if (currentPreset >= 0
            && currentPreset < static_cast<int>(multicompp::kFactoryPresets.size()))
            return multicompp::kFactoryPresets[static_cast<size_t>(currentPreset)].name;
        if (!currentUserName.empty()) return currentUserName.c_str();
        return defaultsActive ? "Default" : "Custom";
    }

    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, kHeaderH),
                          IM_COL32(14, 14, 15, 255));
        dl->AddRectFilledMultiColor(panel.P(0, 0), panel.P(kDesignW, 4),
                                    IM_COL32(205, 203, 197, 255),
                                    IM_COL32(155, 154, 151, 255),
                                    IM_COL32(91, 91, 91, 255),
                                    IM_COL32(118, 118, 116, 255));
        dl->AddLine(panel.P(0, 48), panel.P(kDesignW, 48),
                    IM_COL32(91, 90, 88, 255), 1.2f * panel.scale());

        constexpr float plateX0 = 28.0f, plateX1 = 358.0f;
        constexpr float plateY0 = 8.0f, plateY1 = 40.0f;
        dl->AddRectFilledMultiColor(panel.P(plateX0, plateY0), panel.P(plateX1, plateY1),
                                    IM_COL32(37, 37, 38, 255),
                                    IM_COL32(29, 29, 30, 255),
                                    IM_COL32(16, 16, 17, 255),
                                    IM_COL32(19, 19, 20, 255));
        dl->AddRect(panel.P(plateX0, plateY0), panel.P(plateX1, plateY1),
                    IM_COL32(185, 184, 180, 220), 3.5f * panel.scale(), 0,
                    1.2f * panel.scale());
        dl->AddLine(panel.P(40, 37), panel.P(348, 37),
                    IM_COL32(116, 145, 75, 210), 1.2f * panel.scale());
        panel.text(dl, 42, 10, 24, kText, "MULTI-COMP", -1, true);
        panel.text(dl, 244, 14, 20, kText, "MC-2", 0, true);
        panel.text(dl, 346, 20, 11, kDim, "v" MULTICOMP2_VERSION_STRING, 1);
        panel.text(dl, kDesignW - 34, 12, 22, kText, "DUSK AUDIO", 1, true);

        ImGui::SetCursorScreenPos(panel.P(plateX0, plateY0));
        if (ImGui::InvisibleButton("##mc_titlecredits",
                                   ImVec2((plateX1 - plateX0) * panel.scale(),
                                          (plateY1 - plateY0) * panel.scale())))
        {
            supporters.resetInteraction();
            showSupporters = true;
        }

        if (headerChevron(dl, "##mc_presetprev", 390.5f, true)) stepPreset(-1);
        if (headerChevron(dl, "##mc_presetnext", 697.5f, false)) stepPreset(1);

        constexpr float bandY0 = 11.5f, bandY1 = 36.5f;
        constexpr float bandH = bandY1 - bandY0;
        ImGui::SetCursorScreenPos(panel.P(404, bandY0));
        ImGui::SetNextItemWidth(280.0f * panel.scale());
        ImFont* presetFont = fontSet.pick(13.0f * panel.scale());
        if (presetFont != nullptr) ImGui::PushFont(presetFont);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(7.0f * panel.scale(),
                                   std::max(0.0f, 0.5f * (bandH * panel.scale()
                                                         - ImGui::GetFontSize()))));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(48, 48, 51, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(55, 55, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(76, 103, 48, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(92, 124, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(62, 85, 39, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, kHeaderGreenDark);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kHeaderGreen);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 58, 27, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kText);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::BeginCombo("##mc_factory", presetPreview()))
        {
            for (size_t i = 0; i < multicompp::kFactoryPresets.size(); ++i)
            {
                const bool selected = static_cast<int>(i) == currentPreset;
                if (ImGui::Selectable(multicompp::kFactoryPresets[i].name, selected))
                {
                    applyPreset(static_cast<int>(i));
                    ImGui::CloseCurrentPopup();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            if (!userPresets.empty())
            {
                ImGui::SeparatorText("User");
                for (size_t i = 0; i < userPresets.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    const auto& preset = userPresets[i];
                    if (ImGui::Selectable(preset.name.c_str(),
                                          currentPreset < 0
                                              && preset.path == currentUserPath))
                    {
                        loadUserPreset(preset);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(11);
        ImGui::PopStyleVar();
        if (presetFont != nullptr) ImGui::PopFont();

        if (headerTextButton(dl, "##mc_init", 716, 772, "INIT")) initDefaults();
        if (headerTextButton(dl, "##mc_save", 780, 836, "SAVE"))
        {
            std::snprintf(saveBuf, sizeof(saveBuf), "%s", currentUserName.c_str());
            ImGui::OpenPopup("Save Multi-Comp Preset");
        }
        drawSaveModal();
    }

    void drawSaveModal()
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(26, 26, 28, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kText);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(90, 90, 94, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(40, 40, 43, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(50, 50, 54, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(56, 56, 60, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, kHeaderGreenDark);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kHeaderGreen);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 58, 27, 255));
        if (ImGui::BeginPopupModal("Save Multi-Comp Preset", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            const bool appearing = ImGui::IsWindowAppearing();
            if (appearing)
                saveFailed = false;
            ImGui::TextUnformatted("Preset name");
            ImGui::SetNextItemWidth(260.0f * panel.scale());
            if (appearing) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText(
                "##mc_savename", saveBuf, sizeof(saveBuf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool save = ImGui::Button("Save") || enter;
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel");
            if (save && saveBuf[0] != '\0')
            {
                if (saveUserPreset(saveBuf))
                {
                    saveFailed = false;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    saveFailed = true;
                }
            }
            if (saveFailed)
                ImGui::TextColored(ImVec4(0.90f, 0.42f, 0.35f, 1.0f),
                                   "Could not save. Try a different name.");
            if (cancel)
            {
                saveFailed = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(9);
    }

    void stepPreset(int direction)
    {
        int index = currentPreset < 0 ? (direction < 0 ? 0 : -1) : currentPreset;
        index += direction;
        index = std::clamp(index, 0,
                           static_cast<int>(multicompp::kFactoryPresets.size()) - 1);
        applyPreset(index);
    }

    void initDefaults()
    {
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
        {
            if (i == static_cast<int>(P_BYPASS)) continue;
            const float hostDefault = multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
                [](const multicompp::BandParam& d, int) {
                    return multicompp::hostDefault(d);
                });
            setHostValue(static_cast<uint32_t>(i), hostDefault);
        }
        clearPresetSelection(true);
    }

    std::filesystem::path configDir() const
    {
        return duskdaf::userPresetDirectory("MultiComp2");
    }

    static bool readUserPresetFile(const std::filesystem::path& path, UserPreset& preset)
    {
        std::ifstream input(path);
        if (!input) return false;
        std::string line;
        std::string encoded;
        while (std::getline(input, line))
        {
            if (line.compare(0, 5, "name=") == 0)
                preset.name = line.substr(5);
            else if (line.compare(0, 6, "state=") == 0)
                encoded = line.substr(6);
        }
        if (preset.name.empty() || encoded.empty()
            || !multicompp::decodeState(encoded, preset.values))
            return false;
        preset.path = path.string();
        return true;
    }

    void scanUserPresets()
    {
        duskdaf::scanUserPresets(
            configDir(), ".mcpreset", userPresets,
            [](const std::filesystem::path& path, UserPreset& preset) {
                return readUserPresetFile(path, preset);
            });
    }

    bool saveUserPreset(const char* rawName)
    {
        multicompp::StateValues snapshot{};
        std::copy_n(values.begin(), snapshot.size(), snapshot.begin());
        snapshot[P_BYPASS] = multicompp::hostDefault(
            multicompp::kParams[static_cast<size_t>(P_BYPASS)]);
        const auto saved = duskdaf::writeUserPreset(
            configDir(), ".mcpreset", rawName,
            [&snapshot](std::ostream& output) {
                output << "state=" << multicompp::encodeState(snapshot) << '\n';
            },
            [](const std::filesystem::path& path) {
                UserPreset preset;
                return readUserPresetFile(path, preset) ? preset.name : std::string();
            });
        if (!saved) return false;

        scanUserPresets();
        currentPreset = -1;
        currentUserName = saved.name;
        currentUserPath = saved.path;
        defaultsActive = false;
        return true;
    }

    void loadUserPreset(const UserPreset& preset)
    {
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
        {
            if (i == static_cast<int>(P_BYPASS)) continue;
            setHostValue(static_cast<uint32_t>(i),
                         preset.values[static_cast<size_t>(i)]);
        }
        currentPreset = -1;
        currentUserName = preset.name;
        currentUserPath = preset.path;
        defaultsActive = false;
    }

    void drawModeToolbar(ImDrawList* dl)
    {
        constexpr int modeCount = static_cast<int>(
            sizeof(multicompp::kModes) / sizeof(multicompp::kModes[0]));
        static_assert(modeCount == 8,
                      "the mode toolbar layout must be revisited when modes change");
        const int selected = multicompp::ui_detail::choiceIndex(value(P_MODE), 8);
        constexpr float rowLeft = 20.0f;
        constexpr float rowRight = 996.0f;
        constexpr float gap = 3.0f;
        constexpr float buttonWidth =
            (rowRight - rowLeft - gap * static_cast<float>(modeCount - 1))
            / static_cast<float>(modeCount);
        constexpr float y0 = kModeCanvasTop + 3.0f;
        constexpr float y1 = kModeCanvasTop + 21.0f;
        for (int mode = 0; mode < modeCount; ++mode)
        {
            const float x0 = rowLeft + static_cast<float>(mode) * (buttonWidth + gap);
            const float x1 = x0 + buttonWidth;
            const ImVec2 b0 = panel.P(x0, y0);
            const ImVec2 b1 = panel.P(x1, y1);
            char id[32];
            std::snprintf(id, sizeof(id), "##mc_mode_button_%d", mode);
            ImGui::SetCursorScreenPos(b0);
            const bool clicked = ImGui::InvisibleButton(
                id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            const bool hovered = ImGui::IsItemHovered();
            const bool active = mode == selected;
            const ImU32 fill = active ? kHeaderGreenDark
                : hovered ? kPanelRaised : IM_COL32(24, 25, 29, 255);
            const ImU32 outline = active
                ? IM_COL32(103, 132, 75, 255) : kLine;
            dl->AddRectFilled(b0, b1, fill, 2.0f * panel.scale());
            dl->AddRect(b0, b1, outline, 2.0f * panel.scale(), 0,
                        panel.scale());
            if (active)
                dl->AddRectFilled(panel.P(x0 + 2.0f, y1 - 2.0f),
                                  panel.P(x1 - 2.0f, y1), kHeaderGreen);
            panel.text(dl, 0.5f * (x0 + x1), y0 + 3.0f, 8.8f,
                       active || hovered ? kText : kDim,
                       multicompp::kModes[mode], 0, true);
            if (clicked && !active)
                setValue(P_MODE, static_cast<float>(mode));
        }
        panel.toggle("mc_bypass", P_BYPASS, 1008, kModeCanvasTop + 3,
                     1098, kModeCanvasTop + 21, values[P_BYPASS], "BYPASS");
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

    static float fetTimingHostToDial(float host, uint32_t p, void*)
    {
        return multicompp::ui_detail::fetTimingDialValue(host, p);
    }

    static float fetTimingDialToHost(float dial, uint32_t p, void*)
    {
        return multicompp::ui_detail::fetTimingHostValue(dial, p);
    }

    void drawModePanel(ImDrawList* dl)
    {
        const int mode = multicompp::ui_detail::choiceIndex(value(P_MODE), 8);
        drawSection(dl, kModeCanvasTop, kModeCanvasBottom - 8, "");
        drawModeToolbar(dl);
        if (mode == 0 || mode == 1)
        {
            // Opto and vintage FET own panel-integrated analogue GR meters.
        }
        else if (mode == 7)
            drawMeter(dl, 1062, kModeCanvasTop + 42, 42, 92, meter(kMeterMaster), kAccent, "MASTER GR");
        else
            drawMeter(dl, 28, kModeCanvasTop + 42, 48, 300, meter(kMeterMaster), kAccent, "MASTER GR");
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

        // The face follows the same control contract as the flat Opto panel,
        // but uses physical rack construction and hardware depth. Keep all
        // branding original to Dusk rather than reproducing a reference unit.
        dl->AddRectFilled(panel.P(left + 5, top + 8), panel.P(right + 5, bottom + 8),
                          IM_COL32(0, 0, 0, 145), 4.0f * panel.scale());
        dl->AddRectFilled(panel.P(left, top), panel.P(right, bottom),
                          IM_COL32(21, 23, 24, 255), 3.0f * panel.scale());

        constexpr float earWidth = 46.0f;
        const float faceLeft = left + earWidth;
        const float faceRight = right - earWidth;
        dl->AddRectFilled(panel.P(left + 3, top + 3), panel.P(faceLeft, bottom - 3),
                          IM_COL32(143, 147, 147, 255), 2.0f * panel.scale());
        dl->AddRectFilled(panel.P(faceRight, top + 3), panel.P(right - 3, bottom - 3),
                          IM_COL32(143, 147, 147, 255), 2.0f * panel.scale());
        dl->AddRectFilledMultiColor(panel.P(faceLeft, top + 3),
                                    panel.P(faceRight, bottom - 3),
                                    IM_COL32(226, 228, 226, 255),
                                    IM_COL32(193, 196, 195, 255),
                                    IM_COL32(181, 184, 183, 255),
                                    IM_COL32(214, 216, 214, 255));

        // Deterministic one-pixel strokes keep the panel feeling like brushed
        // aluminium without requiring a resolution-specific bitmap texture.
        for (int row = static_cast<int>(top + 5.0f);
             row < static_cast<int>(bottom - 4.0f); row += 3)
        {
            const int variation = (row * 37) % 5;
            const ImU32 shade = variation < 2
                ? IM_COL32(255, 255, 252, 25)
                : IM_COL32(74, 78, 78, 18);
            dl->AddLine(panel.P(left + 5.0f, static_cast<float>(row)),
                        panel.P(right - 5.0f, static_cast<float>(row)),
                        shade, panel.scale());
        }
        dl->AddLine(panel.P(faceLeft, top + 4), panel.P(faceLeft, bottom - 4),
                    IM_COL32(54, 58, 59, 210), 1.5f * panel.scale());
        dl->AddLine(panel.P(faceRight, top + 4), panel.P(faceRight, bottom - 4),
                    IM_COL32(54, 58, 59, 210), 1.5f * panel.scale());
        dl->AddLine(panel.P(faceLeft + 1, top + 5), panel.P(faceLeft + 1, bottom - 5),
                    IM_COL32(255, 255, 255, 115), panel.scale());
        dl->AddLine(panel.P(left + 4, top + 4), panel.P(right - 4, top + 4),
                    IM_COL32(255, 255, 255, 160), 1.2f * panel.scale());
        dl->AddLine(panel.P(left + 4, bottom - 4), panel.P(right - 4, bottom - 4),
                    IM_COL32(23, 25, 26, 220), 2.0f * panel.scale());

        // Rack-ear slots are intentionally clipped by the panel edge, like a
        // photographed 19-inch unit sitting in a dark rack.
        for (const float slotY : {top + 72.0f, bottom - 72.0f})
        {
            dl->AddRectFilled(panel.P(left - 7.0f, slotY - 9.0f),
                              panel.P(left + 23.0f, slotY + 9.0f),
                              IM_COL32(7, 8, 8, 255), 9.0f * panel.scale());
            dl->AddRectFilled(panel.P(right - 23.0f, slotY - 9.0f),
                              panel.P(right + 7.0f, slotY + 9.0f),
                              IM_COL32(7, 8, 8, 255), 9.0f * panel.scale());
            dl->AddLine(panel.P(left - 2.0f, slotY - 6.0f),
                        panel.P(left + 19.0f, slotY - 6.0f),
                        IM_COL32(255, 255, 255, 38), panel.scale());
            dl->AddLine(panel.P(right - 19.0f, slotY - 6.0f),
                        panel.P(right + 2.0f, slotY - 6.0f),
                        IM_COL32(255, 255, 255, 38), panel.scale());
        }

        for (const ImVec2 screw : {ImVec2(faceLeft + 28, top + 20),
                                   ImVec2(faceRight - 28, top + 20),
                                   ImVec2(faceLeft + 28, bottom - 20),
                                   ImVec2(560.0f, bottom - 20),
                                   ImVec2(faceRight - 28, bottom - 20)})
            drawOptoScrew(dl, screw.x, screw.y, 7.0f);

        panel.text(dl, 156, top + 20, 18.0f, kOptoRed,
                   "LEVELING AMPLIFIER", -1, true);
        dl->AddLine(panel.P(156, top + 47), panel.P(354, top + 47),
                    kOptoRed, 2.0f * panel.scale());
        panel.text(dl, 953, top + 20, 9.0f, kOptoInk, "OPTO CELL", 0, true);
        panel.text(dl, 953, top + 36, 7.5f, IM_COL32(75, 78, 77, 255),
                   "LEVEL CONTROL", 0, true);

        optoModeSwitch(dl, 139, top + 170.0f);
        optoKnob(dl, "opto_gain", P_OPTO_GAIN, 287, top + 170.0f, 53.0f, "GAIN");
        drawOptoMeter(dl, 390, top + 54.0f, 348, 196,
                      multicompp::ui_detail::optoMeterDisplayValue(meter(kMeterMaster)));
        optoKnob(dl, "opto_peak", P_OPTO_PEAK, 838, top + 170.0f, 53.0f,
                 "PEAK REDUCTION");
        optoKnob(dl, "opto_sc_hp", P_SC_HP, 996, top + 108.0f, 18.0f,
                 "SC HP", false, "%.0f", " Hz");
        optoKnob(dl, "opto_mix", P_MIX, 996, top + 224.0f, 18.0f,
                 "MIX", false);
    }

    void drawOptoScrew(ImDrawList* dl, float x, float y, float radius)
    {
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        dl->AddCircleFilled(ImVec2(center.x + 2.2f * scale, center.y + 3.2f * scale),
                            radius * scale, IM_COL32(0, 0, 0, 105), 28);
        dl->AddCircleFilled(center, radius * scale,
                            IM_COL32(86, 90, 90, 255), 28);
        dl->AddCircleFilled(ImVec2(center.x - 0.8f * scale, center.y - 1.0f * scale),
                            (radius - 1.2f) * scale,
                            IM_COL32(206, 208, 205, 255), 28);
        dl->AddCircle(center, (radius - 1.2f) * scale,
                      IM_COL32(60, 63, 63, 255), 28, 0.9f * scale);
        const float slot = radius * 0.55f * scale;
        const ImU32 slotDark = IM_COL32(57, 59, 58, 255);
        dl->AddLine(ImVec2(center.x - slot, center.y - slot),
                    ImVec2(center.x + slot, center.y + slot),
                    slotDark, 1.3f * scale);
        dl->AddLine(ImVec2(center.x + slot, center.y - slot),
                    ImVec2(center.x - slot, center.y + slot),
                    slotDark, 1.3f * scale);
        dl->AddCircleFilled(ImVec2(center.x - radius * 0.30f * scale,
                                   center.y - radius * 0.34f * scale),
                            1.15f * scale, IM_COL32(255, 255, 255, 170), 12);
    }

    void optoKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
                  float radius, const char* label, bool numberedScale = true,
                  const char* format = "%.0f",
                  const char* suffix = multicompp::ui_detail::optoKnobValueSuffix())
    {
        // Put the shared gesture/editor layer above the custom body while
        // letting it mutate the value first, so the pointer and readout update
        // in the same frame. The active-only bubble exposes shift-fine values.
        optoKnobSplitter.Split(dl, 2);
        optoKnobSplitter.SetCurrentChannel(dl, 1);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this,
                   multicompp::ui_detail::optoKnobUsesPlainDomainDrag(p));
        optoKnobSplitter.SetCurrentChannel(dl, 0);

        const float plain = plainValueForHost(p, values[p]);
        const float minimum = plainValueForHost(p, hostMinimum(p));
        const float maximum = plainValueForHost(p, hostMaximum(p));
        const float t = std::clamp((plain - minimum)
            / std::max(maximum - minimum, 1.0e-6f), 0.0f, 1.0f);
        const ImVec2 center = panel.P(x, y);
        const float scaledRadius = radius * panel.scale();

        if (!numberedScale)
        {
            // The original hardware did not have Mix. Treat our required Mix
            // control as a deliberately small set-screw so it does not compete
            // with the two primary controls.
            std::array<ImVec2, 6> nut{};
            for (int point = 0; point < 6; ++point)
            {
                const float angle = kUiPi / 6.0f + static_cast<float>(point)
                    * kUiPi / 3.0f;
                nut[static_cast<size_t>(point)] = ImVec2(
                    center.x + std::cos(angle) * scaledRadius * 1.22f,
                    center.y + std::sin(angle) * scaledRadius * 1.22f);
            }
            std::array<ImVec2, 6> shadow = nut;
            for (auto& point : shadow)
            {
                point.x += 2.0f * panel.scale();
                point.y += 3.0f * panel.scale();
            }
            dl->AddConvexPolyFilled(shadow.data(), 6, IM_COL32(0, 0, 0, 90));
            dl->AddConvexPolyFilled(nut.data(), 6, IM_COL32(116, 119, 117, 255));
            dl->AddCircleFilled(center, scaledRadius,
                                IM_COL32(210, 212, 208, 255), 32);
            dl->AddCircle(center, scaledRadius, IM_COL32(57, 59, 58, 255),
                          32, 1.2f * panel.scale());
            const float slotAngle = duskdaf::DuskPanel::knobAngle(t);
            const ImVec2 slot(std::sin(slotAngle), -std::cos(slotAngle));
            dl->AddLine(ImVec2(center.x - slot.x * scaledRadius * 0.63f,
                               center.y - slot.y * scaledRadius * 0.63f),
                        ImVec2(center.x + slot.x * scaledRadius * 0.63f,
                               center.y + slot.y * scaledRadius * 0.63f),
                        IM_COL32(47, 49, 48, 255), 2.4f * panel.scale());
            panel.text(dl, x, y - 40.0f, 9.0f, kOptoInk, label, 0, true);
            panel.text(dl, x - 31.0f, y - 4.0f, 12.0f, kOptoInk, "-", 0, true);
            panel.text(dl, x + 31.0f, y - 4.0f, 12.0f, kOptoInk, "+", 0, true);
            if (p == P_SC_HP)
            {
                char cutoff[20];
                if (plain < 1.0f)
                    std::snprintf(cutoff, sizeof(cutoff), "OFF");
                else
                    std::snprintf(cutoff, sizeof(cutoff), "%.0f Hz",
                                  static_cast<double>(plain));
                panel.text(dl, x, y + 28.0f, 7.5f, kOptoInk, cutoff, 0, true);
            }
            optoKnobSplitter.Merge(dl);
            return;
        }

        constexpr std::array<const char*, 11> scaleLabels{{
            "0", "10", "20", "30", "40", "50",
            "60", "70", "80", "90", "100"}};
        for (int tick = 0; tick <= 50; ++tick)
        {
            const float angle = duskdaf::DuskPanel::knobAngle(
                static_cast<float>(tick) / 50.0f);
            const ImVec2 direction(std::sin(angle), -std::cos(angle));
            const bool major = tick % 5 == 0;
            const bool medium = tick % 5 == 0 || tick % 5 == 2;
            const float inner = scaledRadius
                + (major ? 4.0f : medium ? 6.0f : 8.0f) * panel.scale();
            const float outer = scaledRadius + 12.0f * panel.scale();
            dl->AddLine(ImVec2(center.x + direction.x * inner,
                               center.y + direction.y * inner),
                        ImVec2(center.x + direction.x * outer,
                               center.y + direction.y * outer),
                        kOptoInk, (major ? 1.45f : 0.75f) * panel.scale());
            if (major)
            {
                const int labelIndex = tick / 5;
                const float labelRadius = radius + 23.0f;
                panel.text(dl,
                           x + direction.x * labelRadius,
                           y + direction.y * labelRadius - 4.0f,
                           7.8f, kOptoInk,
                           scaleLabels[static_cast<size_t>(labelIndex)], 0, true);
            }
        }

        const ImVec2 shadowCenter(center.x + 3.0f * panel.scale(),
                                  center.y + 5.0f * panel.scale());
        dl->AddCircleFilled(shadowCenter, scaledRadius * 1.01f,
                            IM_COL32(0, 0, 0, 105), 56);
        for (int lobe = 0; lobe < 14; ++lobe)
        {
            const float angle = 2.0f * kUiPi * static_cast<float>(lobe) / 14.0f;
            const ImVec2 direction(std::sin(angle), -std::cos(angle));
            dl->AddCircleFilled(ImVec2(center.x + direction.x * scaledRadius * 0.81f,
                                       center.y + direction.y * scaledRadius * 0.81f),
                                scaledRadius * 0.18f,
                                IM_COL32(20, 21, 20, 255), 20);
        }
        dl->AddCircleFilled(center, scaledRadius * 0.92f,
                            IM_COL32(24, 25, 24, 255), 56);
        dl->AddCircleFilled(ImVec2(center.x - scaledRadius * 0.09f,
                                   center.y - scaledRadius * 0.11f),
                            scaledRadius * 0.70f,
                            IM_COL32(52, 53, 51, 255), 48);
        dl->PathArcTo(center, scaledRadius * 0.73f,
                      -2.75f, -0.32f, 28);
        dl->PathStroke(IM_COL32(122, 124, 119, 118), 0,
                       1.6f * panel.scale());
        dl->PathArcTo(center, scaledRadius * 0.91f,
                      0.35f, 2.70f, 28);
        dl->PathStroke(IM_COL32(0, 0, 0, 145), 0, 2.0f * panel.scale());
        const float pointerAngle = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 pointer(std::sin(pointerAngle), -std::cos(pointerAngle));
        dl->AddLine(ImVec2(center.x + pointer.x * scaledRadius * 0.18f,
                           center.y + pointer.y * scaledRadius * 0.18f),
                    ImVec2(center.x + pointer.x * scaledRadius * 0.84f,
                           center.y + pointer.y * scaledRadius * 0.84f),
                    IM_COL32(243, 244, 238, 255), 3.0f * panel.scale());
        dl->AddLine(ImVec2(center.x + pointer.x * scaledRadius * 0.30f
                                   - 1.0f * panel.scale(),
                           center.y + pointer.y * scaledRadius * 0.30f),
                    ImVec2(center.x + pointer.x * scaledRadius * 0.78f
                                   - 1.0f * panel.scale(),
                           center.y + pointer.y * scaledRadius * 0.78f),
                    IM_COL32(255, 255, 255, 95), panel.scale());
        panel.text(dl, x, y + radius + 38.0f, 9.5f,
                   kOptoInk, label, 0, true);
        optoKnobSplitter.Merge(dl);
    }

    void optoModeSwitch(ImDrawList* dl, float x, float y)
    {
        const ImVec2 p0 = panel.P(x - 48.0f, y - 77.0f);
        const ImVec2 p1 = panel.P(x + 48.0f, y + 74.0f);
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##opto_comp_limit", ImVec2(p1.x - p0.x, p1.y - p0.y));
        if (ImGui::IsItemClicked())
            setValue(P_OPTO_LIMIT, values[P_OPTO_LIMIT] > 0.5f ? 0.0f : 1.0f);

        const bool limit = multicompp::ui_detail::choiceIndex(
            values[P_OPTO_LIMIT], 2) == 1;

        auto engravedLabel = [&](float labelY, const char* text, bool active) {
            dl->AddRectFilled(panel.P(x - 39.0f, labelY - 3.0f),
                              panel.P(x + 39.0f, labelY + 16.0f),
                              IM_COL32(28, 29, 28, 255), 1.2f * panel.scale());
            dl->AddLine(panel.P(x - 36.0f, labelY - 1.0f),
                        panel.P(x + 36.0f, labelY - 1.0f),
                        active ? kOptoRed : IM_COL32(115, 118, 115, 120),
                        (active ? 2.0f : 1.0f) * panel.scale());
            panel.text(dl, x, labelY, 8.0f,
                       active ? IM_COL32(248, 247, 238, 255)
                              : IM_COL32(171, 173, 168, 255),
                       text, 0, true);
        };
        engravedLabel(y - 73.0f, multicompp::ui_detail::optoModeLabel(1.0f), limit);
        engravedLabel(y + 57.0f, multicompp::ui_detail::optoModeLabel(0.0f), !limit);

        const ImVec2 base = panel.P(x, y);
        std::array<ImVec2, 6> nut{};
        for (int point = 0; point < 6; ++point)
        {
            const float angle = kUiPi / 6.0f + static_cast<float>(point)
                * kUiPi / 3.0f;
            nut[static_cast<size_t>(point)] = ImVec2(
                base.x + std::cos(angle) * 19.0f * panel.scale(),
                base.y + std::sin(angle) * 19.0f * panel.scale());
        }
        std::array<ImVec2, 6> nutShadow = nut;
        for (auto& point : nutShadow)
        {
            point.x += 2.0f * panel.scale();
            point.y += 4.0f * panel.scale();
        }
        dl->AddConvexPolyFilled(nutShadow.data(), 6, IM_COL32(0, 0, 0, 115));
        dl->AddConvexPolyFilled(nut.data(), 6, IM_COL32(121, 124, 121, 255));
        dl->AddCircleFilled(base, 12.5f * panel.scale(),
                            IM_COL32(49, 51, 50, 255), 32);
        dl->AddCircle(base, 12.5f * panel.scale(),
                      IM_COL32(225, 226, 221, 190), 32, panel.scale());
        const float leverY = y + (limit ? -27.0f : 27.0f);
        const float leverX = x + (limit ? -3.0f : 4.0f);
        dl->AddLine(panel.P(x + 2.0f, y + 3.0f),
                    panel.P(leverX + 2.0f, leverY + 4.0f),
                    IM_COL32(0, 0, 0, 130), 8.5f * panel.scale());
        dl->AddLine(base, panel.P(leverX, leverY),
                    IM_COL32(163, 165, 160, 255), 7.0f * panel.scale());
        dl->AddLine(panel.P(x - 1.5f, y - 1.5f),
                    panel.P(leverX - 1.5f, leverY - 1.5f),
                    IM_COL32(245, 245, 238, 215), 2.0f * panel.scale());
        dl->AddCircleFilled(panel.P(leverX, leverY), 7.0f * panel.scale(),
                            IM_COL32(180, 182, 176, 255), 28);
        dl->AddCircle(panel.P(leverX, leverY), 7.0f * panel.scale(),
                      IM_COL32(48, 50, 49, 255), 28, 1.0f * panel.scale());
    }

    void drawOptoMeter(ImDrawList* dl, float x, float y, float w, float h, float gr)
    {
        const float scale = panel.scale();
        dl->AddRectFilled(panel.P(x + 5.0f, y + 7.0f),
                          panel.P(x + w + 5.0f, y + h + 7.0f),
                          IM_COL32(0, 0, 0, 125), 3.0f * scale);
        dl->AddRectFilled(panel.P(x, y), panel.P(x + w, y + h),
                          IM_COL32(37, 40, 41, 255), 3.0f * scale);
        dl->AddRectFilled(panel.P(x + 7.0f, y + 7.0f),
                          panel.P(x + w - 7.0f, y + h - 7.0f),
                          IM_COL32(75, 79, 80, 255), 1.5f * scale);

        // Four asymmetric bevel rails give the meter housing a machined depth.
        dl->AddQuadFilled(panel.P(x + 7, y + 7), panel.P(x + w - 7, y + 7),
                          panel.P(x + w - 11, y + 11), panel.P(x + 11, y + 11),
                          IM_COL32(112, 117, 118, 255));
        dl->AddQuadFilled(panel.P(x + 7, y + h - 7), panel.P(x + 11, y + h - 11),
                          panel.P(x + w - 11, y + h - 11), panel.P(x + w - 7, y + h - 7),
                          IM_COL32(26, 28, 29, 255));
        dl->AddQuadFilled(panel.P(x + 7, y + 7), panel.P(x + 11, y + 11),
                          panel.P(x + 11, y + h - 11), panel.P(x + 7, y + h - 7),
                          IM_COL32(88, 93, 94, 255));
        dl->AddQuadFilled(panel.P(x + w - 7, y + 7), panel.P(x + w - 7, y + h - 7),
                          panel.P(x + w - 11, y + h - 11), panel.P(x + w - 11, y + 11),
                          IM_COL32(42, 45, 46, 255));

        dl->AddRectFilledMultiColor(panel.P(x + 11.0f, y + 11.0f),
                                    panel.P(x + w - 11.0f, y + h - 11.0f),
                                    IM_COL32(255, 226, 160, 255),
                                    IM_COL32(249, 214, 141, 255),
                                    IM_COL32(221, 177, 100, 255),
                                    IM_COL32(232, 190, 112, 255));
        dl->AddRectFilled(panel.P(x + 17.0f, y + 16.0f),
                          panel.P(x + w - 17.0f, y + 50.0f),
                          IM_COL32(255, 255, 238, 38), 12.0f * scale);
        dl->AddRect(panel.P(x + 11.0f, y + 11.0f),
                    panel.P(x + w - 11.0f, y + h - 11.0f),
                    IM_COL32(75, 61, 39, 220), 1.0f * scale, 0, scale);
        const float pivotX = x + w * 0.5f;
        const float pivotY = y + h - 13.0f;
        constexpr float radiusDesign = 165.0f;
        const ImVec2 pivot = panel.P(pivotX, pivotY);
        const float radius = radiusDesign * scale;
        dl->PathArcTo(pivot, radius * 0.87f, -145.0f * kUiPi / 180.0f,
                      -35.0f * kUiPi / 180.0f, 40);
        dl->PathStroke(IM_COL32(92, 70, 38, 185), 0, scale);
        constexpr std::array<const char*, 5> labels{{"20", "15", "10", "5", "0"}};
        for (int tick = 0; tick <= 20; ++tick)
        {
            const float amount = static_cast<float>(tick) / 20.0f;
            const float angle = (-55.0f + 110.0f * amount)
                * kUiPi / 180.0f;
            const ImVec2 direction(std::sin(angle), -std::cos(angle));
            const bool major = tick % 5 == 0;
            dl->AddLine(ImVec2(pivot.x + direction.x * radius
                                   * (major ? 0.70f : 0.78f),
                               pivot.y + direction.y * radius
                                   * (major ? 0.70f : 0.78f)),
                        ImVec2(pivot.x + direction.x * radius * 0.88f,
                               pivot.y + direction.y * radius * 0.88f),
                        kOptoInk, (major ? 1.45f : 0.70f) * scale);
            if (major)
            {
                const size_t label = static_cast<size_t>(tick / 5);
                const float labelRadius = radiusDesign * 0.61f;
                panel.text(dl, pivotX + direction.x * labelRadius,
                           pivotY + direction.y * labelRadius - 4.0f,
                           8.5f, kOptoInk, labels[label], 0, true);
            }
        }
        panel.text(dl, x + 31.0f, y + 54.0f, 12.0f, kOptoInk, "VU", 0, true);
        panel.text(dl, x + w - 32.0f, y + 54.0f, 12.0f, kOptoRed,
                   multicompp::ui_detail::optoMeterLabel(), 0, true);
        panel.text(dl, x + w * 0.5f, y + h - 51.0f, 7.0f,
                   IM_COL32(94, 64, 34, 255), "GAIN REDUCTION", 0, true);
        const float needleAngle = multicompp::ui_detail::optoMeterNeedleAngle(gr);
        const ImVec2 needle(std::sin(needleAngle), -std::cos(needleAngle));
        dl->AddLine(ImVec2(pivot.x + 1.2f * scale, pivot.y + 1.2f * scale),
                    ImVec2(pivot.x + needle.x * radius * 0.87f + 1.2f * scale,
                           pivot.y + needle.y * radius * 0.87f + 1.2f * scale),
                    IM_COL32(0, 0, 0, 75), 2.8f * scale);
        dl->AddLine(pivot,
                    ImVec2(pivot.x + needle.x * radius * 0.87f,
                           pivot.y + needle.y * radius * 0.87f),
                    IM_COL32(97, 47, 31, 255), 1.8f * scale);
        dl->AddCircleFilled(pivot, 7.0f * scale, kOptoInk, 24);
        dl->AddCircleFilled(ImVec2(pivot.x - 1.5f * scale,
                                   pivot.y - 1.5f * scale),
                            2.2f * scale, IM_COL32(179, 151, 99, 255), 16);

        // A subtle glass reflection makes the amber illumination read as a
        // recessed meter rather than a flat painted rectangle.
        std::array<ImVec2, 4> glass{{
            panel.P(x + 17.0f, y + 17.0f), panel.P(x + w * 0.58f, y + 17.0f),
            panel.P(x + w * 0.42f, y + h - 17.0f),
            panel.P(x + 17.0f, y + h - 17.0f)}};
        dl->AddConvexPolyFilled(glass.data(), 4, IM_COL32(255, 255, 255, 18));
    }

    void drawFet(ImDrawList* dl, bool studio)
    {
        if (studio)
        {
            knob(dl, "sf_in", P_FET_IN, 120, 380, "INPUT", "%.1f", " dB");
            knob(dl, "sf_out", P_FET_OUT, 250, 380, "OUTPUT", "%.1f", " dB");
            knob(dl, "sf_att", P_FET_ATTACK, 380, 380, "ATTACK", "%.2f", " ms");
            knob(dl, "sf_rel", P_FET_RELEASE, 510, 380, "RELEASE", "%.0f", " ms");
            knob(dl, "sf_mix", P_MIX, 640, 380, "MIX", "%.0f", "%");
            combo("sf_ratio", P_FET_RATIO, multicompp::kRatios, 5, 775, 352, 118, "RATIO");
            combo("sf_curve", P_FET_CURVE, multicompp::kFetCurve, 2, 905, 352, 142, "CURVE");
            knob(dl, "sf_trans", P_FET_TRANSIENT, 1020, 380, "TRANSIENT", "%.0f", "%");
            panel.text(dl, 560, 500, 12, IM_COL32(80, 215, 205, 255),
                       "Clean FET response with controlled harmonics", 0);
            return;
        }

        constexpr float left = 0.0f, right = 1120.0f;
        constexpr float top = 344.0f, bottom = 654.0f;
        dl->AddRectFilled(panel.P(left + 5, top + 7), panel.P(right + 5, bottom + 7),
                          IM_COL32(0, 0, 0, 150), 4.0f * panel.scale());
        dl->AddRectFilledMultiColor(panel.P(left, top), panel.P(right, bottom),
                                    IM_COL32(27, 28, 27, 255),
                                    IM_COL32(17, 18, 18, 255),
                                    IM_COL32(7, 8, 8, 255),
                                    IM_COL32(13, 14, 14, 255));
        dl->AddRect(panel.P(left, top), panel.P(right, bottom),
                    IM_COL32(114, 116, 111, 255), 4.0f * panel.scale(), 0,
                    1.3f * panel.scale());
        for (int row = static_cast<int>(top + 4); row < static_cast<int>(bottom); row += 4)
            dl->AddLine(panel.P(left + 3, static_cast<float>(row)),
                        panel.P(right - 3, static_cast<float>(row)),
                        row % 8 == 0 ? IM_COL32(255, 255, 255, 9)
                                     : IM_COL32(0, 0, 0, 17), panel.scale());
        // Rack ears and mounting slots use the same full-face construction as
        // Opto mode so switching hardware models feels like swapping rack units.
        dl->AddRectFilled(panel.P(left, top), panel.P(left + 38.0f, bottom),
                          IM_COL32(35, 36, 35, 255));
        dl->AddRectFilled(panel.P(right - 38.0f, top), panel.P(right, bottom),
                          IM_COL32(35, 36, 35, 255));
        dl->AddLine(panel.P(left + 38.0f, top), panel.P(left + 38.0f, bottom),
                    IM_COL32(104, 106, 101, 180), panel.scale());
        dl->AddLine(panel.P(right - 38.0f, top), panel.P(right - 38.0f, bottom),
                    IM_COL32(104, 106, 101, 180), panel.scale());
        for (const float slotY : {top + 62.0f, bottom - 62.0f})
        {
            dl->AddRectFilled(panel.P(-9.0f, slotY - 9.0f),
                              panel.P(23.0f, slotY + 9.0f),
                              IM_COL32(2, 3, 3, 255), 8.0f * panel.scale());
            dl->AddRectFilled(panel.P(right - 23.0f, slotY - 9.0f),
                              panel.P(right + 9.0f, slotY + 9.0f),
                              IM_COL32(2, 3, 3, 255), 8.0f * panel.scale());
        }
        for (const ImVec2 screw : {ImVec2(left + 73, top + 18),
                                   ImVec2(right - 73, top + 18),
                                   ImVec2(left + 73, bottom - 18),
                                   ImVec2(right - 73, bottom - 18)})
        {
            dl->AddCircleFilled(panel.P(screw.x, screw.y), 5.5f * panel.scale(),
                                IM_COL32(130, 132, 127, 255), 24);
            dl->AddLine(panel.P(screw.x - 3.0f, screw.y),
                        panel.P(screw.x + 3.0f, screw.y),
                        IM_COL32(38, 39, 38, 255), panel.scale());
        }

        // Match the reference unit's left-to-right reading order: two large gain
        // controls, vertically paired timing controls, ratio buttons, the same
        // VU assembly proven in Opto mode, then the fixed GR meter bank.
        fetKnob(dl, "fet_in", P_FET_IN, 194, 496, 45.0f,
                "INPUT", true, "%.1f", " dB");
        fetKnob(dl, "fet_out", P_FET_OUT, 390, 496, 45.0f,
                "OUTPUT", true, "%.1f", " dB");
        fetKnob(dl, "fet_att", P_FET_ATTACK, 548, 422, 22.0f,
                "ATTACK", false, "%.2f", "");
        fetKnob(dl, "fet_rel", P_FET_RELEASE, 548, 526, 22.0f,
                "RELEASE", false, "%.2f", "");
        drawFetRatioButtons(dl, 635, 392);
        drawOptoMeter(dl, 675, 384, 300, 178,
                      multicompp::ui_detail::optoMeterDisplayValue(meter(kMeterMaster)));
        panel.text(dl, 825, top + 14, 13.0f, IM_COL32(234, 234, 221, 255),
                   "FET 76", 0, true);
        panel.text(dl, 825, top + 32, 7.2f, IM_COL32(177, 179, 170, 255),
                   "REFERENCE SERIES", 0, true);
        panel.text(dl, 825, top + 226, 9.2f, IM_COL32(234, 234, 221, 255),
                   "MC-2", 0, true);
        panel.text(dl, 825, top + 243, 8.0f, IM_COL32(202, 204, 194, 255),
                   "LIMITING AMPLIFIER", 0, true);
        drawFetMeterSwitch(dl, 997, 393);
        fetTrimKnob(dl, "fet_mix", P_MIX, 1048, 594, 13.0f,
                    "MIX", "%.0f", "%");
    }

    void fetKnob(ImDrawList* dl, const char* id, uint32_t p,
                 float x, float y, float radius, const char* label,
                 bool gainScale, const char* format, const char* suffix)
    {
        // Reuse the shared gesture and type-entry contract, but draw the body
        // here so FET mode gets the reference unit's silver skirt and knurling.
        fetKnobSplitter.Split(dl, 2);
        fetKnobSplitter.SetCurrentChannel(dl, 1);
        const bool timingDial = multicompp::ui_detail::fetTimingUsesDialReadout(p);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   timingDial ? &fetTimingHostToDial : &knobHostToPlain,
                   timingDial ? &fetTimingDialToHost : &knobPlainToHost,
                   this, true);
        fetKnobSplitter.SetCurrentChannel(dl, 0);

        const float plain = plainValueForHost(p, values[p]);
        const float minimum = plainValueForHost(p, hostMinimum(p));
        const float maximum = plainValueForHost(p, hostMaximum(p));
        const float t = timingDial
            ? std::clamp((multicompp::ui_detail::fetTimingDialValue(values[p], p) - 1.0f)
                             / 6.0f,
                         0.0f, 1.0f)
            : std::clamp((plain - minimum)
                             / std::max(maximum - minimum, 1.0e-6f),
                         0.0f, 1.0f);
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        const float r = radius * scale;

        if (gainScale)
        {
            constexpr std::array<const char*, 9> labels{{
                "INF", "48", "36", "30", "24", "18", "12", "6", "0"}};
            for (int tick = 0; tick <= 16; ++tick)
            {
                const float tickT = static_cast<float>(tick) / 16.0f;
                const float angle = duskdaf::DuskPanel::knobAngle(tickT);
                const ImVec2 direction(std::sin(angle), -std::cos(angle));
                const bool major = tick % 2 == 0;
                const float inner = r + (major ? 4.0f : 7.0f) * scale;
                const float outer = r + 10.0f * scale;
                dl->AddLine(ImVec2(center.x + direction.x * inner,
                                   center.y + direction.y * inner),
                            ImVec2(center.x + direction.x * outer,
                                   center.y + direction.y * outer),
                            IM_COL32(229, 230, 219, 240),
                            (major ? 1.25f : 0.75f) * scale);
                if (major)
                    panel.text(dl, x + direction.x * (radius + 19.0f),
                               y + direction.y * (radius + 19.0f) - 3.0f,
                               7.3f, IM_COL32(232, 232, 220, 255),
                               labels[static_cast<size_t>(tick / 2)], 0, true);
            }
        }
        else
        {
            constexpr std::array<const char*, 4> labels{{"1", "3", "5", "7"}};
            for (int tick = 0; tick <= 6; ++tick)
            {
                const float tickT = static_cast<float>(tick) / 6.0f;
                const float angle = duskdaf::DuskPanel::knobAngle(tickT);
                const ImVec2 direction(std::sin(angle), -std::cos(angle));
                const bool major = tick % 2 == 0;
                dl->AddCircleFilled(ImVec2(center.x + direction.x * (r + 7.0f * scale),
                                           center.y + direction.y * (r + 7.0f * scale)),
                                    (major ? 1.45f : 0.85f) * scale,
                                    IM_COL32(230, 230, 218, 255), 10);
                if (major)
                    panel.text(dl, x + direction.x * (radius + 15.0f),
                               y + direction.y * (radius + 15.0f) - 3.0f,
                               7.0f, IM_COL32(232, 232, 220, 255),
                               labels[static_cast<size_t>(tick / 2)], 0, true);
            }
        }

        // Soft shadow, concentric spun-metal skirt, knurled black collar and
        // machined cap. Keep every structural circle on the exact center; the
        // previous offset 91% disk read as a second gray knob at upper left.
        dl->AddCircleFilled(ImVec2(center.x + 3.0f * scale, center.y + 4.0f * scale),
                            r * 1.01f, IM_COL32(0, 0, 0, 150), 56);
        dl->AddCircleFilled(center, r, IM_COL32(66, 67, 66, 255), 56);
        dl->AddCircleFilled(center, r * 0.96f, IM_COL32(139, 140, 137, 255), 56);
        dl->AddCircleFilled(center, r * 0.83f, IM_COL32(176, 177, 173, 255), 56);
        dl->AddCircleFilled(center, r * 0.75f, IM_COL32(91, 92, 90, 255), 56);
        dl->PathArcTo(center, r * 0.90f, -2.55f, -0.55f, 30);
        dl->PathStroke(IM_COL32(238, 239, 234, 75), 0, 1.5f * scale);
        dl->PathArcTo(center, r * 0.91f, 0.55f, 2.55f, 30);
        dl->PathStroke(IM_COL32(24, 25, 24, 110), 0, 1.8f * scale);
        dl->AddCircle(center, r, IM_COL32(31, 32, 31, 255), 56, 1.3f * scale);
        const float collarR = r * 0.69f;
        dl->AddCircleFilled(center, collarR, IM_COL32(20, 20, 20, 255), 52);
        for (int notch = 0; notch < 28; ++notch)
        {
            const float angle = 2.0f * kUiPi * static_cast<float>(notch) / 28.0f;
            const ImVec2 direction(std::sin(angle), -std::cos(angle));
            dl->AddLine(ImVec2(center.x + direction.x * collarR * 0.78f,
                               center.y + direction.y * collarR * 0.78f),
                        ImVec2(center.x + direction.x * collarR,
                               center.y + direction.y * collarR),
                        IM_COL32(92, 93, 90, 235), 1.0f * scale);
        }
        const float capR = r * 0.49f;
        dl->AddCircleFilled(center, capR, IM_COL32(111, 112, 110, 255), 48);
        dl->AddCircleFilled(center, capR * 0.92f,
                            IM_COL32(198, 199, 196, 255), 48);
        dl->PathArcTo(center, capR * 0.78f, -2.50f, -0.65f, 22);
        dl->PathStroke(IM_COL32(255, 255, 251, 95), 0, 1.2f * scale);
        dl->AddCircle(center, capR, IM_COL32(44, 45, 44, 255), 48, 1.1f * scale);
        const float pointerAngle = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 pointer(std::sin(pointerAngle), -std::cos(pointerAngle));
        dl->AddLine(ImVec2(center.x + pointer.x * capR * 0.12f,
                           center.y + pointer.y * capR * 0.12f),
                    ImVec2(center.x + pointer.x * capR * 0.88f,
                           center.y + pointer.y * capR * 0.88f),
                    IM_COL32(36, 37, 36, 255), 2.2f * scale);
        dl->AddCircleFilled(ImVec2(center.x + pointer.x * collarR * 0.88f,
                                   center.y + pointer.y * collarR * 0.88f),
                            1.8f * scale, IM_COL32(221, 197, 145, 255), 12);

        panel.text(dl, x, gainScale ? y + radius + 27.0f
                                    : y + (label[0] == 'A' ? -50.0f : 39.0f),
                   gainScale ? 8.8f : 8.0f,
                   IM_COL32(235, 235, 222, 255), label, 0, true);
        fetKnobSplitter.Merge(dl);
    }

    void fetTrimKnob(ImDrawList* dl, const char* id, uint32_t p,
                     float x, float y, float radius, const char* label,
                     const char* format, const char* suffix)
    {
        // The Opto face established the fleet contract for non-reference
        // controls: keep the full parameter available as a small set-screw trim
        // without letting it compete with the hardware's primary controls.
        fetKnobSplitter.Split(dl, 2);
        fetKnobSplitter.SetCurrentChannel(dl, 1);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 8.0f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this, true);
        fetKnobSplitter.SetCurrentChannel(dl, 0);

        const float plain = plainValueForHost(p, values[p]);
        const float minimum = plainValueForHost(p, hostMinimum(p));
        const float maximum = plainValueForHost(p, hostMaximum(p));
        const float t = std::clamp((plain - minimum)
            / std::max(maximum - minimum, 1.0e-6f), 0.0f, 1.0f);
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        const float r = radius * scale;

        std::array<ImVec2, 6> nut{};
        for (int point = 0; point < 6; ++point)
        {
            const float angle = kUiPi / 6.0f + static_cast<float>(point)
                * kUiPi / 3.0f;
            nut[static_cast<size_t>(point)] = ImVec2(
                center.x + std::cos(angle) * r * 1.18f,
                center.y + std::sin(angle) * r * 1.18f);
        }
        std::array<ImVec2, 6> shadow = nut;
        for (auto& point : shadow)
        {
            point.x += 2.0f * scale;
            point.y += 3.0f * scale;
        }
        dl->AddConvexPolyFilled(shadow.data(), 6, IM_COL32(0, 0, 0, 120));
        dl->AddConvexPolyFilled(nut.data(), 6, IM_COL32(94, 96, 93, 255));
        dl->AddCircleFilled(center, r, IM_COL32(116, 118, 115, 255), 32);
        dl->AddCircleFilled(center, r * 0.84f,
                            IM_COL32(190, 192, 188, 255), 32);
        dl->PathArcTo(center, r * 0.70f, -2.45f, -0.75f, 18);
        dl->PathStroke(IM_COL32(247, 248, 242, 80), 0, panel.scale());
        dl->AddCircle(center, r, IM_COL32(43, 44, 43, 255), 32, 1.1f * scale);
        const float angle = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 pointer(std::sin(angle), -std::cos(angle));
        dl->AddLine(ImVec2(center.x - pointer.x * r * 0.58f,
                           center.y - pointer.y * r * 0.58f),
                    ImVec2(center.x + pointer.x * r * 0.58f,
                           center.y + pointer.y * r * 0.58f),
                    IM_COL32(42, 43, 42, 255), 2.0f * scale);
        panel.text(dl, x, y - 38.0f, 7.5f,
                   IM_COL32(225, 226, 215, 255), label, 0, true);
        panel.text(dl, x - 27.0f, y - 5.0f, 10.0f,
                   IM_COL32(157, 160, 152, 255), "-", 0, true);
        panel.text(dl, x + 27.0f, y - 5.0f, 10.0f,
                   IM_COL32(210, 212, 201, 255), "+", 0, true);
        fetKnobSplitter.Merge(dl);
    }

    void drawFetRatioButtons(ImDrawList* dl, float x, float y)
    {
        const int selected = multicompp::ui_detail::choiceIndex(value(P_FET_RATIO), 5);
        panel.text(dl, x + 14, y - 23, 8.5f, IM_COL32(230, 231, 219, 255),
                   "RATIO", 0, true);
        constexpr std::array<int, 5> visualOrder{{3, 2, 1, 0, 4}};
        constexpr std::array<const char*, 5> visualLabels{{
            "20", "12", "8", "4", "ALL"}};
        for (int row = 0; row < 5; ++row)
        {
            const int index = visualOrder[static_cast<size_t>(row)];
            const float y0 = y + static_cast<float>(row) * 31.0f;
            const ImVec2 p0 = panel.P(x, y0);
            const ImVec2 p1 = panel.P(x + 28.0f, y0 + 27.0f);
            char id[32];
            std::snprintf(id, sizeof(id), "##fet_ratio_button_%d", index);
            ImGui::SetCursorScreenPos(p0);
            const bool clicked = ImGui::InvisibleButton(
                id, ImVec2(p1.x - p0.x, p1.y - p0.y));
            const bool active = index == selected;
            dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f),
                              panel.P(x + 31.0f, y0 + 30.0f),
                              IM_COL32(0, 0, 0, 130), 1.5f * panel.scale());
            dl->AddRectFilled(p0, p1,
                              active ? IM_COL32(24, 24, 23, 255)
                                     : ImGui::IsItemHovered()
                                         ? IM_COL32(52, 52, 49, 255)
                                         : IM_COL32(31, 31, 29, 255),
                              1.5f * panel.scale());
            dl->AddRect(p0, p1, IM_COL32(91, 92, 87, 255),
                        1.5f * panel.scale(), 0, panel.scale());
            if (active)
                dl->AddRectFilled(panel.P(x + 1.0f, y0 + 2.0f),
                                  panel.P(x + 3.0f, y0 + 25.0f),
                                  index == 4 ? IM_COL32(160, 48, 34, 255)
                                             : IM_COL32(226, 151, 61, 255));
            panel.text(dl, x - 10.0f, y0 + 7.0f, 8.0f,
                       active ? IM_COL32(246, 224, 177, 255)
                              : IM_COL32(228, 228, 216, 255),
                       visualLabels[static_cast<size_t>(row)], 1, true);
            if (clicked && !active) setValue(P_FET_RATIO, static_cast<float>(index));
        }
    }

    void drawFetMeterSwitch(ImDrawList* dl, float x, float y)
    {
        panel.text(dl, x + 10.0f, y - 24.0f, 8.5f,
                   IM_COL32(230, 231, 219, 255), "METER", 0, true);
        constexpr std::array<const char*, 4> labels{{"GR", "+8", "+4", "OFF"}};
        for (int row = 0; row < 4; ++row)
        {
            const float y0 = y + static_cast<float>(row) * 38.0f;
            dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f),
                              panel.P(x + 23.0f, y0 + 35.0f),
                              IM_COL32(0, 0, 0, 145), 1.5f * panel.scale());
            dl->AddRectFilled(panel.P(x, y0), panel.P(x + 20.0f, y0 + 32.0f),
                              IM_COL32(29, 29, 28, 255), 1.5f * panel.scale());
            dl->AddRect(panel.P(x, y0), panel.P(x + 20.0f, y0 + 32.0f),
                        IM_COL32(87, 88, 84, 255), 1.5f * panel.scale(), 0,
                        panel.scale());
            if (row == 0)
                dl->AddRectFilled(panel.P(x + 1.0f, y0 + 2.0f),
                                  panel.P(x + 3.0f, y0 + 30.0f),
                                  IM_COL32(226, 151, 61, 255));
            panel.text(dl, x + 31.0f, y0 + 9.0f, 8.0f,
                       row == 0 ? IM_COL32(246, 224, 177, 255)
                                : IM_COL32(227, 227, 215, 255),
                       labels[static_cast<size_t>(row)], -1, true);
        }
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
        multicompp::applyFactoryPresetToHostParameters(
            static_cast<uint32_t>(index),
            [this](int parameterIndex, float hostValue)
            {
                setHostValue(static_cast<uint32_t>(parameterIndex), hostValue);
            });
        currentPreset = index;
        currentUserName.clear();
        currentUserPath.clear();
        defaultsActive = false;
    }
};

UI* createUI() { return new MultiCompUI(); }

END_NAMESPACE_DAF

#endif // MULTICOMP_UI_LOGIC_TEST

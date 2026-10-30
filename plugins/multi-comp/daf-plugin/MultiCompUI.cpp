// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Multi-Comp 2 Dear ImGui UI.  The controls mirror EnhancedCompressorEditor and
// ModernCompressorPanels; the DSP and parameter ownership remain in the shell/core.

#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"
#include "MultiCompVintageMeterGeometry.hpp"
#include "MultiCompVintageMeterControls.hpp"
#include "../core/MultiCompVcaLaw.hpp"
#include "../core/MultiCompStudioVcaLaw.hpp"
#include "../../shared-daf/ui/DuskValueText.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>

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

// The VCA face shares the rack-height canvas with the Opto and FET faces
// (1120 x 310). The reference VCA compressor's own ~2.5:1 proportions were tried on the tall
// canvas (2026-09-01) and abandoned: the AU wrapper's resize path leaves the
// GL view at half size / the host keeps the old window (#240), so a mode
// whose canvas differs from the opening size shows up shrunken in Logic.
struct VcaFaceplateLayout
{
    static constexpr float left = 0.0f;
    static constexpr float right = 1120.0f;
    static constexpr float top = 344.0f;
    static constexpr float bottom = 654.0f;
};

inline constexpr float vcaFaceplateAspect() noexcept
{
    return (VcaFaceplateLayout::right - VcaFaceplateLayout::left)
        / (VcaFaceplateLayout::bottom - VcaFaceplateLayout::top);
}

// Every mode shares one rack-height canvas (1120 x 380), so a mode change never resizes the window: a mode
// whose canvas differed from the opening size showed up shrunken in Logic (#240).
inline constexpr float kDesignHeight = 380.0f;

// Digital and Multiband content bounds in visible-canvas y (0 = the top of the header).
// The rack shell extends to the canvas edges; its lettering keeps an 8 px bottom margin.
struct ModernFaceLayout
{
    static constexpr float top = 72.0f, bottom = 372.0f;
    static constexpr float titleY = 89.0f, descriptionY = 361.0f;
    static constexpr float meterTitleY = 112.0f, meterTop = 122.0f, meterBottom = 322.0f, meterReadoutY = 337.0f;
};

// VCA-compressor-style threshold lamps: BELOW lights while the programme sits under the
// threshold, ABOVE from the threshold up. One boolean keeps the pair mutually
// exclusive by construction.
inline bool vcaSignalAboveThreshold(float inputDb, float thresholdDb) noexcept
{
    if (!std::isfinite(inputDb) || !std::isfinite(thresholdDb)) return false;
    return inputDb >= thresholdDb;
}

// The VCA face's meter selector is display-only hardware behaviour (matching
// the reference unit's INPUT / OUTPUT / GAIN CHANGE buttons), so it is UI
// state, not a host parameter.
inline constexpr int kVcaMeterSourceCount = 3;

inline const char* vcaMeterSourceLabel(int source) noexcept
{
    switch (source)
    {
        case 0: return "INPUT";
        case 1: return "OUTPUT";
        default: return "GAIN CHANGE";
    }
}

// Mode toolbar captions, set in capitals like every other legend on the
// panel. The host-facing enum labels stay kModes.
inline constexpr std::array<const char*, 8> kModeTabLabels{{
    "OPTO", "FET BLACK", "VCA", "BUS", "FET BLUE", "STUDIO VCA", "DIGITAL", "MULTIBAND"}};
static_assert(kModeTabLabels.size() == sizeof(kModes) / sizeof(kModes[0]),
              "one toolbar caption per mode");

inline float vcaSidechainSwitchTarget(bool currentlyEngaged,
                                      float currentHostValue,
                                      float& lastEngagedHostValue,
                                      float minimumHostValue,
                                      float maximumHostValue) noexcept
{
    if (currentlyEngaged)
    {
        lastEngagedHostValue = std::clamp(
            currentHostValue, minimumHostValue, maximumHostValue);
        return minimumHostValue;
    }
    return std::clamp(lastEngagedHostValue, minimumHostValue, maximumHostValue);
}

inline float optoMeterNeedleAngle(float gainReductionDb) noexcept
{
    return vintageVuNeedleAngle(gainReductionDb);
}

inline float optoMeterDisplayValue(float gainReductionDb) noexcept
{
    // Only a finite guard. The Opto and FET needles' movement runs in the plugin
    // on the audio clock (MultiCompGainReductionNeedle.hpp, a VU movement measured
    // against both references). (A UI-side one-pole lag of 300 ms once stood here:
    // a VU reaches 99% in 300 ms, so that pole returned the needle about four
    // times too late.)
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

// FET BLUE's Attack has the reference's SLO detent at its counter-clockwise stop: the loop takes its slower attack
// and raised threshold (FetVoice::sloAttackScales, sloLoadDb) while the attack position is 0, i.e. while the plain
// value the DSP receives sits on the descriptor's minimum. The face reads the core's own position law
// (duskaudio::fetAttackPositionFromMilliseconds), so it names SLO exactly where the DSP engages it.
// The reference names that stop SLO in its own readout. The face prints SLO there, and its readout and typed entry
// name the detent.
inline constexpr const char* kFetSloText = "SLO";

inline bool fetAttackAtSlo(float plainMilliseconds) noexcept
{
    return duskaudio::fetAttackPositionFromMilliseconds(plainMilliseconds) <= 0.0f;
}

// Typed entry on FET BLUE's Attack, returning the host value: SLO (SLO, slo or Slo) selects the detent; a number is a
// dial reading as the face shows it (1-7, FET BLACK's conversion). Anything else is refused.
inline bool fetSloAttackTypedHost(const char* text, float& host) noexcept
{
    namespace entry = duskdaf::value_text;
    const auto attack = static_cast<uint32_t>(ParamId::FetAttack);
    if (entry::suffix(text, kFetSloText, "slo") || entry::suffix(text, "Slo"))
    {
        host = hostMin(kParams[attack]);
        return true;
    }
    const char* end = nullptr;
    float dial = 0.0f;
    if (!entry::parseDecimal(text, end, dial) || !entry::finish(end, "")) return false;
    host = fetTimingHostValue(dial, attack);
    return true;
}

inline bool optoKnobUsesPlainDomainDrag(uint32_t parameter) noexcept
{
    return parameter == static_cast<uint32_t>(ParamId::SidechainHP);
}

inline const char* optoModeLabel(float hostValue) noexcept
{
    return choiceIndex(hostValue, 2) == 0 ? "COMPRESS" : "LIMIT";
}

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
    if (isDisplayParameter(static_cast<int>(parameterIndex))) return false;
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
// Studio VCA face dials: the reference panel's knob travel (0..1) for a control's plain value, and back. Ratio and
// Attack follow the reference's own dial laws (MultiCompStudioVcaLaw.hpp); the others are linear. The host ranges are
// wider than the panel's (they keep older sessions' values: threshold up to +20 dB, ratio from 1:1, attack to 75 ms,
// make-up from -20 dB), so such a value pins the dial at the end it lies beyond; typed entry still reaches it.
inline float studioVcaDialPosition(uint32_t parameter, float plain) noexcept
{
    namespace law = duskaudio::studioVcaLaw;
    const auto linear = [plain](float lo, float hi) { return std::clamp((plain - lo) / (hi - lo), 0.0f, 1.0f); };
    switch (static_cast<ParamId>(parameter))
    {
        case ParamId::StudioVcaThreshold: return linear(-50.0f, -10.0f);
        case ParamId::StudioVcaRatio: return law::knobForRatio(plain);
        case ParamId::StudioVcaAttack: return law::knobForAttackMs(plain);
        case ParamId::StudioVcaRelease: return linear(100.0f, 4000.0f);
        case ParamId::StudioVcaOutput: return linear(0.0f, 40.0f);
        case ParamId::StudioVcaInput: return linear(-18.0f, 18.0f);
        case ParamId::Mix: return linear(0.0f, 100.0f);
        default: return 0.0f;
    }
}

inline float studioVcaDialPlain(uint32_t parameter, float position) noexcept
{
    namespace law = duskaudio::studioVcaLaw;
    const float t = std::clamp(position, 0.0f, 1.0f);
    const auto linear = [t](float lo, float hi) { return lo + (hi - lo) * t; };
    switch (static_cast<ParamId>(parameter))
    {
        case ParamId::StudioVcaThreshold: return linear(-50.0f, -10.0f);
        case ParamId::StudioVcaRatio: return law::ratioForKnob(t);
        case ParamId::StudioVcaAttack: return law::attackMsForKnob(t);
        case ParamId::StudioVcaRelease: return linear(100.0f, 4000.0f);
        case ParamId::StudioVcaOutput: return linear(0.0f, 40.0f);
        case ParamId::StudioVcaInput: return linear(-18.0f, 18.0f);
        case ParamId::Mix: return linear(0.0f, 100.0f);
        default: return 0.0f;
    }
}

// A value typed into a Studio VCA dial, in the control's plain unit (Release in ms). The unit may be left out, as the
// readout prints it, or named: dB (or db), :1, ms, %, and for Release s (the dial's unit, also a bare number's) or ms.
inline bool studioVcaTypedPlain(const char* text, uint32_t parameter, float& plain) noexcept
{
    namespace entry = duskdaf::value_text;
    const char* end = nullptr;
    float typed = 0.0f;
    if (!entry::parseDecimal(text, end, typed)) return false;
    const bool bare = entry::finish(end, "");
    plain = typed;
    switch (static_cast<ParamId>(parameter))
    {
        case ParamId::StudioVcaRelease:
            if (entry::suffix(end, "ms")) return true;
            plain = typed * 1000.0f;
            return bare || entry::suffix(end, "s");
        case ParamId::StudioVcaRatio: return bare || entry::suffix(end, ":1");
        case ParamId::StudioVcaAttack: return bare || entry::suffix(end, "ms");
        case ParamId::Mix: return bare || entry::suffix(end, "%");
        default: return bare || entry::suffix(end, "dB", "db");
    }
}

// The Studio VCA face's needles, as the reference's sweep (measured 2026-10-26): +-30 degrees from straight up over
// minDb..maxDb, linear in dB (the I/O meter -60..0, 1 degree per dB; gain reduction -20..0, 3 degrees per dB),
// pinned at the ends; a non-finite reading rests at the left.
inline float studioVcaNeedleDegrees(float db, float minDb, float maxDb) noexcept
{
    const float t = std::isfinite(db) ? std::clamp((db - minDb) / (maxDb - minDb), 0.0f, 1.0f) : 0.0f;
    return -30.0f + 60.0f * t;
}

// The Digital and Multiband faces follow the original JUCE editor (EnhancedCompressorEditor, ModernCompressorPanels.h,
// AnalogLookAndFeel.cpp). Their moving parts keep its timing, transcribed here frame-rate independent.

// The number under an INPUT / OUTPUT ladder (EnhancedCompressorEditor::updateMeters): every 0.2 s it jumps up to the
// loudest reading since the last update, or else falls 1.2 dB (6 dB/s), never below -60 dB.
struct LevelReadout
{
    float shownDb = -60.0f, peakDb = -60.0f, clock = 0.0f;
};

inline void advanceLevelReadout(LevelReadout& readout, float db, float dtSeconds) noexcept
{
    if (std::isfinite(db)) readout.peakDb = std::max(readout.peakDb, db);
    readout.clock += std::clamp(dtSeconds, 0.0f, 0.1f);
    if (readout.clock < 0.2f) return;
    readout.clock -= 0.2f;
    readout.shownDb = std::max(readout.peakDb > readout.shownDb ? readout.peakDb : readout.shownDb - 1.2f, -60.0f);
    readout.peakDb = -60.0f;
}

// The Digital face's gain-reduction VU (AnalogVUMeter::timerCallback, 60 Hz steps): -20..+3 dB over 0..1, 50 ms
// attack and 150 ms release blended with a slightly underdamped spring (about 1 % overshoot), and a peak marker that
// holds the deepest reading for 2 s.
struct VuNeedle
{
    float position = 20.0f / 23.0f, velocity = 0.0f, pending = 0.0f;
    float peakDb = 0.0f, peakHold = 0.0f, peakPosition = 20.0f / 23.0f;
};

inline float vuNeedlePositionForDb(float db) noexcept
{
    const float clamped = std::isfinite(db) ? std::clamp(db, -20.0f, 3.0f) : 0.0f;
    return (clamped + 20.0f) / 23.0f;
}

inline void advanceVuNeedle(VuNeedle& needle, float grDb, float dtSeconds) noexcept
{
    constexpr float kStep = 1.0f / 60.0f;
    const float target = std::isfinite(grDb) ? grDb : 0.0f;
    if (target < needle.peakDb)
    {
        needle.peakDb = target;
        needle.peakHold = 2.0f;
    }
    needle.pending = std::min(needle.pending + std::clamp(dtSeconds, 0.0f, 0.25f), 0.25f);
    while (needle.pending >= kStep)
    {
        needle.pending -= kStep;
        const float targetPosition = vuNeedlePositionForDb(std::abs(target) < 0.001f ? 0.0f : target);
        const float displacement = targetPosition - needle.position;
        const float timeConstantMs = displacement < 0.0f ? 50.0f : 150.0f;
        const float coefficient = 1.0f - std::exp(-1000.0f * kStep / timeConstantMs);
        const float acceleration = displacement * 200.0f - needle.velocity * 0.65f * 2.0f * std::sqrt(200.0f);
        needle.velocity += acceleration * kStep;
        needle.position += needle.velocity * kStep;
        needle.position += coefficient * (targetPosition - needle.position) * 0.4f;
        needle.position = std::clamp(needle.position, 0.0f, 1.0f);
        if (std::abs(needle.velocity) < 0.0005f && std::abs(displacement) < 0.001f)
            needle.velocity = 0.0f;
        if (needle.peakHold > 0.0f)
        {
            needle.peakHold -= kStep;
            if (needle.peakHold <= 0.0f) needle.peakDb = target;
        }
        needle.peakPosition = vuNeedlePositionForDb(needle.peakDb);
    }
}

// A Multiband band's gain-reduction ladder (MultibandCompressorPanel::setBandGainReduction, 30 Hz steps): half way to
// a deeper reading per step, 15 % back per step, and a peak that holds for 30 steps (1 s) before falling 5 % per step.
struct BandGrMeter
{
    float shownDb = 0.0f, peakDb = 0.0f, pending = 0.0f;
    int peakHoldSteps = 0;
};

inline void advanceBandGrMeter(BandGrMeter& meter, float grDb, float dtSeconds) noexcept
{
    constexpr float kStep = 1.0f / 30.0f;
    const float target = std::isfinite(grDb) ? grDb : 0.0f;
    meter.pending = std::min(meter.pending + std::clamp(dtSeconds, 0.0f, 0.25f), 0.25f);
    while (meter.pending >= kStep)
    {
        meter.pending -= kStep;
        meter.shownDb += (target - meter.shownDb) * (target < meter.shownDb ? 0.5f : 0.15f);
        if (-meter.shownDb > -meter.peakDb)
        {
            meter.peakDb = meter.shownDb;
            meter.peakHoldSteps = 30;
        }
        else if (meter.peakHoldSteps > 0)
            --meter.peakHoldSteps;
        else
            meter.peakDb *= 0.95f;
    }
}

// A Multiband crossover fader's travel (CrossoverFader): the frequency's place between the control's limits on a
// logarithmic axis, 0 at the lowest frequency, and back.
inline float crossoverFaderPosition(float hz, float minHz, float maxHz) noexcept
{
    const float lo = std::max(0.1f, minHz), hi = std::max(lo + 0.1f, maxHz);
    return std::log(std::clamp(hz, lo, hi) / lo) / std::log(hi / lo);
}

inline float crossoverFaderFrequency(float position, float minHz, float maxHz) noexcept
{
    const float lo = std::max(0.1f, minHz), hi = std::max(lo + 0.1f, maxHz);
    return lo * std::pow(hi / lo, std::clamp(position, 0.0f, 1.0f));
}

// The bands the Multiband face shows: the enabled ones, repaired to at least two the way the DSP repairs its
// snapshot (MultiCompDSP's multiband block adds the lowest disabled bands), so the face never shows fewer bands
// than are playing. Bit b set = band b shown.
inline unsigned multibandShownBands(const bool enabled[4]) noexcept
{
    unsigned mask = 0;
    int count = 0;
    for (int band = 0; band < 4; ++band)
        if (enabled[band]) { mask |= 1u << band; ++count; }
    for (int band = 0; band < 4 && count < 2; ++band)
        if ((mask & (1u << band)) == 0) { mask |= 1u << band; ++count; }
    return mask;
}

} // namespace multicompp::ui_detail

#ifndef MULTICOMP_UI_LOGIC_TEST

#ifndef MULTICOMP_UI_TEST_HOST
#include "DafUI.hpp"
#endif
#include "MultiCompAccess.hpp"
#include "MultiCompVersion.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"
#include "DuskKnobRing.hpp"
#include "DuskLedMeter.hpp"
#include "DuskVuMeter.hpp"
#include "MultiCompGainReductionNeedle.hpp"
#include "MultiCompVcaMeter.hpp"
#include "MultiCompVintageMeter.hpp"
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
constexpr float kDesignH = multicompp::ui_detail::kDesignHeight;
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
constexpr ImU32 kVcaInk = IM_COL32(228, 229, 224, 255);
constexpr ImU32 kVcaInkDim = IM_COL32(168, 170, 166, 255);
// Silkscreen and small functional accents on the Digital / Multiband rack faces.
constexpr ImU32 kModernText = IM_COL32(228, 229, 224, 255);
constexpr ImU32 kDigitalCyan = IM_COL32(121, 185, 191, 255);
constexpr ImU32 kBandColors[4] = {
    IM_COL32(205, 127, 117, 255), IM_COL32(210, 187, 116, 255),
    IM_COL32(137, 177, 141, 255), IM_COL32(127, 162, 196, 255)
};

// Parameter indices are aliases of the single table order in MultiCompParams.hpp.
using ParamId = multicompp::ParamId;
constexpr uint32_t P_MODE = static_cast<uint32_t>(ParamId::Mode), P_BYPASS = static_cast<uint32_t>(ParamId::Bypass), P_MIX = static_cast<uint32_t>(ParamId::Mix);
constexpr uint32_t P_SC_HP = static_cast<uint32_t>(ParamId::SidechainHP);
constexpr uint32_t P_AUTO_MAKEUP = static_cast<uint32_t>(ParamId::AutoMakeup);
#define MC_PID(name) static_cast<uint32_t>(ParamId::name)
constexpr uint32_t P_OPTO_PEAK = MC_PID(OptoPeakReduction), P_OPTO_GAIN = MC_PID(OptoGain), P_OPTO_LIMIT = MC_PID(OptoLimit);
constexpr uint32_t P_OPTO_METER = MC_PID(OptoMeter), P_FET_METER = MC_PID(FetMeter);
constexpr uint32_t P_FET_IN = MC_PID(FetInput), P_FET_OUT = MC_PID(FetOutput), P_FET_ATTACK = MC_PID(FetAttack), P_FET_RELEASE = MC_PID(FetRelease);
constexpr uint32_t P_FET_RATIO = MC_PID(FetRatio), P_FET_CURVE = MC_PID(FetCurve), P_FET_TRANSIENT = MC_PID(FetTransient), P_FET_THRESHOLD = MC_PID(FetThreshold);
constexpr uint32_t P_FET_BLUE_RATIO = MC_PID(FetBlueRatio), P_FET_BLUE_SC = MC_PID(FetBlueScFilter), P_FET_BLUE_HEADROOM = MC_PID(FetBlueHeadroom);
constexpr uint32_t P_VCA_THRESHOLD = MC_PID(VcaThreshold), P_VCA_RATIO = MC_PID(VcaRatio), P_VCA_ATTACK = MC_PID(VcaAttack), P_VCA_RELEASE = MC_PID(VcaRelease);
constexpr uint32_t P_VCA_OUT = MC_PID(VcaOutput), P_VCA_OVER_EASY = MC_PID(VcaOverEasy), P_VCA_DETECTOR = MC_PID(VcaClassicDetector);
constexpr uint32_t P_BUS_THRESHOLD = MC_PID(BusThreshold), P_BUS_RATIO = MC_PID(BusRatio), P_BUS_ATTACK = MC_PID(BusAttack), P_BUS_RELEASE = MC_PID(BusRelease);
constexpr uint32_t P_BUS_MAKEUP = MC_PID(BusMakeup), P_BUS_MIX = MC_PID(BusMix);
constexpr uint32_t P_BUS_HEADROOM = MC_PID(BusHeadroom), P_BUS_FADE_RATE = MC_PID(BusFadeRate);
constexpr uint32_t P_BUS_FADE = MC_PID(BusFade), P_BUS_SC = MC_PID(SidechainHP);
constexpr uint32_t P_SVCA_THRESHOLD = MC_PID(StudioVcaThreshold), P_SVCA_RATIO = MC_PID(StudioVcaRatio), P_SVCA_ATTACK = MC_PID(StudioVcaAttack);
constexpr uint32_t P_SVCA_RELEASE = MC_PID(StudioVcaRelease), P_SVCA_OUT = MC_PID(StudioVcaOutput);
constexpr uint32_t P_SVCA_INPUT = MC_PID(StudioVcaInput), P_SVCA_AUTO = MC_PID(StudioVcaAutoRelease);
constexpr uint32_t P_SVCA_METER = MC_PID(StudioVcaMeter);
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

// The original editor's colour arithmetic (juce::Colour), for the Digital and Multiband faces.
ImU32 colourWithAlpha(ImU32 colour, float alpha)
{
    const auto a = static_cast<unsigned>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    return (colour & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 colourBrighter(ImU32 colour, float amount)
{
    const float k = 1.0f / (1.0f + amount);
    const auto channel = [&](int shift) {
        const float v = static_cast<float>((colour >> shift) & 0xFFu);
        return static_cast<unsigned>(255.0f - k * (255.0f - v)) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT)
         | (colour & IM_COL32_A_MASK);
}

ImU32 colourDarker(ImU32 colour, float amount)
{
    const float k = 1.0f / (1.0f + amount);
    const auto channel = [&](int shift) {
        return static_cast<unsigned>(k * static_cast<float>((colour >> shift) & 0xFFu)) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT)
         | (colour & IM_COL32_A_MASK);
}

ImU32 colourMix(ImU32 from, ImU32 to, float t)
{
    const float k = std::clamp(t, 0.0f, 1.0f);
    const auto channel = [&](int shift) {
        const float a = static_cast<float>((from >> shift) & 0xFFu), b = static_cast<float>((to >> shift) & 0xFFu);
        return static_cast<unsigned>(std::lround(a + (b - a) * k)) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT)
         | channel(IM_COL32_A_SHIFT);
}

// Recolours the vertices drawn since `firstVertex` with the linear gradient from c0 at p0 to c1 at p1 (a
// juce::ColourGradient), keeping each vertex's own alpha as coverage so anti-aliased edges stay soft.
void shadeLinear(ImDrawList* dl, int firstVertex, ImVec2 p0, ImVec2 p1, ImU32 c0, ImU32 c1)
{
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(c0), b = ImGui::ColorConvertU32ToFloat4(c1);
    const ImVec2 axis(p1.x - p0.x, p1.y - p0.y);
    const float lengthSquared = axis.x * axis.x + axis.y * axis.y;
    for (int i = firstVertex; i < dl->VtxBuffer.Size; ++i)
    {
        ImDrawVert& vertex = dl->VtxBuffer[i];
        const float t = lengthSquared > 0.0f
            ? std::clamp(((vertex.pos.x - p0.x) * axis.x + (vertex.pos.y - p0.y) * axis.y) / lengthSquared, 0.0f, 1.0f)
            : 0.0f;
        const float coverage = static_cast<float>((vertex.col >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f;
        vertex.col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                                           a.z + (b.z - a.z) * t, (a.w + (b.w - a.w) * t) * coverage));
    }
}

void gradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImVec2 p0, ImVec2 p1, ImU32 c0, ImU32 c1)
{
    const int first = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding);
    shadeLinear(dl, first, p0, p1, c0, c1);
}

void gradientCircle(ImDrawList* dl, ImVec2 c, float r, ImVec2 p0, ImVec2 p1, ImU32 c0, ImU32 c1)
{
    const int first = dl->VtxBuffer.Size;
    dl->AddCircleFilled(c, r, IM_COL32_WHITE, 64);
    shadeLinear(dl, first, p0, p1, c0, c1);
}
}

class MultiCompUI final : public UI, public duskdaf::ParamHost
{
public:
    MultiCompUI()
        : UI(DAF_UI_DEFAULT_WIDTH, DAF_UI_DEFAULT_HEIGHT)
    {
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
            values[static_cast<size_t>(i)] = multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
                [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });

        setGeometryConstraints(static_cast<uint32_t>(kDesignW), static_cast<uint32_t>(kDesignH), true);
        static const float kFontSizes[] = {9.f, 11.f, 13.f, 16.f, 20.f, 26.f, 30.f};
        fontSet = duskdaf::loadCrispFontSet(kFontSizes, 7, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
        scanUserPresets();
    }

    ~MultiCompUI() override { finishTrackedEdits(true); }

    void beginEdit(uint32_t idx) override
    {
        if (drawingTrackedControl)
        {
            trackedEditsOpen[idx] = true;
            trackedEditOwners[idx] = drawingTrackedId;
            trackedEditsDrawn[idx] = true;
        }
        editParameter(idx, true);
    }
    void endEdit(uint32_t idx) override
    {
        if (drawingTrackedControl)
        {
            // A hidden/disabled control may already have closed its gesture.
            if (!trackedEditsOpen[idx]) return;
            trackedEditsOpen[idx] = false;
        }
        editParameter(idx, false);
    }
    void setParam(uint32_t idx, float value) override
    {
        if (!multicompp::isControlParameter(static_cast<int>(idx))) return;
        clearPresetSelectionForEdit(idx);
        values[idx] = value;
        setParameterValue(idx, value);
        if (idx >= P_X1 && idx <= P_X3) refreshCrossoverMirror();
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values.size()) values[index] = value;
        if (index >= P_X1 && index <= P_X3) refreshCrossoverMirror();
        if (multicompp::isControlParameter(static_cast<int>(index))
            && index != P_BYPASS)
            syncPresetSelection();
    }

    void stateChanged(const char* key, const char* state) override
    {
        if (key == nullptr || state == nullptr || std::strcmp(key, "parameters") != 0) return;
        multicompp::StateValues decoded{};
        if (!multicompp::decodeState(state, decoded)) return;
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
            values[static_cast<size_t>(i)] = decoded[static_cast<size_t>(i)];
        syncPresetSelection();
        repaint();
    }

    void programLoaded(uint32_t index) override
    {
        currentPreset = multicompp::ui_detail::loadProgramIntoMirror(index, values);
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
        trackedEditsDrawn.fill(false);

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
        // Treat the supporters panel as a modal: the panel stays visible beneath
        // its scrim but takes no clicks, wheel or drags (a click meant to close
        // the overlay used to land on INIT, a mode button or a knob). Snapshot
        // before drawHeader(), where a title click can open it.
        const bool modalOpen = showSupporters;
        if (modalOpen)
            ImGui::BeginDisabled();
        drawHeader(dl);

        // The mode-panel drawing code retains its original internal coordinates;
        // translate only that canvas so removing the two utility bands does not
        // perturb the carefully tuned per-mode layouts or hit targets.
        panel.begin(scale, ImVec2(origin.x, origin.y - kModeCanvasShiftY * scale),
                    labelFont, this);
        drawModePanel(dl);
        if (modalOpen)
            ImGui::EndDisabled();

        // Overlays and the resize grip live in the compact visible canvas.
        panel.begin(scale, origin, labelFont, this);

        if (showSupporters)
            duskdaf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH, showSupporters,
                                           "Multi-Comp 2", MULTICOMP2_VERSION_STRING,
                                           &supporters);

        const duskdaf::ResizeGripState grip =
            panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH, 0.5f);
        finishTrackedEdits(false);
        ImGui::End();
        ImGui::PopStyleVar(2);
        // A host can service the request synchronously, so never resize from
        // inside the active ImGui window.
        if (grip.resized)
            setSize(grip.width, grip.height);
    }

private:
    std::array<float, multicompp::kTotalParamCount> values{};
    // Every knob opts in (beginTrackedControl), and so do Digital/Multiband's own
    // drag controls. A host mode/band/Auto Makeup change can remove a held widget
    // before ImGui reports deactivation; finishTrackedEdits then closes its gesture.
    // A gesture belongs to the widget that opened it (its ImGui ID): another
    // face's control for the same parameter does not hold it open.
    std::array<bool, multicompp::kTotalParamCount> trackedEditsOpen{}, trackedEditsDrawn{};
    std::array<ImGuiID, multicompp::kTotalParamCount> trackedEditOwners{};
    bool drawingTrackedControl = false;
    ImGuiID drawingTrackedId = 0;
    duskdaf::DuskPanel panel;
    ImDrawListSplitter optoKnobSplitter;
    ImDrawListSplitter fetKnobSplitter;
    ImDrawListSplitter vcaKnobSplitter;
    // Display-only hardware behaviour (reference INPUT/OUTPUT/GAIN CHANGE
    // buttons): UI state, deliberately not a host parameter.
    int vcaMeterSource = 2;
    float vcaVuNeedle = 0.0f;
    float vcaLastScHp = 1.0f;
    float busVuNeedle = 0.0f;
    ImDrawListSplitter busKnobSplitter;
    ImDrawListSplitter studioVcaKnobSplitter;
    duskdaf::CrispFontSet fontSet;
    ImFont* labelFont = nullptr;
    duskdaf::SupportersOverlay supporters;
    bool showSupporters = false;
    bool geometryDiagnosticLogged = false;
    bool parameterMirrorSeeded = false;
    static constexpr uint32_t kNoCrossover = ~uint32_t(0);
    // The crossover handle under an active drag. Owns both the open automation
    // gesture and the mirror's skip, so the two cannot disagree.
    uint32_t draggedCrossover = kNoCrossover;
    // The held crossover's place on its logarithmic travel, moved by the drag.
    float crossoverDragPosition = 0.0f;
    // Digital and Multiband display state (UI only, never host parameters).
    int facedMode = -1;
    duskdaf::LedLadder inputLadder, outputLadder;
    multicompp::ui_detail::LevelReadout inputReadout, outputReadout;
    multicompp::ui_detail::VuNeedle digitalVu;
    bool digitalShowsHistory = false;
    std::array<float, 128> grHistory{};
    int grHistoryWrite = 0;
    float grHistoryClock = 0.0f;
    bool sidechainHpReset = false;
    int selectedBand = 0;
    std::array<multicompp::ui_detail::BandGrMeter, 4> bandGrMeters{};
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

    // A drawn control holds its parameter's open gesture only if it is the widget
    // that opened it; `drawn` false (a disabled control) holds nothing.
    void markTrackedDrawn(uint32_t p, ImGuiID widget, bool drawn) noexcept
    {
        if (drawn && trackedEditsOpen[p] && trackedEditOwners[p] == widget) trackedEditsDrawn[p] = true;
    }
    // Around a knob's draw, in the ID scope the knob itself uses: `id` is its
    // widget id, so a gesture it opens is recorded as its own.
    void beginTrackedControl(uint32_t p, const char* id, bool drawn) noexcept
    {
        drawingTrackedId = ImGui::GetID(id);
        drawingTrackedControl = true;
        markTrackedDrawn(p, drawingTrackedId, drawn);
    }
    void endTrackedControl() noexcept { drawingTrackedControl = false; }

    void finishTrackedEdits(bool closeAll)
    {
        for (size_t p = 0; p < trackedEditsOpen.size(); ++p)
            if (trackedEditsOpen[p] && (closeAll || !trackedEditsDrawn[p]))
            {
                editParameter(static_cast<uint32_t>(p), false);
                trackedEditsOpen[p] = false;
                if (p == draggedCrossover) draggedCrossover = kNoCrossover;
                if (p == P_SC_HP) sidechainHpReset = false;
            }
    }

    bool modernManualOutputAvailable() const
    {
        if (values[P_AUTO_MAKEUP] <= 0.5f) return true;
        if (values[static_cast<uint32_t>(ParamId::ExternalSidechain)] <= 0.5f) return false;
        void* const instance = getPluginInstancePointer();
        // The split/out-of-process fallback has only the requested key source.
        // Monolithic formats can mirror the detector the core actually uses.
        return instance == nullptr || multiCompGetExternalSidechainActive == nullptr
            || multiCompGetExternalSidechainActive(instance) > 0.5f;
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
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        {
            if (i == static_cast<int>(P_BYPASS) || multicompp::isDisplayParameter(i)) continue;
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
                 parameterIndex < multicompp::kTotalParamCount && matches;
                 parameterIndex = multicompp::nextControlParameter(parameterIndex))
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
            for (int i = 0; i < multicompp::kTotalParamCount && matches; i = multicompp::nextControlParameter(i))
                if (i != static_cast<int>(P_BYPASS) && !multicompp::isDisplayParameter(i)
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
            static_cast<uint32_t>(multicompp::kTotalParamCount));
        parameterMirrorSeeded = true;
        syncPresetSelection();
    }

    void setValue(uint32_t p, float v)
    {
        clearPresetSelectionForEdit(p);
        editParameter(p, true);
        const float hostValue = hostValueForPlain(p, v);
        values[p] = hostValue;
        setParameterValue(p, hostValue);
        editParameter(p, false);
    }

    void setHostValue(uint32_t p, float hostValue)
    {
        clearPresetSelectionForEdit(p);
        editParameter(p, true);
        values[p] = hostValue;
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
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
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
            }, true); // This editor retains its existing Save-as-replace behavior.
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
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
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
        constexpr float rowRight = 896.0f;
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
                       multicompp::ui_detail::kModeTabLabels[static_cast<size_t>(mode)], 0, true);
            if (clicked && !active)
                setValue(P_MODE, static_cast<float>(mode));
        }
        // Five factory presets engage Auto Makeup, which bypasses each face's
        // manual makeup / output control; it needs a switch the user can see.
        modeRowToggle(dl, "mc_auto_makeup", P_AUTO_MAKEUP, 908, 998, "AUTO MAKEUP");
        modeRowToggle(dl, "mc_bypass", P_BYPASS, 1008, 1098, "BYPASS");
    }

    // The shared toggle with its label centred in the button. The shared
    // widget sets its text right of centre and high to clear the LED, which
    // these wide buttons do not need.
    void modeRowToggle(ImDrawList* dl, const char* id, uint32_t p, float x0, float x1,
                       const char* label)
    {
        constexpr float y0 = kModeCanvasTop + 3.0f, y1 = kModeCanvasTop + 21.0f;
        constexpr float textSize = 10.0f;
        panel.toggle(id, p, x0, y0, x1, y1, values[p], "");
        const bool on = values[p] > 0.5f;
        const duskdaf::Palette palette{};
        panel.text(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1 - textSize), textSize,
                   on ? palette.white : palette.whiteDim, label, 0, on);
    }

    void knob(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
              const char* label, const char* fmt, const char* suffix,
              bool applicable = true)
    {
        if (!applicable) ImGui::BeginDisabled();
        beginTrackedControl(p, id, applicable);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, 25, values[p],
                   hostDefaultValue(p), false, true,
                   fmt, suffix, kPanelRaised, false, true, nullptr, false, 1.0f, 0.0f,
                   nullptr, false, nullptr, false, 0.0f, 0.0f, false, false, 9.5f,
                   false, false, &knobHostToPlain, &knobPlainToHost, this);
        endTrackedControl();
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
        const float plain = static_cast<MultiCompUI*>(context)->plainValueForHost(p, host);
        if (multicompp::usesOptoReferenceDisplay(p))
            return multicompp::optoReferenceDisplayValue(plain);
        return p == P_BUS_HEADROOM || p == P_FET_BLUE_HEADROOM ? 4.0f + 4.0f * plain : plain;
    }

    static float knobPlainToHost(float plain, uint32_t p, void* context)
    {
        if (multicompp::usesOptoReferenceDisplay(p))
            plain = multicompp::optoReferenceHostValue(plain);
        if (p == P_BUS_HEADROOM || p == P_FET_BLUE_HEADROOM) plain = std::round((plain - 4.0f) / 4.0f);
        return static_cast<MultiCompUI*>(context)->hostValueForPlain(p, plain);
    }

    static float vcaCompressionHostToRatio(float host, uint32_t p, void* context)
    {
        const float position = static_cast<MultiCompUI*>(context)->plainValueForHost(p, host);
        return duskaudio::vcaLaw::compressRatio(position);
    }

    static float vcaCompressionRatioToHost(float ratio, uint32_t p, void* context)
    {
        const float position = duskaudio::vcaLaw::compressPosition(ratio);
        return static_cast<MultiCompUI*>(context)->hostValueForPlain(p, position);
    }

    static float fetTimingHostToDial(float host, uint32_t p, void*)
    {
        return multicompp::ui_detail::fetTimingDialValue(host, p);
    }

    static float fetTimingDialToHost(float dial, uint32_t p, void*)
    {
        return multicompp::ui_detail::fetTimingHostValue(dial, p);
    }

    // Typed entry on FET BLUE's Attack: SLO or a dial reading (fetSloAttackTypedHost).
    static bool fetSloAttackParseText(const char* text, uint32_t, void*, float& host)
    {
        return multicompp::ui_detail::fetSloAttackTypedHost(text, host);
    }

    void drawModePanel(ImDrawList* dl)
    {
        const int mode = multicompp::ui_detail::choiceIndex(value(P_MODE), 8);
        drawSection(dl, kModeCanvasTop, kModeCanvasBottom - 8, "");
        drawModeToolbar(dl);
        if (mode != facedMode)
        {
            facedMode = mode;
            resetModernFaces();
        }
        // Every face carries its own gain-reduction metering: the hardware
        // faces' analogue meters, Digital's VU and Multiband's band meters.
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
                                    IM_COL32(218, 220, 218, 255),
                                    IM_COL32(211, 213, 211, 255),
                                    IM_COL32(199, 201, 199, 255),
                                    IM_COL32(207, 209, 207, 255));

        // Deterministic one-pixel strokes keep the panel feeling like brushed
        // aluminium without requiring a resolution-specific bitmap texture.
        for (int row = static_cast<int>(top + 5.0f);
             row < static_cast<int>(bottom - 4.0f); row += 3)
        {
            const int variation = (row * 37) % 5;
            const ImU32 shade = variation < 2
                ? IM_COL32(255, 255, 252, 5)
                : IM_COL32(74, 78, 78, 4);
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

        panel.text(dl, 127, top + 57, 15.0f, kOptoRed,
                   "LEVELING AMPLIFIER", -1, true);
        dl->AddLine(panel.P(127, top + 79), panel.P(325, top + 79),
                    kOptoRed, 1.5f * panel.scale());
        spacedText(dl, 127, top + 86, 8.5f, kOptoInk, "MC-2 OPTICAL", -1, 0.12f);
        spacedText(dl, 560, top + 25, 13.0f, kOptoInk, "OPTO 2A", 0, 0.10f);

        optoModeSwitch(dl, 119, top + 210.0f);
        optoKnob(dl, "opto_gain", P_OPTO_GAIN, 322, top + 180.0f, 39.0f, "GAIN");
        drawOptoMeter(dl, 441, top + 61.0f, 238, 141,
                      multicompp::ui_detail::optoMeterDisplayValue(needleGainReduction()));
        optoKnob(dl, "opto_peak", P_OPTO_PEAK, 794, top + 180.0f, 39.0f,
                 "PEAK REDUCTION");
        optoKnob(dl, "opto_sc_hp", P_SC_HP, 989, top + 114.0f, 13.0f,
                 "SC HP", false, "%.0f", " Hz");
        optoKnob(dl, "opto_mix", P_MIX, 989, top + 222.0f, 13.0f,
                 "MIX", false);
        drawOptoMeterSwitch(dl, 560, top + 238);
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
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this,
                   multicompp::ui_detail::optoKnobUsesPlainDomainDrag(p));
        endTrackedControl();
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
        // Three tiers of mark, so the styles differ only by how far in they
        // start; the placement comes from the shared ring either way.
        duskdaf::KnobRingStyle ring;
        ring.markOuter = 12.0f;   ring.minorOuter = 12.0f;
        ring.markInner = 4.0f;    ring.markThickness = 1.45f;
        ring.minorThickness = 0.75f;
        ring.markColor = kOptoInk;
        ring.labelOffset = 23.0f; ring.labelNudgeY = -4.0f; ring.labelSize = 7.8f;
        ring.labelColor = kOptoInk;
        ring.labelCentred = true;
        duskdaf::KnobRingStyle medium = ring;   medium.minorInner = 6.0f;
        duskdaf::KnobRingStyle fine = ring;     fine.minorInner = 8.0f;

        for (int tick = 0; tick <= 50; ++tick)
        {
            const bool isMajor = tick % 5 == 0;
            const bool isMedium = tick % 5 == 2;
            duskdaf::drawKnobRingMark(
                panel, dl, x, y, radius, static_cast<float>(tick) / 50.0f,
                isMajor ? scaleLabels[static_cast<size_t>(tick / 5)] : nullptr,
                isMajor ? ring : (isMedium ? medium : fine), !isMajor);
        }

        const ImVec2 shadowCenter(center.x + 3.0f * panel.scale(),
                                  center.y + 5.0f * panel.scale());
        dl->AddCircleFilled(shadowCenter, scaledRadius * 1.01f,
                            IM_COL32(0, 0, 0, 105), 56);
        // The scallops turn with the pointer, so the whole body reads as one
        // turning part; the shadow and the highlight arcs below stay put
        // because the light does not move.
        const float pointerAngle = duskdaf::DuskPanel::knobAngle(t);
        std::array<ImVec2, 96> rim{};
        for (int i = 0; i < 96; ++i)
        {
            const float local = 2.0f * kUiPi * i / 96.0f;
            const float a = local + pointerAngle;
            const float rr = scaledRadius * (0.96f + 0.035f * std::cos(local * 16.0f));
            rim[static_cast<size_t>(i)] = ImVec2(center.x + std::sin(a) * rr,
                                                 center.y - std::cos(a) * rr);
        }
        // The scalloped outline is star-convex, not convex. A shared-vertex fan
        // fills its recesses without the internal anti-aliasing seams of wedges.
        dl->PrimReserve(96 * 3, 97);
        const unsigned int rimBase = dl->_VtxCurrentIdx;
        const ImVec2 rimUv = ImGui::GetIO().Fonts->TexUvWhitePixel;
        dl->PrimWriteVtx(center, rimUv, IM_COL32(22, 24, 23, 255));
        for (const auto& point : rim)
            dl->PrimWriteVtx(point, rimUv, IM_COL32(22, 24, 23, 255));
        for (int i = 0; i < 96; ++i)
        {
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(rimBase));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(rimBase + 1 + i));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(rimBase + 1 + (i + 1) % 96));
        }
        dl->AddCircleFilled(center, scaledRadius * 0.86f, IM_COL32(37, 39, 37, 255), 64);
        dl->PathArcTo(center, scaledRadius * 0.86f, -2.8f, -0.35f, 32);
        dl->PathStroke(IM_COL32(112, 117, 110, 180), 0, 1.2f * panel.scale());
        dl->PathArcTo(center, scaledRadius * 0.88f, 0.2f, 2.8f, 32);
        dl->PathStroke(IM_COL32(5, 7, 6, 220), 0, 2.0f * panel.scale());
        const ImVec2 pointer(std::sin(pointerAngle), -std::cos(pointerAngle));
        const ImVec2 across(pointer.y, -pointer.x);
        const auto ridge = [&](float along, float side) {
            return ImVec2(center.x + scaledRadius * (pointer.x * along + across.x * side),
                          center.y + scaledRadius * (pointer.y * along + across.y * side));
        };
        dl->AddQuadFilled(ridge(-0.79f, -0.17f), ridge(0.91f, -0.10f),
                          ridge(0.91f, 0.10f), ridge(-0.79f, 0.17f), IM_COL32(14, 16, 15, 255));
        dl->AddLine(ridge(-0.68f, -0.12f), ridge(0.87f, -0.08f),
                    IM_COL32(98, 103, 96, 180), 1.5f * panel.scale());
        dl->AddLine(ridge(0.48f, 0.0f), ridge(0.93f, 0.0f),
                    IM_COL32(242, 243, 233, 255), 1.8f * panel.scale());
        panel.text(dl, x, y + radius + 37.0f, 10.5f,
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

        panel.text(dl, x, y - 40, 9.5f, kOptoInk, multicompp::ui_detail::optoModeLabel(1), 0, limit);
        panel.text(dl, x, y + 30, 9.5f, kOptoInk, multicompp::ui_detail::optoModeLabel(0), 0, !limit);

        const ImVec2 base = panel.P(x, y);
        std::array<ImVec2, 6> nut{};
        for (int point = 0; point < 6; ++point)
        {
            const float angle = kUiPi / 6.0f + static_cast<float>(point)
                * kUiPi / 3.0f;
            nut[static_cast<size_t>(point)] = ImVec2(
                base.x + std::cos(angle) * 13.0f * panel.scale(),
                base.y + std::sin(angle) * 13.0f * panel.scale());
        }
        std::array<ImVec2, 6> nutShadow = nut;
        for (auto& point : nutShadow)
        {
            point.x += 2.0f * panel.scale();
            point.y += 4.0f * panel.scale();
        }
        dl->AddConvexPolyFilled(nutShadow.data(), 6, IM_COL32(0, 0, 0, 115));
        dl->AddConvexPolyFilled(nut.data(), 6, IM_COL32(121, 124, 121, 255));
        dl->AddCircleFilled(base, 8.5f * panel.scale(),
                            IM_COL32(49, 51, 50, 255), 32);
        dl->AddCircle(base, 8.5f * panel.scale(),
                      IM_COL32(225, 226, 221, 190), 32, panel.scale());
        const float leverY = y + (limit ? -16.0f : 16.0f);
        const float leverX = x + (limit ? -3.0f : 4.0f);
        dl->AddLine(panel.P(x + 2.0f, y + 3.0f),
                    panel.P(leverX + 2.0f, leverY + 4.0f),
                    IM_COL32(0, 0, 0, 130), 6.0f * panel.scale());
        dl->AddLine(base, panel.P(leverX, leverY),
                    IM_COL32(163, 165, 160, 255), 4.5f * panel.scale());
        dl->AddLine(panel.P(x - 1.5f, y - 1.5f),
                    panel.P(leverX - 1.5f, leverY - 1.5f),
                    IM_COL32(245, 245, 238, 215), 2.0f * panel.scale());
        dl->AddCircleFilled(panel.P(leverX, leverY), 4.5f * panel.scale(),
                            IM_COL32(180, 182, 176, 255), 28);
        dl->AddCircle(panel.P(leverX, leverY), 4.5f * panel.scale(),
                      IM_COL32(48, 50, 49, 255), 28, 1.0f * panel.scale());
    }

    void drawOptoMeter(ImDrawList* dl, float x, float y, float w, float h, float gr)
    {
        const bool opto = multicompp::ui_detail::choiceIndex(values[P_MODE], 8) == 0;
        const int choice = multicompp::ui_detail::vintageMeterChoice(values[opto ? P_OPTO_METER : P_FET_METER]);
        const bool powered = opto || values[P_BYPASS] < 0.5f;
        void* const instance = getPluginInstancePointer();
        const float output = instance != nullptr && multiCompGetOutputVuLevel != nullptr
            ? multiCompGetOutputVuLevel(instance) : -120.0f;
        const char* label = !powered ? "OFF" : choice == 0 ? "GAIN REDUCTION"
                          : choice == 2 ? "OUTPUT +4" : opto ? "OUTPUT +10" : "OUTPUT +8";
        multicompp::ui_detail::drawVintageMeter(panel, dl, x, y, w, h,
            multicompp::ui_detail::vintageMeterDb(opto, choice, gr, output), opto, label, powered);
    }

    void drawOptoMeterSwitch(ImDrawList* dl, float x, float y)
    {
        const int selected = multicompp::ui_detail::vintageMeterChoice(values[P_OPTO_METER]);
        constexpr std::array<const char*, 3> labels{{"GR", "+10", "+4"}};
        panel.text(dl, x, y - 19, 8.0f, kOptoInk, "METER", 0, true);
        for (int i = 0; i < 3; ++i)
        {
            const float cx = x + (i - 1) * 36.0f;
            ImGui::SetCursorScreenPos(panel.P(cx - 16, y - 4));
            ImGui::PushID(i);
            ImGui::InvisibleButton("##opto_meter", ImVec2(32 * panel.scale(), 24 * panel.scale()));
            if (ImGui::IsItemClicked()) setValue(P_OPTO_METER, static_cast<float>(i));
            ImGui::PopID();
            panel.text(dl, cx, y, 9.0f, kOptoInk, labels[static_cast<size_t>(i)], 0, selected == i);
            if (selected == i)
                dl->AddLine(panel.P(cx - 10, y + 14), panel.P(cx + 10, y + 14), kOptoRed, 1.5f * panel.scale());
        }
    }

    // FET BLACK's face, and for now FET BLUE's on the same panel (`blue`: its own ratio buttons, side-chain
    // filter and Headroom; it shares Input, Output, Attack and Release).
    void drawFet(ImDrawList* dl, bool blue)
    {
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
                        row % 8 == 0 ? IM_COL32(255, 255, 255, 3)
                                     : IM_COL32(0, 0, 0, 5), panel.scale());
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
        fetKnob(dl, "fet_in", P_FET_IN, 177, 501, 48.0f,
                "INPUT", true, "%.1f", " dB");
        fetKnob(dl, "fet_out", P_FET_OUT, 397, 501, 48.0f,
                "OUTPUT", true, "%.1f", " dB");
        fetKnob(dl, "fet_att", P_FET_ATTACK, 559, 438, 23.0f,
                "ATTACK", false, "%.2f", "", blue);
        fetKnob(dl, "fet_rel", P_FET_RELEASE, 559, 553, 23.0f,
                "RELEASE", false, "%.2f", "");
        // The VU sits vertically centred on the two button columns either
        // side of it (ratio: five 27-high buttons at a 31 pitch; meter: four
        // 32-high at a 38 pitch, both from kFetButtonsTop).
        constexpr float kFetButtonsTop = 418.0f;
        constexpr float ratioSpan = 4.0f * 31.0f + 27.0f, meterSpan = 3.0f * 38.0f + 32.0f;
        constexpr float vuHeight = 141.0f;
        constexpr float vuTop = kFetButtonsTop + 0.5f * (std::max(ratioSpan, meterSpan) - vuHeight);
        if (blue)
            drawFetBlueRatioButtons(dl, 685, kFetButtonsTop);
        else
            drawFetRatioButtons(dl, 685, kFetButtonsTop);
        drawOptoMeter(dl, 743, vuTop, 234, vuHeight,
                      multicompp::ui_detail::optoMeterDisplayValue(needleGainReduction()));
        panel.text(dl, 860, top + 34, 14.0f, IM_COL32(234, 234, 221, 255),
                   blue ? "FET BLUE" : "FET 76", 0, true);
        panel.text(dl, 860, top + 54, 8.0f, IM_COL32(177, 179, 170, 255),
                   "REFERENCE SERIES", 0, true);
        const float badge = vuTop + vuHeight + 13.0f;
        panel.text(dl, 860, badge, 10.0f, IM_COL32(234, 234, 221, 255),
                   "MC-2", 0, true);
        panel.text(dl, 860, badge + 18.0f, 9.2f, IM_COL32(202, 204, 194, 255),
                   "LIMITING AMPLIFIER", 0, true);
        drawFetMeterSwitch(dl, 1000, kFetButtonsTop);
        // Under the meter-button column, level with the badge, labelled below
        // like the face's other knobs so it does not read as a fifth meter row.
        trimScrewKnob(dl, "fet_mix", P_MIX, 1010, badge + 19.0f, 13.0f,
                    "MIX", "%.0f", "%", true);
        if (blue)
            trimScrewKnob(dl, "fet_blue_hr", P_FET_BLUE_HEADROOM, 955, badge + 19.0f, 13.0f,
                          "HEADROOM", "%.0f", " dB", true);
    }

    // FET BLUE's ratio column: 20:1, 8:1, 4:1, 2:1 from the top, pressed alone or two together
    // (ui_detail::fetBlueRatioAfterPress), then the side-chain filter's button where FET BLACK has All.
    void drawFetBlueRatioButtons(ImDrawList* dl, float x, float y)
    {
        const int position = multicompp::ui_detail::choiceIndex(value(P_FET_BLUE_RATIO), 11);
        const int lit = multicompp::ui_detail::fetBlueRatioButtons(position);
        panel.text(dl, x + 14, y - 23, 8.5f, IM_COL32(230, 231, 219, 255), "RATIO", 0, true);
        constexpr std::array<int, 4> visualButton{{3, 2, 1, 0}};
        constexpr std::array<const char*, 5> visualLabels{{"20", "8", "4", "2", "SC"}};
        for (int row = 0; row < 5; ++row)
        {
            const bool filterRow = row == 4;
            const float y0 = y + static_cast<float>(row) * 31.0f;
            const ImVec2 p0 = panel.P(x, y0);
            const ImVec2 p1 = panel.P(x + 28.0f, y0 + 27.0f);
            char id[40];
            std::snprintf(id, sizeof(id), "##fet_blue_ratio_button_%d", row);
            ImGui::SetCursorScreenPos(p0);
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(p1.x - p0.x, p1.y - p0.y));
            const int button = filterRow ? -1 : visualButton[static_cast<size_t>(row)];
            const bool active = filterRow ? value(P_FET_BLUE_SC) >= 0.5f : (lit & (1 << button)) != 0;
            dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f), panel.P(x + 31.0f, y0 + 30.0f),
                              IM_COL32(0, 0, 0, 130), 1.5f * panel.scale());
            dl->AddRectFilled(p0, p1,
                              active ? IM_COL32(24, 24, 23, 255)
                                     : ImGui::IsItemHovered() ? IM_COL32(52, 52, 49, 255) : IM_COL32(31, 31, 29, 255),
                              1.5f * panel.scale());
            dl->AddRect(p0, p1, IM_COL32(91, 92, 87, 255), 1.5f * panel.scale(), 0, panel.scale());
            if (active)
                dl->AddRectFilled(panel.P(x + 1.0f, y0 + 2.0f), panel.P(x + 3.0f, y0 + 25.0f),
                                  filterRow ? IM_COL32(160, 48, 34, 255) : IM_COL32(226, 151, 61, 255));
            panel.text(dl, x - 10.0f, y0 + 7.0f, 8.0f,
                       active ? IM_COL32(246, 224, 177, 255) : IM_COL32(228, 228, 216, 255),
                       visualLabels[static_cast<size_t>(row)], 1, true);
            if (!clicked) continue;
            if (filterRow)
                setValue(P_FET_BLUE_SC, active ? 0.0f : 1.0f);
            else
                setValue(P_FET_BLUE_RATIO,
                         static_cast<float>(multicompp::ui_detail::fetBlueRatioAfterPress(position, button)));
        }
    }

    // sloDetent: FET BLUE's Attack, whose counter-clockwise stop is the SLO detent (ui_detail::fetAttackAtSlo).
    void fetKnob(ImDrawList* dl, const char* id, uint32_t p,
                 float x, float y, float radius, const char* label,
                 bool gainScale, const char* format, const char* suffix,
                 bool sloDetent = false)
    {
        // Reuse the shared gesture and type-entry contract, but draw the body
        // here so FET mode gets the reference unit's silver skirt and knurling.
        fetKnobSplitter.Split(dl, 2);
        fetKnobSplitter.SetCurrentChannel(dl, 1);
        const bool timingDial = multicompp::ui_detail::fetTimingUsesDialReadout(p);
        const bool atSlo = sloDetent && multicompp::ui_detail::fetAttackAtSlo(plainValueForHost(p, values[p]));
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   atSlo ? multicompp::ui_detail::kFetSloText : nullptr,
                   false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   timingDial ? &fetTimingHostToDial : &knobHostToPlain,
                   timingDial ? &fetTimingDialToHost : &knobPlainToHost,
                   this, true, sloDetent ? &fetSloAttackParseText : nullptr);
        endTrackedControl();
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
            // Printed as on the reference faceplate. The audio law's
            // counter-clockwise stop is finite (Input -41.95 dB relative),
            // but the silkscreen reads INF.
            constexpr std::array<const char*, 9> labels{{
                "INF", "48", "36", "30", "24", "18", "12", "6", "0"}};
            duskdaf::KnobRingStyle ring;
            ring.markInner = 4.0f;   ring.markOuter = 10.0f;  ring.markThickness = 1.25f;
            ring.minorInner = 7.0f;  ring.minorOuter = 10.0f; ring.minorThickness = 0.75f;
            ring.markColor = IM_COL32(229, 230, 219, 240);
            ring.labelOffset = 19.0f; ring.labelNudgeY = -3.0f; ring.labelSize = 7.3f;
            ring.labelColor = IM_COL32(232, 232, 220, 255);
            ring.labelCentred = true;
            for (int tick = 0; tick <= 16; ++tick)
            {
                const bool major = tick % 2 == 0;
                duskdaf::drawKnobRingMark(
                    panel, dl, x, y, radius, static_cast<float>(tick) / 16.0f,
                    major ? labels[static_cast<size_t>(tick / 2)] : nullptr,
                    ring, !major);
            }
        }
        else
        {
            // FET BLUE's Attack: the stop is the SLO detent, as the reference names it.
            const std::array<const char*, 4> labels{{
                sloDetent ? multicompp::ui_detail::kFetSloText : "1", "3", "5", "7"}};
            duskdaf::KnobRingStyle ring;
            ring.dotMarks = true;
            ring.markOuter = 7.0f;   ring.minorOuter = 7.0f;
            ring.dotRadius = 1.45f;  ring.minorDotRadius = 0.85f;
            ring.dotSegments = 10;
            ring.markColor = IM_COL32(230, 230, 218, 255);
            ring.labelOffset = 15.0f; ring.labelNudgeY = -3.0f; ring.labelSize = 7.0f;
            ring.labelColor = IM_COL32(232, 232, 220, 255);
            ring.labelCentred = true;
            for (int tick = 0; tick <= 6; ++tick)
            {
                const bool major = tick % 2 == 0;
                duskdaf::drawKnobRingMark(
                    panel, dl, x, y, radius, static_cast<float>(tick) / 6.0f,
                    major ? labels[static_cast<size_t>(tick / 2)] : nullptr,
                    ring, !major);
            }
        }

        // Soft shadow, concentric spun-metal skirt, knurled black collar and
        // machined cap. Keep every structural circle on the exact center; the
        // previous offset 91% disk read as a second gray knob at upper left.
        dl->AddCircleFilled(ImVec2(center.x + 3.0f * scale, center.y + 4.0f * scale),
                            r * 1.01f, IM_COL32(0, 0, 0, 150), 56);
        dl->AddCircleFilled(center, r, IM_COL32(66, 67, 66, 255), 56);
        dl->AddCircleFilled(center, r * 0.96f, IM_COL32(32, 34, 32, 255), 56);
        dl->AddCircleFilled(center, r * 0.83f, IM_COL32(24, 26, 24, 255), 56);
        dl->AddCircleFilled(center, r * 0.75f, IM_COL32(23, 25, 23, 255), 56);
        dl->PathArcTo(center, r * 0.90f, -2.55f, -0.55f, 30);
        dl->PathStroke(IM_COL32(158, 162, 153, 80), 0, 1.5f * scale);
        dl->PathArcTo(center, r * 0.91f, 0.55f, 2.55f, 30);
        dl->PathStroke(IM_COL32(24, 25, 24, 110), 0, 1.8f * scale);
        dl->AddCircle(center, r, IM_COL32(31, 32, 31, 255), 56, 1.3f * scale);
        const float collarR = r * 0.69f;
        // The knurling turns with the pointer; the skirt and cap highlights
        // are lighting and stay put.
        const float pointerAngle = duskdaf::DuskPanel::knobAngle(t);
        dl->AddCircleFilled(center, collarR, IM_COL32(20, 20, 20, 255), 52);
        for (int notch = 0; notch < 28; ++notch)
        {
            const float angle = pointerAngle
                + 2.0f * kUiPi * static_cast<float>(notch) / 28.0f;
            const ImVec2 direction(std::sin(angle), -std::cos(angle));
            dl->AddLine(ImVec2(center.x + direction.x * collarR * 0.78f,
                               center.y + direction.y * collarR * 0.78f),
                        ImVec2(center.x + direction.x * collarR,
                               center.y + direction.y * collarR),
                        IM_COL32(75, 79, 71, 100), 1.0f * scale);
        }
        const float capR = r * 0.49f;
        dl->AddCircleFilled(center, capR, IM_COL32(111, 112, 110, 255), 48);
        constexpr int facets = 96;
        // Interpolate the metal highlight across one mesh. Anti-aliasing each
        // triangle separately would expose bright spokes between the facets.
        dl->PrimReserve(facets * 3, facets + 1);
        const unsigned int capBase = dl->_VtxCurrentIdx;
        const ImVec2 capUv = ImGui::GetIO().Fonts->TexUvWhitePixel;
        dl->PrimWriteVtx(center, capUv, IM_COL32(181, 182, 179, 255));
        for (int i = 0; i < facets; ++i)
        {
            const float a = i * 2.0f * kUiPi / facets;
            const int shade = static_cast<int>(176 + 58 * std::cos(2 * a - 0.7f));
            dl->PrimWriteVtx(
                ImVec2(center.x + std::cos(a) * capR * 0.95f, center.y + std::sin(a) * capR * 0.95f),
                capUv, IM_COL32(shade, shade + 1, shade - 2, 255));
        }
        for (int i = 0; i < facets; ++i)
        {
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(capBase));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(capBase + 1 + i));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(capBase + 1 + (i + 1) % facets));
        }
        dl->PathArcTo(center, capR * 0.78f, -2.50f, -0.65f, 22);
        dl->PathStroke(IM_COL32(255, 255, 251, 95), 0, 1.2f * scale);
        dl->AddCircle(center, capR, IM_COL32(44, 45, 44, 255), 48, 1.1f * scale);
        const ImVec2 pointer(std::sin(pointerAngle), -std::cos(pointerAngle));
        dl->AddLine(ImVec2(center.x + pointer.x * r * 0.72f,
                           center.y + pointer.y * r * 0.72f),
                    ImVec2(center.x + pointer.x * r * 0.94f,
                           center.y + pointer.y * r * 0.94f),
                    IM_COL32(229, 230, 218, 255), 2.0f * scale);

        panel.text(dl, x, gainScale ? y + radius + 27.0f
                                    : y + (label[0] == 'A' ? -50.0f : 39.0f),
                   gainScale ? 10.5f : 9.5f,
                   IM_COL32(235, 235, 222, 255), label, 0, true);
        fetKnobSplitter.Merge(dl);
    }

    void trimScrewKnob(ImDrawList* dl, const char* id, uint32_t p,
                     float x, float y, float radius, const char* label,
                     const char* format, const char* suffix, bool labelBelow = false)
    {
        // The Opto face established the fleet contract for non-reference
        // controls: keep the full parameter available as a small set-screw trim
        // without letting it compete with the hardware's primary controls.
        fetKnobSplitter.Split(dl, 2);
        fetKnobSplitter.SetCurrentChannel(dl, 1);
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 8.0f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this, true);
        endTrackedControl();
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
        panel.text(dl, x, labelBelow ? y + radius + 9.0f : y - 38.0f, 7.5f,
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
        const int selected = multicompp::ui_detail::fetMeterButton(values[P_FET_METER], values[P_BYPASS] >= 0.5f);
        panel.text(dl, x + 10.0f, y - 24.0f, 8.5f,
                   IM_COL32(230, 231, 219, 255), "METER", 0, true);
        constexpr std::array<const char*, 4> labels{{"GR", "+8", "+4", "OFF"}};
        for (int row = 0; row < 4; ++row)
        {
            const float y0 = y + static_cast<float>(row) * 38.0f;
            ImGui::SetCursorScreenPos(panel.P(x, y0));
            ImGui::PushID(row);
            ImGui::InvisibleButton("##fet_meter", ImVec2(59 * panel.scale(), 34 * panel.scale()));
            if (ImGui::IsItemClicked())
                multicompp::ui_detail::selectFetMeterButton(row, [this](uint32_t p, float v) { setValue(p, v); });
            ImGui::PopID();
            dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f),
                              panel.P(x + 23.0f, y0 + 35.0f),
                              IM_COL32(0, 0, 0, 145), 1.5f * panel.scale());
            dl->AddRectFilled(panel.P(x, y0), panel.P(x + 20.0f, y0 + 32.0f),
                              IM_COL32(29, 29, 28, 255), 1.5f * panel.scale());
            dl->AddRect(panel.P(x, y0), panel.P(x + 20.0f, y0 + 32.0f),
                        IM_COL32(87, 88, 84, 255), 1.5f * panel.scale(), 0,
                        panel.scale());
            if (row == selected)
                dl->AddRectFilled(panel.P(x + 1.0f, y0 + 2.0f),
                                  panel.P(x + 3.0f, y0 + 30.0f),
                                  IM_COL32(226, 151, 61, 255));
            panel.text(dl, x + 31.0f, y0 + 9.0f, 8.0f,
                       row == selected ? IM_COL32(246, 224, 177, 255)
                                : IM_COL32(227, 227, 215, 255),
                       labels[static_cast<size_t>(row)], -1, true);
        }
    }

    // Silkscreen lettering with letter-spacing, which ImGui text lacks: each
    // glyph is placed by its own advance plus a tracking gap. align: -1 left,
    // 0 centred, 1 right.
    void spacedText(ImDrawList* dl, float x, float y, float size, ImU32 col,
                    const char* text, int align, float tracking = 0.16f)
    {
        duskdaf::spacedText(panel, dl, x, y, size, col, text, align, tracking);
    }

    void drawVca(ImDrawList* dl)
    {
        // VCA compressor face for the VCA mode, laid out from the reference panel's
        // own proportions (x scaled 0.56, y scaled 0.52 from the 2000x802
        // reference image onto the 1120x416 face). Only the reference's
        // front-panel controls are drawn; attack, release, Over Easy and the
        // detector selector have no reference-unit equivalent and stay host-visible
        // parameters without faceplate controls. No third-party marks.
        using Layout = multicompp::ui_detail::VcaFaceplateLayout;
        constexpr float left = Layout::left, right = Layout::right;
        constexpr float top = Layout::top, bottom = Layout::bottom;
        constexpr float cheek = 44.0f, rail = 16.0f;
        const float s = panel.scale();
        dl->AddRectFilled(panel.P(left + 5, top + 7), panel.P(right + 5, bottom + 7),
                          IM_COL32(0, 0, 0, 150), 4.0f * s);
        // Matte charcoal face with a faint speckle and a gentle vertical fall-off.
        dl->AddRectFilledMultiColor(panel.P(left, top), panel.P(right, bottom),
                                    IM_COL32(44, 44, 45, 255), IM_COL32(44, 44, 45, 255),
                                    IM_COL32(24, 24, 25, 255), IM_COL32(26, 26, 27, 255));
        {
            uint32_t seed = 0x1600u;
            for (int i = 0; i < 120; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                const float px = left + cheek + static_cast<float>((seed >> 8) % 1032);
                seed = seed * 1664525u + 1013904223u;
                const float py = top + rail + static_cast<float>((seed >> 8) % 276);
                dl->AddRectFilled(panel.P(px, py), panel.P(px + 1.2f, py + 1.2f),
                                  (i & 3) ? IM_COL32(255, 255, 255, 9) : IM_COL32(0, 0, 0, 40));
            }
        }
        // Brushed aluminium rails with a dark seam against the face.
        for (const bool topRail : {true, false})
        {
            const float y0 = topRail ? top : bottom - rail;
            const float y1 = topRail ? top + rail : bottom;
            dl->AddRectFilledMultiColor(panel.P(left, y0), panel.P(right, y1),
                                        IM_COL32(190, 192, 192, 255), IM_COL32(190, 192, 192, 255),
                                        IM_COL32(118, 120, 120, 255), IM_COL32(118, 120, 120, 255));
            for (int row = static_cast<int>(y0 + 1); row < static_cast<int>(y1); row += 2)
                dl->AddLine(panel.P(left, static_cast<float>(row)), panel.P(right, static_cast<float>(row)),
                            (row * 7) % 5 == 0 ? IM_COL32(255, 255, 255, 40) : IM_COL32(0, 0, 0, 28), s);
            dl->AddLine(panel.P(left, topRail ? y1 : y0), panel.P(right, topRail ? y1 : y0),
                        IM_COL32(6, 6, 6, 255), 1.6f * s);
            dl->AddLine(panel.P(left, topRail ? y1 + 1.5f : y0 - 1.5f), panel.P(right, topRail ? y1 + 1.5f : y0 - 1.5f),
                        IM_COL32(90, 90, 92, 120), s);
        }
        // Walnut cheeks: warm gradient, deterministic grain, a soft vignette
        // towards the face, and a dark seam.
        for (const bool leftCheek : {true, false})
        {
            const float x0 = leftCheek ? left : right - cheek;
            const float x1 = leftCheek ? left + cheek : right;
            dl->AddRectFilledMultiColor(panel.P(x0, top), panel.P(x1, bottom),
                                        IM_COL32(128, 78, 40, 255), IM_COL32(112, 66, 32, 255),
                                        IM_COL32(72, 40, 18, 255), IM_COL32(84, 48, 22, 255));
            for (int grain = 0; grain < 14; ++grain)
            {
                const float gx = x0 + 2.5f + static_cast<float>(grain) * (cheek - 5.0f) / 13.0f;
                const float wobble = 2.0f * std::sin(static_cast<float>(grain) * 1.9f);
                dl->AddLine(panel.P(gx + wobble, top + 2.0f), panel.P(gx - wobble, bottom - 2.0f),
                            grain % 3 == 0 ? IM_COL32(168, 108, 58, 80) : IM_COL32(34, 16, 4, 120), 1.1f * s);
            }
            dl->AddRectFilledMultiColor(panel.P(leftCheek ? x1 - 10.0f : x0, top), panel.P(leftCheek ? x1 : x0 + 10.0f, bottom),
                                        leftCheek ? IM_COL32(0, 0, 0, 0) : IM_COL32(0, 0, 0, 90),
                                        leftCheek ? IM_COL32(0, 0, 0, 90) : IM_COL32(0, 0, 0, 0),
                                        leftCheek ? IM_COL32(0, 0, 0, 90) : IM_COL32(0, 0, 0, 0),
                                        leftCheek ? IM_COL32(0, 0, 0, 0) : IM_COL32(0, 0, 0, 90));
            dl->AddLine(panel.P(leftCheek ? x1 : x0, top), panel.P(leftCheek ? x1 : x0, bottom),
                        IM_COL32(4, 3, 2, 255), 1.8f * s);
        }
        dl->AddRect(panel.P(left, top), panel.P(right, bottom), IM_COL32(18, 18, 18, 255), 4.0f * s, 0, 1.3f * s);

        // Section titles.
        spacedText(dl, 171, 368, 14.0f, kVcaInk, "THRESHOLD", 0);
        spacedText(dl, 385, 368, 14.0f, kVcaInk, "COMPRESSION", 0);
        spacedText(dl, 595, 360, 14.0f, kVcaInk, "OUTPUT", 0);
        spacedText(dl, 595, 378, 14.0f, kVcaInk, "GAIN", 0);

        // Threshold lamps: BELOW (amber) under threshold, ABOVE (red) at or
        // over it, from the DSP's published input peak level; labels sit
        // outboard of the lamps like the reference.
        const float inputDb = levelMeterDb(false);
        const float thresholdDb = plainValueForHost(P_VCA_THRESHOLD, values[P_VCA_THRESHOLD]);
        const bool above = multicompp::ui_detail::vcaSignalAboveThreshold(inputDb, thresholdDb);
        spacedText(dl, 110, 391, 9.5f, kVcaInk, "BELOW", 1);
        // The reference's two jewels: amber below the threshold, red above.
        duskdaf::DuskPanel::LedStyle jewel;
        jewel.bezelColor = IM_COL32(52, 52, 54, 255);
        jewel.rimRadiusAdd = 1.2f;
        jewel.glowRadiusAdd = 7.0f;
        jewel.dropShadowRadiusAdd = 3.0f;
        jewel.highlightScale = 1.5f;
        jewel.highlightColor = IM_COL32(255, 248, 225, 220);
        jewel.offFromOnColor = true;
        jewel.offHighlight = true;

        jewel.onColor = IM_COL32(240, 205, 70, 255);
        jewel.glowColor = IM_COL32(240, 205, 70, 80);
        panel.led(dl, 128, 397, !above, 7.0f, jewel);

        jewel.onColor = IM_COL32(220, 62, 46, 255);
        jewel.glowColor = IM_COL32(220, 62, 46, 80);
        panel.led(dl, 214, 397, above, 7.0f, jewel);
        spacedText(dl, 232, 391, 9.5f, kVcaInk, "ABOVE", -1);

        {
            // The reference's ring artwork: 10 mV .. 3 V at the hardware's
            // angles. Our threshold law follows the reference's knob positions,
            // so the same angles are honest; the values passed are the dB the
            // linear -55..0 law places at those angles (t = 0, .25, .40, .60,
            // .75, 1). The read-out bubble keeps the real dB threshold.
            constexpr std::array<float, 6> majors{{-55.0f, -41.25f, -33.0f, -22.0f, -13.75f, 0.0f}};
            constexpr std::array<const char*, 6> labels{{"10 mV", "30 mV", "100 mV", "300 mV", "1 V", "3 V"}};
            constexpr std::array<float, 4> minors{{-48.0f, -27.5f, -17.5f, -6.5f}};
            vcaKnob(dl, "vca_thr", P_VCA_THRESHOLD, 171, 478, 34.0f,
                    "THRESHOLD", "%.1f", " dB", majors.data(), labels.data(),
                    static_cast<int>(majors.size()), minors.data(), static_cast<int>(minors.size()));
        }
        {
            // Position knob; ring marks placed by the measured ratio law and the
            // read-out shows the applied ratio (MultiCompVcaLaw.hpp).
            constexpr std::array<float, 9> ratios{{1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f,
                                                   std::numeric_limits<float>::infinity()}};
            constexpr std::array<const char*, 9> labels{{"1", "1.5", "2", "3", "4", "6", "10", "20", "INF"}};
            std::array<float, 9> majors{};
            for (size_t i = 0; i < ratios.size(); ++i)
                majors[i] = duskaudio::vcaLaw::compressPosition(ratios[i]);
            vcaKnob(dl, "vca_ratio", P_VCA_RATIO, 385, 478, 42.0f,
                    "COMPRESSION", "%.1f", ":1", majors.data(), labels.data(),
                    static_cast<int>(majors.size()), nullptr, 0,
                    &vcaCompressionHostToRatio, &vcaCompressionRatioToHost);
        }
        {
            constexpr std::array<float, 5> majors{{-20.0f, -10.0f, 0.0f, 10.0f, 20.0f}};
            constexpr std::array<const char*, 5> labels{{"-20", "-10", "0", "+10", "+20"}};
            constexpr std::array<float, 4> minors{{-15.0f, -5.0f, 5.0f, 15.0f}};
            vcaKnob(dl, "vca_out", P_VCA_OUT, 595, 478, 34.0f,
                    "OUTPUT GAIN", "%.1f", " dB", majors.data(), labels.data(),
                    static_cast<int>(majors.size()), minors.data(), static_cast<int>(minors.size()));
        }
        // PULL/SC: printed under the threshold knob like the reference; the
        // switch itself takes the POWER button's place (global bypass covers
        // power).
        spacedText(dl, 171, 534, 9.5f, kVcaInk, "PULL/SC", 0);
        vcaPullSwitch(dl, 171, 594);

        drawVcaMeterButtons(dl, 355, 562);

        // Meter: the reference's amber-lit window with blue scale, on the
        // shared needle meter. One -40..+20 dB scale reads level (0 VU =
        // -12 dBFS, fleet calibration) or gain change per the selected source.
        const auto scale = multicompp::ui_detail::vcaMeterScale(
            multicompp::ui_detail::vcaMeterSourceLabel(vcaMeterSource));
        // Gain change reads the plugin's needle movement (MultiCompGainReductionNeedle.hpp),
        // so the widget's cosmetic pole is off for it.
        auto style = multicompp::ui_detail::vcaMeterStyle();
        style.smoothNeedle = vcaMeterSource != 2 || !hasNeedles();
        const float meterDb = vcaMeterSource == 0 ? inputDb + 12.0f
                            : vcaMeterSource == 1 ? levelMeterDb(true) + 12.0f
                            : vcaNeedleGainReduction();
        using Meter = multicompp::ui_detail::VcaMeterRect;
        duskdaf::drawVuMeter(panel, dl, Meter::x0, Meter::y0, Meter::x1, Meter::y1, meterDb,
                             vcaVuNeedle, scale, IM_COL32(40, 40, 42, 255), style);

        // Model line above the meter and badge below it, at the FET face's
        // sizes and spacing; the MIX trim is the same set-screw as Opto and
        // FET, in the column right of the meter, level with the badge.
        constexpr float meterCentre = 0.5f * (Meter::x0 + Meter::x1);
        constexpr ImU32 kVcaWhite = IM_COL32(240, 240, 236, 255);
        spacedText(dl, meterCentre, 378.0f, 14.0f, kVcaWhite, "VCA 60", 0, 0.12f);
        spacedText(dl, meterCentre, 398.0f, 8.0f, kVcaInk, "REFERENCE SERIES", 0, 0.12f);
        const float badge = Meter::y1 + 13.0f;
        spacedText(dl, meterCentre, badge, 10.0f, kVcaWhite, "MC-2", 0, 0.12f);
        spacedText(dl, meterCentre, badge + 18.0f, 9.2f, kVcaInk, "COMPRESSOR/LIMITER", 0, 0.12f);
        trimScrewKnob(dl, "vca_mix", P_MIX, 0.5f * (Meter::x1 + Layout::right - cheek), badge + 19.0f,
                      13.0f, "MIX", "%.0f", "%", true);
    }

    void vcaPullSwitch(ImDrawList* dl, float x, float y)
    {
        const bool on = plainValueForHost(P_SC_HP, values[P_SC_HP]) >= 1.0f;   // same OFF rule as optoKnob
        if (on) vcaLastScHp = values[P_SC_HP];
        const ImVec2 p0 = panel.P(x - 40.0f, y - 16.0f);
        const ImVec2 p1 = panel.P(x + 40.0f, y + 16.0f);
        ImGui::SetCursorScreenPos(p0);
        const bool clicked = ImGui::InvisibleButton("##vca_pull_sc", ImVec2(p1.x - p0.x, p1.y - p0.y));
        const float s = panel.scale();
        // Black bezel like the reference's push buttons, holding a sliding
        // silver bar that sits right when the filter is in.
        dl->AddRectFilled(panel.P(x - 40.0f, y - 15.0f), panel.P(x + 40.0f, y + 15.0f),
                          IM_COL32(8, 8, 8, 255), 3.0f * s);
        dl->AddRect(panel.P(x - 40.0f, y - 15.0f), panel.P(x + 40.0f, y + 15.0f),
                    IM_COL32(60, 62, 60, 255), 3.0f * s, 0, s);
        const float barX = on ? x + 17.0f : x - 17.0f;
        dl->AddRectFilled(panel.P(barX - 16.0f + 1.5f, y - 9.0f + 2.0f), panel.P(barX + 16.0f + 1.5f, y + 9.0f + 2.0f),
                          IM_COL32(0, 0, 0, 140), 2.5f * s);
        dl->AddRectFilledMultiColor(panel.P(barX - 16.0f, y - 9.0f), panel.P(barX + 16.0f, y + 9.0f),
                                    IM_COL32(222, 224, 222, 255), IM_COL32(222, 224, 222, 255),
                                    IM_COL32(158, 160, 158, 255), IM_COL32(158, 160, 158, 255));
        dl->AddLine(panel.P(barX - 14.0f, y - 7.5f), panel.P(barX + 14.0f, y - 7.5f), IM_COL32(250, 250, 250, 180), 1.2f * s);
        dl->AddRect(panel.P(barX - 16.0f, y - 9.0f), panel.P(barX + 16.0f, y + 9.0f), IM_COL32(36, 37, 36, 255), 2.5f * s, 0, s);
        if (ImGui::IsItemHovered())
            dl->AddRectFilled(panel.P(x - 40.0f, y - 15.0f), panel.P(x + 40.0f, y + 15.0f), IM_COL32(255, 255, 255, 14), 3.0f * s);
        spacedText(dl, x - 48.0f, y - 6.0f, 9.0f, on ? kVcaInkDim : kVcaInk, "OUT", 1);
        spacedText(dl, x + 48.0f, y - 6.0f, 9.0f, on ? kVcaInk : kVcaInkDim, "IN", -1);
        if (clicked)
            setHostValue(P_SC_HP, multicompp::ui_detail::vcaSidechainSwitchTarget(
                on, values[P_SC_HP], vcaLastScHp,
                hostMinimum(P_SC_HP), hostMaximum(P_SC_HP)));
    }

    // Small set-screw trim (the reference's MIX control). Same gesture
    // contract as fetTrimKnob; label optional (the badge prints its own).
    void vcaTrim(ImDrawList* dl, const char* id, uint32_t p, float x, float y, float radius,
                 const char* label, const char* format, const char* suffix)
    {
        vcaKnobSplitter.Split(dl, 2);
        vcaKnobSplitter.SetCurrentChannel(dl, 1);
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label ? label : "MIX", true,
                   nullptr, false, 0.0f, 0.0f, false, true, 8.0f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this, true);
        endTrackedControl();
        vcaKnobSplitter.SetCurrentChannel(dl, 0);
        const float hostMinV = hostMinimum(p);
        const float hostRange = std::max(hostMaximum(p) - hostMinV, 1.0e-6f);
        const float t = std::clamp((values[p] - hostMinV) / hostRange, 0.0f, 1.0f);
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        const float r = radius * scale;
        dl->AddCircleFilled(ImVec2(center.x + 1.5f * scale, center.y + 2.5f * scale), r * 1.35f, IM_COL32(0, 0, 0, 140), 32);
        dl->AddCircleFilled(center, r * 1.3f, IM_COL32(28, 28, 28, 255), 32);
        dl->AddCircle(center, r * 1.3f, IM_COL32(70, 71, 70, 255), 32, scale);
        dl->AddCircleFilled(center, r, IM_COL32(158, 160, 158, 255), 32);
        dl->AddCircleFilled(center, r * 0.8f, IM_COL32(206, 208, 206, 255), 32);
        dl->PathArcTo(center, r * 0.62f, -2.45f, -0.75f, 18);
        dl->PathStroke(IM_COL32(250, 251, 248, 120), 0, scale);
        const float angle = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 pointer(std::sin(angle), -std::cos(angle));
        dl->AddLine(ImVec2(center.x - pointer.x * r * 0.62f, center.y - pointer.y * r * 0.62f),
                    ImVec2(center.x + pointer.x * r * 0.62f, center.y + pointer.y * r * 0.62f),
                    IM_COL32(34, 35, 34, 255), 1.8f * scale);
        if (label != nullptr) spacedText(dl, x, y - radius - 22.0f, 9.0f, kVcaInk, label, 0);
        vcaKnobSplitter.Merge(dl);
    }


    void vcaKnob(ImDrawList* dl, const char* id, uint32_t p,
                 float x, float y, float radius, const char* label,
                 const char* format, const char* suffix,
                 const float* majorValues, const char* const* majorLabels,
                 int majorCount, const float* minorValues, int minorCount,
                 float (*toDisplay)(float, uint32_t, void*) = &knobHostToPlain,
                 float (*fromDisplay)(float, uint32_t, void*) = &knobPlainToHost)
    {
        // Shared gesture/readout contract; the reference's silver domed knob
        // on a black serrated skirt is drawn here. Ring marks are placed
        // through the parameter's own host law, so the printed ring and the
        // pointer agree by construction; the read-out can use a different
        // law (Compression shows the applied ratio for its position).
        const bool dragInDisplay = toDisplay == &knobHostToPlain;
        vcaKnobSplitter.Split(dl, 2);
        vcaKnobSplitter.SetCurrentChannel(dl, 1);
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   toDisplay, fromDisplay, this, dragInDisplay);
        endTrackedControl();
        vcaKnobSplitter.SetCurrentChannel(dl, 0);

        const float hostMinV = hostMinimum(p);
        const float hostRange = std::max(hostMaximum(p) - hostMinV, 1.0e-6f);
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        const float r = radius * scale;
        const auto ringT = [&](float plain) {
            return std::clamp((hostValueForPlain(p, plain) - hostMinV) / hostRange, 0.0f, 1.0f);
        };
        for (int i = 0; i < minorCount; ++i)
        {
            const float angle = duskdaf::DuskPanel::knobAngle(ringT(minorValues[i]));
            const ImVec2 d(std::sin(angle), -std::cos(angle));
            dl->AddLine(ImVec2(center.x + d.x * (r + 8.0f * scale), center.y + d.y * (r + 8.0f * scale)),
                        ImVec2(center.x + d.x * (r + 13.0f * scale), center.y + d.y * (r + 13.0f * scale)),
                        IM_COL32(214, 215, 210, 220), 1.1f * scale);
        }
        {
            duskdaf::KnobRingStyle ring;
            ring.markInner = 7.0f;
            ring.markOuter = 15.0f;
            ring.markThickness = 1.7f;
            ring.markColor = kVcaInk;
            ring.labelOffset = 27.0f;
            ring.labelNudgeY = -5.5f;
            ring.labelColor = kVcaInk;
            ring.labelCentred = true;
            ring.labelTracking = 0.06f;
            duskdaf::drawKnobRing(panel, dl, x, y, radius,
                                  majorValues, majorLabels, majorCount, nullptr, 0,
                                  ringT, ring);
        }

        // Drop shadow, black serrated skirt, silver dome with radial brushing
        // and a top-left highlight, dark pointer line to the cap edge. The
        // serrations and brushing turn with the pointer; the shadow and
        // highlights are lighting and stay put.
        const float t = std::clamp((values[p] - hostMinV) / hostRange, 0.0f, 1.0f);
        const float pointerAngle = duskdaf::DuskPanel::knobAngle(t);
        dl->AddCircleFilled(ImVec2(center.x + 3.0f * scale, center.y + 4.5f * scale), r * 1.03f, IM_COL32(0, 0, 0, 160), 64);
        dl->AddCircleFilled(center, r, IM_COL32(24, 24, 24, 255), 64);
        for (int tooth = 0; tooth < 72; ++tooth)
        {
            const float a = pointerAngle + 2.0f * kUiPi * static_cast<float>(tooth) / 72.0f;
            const ImVec2 d(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(center.x + d.x * r * 0.90f, center.y + d.y * r * 0.90f),
                        ImVec2(center.x + d.x * r, center.y + d.y * r),
                        (tooth & 1) ? IM_COL32(76, 77, 76, 255) : IM_COL32(10, 10, 10, 255), 1.6f * scale);
        }
        const float capR = r * 0.86f;
        dl->AddCircleFilled(center, capR, IM_COL32(150, 152, 150, 255), 64);
        dl->AddCircleFilled(ImVec2(center.x, center.y - capR * 0.06f), capR * 0.94f, IM_COL32(204, 206, 204, 255), 64);
        for (int spoke = 0; spoke < 96; ++spoke)
        {
            const float a = pointerAngle + 2.0f * kUiPi * static_cast<float>(spoke) / 96.0f;
            const ImVec2 d(std::cos(a), std::sin(a));
            const int shade = (spoke * 7) % 5;
            dl->AddLine(ImVec2(center.x + d.x * capR * 0.18f, center.y + d.y * capR * 0.18f),
                        ImVec2(center.x + d.x * capR * 0.93f, center.y + d.y * capR * 0.93f),
                        shade == 0 ? IM_COL32(255, 255, 255, 40) : shade == 2 ? IM_COL32(0, 0, 0, 28) : IM_COL32(0, 0, 0, 0),
                        0.9f * scale);
        }
        dl->AddCircleFilled(ImVec2(center.x - capR * 0.28f, center.y - capR * 0.32f), capR * 0.42f, IM_COL32(255, 255, 255, 34), 40);
        dl->PathArcTo(center, capR * 0.97f, 0.35f, 2.75f, 30);
        dl->PathStroke(IM_COL32(60, 62, 60, 170), 0, 1.6f * scale);
        dl->AddCircle(center, capR, IM_COL32(88, 90, 88, 255), 64, 1.1f * scale);
        const ImVec2 pointer(std::sin(pointerAngle), -std::cos(pointerAngle));
        dl->AddLine(ImVec2(center.x + pointer.x * capR * 0.12f, center.y + pointer.y * capR * 0.12f),
                    ImVec2(center.x + pointer.x * capR * 0.97f, center.y + pointer.y * capR * 0.97f),
                    IM_COL32(28, 29, 28, 255), 2.6f * scale);
        vcaKnobSplitter.Merge(dl);
    }

    void drawVcaMeterButtons(ImDrawList* dl, float x, float y)
    {
        // Reference layout: source labels above three silver keycaps in black
        // bezels, a square-cornered bracket with METER beneath. Selection is
        // display-only UI state.
        struct Cell { float x0, x1; };
        constexpr std::array<Cell, 3> cells{{{0.0f, 82.0f}, {88.0f, 170.0f}, {176.0f, 262.0f}}};
        constexpr float capTop = 14.0f, capBottom = 44.0f;
        for (int i = 0; i < multicompp::ui_detail::kVcaMeterSourceCount; ++i)
        {
            const float bx0 = x + cells[static_cast<size_t>(i)].x0;
            const float bx1 = x + cells[static_cast<size_t>(i)].x1;
            const float by0 = y + capTop, by1 = y + capBottom;
            const char* label = multicompp::ui_detail::vcaMeterSourceLabel(i);
            // The bezel below starts at capTop - 4 and is drawn after the
            // label, so the bottom line has to finish clear of it.
            if (i == 2)
            {
                spacedText(dl, 0.5f * (bx0 + bx1), y - 16.0f, 10.0f, kVcaInk, "GAIN", 0);
                spacedText(dl, 0.5f * (bx0 + bx1), y - 5.0f, 10.0f, kVcaInk, "CHANGE", 0);
            }
            else
                spacedText(dl, 0.5f * (bx0 + bx1), y - 5.0f, 10.0f, kVcaInk, label, 0);
            char id[32];
            std::snprintf(id, sizeof(id), "##vca_meter_source_%d", i);
            const ImVec2 b0 = panel.P(bx0, by0), b1 = panel.P(bx1, by1);
            ImGui::SetCursorScreenPos(b0);
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            const bool active = i == vcaMeterSource;
            const float s = panel.scale();
            // Black bezel with a soft outer shadow, then the keycap.
            dl->AddRectFilled(panel.P(bx0 - 4.0f + 2.0f, by0 - 4.0f + 3.0f), panel.P(bx1 + 4.0f + 2.0f, by1 + 4.0f + 3.0f),
                              IM_COL32(0, 0, 0, 130), 3.0f * s);
            dl->AddRectFilled(panel.P(bx0 - 4.0f, by0 - 4.0f), panel.P(bx1 + 4.0f, by1 + 4.0f), IM_COL32(8, 8, 8, 255), 3.0f * s);
            dl->AddRect(panel.P(bx0 - 4.0f, by0 - 4.0f), panel.P(bx1 + 4.0f, by1 + 4.0f), IM_COL32(58, 60, 58, 255), 3.0f * s, 0, s);
            const ImU32 capHi = active ? IM_COL32(150, 152, 150, 255) : IM_COL32(226, 228, 226, 255);
            const ImU32 capLo = active ? IM_COL32(104, 106, 104, 255) : IM_COL32(164, 166, 164, 255);
            dl->AddRectFilledMultiColor(b0, b1, capHi, capHi, capLo, capLo);
            if (active)
                dl->AddRectFilledMultiColor(b0, panel.P(bx1, by0 + 8.0f), IM_COL32(0, 0, 0, 110), IM_COL32(0, 0, 0, 110),
                                            IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
            else
                dl->AddLine(panel.P(bx0 + 3.0f, by0 + 2.5f), panel.P(bx1 - 3.0f, by0 + 2.5f), IM_COL32(250, 251, 250, 200), 1.3f * s);
            if (ImGui::IsItemHovered() && !active)
                dl->AddRectFilled(b0, b1, IM_COL32(255, 255, 255, 22), 2.0f * s);
            dl->AddRect(b0, b1, IM_COL32(40, 41, 40, 255), 2.0f * s, 0, s);
            if (clicked) vcaMeterSource = i;
        }
        // Square-ended bracket with METER in the gap.
        const float bx0 = x - 4.0f, bx1 = x + cells.back().x1 + 4.0f;
        const float byy = y + 66.0f;
        const float s = panel.scale();
        const float gap = 34.0f;
        const float mid = 0.5f * (bx0 + bx1);
        dl->AddLine(panel.P(bx0, byy), panel.P(mid - gap, byy), kVcaInk, 2.0f * s);
        dl->AddLine(panel.P(mid + gap, byy), panel.P(bx1, byy), kVcaInk, 2.0f * s);
        dl->AddLine(panel.P(bx0, byy - 9.0f), panel.P(bx0, byy), kVcaInk, 2.0f * s);
        dl->AddLine(panel.P(bx1, byy - 9.0f), panel.P(bx1, byy), kVcaInk, 2.0f * s);
        spacedText(dl, mid, byy - 7.0f, 11.0f, kVcaInk, "METER", 0);
    }

    float levelMeterDb(bool output) const
    {
        void* instance = getPluginInstancePointer();
        if (instance != nullptr)
        {
            if (output && multiCompGetOutputLevel != nullptr)
                return multiCompGetOutputLevel(instance);
            if (!output && multiCompGetInputLevel != nullptr)
                return multiCompGetInputLevel(instance);
        }
        return -60.0f;
    }

    void busKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
                 const char* label, const char* const* labels, int count,
                 bool discrete, const char* suffix = "")
    {
        const float s = panel.scale(), radius = 23.0f;
        // Preserve the wider legacy host ranges for existing sessions while the
        // face and its gestures follow the reference's +/-15 and 0..15 dB travel.
        const float lo = p == P_BUS_THRESHOLD ? -15.0f : hostMinimum(p);
        const float hi = p == P_BUS_MAKEUP ? 15.0f : hostMaximum(p);
        busKnobSplitter.Split(dl, 2);
        busKnobSplitter.SetCurrentChannel(dl, 1);
        const int choice = multicompp::ui_detail::choiceIndex(values[p], count);
        const char* choiceText = p == P_BUS_ATTACK ? multicompp::kBusAttack[choice]
            : p == P_BUS_RELEASE ? multicompp::kBusRelease[choice]
            : p == P_BUS_RATIO ? multicompp::kBusRatios[choice] : nullptr;
        beginTrackedControl(p, id, true);
        panel.knob(id, p, lo, hi, x, y, radius, values[p], hostDefaultValue(p),
                   discrete, false, discrete ? "%.0f" : "%.1f", suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, !discrete,
                   choiceText, false, 0.0f, 0.0f, false, true, 10.0f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this, !discrete);
        endTrackedControl();
        // These are switches. Offer their actual timing/ratio labels instead of
        // a numeric editor that would interpret a typed value as an enum index.
        if (discrete)
        {
            ImGui::PushID(id);
            if (ImGui::BeginPopupContextItem("bus_switch"))
            {
                const char* const* choices = p == P_BUS_ATTACK ? multicompp::kBusAttack
                    : p == P_BUS_RELEASE ? multicompp::kBusRelease : multicompp::kBusRatios;
                for (int i = 0; i < count; ++i)
                    if (ImGui::MenuItem(choices[i], nullptr, i == choice))
                        setHostValue(p, static_cast<float>(i));
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        busKnobSplitter.SetCurrentChannel(dl, 0);
        // Switches use the narrow sweep printed on the console knobs. The
        // continuously variable controls use the full 270 degree travel.
        const float half = discrete ? 65.0f : 135.0f;
        const auto angle = [half](float t) { return (-half + 2.0f * half * t) * kUiPi / 180.0f; };
        const int dots = discrete ? count : 9;
        for (int i = 0; i < dots; ++i)
        {
            const float a = angle(static_cast<float>(i) / (dots - 1));
            dl->AddCircleFilled(panel.P(x + 32.0f * std::sin(a), y - 32.0f * std::cos(a)),
                                 2.1f * s, IM_COL32(218, 220, 218, 255), 12);
        }
        for (int i = 0; i < count; ++i)
        {
            float t = static_cast<float>(i) / (count - 1);
            if (p == P_BUS_FADE_RATE)
            {
                static constexpr float rates[] = {1, 3, 5, 10, 20, 40, 60};
                t = (hostValueForPlain(p, rates[i]) - lo) / (hi - lo);
            }
            else if (p == P_BUS_SC && i == 1)
                t = (hostValueForPlain(p, 80.0f) - lo) / (hi - lo);
            const float a = angle(t);
            panel.text(dl, x + 46.0f * std::sin(a), y - 46.0f * std::cos(a) - 5,
                       10.0f, IM_COL32(224, 226, 223, 255), labels[i], 0, true);
        }
        const ImVec2 c = panel.P(x, y);
        // The skirt ribs turn with the pointer, so the whole knob reads as
        // one turning part; the drop shadow stays put.
        const float t = std::clamp((values[p] - lo) / std::max(hi - lo, 0.001f), 0.0f, 1.0f);
        const float a = angle(t);
        dl->AddCircleFilled(panel.P(x + 3, y + 4), 26.0f * s, IM_COL32(0, 0, 0, 110), 48);
        dl->AddCircleFilled(c, 25.0f * s, IM_COL32(86, 89, 87, 255), 48);
        for (int i = 0; i < 16; ++i)
        {
            const float rib = a + i * 2.0f * kUiPi / 16.0f;
            dl->AddLine(panel.P(x + 21.0f * std::sin(rib), y - 21.0f * std::cos(rib)),
                        panel.P(x + 24.5f * std::sin(rib), y - 24.5f * std::cos(rib)),
                        IM_COL32(144, 148, 144, 255), 3.0f * s);
        }
        dl->AddCircleFilled(c, 21.0f * s, IM_COL32(144, 148, 144, 255), 48);
        dl->AddCircle(c, 19.5f * s, IM_COL32(192, 196, 191, 180), 48, 1.2f * s);
        dl->AddCircleFilled(c, 18.0f * s, IM_COL32(165, 169, 164, 255), 48);
        dl->AddLine(panel.P(x + 3.0f * std::sin(a), y - 3.0f * std::cos(a)),
                    panel.P(x + 20.0f * std::sin(a), y - 20.0f * std::cos(a)),
                    IM_COL32(20, 23, 21, 255), 2.4f * s);
        panel.text(dl, x, y + 39, 10.0f, IM_COL32(239, 240, 236, 255), label, 0, true);
        if (values[p] < lo || values[p] > hi)
        {
            char readout[40];
            std::snprintf(readout, sizeof(readout), "%.1f%s", plainValueForHost(p, values[p]), suffix);
            panel.text(dl, x, y + 52, 9.0f, IM_COL32(239, 240, 236, 255), readout, 0, true);
        }
        busKnobSplitter.Merge(dl);
    }

    void busButton(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
                   const char* label, bool inverted = false)
    {
        const float s = panel.scale();
        bool on = (values[p] >= 0.5f) != inverted;
        if (p == P_BUS_FADE && multiCompGetBusFadePosition != nullptr)
        {
            const float position = multiCompGetBusFadePosition(getPluginInstancePointer());
            const bool moving = on ? position < 1.0f : position > 0.0f;
            if (moving) on = std::fmod(ImGui::GetTime(), 0.6) < 0.3;
        }
        ImGui::SetCursorScreenPos(panel.P(x - 29, y - 29));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(58 * s, 58 * s));
        dl->AddRectFilled(panel.P(x - 33, y - 33), panel.P(x + 33, y + 33), IM_COL32(18, 20, 18, 255));
        dl->AddRect(panel.P(x - 33, y - 33), panel.P(x + 33, y + 33), IM_COL32(107, 110, 104, 255), 0, 0, 2 * s);
        dl->AddRectFilledMultiColor(panel.P(x - 28, y - 28), panel.P(x + 28, y + 28),
                                    IM_COL32(177, 180, 168, 255), IM_COL32(93, 96, 85, 255),
                                    IM_COL32(27, 29, 25, 255), IM_COL32(81, 84, 75, 255));
        dl->AddRectFilled(panel.P(x - 23, y - 23), panel.P(x + 23, y + 23),
                          on ? IM_COL32(246, 209, 102, 255) : IM_COL32(90, 95, 88, 255));
        dl->AddRectFilledMultiColor(panel.P(x - 20, y - 20), panel.P(x + 20, y + 20),
                                    on ? IM_COL32(255, 255, 241, 255) : IM_COL32(206, 210, 201, 255),
                                    on ? IM_COL32(255, 255, 241, 255) : IM_COL32(196, 200, 191, 255),
                                    on ? IM_COL32(255, 245, 189, 255) : IM_COL32(145, 151, 141, 255),
                                    on ? IM_COL32(255, 245, 189, 255) : IM_COL32(153, 158, 149, 255));
        panel.text(dl, x, y - 6, 10.0f, IM_COL32(46, 48, 39, 255), label, 0, true);
        if (clicked) setValue(p, values[p] >= 0.5f ? 0.0f : 1.0f);
    }

    void drawBus(ImDrawList* dl)
    {
        const float s = panel.scale();
        constexpr ImU32 ink = IM_COL32(235, 237, 233, 255);
        dl->AddRectFilled(panel.P(0, 344), panel.P(1120, 654), IM_COL32(43, 47, 45, 255));
        dl->AddRectFilledMultiColor(panel.P(44, 346), panel.P(1076, 651),
                                    IM_COL32(94, 100, 97, 255), IM_COL32(77, 83, 80, 255),
                                    IM_COL32(49, 54, 52, 255), IM_COL32(66, 71, 68, 255));
        // Fine deterministic powder-coat grain; remains static while meters move.
        // 1500 quads (6k vertices) keeps the whole panel well inside ImGui's
        // 16-bit index space; 9000 pushed the draw list toward the limit.
        uint32_t grain = 0x534c4742u;
        for (int i = 0; i < 1500; ++i)
        {
            grain = grain * 1664525u + 1013904223u;
            const float x = 46.0f + static_cast<float>(grain & 65535u) * (1028.0f / 65535.0f);
            grain = grain * 1664525u + 1013904223u;
            const float y = 347.0f + static_cast<float>(grain & 65535u) * (302.0f / 65535.0f);
            dl->AddRectFilled(panel.P(x, y), panel.P(x + 0.65f, y + 0.65f),
                              (grain & 65536u) ? IM_COL32(232, 236, 230, 23) : IM_COL32(0, 0, 0, 30));
        }
        for (float x : {23.0f, 1097.0f})
            for (float y : {362.0f, 634.0f})
            {
                dl->AddCircleFilled(panel.P(x, y), 6.0f * s, IM_COL32(26, 29, 26, 255), 24);
                dl->AddCircle(panel.P(x, y), 6.0f * s, IM_COL32(142, 147, 140, 255), 24, s);
                dl->AddLine(panel.P(x - 3, y + 3), panel.P(x + 3, y - 3), IM_COL32(138, 143, 135, 255), 1.4f * s);
            }
        panel.text(dl, 560, 356, 22, ink, "BUS COMPRESSOR", 0, true);
        dl->AddRect(panel.P(69, 391), panel.P(1051, 633), ink, 6 * s, 0, 1.8f * s);
        dl->AddRectFilled(panel.P(430, 624), panel.P(690, 642), IM_COL32(57, 62, 59, 255));
        panel.text(dl, 560, 626, 13, ink, "STEREO BUS COMPRESSION", 0, true);

        static constexpr duskdaf::VuTick ticks[] = {
            {0, "0", true}, {4, "4", true}, {8, "8", true},
            {12, "12", true}, {16, "16", true}, {20, "20", true}};
        duskdaf::VuScaleConfig meterScale;
        meterScale.ticks = ticks; meterScale.tickCount = 6;
        meterScale.minDb = 0; meterScale.maxDb = 20; meterScale.redFromDb = 100;
        meterScale.legend = "dB"; meterScale.sublabel = "COMPRESSION";
        duskdaf::VuStyle meterStyle;
        meterStyle.bezelLight = IM_COL32(80, 83, 79, 255);
        meterStyle.bezelDark = IM_COL32(14, 17, 14, 255);
        meterStyle.bezelLine = IM_COL32(15, 17, 15, 255);
        meterStyle.lip = IM_COL32(22, 24, 22, 255);
        meterStyle.faceBase = IM_COL32(26, 28, 26, 255);
        meterStyle.faceTopTint = IM_COL32(69, 72, 68, 100);
        meterStyle.faceBottomTint = IM_COL32(12, 14, 12, 80);
        meterStyle.ink = IM_COL32(188, 194, 184, 255);
        meterStyle.sublabelColor = meterStyle.ink;
        meterStyle.needle = IM_COL32(244, 246, 239, 255);
        meterStyle.overloadArc = false; meterStyle.cornerMarks = false;
        meterStyle.pivotBelowFace = 15; meterStyle.sweepHalfAngleDeg = 45;
        meterStyle.smoothNeedle = false;
        meterStyle.maximumNeedleDeflection = 1.1f;
        meterScale.deflection = [](float db, const duskdaf::VuScaleConfig&) {
            return std::clamp(db / 20.0f, 0.0f, 1.1f);
        };
        auto* instance = getPluginInstancePointer();
        const float busReading = instance && multiCompGetBusMeterReading != nullptr
            ? multiCompGetBusMeterReading(instance) : -meter(kMeterMaster);
        meterStyle.radiusTopInset = 22; meterStyle.tickLabelRadiusFrac = 0.99f;
        meterStyle.majorTickLength = -17; meterStyle.tickLabelSize = 12;
        meterStyle.legendOffset = 0.49f; meterStyle.sublabelSize = 10;
        duskdaf::drawVuMeter(panel, dl, 91, 407, 344, 553,
                             busReading, busVuNeedle, meterScale,
                             IM_COL32(119, 124, 115, 255), meterStyle);
        busButton(dl, "bus_in", P_BYPASS, 217, 593, "IN", true);

        static constexpr const char* threshold[] = {"-15", "0", "+15"};
        static constexpr const char* makeup[] = {"0", "7.5", "+15"};
        static constexpr const char* ratios[] = {"2", "4", "10"};
        static constexpr const char* attacks[] = {".1", ".3", "1", "3", "10", "30"};
        static constexpr const char* releases[] = {".1", ".3", ".6", "1.2", "Auto"};
        static constexpr const char* sc[] = {"OFF", "80", "500"};
        static constexpr const char* mix[] = {"0", "50", "100"};
        static constexpr const char* rates[] = {"1", "3", "5", "10", "20", "40", "60"};
        busKnob(dl, "bus_thr", P_BUS_THRESHOLD, 409, 449, "THRESHOLD", threshold, 3, false, " dB");
        busKnob(dl, "bus_ratio", P_BUS_RATIO, 536, 449, "RATIO", ratios, 3, true, ":1");
        busKnob(dl, "bus_makeup", P_BUS_MAKEUP, 663, 449, "MAKE-UP", makeup, 3, false, " dB");
        busKnob(dl, "bus_sc", P_BUS_SC, 790, 449, "SC FILTER Hz", sc, 3, false, " Hz");
        busKnob(dl, "bus_mix", P_BUS_MIX, 929, 449, "MIX %", mix, 3, false, "%");
        busKnob(dl, "bus_attack", P_BUS_ATTACK, 409, 568, "ATTACK ms", attacks, 6, true);
        busKnob(dl, "bus_release", P_BUS_RELEASE, 536, 568, "RELEASE s", releases, 5, true);
        vcaTrim(dl, "bus_hr", P_BUS_HEADROOM, 663, 568, 11, "HR dB", "%.0f", " dB");
        busKnob(dl, "bus_rate", P_BUS_FADE_RATE, 790, 568, "RATE s", rates, 7, false, " s");
        busButton(dl, "bus_fade", P_BUS_FADE, 929, 568, "FADE");
        panel.text(dl, 929, 610, 10, ink, "AUTO FADE", 0, true);
    }

    // Studio VCA face: the reference studio VCA compressor's panel, laid out from its own proportions (its 1776 x
    // 568 px panel onto the 1120 x 310 face: x 0.631 and y 0.546 per px; round parts use the y scale). The dials
    // follow the reference's travel and printed scales (ui_detail::studioVcaDialPosition); the meters are its I/O
    // meter (INPUT/OUTPUT switch, threshold mark on INPUT) and gain reduction meter. Label STUDIO VCA, no marks.
    struct StudioVcaMark { float plain; const char* label; };
    static constexpr ImU32 kSvcaInk = IM_COL32(246, 232, 226, 255);

    void drawStudioVca(ImDrawList* dl)
    {
        drawStudioVcaPanel(dl);
        const float s = panel.scale();
        // The engraved S-curve across the meter bay, behind both meters.
        dl->AddBezierCubic(panel.P(511, 623), panel.P(707, 623), panel.P(663, 382), panel.P(859, 382),
                           IM_COL32(255, 120, 105, 70), 2.6f * s, 48);
        dl->AddBezierCubic(panel.P(510, 622), panel.P(706, 622), panel.P(662, 381), panel.P(858, 381),
                           IM_COL32(70, 6, 6, 230), 1.7f * s, 48);

        static constexpr StudioVcaMark inputMarks[] = {{-18, "-18"}, {-12, "-12"}, {-6, "-6"}, {0, "0"},
                                                       {6, "+6"}, {12, "+12"}, {18, "+18"}};
        static constexpr StudioVcaMark ratioMarks[] = {{1.5f, "1.5"}, {2, "2"}, {3, "3"}, {5, "5"},
                                                       {10, "10"}, {50, nullptr}};   // nullptr: the infinity mark
        static constexpr StudioVcaMark thresholdMarks[] = {{-50, "-50"}, {-40, "-40"}, {-30, "-30"},
                                                           {-20, "-20"}, {-10, "-10"}};
        static constexpr StudioVcaMark attackMarks[] = {{0.1805f, "FAST"}, {70.6865f, "SLOW"}};
        static constexpr StudioVcaMark releaseMarks[] = {{100, "0.1"}, {1000, "1"}, {2000, "2"}, {3000, "3"},
                                                         {4000, "4"}};
        static constexpr StudioVcaMark makeupMarks[] = {{0, "0"}, {10, "10"}, {20, "20"}, {30, "30"}, {40, "40"}};
        static constexpr StudioVcaMark mixMarks[] = {{0, "0"}, {100, "100"}};
        studioVcaKnob(dl, "sv_in", P_SVCA_INPUT, 77, 550, 25, "INPUT", inputMarks, 7);
        studioVcaKnob(dl, "sv_ratio", P_SVCA_RATIO, 230, 429, 25, "RATIO", ratioMarks, 6);
        studioVcaKnob(dl, "sv_att", P_SVCA_ATTACK, 230, 550, 25, "ATTACK", attackMarks, 2);
        studioVcaKnob(dl, "sv_thr", P_SVCA_THRESHOLD, 390, 429, 25, "THRESHOLD", thresholdMarks, 5);
        studioVcaKnob(dl, "sv_rel", P_SVCA_RELEASE, 390, 550, 25, "RELEASE", releaseMarks, 5);
        studioVcaKnob(dl, "sv_out", P_SVCA_OUT, 1013, 428, 25, "MAKE UP GAIN", makeupMarks, 5);
        studioVcaKnob(dl, "sv_mix", P_MIX, 1013, 554, 22, "DRY/WET", mixMarks, 2);
        panel.text(dl, 230, 476, 11.0f, kSvcaInk, "RATIO", 0, true);
        panel.text(dl, 390, 476, 11.0f, kSvcaInk, "THRESHOLD", 0, true);
        panel.text(dl, 77, 603, 11.0f, kSvcaInk, "INPUT", 0, true);
        panel.text(dl, 230, 603, 11.0f, kSvcaInk, "ATTACK", 0, true);
        panel.text(dl, 390, 603, 11.0f, kSvcaInk, "RELEASE", 0, true);
        panel.text(dl, 1013, 469, 11.0f, kSvcaInk, "MAKE UP", 0, true);
        panel.text(dl, 1013, 482, 11.0f, kSvcaInk, "GAIN", 0, true);
        panel.text(dl, 1013, 603, 11.0f, kSvcaInk, "DRY/WET", 0, true);
        panel.text(dl, 824, 603, 11.0f, kSvcaInk, "GAIN REDUCTION", 0, true);

        studioVcaLamp(dl, "sv_auto", P_SVCA_AUTO, 308, 487);
        panel.text(dl, 308, 503, 10.0f, kSvcaInk, "AUTO", 0, true);
        panel.text(dl, 308, 515, 10.0f, kSvcaInk, "RELEASE", 0, true);

        // I/O meter: the input after the Input control, or the output; on INPUT a red mark shows the threshold.
        static constexpr StudioVcaMark ioScale[] = {{-60, "-60"}, {-50, "-50"}, {-40, "-40"}, {-30, "-30"},
                                                    {-20, "-20"}, {-10, "-10"}, {0, "0"}};
        static constexpr StudioVcaMark grScale[] = {{-20, "-20"}, {-15, "-15"}, {-10, "-10"}, {-5, "-5"},
                                                    {0, "0"}};
        void* const instance = getPluginInstancePointer();
        const bool outputMeter = values[P_SVCA_METER] >= 0.5f;
        const auto level = outputMeter ? multiCompGetStudioVcaOutputLevel : multiCompGetStudioVcaInputLevel;
        const float ioDb = instance != nullptr && level != nullptr ? level(instance) : -120.0f;
        const float threshold = plainValueForHost(P_SVCA_THRESHOLD, values[P_SVCA_THRESHOLD]);
        studioVcaMeter(dl, 561, 459, ioScale, 7, -60.0f, 0.0f, ioDb,
                       outputMeter ? std::numeric_limits<float>::quiet_NaN() : threshold);
        studioVcaSwitch(dl, "sv_meter", P_SVCA_METER, 561, 558);
        studioVcaMeter(dl, 825, 510, grScale, 5, -20.0f, 0.0f, meter(kMeterMaster),
                       std::numeric_limits<float>::quiet_NaN());
    }

    // Red brushed panel with rack ears and the engraved STUDIO VCA label.
    void drawStudioVcaPanel(ImDrawList* dl)
    {
        const float s = panel.scale();
        dl->AddRectFilledMultiColor(panel.P(0, 344), panel.P(1120, 654),
                                    IM_COL32(214, 46, 38, 255), IM_COL32(196, 38, 32, 255),
                                    IM_COL32(150, 24, 22, 255), IM_COL32(168, 30, 26, 255));
        // Horizontal brushing: deterministic hairlines, lighter and darker.
        uint32_t grain = 0x53564341u;
        for (float y = 345.0f; y < 653.0f; y += 1.5f)
        {
            grain = grain * 1664525u + 1013904223u;
            const int shade = static_cast<int>((grain >> 24) & 31u);
            dl->AddLine(panel.P(0, y), panel.P(1120, y),
                        (grain & 1u) ? IM_COL32(255, 170, 160, shade / 2) : IM_COL32(60, 0, 0, shade), 0.8f * s);
        }
        dl->AddLine(panel.P(0, 345), panel.P(1120, 345), IM_COL32(255, 150, 135, 150), 1.5f * s);
        dl->AddLine(panel.P(0, 653), panel.P(1120, 653), IM_COL32(70, 6, 6, 220), 1.5f * s);
        // Rack ears: a slot in each corner.
        for (float x : {16.0f, 1104.0f})
            for (float y : {353.0f, 644.0f})
            {
                dl->AddRectFilled(panel.P(x - 11, y - 5.5f), panel.P(x + 11, y + 5.5f), IM_COL32(28, 4, 4, 255),
                                  5.5f * s);
                dl->AddRect(panel.P(x - 11, y - 5.5f), panel.P(x + 11, y + 5.5f), IM_COL32(255, 130, 115, 90),
                            5.5f * s, 0, 0.8f * s);
            }
        // Engraved label where the reference prints its model numeral: highlight, shadow, then the face.
        struct Layer { float dx, dy; ImU32 colour; };
        static constexpr Layer layers[] = {{-0.8f, -0.8f, IM_COL32(255, 135, 120, 120)},
                                           {1.4f, 1.6f, IM_COL32(85, 8, 8, 230)},
                                           {0.0f, 0.0f, IM_COL32(176, 28, 25, 255)}};
        for (const Layer& layer : layers)
        {
            panel.text(dl, 82 + layer.dx, 396 + layer.dy, 19.0f, layer.colour, "STUDIO", 0, true);
            panel.text(dl, 82 + layer.dx, 420 + layer.dy, 40.0f, layer.colour, "VCA", 0, true);
        }
    }

    // A Studio VCA dial as the reference draws it: a polished chrome knob in a dark recessed well, crossed along its
    // pointer by a flat grip bar with a cream index cup near the pointer end, and a cream dot at each scale mark with
    // its numeral further out. The light is fixed at the upper right: the chrome's reflections and the well stay put
    // while the bar turns, and the bar's lit and shaded edges follow the light.
    void studioVcaKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y, float radius,
                       const char* name, const StudioVcaMark* marks, int markCount)
    {
        char readout[32];
        studioVcaReadout(p, plainValueForHost(p, values[p]), readout, sizeof(readout));
        studioVcaKnobSplitter.Split(dl, 2);
        studioVcaKnobSplitter.SetCurrentChannel(dl, 1);
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius, values[p], hostDefaultValue(p),
                   false, false, "%.3f", "", 0, true, false, nullptr, false, 1.0f, 0.0f, name, true,
                   readout, false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   &studioVcaHostToDial, &studioVcaDialToHost, this, true, &studioVcaParseText);
        endTrackedControl();
        studioVcaKnobSplitter.SetCurrentChannel(dl, 0);
        const float s = panel.scale();
        const ImVec2 c = panel.P(x, y);
        const float r = radius * s;
        for (int i = 0; i < markCount; ++i)
        {
            const float angle = duskdaf::DuskPanel::knobAngle(multicompp::ui_detail::studioVcaDialPosition(p, marks[i].plain));
            dl->AddCircleFilled(ImVec2(c.x + std::sin(angle) * r * 1.21f, c.y - std::cos(angle) * r * 1.21f),
                                0.032f * r, kSvcaInk, 12);
            // The numeral further out, clear of its dot even beside the dial, where a label is at its widest.
            const float lx = x + std::sin(angle) * (radius + 16.0f);
            const float ly = y - std::cos(angle) * (radius + 16.0f) - 5.5f;
            if (marks[i].label != nullptr)
                panel.text(dl, lx, ly, 9.5f, kSvcaInk, marks[i].label, 0, false);
            else
                studioVcaInfinityMark(dl, lx, ly + 5.5f);
        }
        studioVcaWell(dl, c, r);
        studioVcaChrome(dl, c, r);
        const float angle = duskdaf::DuskPanel::knobAngle(
            multicompp::ui_detail::studioVcaDialPosition(p, plainValueForHost(p, values[p])));
        studioVcaGripBar(dl, c, r, ImVec2(std::sin(angle), -std::cos(angle)), s);
        studioVcaKnobSplitter.Merge(dl);
    }

    // The Studio VCA face's light: a unit vector toward the upper right (screen axes, y down).
    static constexpr float kSvcaLightX = 0.70710678f, kSvcaLightY = -0.70710678f;

    // The point `distance` from q along the light (screen pixels; negative: away from it).
    static ImVec2 studioVcaTowardLight(ImVec2 q, float distance)
    {
        return ImVec2(q.x + distance * kSvcaLightX, q.y + distance * kSvcaLightY);
    }

    static constexpr int kSvcaRound = 64;   // points on a Studio VCA dial's round outlines

    // An ellipse round `centre` with semi-axes `along` the light and `across` it, as kSvcaRound points, clockwise on
    // screen as an anti-aliased fill wants them.
    static void studioVcaOval(ImVec2 centre, float along, float across, ImVec2* out)
    {
        for (int i = 0; i < kSvcaRound; ++i)
        {
            const float phi = 2.0f * duskdaf::DuskPanel::kPi * static_cast<float>(i) / kSvcaRound;
            const float a = along * std::cos(phi), b = across * std::sin(phi);
            out[i] = ImVec2(centre.x + a * kSvcaLightX - b * kSvcaLightY, centre.y + a * kSvcaLightY + b * kSvcaLightX);
        }
    }

    // The recess the dial sits in: near-black maroon, wider and darker toward the upper left, its lower right lip
    // catching the light.
    static void studioVcaWell(ImDrawList* dl, ImVec2 c, float r)
    {
        const ImVec2 centre(c.x - 0.018f * r, c.y - 0.018f * r);
        const float radius = 1.085f * r;
        dl->AddCircleFilled(ImVec2(centre.x + 0.035f * r, centre.y + 0.035f * r), radius,
                            IM_COL32(255, 128, 112, 140), kSvcaRound);
        const float diagonal = 0.70710678f * radius;
        gradientCircle(dl, centre, radius, ImVec2(centre.x - diagonal, centre.y - diagonal),
                       ImVec2(centre.x + diagonal, centre.y + diagonal), IM_COL32(22, 5, 7, 255),
                       IM_COL32(58, 15, 17, 255));
    }

    // The polished chrome top, a shallow dome mirroring its surroundings under the fixed light: the red panel as a
    // brownish grey toward the lower left, then light silver bands, each a disc set further toward the light so its
    // edge is a hard curved reflection line, a large near-white highlight in the upper right, and the polished edge
    // lit toward the light. None of it turns with the dial.
    static void studioVcaChrome(ImDrawList* dl, ImVec2 c, float r)
    {
        // Each band's offset toward the light and radius, in knob radii, and its colours at the edges away from and
        // toward the light.
        struct Band { float offset, radius; ImU32 shaded, lit; };
        static constexpr Band bands[] = {
            {0.0f, 1.0f, IM_COL32(146, 126, 121, 255), IM_COL32(186, 166, 161, 255)},    // the panel, mirrored
            {0.12f, 0.86f, IM_COL32(126, 120, 118, 255), IM_COL32(196, 190, 187, 255)},  // the dark horizon band
            {0.2f, 0.78f, IM_COL32(198, 193, 190, 255), IM_COL32(222, 218, 215, 255)}};  // the light silver
        for (const Band& band : bands)
        {
            const ImVec2 centre = studioVcaTowardLight(c, band.offset * r);
            gradientCircle(dl, centre, band.radius * r, studioVcaTowardLight(centre, -band.radius * r),
                           studioVcaTowardLight(centre, band.radius * r), band.shaded, band.lit);
        }
        ImVec2 outline[kSvcaRound];
        const ImVec2 spot = studioVcaTowardLight(c, 0.48f * r);
        studioVcaOval(spot, 0.36f * r, 0.6f * r, outline);
        dl->AddConvexPolyFilled(outline, kSvcaRound, IM_COL32(255, 255, 255, 60));
        studioVcaOval(spot, 0.26f * r, 0.48f * r, outline);
        dl->AddConvexPolyFilled(outline, kSvcaRound, IM_COL32(251, 251, 249, 240));
        constexpr float upperRight = -0.25f * duskdaf::DuskPanel::kPi;   // ImGui arc angles: clockwise from +x
        dl->PathArcTo(c, 0.98f * r, upperRight - 1.0f, upperRight + 1.0f, 24);
        dl->PathStroke(IM_COL32(255, 250, 246, 150), ImDrawFlags_None, 0.035f * r);
        dl->PathArcTo(c, 0.98f * r, upperRight + duskdaf::DuskPanel::kPi - 1.0f,
                      upperRight + duskdaf::DuskPanel::kPi + 1.0f, 24);
        dl->PathStroke(IM_COL32(60, 30, 28, 150), ImDrawFlags_None, 0.035f * r);
    }

    // The flat grip bar across the full diameter along the pointer d: its shadow on the chrome, an olive grey top
    // lighter toward the light, a bright line on the edge facing the light and a dark one opposite, and the index.
    static void studioVcaGripBar(ImDrawList* dl, ImVec2 c, float r, ImVec2 d, float s)
    {
        const ImVec2 n(-d.y, d.x);
        const float facing = n.x * kSvcaLightX + n.y * kSvcaLightY;   // how squarely the +n edge faces the light
        const float half = 0.30f * r, reach = 0.985f * r;
        constexpr int arcPoints = 8;
        ImVec2 outline[2 * arcPoints];
        int count = studioVcaSlab(c, reach, d, -0.09f * r * facing, half, outline, arcPoints);
        dl->AddConvexPolyFilled(outline, count, IM_COL32(24, 10, 8, 80));
        count = studioVcaSlab(c, reach, d, 0.0f, half, outline, arcPoints);
        const int topVertices = dl->VtxBuffer.Size;
        dl->AddConvexPolyFilled(outline, count, IM_COL32_WHITE);
        shadeLinear(dl, topVertices, studioVcaTowardLight(c, -r), studioVcaTowardLight(c, r),
                    IM_COL32(106, 100, 90, 255), IM_COL32(136, 130, 119, 255));
        const auto edge = [&](float side, ImU32 colour, float width) {
            const float k = side * (half - 0.5f * width);
            const float along = std::sqrt(reach * reach - k * k) - 0.5f * width;
            dl->AddLine(ImVec2(c.x + n.x * k - d.x * along, c.y + n.y * k - d.y * along),
                        ImVec2(c.x + n.x * k + d.x * along, c.y + n.y * k + d.y * along), colour, width);
        };
        const float litSide = facing >= 0.0f ? 1.0f : -1.0f, strength = std::fabs(facing);
        edge(litSide, colourMix(IM_COL32(240, 236, 226, 70), IM_COL32(240, 236, 226, 240), strength), 0.9f * s);
        edge(-litSide, colourMix(IM_COL32(28, 24, 20, 70), IM_COL32(28, 24, 20, 220), strength), 1.1f * s);
        studioVcaIndexCup(dl, c, r, d, s);
    }

    // The pointer index: a small cream cup engraved near the bar's pointer end, open toward the rim.
    static void studioVcaIndexCup(ImDrawList* dl, ImVec2 c, float r, ImVec2 d, float s)
    {
        const ImVec2 n(-d.y, d.x);
        const float bowl = 0.065f * r, leg = 0.05f * r, at = 0.74f * r;
        const auto point = [&](float along, float side) {
            return ImVec2(c.x + d.x * along + n.x * side, c.y + d.y * along + n.y * side);
        };
        constexpr int arcPoints = 9;
        ImVec2 cup[arcPoints + 2];
        cup[0] = point(at + leg, bowl);
        for (int i = 0; i < arcPoints; ++i)
        {
            const float beta = duskdaf::DuskPanel::kPi * static_cast<float>(i) / (arcPoints - 1);
            cup[i + 1] = point(at - bowl * std::sin(beta), bowl * std::cos(beta));
        }
        cup[arcPoints + 1] = point(at + leg, -bowl);
        ImVec2 groove[arcPoints + 2];   // the engraving's shaded wall, on its side toward the light
        for (int i = 0; i < arcPoints + 2; ++i)
            groove[i] = ImVec2(cup[i].x + 0.5f * s * kSvcaLightX, cup[i].y + 0.5f * s * kSvcaLightY);
        dl->AddPolyline(groove, arcPoints + 2, IM_COL32(40, 32, 26, 150), ImDrawFlags_None, s);
        dl->AddPolyline(cup, arcPoints + 2, IM_COL32(246, 238, 216, 255), ImDrawFlags_None, s);
    }

    // The part of the disc (c, r) between the lines at offset - halfWidth and offset + halfWidth along n, which is d
    // turned a quarter clockwise: the grip bar (d its axis) and its shadow. Writes 2 * arcPoints points, clockwise on
    // screen as an anti-aliased fill wants them, and returns that count. Both lines must cut the disc
    // (|offset| + halfWidth < r).
    static int studioVcaSlab(ImVec2 c, float r, ImVec2 d, float offset, float halfWidth, ImVec2* out, int arcPoints)
    {
        const ImVec2 n(-d.y, d.x);
        const float low = std::asin((offset - halfWidth) / r), high = std::asin((offset + halfWidth) / r);
        int count = 0;
        const auto arc = [&](float from, float to) {
            for (int i = 0; i < arcPoints; ++i)
            {
                const float phi = from + (to - from) * static_cast<float>(i) / static_cast<float>(arcPoints - 1);
                out[count++] = ImVec2(c.x + r * (std::cos(phi) * d.x + std::sin(phi) * n.x),
                                      c.y + r * (std::cos(phi) * d.y + std::sin(phi) * n.y));
            }
        };
        arc(low, high);
        arc(duskdaf::DuskPanel::kPi - high, duskdaf::DuskPanel::kPi - low);
        return count;
    }

    // The infinity sign the font lacks, centred on (x, y).
    void studioVcaInfinityMark(ImDrawList* dl, float x, float y)
    {
        const float s = panel.scale();
        constexpr int points = 32;
        ImVec2 path[points];
        for (int i = 0; i < points; ++i)
        {
            const float t = 2.0f * duskdaf::DuskPanel::kPi * static_cast<float>(i) / points;
            const float k = 1.0f + std::sin(t) * std::sin(t);
            path[i] = panel.P(x + 5.0f * std::cos(t) / k, y + 5.0f * std::sin(t) * std::cos(t) / k);
        }
        dl->AddPolyline(path, points, kSvcaInk, ImDrawFlags_Closed, 1.3f * s);
    }

    // The AUTO RELEASE lamp button.
    void studioVcaLamp(ImDrawList* dl, const char* id, uint32_t p, float x, float y)
    {
        const float s = panel.scale();
        const bool on = values[p] >= 0.5f;
        ImGui::SetCursorScreenPos(panel.P(x - 13, y - 13));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(26 * s, 26 * s));
        const ImVec2 c = panel.P(x, y);
        if (on) dl->AddCircleFilled(c, 17.0f * s, IM_COL32(255, 70, 50, 70), 32);
        dl->AddCircleFilled(c, 13.0f * s, IM_COL32(52, 4, 4, 255), 32);
        gradientDisc(dl, c, 11.0f * s, -0.6f, on ? IM_COL32(255, 150, 130, 255) : IM_COL32(170, 40, 36, 255),
                     on ? IM_COL32(220, 20, 16, 255) : IM_COL32(92, 10, 10, 255));
        dl->AddCircleFilled(ImVec2(c.x - 3.5f * s, c.y - 3.5f * s), 3.0f * s,
                            on ? IM_COL32(255, 235, 225, 200) : IM_COL32(255, 160, 150, 70), 16);
        if (clicked) setValue(p, on ? 0.0f : 1.0f);
    }

    // The INPUT/OUTPUT meter switch: a pill with the lit ball on the selected side.
    void studioVcaSwitch(ImDrawList* dl, const char* id, uint32_t p, float x, float y)
    {
        const float s = panel.scale();
        const bool output = values[p] >= 0.5f;
        ImGui::SetCursorScreenPos(panel.P(x - 22, y - 10));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(44 * s, 20 * s));
        dl->AddRectFilled(panel.P(x - 22, y - 10), panel.P(x + 22, y + 10), IM_COL32(24, 4, 4, 255), 10.0f * s);
        dl->AddRect(panel.P(x - 22, y - 10), panel.P(x + 22, y + 10), IM_COL32(255, 120, 105, 80), 10.0f * s, 0, s);
        const ImVec2 ball = panel.P(output ? x + 11 : x - 11, y);
        gradientDisc(dl, ball, 8.0f * s, -0.6f, IM_COL32(255, 196, 120, 255), IM_COL32(200, 92, 24, 255));
        panel.text(dl, x - 28, y - 6, 10.0f, kSvcaInk, "INPUT", 1, true);
        panel.text(dl, x + 28, y - 6, 10.0f, kSvcaInk, "OUTPUT", -1, true);
        if (clicked) setValue(p, output ? 0.0f : 1.0f);
    }

    // One of the face's round meters, the reference's geometry: the needle pivots below the window centre and
    // sweeps +-30 degrees over minDb..maxDb (ui_detail::studioVcaNeedleDegrees); the dark lower half hides its foot.
    // markDb (NaN = none) draws the red threshold mark.
    void studioVcaMeter(ImDrawList* dl, float cx, float cy, const StudioVcaMark* scale, int count,
                        float minDb, float maxDb, float valueDb, float markDb)
    {
        const float s = panel.scale();
        constexpr float bezel = 72.0f, face = 64.0f, split = 8.0f, pivotDrop = 61.0f, arc = 92.0f;   // arc top at 0.49 face
        const ImVec2 c = panel.P(cx, cy);
        dl->AddCircleFilled(ImVec2(c.x + 2.0f * s, c.y + 3.0f * s), (bezel + 2.0f) * s, IM_COL32(60, 0, 0, 150), 64);
        gradientDisc(dl, c, bezel * s, -0.5f, IM_COL32(150, 24, 22, 255), IM_COL32(70, 6, 6, 255), 64);
        dl->AddCircle(c, (bezel - 3.0f) * s, IM_COL32(255, 120, 105, 60), 64, s);
        dl->AddCircleFilled(c, (face + 2.0f) * s, IM_COL32(16, 10, 8, 255), 64);
        // Cream window above the split line, dark below it.
        dl->AddCircleFilled(c, face * s, IM_COL32(38, 26, 20, 255), 64);
        constexpr int segments = 48;
        ImVec2 window[segments + 1];
        const float limit = duskdaf::DuskPanel::kPi - std::acos(split / face);   // from straight up to the split
        for (int i = 0; i <= segments; ++i)
        {
            const float a = -limit + 2.0f * limit * static_cast<float>(i) / segments;
            window[i] = panel.P(cx + face * std::sin(a), cy - face * std::cos(a));
        }
        dl->AddConvexPolyFilled(window, segments + 1, IM_COL32(236, 204, 150, 255));
        dl->PushClipRect(panel.P(cx - face, cy - face), panel.P(cx + face, cy + split), true);
        dl->AddCircleFilled(panel.P(cx, cy - 6), 30.0f * s, IM_COL32(255, 196, 110, 60), 32);   // lamp glow
        dl->PopClipRect();
        // Scale: an arc about the pivot, ticks and numerals (design coordinates).
        const float px = cx, py = cy + pivotDrop;
        const auto at = [&](float degrees, float radius) {
            const float a = degrees * duskdaf::DuskPanel::kPi / 180.0f;
            return panel.P(px + std::sin(a) * radius, py - std::cos(a) * radius);
        };
        dl->PathArcTo(panel.P(px, py), arc * s, (-90.0f - 30.0f) * duskdaf::DuskPanel::kPi / 180.0f,
                      (-90.0f + 30.0f) * duskdaf::DuskPanel::kPi / 180.0f, 32);
        dl->PathStroke(IM_COL32(30, 20, 12, 255), 0, 1.6f * s);
        for (int i = 0; i < count; ++i)
        {
            const float degrees = multicompp::ui_detail::studioVcaNeedleDegrees(scale[i].plain, minDb, maxDb);
            dl->AddLine(at(degrees, arc), at(degrees, arc + 6.0f), IM_COL32(30, 20, 12, 255), 1.4f * s);
            const float a = degrees * duskdaf::DuskPanel::kPi / 180.0f;
            panel.text(dl, px + std::sin(a) * (arc + 10.0f), py - std::cos(a) * (arc + 10.0f) - 4.25f, 8.5f,
                       IM_COL32(30, 20, 12, 255), scale[i].label, 0, false);
        }
        panel.text(dl, cx + 44, cy - 13, 11.0f, IM_COL32(30, 20, 12, 255), "VU", 0, true);
        // Needles, clipped to the window; the window's lower edge is where the dark half hides their feet.
        dl->PushClipRect(panel.P(cx - face, cy - face), panel.P(cx + face, cy + split), true);
        if (std::isfinite(markDb))
            dl->AddLine(panel.P(px, py), at(multicompp::ui_detail::studioVcaNeedleDegrees(markDb, minDb, maxDb), arc - 3.0f),
                        IM_COL32(214, 30, 24, 255), 1.6f * s);
        dl->AddLine(panel.P(px, py), at(multicompp::ui_detail::studioVcaNeedleDegrees(valueDb, minDb, maxDb), arc - 3.0f),
                    IM_COL32(16, 14, 12, 255), 1.8f * s);
        dl->PopClipRect();
        // Glass: a soft reflection band on the right.
        dl->AddRectFilledMultiColor(panel.P(cx + 22, cy - 58), panel.P(cx + 44, cy + split),
                                    IM_COL32(255, 255, 255, 40), IM_COL32(255, 255, 255, 10),
                                    IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 20));
    }

    // A disc lit along `angle` (radians from straight up): `lit` on that side, `shade` opposite.
    static void gradientDisc(ImDrawList* dl, ImVec2 c, float r, float angle, ImU32 lit, ImU32 shade,
                             int segments = 48)
    {
        const ImVec4 a = ImGui::ColorConvertU32ToFloat4(lit), b = ImGui::ColorConvertU32ToFloat4(shade);
        const auto mix = [&](float t) {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(b.x + (a.x - b.x) * t, b.y + (a.y - b.y) * t,
                                                         b.z + (a.z - b.z) * t, b.w + (a.w - b.w) * t));
        };
        const ImVec2 uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
        const ImVec2 dir(std::sin(angle), -std::cos(angle));
        dl->PrimReserve(segments * 3, segments + 1);
        const unsigned int base = dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(c, uv, mix(0.5f));
        for (int i = 0; i < segments; ++i)
        {
            const float t = 2.0f * duskdaf::DuskPanel::kPi * static_cast<float>(i) / segments;
            const ImVec2 p(c.x + r * std::cos(t), c.y + r * std::sin(t));
            dl->PrimWriteVtx(p, uv, mix(0.5f + 0.5f * ((p.x - c.x) * dir.x + (p.y - c.y) * dir.y) / r));
        }
        for (int i = 0; i < segments; ++i)
        {
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % segments));
        }
    }

    // The dial's value as the reference prints it (Release in seconds), with locale-free decimals, so the typed-
    // entry field it seeds parses back (studioVcaParseText).
    static void studioVcaReadout(uint32_t p, float plain, char* text, size_t size)
    {
        const auto fixed = [&](float v, int decimals, const char* unit) {
            const float scale = decimals == 2 ? 100.0f : decimals == 1 ? 10.0f : 1.0f;
            const long scaled = std::lround(std::fabs(v) * scale);
            const long whole = static_cast<long>(scaled / static_cast<long>(scale));
            const long part = scaled % static_cast<long>(scale);
            const char* sign = v < 0.0f && scaled != 0 ? "-" : "";
            if (decimals == 0) std::snprintf(text, size, "%s%ld%s", sign, whole, unit);
            else std::snprintf(text, size, decimals == 2 ? "%s%ld.%02ld%s" : "%s%ld.%01ld%s", sign, whole, part, unit);
        };
        if (p == P_SVCA_RATIO) fixed(plain, 1, ":1");
        else if (p == P_SVCA_ATTACK) fixed(plain, 2, " ms");
        else if (p == P_SVCA_RELEASE) fixed(plain * 0.001f, 2, " s");
        else if (p == P_MIX) fixed(plain, 0, "%");
        else fixed(plain, 1, " dB");
    }

    static float studioVcaHostToDial(float host, uint32_t p, void* context)
    {
        const float plain = static_cast<MultiCompUI*>(context)->plainValueForHost(p, host);
        return multicompp::ui_detail::studioVcaDialPosition(p, plain);
    }

    static float studioVcaDialToHost(float position, uint32_t p, void* context)
    {
        const float plain = multicompp::ui_detail::studioVcaDialPlain(p, position);
        return static_cast<MultiCompUI*>(context)->hostValueForPlain(p, plain);
    }

    // Typed entry in the readout's units (Release in seconds), with or without the unit; any value of the
    // control's full range, including those beyond the dial's travel.
    static bool studioVcaParseText(const char* text, uint32_t p, void* context, float& host)
    {
        float plain = 0.0f;
        if (!multicompp::ui_detail::studioVcaTypedPlain(text, p, plain)) return false;
        host = static_cast<MultiCompUI*>(context)->hostValueForPlain(p, plain);
        return true;
    }

    // ---- Digital and Multiband rack faces -------------------------------------------------------------------
    // Both carry the JUCE editor's controls, meters and drawing (EnhancedCompressorEditor, ModernCompressorPanels.h,
    // AnalogLookAndFeel.cpp) on the rack-height canvas every mode shares: the INPUT and OUTPUT meters keep their
    // distance from the window edges, and what the original stacked between them sits side by side (Digital: the
    // gain-reduction display, the side-chain high-pass, the two rows of knobs; Multiband: the band meters and the
    // selected band's knobs). Positions are written in visible-canvas coordinates (0 = the top of the header);
    // faceY() moves them onto the mode canvas.
    static constexpr float faceY(float visibleY) noexcept { return visibleY + kModeCanvasShiftY; }

    // A line of text centred on (x, centreY).
    void textCentred(ImDrawList* dl, float x, float centreY, float size, ImU32 colour, const char* text, bool bold)
    {
        panel.text(dl, x, centreY - 0.5f * size, size, colour, text, 0, bold);
    }

    // Same full-width rack, mounting ears and printed lettering as the hardware faces.
    // Kept local to these two modes so their finish does not alter the reference panels.
    void drawModernBody(ImDrawList* dl, const char* title, const char* description)
    {
        using Layout = multicompp::ui_detail::ModernFaceLayout;
        const float s = panel.scale();
        const float top = faceY(70), bottom = faceY(kDesignH), titleY = faceY(Layout::titleY);
        dl->AddRectFilled(panel.P(0, top), panel.P(kDesignW, bottom), IM_COL32(37, 39, 38, 255));
        gradientRect(dl, panel.P(32, top + 1), panel.P(kDesignW - 32, bottom - 1), 0,
                     panel.P(32, top), panel.P(32, bottom), IM_COL32(48, 51, 50, 255), IM_COL32(24, 27, 26, 255));
        for (int row = 1; row < 155; ++row)
        {
            const float y = top + 2.0f * static_cast<float>(row);
            dl->AddLine(panel.P(33, y), panel.P(kDesignW - 33, y),
                        row % 3 == 0 ? IM_COL32(255, 255, 255, 3) : IM_COL32(0, 0, 0, 8), s);
        }
        for (float x : {32.0f, kDesignW - 32.0f})
        {
            dl->AddLine(panel.P(x, top), panel.P(x, bottom), IM_COL32(8, 10, 9, 255), 1.5f * s);
            dl->AddLine(panel.P(x + 1, top), panel.P(x + 1, bottom), IM_COL32(107, 112, 108, 80), s);
        }
        dl->AddLine(panel.P(0, top + 1), panel.P(kDesignW, top + 1), IM_COL32(123, 129, 124, 150), s);
        dl->AddLine(panel.P(0, bottom - 2), panel.P(kDesignW, bottom - 2), IM_COL32(6, 8, 7, 255), 2 * s);
        for (float x : {17.0f, kDesignW - 17.0f})
            for (float y : {top + 22.0f, bottom - 23.0f})
                drawOptoScrew(dl, x, y, 5.0f);
        duskdaf::spacedText(panel, dl, kDesignW * 0.5f, titleY - 8.0f, 16.0f, kModernText, title, 0, 0.12f);
        panel.text(dl, 111, titleY - 5, 10, kDim, "MC-2", -1, true);
        panel.text(dl, 1009, titleY - 5, 10, kDim, "PRECISION SERIES", 1, true);
        dl->AddLine(panel.P(111, faceY(106)), panel.P(1009, faceY(106)), IM_COL32(120, 128, 122, 100), s);
        duskdaf::spacedText(panel, dl, kDesignW * 0.5f, faceY(Layout::descriptionY) - 5, 10, kDim,
                           description, 0, 0.12f);
    }

    // One LED of the INPUT/OUTPUT ladders (LEDMeter::drawLEDSegment).
    void drawLedSegment(ImDrawList* dl, float x0, float y0, float x1, float y1, bool lit, bool peak,
                        const duskdaf::LedLadderColors& colours)
    {
        const float s = panel.scale();
        const float h = y1 - y0;
        const ImVec2 a = panel.P(x0, y0), b = panel.P(x1, y1);
        if (lit)
        {
            dl->AddRectFilled(panel.P(x0 - 6, y0 - 6), panel.P(x1 + 6, y1 + 6), colourWithAlpha(colours.glow, 0.25f),
                              4.0f * s);
            dl->AddRectFilled(panel.P(x0 - 4, y0 - 4), panel.P(x1 + 4, y1 + 4), colourWithAlpha(colours.glow, 0.4f),
                              3.0f * s);
            gradientRect(dl, a, b, 2.0f * s, a, ImVec2(a.x, b.y), colourBrighter(colours.lit, 0.2f),
                         colourDarker(colours.lit, 0.3f));
            const ImVec2 h0 = panel.P(x0 + 1, y0 + 1), h1 = panel.P(x1 - 1, y0 + 1 + 0.4f * h);
            gradientRect(dl, h0, h1, 1.5f * s, h0, ImVec2(h0.x, h1.y), IM_COL32(255, 255, 255, 89),
                         IM_COL32(255, 255, 255, 0));
        }
        else if (peak)
        {
            dl->AddRectFilled(panel.P(x0 - 2, y0 - 2), panel.P(x1 + 2, y1 + 2), colourWithAlpha(colours.glow, 0.2f),
                              3.0f * s);
            const ImU32 body = colourMix(colours.lit, colours.unlit, 0.3f);
            gradientRect(dl, a, b, 2.0f * s, a, ImVec2(a.x, b.y), colourBrighter(body, 0.1f), colourDarker(body, 0.2f));
            dl->AddRectFilled(panel.P(x0 + 1, y0 + 1), panel.P(x1 - 1, y0 + 1 + 0.4f * h), IM_COL32(255, 255, 255, 51),
                              1.5f * s);
        }
        else
        {
            gradientRect(dl, a, b, 2.0f * s, a, ImVec2(a.x, b.y), colourBrighter(colours.unlit, 0.15f),
                         colourDarker(colours.unlit, 0.1f));
            dl->AddRect(panel.P(x0 + 0.5f, y0 + 0.5f), panel.P(x1 - 0.5f, y1 - 0.5f), IM_COL32(0, 0, 0, 102), 2.0f * s,
                        0, 0.5f * s);
            dl->AddRectFilled(panel.P(x0 + 1, y0 + 1), panel.P(x1 - 1, y0 + 1 + 0.3f * h), IM_COL32(255, 255, 255, 13),
                              1.5f * s);
        }
    }

    // One INPUT/OUTPUT meter (LEDMeter in stereo, with LEDMeterStyle's title and readout): a recessed frame, two
    // twelve-LED columns over -60..+6 dB with L/R legends, the title above and the peak readout below.
    void drawLevelMeter(ImDrawList* dl, float x0, const duskdaf::LedLadder& ladder, const char* title, float readoutDb)
    {
        using Layout = multicompp::ui_detail::ModernFaceLayout;
        const float s = panel.scale();
        const float x1 = x0 + 40.0f, y0 = faceY(Layout::meterTop), y1 = faceY(Layout::meterBottom);
        textCentred(dl, 0.5f * (x0 + x1), faceY(Layout::meterTitleY), 13.0f, kModernText, title, true);
        const ImVec2 a = panel.P(x0, y0), b = panel.P(x1, y1);
        dl->AddRectFilled(a, b, IM_COL32(0x0A, 0x0A, 0x0A, 255), 4.0f * s);
        gradientRect(dl, a, panel.P(x1, y0 + 15.0f), 4.0f * s, a, panel.P(x0, y0 + 10.0f), IM_COL32(0, 0, 0, 153),
                     IM_COL32(0, 0, 0, 0));
        dl->AddRect(a, b, IM_COL32(0x2A, 0x2A, 0x2A, 255), 4.0f * s, 0, 1.0f * s);
        static constexpr int kLeds = 12;   // static: the lambda below reads it uncaptured (MSVC C3493)
        constexpr float kGap = 2.0f;
        const float columnWidth = (40.0f - 2.0f) * 0.5f;
        const duskdaf::LedLadderStyle style;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float cx0 = x0 + static_cast<float>(ch) * (columnWidth + 2.0f);
            const float lx0 = cx0 + 3.0f, lx1 = cx0 + columnWidth - 3.0f, ly0 = y0 + 3.0f, ly1 = y1 - 12.0f - 3.0f;
            const float ledHeight = ((ly1 - ly0) - (kLeds - 1) * kGap) / kLeds;
            const auto ledsFor = [](float db) {
                return static_cast<int>(std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f) * static_cast<float>(kLeds));
            };
            const int lit = ledsFor(ladder.displayDb(ch)), peak = ledsFor(ladder.peakDb(ch)) - 1;
            for (int i = 0; i < kLeds; ++i)
            {
                const float top = ly1 - static_cast<float>(i + 1) * (ledHeight + kGap) + kGap;
                drawLedSegment(dl, lx0, top, lx1, top + ledHeight, i < lit, i == peak && i >= lit,
                               duskdaf::ledLadderColors(static_cast<float>(i) / kLeds, style));
            }
            textCentred(dl, cx0 + 0.5f * columnWidth, y1 - 6.0f, 8.0f, IM_COL32(128, 128, 128, 153),
                        ch == 0 ? "L" : "R", false);
        }
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f dB", static_cast<double>(readoutDb));
        textCentred(dl, 0.5f * (x0 + x1), faceY(Layout::meterReadoutY), 13.0f, IM_COL32(0xCC, 0xCC, 0xCC, 255), text,
                    true);
    }

    // The INPUT and OUTPUT meters of both faces: per-channel block peaks from the plugin, the shared LED ladder's
    // ballistics (the original meter's), and the readout's peak-then-fall behaviour.
    void drawLevelMeters(ImDrawList* dl, float dt)
    {
        float in[2] = {-60.0f, -60.0f}, out[2] = {-60.0f, -60.0f};
        void* const instance = getPluginInstancePointer();
        if (instance != nullptr && multiCompGetInputChannelLevel != nullptr && multiCompGetOutputChannelLevel != nullptr)
            for (int ch = 0; ch < 2; ++ch)
            {
                in[ch] = multiCompGetInputChannelLevel(instance, ch);
                out[ch] = multiCompGetOutputChannelLevel(instance, ch);
            }
        inputLadder.update(in[0], in[1], dt);
        outputLadder.update(out[0], out[1], dt);
        multicompp::ui_detail::advanceLevelReadout(inputReadout, std::max(in[0], in[1]), dt);
        multicompp::ui_detail::advanceLevelReadout(outputReadout, std::max(out[0], out[1]), dt);
        drawLevelMeter(dl, 43.0f, inputLadder, "INPUT", inputReadout.shownDb);
        drawLevelMeter(dl, kDesignW - 83.0f, outputLadder, "OUTPUT", outputReadout.shownDb);
    }

    // Silver cap on a serrated black skirt, with the shared gesture layer's exact pointer travel.
    // Auto Makeup dims the complete control, including its printed scale.
    void drawMetallicKnob(ImDrawList* dl, float x, float y, float r, float position, float opacity)
    {
        const float s = panel.scale();
        const auto fade = [opacity](ImU32 colour) {
            return colourWithAlpha(colour, opacity * static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f);
        };
        const ImVec2 c = panel.P(x, y);
        dl->AddCircleFilled(panel.P(x + 2, y + 3), (r + 2) * s, fade(IM_COL32(0, 0, 0, 150)), 64);
        dl->AddCircleFilled(c, (r + 1) * s, fade(IM_COL32(12, 14, 13, 255)), 64);
        const float angle = duskdaf::DuskPanel::knobAngle(std::clamp(position, 0.0f, 1.0f));
        for (int tooth = 0; tooth < 48; ++tooth)
        {
            const float a = angle + 2 * kUiPi * static_cast<float>(tooth) / 48;
            dl->AddLine(panel.P(x + r * 0.85f * std::sin(a), y - r * 0.85f * std::cos(a)),
                        panel.P(x + r * std::sin(a), y - r * std::cos(a)),
                        fade(tooth % 2 ? IM_COL32(83, 87, 83, 255) : IM_COL32(20, 23, 21, 255)), 1.4f * s);
        }
        const float cap = r * 0.81f;
        gradientCircle(dl, c, cap * s, panel.P(x - cap, y - cap), panel.P(x + cap, y + cap),
                       fade(IM_COL32(226, 229, 220, 255)), fade(IM_COL32(127, 133, 126, 255)));
        dl->AddCircle(c, cap * s, fade(IM_COL32(227, 230, 222, 190)), 64, s);
        dl->AddLine(panel.P(x + cap * 0.20f * std::sin(angle), y - cap * 0.20f * std::cos(angle)),
                    panel.P(x + cap * 0.92f * std::sin(angle), y - cap * 0.92f * std::cos(angle)),
                    fade(IM_COL32(29, 33, 29, 255)), 2.0f * s);
        duskdaf::KnobRingStyle ring;
        ring.markInner = 4; ring.markOuter = 8;
        ring.markColor = fade(IM_COL32(193, 199, 189, 220));
        ring.minorColor = fade(IM_COL32(151, 159, 149, 180));
        ring.minorInner = 5; ring.minorOuter = 7;
        for (int i = 0; i < 11; ++i)
            duskdaf::drawKnobRingMark(panel, dl, x, y, r, static_cast<float>(i) / 10, nullptr, ring,
                                     i != 0 && i != 5 && i != 10);
    }

    // A Digital or Multiband control: the metallic knob, its name above and its value below (the original slider's
    // text box: white, bold, the parameter's decimals and suffix). A control Auto Makeup overrides is dimmed and
    // locked, as in the original.
    void modernKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y, float r, const char* name,
                    float nameCentreY, float nameSize, ImU32 nameColour, int decimals, const char* suffix,
                    float valueCentreY, bool applicable = true)
    {
        const float lo = hostMinimum(p), hi = hostMaximum(p);
        const float opacity = applicable ? 1.0f : 0.4f;
        // Drawn before the widget, so a typed-value editor it opens lands on top.
        drawMetallicKnob(dl, x, y, r, hi > lo ? (values[p] - lo) / (hi - lo) : 0.0f, opacity);
        if (!applicable) ImGui::BeginDisabled();
        // The selected band's knobs reuse their captions. Give each parameter
        // its own active ID so a disappearing band cannot redirect a held drag.
        ImGui::PushID(static_cast<int>(p));
        beginTrackedControl(p, id, applicable);
        panel.knob(id, p, lo, hi, x, y, r, values[p], hostDefaultValue(p), false, false, "%.2f", suffix, 0, true,
                   false, nullptr, false, 1.0f, 0.0f, nullptr, false, nullptr, true, 0.0f, 0.0f, false, false, 9.5f,
                   false, false, &knobHostToPlain, &knobPlainToHost, this);
        endTrackedControl();
        ImGui::PopID();
        if (!applicable) ImGui::EndDisabled();
        textCentred(dl, x, nameCentreY, nameSize, colourWithAlpha(nameColour, opacity * static_cast<float>(
                        (nameColour >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f), name, true);
        char text[48];
        std::snprintf(text, sizeof(text), "%.*f%s", decimals,
                      static_cast<double>(knobHostToPlain(values[p], p, this)), suffix);
        textCentred(dl, x, valueCentreY, 13.0f, colourWithAlpha(IM_COL32_WHITE, opacity), text, true);
    }

    // Recessed hardware switch with a separate status lamp, matching the rack's silkscreen.
    void illuminatedToggle(ImDrawList* dl, const char* id, uint32_t p, float x0, float y0, float x1, float y1,
                           const char* label)
    {
        const float s = panel.scale();
        const ImVec2 hit0 = panel.P(x0, y0), hit1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(hit0);
        if (ImGui::InvisibleButton(id, ImVec2(hit1.x - hit0.x, hit1.y - hit0.y)))
            setHostValue(p, values[p] > 0.5f ? 0.0f : 1.0f);
        const bool on = values[p] > 0.5f, down = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(hit0, hit1, IM_COL32(9, 12, 10, 255), 2 * s);
        const ImVec2 i0 = panel.P(x0 + 2, y0 + 2), i1 = panel.P(x1 - 2, y1 - 2);
        const ImU32 topColour = down ? IM_COL32(39, 44, 41, 255) : IM_COL32(76, 83, 77, 255);
        gradientRect(dl, i0, i1, s, i0, ImVec2(i0.x, i1.y), topColour, IM_COL32(31, 36, 32, 255));
        dl->AddRect(hit0, hit1, hovered ? kModernText : IM_COL32(117, 125, 116, 255), 2 * s, 0, s);
        const float cy = 0.5f * (y0 + y1);
        dl->AddCircleFilled(panel.P(x0 + 13, cy), 4 * s, IM_COL32(10, 13, 10, 255), 16);
        dl->AddCircleFilled(panel.P(x0 + 13, cy), 2.6f * s, on ? kDigitalCyan : IM_COL32(60, 77, 67, 255), 16);
        textCentred(dl, 0.5f * (x0 + x1) + 5, cy, 11, kModernText, label, true);
    }

    // The Digital face's gain-reduction VU (AnalogVUMeter::paint) in the rectangle given, needle from digitalVu.
    void drawDigitalVu(ImDrawList* dl, float x0, float y0, float x1, float y1)
    {
        const float s = panel.scale();
        const float sf = std::max(0.5f, std::min((x1 - x0) / 400.0f, (y1 - y0) / 250.0f));
        gradientRect(dl, panel.P(x0, y0), panel.P(x1, y1), 3 * s, panel.P(x0, y0), panel.P(x0, y1),
                     IM_COL32(101, 109, 99, 255), IM_COL32(22, 27, 23, 255));
        const float ix0 = x0 + 6, iy0 = y0 + 6, ix1 = x1 - 6, iy1 = y1 - 6;
        dl->AddRectFilled(panel.P(ix0, iy0), panel.P(ix1, iy1), IM_COL32(0x3A, 0x3A, 0x3A, 255), 2.0f * sf * s);
        const float fx0 = ix0 + 3 * sf, fy0 = iy0 + 3 * sf, fx1 = ix1 - 3 * sf, fy1 = iy1 - 3 * sf;
        gradientRect(dl, panel.P(fx0, fy0), panel.P(fx1, fy1), 2 * sf * s, panel.P(fx0, fy0), panel.P(fx0, fy1),
                     IM_COL32(234, 222, 187, 255), IM_COL32(211, 193, 146, 255));
        dl->PushClipRect(panel.P(fx0, fy0), panel.P(fx1, fy1), true);
        const float cx = 0.5f * (fx0 + fx1), pivotY = fy1 - 3 * sf;
        const float length = std::min((fx1 - fx0) * 0.49f, (fy1 - fy0) * 0.88f);
        constexpr float kStart = -2.7f, kEnd = -0.44f;   // screen angles, the scale's left and right ends
        const auto at = [&](float angle, float radius) {
            return panel.P(cx + radius * std::cos(angle), pivotY + radius * std::sin(angle));
        };
        // The original strokes this arc with JUCE's clockwise-from-up arc convention while every tick, label and the
        // needle use these screen angles, which leaves it on the left of the face; it belongs under the ticks.
        dl->PathClear();
        for (int i = 0; i <= 48; ++i)
            dl->PathLineTo(at(kStart + (kEnd - kStart) * static_cast<float>(i) / 48.0f, length * 0.95f));
        dl->PathStroke(IM_COL32(0x1A, 0x1A, 0x1A, 179), 0, 2.0f * sf * s);
        static constexpr float kMarks[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
        for (const float db : kMarks)
        {
            const float angle = kStart + (db + 20.0f) / 23.0f * (kEnd - kStart);
            const bool major = db != 2.0f;
            const ImU32 ink = db >= 0.0f ? IM_COL32(0xD4, 0x2C, 0x2C, 255) : IM_COL32(0x2A, 0x2A, 0x2A, 255);
            dl->AddLine(at(angle, length * 0.95f), at(angle, length * 0.95f + (major ? 10.0f : 6.0f) * sf), ink,
                        (major ? 2.0f : 1.0f) * sf * s);
            if (db > 0.0f) continue;
            const float radius = length * 0.72f;
            const float labelY = std::max(pivotY + radius * std::sin(angle), fy0 + 5 * sf + 7.5f * sf);
            char text[8];
            std::snprintf(text, sizeof(text), "%d", static_cast<int>(db));
            textCentred(dl, cx + radius * std::cos(angle), labelY, std::max(10.0f, 14.0f * sf), ink, text, false);
        }
        for (const int percent : {0, 100})
        {
            const float angle = kStart + static_cast<float>(percent) / 100.0f * (kEnd - kStart);
            const float radius = length * 1.15f;
            textCentred(dl, cx + radius * std::cos(angle), pivotY + radius * std::sin(angle) + 5 * sf,
                        std::max(7.0f, 9.0f * sf), IM_COL32(0x60, 0x60, 0x60, 255), percent == 0 ? "0%" : "100%", false);
        }
        textCentred(dl, cx, pivotY - length * 0.4f + 10 * sf, std::max(18.0f, 24.0f * sf), IM_COL32(0x2A, 0x2A, 0x2A, 255),
                    "VU", false);
        dl->AddLine(panel.P(cx, pivotY), at(kStart + digitalVu.position * (kEnd - kStart), length * 0.96f),
                    IM_COL32_BLACK, 1.5f * sf * s);
        if (digitalVu.peakPosition < digitalVu.position - 0.02f)
        {
            const ImVec2 mark = at(kStart + digitalVu.peakPosition * (kEnd - kStart), length * 0.92f);
            dl->AddCircleFilled(mark, 2.0f * sf * s, IM_COL32(0xFF, 0x33, 0x33, 255), 16);
            dl->AddLine(panel.P(cx, pivotY), mark, IM_COL32(0xFF, 0x33, 0x33, 0x60), 0.5f * sf * s);
        }
        dl->AddCircleFilled(panel.P(cx, pivotY), 3.0f * sf * s, IM_COL32_BLACK, 16);
        dl->PopClipRect();
        // Glass: a faint highlight over the top fifth.
        const float gx0 = ix0 + sf, gy0 = iy0 + sf, gx1 = ix1 - sf, gy1 = iy1 - sf;
        const ImVec2 h0 = panel.P(gx0 + 10 * sf, gy0 + 5 * sf), h1 = panel.P(gx1 - 10 * sf, gy0 + (gy1 - gy0) * 0.2f - 5 * sf);
        gradientRect(dl, h0, h1, 3.0f * sf * s, h0, ImVec2(h0.x, h1.y), IM_COL32(255, 255, 255, 0x20),
                     IM_COL32(255, 255, 255, 0));
    }

    // The Digital face's gain-reduction history (GRHistoryGraph::paint): the last 128 readings at 30 Hz (about 4 s)
    // as a filled trace over 0..-30 dB, with the deepest reading marked.
    void drawGrHistory(ImDrawList* dl, float x0, float y0, float x1, float y1)
    {
        const float s = panel.scale();
        const float sf = std::max(0.5f, std::min((x1 - x0) / 400.0f, (y1 - y0) / 250.0f));
        dl->AddRectFilled(panel.P(x0, y0), panel.P(x1, y1), IM_COL32(0x60, 0x60, 0x60, 255), 4.0f * sf * s);
        const float ix0 = x0 + 2 * sf, iy0 = y0 + 2 * sf, ix1 = x1 - 2 * sf, iy1 = y1 - 2 * sf;
        gradientRect(dl, panel.P(ix0, iy0), panel.P(ix1, iy1), 3.0f * sf * s, panel.P(ix0, iy0), panel.P(ix0, iy1),
                     IM_COL32(0x1A, 0x1A, 0x1E, 255), IM_COL32(0x10, 0x10, 0x14, 255));
        const float gx0 = ix0 + 38 * sf, gy0 = iy0 + 10 * sf, gx1 = ix1 - 10 * sf, gy1 = iy1 - 24 * sf;
        const auto rowOf = [&](float db) { return gy0 + std::clamp(-db / 30.0f, 0.0f, 1.0f) * (gy1 - gy0); };
        const float labelSize = std::max(8.0f, 9.0f * sf);
        static constexpr float kRows[] = {0, -6, -12, -18, -24, -30};
        for (const float db : kRows)
        {
            const float y = rowOf(db);
            dl->AddRectFilled(panel.P(gx0, y), panel.P(gx1, y + 1),
                              db == 0.0f ? IM_COL32(0x50, 0x50, 0x50, 255) : IM_COL32(0x2A, 0x2A, 0x2E, 255));
            char text[8];
            std::snprintf(text, sizeof(text), "%d", static_cast<int>(db));
            panel.text(dl, ix0 + 26 * sf, y - 0.5f * labelSize, labelSize,
                       db == 0.0f ? IM_COL32(0xAA, 0xAA, 0xAA, 255) : IM_COL32(0x70, 0x70, 0x70, 255), text, 1);
        }
        for (int i = 1; i < 4; ++i)
        {
            const float x = gx0 + (gx1 - gx0) * static_cast<float>(i) / 4.0f;
            dl->AddRectFilled(panel.P(x, gy0), panel.P(x + 1, gy1), IM_COL32(0x2A, 0x2A, 0x2E, 255));
        }
        constexpr int kCount = static_cast<int>(std::tuple_size<decltype(grHistory)>::value);
        ImVec2 trace[kCount];
        float deepest = 0.0f;
        for (int i = 0; i < kCount; ++i)
        {
            const float gr = grHistory[static_cast<size_t>((grHistoryWrite + i) % kCount)];
            deepest = std::min(deepest, gr);
            trace[i] = panel.P(gx0 + (gx1 - gx0) * static_cast<float>(i) / (kCount - 1), rowOf(gr));
        }
        // The fill hangs from the 0 dB line: one trapezoid per step (each convex), shaded as one gradient, without
        // anti-aliased fringes that would seam the joins.
        const ImDrawListFlags flags = dl->Flags;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        const int firstVertex = dl->VtxBuffer.Size;
        const float zeroRow = panel.P(0, gy0).y;
        for (int i = 0; i + 1 < kCount; ++i)
        {
            const ImVec2 step[4] = {ImVec2(trace[i].x, zeroRow), ImVec2(trace[i + 1].x, zeroRow), trace[i + 1], trace[i]};
            dl->AddConvexPolyFilled(step, 4, IM_COL32_WHITE);
        }
        dl->Flags = flags;
        shadeLinear(dl, firstVertex, panel.P(gx0, gy0), panel.P(gx0, gy1), IM_COL32(0x00, 0xCC, 0x77, 230),
                    IM_COL32(0x00, 0x33, 0x22, 179));
        dl->AddPolyline(trace, kCount, IM_COL32(0x00, 0xFF, 0x88, 0x30), 0, 4.0f * sf * s);
        dl->AddPolyline(trace, kCount, IM_COL32(0x00, 0xFF, 0x88, 255), 0, 1.5f * sf * s);
        dl->AddRectFilled(panel.P(gx1, gy0), panel.P(gx1 + 1, gy1), IM_COL32(0xFF, 0xAA, 0x00, 255));
        if (deepest < -0.5f)
        {
            const float y = rowOf(deepest);
            dl->AddRectFilled(panel.P(gx0, y), panel.P(gx1, y + 1), IM_COL32(0xFF, 0x66, 0x66, 0x80));
            char text[16];
            std::snprintf(text, sizeof(text), "%.1fdB", static_cast<double>(deepest));
            panel.text(dl, gx1 - 2 * sf, y - 6 * sf - 0.5f * labelSize, labelSize, IM_COL32(0xFF, 0x66, 0x66, 255), text,
                       1, true);
        }
        // Time legends. The original also prints "~4 sec" over the same corner as "now"; the two overlap there.
        const float legendTop = gy1 + 8 * sf - 0.5f * labelSize;
        constexpr ImU32 kLegend = IM_COL32(0x70, 0x70, 0x70, 255);
        panel.text(dl, gx0, legendTop, labelSize, kLegend, "-4s", -1);
        panel.text(dl, 0.5f * (gx0 + gx1), legendTop, labelSize, kLegend, "-2s", 0);
        panel.text(dl, gx1, legendTop, labelSize, kLegend, "now", 1);
        const float tx0 = gx0 + (gx1 - gx0) * 0.2f, tx1 = gx0 + (gx1 - gx0) * 0.8f, ty0 = gy0 + 4 * sf;
        const float ty1 = ty0 + 18 * sf;
        dl->AddRectFilled(panel.P(tx0, ty0), panel.P(tx1, ty1), IM_COL32(0x1A, 0x1A, 0x1A, 0xDD), 3.0f * s);
        textCentred(dl, 0.5f * (tx0 + tx1), 0.5f * (ty0 + ty1), std::max(11.0f, 14.0f * sf), IM_COL32_WHITE,
                    "GR HISTORY", true);
    }

    // The sidechain high-pass as the original's vertical slider beside the VU (a LookAndFeel_V4 linear slider):
    // click or drag along the track; a double-click returns the default.
    void sidechainHpSlider(ImDrawList* dl, float x, float trackTop, float trackBottom)
    {
        const float s = panel.scale();
        const uint32_t p = P_SC_HP;
        const ImGuiID widget = ImGui::GetID("##dig_sc_hp");
        markTrackedDrawn(p, widget, true);
        const float lo = hostMinimum(p), hi = hostMaximum(p);
        ImGui::SetCursorScreenPos(panel.P(x - 14.0f, trackTop - 8.0f));
        ImGui::InvisibleButton("##dig_sc_hp", ImVec2(28.0f * s, (trackBottom - trackTop + 16.0f) * s));
        if (ImGui::IsItemActivated())
        {
            trackedEditsOpen[p] = true;
            trackedEditOwners[p] = widget;
            trackedEditsDrawn[p] = true;
            editParameter(p, true);
            sidechainHpReset = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (sidechainHpReset) setParam(p, hostDefaultValue(p));
        }
        if (ImGui::IsItemActive() && !sidechainHpReset && hi > lo)
        {
            const float mouseY = (ImGui::GetIO().MousePos.y - panel.P(0, 0).y) / s;
            const float position = std::clamp((trackBottom - mouseY) / (trackBottom - trackTop), 0.0f, 1.0f);
            const float host = lo + position * (hi - lo);
            if (host != values[p]) setParam(p, host);
        }
        if (ImGui::IsItemDeactivated())
        {
            if (trackedEditsOpen[p]) editParameter(p, false);
            trackedEditsOpen[p] = false;
            sidechainHpReset = false;
        }
        const float position = hi > lo ? std::clamp((values[p] - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
        const float thumbY = trackBottom - position * (trackBottom - trackTop);
        dl->AddRectFilled(panel.P(x - 3, trackTop - 3), panel.P(x + 3, trackBottom + 3), IM_COL32(8, 11, 9, 255), s);
        dl->AddLine(panel.P(x + 4, trackTop), panel.P(x + 4, trackBottom), IM_COL32(98, 107, 97, 150), s);
        const ImVec2 t0 = panel.P(x - 10, thumbY - 5), t1 = panel.P(x + 10, thumbY + 5);
        gradientRect(dl, t0, t1, s, t0, ImVec2(t0.x, t1.y), IM_COL32(207, 213, 202, 255), IM_COL32(110, 119, 108, 255));
        dl->AddLine(panel.P(x - 7, thumbY), panel.P(x + 7, thumbY), IM_COL32(32, 39, 32, 255), s);
        textCentred(dl, x, trackTop - 18.0f, 11, kModernText, "SC HP", true);
        const float hz = knobHostToPlain(values[p], p, this);
        char text[16];
        if (hz < 1.0f)
            std::snprintf(text, sizeof(text), "OFF");
        else
            std::snprintf(text, sizeof(text), "%.0f Hz", static_cast<double>(hz));
        textCentred(dl, x, trackBottom + 20.0f, 13.0f, IM_COL32_WHITE, text, true);
    }

    void drawDigital(ImDrawList* dl)
    {
        const float s = panel.scale();
        const float dt = ImGui::GetIO().DeltaTime;
        drawModernBody(dl, "DIGITAL COMPRESSOR", "PRECISION DYNAMICS");
        drawLevelMeters(dl, dt);
        dl->AddLine(panel.P(533, faceY(122)), panel.P(533, faceY(338)), IM_COL32(121, 130, 122, 100), s);

        // Gain reduction: the VU, or its 4-second history; a click on either (or the caption) swaps them.
        const float gr = meter(kMeterMaster);
        multicompp::ui_detail::advanceVuNeedle(digitalVu, gr, dt);
        grHistoryClock = std::min(grHistoryClock + std::max(dt, 0.0f), 0.25f);
        for (; grHistoryClock >= 1.0f / 30.0f; grHistoryClock -= 1.0f / 30.0f)
        {
            grHistory[static_cast<size_t>(grHistoryWrite)] = std::isfinite(gr) ? gr : 0.0f;
            grHistoryWrite = (grHistoryWrite + 1) % static_cast<int>(grHistory.size());
        }
        // Left of centre: the display, its caption and the side-chain high-pass beside it.
        constexpr float vuX0 = 112, vuX1 = 452, vuTop = 122, vuBottom = 280, captionBottom = 302;
        ImGui::SetCursorScreenPos(panel.P(vuX0, faceY(vuTop)));
        if (ImGui::InvisibleButton("##dig_vu", ImVec2((vuX1 - vuX0) * s, (captionBottom - vuTop) * s)))
            digitalShowsHistory = !digitalShowsHistory;
        if (digitalShowsHistory)
            drawGrHistory(dl, vuX0, faceY(vuTop), vuX1, faceY(vuBottom));
        else
            drawDigitalVu(dl, vuX0, faceY(vuTop), vuX1, faceY(vuBottom));
        dl->AddRectFilled(panel.P(vuX0 + 2, faceY(vuBottom + 2)), panel.P(vuX1 - 2, faceY(captionBottom - 2)),
                          IM_COL32(0, 0, 0, 0x30), 3.0f * s);
        const char* caption = digitalShowsHistory ? "HISTORY / CLICK FOR VU" : "VU / CLICK FOR HISTORY";
        constexpr float captionY = 0.5f * (vuBottom + captionBottom);
        textCentred(dl, 0.5f * (vuX0 + vuX1), faceY(captionY), 10, kDim, caption, true);
        duskdaf::spacedText(panel, dl, 0.5f * (vuX0 + vuX1), faceY(318), 11, kModernText, "GAIN REDUCTION", 0, 0.10f);
        sidechainHpSlider(dl, 492.0f, faceY(132.0f), faceY(282.0f));

        // Right of it, the original's two rows of knobs: five columns 94 apart.
        constexpr float r = 27.0f, row1 = 180.0f, row2 = 286.0f;
        const auto column = [](int i) { return 770.0f + 94.0f * static_cast<float>(i - 2); };
        const auto knobAt = [&](const char* id, uint32_t p, int col, float row, const char* name, int decimals,
                                const char* suffix, bool applicable) {
            modernKnob(dl, id, p, column(col), faceY(row), r, name, faceY(row - 44.0f), 11.0f, kModernText, decimals,
                       suffix, faceY(row + 39.0f), applicable);
        };
        knobAt("dig_thr", P_DIG_THRESHOLD, 0, row1, "THRESHOLD", 1, " dB", true);
        knobAt("dig_ratio", P_DIG_RATIO, 1, row1, "RATIO", 1, ":1", true);
        knobAt("dig_knee", P_DIG_KNEE, 2, row1, "KNEE", 1, " dB", true);
        knobAt("dig_att", P_DIG_ATTACK, 3, row1, "ATTACK", 2, " ms", true);
        knobAt("dig_rel", P_DIG_RELEASE, 4, row1, "RELEASE", 0, " ms", true);
        knobAt("dig_look", P_DIG_LOOK, 0, row2, "LOOKAHEAD", 1, " ms", true);
        knobAt("dig_mix", P_DIG_MIX, 1, row2, "MIX", 0, " %", true);
        knobAt("dig_out", P_DIG_OUT, 2, row2, "OUTPUT", 1, " dB", modernManualOutputAvailable());
        illuminatedToggle(dl, "##dig_adapt", P_DIG_ADAPT, column(3) - 40.0f, faceY(row2 - 15.0f), column(4) + 40.0f,
                          faceY(row2 + 15.0f), "ADAPTIVE RELEASE");
    }

    // Compact rack switches. A colored underline identifies an active band / bypass / solo;
    // neutral caps and printed legends keep color from overwhelming the faceplate.
    bool bandButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1, const char* label,
                    ImU32 fill, ImU32 textColour, const char* tooltip, bool active, bool enabled = true)
    {
        const float s = panel.scale();
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        if (!enabled) ImGui::BeginDisabled();
        ImGui::SetCursorScreenPos(b0);
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool down = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
        if (tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tooltip);
        if (!enabled) ImGui::EndDisabled();
        const ImVec2 f0 = panel.P(x0 + 0.5f, y0 + 0.5f), f1 = panel.P(x1 - 0.5f, y1 - 0.5f);
        const ImU32 cap = down ? IM_COL32(34, 40, 35, 255) : hovered ? IM_COL32(88, 96, 86, 255) : IM_COL32(65, 73, 65, 255);
        gradientRect(dl, f0, f1, 1.5f * s, f0, ImVec2(f0.x, f1.y), cap, IM_COL32(26, 32, 27, 255));
        dl->AddRect(f0, f1, active ? fill : IM_COL32(102, 113, 101, 190), 1.5f * s, 0, s);
        if (active)
            dl->AddRectFilled(panel.P(x0 + 3, y1 - 3), panel.P(x1 - 3, y1 - 1), fill);
        const ImU32 ink = active ? kModernText : textColour;
        textCentred(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1) - 1, std::min(11.0f, (y1 - y0) * 0.55f),
                    enabled ? ink : colourWithAlpha(ink, 0.55f), label, true);
        return clicked && enabled;
    }

    // One band's gain-reduction meter (MultibandCompressorPanel::drawBandMeter): twenty LEDs lit from the top over
    // 0..-20 dB, warming from the band's colour to red, a white peak, a value pill, the border bright when selected.
    void drawBandMeter(ImDrawList* dl, int band, float x0, float y0, float x1, float y1, bool selected)
    {
        const float s = panel.scale();
        const ImU32 colour = kBandColors[band];
        const auto& m = bandGrMeters[static_cast<size_t>(band)];
        if (selected)
            dl->AddRectFilled(panel.P(x0 - 4, y0 - 4), panel.P(x1 + 4, y1 + 4), colourWithAlpha(colour, 0.2f), 6.0f * s);
        const ImVec2 a = panel.P(x0, y0), b = panel.P(x1, y1);
        gradientRect(dl, a, b, 5.0f * s, a, ImVec2(a.x, b.y), IM_COL32(0x0C, 0x0C, 0x0C, 255),
                     IM_COL32(0x18, 0x18, 0x18, 255));
        dl->AddRect(panel.P(x0 + 1, y0 + 1), panel.P(x1 - 1, y1 - 1), IM_COL32(0, 0, 0, 128), 4.0f * s, 0, 1.0f * s);
        static constexpr int kSegments = 20;   // static: read uncaptured by segmentsFor (MSVC C3493)
        constexpr float kGap = 1.5f;
        constexpr ImU32 kYellow = IM_COL32(0xFF, 0xDD, 0x00, 255), kOrange = IM_COL32(0xFF, 0x88, 0x00, 255);
        const float mx0 = x0 + 4, mx1 = x1 - 4, my0 = y0 + 4, my1 = y1 - 4;
        const float segmentHeight = ((my1 - my0) - (kSegments - 1) * kGap) / kSegments;
        const auto segmentsFor = [](float db) {
            return static_cast<int>(std::clamp(-db / 20.0f, 0.0f, 1.0f) * static_cast<float>(kSegments));
        };
        const int lit = segmentsFor(m.shownDb), peak = segmentsFor(m.peakDb);
        for (int seg = 0; seg < kSegments; ++seg)
        {
            const float top = my0 + static_cast<float>(seg) * (segmentHeight + kGap);
            const ImVec2 s0 = panel.P(mx0, top), s1 = panel.P(mx1, top + segmentHeight);
            if (seg < lit)
            {
                const float intensity = static_cast<float>(seg) / kSegments;
                const ImU32 c = intensity < 0.4f ? colourBrighter(colour, 0.2f)
                    : intensity < 0.6f ? colourMix(colour, kYellow, (intensity - 0.4f) * 5.0f)
                    : intensity < 0.8f ? colourMix(kYellow, kOrange, (intensity - 0.6f) * 5.0f)
                    : colourMix(kOrange, IM_COL32(0xFF, 0x33, 0x33, 255), (intensity - 0.8f) * 5.0f);
                gradientRect(dl, s0, s1, 2.0f * s, s0, ImVec2(s0.x, s1.y), colourBrighter(c, 0.4f), colourDarker(c, 0.1f));
                dl->AddRectFilled(panel.P(mx0 + 2, top + 1), panel.P(mx1 - 2, top + 2.5f),
                                  colourWithAlpha(colourBrighter(c, 0.7f), 0.4f));
            }
            else if (seg == peak && peak > 0)
            {
                dl->AddRectFilled(panel.P(mx0 - 1, top - 1), panel.P(mx1 + 1, top + segmentHeight + 1),
                                  IM_COL32(255, 255, 255, 77), 3.0f * s);
                dl->AddRectFilled(s0, s1, IM_COL32_WHITE, 2.0f * s);
            }
            else
            {
                dl->AddRectFilled(s0, s1, IM_COL32(0x24, 0x24, 0x29, 255), 2.0f * s);
                dl->AddRectFilled(panel.P(mx0 + 2, top + 1), panel.P(mx1 - 2, top + 2), IM_COL32(0x30, 0x30, 0x36, 255));
            }
        }
        dl->AddRect(a, b, colourWithAlpha(colour, selected ? 1.0f : 0.5f), 5.0f * s, 0, (selected ? 2.5f : 1.5f) * s);
        const float px0 = x0 + 6, px1 = x1 - 6, py0 = y1 - 24, py1 = y1 - 4;
        const ImVec2 p0 = panel.P(px0, py0), p1 = panel.P(px1, py1);
        gradientRect(dl, p0, p1, 4.0f * s, p0, ImVec2(p0.x, p1.y), IM_COL32(0x0A, 0x0A, 0x0A, 255),
                     IM_COL32(0x1A, 0x1A, 0x1A, 255));
        dl->AddRect(p0, p1, colourWithAlpha(colour, 0.5f), 4.0f * s, 0, 1.0f * s);
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f dB", static_cast<double>(m.shownDb));
        const float cx = 0.5f * (px0 + px1), cy = 0.5f * (py0 + py1);
        textCentred(dl, cx + 1, cy + 1, 12.5f, IM_COL32(0, 0, 0, 128), text, true);
        textCentred(dl, cx, cy, 12.5f, IM_COL32_WHITE, text, true);
    }

    // A crossover handle between two band meters (CrossoverFader): a track in the two bands' colours and a grip on
    // a logarithmic frequency axis. Drag up to raise the frequency (the whole travel spans the control's range);
    // a double-click returns the default.
    void crossoverFader(ImDrawList* dl, uint32_t p, float centreX, float top, float bottom, ImU32 lowColour,
                        ImU32 highColour)
    {
        const float s = panel.scale();
        const float minHz = plainValueForHost(p, hostMinimum(p)), maxHz = plainValueForHost(p, hostMaximum(p));
        const float trackTop = top, trackBottom = bottom - 22.0f;
        ImGui::PushID(static_cast<int>(p));
        const ImGuiID widget = ImGui::GetID("##crossover");
        markTrackedDrawn(p, widget, true);
        ImGui::SetCursorScreenPos(panel.P(centreX - 35.0f, top));
        ImGui::InvisibleButton("##crossover", ImVec2(70.0f * s, (bottom - top) * s));
        ImGui::PopID();
        if (ImGui::IsItemActivated())
        {
            trackedEditsOpen[p] = true;
            trackedEditOwners[p] = widget;
            trackedEditsDrawn[p] = true;
            editParameter(p, true);
            draggedCrossover = p;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) setParam(p, hostDefaultValue(p));
            crossoverDragPosition = multicompp::ui_detail::crossoverFaderPosition(
                plainValueForHost(p, values[p]), minHz, maxHz);
        }
        const float deltaY = ImGui::GetIO().MouseDelta.y / s;
        if (ImGui::IsItemActive() && deltaY != 0.0f)
        {
            // CrossoverFader::mouseDrag: the travel's height in pixels spans the whole logarithmic range.
            crossoverDragPosition = std::clamp(crossoverDragPosition - deltaY / (trackBottom - (trackTop + 4.0f)),
                                               0.0f, 1.0f);
            setParam(p, hostValueForPlain(p, multicompp::ui_detail::crossoverFaderFrequency(crossoverDragPosition,
                                                                                            minHz, maxHz)));
        }
        if (ImGui::IsItemDeactivated())
        {
            if (trackedEditsOpen[p]) editParameter(p, false);
            trackedEditsOpen[p] = false;
            // Released: the pending write has had the gesture to land, and the
            // next refresh adopts whatever ordering the DSP settled on.
            if (draggedCrossover == p) draggedCrossover = kNoCrossover;
        }
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        dl->AddRectFilled(panel.P(centreX - 6, trackTop), panel.P(centreX + 6, trackBottom),
                          IM_COL32(0x1A, 0x1A, 0x1A, 255), 4.0f * s);
        const ImVec2 g0 = panel.P(centreX - 4, trackTop + 2), g1 = panel.P(centreX + 4, trackBottom - 2);
        gradientRect(dl, g0, g1, 3.0f * s, g0, ImVec2(g0.x, g1.y), colourWithAlpha(highColour, 0.3f),
                     colourWithAlpha(lowColour, 0.3f));
        const float hz = plainValueForHost(p, values[p]);
        // The grip's travel is inset by half its height, so it stays inside the fader at either end.
        const float travelTop = trackTop + 9.0f, travelBottom = trackBottom - 9.0f;
        const float gripY = travelBottom - multicompp::ui_detail::crossoverFaderPosition(hz, minHz, maxHz)
                                               * (travelBottom - travelTop);
        const float gx0 = centreX - 13.0f, gy0 = gripY - 9.0f;
        dl->AddRectFilled(panel.P(gx0 + 1, gy0 + 2), panel.P(gx0 + 27, gy0 + 20), IM_COL32(0, 0, 0, 102), 4.0f * s);
        const ImU32 grip = hot ? kDigitalCyan : IM_COL32(0x40, 0x40, 0x50, 255);
        const ImVec2 t0 = panel.P(gx0, gy0), t1 = panel.P(gx0 + 26, gy0 + 18);
        gradientRect(dl, t0, t1, 4.0f * s, t0, ImVec2(t0.x, t1.y), colourBrighter(grip, 0.3f), colourDarker(grip, 0.2f));
        for (int i = -1; i <= 1; ++i)
        {
            const float y = gripY + 4.0f * static_cast<float>(i);
            dl->AddRectFilled(panel.P(gx0 + 6, y), panel.P(gx0 + 20, y + 1), IM_COL32(255, 255, 255, 128));
        }
        dl->AddRect(t0, t1, hot ? kDigitalCyan : IM_COL32(255, 255, 255, 77), 4.0f * s, 0, 1.5f * s);
        if (hot)
        {
            const ImU32 arrow = colourWithAlpha(kDigitalCyan, 0.8f);
            dl->AddTriangleFilled(panel.P(centreX, trackTop - 2), panel.P(centreX + 5, trackTop + 5),
                                  panel.P(centreX - 5, trackTop + 5), arrow);
            dl->AddTriangleFilled(panel.P(centreX, trackBottom + 2), panel.P(centreX - 5, trackBottom - 5),
                                  panel.P(centreX + 5, trackBottom - 5), arrow);
        }
        char text[24];
        if (hz >= 1000.0f)
            std::snprintf(text, sizeof(text), "%.1f kHz", static_cast<double>(hz / 1000.0f));
        else
            std::snprintf(text, sizeof(text), "%d Hz", static_cast<int>(hz));
        textCentred(dl, centreX, bottom - 10.0f, 12.0f, hot ? kDigitalCyan : IM_COL32(255, 255, 255, 230), text, true);
    }

    void drawMultiband(ImDrawList* dl)
    {
        const float s = panel.scale();
        const float dt = ImGui::GetIO().DeltaTime;
        drawModernBody(dl, "MULTIBAND COMPRESSOR", "FOUR-BAND DYNAMICS");
        drawLevelMeters(dl, dt);
        dl->AddLine(panel.P(620, faceY(122)), panel.P(620, faceY(338)), IM_COL32(121, 130, 122, 100), s);

        bool enabled[4] = {};
        int enabledCount = 0;
        for (int b = 0; b < 4; ++b)
        {
            enabled[b] = values[static_cast<size_t>(multicompp::kBandBase + b * 8 + 7)] > 0.5f;
            enabledCount += enabled[b] ? 1 : 0;
            multicompp::ui_detail::advanceBandGrMeter(bandGrMeters[static_cast<size_t>(b)],
                                                     meter(kMeterBand0 + static_cast<uint32_t>(b)), dt);
        }
        const unsigned shown = multicompp::ui_detail::multibandShownBands(enabled);
        int slots[4] = {};
        int slotCount = 0;
        for (int b = 0; b < 4; ++b)
            if ((shown & (1u << b)) != 0) slots[slotCount++] = b;
        if ((shown & (1u << selectedBand)) == 0) selectedBand = slots[0];

        // Left, the bands. Enable strip: all four bands at fixed quarters, so a hidden band stays reachable; the last
        // two enabled bands are locked on.
        constexpr float sectionLeft = 144.0f, sectionRight = 600.0f;
        constexpr float stripWidth = (sectionRight - sectionLeft) / 4.0f;
        for (int b = 0; b < 4; ++b)
        {
            const uint32_t enable = static_cast<uint32_t>(multicompp::kBandBase + b * 8 + 7);
            const bool locked = enabled[b] && enabledCount <= 2;
            char tooltip[64];
            if (locked)
                std::snprintf(tooltip, sizeof(tooltip), "Multiband requires at least 2 enabled bands");
            else
                std::snprintf(tooltip, sizeof(tooltip), "Enable / disable %s band", bandName(b));
            ImGui::PushID(b);
            const float x0 = sectionLeft + static_cast<float>(b) * stripWidth;
            if (bandButton(dl, "##mb_enable", x0 + 2.5f, faceY(116.0f), x0 + stripWidth - 2.5f, faceY(136.0f),
                               bandName(b), enabled[b] ? kBandColors[b] : IM_COL32(0x2A, 0x2A, 0x2A, 255),
                               kModernText, tooltip, enabled[b], !locked))
            {
                // Clear Solo first, while its control is still visible. Host
                // automation/restored disabled Solo is also ignored by the DSP.
                const uint32_t solo = enable - 1;
                if (enabled[b] && values[solo] > 0.5f) setHostValue(solo, 0.0f);
                setHostValue(enable, enabled[b] ? 0.0f : 1.0f);
            }
            ImGui::PopID();
        }
        textCentred(dl, 0.5f * (sectionLeft + sectionRight), faceY(342.0f), 10.0f, kDim,
                    "GAIN REDUCTION", true);

        // dB scale beside the meters.
        constexpr float meterTop = 170.0f, meterBottom = 298.0f, faderBottom = 326.0f;
        for (const float db : {0.0f, -3.0f, -6.0f, -10.0f, -15.0f, -20.0f})
        {
            const float y = meterTop + (-db / 20.0f) * (meterBottom - meterTop);
            dl->AddRectFilled(panel.P(sectionLeft - 11, faceY(y)), panel.P(sectionLeft - 5, faceY(y) + 1), IM_COL32(255, 255, 255, 128));
            char text[8];
            std::snprintf(text, sizeof(text), "%d", static_cast<int>(db));
            panel.text(dl, sectionLeft - 13, faceY(y) - 5.0f, 10.0f, IM_COL32(255, 255, 255, 179), text, 1);
        }

        // Band slots: only the bands shown share the width.
        const float slotWidth = (sectionRight - sectionLeft) / static_cast<float>(slotCount);
        for (int slot = 0; slot < slotCount; ++slot)
        {
            const int band = slots[slot];
            const float sx = sectionLeft + static_cast<float>(slot) * slotWidth;
            dl->AddRectFilled(panel.P(sx + 1, faceY(140.0f)), panel.P(sx + slotWidth - 1, faceY(faderBottom)),
                              colourWithAlpha(kBandColors[band], band == selectedBand ? 0.06f : 0.02f));
        }
        for (int slot = 0; slot < slotCount; ++slot)
        {
            const int band = slots[slot];
            const uint32_t base = static_cast<uint32_t>(multicompp::kBandBase + band * 8);
            const float sx = sectionLeft + static_cast<float>(slot) * slotWidth;
            const float mx0 = sx + 8.0f, mx1 = sx + slotWidth - 8.0f;
            constexpr float buttonTop = 142.0f, buttonBottom = 162.0f, smallButton = 21.0f, gap = 3.0f;
            const float tabRight = mx1 - 2.0f * (smallButton + gap);
            ImGui::PushID(band);
            if (bandButton(dl, "##mb_tab", mx0, faceY(buttonTop), tabRight, faceY(buttonBottom), bandName(band),
                               band == selectedBand ? kBandColors[band] : colourWithAlpha(kBandColors[band], 0.3f),
                               kModernText, nullptr, band == selectedBand))
                selectedBand = band;
            char tooltip[48];
            const bool bypassed = values[base + 5] > 0.5f, soloed = values[base + 6] > 0.5f;
            std::snprintf(tooltip, sizeof(tooltip), "Bypass %s band", bandName(band));
            if (bandButton(dl, "##mb_bypass", tabRight + gap, faceY(buttonTop), tabRight + gap + smallButton,
                               faceY(buttonBottom), "B",
                               bypassed ? IM_COL32(0xFF, 0x8C, 0x42, 255) : IM_COL32(0x3A, 0x3A, 0x3A, 255),
                               bypassed ? IM_COL32_BLACK : IM_COL32(0xAA, 0xAA, 0xAA, 255), tooltip, bypassed))
                setHostValue(base + 5, bypassed ? 0.0f : 1.0f);
            std::snprintf(tooltip, sizeof(tooltip), "Solo %s band", bandName(band));
            if (bandButton(dl, "##mb_solo", mx1 - smallButton, faceY(buttonTop), mx1, faceY(buttonBottom), "S",
                               soloed ? IM_COL32(0xFF, 0xCC, 0x00, 255) : IM_COL32(0x3A, 0x3A, 0x3A, 255),
                               soloed ? IM_COL32_BLACK : IM_COL32(0xAA, 0xAA, 0xAA, 255), tooltip, soloed))
                setHostValue(base + 6, soloed ? 0.0f : 1.0f);
            ImGui::PopID();
            drawBandMeter(dl, band, mx0, faceY(meterTop), mx1, faceY(meterBottom), band == selectedBand);
        }
        // A crossover handle on each boundary between neighbouring shown bands: the crossover below the upper band.
        for (int slot = 0; slot + 1 < slotCount; ++slot)
        {
            const int boundary = slots[slot + 1] - 1;
            crossoverFader(dl, P_X1 + static_cast<uint32_t>(boundary),
                           sectionLeft + static_cast<float>(slot + 1) * slotWidth, faceY(meterTop), faceY(faderBottom),
                           kBandColors[boundary], kBandColors[boundary + 1]);
        }

        // Right, the selected band: its tab over its knobs.
        const ImU32 bandColour = kBandColors[selectedBand];
        constexpr float knobsCentre = 812.0f;
        constexpr float tagLeft = 652, tagRight = 972, tagTop = 116, tagBottom = 136;
        dl->AddLine(panel.P(tagLeft, faceY(tagBottom)), panel.P(tagRight, faceY(tagBottom)),
                    colourWithAlpha(bandColour, 0.6f), s);
        char tag[24];
        std::snprintf(tag, sizeof(tag), "%s BAND", bandName(selectedBand));
        textCentred(dl, knobsCentre, faceY(0.5f * (tagTop + tagBottom)), 12.0f, bandColour,
                    tag, true);

        // Seven knobs: the selected band's four timing and level controls, then its makeup with the multiband output
        // and mix, arranged in two rows with the same controls as Digital.
        const uint32_t base = static_cast<uint32_t>(multicompp::kBandBase + selectedBand * 8);
        constexpr float knobR = 25.0f, row1 = 194.0f, row2 = 294.0f;
        const auto knobAt = [&](const char* id, uint32_t p, float column, float row, const char* name, int decimals,
                                const char* suffix, bool applicable) {
            modernKnob(dl, id, p, knobsCentre + 92.0f * column, faceY(row), knobR, name, faceY(row - 40.0f), 11.0f,
                       kModernText, decimals, suffix, faceY(row + 35.0f), applicable);
        };
        knobAt("mb_thr", base, -1.5f, row1, "THRESHOLD", 1, " dB", true);
        knobAt("mb_ratio", base + 1, -0.5f, row1, "RATIO", 1, ":1", true);
        knobAt("mb_att", base + 2, 0.5f, row1, "ATTACK", 1, " ms", true);
        knobAt("mb_rel", base + 3, 1.5f, row1, "RELEASE", 0, " ms", true);
        knobAt("mb_makeup", base + 4, -1.0f, row2, "MAKE-UP", 1, " dB", true);
        knobAt("mb_out", P_MB_OUT, 0.0f, row2, "OUTPUT", 1, " dB", modernManualOutputAvailable());
        knobAt("mb_mix", P_MB_MIX, 1.0f, row2, "MIX", 0, "%", true);
    }

    // Clears the Digital and Multiband faces' meters when one of them comes into view, so nothing replays from the
    // last time it was shown.
    void resetModernFaces()
    {
        inputLadder.reset();
        outputLadder.reset();
        inputReadout = {};
        outputReadout = {};
        digitalVu = {};
        grHistory.fill(0.0f);
        grHistoryWrite = 0;
        grHistoryClock = 0.0f;
        bandGrMeters = {};
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

    // The Opto and FET needles' VU movement (MultiCompGainReductionNeedle.hpp).
    // A UI without the plugin instance (split LV2) draws the raw reading instead.
    float needleGainReduction() const
    {
        void* const instance = getPluginInstancePointer();
        return instance != nullptr && multiCompGetGainReductionNeedle != nullptr
            ? multiCompGetGainReductionNeedle(instance) : meter(kMeterMaster);
    }

    // The needles live in the plugin; a UI without the instance (split LV2) has none.
    bool hasNeedles() const
    {
        return getPluginInstancePointer() != nullptr && multiCompGetVcaGainReductionNeedle != nullptr
            && multiCompGetGainReductionNeedle != nullptr;
    }

    // The VCA face's gain-change needle (kVcaNeedle). Without the instance the raw
    // reading still goes through the dial's law, so the face reads the same scale.
    float vcaNeedleGainReduction() const
    {
        void* const instance = getPluginInstancePointer();
        return instance != nullptr && multiCompGetVcaGainReductionNeedle != nullptr
            ? multiCompGetVcaGainReductionNeedle(instance)
            : static_cast<float>(multicompp::dialReading(multicompp::kVcaNeedle, meter(kMeterMaster)));
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

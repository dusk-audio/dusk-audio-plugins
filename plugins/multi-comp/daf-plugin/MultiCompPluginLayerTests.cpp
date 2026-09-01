#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"
#include "DafPluginInfo.h"

#ifndef DAF_PLUGIN_EXTRA_IO
#error "Multi-Comp must advertise its AU mono input/output layout"
#endif

#define MULTICOMP_UI_LOGIC_TEST
#include "MultiCompUI.cpp"
#undef MULTICOMP_UI_LOGIC_TEST

#define MULTICOMP_PLUGIN_LOGIC_TEST
#include "MultiCompPlugin.cpp"
#undef MULTICOMP_PLUGIN_LOGIC_TEST

#define DUSK_IMGUI_WIDGETS_LOGIC_TEST
#include "../../shared-daf/ui/DuskImGuiWidgets.hpp"
#undef DUSK_IMGUI_WIDGETS_LOGIC_TEST

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using duskaudio::MultiCompDSP;

namespace
{
// DAF prepends { DAF_PLUGIN_NUM_INPUTS, DAF_PLUGIN_NUM_OUTPUTS } to this table,
// so the entries here are the two total-port layouts narrower than the full
// sidechain one. The AU wrapper projects the full layout onto its main bus and
// deduplicates the resulting { 2, 2 } capability.
constexpr uint16_t kAdvertisedExtraIo[][2] = {DAF_PLUGIN_EXTRA_IO};
static_assert(sizeof(kAdvertisedExtraIo) / sizeof(kAdvertisedExtraIo[0]) == 2
              && kAdvertisedExtraIo[0][0] == 2 && kAdvertisedExtraIo[0][1] == 2
              && kAdvertisedExtraIo[1][0] == 1 && kAdvertisedExtraIo[1][1] == 1,
              "Multi-Comp AU extra I/O must advertise matched stereo then mono");
static_assert(DAF_PLUGIN_NUM_INPUTS == 4 && DAF_PLUGIN_NUM_OUTPUTS == 2,
              "the sidechain gate keys off an input count of 4; update it with the port count");

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int reviewFailureCount = 0;
void reviewCheck(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++reviewFailureCount; }
}

void testHostParameterTapers()
{
    const auto& ratio = multicompp::kParams[static_cast<size_t>(multicompp::ParamId::VcaRatio)];
    const auto& attack = multicompp::kParams[static_cast<size_t>(multicompp::ParamId::DigitalAttack)];
    const float positions[] = {0.25f, 0.5f, 0.75f};
    const float expectedRatio[] = {2.2f, 12.8f, 46.6f};
    const float expectedAttack[] = {4.93f, 49.62f, 191.66f};
    require(multicompp::hostMin(ratio) == 0.0f && multicompp::hostMax(ratio) == 1.0f,
            "tapered VCA Ratio is declared in normalized host space");
    require(multicompp::hostMin(attack) == 0.0f && multicompp::hostMax(attack) == 1.0f,
            "tapered Digital Attack is declared in normalized host space");
    for (size_t i = 0; i < 3; ++i)
    {
        require(std::abs(multicompp::hostToPlain(ratio, positions[i]) - expectedRatio[i]) < 0.011f,
                "VCA Ratio follows JUCE skew at quarter points");
        require(std::abs(multicompp::hostToPlain(attack, positions[i]) - expectedAttack[i]) < 0.011f,
                "Digital Attack follows JUCE skew at quarter points");
    }
    std::puts("host tapers: VCA Ratio and Digital Attack match JUCE at 0.25/0.5/0.75");
}

void testParameterIntervals()
{
    const multicompp::Param linear = {
        "linear_interval", "Linear Interval", "", 10.0f, 20.0f, 10.0f,
        MultiCompDSP::Parameter::Mode, false, 2.0f, 1.0f};
    require(multicompp::hostToPlain(linear, 13.1f) == 14.0f,
            "linear host-to-plain mapping applies the descriptor interval");
    require(multicompp::plainToHost(linear, 13.1f) == 14.0f,
            "linear plain-to-host mapping applies the descriptor interval");

    const multicompp::Param skewed = {
        "skewed_interval", "Skewed Interval", "", 1.0f, 121.0f, 1.0f,
        MultiCompDSP::Parameter::Mode, false, 5.0f, 0.5f};
    const float expectedHost = std::pow((16.0f - skewed.min) / (skewed.max - skewed.min),
                                        skewed.skew);
    require(std::abs(multicompp::plainToHost(skewed, 14.0f) - expectedHost) < 1.0e-7f,
            "skewed plain-to-host mapping snaps before applying the unchanged taper");
    require(multicompp::hostToPlain(skewed, expectedHost) == 16.0f,
            "skewed host-to-plain mapping snaps after applying the unchanged taper");
    require(std::abs(multicompp::plainToHost(
                         skewed, multicompp::hostToPlain(skewed, expectedHost)) - expectedHost)
                <= 1.5e-8f,
            "symmetric interval snapping preserves the skewed taper round trip");
    std::puts("parameter intervals: applied symmetrically for linear and skewed mappings");
}

void testStrictStateValidationAndRoundTrip()
{
    multicompp::StateValues saved{};
    for (int i = 0; i < multicompp::kMeterMaster; ++i)
    {
        saved[static_cast<size_t>(i)] = multicompp::resolveParameter(i,
            [i](const multicompp::Param& d) {
                return d.integer ? (d.def == d.min ? d.max : d.min)
                    : multicompp::hostMin(d) + (multicompp::hostMax(d) - multicompp::hostMin(d))
                        * (0.1f + 0.8f * static_cast<float>((i * 37) % 101) / 100.0f);
            },
            [i](const multicompp::BandParam& d, int) {
                return d.integer ? (d.def == d.min ? d.max : d.min)
                    : multicompp::hostMin(d) + (multicompp::hostMax(d) - multicompp::hostMin(d))
                        * (0.1f + 0.8f * static_cast<float>((i * 29) % 101) / 100.0f);
            });
    }

    const std::string valid = multicompp::encodeState(saved);
    multicompp::StateValues restored{};
    require(multicompp::decodeState(valid, restored), "complete state accepted");
    require(std::memcmp(saved.data(), restored.data(), sizeof(saved)) == 0,
            "state save/restore is bit-identical for every parameter");

    auto rejectedWithoutMutation = [&](std::string invalid, const char* message) {
        multicompp::StateValues before{};
        before.fill(0.1234567f);
        const auto unchanged = before;
        require(!multicompp::decodeState(invalid, before), message);
        require(std::memcmp(before.data(), unchanged.data(), sizeof(before)) == 0,
                "rejected state changes zero parameters");
    };

    auto version2Values = saved;
    for (int i = 0; i < multicompp::kMeterMaster; ++i)
        version2Values[static_cast<size_t>(i)] = multicompp::resolveParameter(i,
            [&](const multicompp::Param& d) {
                return multicompp::hostToPlain(d, version2Values[static_cast<size_t>(i)]);
            },
            [&](const multicompp::BandParam& d, int) {
                return multicompp::hostToPlain(d, version2Values[static_cast<size_t>(i)]);
            });
    std::string version2 = multicompp::encodeState(version2Values);
    version2.replace(0, 3, "v=2");
    version2 += ";envelope_curve=0;saturation_mode=0";
    rejectedWithoutMutation(version2, "complete version-2 state rejected");

    auto fractionalInteger = saved;
    fractionalInteger[static_cast<size_t>(multicompp::ParamId::Mode)] = 0.6f;
    rejectedWithoutMutation(multicompp::encodeState(fractionalInteger),
                            "fractional integer state rejected");

    std::string malformed = valid;
    const size_t ratio = malformed.find(";digital_ratio=");
    const size_t ratioEnd = malformed.find(';', ratio + 1);
    malformed.replace(ratio + std::strlen(";digital_ratio="),
                      ratioEnd - ratio - std::strlen(";digital_ratio="), "garbage");
    rejectedWithoutMutation(malformed, "malformed state rejected");

    std::string truncated = valid;
    truncated.erase(truncated.rfind(';'));
    rejectedWithoutMutation(truncated, "truncated state rejected");

    const size_t mix = valid.find(";mix=");
    const size_t mixEnd = valid.find(';', mix + 1);
    rejectedWithoutMutation(valid + valid.substr(mix, mixEnd - mix), "duplicate state key rejected");
    rejectedWithoutMutation(valid + ";unknown=0", "unknown state key rejected");
    std::puts("state validation: v2/fractional integer/malformed/truncated/duplicate/unknown rejected atomically; round-trip exact");
}

void testFactoryPresetOwnership()
{
    using ParamId = multicompp::ParamId;
    struct PresetOwnership
    {
        std::array<ParamId, 13> parameters;
        size_t parameterCount;
        bool ownsBandParameters;
    };
    const std::array<PresetOwnership, 8> expectedOwnership = {{
        {{ParamId::Mode, ParamId::Mix, ParamId::SidechainHP, ParamId::AutoMakeup,
          ParamId::OptoPeakReduction, ParamId::OptoGain, ParamId::OptoLimit}, 7, false},
        {{ParamId::Mode, ParamId::Mix, ParamId::SidechainHP, ParamId::AutoMakeup,
          ParamId::FetInput, ParamId::FetOutput, ParamId::FetAttack, ParamId::FetRelease,
          ParamId::FetRatio, ParamId::FetCurve, ParamId::FetTransient, ParamId::FetThreshold},
         12, false},
        {{ParamId::Mode, ParamId::Mix, ParamId::SidechainHP, ParamId::AutoMakeup,
          ParamId::VcaThreshold, ParamId::VcaRatio, ParamId::VcaAttack, ParamId::VcaRelease,
          ParamId::VcaOutput, ParamId::VcaOverEasy, ParamId::VcaClassicDetector}, 11, false},
        {{ParamId::Mode, ParamId::Mix, ParamId::SidechainHP, ParamId::AutoMakeup,
          ParamId::BusThreshold, ParamId::BusRatio, ParamId::BusAttack, ParamId::BusRelease,
          ParamId::BusMakeup, ParamId::BusMix}, 10, false},
        {{ParamId::Mode, ParamId::Mix, ParamId::SidechainHP, ParamId::AutoMakeup,
          ParamId::FetInput, ParamId::FetOutput, ParamId::FetAttack, ParamId::FetRelease,
          ParamId::FetRatio, ParamId::FetCurve, ParamId::FetTransient, ParamId::FetThreshold},
         12, false},
        {{ParamId::Mode, ParamId::Mix, ParamId::SidechainHP, ParamId::AutoMakeup,
          ParamId::StudioVcaThreshold, ParamId::StudioVcaRatio, ParamId::StudioVcaAttack,
          ParamId::StudioVcaRelease, ParamId::StudioVcaOutput}, 9, false},
        {{ParamId::Mode, ParamId::Mix, ParamId::SidechainHP, ParamId::AutoMakeup,
          ParamId::DigitalThreshold, ParamId::DigitalRatio, ParamId::DigitalKnee,
          ParamId::DigitalAttack, ParamId::DigitalRelease, ParamId::DigitalLookahead,
          ParamId::DigitalMix, ParamId::DigitalOutput, ParamId::DigitalAdaptive}, 13, false},
        {{ParamId::Mode, ParamId::Mix, ParamId::SidechainHP, ParamId::AutoMakeup,
          ParamId::Crossover1, ParamId::Crossover2, ParamId::Crossover3,
          ParamId::MbMix, ParamId::MbOutput}, 9, true},
    }};

    auto verifyPreset = [&expectedOwnership](const duskaudio::MultiCompPreset& preset) {
        std::array<float, multicompp::kMeterMaster> values{};
        std::array<bool, multicompp::kMeterMaster> owned{};
        values.fill(-123.0f);
        multicompp::forEachPresetParam(preset,
            [&](multicompp::CoreParameter parameter, float value) {
                const int index = multicompp::coreParamIndex(parameter);
                require(index >= 0, "preset-owned core parameter remains host-visible");
                values[static_cast<size_t>(index)] = value;
                owned[static_cast<size_t>(index)] = true;
            },
            [&](int band, int field, float value) {
                const size_t index = static_cast<size_t>(multicompp::kBandBase + band * 8 + field);
                values[index] = value;
                owned[index] = true;
            });

        auto expect = [&](multicompp::ParamId id, float expected, const char* message) {
            const size_t index = static_cast<size_t>(id);
            require(owned[index], "factory preset applies every owned parameter");
            require(values[index] == expected, message);
        };
        auto descriptorDefault = [](multicompp::ParamId id) {
            return multicompp::kParams[static_cast<size_t>(id)].def;
        };

        expect(multicompp::ParamId::Mode, static_cast<float>(preset.mode),
               "factory preset applies Mode");
        expect(multicompp::ParamId::Mix, preset.mix, "factory preset applies Mix");
        expect(multicompp::ParamId::SidechainHP, preset.sidechainHP,
               "factory preset applies Sidechain HP");
        expect(multicompp::ParamId::AutoMakeup, preset.autoMakeup ? 1.0f : 0.0f,
               "factory preset applies Auto Makeup");

        switch (preset.mode)
        {
            case 0:
                expect(multicompp::ParamId::OptoPeakReduction, preset.peakReduction,
                       "Opto preset applies Peak Reduction");
                expect(multicompp::ParamId::OptoGain, duskaudio::optoGainDbToKnob(preset.makeup),
                       "Opto preset applies Gain");
                expect(multicompp::ParamId::OptoLimit, preset.limitMode ? 1.0f : 0.0f,
                       "Opto preset applies Limit");
                break;
            case 1:
            case 4:
                expect(multicompp::ParamId::FetInput, -preset.threshold,
                       "FET preset applies Input");
                expect(multicompp::ParamId::FetOutput, preset.makeup,
                       "FET preset applies Output");
                expect(multicompp::ParamId::FetAttack, preset.attack,
                       "FET preset applies Attack");
                expect(multicompp::ParamId::FetRelease, preset.release,
                       "FET preset applies Release");
                expect(multicompp::ParamId::FetRatio, static_cast<float>(preset.fetRatio),
                       "FET preset applies Ratio");
                expect(multicompp::ParamId::FetCurve,
                       descriptorDefault(multicompp::ParamId::FetCurve),
                       "FET preset resets Curve to its descriptor default");
                expect(multicompp::ParamId::FetTransient,
                       descriptorDefault(multicompp::ParamId::FetTransient),
                       "FET preset resets Transient to its descriptor default");
                expect(multicompp::ParamId::FetThreshold,
                       descriptorDefault(multicompp::ParamId::FetThreshold),
                       "FET preset resets Threshold to its descriptor default");
                break;
            case 2:
                expect(multicompp::ParamId::VcaThreshold, preset.threshold,
                       "VCA preset applies Threshold");
                expect(multicompp::ParamId::VcaRatio, preset.ratio,
                       "VCA preset applies Ratio");
                expect(multicompp::ParamId::VcaAttack, preset.attack,
                       "VCA preset applies Attack");
                expect(multicompp::ParamId::VcaRelease, preset.release,
                       "VCA preset applies Release");
                expect(multicompp::ParamId::VcaOutput, preset.makeup,
                       "VCA preset applies Output");
                expect(multicompp::ParamId::VcaOverEasy, preset.vcaOverEasy,
                       "VCA preset applies Over Easy");
                expect(multicompp::ParamId::VcaClassicDetector,
                       descriptorDefault(multicompp::ParamId::VcaClassicDetector),
                       "VCA preset resets Detector to its descriptor default");
                break;
            case 3:
                expect(multicompp::ParamId::BusThreshold, preset.threshold,
                       "Bus preset applies Threshold");
                expect(multicompp::ParamId::BusRatio,
                       static_cast<float>(preset.ratio <= 2.0f ? 0 : preset.ratio <= 4.0f ? 1 : 2),
                       "Bus preset applies Ratio");
                expect(multicompp::ParamId::BusAttack, static_cast<float>(preset.busAttack),
                       "Bus preset applies Attack");
                expect(multicompp::ParamId::BusRelease, static_cast<float>(preset.busRelease),
                       "Bus preset applies Release");
                expect(multicompp::ParamId::BusMakeup, preset.makeup,
                       "Bus preset applies Makeup");
                expect(multicompp::ParamId::BusMix, preset.mix,
                       "Bus preset applies local Mix");
                break;
            case 5:
                expect(multicompp::ParamId::StudioVcaThreshold, preset.threshold,
                       "Studio VCA preset applies Threshold");
                expect(multicompp::ParamId::StudioVcaRatio, preset.ratio,
                       "Studio VCA preset applies Ratio");
                expect(multicompp::ParamId::StudioVcaAttack, preset.attack,
                       "Studio VCA preset applies Attack");
                expect(multicompp::ParamId::StudioVcaRelease, preset.release,
                       "Studio VCA preset applies Release");
                expect(multicompp::ParamId::StudioVcaOutput, preset.makeup,
                       "Studio VCA preset applies Output");
                break;
            case 6:
                expect(multicompp::ParamId::DigitalThreshold, preset.threshold,
                       "Digital preset applies Threshold");
                expect(multicompp::ParamId::DigitalRatio, preset.ratio,
                       "Digital preset applies Ratio");
                expect(multicompp::ParamId::DigitalKnee,
                       descriptorDefault(multicompp::ParamId::DigitalKnee),
                       "Digital preset resets Knee to its descriptor default");
                expect(multicompp::ParamId::DigitalAttack, preset.attack,
                       "Digital preset applies Attack");
                expect(multicompp::ParamId::DigitalRelease, preset.release,
                       "Digital preset applies Release");
                expect(multicompp::ParamId::DigitalLookahead,
                       descriptorDefault(multicompp::ParamId::DigitalLookahead),
                       "Digital preset resets local Lookahead to its descriptor default");
                expect(multicompp::ParamId::DigitalMix,
                       descriptorDefault(multicompp::ParamId::DigitalMix),
                       "Digital preset resets local Mix to its descriptor default");
                expect(multicompp::ParamId::DigitalOutput, preset.makeup,
                       "Digital preset applies Output");
                expect(multicompp::ParamId::DigitalAdaptive,
                       descriptorDefault(multicompp::ParamId::DigitalAdaptive),
                       "Digital preset resets Adaptive Release to its descriptor default");
                break;
            case 7:
                expect(multicompp::ParamId::Crossover1,
                       descriptorDefault(multicompp::ParamId::Crossover1),
                       "Multiband preset resets Crossover 1 to its descriptor default");
                expect(multicompp::ParamId::Crossover2,
                       descriptorDefault(multicompp::ParamId::Crossover2),
                       "Multiband preset resets Crossover 2 to its descriptor default");
                expect(multicompp::ParamId::Crossover3,
                       descriptorDefault(multicompp::ParamId::Crossover3),
                       "Multiband preset resets Crossover 3 to its descriptor default");
                expect(multicompp::ParamId::MbMix,
                       descriptorDefault(multicompp::ParamId::MbMix),
                       "Multiband preset resets Mix to its descriptor default");
                expect(multicompp::ParamId::MbOutput,
                       descriptorDefault(multicompp::ParamId::MbOutput),
                       "Multiband preset resets Output to its descriptor default");
                for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
                    for (int field = 0; field < 8; ++field)
                    {
                        const size_t index = static_cast<size_t>(
                            multicompp::kBandBase + band * 8 + field);
                        require(owned[index], "Multiband preset applies every band parameter");
                        require(values[index] == multicompp::bandParam(field, band).def,
                                "Multiband preset resets band parameter to its descriptor default");
                    }
                break;
            default:
                break;
        }

        require(preset.mode >= 0 && static_cast<size_t>(preset.mode) < expectedOwnership.size(),
                "factory preset mode has an ownership table entry");
        std::array<bool, multicompp::kMeterMaster> expected{};
        const auto& modeOwnership = expectedOwnership[static_cast<size_t>(preset.mode)];
        for (size_t i = 0; i < modeOwnership.parameterCount; ++i)
            expected[static_cast<size_t>(modeOwnership.parameters[i])] = true;
        if (modeOwnership.ownsBandParameters)
            for (int i = multicompp::kBandBase; i < multicompp::kMeterMaster; ++i)
                expected[static_cast<size_t>(i)] = true;
        require(owned == expected,
                "factory preset owns exactly the common and active-mode parameters");
    };

    for (const auto& preset : multicompp::kFactoryPresets)
        verifyPreset(preset);

    auto syntheticDigital = multicompp::kFactoryPresets.front();
    syntheticDigital.mode = 6;
    verifyPreset(syntheticDigital);
    auto syntheticMultiband = multicompp::kFactoryPresets.front();
    syntheticMultiband.mode = 7;
    verifyPreset(syntheticMultiband);

    require(multicompp::coreParamIndex(MultiCompDSP::Parameter::EnvelopeCurve) < 0
                && multicompp::coreParamIndex(MultiCompDSP::Parameter::SaturationMode) < 0,
            "JUCE-inert controls are absent from the DAF parameter table");
    std::puts("factory preset ownership: every active-mode control applied; machine/global controls untouched");
}

void testHostProgramChangeAppliesBandParameters()
{
    auto multibandProgram = multicompp::kFactoryPresets.front();
    multibandProgram.mode = 7;
    std::array<float, multicompp::kMeterMaster> hostValues{};
    hostValues.fill(-123.0f);

    auto setHostParameter = [&](int parameterIndex, float hostValue) {
        const bool validIndex = parameterIndex >= 0 && parameterIndex < multicompp::kMeterMaster;
        require(validIndex, "host program change emits an in-range parameter index");
        if (validIndex)
            hostValues[static_cast<size_t>(parameterIndex)] = hostValue;
    };
    multicompp::applyPresetToHostParameters(multibandProgram, setHostParameter);

    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
        for (int field = 0; field < 8; ++field)
        {
            const int parameterIndex = multicompp::kBandBase + band * 8 + field;
            const auto descriptor = multicompp::bandParam(field, band);
            require(hostValues[static_cast<size_t>(parameterIndex)]
                        == multicompp::plainToHost(descriptor, descriptor.def),
                    "host program change applies every Multiband parameter in host space");
        }
    std::puts("host program change: every Multiband parameter applied in host space");
}

void testFractionalEnumUiAndDspAgreement()
{
    for (const float hostValue : {0.4f, 0.6f, 1.4f, 1.6f, 6.6f})
    {
        const auto& mode = multicompp::kParams[static_cast<size_t>(multicompp::ParamId::Mode)];
        const int dspIndex = static_cast<int>(multicompp::snapHostValue(mode, hostValue));
        const int uiIndex = multicompp::ui_detail::choiceIndex(hostValue, 8);
        require(uiIndex == dspIndex,
                "fractional enum host values select the same UI and DSP index");
    }
    std::puts("fractional enum automation: UI and DSP select the same rounded index");
}

void testEditorMirrorSeedsFromCurrentPluginState()
{
    multicompp::StateValues mirror{};
    for (int i = 0; i < multicompp::kMeterMaster; ++i)
        mirror[static_cast<size_t>(i)] = multicompp::resolveParameter(i,
            [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
            [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });
    auto pluginValues = mirror;
    const auto mode = static_cast<uint32_t>(multicompp::ParamId::Mode);
    const auto fetInput = static_cast<uint32_t>(multicompp::ParamId::FetInput);
    pluginValues[mode] = 1.0f;
    pluginValues[fetInput] = 17.25f;
    multicompp::ui_detail::refreshParameterMirror(mirror,
        [&pluginValues](uint32_t index) { return pluginValues[index]; },
        static_cast<uint32_t>(multicompp::kMeterMaster));
    require(mirror[mode] == 1.0f && mirror[fetInput] == 17.25f,
            "a newly opened editor seeds its mirror from the live plugin state");
    std::puts("editor mirror seed: pre-open FET host state adopted on the first frame");
}

void testCrossoverMirrorRefreshesEveryPluginChangedValue()
{
    multicompp::StateValues values{};
    for (int i = 0; i < multicompp::kMeterMaster; ++i)
        values[static_cast<size_t>(i)] = multicompp::resolveParameter(i,
            [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
            [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });
    const auto x1 = static_cast<uint32_t>(multicompp::ParamId::Crossover1);
    const auto x2 = static_cast<uint32_t>(multicompp::ParamId::Crossover2);
    const auto x3 = static_cast<uint32_t>(multicompp::ParamId::Crossover3);
    values[x1] = multicompp::plainToHost(multicompp::kParams[x1], 500.0f);
    values[x2] = multicompp::plainToHost(multicompp::kParams[x2], 300.0f);
    values[x3] = multicompp::plainToHost(multicompp::kParams[x3], 2000.0f);
    const auto staleValues = values;
    auto pluginValues = values;
    pluginValues[x2] = multicompp::plainToHost(multicompp::kParams[x2], 750.0f);
    multicompp::ui_detail::refreshCrossoverMirror(values,
        [&pluginValues](uint32_t index) { return pluginValues[index]; });
    const float staleX2 = multicompp::hostToPlain(multicompp::kParams[x2], staleValues[x2]);
    const float mirroredX2 = multicompp::hostToPlain(multicompp::kParams[x2], values[x2]);
    values[x2] = multicompp::plainToHost(multicompp::kParams[x2], 5000.0f);
    values[x3] = multicompp::plainToHost(multicompp::kParams[x3], 2000.0f);
    pluginValues = values;
    pluginValues[x3] = multicompp::plainToHost(multicompp::kParams[x3], 7500.0f);
    multicompp::ui_detail::refreshCrossoverMirror(values,
        [&pluginValues](uint32_t index) { return pluginValues[index]; });
    const float mirroredX3 = multicompp::hostToPlain(multicompp::kParams[x3], values[x3]);
    std::printf("ordered crossover mirror: stale X2 %.0f Hz; refreshed X2 %.0f Hz; sibling X3 %.0f Hz\n",
                staleX2, mirroredX2, mirroredX3);
    reviewCheck(values[x1] == pluginValues[x1]
                    && values[x2] == pluginValues[x2]
                    && values[x3] == pluginValues[x3],
                "UI mirror refreshes the plugin's full ordered crossover set");
    reviewCheck(mirroredX2 == 750.0f && mirroredX3 == 7500.0f,
                "crossover handles and readouts use the refreshed plugin values");

    // Mid-drag: the plugin still reports the pre-edit value for the handle the
    // user is holding, because only the AU wrapper writes a UI parameter change
    // through synchronously. That handle must keep the value the mouse just set
    // while its neighbours still adopt the DSP's ordering.
    values[x2] = multicompp::plainToHost(multicompp::kParams[x2], 900.0f);
    const float draggedHost = values[x2];
    pluginValues = values;
    pluginValues[x2] = multicompp::plainToHost(multicompp::kParams[x2], 750.0f);
    pluginValues[x3] = multicompp::plainToHost(multicompp::kParams[x3], 9000.0f);
    multicompp::ui_detail::refreshCrossoverMirror(values,
        [&pluginValues](uint32_t index) { return pluginValues[index]; }, x2);
    const float heldX2 = multicompp::hostToPlain(multicompp::kParams[x2], values[x2]);
    const float siblingX3 = multicompp::hostToPlain(multicompp::kParams[x3], values[x3]);
    std::printf("mid-drag mirror: held X2 %.0f Hz (plugin still says 750); sibling X3 %.0f Hz\n",
                heldX2, siblingX3);
    reviewCheck(values[x2] == draggedHost,
                "the crossover under an active drag keeps the value the mouse set");
    reviewCheck(siblingX3 == 9000.0f,
                "crossovers not being dragged still follow the plugin's ordering");
}

std::string readSiblingSource(const char* filename)
{
    std::string sourcePath = __FILE__;
    const size_t slash = sourcePath.rfind('/');
    require(slash != std::string::npos, "plugin-layer test source path is recognisable");
    sourcePath.replace(slash + 1, std::string::npos, filename);
    std::ifstream input(sourcePath);
    require(input.good(), "reviewed source file is available to structural regression");
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

// The parameter store keeps what the host wrote; only the read-back orders the
// three. Sweeping Crossover 1 up and back down must therefore leave Crossover 2
// exactly where it started -- if the ordering were stored, it would not.
void testCrossoverOrderingDoesNotRatchet()
{
    // What the host owns: never rewritten by the ordering.
    const float rawX1Start = 100.0f, rawX2 = 300.0f, rawX3 = 2000.0f;
    const auto atRest = multicompp::plugin_detail::orderedCrossovers(rawX1Start, rawX2, rawX3);
    const auto raised = multicompp::plugin_detail::orderedCrossovers(500.0f, rawX2, rawX3);
    const auto returned = multicompp::plugin_detail::orderedCrossovers(rawX1Start, rawX2, rawX3);
    std::printf("crossover sweep: X2 reads %.0f Hz at rest, %.0f Hz with X1 at 500, %.0f Hz back at 100\n",
                static_cast<double>(atRest[1]), static_cast<double>(raised[1]),
                static_cast<double>(returned[1]));
    reviewCheck(raised[1] > atRest[1],
                "raising Crossover 1 pushes the Crossover 2 the UI displays");
    reviewCheck(returned[1] == atRest[1] && returned[2] == atRest[2],
                "lowering Crossover 1 again returns the displayed neighbours");
}

// One case per DAF_PLUGIN_EXTRA_IO layout. The { 2, 2 } row is the one the
// output count could not express: it is stereo, so a channel-count test would
// have called it sidechain-capable and read two elements past the end of the
// array the host supplied.
void testRunGuardsSidechainPortsPerLayout()
{
    float main = 0.1f, sidechain = 0.8f;
    const float* monoInputs[] = {&main};
    const float* stereoInputs[] = {&main, &main};
    const float* sidechainInputs[] = {&main, &main, &sidechain, &sidechain};
    const bool externalArmed = true;
    const bool monoUsesSidechain = externalArmed
        && multicompp::plugin_detail::hasStereoExternalSidechainPorts(1, monoInputs);
    const bool stereoUsesSidechain = externalArmed
        && multicompp::plugin_detail::hasStereoExternalSidechainPorts(2, stereoInputs);
    const bool fullUsesSidechain = externalArmed
        && multicompp::plugin_detail::hasStereoExternalSidechainPorts(4, sidechainInputs);
    std::printf("external sidechain armed: 1-in uses aux ports %s; 2-in %s; 4-in %s\n",
                monoUsesSidechain ? "yes" : "no", stereoUsesSidechain ? "yes" : "no",
                fullUsesSidechain ? "yes" : "no");
    reviewCheck(!monoUsesSidechain,
                "mono run path does not index absent stereo sidechain ports");
    reviewCheck(!stereoUsesSidechain,
                "stereo-insert run path does not index absent sidechain ports");
    reviewCheck(fullUsesSidechain,
                "full input layout still accepts its external sidechain ports");
}

void testQuantisedFineStepFallsBackToRestingPrecision()
{
    const float minValue = 0.0f, maxValue = 1.0f, value = 0.0f;
    const float coordinateStep = 0.0008f * (maxValue - minValue);
    const float adjacent = std::min(maxValue, value + coordinateStep);
    const auto quantisedDisplay = [](float coordinate) {
        return std::round(60.0f + coordinate * 100.0f);
    };
    const float fineStep = std::fabs(quantisedDisplay(adjacent) - quantisedDisplay(value));
    const int decimals = duskdaf::dragDecimalPlaces(fineStep, "%.0f");
    std::printf("quantised tapered fine-step: display delta %.1f, drag decimals %d\n",
                fineStep, decimals);
    reviewCheck(decimals == 0,
                "a quantised fine-step collapse falls back to resting precision");
    reviewCheck(duskdaf::dragDecimalPlaces(0.01f, "%.1f") == 2
                    && duskdaf::dragDecimalPlaces(1.0f, "%.1f") == 1,
                "nonzero fine steps still add precision only when needed");
}

void testOptoSidechainHpUsesPhysicalDragDomain()
{
    const auto parameter = static_cast<uint32_t>(multicompp::ParamId::SidechainHP);
    const auto& descriptor = multicompp::kParams[parameter];
    const float draggedHz = duskdaf::dragKnobCoordinate(
        0.0f, -66.0f, descriptor.min, descriptor.max, false);
    const float draggedHost = multicompp::plainToHost(descriptor, draggedHz);
    const float resultingHz = multicompp::hostToPlain(descriptor, draggedHost);
    const float oldHostDrag = duskdaf::dragKnobCoordinate(
        0.0f, -66.0f, multicompp::hostMin(descriptor),
        multicompp::hostMax(descriptor), false);
    const float oldResultingHz = multicompp::hostToPlain(descriptor, oldHostDrag);

    require(multicompp::ui_detail::optoKnobUsesPlainDomainDrag(parameter)
                && !multicompp::ui_detail::optoKnobUsesPlainDomainDrag(
                    static_cast<uint32_t>(multicompp::ParamId::Mix)),
            "only the tapered Opto SC HP knob opts into physical-domain dragging");
    require(resultingHz == 165.0f && oldResultingHz == 54.0f,
            "a 66-pixel SC HP drag reaches 165 Hz instead of the old 54 Hz");
    std::printf("Opto SC HP drag: 66 px reaches %.0f Hz (old host-domain drag %.0f Hz)\n",
                resultingHz, oldResultingHz);
}

void testShippingUiHasNoDeadCrossoverDescriptor()
{
    const std::string source = readSiblingSource("MultiCompUI.cpp");
    const bool hasDeadDescriptor = source.find("const auto& d = multicompp::kParams[p];")
        != std::string::npos;
    std::printf("shipping UI dead crossover descriptor present: %s\n",
                hasDeadDescriptor ? "yes" : "no");
    reviewCheck(!hasDeadDescriptor, "shipping crossover UI has no unused descriptor variable");
}

void testShippingUiUsesConformingHeaderWithoutUtilityBands()
{
    const std::string source = readSiblingSource("MultiCompUI.cpp");
    const std::string info = readSiblingSource("DafPluginInfo.h");
    auto countText = [&source](const char* text) {
        size_t count = 0;
        for (size_t at = source.find(text); at != std::string::npos;
             at = source.find(text, at + 1))
            ++count;
        return count;
    };
    const bool drawsGlobalBand = source.find("drawGlobal(dl);") != std::string::npos;
    const bool drawsSidechainBand = source.find("drawSidechain(dl);") != std::string::npos;
    const bool hasConformingIdentity = source.find("\"MULTI-COMP\"") != std::string::npos
        && source.find("\"MC-2\"") != std::string::npos
        && source.find("\"DUSK AUDIO\"") != std::string::npos;
    const bool hasPresetActions = source.find("##mc_presetprev") != std::string::npos
        && source.find("##mc_presetnext") != std::string::npos
        && source.find("##mc_init") != std::string::npos
        && source.find("##mc_save") != std::string::npos;
    const bool hasCompactDefaultHeight =
        info.find("#define DAF_UI_DEFAULT_HEIGHT       380") != std::string::npos;
    const bool hasSingleHeaderIdentity = countText("\"DUSK AUDIO\"") == 1
        && source.find("\"DUSK\"") == std::string::npos;
    const bool hasSimplifiedOptoTitle =
        source.find("\"LEVELING AMPLIFIER\"") != std::string::npos
        && source.find("\"OPTICAL LEVELING AMPLIFIER\"") == std::string::npos;
    const bool hasOptoSidechainHp =
        source.find("optoKnob(dl, \"opto_sc_hp\", P_SC_HP") != std::string::npos;
    const bool hasModeButtonRow =
        source.find("##mc_mode_button_%d") != std::string::npos;
    const bool hasModeDropdown = source.find("ImGui::BeginCombo(\"##mc_mode\"")
        != std::string::npos;

    std::printf("header conformity: global=%s sidechain=%s identity=%s actions=%s height380=%s "
                "single-brand=%s opto-title=%s\n",
                drawsGlobalBand ? "drawn" : "removed",
                drawsSidechainBand ? "drawn" : "removed",
                hasConformingIdentity ? "yes" : "no",
                hasPresetActions ? "yes" : "no",
                hasCompactDefaultHeight ? "yes" : "no",
                hasSingleHeaderIdentity ? "yes" : "no",
                hasSimplifiedOptoTitle ? "yes" : "no");
    reviewCheck(!drawsGlobalBand && !drawsSidechainBand,
                "shipping UI omits the visible Global and Sidechain utility bands");
    reviewCheck(hasConformingIdentity && hasPresetActions && hasCompactDefaultHeight,
                "shipping UI follows the MC-2 identity, preset-action, and compact-height contract");
    reviewCheck(hasSingleHeaderIdentity && hasSimplifiedOptoTitle,
                "Dusk Audio appears only in the header and Opto uses the simplified faceplate title");
    reviewCheck(hasOptoSidechainHp,
                "Opto faceplate exposes the existing Sidechain HP parameter");
    reviewCheck(hasModeButtonRow && !hasModeDropdown,
                "all eight compressor modes are exposed as top-row buttons");
}

void testFetFaceplateContract()
{
    const std::string source = readSiblingSource("MultiCompUI.cpp");
    const bool hasBlackFaceIdentity = source.find("\"FET 76\"") != std::string::npos
        && source.find("\"LIMITING AMPLIFIER\"") != std::string::npos;
    const bool reusesOptoMeter =
        source.find("drawOptoMeter(dl, 675") != std::string::npos
        && source.find("void drawFetMeter(") == std::string::npos
        && source.find("mode == 0 || mode == 1") != std::string::npos;
    const bool hasFiveRatioButtons = source.find("drawFetRatioButtons(dl") != std::string::npos
        && source.find("visualOrder") != std::string::npos
        && source.find("row < 5") != std::string::npos;
    const bool exposesOnlyMixExtension =
        source.find("fetTrimKnob(dl, \"fet_mix\"") != std::string::npos
        && source.find("fetTrimKnob(dl, \"fet_threshold\"") == std::string::npos
        && source.find("fetTrimKnob(dl, \"fet_trans\"") == std::string::npos
        && source.find("drawFetCurveSwitch") == std::string::npos;
    const bool hasConcentricKnobSkirts =
        source.find("AddCircleFilled(center, r * 0.96f") != std::string::npos
        && source.find("center.x - r * 0.13f") == std::string::npos;
    const size_t studioBranch = source.find("if (studio)");
    const size_t vintageFace = source.find("constexpr float left = 0.0f",
                                           studioBranch);
    const bool keepsStudioDistinct = studioBranch != std::string::npos
        && vintageFace != std::string::npos && studioBranch < vintageFace
        && source.find("\"sf_in\"", studioBranch) < vintageFace;
    const auto attack = static_cast<uint32_t>(multicompp::ParamId::FetAttack);
    const auto release = static_cast<uint32_t>(multicompp::ParamId::FetRelease);
    const auto input = static_cast<uint32_t>(multicompp::ParamId::FetInput);
    const bool timingReadoutUsesDialPosition =
        multicompp::ui_detail::fetTimingDialValue(multicompp::hostMin(
            multicompp::kParams[attack]), attack) == 1.0f
        && multicompp::ui_detail::fetTimingDialValue(multicompp::hostMax(
            multicompp::kParams[attack]), attack) == 7.0f
        && multicompp::ui_detail::fetTimingDialValue(multicompp::hostMin(
            multicompp::kParams[release]), release) == 1.0f
        && multicompp::ui_detail::fetTimingDialValue(multicompp::hostMax(
            multicompp::kParams[release]), release) == 7.0f
        && multicompp::ui_detail::fetTimingHostValue(4.0f, attack) == 0.5f
        && multicompp::ui_detail::fetTimingHostValue(4.0f, release) == 575.0f
        && !multicompp::ui_detail::fetTimingUsesDialReadout(input);
    std::printf("FET faceplate: identity=%s opto-meter=%s ratios=%s mix-only=%s "
                "concentric-knobs=%s studio-distinct=%s timing-readout=%s\n",
                hasBlackFaceIdentity ? "yes" : "no",
                reusesOptoMeter ? "yes" : "no",
                hasFiveRatioButtons ? "yes" : "no",
                exposesOnlyMixExtension ? "yes" : "no",
                hasConcentricKnobSkirts ? "yes" : "no",
                keepsStudioDistinct ? "yes" : "no",
                timingReadoutUsesDialPosition ? "1-7" : "legacy-ms");
    require(hasBlackFaceIdentity && reusesOptoMeter && hasFiveRatioButtons
                && exposesOnlyMixExtension && hasConcentricKnobSkirts
                && keepsStudioDistinct && timingReadoutUsesDialPosition,
            "vintage FET uses 1-7 timing readouts while preserving its hardware faceplate");
}

void testAuCustomViewUsesComponentBundle()
{
#if defined(__APPLE__)
    const std::string cmake = readSiblingSource("CMakeLists.txt");
    const std::string resolver = readSiblingSource("MultiCompAUComponentBundlePath.mm");
    const bool resolverIsLinkedIntoAu =
        cmake.find("multi_comp_2-au") != std::string::npos
        && cmake.find("MultiCompAUComponentBundlePath.mm") != std::string::npos;
    const bool resolverUsesItsOwnImage =
        resolver.find("&setMultiCompAUComponentBundlePath") != std::string::npos
        && resolver.find("DAF_NAMESPACE::d_nextBundlePath = componentBundlePath")
            != std::string::npos;
    require(resolverIsLinkedIntoAu && resolverUsesItsOwnImage,
            "AU custom view bundle is resolved from code inside the component image");
    std::printf("AU Cocoa editor path: component-local resolver linked=%s source=%s\n",
                resolverIsLinkedIntoAu ? "yes" : "no",
                resolverUsesItsOwnImage ? "yes" : "no");
#endif
}

void testHostProgramChangeUpdatesUiMirror()
{
    for (size_t presetIndex = 0; presetIndex < multicompp::kFactoryPresets.size(); ++presetIndex)
    {
        multicompp::StateValues uiValues{};
        for (size_t i = 0; i < uiValues.size(); ++i)
            uiValues[i] = -123.0f + static_cast<float>(i) * 0.01f;
        auto expected = uiValues;
        multicompp::applyPresetToHostParameters(multicompp::kFactoryPresets[presetIndex],
            [&](int parameterIndex, float hostValue)
            {
                expected[static_cast<size_t>(parameterIndex)] = hostValue;
            });

        const int selected = multicompp::ui_detail::loadProgramIntoMirror(
            static_cast<uint32_t>(presetIndex), uiValues);
        require(selected == static_cast<int>(presetIndex),
                "host program change selects the recalled preset in the UI");
        require(uiValues == expected,
                "host program change updates the UI mirror to the applied preset values");
    }
    std::puts("host program change: UI mirror matches every applied factory preset");
}

void testPresetIdentityFollowsPresetOwnership()
{
    const int factoryPreset = 5;
    const auto mode = static_cast<uint32_t>(multicompp::ParamId::Mode);
    const auto bypass = static_cast<uint32_t>(multicompp::ParamId::Bypass);
    const auto oversampling = static_cast<uint32_t>(multicompp::ParamId::Oversampling);
    const auto externalSidechain =
        static_cast<uint32_t>(multicompp::ParamId::ExternalSidechain);

    const bool factoryContract =
        multicompp::ui_detail::selectionOwnsParam(factoryPreset, false, false, mode)
        && !multicompp::ui_detail::selectionOwnsParam(
            factoryPreset, false, false, bypass)
        && !multicompp::ui_detail::selectionOwnsParam(
            factoryPreset, false, false, oversampling)
        && !multicompp::ui_detail::selectionOwnsParam(
            factoryPreset, false, false, externalSidechain);
    const bool userContract =
        multicompp::ui_detail::selectionOwnsParam(-1, true, false, oversampling)
        && !multicompp::ui_detail::selectionOwnsParam(-1, true, false, bypass);
    const bool defaultContract =
        multicompp::ui_detail::selectionOwnsParam(-1, false, true, oversampling)
        && !multicompp::ui_detail::selectionOwnsParam(-1, false, true, bypass);
    const bool customContract =
        !multicompp::ui_detail::selectionOwnsParam(-1, false, false, mode);

    require(factoryContract && userContract && defaultContract && customContract,
            "preset identity clears only when an edit changes state that selection owns");
    std::puts("preset identity: factory machine controls and every bypass preserve selection");
}

void testOptoFaceplateContract()
{
    using Layout = multicompp::ui_detail::OptoFaceplateLayout;
    constexpr float referenceAspect = 1120.0f / 310.0f;
    const float aspect = multicompp::ui_detail::optoFaceplateAspect();
    require(Layout::left == 0.0f && Layout::right == 1120.0f
                && Layout::top == 344.0f && Layout::bottom == 654.0f
                && std::abs(aspect - referenceAspect) < 0.01f,
            "Opto faceplate fills the complete mode canvas below the toolbar");
    require(multicompp::ui_detail::designHeightForMode(0.0f) == 380.0f
                && multicompp::ui_detail::designHeightForMode(0.4f) == 380.0f
                && multicompp::ui_detail::designHeightForMode(0.6f) == 380.0f
                && multicompp::ui_detail::designHeightForMode(1.4f) == 380.0f
                && multicompp::ui_detail::designHeightForMode(1.6f) == 486.0f
                && multicompp::ui_detail::designHeightForMode(7.0f) == 486.0f,
            "Opto and vintage FET use rack-height canvases while the other modes keep the full canvas");

    const float zero = multicompp::ui_detail::optoMeterNeedleAngle(0.0f);
    const float ten = multicompp::ui_detail::optoMeterNeedleAngle(-10.0f);
    const float twenty = multicompp::ui_detail::optoMeterNeedleAngle(-20.0f);
    require(std::isfinite(zero) && std::isfinite(ten) && std::isfinite(twenty)
                && zero > ten && ten > twenty,
            "Opto GR needle moves monotonically from zero to 20 dB reduction");
    require(std::strcmp(multicompp::ui_detail::optoModeLabel(0.0f), "COMPRESS") == 0
                && std::strcmp(multicompp::ui_detail::optoModeLabel(1.0f), "LIMIT") == 0,
            "Opto mode control exposes the Comp and Limit panel labels");
    require(std::strcmp(multicompp::ui_detail::optoMeterLabel(), "GR") == 0,
            "Opto analogue meter is fixed to the GR panel readout");
    const float attackDisplay = multicompp::ui_detail::optoMeterDisplayValue(-20.0f);
    const float releaseDisplay = multicompp::ui_detail::optoMeterDisplayValue(0.0f);
    const std::string source = readSiblingSource("MultiCompUI.cpp");
    const size_t meterStart = source.find("void drawOptoMeter(");
    const size_t meterEnd = source.find("void drawFet(", meterStart);
    require(meterStart != std::string::npos && meterEnd != std::string::npos,
            "shared Opto/FET meter renderer is present");
    const std::string meterSource = source.substr(meterStart, meterEnd - meterStart);
    const bool hasOneDbTickScale =
        meterSource.find("for (int tick = 0; tick <= 20; ++tick)") != std::string::npos
        && meterSource.find("static_cast<float>(tick) / 20.0f") != std::string::npos
        && meterSource.find("const bool major = tick % 5 == 0") != std::string::npos;
    const bool omitsVuLevelLegend =
        meterSource.find("VU LEVEL INDICATOR") == std::string::npos;
    require(hasOneDbTickScale && omitsVuLevelLegend,
            "shared Opto/FET meter has one tick per dB and no VU level legend");
    const bool drawsRawGainReduction = source.find(
        "optoMeterDisplayValue(meter(kMeterMaster))") != std::string::npos;
    const bool keepsDisplayEnvelope = source.find("optoMeterBallisticStep")
            != std::string::npos
        || source.find("optoMeterGainReductionDb") != std::string::npos;
    reviewCheck(attackDisplay == -20.0f && releaseDisplay == 0.0f
                    && drawsRawGainReduction && !keepsDisplayEnvelope,
                "Opto needle follows raw DSP gain reduction without added display lag or state");
    reviewCheck(std::strcmp(multicompp::ui_detail::optoKnobValueSuffix(), "") == 0,
                "Opto knob value bubbles show unitless 0-100 values");
    const float idleReadout = multicompp::ui_detail::optoMeterReadoutAmount(0.0f);
    require(idleReadout == 0.0f && !std::signbit(idleReadout)
                && multicompp::ui_detail::optoMeterReadoutAmount(-12.5f) == 12.5f
                && multicompp::ui_detail::optoMeterReadoutAmount(1.0f) == 0.0f
                && multicompp::ui_detail::optoMeterReadoutAmount(-150.0f) == 99.9f,
            "Opto GR readout is positive, bounded, and never displays negative zero");
    std::printf("opto faceplate: aspect %.6f reference %.6f; meter ticks=21 "
                "(1 dB each), legend=removed; meter angles "
                "0/10/20 dB %.6f/%.6f/%.6f rad; modes %s/%s\n",
                aspect, referenceAspect, zero, ten, twenty,
                multicompp::ui_detail::optoModeLabel(0.0f),
                multicompp::ui_detail::optoModeLabel(1.0f));
}

static_assert(multicompp::kParamCount == 63,
              "DAF host table must exclude the two JUCE-inert controls");
} // namespace

// A host ramping an integer parameter delivers fractional intermediates. Those
// used to be stored verbatim, so getState() emitted a fractional integer and
// decodeState() then rejected the ENTIRE state -- a project saved mid-ramp lost
// every plugin setting on reload. setParameterValue() now snaps integer
// descriptors, so anything that can be stored can also be loaded.
void testFractionalIntegerAutomationStaysLoadable()
{
    const auto defaults = []() {
        multicompp::StateValues v{};
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
            v[static_cast<size_t>(i)] = multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
                [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });
        return v;
    };

    int integerIndex = -1;
    for (int i = 0; i < multicompp::kMeterMaster && integerIndex < 0; ++i)
        if (multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return d.integer; },
                [](const multicompp::BandParam& d, int) { return d.integer; }))
            integerIndex = i;
    require(integerIndex >= 0, "an integer-valued host parameter exists");

    // What a host actually sends part way through an automation ramp.
    const float fractional = multicompp::resolveParameter(integerIndex,
        [](const multicompp::Param& d) { return multicompp::hostMin(d) + 0.6f; },
        [](const multicompp::BandParam& d, int) { return multicompp::hostMin(d) + 0.6f; });
    const float snapped = multicompp::resolveParameter(integerIndex,
        [fractional](const multicompp::Param& d) { return multicompp::snapHostValue(d, fractional); },
        [fractional](const multicompp::BandParam& d, int) { return multicompp::snapHostValue(d, fractional); });
    require(std::trunc(snapped) == snapped, "snapped automation value is integral");

    multicompp::StateValues stored = defaults();
    stored[static_cast<size_t>(integerIndex)] = snapped;
    multicompp::StateValues restored = defaults();
    require(multicompp::decodeState(multicompp::encodeState(stored), restored),
            "state stored after fractional automation reloads");
    require(restored[static_cast<size_t>(integerIndex)] == snapped, "reloaded integer value survives");

    // Control: the unsnapped value is what used to be stored, and it makes the
    // whole state unloadable while leaving the destination untouched.
    multicompp::StateValues bad = defaults();
    bad[static_cast<size_t>(integerIndex)] = fractional;
    multicompp::StateValues destination = defaults();
    const multicompp::StateValues before = destination;
    require(!multicompp::decodeState(multicompp::encodeState(bad), destination),
            "an unsnapped fractional integer would reject the whole state");
    for (size_t i = 0; i < destination.size(); ++i)
        require(destination[i] == before[i], "rejected state leaves the destination untouched");
    std::puts("fractional integer automation: snapped on store, state stays loadable");
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--fet-ui") == 0)
    {
        testFetFaceplateContract();
        std::puts("Multi-Comp FET faceplate test: PASS");
        return 0;
    }
    testRunGuardsSidechainPortsPerLayout();
    testCrossoverOrderingDoesNotRatchet();
    testQuantisedFineStepFallsBackToRestingPrecision();
    testOptoSidechainHpUsesPhysicalDragDomain();
    testShippingUiHasNoDeadCrossoverDescriptor();
    testShippingUiUsesConformingHeaderWithoutUtilityBands();
    testFetFaceplateContract();
    testAuCustomViewUsesComponentBundle();
    testOptoFaceplateContract();
    testCrossoverMirrorRefreshesEveryPluginChangedValue();
    require(reviewFailureCount == 0, "review regressions are fixed");
    testHostParameterTapers();
    testParameterIntervals();
    testStrictStateValidationAndRoundTrip();
    testFactoryPresetOwnership();
    testHostProgramChangeAppliesBandParameters();
    testHostProgramChangeUpdatesUiMirror();
    testPresetIdentityFollowsPresetOwnership();
    testFractionalEnumUiAndDspAgreement();
    testEditorMirrorSeedsFromCurrentPluginState();
    testFractionalIntegerAutomationStaysLoadable();
    std::puts("Multi-Comp plugin-layer tests: PASS");
    return 0;
}

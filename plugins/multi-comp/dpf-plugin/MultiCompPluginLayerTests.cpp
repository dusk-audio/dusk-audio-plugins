#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using duskaudio::MultiCompDSP;

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
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
    auto verifyPreset = [](const duskaudio::MultiCompPreset& preset) {
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

        for (const auto id : {multicompp::ParamId::Bypass,
                              multicompp::ParamId::StereoLink,
                              multicompp::ParamId::TruePeakEnable,
                              multicompp::ParamId::TruePeakQuality,
                              multicompp::ParamId::ExternalSidechain,
                              multicompp::ParamId::Distortion,
                              multicompp::ParamId::DistortionAmount,
                              multicompp::ParamId::Oversampling,
                              multicompp::ParamId::GlobalLookahead,
                              multicompp::ParamId::GlobalSidechainListen,
                              multicompp::ParamId::NoiseEnable,
                              multicompp::ParamId::ScLowFreq,
                              multicompp::ParamId::ScLowGain,
                              multicompp::ParamId::ScHighFreq,
                              multicompp::ParamId::ScHighGain,
                              multicompp::ParamId::StereoLinkMode})
            require(!owned[static_cast<size_t>(id)],
                    "factory preset leaves machine/global parameter untouched");
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
            "JUCE-inert controls are absent from the DPF parameter table");
    std::puts("factory preset ownership: every active-mode control applied; machine/global controls untouched");
}

void testHostProgramChangeAppliesBandParameters()
{
    auto multibandProgram = multicompp::kFactoryPresets.front();
    multibandProgram.mode = 7;
    std::array<float, multicompp::kMeterMaster> hostValues{};
    hostValues.fill(-123.0f);

    multicompp::applyPresetToHostParameters(multibandProgram,
        [&](int parameterIndex, float hostValue) {
            hostValues[static_cast<size_t>(parameterIndex)] = hostValue;
        });

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

static_assert(multicompp::kParamCount == 63,
              "DPF host table must exclude the two JUCE-inert controls");
} // namespace

int main()
{
    testHostParameterTapers();
    testStrictStateValidationAndRoundTrip();
    testFactoryPresetOwnership();
    testHostProgramChangeAppliesBandParameters();
    std::puts("Multi-Comp plugin-layer tests: PASS");
    return 0;
}

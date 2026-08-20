#pragma once
#include "../core/MultiCompPresets.hpp"
#include "MultiCompParams.hpp"
namespace multicompp { inline constexpr auto& kFactoryPresets = duskaudio::kMultiCompPresets; }

namespace multicompp
{
inline int coreParamIndex(CoreParameter parameter) noexcept
{
    for (int i = 0; i < kParamCount; ++i)
        if (kParams[static_cast<size_t>(i)].core == parameter) return i;
    return -1;
}

// Shared factory-program walker. A recall owns only the JUCE preset's common
// controls and the controls for its selected compressor mode. Machine/global
// choices (including Bypass, oversampling, external sidechain and global
// lookahead) remain untouched.
template <class SetParameter, class SetBandParameter>
void forEachPresetParam(const duskaudio::MultiCompPreset& preset,
                        SetParameter&& setParameter,
                        SetBandParameter&& setBandParameter)
{
    using P = CoreParameter;
    setParameter(P::Mode, static_cast<float>(preset.mode));
    setParameter(P::Mix, preset.mix);
    setParameter(P::SidechainHP, preset.sidechainHP);
    setParameter(P::AutoMakeup, preset.autoMakeup ? 1.0f : 0.0f);

    switch (preset.mode)
    {
        case 0:
            setParameter(P::OptoPeakReduction, preset.peakReduction);
            setParameter(P::OptoGain, duskaudio::optoGainDbToKnob(preset.makeup));
            setParameter(P::OptoLimit, preset.limitMode ? 1.0f : 0.0f);
            break;
        case 1:
        case 4:
            setParameter(P::FetInput, -preset.threshold);
            setParameter(P::FetOutput, preset.makeup);
            setParameter(P::FetAttack, preset.attack);
            setParameter(P::FetRelease, preset.release);
            setParameter(P::FetRatio, static_cast<float>(preset.fetRatio));
            setParameter(P::FetCurve, kParams[static_cast<size_t>(ParamId::FetCurve)].def);
            setParameter(P::FetTransient, kParams[static_cast<size_t>(ParamId::FetTransient)].def);
            setParameter(P::FetThreshold, kParams[static_cast<size_t>(ParamId::FetThreshold)].def);
            break;
        case 2:
            setParameter(P::VcaThreshold, preset.threshold);
            setParameter(P::VcaRatio, preset.ratio);
            setParameter(P::VcaAttack, preset.attack);
            setParameter(P::VcaRelease, preset.release);
            setParameter(P::VcaOutput, preset.makeup);
            setParameter(P::VcaOverEasy, preset.vcaOverEasy);
            setParameter(P::VcaClassicDetector,
                         kParams[static_cast<size_t>(ParamId::VcaClassicDetector)].def);
            break;
        case 3:
        {
            const int ratioChoice = preset.ratio <= 2.0f ? 0 : preset.ratio <= 4.0f ? 1 : 2;
            setParameter(P::BusThreshold, preset.threshold);
            setParameter(P::BusRatio, static_cast<float>(ratioChoice));
            setParameter(P::BusAttack, static_cast<float>(preset.busAttack));
            setParameter(P::BusRelease, static_cast<float>(preset.busRelease));
            setParameter(P::BusMakeup, preset.makeup);
            setParameter(P::BusMix, preset.mix);
            break;
        }
        case 5:
            setParameter(P::StudioVcaThreshold, preset.threshold);
            setParameter(P::StudioVcaRatio, preset.ratio);
            setParameter(P::StudioVcaAttack, preset.attack);
            setParameter(P::StudioVcaRelease, preset.release);
            setParameter(P::StudioVcaOutput, preset.makeup);
            break;
        case 6:
            setParameter(P::DigitalThreshold, preset.threshold);
            setParameter(P::DigitalRatio, preset.ratio);
            setParameter(P::DigitalKnee, kParams[static_cast<size_t>(ParamId::DigitalKnee)].def);
            setParameter(P::DigitalAttack, preset.attack);
            setParameter(P::DigitalRelease, preset.release);
            setParameter(P::DigitalLookahead,
                         kParams[static_cast<size_t>(ParamId::DigitalLookahead)].def);
            setParameter(P::DigitalMix, kParams[static_cast<size_t>(ParamId::DigitalMix)].def);
            setParameter(P::DigitalOutput, preset.makeup);
            setParameter(P::DigitalAdaptive,
                         kParams[static_cast<size_t>(ParamId::DigitalAdaptive)].def);
            break;
        case 7:
            setParameter(P::Crossover1, kParams[static_cast<size_t>(ParamId::Crossover1)].def);
            setParameter(P::Crossover2, kParams[static_cast<size_t>(ParamId::Crossover2)].def);
            setParameter(P::Crossover3, kParams[static_cast<size_t>(ParamId::Crossover3)].def);
            setParameter(P::MbMix, kParams[static_cast<size_t>(ParamId::MbMix)].def);
            setParameter(P::MbOutput, kParams[static_cast<size_t>(ParamId::MbOutput)].def);
            for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
                for (int field = 0; field < 8; ++field)
                    setBandParameter(band, field, bandParam(field, band).def);
            break;
        default:
            break;
    }
}

// Host-program adapter used by Plugin::loadProgram. Values passed to the
// setter are host-domain values at their real DPF parameter indices.
template <class SetHostParameter>
void applyPresetToHostParameters(const duskaudio::MultiCompPreset& preset,
                                 SetHostParameter&& setHostParameter)
{
    forEachPresetParam(preset,
        [&](CoreParameter parameter, float value)
        {
            const int parameterIndex = coreParamIndex(parameter);
            if (parameterIndex >= 0)
            {
                const auto& d = kParams[static_cast<size_t>(parameterIndex)];
                setHostParameter(parameterIndex, plainToHost(d, value));
            }
        },
        [&](int band, int field, float value)
        {
            const auto d = bandParam(field, band);
            setHostParameter(kBandBase + band * 8 + field, plainToHost(d, value));
        });
}
} // namespace multicompp

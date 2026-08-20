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
// choices (including Bypass, oversampling, external sidechain and lookahead)
// and all multiband fields remain untouched.
template <class SetParameter>
void forEachPresetParam(const duskaudio::MultiCompPreset& preset,
                        SetParameter&& setParameter)
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
            break;
        case 2:
            setParameter(P::VcaThreshold, preset.threshold);
            setParameter(P::VcaRatio, preset.ratio);
            setParameter(P::VcaAttack, preset.attack);
            setParameter(P::VcaRelease, preset.release);
            setParameter(P::VcaOutput, preset.makeup);
            setParameter(P::VcaOverEasy, preset.vcaOverEasy);
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
            setParameter(P::DigitalAttack, preset.attack);
            setParameter(P::DigitalRelease, preset.release);
            setParameter(P::DigitalOutput, preset.makeup);
            break;
        case 7:
        default:
            break;
    }
}
} // namespace multicompp

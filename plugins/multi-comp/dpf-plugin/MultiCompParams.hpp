#pragma once

#include "../core/MultiCompDSP.hpp"
#include "../core/MultiCompPresets.hpp"
#include <array>

namespace multicompp
{
using CoreParameter = duskaudio::MultiCompDSP::Parameter;
struct Param { const char* id; const char* name; const char* unit; float min, max, def; CoreParameter core; bool integer; };
struct BandParam { const char* id; const char* name; const char* unit; float min, max, def; duskaudio::MultiCompDSP::MultibandParameter core; bool integer; };

// The first 65 entries mirror the active core/JUCE controls and ranges. The
// retired studio_vca_mix parameter is intentionally absent.
inline constexpr std::array<Param, 65> kParams = {{
 {"mode","Mode","",0,7,0,CoreParameter::Mode,true}, {"bypass","Bypass","",0,1,0,CoreParameter::Bypass,true},
 {"stereo_link","Stereo Link","%",0,100,100,CoreParameter::StereoLink,false}, {"mix","Mix","%",0,100,100,CoreParameter::Mix,false},
 {"sidechain_hp","SC HP Filter","Hz",0,500,0,CoreParameter::SidechainHP,false}, {"true_peak_enable","True Peak","",0,1,0,CoreParameter::TruePeakEnable,true},
 {"true_peak_quality","TP Quality","",0,1,0,CoreParameter::TruePeakQuality,true}, {"sidechain_enable","External Sidechain","",0,1,0,CoreParameter::ExternalSidechain,true},
 {"auto_makeup","Auto Makeup","",0,1,0,CoreParameter::AutoMakeup,true}, {"distortion_type","Distortion","",0,3,0,CoreParameter::Distortion,true},
 {"distortion_amount","Distortion Amt","%",0,100,50,CoreParameter::DistortionAmount,false}, {"oversampling","Oversampling","",0,2,1,CoreParameter::Oversampling,true},
 {"global_lookahead","Lookahead","ms",0,10,0,CoreParameter::GlobalLookahead,false},
 {"opto_peak_reduction","Peak Reduction","%",0,100,0,CoreParameter::OptoPeakReduction,false}, {"opto_gain","Gain","%",0,100,50,CoreParameter::OptoGain,false}, {"opto_limit","Limit Mode","",0,1,0,CoreParameter::OptoLimit,true},
 {"fet_input","Input","dB",-20,40,0,CoreParameter::FetInput,false}, {"fet_output","Output","dB",-20,20,0,CoreParameter::FetOutput,false}, {"fet_attack","Attack","ms",0.02f,80,0.2f,CoreParameter::FetAttack,false}, {"fet_release","Release","ms",50,1100,400,CoreParameter::FetRelease,false}, {"fet_ratio","Ratio",":1",0,4,0,CoreParameter::FetRatio,true}, {"fet_curve_mode","Curve Mode","",0,1,0,CoreParameter::FetCurve,true}, {"fet_transient","Transient","%",0,100,0,CoreParameter::FetTransient,false}, {"fet_threshold","Threshold","dB",-60,0,-10,CoreParameter::FetThreshold,false},
 {"vca_threshold","Threshold","dB",-38,12,0,CoreParameter::VcaThreshold,false}, {"vca_ratio","Ratio",":1",1,120,4,CoreParameter::VcaRatio,false}, {"vca_attack","Attack","ms",0.1f,50,1,CoreParameter::VcaAttack,false}, {"vca_release","Release","ms",10,5000,100,CoreParameter::VcaRelease,false}, {"vca_output","Output","dB",-20,20,0,CoreParameter::VcaOutput,false}, {"vca_overeasy","Over Easy","",0,1,0,CoreParameter::VcaOverEasy,true}, {"vca_detector_mode","VCA Detector","",0,1,0,CoreParameter::VcaClassicDetector,true},
 {"bus_threshold","Threshold","dB",-30,15,0,CoreParameter::BusThreshold,false}, {"bus_ratio","Ratio",":1",0,2,0,CoreParameter::BusRatio,true}, {"bus_attack","Attack","",0,5,2,CoreParameter::BusAttack,true}, {"bus_release","Release","",0,4,1,CoreParameter::BusRelease,true}, {"bus_makeup","Makeup","dB",0,20,0,CoreParameter::BusMakeup,false}, {"bus_mix","Bus Mix","%",0,100,100,CoreParameter::BusMix,false},
 {"studio_vca_threshold","Threshold","dB",-40,20,-10,CoreParameter::StudioVcaThreshold,false}, {"studio_vca_ratio","Ratio",":1",1,10,3,CoreParameter::StudioVcaRatio,false}, {"studio_vca_attack","Attack","ms",0.3f,75,10,CoreParameter::StudioVcaAttack,false}, {"studio_vca_release","Release","ms",100,4000,300,CoreParameter::StudioVcaRelease,false}, {"studio_vca_output","Output","dB",-20,20,0,CoreParameter::StudioVcaOutput,false},
 {"digital_threshold","Threshold","dB",-60,0,-20,CoreParameter::DigitalThreshold,false}, {"digital_ratio","Ratio",":1",1,100,4,CoreParameter::DigitalRatio,false}, {"digital_knee","Knee","dB",0,20,6,CoreParameter::DigitalKnee,false}, {"digital_attack","Attack","ms",0.01f,500,10,CoreParameter::DigitalAttack,false}, {"digital_release","Release","ms",1,5000,100,CoreParameter::DigitalRelease,false}, {"digital_lookahead","Lookahead","ms",0,10,0,CoreParameter::DigitalLookahead,false}, {"digital_mix","Mix","%",0,100,100,CoreParameter::DigitalMix,false}, {"digital_output","Output","dB",-24,24,0,CoreParameter::DigitalOutput,false}, {"digital_adaptive","Adaptive Release","",0,1,0,CoreParameter::DigitalAdaptive,true},
 {"mb_crossover_1","Crossover 1","Hz",20,500,200,CoreParameter::Crossover1,false}, {"mb_crossover_2","Crossover 2","Hz",200,5000,2000,CoreParameter::Crossover2,false}, {"mb_crossover_3","Crossover 3","Hz",2000,16000,8000,CoreParameter::Crossover3,false},
 {"envelope_curve","Envelope Curve","",0,1,0,CoreParameter::EnvelopeCurve,true},
 {"global_sidechain_listen","SC Listen","",0,1,0,CoreParameter::GlobalSidechainListen,true},
 {"mb_mix","MB Mix","%",0,100,100,CoreParameter::MbMix,false},
 {"mb_output","MB Output","dB",-24,24,0,CoreParameter::MbOutput,false},
 {"noise_enable","Analog Noise","",0,1,1,CoreParameter::NoiseEnable,true},
 {"saturation_mode","Saturation Mode","",0,2,0,CoreParameter::SaturationMode,true},
 {"sc_low_freq","SC Low Freq","Hz",60,500,100,CoreParameter::ScLowFreq,false},
 {"sc_low_gain","SC Low Gain","dB",-12,12,0,CoreParameter::ScLowGain,false},
 {"sc_high_freq","SC High Freq","Hz",2000,16000,8000,CoreParameter::ScHighFreq,false},
 {"sc_high_gain","SC High Gain","dB",-12,12,0,CoreParameter::ScHighGain,false},
 {"stereo_link_mode","Link Mode","",0,2,0,CoreParameter::StereoLinkMode,true}
}};
inline constexpr int kParamCount = static_cast<int>(kParams.size());
inline constexpr int kBandParamCount = 8 * duskaudio::kMultiCompBands;
inline constexpr int kBandBase = kParamCount;
inline constexpr int kMeterMaster = kBandBase + kBandParamCount;
inline constexpr int kMeterBand0 = kMeterMaster + 1;
inline constexpr int kMeterBand1 = kMeterMaster + 2;
inline constexpr int kMeterBand2 = kMeterMaster + 3;
inline constexpr int kMeterBand3 = kMeterMaster + 4;
inline constexpr int kTotalParamCount = kMeterMaster + 5;
inline constexpr BandParam bandParam(int field, int band)
{
    const char* names[8] = {"Threshold", "Ratio", "Attack", "Release", "Makeup", "Bypass", "Solo", "Enabled"};
    const char* units[8] = {"dB", ":1", "ms", "ms", "dB", "", "", ""};
    (void)band;
    switch (field)
    {
        case 0: return {"mb_threshold", names[field], units[field], -60, 0, -20, duskaudio::MultiCompDSP::MultibandParameter::Threshold, false};
        case 1: return {"mb_ratio", names[field], units[field], 1, 20, 4, duskaudio::MultiCompDSP::MultibandParameter::Ratio, false};
        case 2: return {"mb_attack", names[field], units[field], 0.1f, 100, 10, duskaudio::MultiCompDSP::MultibandParameter::Attack, false};
        case 3: return {"mb_release", names[field], units[field], 10, 1000, 100, duskaudio::MultiCompDSP::MultibandParameter::Release, false};
        case 4: return {"mb_makeup", names[field], units[field], -12, 12, 0, duskaudio::MultiCompDSP::MultibandParameter::Makeup, false};
        case 5: return {"mb_bypass", names[field], units[field], 0, 1, 0, duskaudio::MultiCompDSP::MultibandParameter::Bypass, true};
        case 6: return {"mb_solo", names[field], units[field], 0, 1, 0, duskaudio::MultiCompDSP::MultibandParameter::Solo, true};
        default:return {"mb_enabled", names[field], units[field], 0, 1, 1, duskaudio::MultiCompDSP::MultibandParameter::Enabled, true};
    }
}
inline constexpr const char* const kModes[8] = {"Opto","FET","VCA","Bus","Studio FET","Studio VCA","Digital","Multiband"};
inline constexpr const char* const kOnOff[2] = {"Off","On"};
inline constexpr const char* const kRatios[5] = {"4:1","8:1","12:1","20:1","All"};
inline constexpr const char* const kOversampling[3] = {"Off","2x","4x"};
inline constexpr const char* const kDistortion[4] = {"Off","Soft","Hard","Clip"};
inline constexpr const char* const kTruePeakQuality[2] = {"4x (Standard)","8x (High)"};
inline constexpr const char* const kEnvelopeCurve[2] = {"Logarithmic (Analog)","Linear (Digital)"};
inline constexpr const char* const kSaturationMode[3] = {"Vintage (Warm)","Modern (Clean)","Pristine (Minimal)"};
inline constexpr const char* const kLinkMode[3] = {"Stereo","Mid-Side","Dual Mono"};
}

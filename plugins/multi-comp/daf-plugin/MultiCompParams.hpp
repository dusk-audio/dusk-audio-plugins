#pragma once

#include "../core/MultiCompDSP.hpp"
#include "../core/MultiCompPresets.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace multicompp
{
using CoreParameter = duskaudio::MultiCompDSP::Parameter;
struct Param
{
    const char* id; const char* name; const char* unit;
    float min, max, def; CoreParameter core; bool integer;
    float interval = 0.0f, skew = 1.0f;
};
struct BandParam
{
    const char* id; const char* name; const char* unit;
    float min, max, def; duskaudio::MultiCompDSP::MultibandParameter core; bool integer;
    float interval = 0.0f, skew = 1.0f;
};

enum class ParamId : uint32_t {
 Mode, Bypass, StereoLink, Mix, SidechainHP, TruePeakEnable, TruePeakQuality,
 ExternalSidechain, AutoMakeup, Distortion, DistortionAmount, Oversampling, GlobalLookahead,
 OptoPeakReduction, OptoGain, OptoLimit, FetInput, FetOutput, FetAttack, FetRelease,
 FetRatio, FetCurve, FetTransient, FetThreshold, VcaThreshold, VcaRatio, VcaAttack,
 VcaRelease, VcaOutput, VcaOverEasy, VcaClassicDetector, BusThreshold, BusRatio,
 BusAttack, BusRelease, BusMakeup, BusMix, StudioVcaThreshold, StudioVcaRatio,
 StudioVcaAttack, StudioVcaRelease, StudioVcaOutput, DigitalThreshold, DigitalRatio,
 DigitalKnee, DigitalAttack, DigitalRelease, DigitalLookahead, DigitalMix, DigitalOutput,
 DigitalAdaptive, Crossover1, Crossover2, Crossover3, GlobalSidechainListen,
 MbMix, MbOutput, NoiseEnable, ScLowFreq, ScLowGain, ScHighFreq,
 ScHighGain, StereoLinkMode, Count
};

// Single source of truth for host symbols, physical ranges/defaults, JUCE taper
// metadata, UI mapping and state keys. The retired studio_vca_mix plus the
// JUCE-inert envelope_curve and saturation_mode controls are absent.
inline constexpr std::array<Param, 63> kParams = {{
 {"mode","Mode","",0,7,0,CoreParameter::Mode,true}, {"bypass","Bypass","",0,1,0,CoreParameter::Bypass,true},
 {"stereo_link","Stereo Link","%",0,100,100,CoreParameter::StereoLink,false}, {"mix","Mix","%",0,100,100,CoreParameter::Mix,false},
 {"sidechain_hp","SC HP Filter","Hz",0,500,0,CoreParameter::SidechainHP,false,1,0.5f}, {"true_peak_enable","True Peak","",0,1,0,CoreParameter::TruePeakEnable,true},
 {"true_peak_quality","TP Quality","",0,1,0,CoreParameter::TruePeakQuality,true}, {"sidechain_enable","External Sidechain","",0,1,0,CoreParameter::ExternalSidechain,true},
 {"auto_makeup","Auto Makeup","",0,1,0,CoreParameter::AutoMakeup,true}, {"distortion_type","Distortion","",0,3,0,CoreParameter::Distortion,true},
 {"distortion_amount","Distortion Amt","%",0,100,50,CoreParameter::DistortionAmount,false}, {"oversampling","Oversampling","",0,2,1,CoreParameter::Oversampling,true},
 {"global_lookahead","Lookahead","ms",0,10,0,CoreParameter::GlobalLookahead,false},
 {"opto_peak_reduction","Peak Reduction","%",0,100,0,CoreParameter::OptoPeakReduction,false}, {"opto_gain","Gain","%",0,100,duskaudio::kOptoGainUnityKnob,CoreParameter::OptoGain,false}, {"opto_limit","Limit Mode","",0,1,0,CoreParameter::OptoLimit,true},
 {"fet_input","Input","dB",-20,40,0,CoreParameter::FetInput,false}, {"fet_output","Output","dB",-20,20,0,CoreParameter::FetOutput,false}, {"fet_attack","Attack","ms",0.02f,80,0.2f,CoreParameter::FetAttack,false,0.01f,0.3f}, {"fet_release","Release","ms",50,1100,400,CoreParameter::FetRelease,false}, {"fet_ratio","Ratio",":1",0,4,0,CoreParameter::FetRatio,true}, {"fet_curve_mode","Curve Mode","",0,1,0,CoreParameter::FetCurve,true}, {"fet_transient","Transient","%",0,100,0,CoreParameter::FetTransient,false}, {"fet_threshold","Threshold","dB",-60,0,-10,CoreParameter::FetThreshold,false},
 {"vca_threshold","Threshold","dB",-38,12,0,CoreParameter::VcaThreshold,false}, {"vca_ratio","Ratio",":1",1,120,4,CoreParameter::VcaRatio,false,0.1f,0.3f}, {"vca_attack","Attack","ms",0.1f,50,1,CoreParameter::VcaAttack,false}, {"vca_release","Release","ms",10,5000,100,CoreParameter::VcaRelease,false}, {"vca_output","Output","dB",-20,20,0,CoreParameter::VcaOutput,false}, {"vca_overeasy","Over Easy","",0,1,0,CoreParameter::VcaOverEasy,true}, {"vca_detector_mode","VCA Detector","",0,1,0,CoreParameter::VcaClassicDetector,true},
 {"bus_threshold","Threshold","dB",-30,15,0,CoreParameter::BusThreshold,false}, {"bus_ratio","Ratio",":1",0,2,0,CoreParameter::BusRatio,true}, {"bus_attack","Attack","",0,5,2,CoreParameter::BusAttack,true}, {"bus_release","Release","",0,4,1,CoreParameter::BusRelease,true}, {"bus_makeup","Makeup","dB",0,20,0,CoreParameter::BusMakeup,false}, {"bus_mix","Bus Mix","%",0,100,100,CoreParameter::BusMix,false},
 {"studio_vca_threshold","Threshold","dB",-40,20,-10,CoreParameter::StudioVcaThreshold,false}, {"studio_vca_ratio","Ratio",":1",1,10,3,CoreParameter::StudioVcaRatio,false}, {"studio_vca_attack","Attack","ms",0.3f,75,10,CoreParameter::StudioVcaAttack,false}, {"studio_vca_release","Release","ms",100,4000,300,CoreParameter::StudioVcaRelease,false}, {"studio_vca_output","Output","dB",-20,20,0,CoreParameter::StudioVcaOutput,false},
 {"digital_threshold","Threshold","dB",-60,0,-20,CoreParameter::DigitalThreshold,false}, {"digital_ratio","Ratio",":1",1,100,4,CoreParameter::DigitalRatio,false,0.1f,0.4f}, {"digital_knee","Knee","dB",0,20,6,CoreParameter::DigitalKnee,false}, {"digital_attack","Attack","ms",0.01f,500,10,CoreParameter::DigitalAttack,false,0.01f,0.3f}, {"digital_release","Release","ms",1,5000,100,CoreParameter::DigitalRelease,false,1,0.4f}, {"digital_lookahead","Lookahead","ms",0,10,0,CoreParameter::DigitalLookahead,false}, {"digital_mix","Mix","%",0,100,100,CoreParameter::DigitalMix,false}, {"digital_output","Output","dB",-24,24,0,CoreParameter::DigitalOutput,false}, {"digital_adaptive","Adaptive Release","",0,1,0,CoreParameter::DigitalAdaptive,true},
 {"mb_crossover_1","Crossover 1","Hz",20,500,200,CoreParameter::Crossover1,false,1,0.4f}, {"mb_crossover_2","Crossover 2","Hz",200,5000,2000,CoreParameter::Crossover2,false,1,0.4f}, {"mb_crossover_3","Crossover 3","Hz",2000,16000,8000,CoreParameter::Crossover3,false,1,0.4f},
 {"global_sidechain_listen","SC Listen","",0,1,0,CoreParameter::GlobalSidechainListen,true},
 {"mb_mix","MB Mix","%",0,100,100,CoreParameter::MbMix,false},
 {"mb_output","MB Output","dB",-24,24,0,CoreParameter::MbOutput,false},
 {"noise_enable","Analog Noise","",0,1,1,CoreParameter::NoiseEnable,true},
 {"sc_low_freq","SC Low Freq","Hz",60,500,100,CoreParameter::ScLowFreq,false,1,0.5f},
 {"sc_low_gain","SC Low Gain","dB",-12,12,0,CoreParameter::ScLowGain,false},
 {"sc_high_freq","SC High Freq","Hz",2000,16000,8000,CoreParameter::ScHighFreq,false,10,0.5f},
 {"sc_high_gain","SC High Gain","dB",-12,12,0,CoreParameter::ScHighGain,false},
 {"stereo_link_mode","Link Mode","",0,2,0,CoreParameter::StereoLinkMode,true}
}};
inline constexpr int kParamCount = static_cast<int>(kParams.size());
static_assert(kParamCount == static_cast<int>(ParamId::Count));
static_assert(kParamCount == 63);
#define MC_ASSERT_PARAM(name) \
    static_assert(kParams[static_cast<size_t>(ParamId::name)].core == CoreParameter::name, \
                  "kParams order does not match ParamId::" #name);
MC_ASSERT_PARAM(Mode) MC_ASSERT_PARAM(Bypass) MC_ASSERT_PARAM(StereoLink) MC_ASSERT_PARAM(Mix)
MC_ASSERT_PARAM(SidechainHP) MC_ASSERT_PARAM(TruePeakEnable) MC_ASSERT_PARAM(TruePeakQuality)
MC_ASSERT_PARAM(ExternalSidechain) MC_ASSERT_PARAM(AutoMakeup) MC_ASSERT_PARAM(Distortion)
MC_ASSERT_PARAM(DistortionAmount) MC_ASSERT_PARAM(Oversampling) MC_ASSERT_PARAM(GlobalLookahead)
MC_ASSERT_PARAM(OptoPeakReduction) MC_ASSERT_PARAM(OptoGain) MC_ASSERT_PARAM(OptoLimit)
MC_ASSERT_PARAM(FetInput) MC_ASSERT_PARAM(FetOutput) MC_ASSERT_PARAM(FetAttack) MC_ASSERT_PARAM(FetRelease)
MC_ASSERT_PARAM(FetRatio) MC_ASSERT_PARAM(FetCurve) MC_ASSERT_PARAM(FetTransient) MC_ASSERT_PARAM(FetThreshold)
MC_ASSERT_PARAM(VcaThreshold) MC_ASSERT_PARAM(VcaRatio) MC_ASSERT_PARAM(VcaAttack) MC_ASSERT_PARAM(VcaRelease)
MC_ASSERT_PARAM(VcaOutput) MC_ASSERT_PARAM(VcaOverEasy) MC_ASSERT_PARAM(VcaClassicDetector)
MC_ASSERT_PARAM(BusThreshold) MC_ASSERT_PARAM(BusRatio) MC_ASSERT_PARAM(BusAttack) MC_ASSERT_PARAM(BusRelease)
MC_ASSERT_PARAM(BusMakeup) MC_ASSERT_PARAM(BusMix) MC_ASSERT_PARAM(StudioVcaThreshold)
MC_ASSERT_PARAM(StudioVcaRatio) MC_ASSERT_PARAM(StudioVcaAttack) MC_ASSERT_PARAM(StudioVcaRelease)
MC_ASSERT_PARAM(StudioVcaOutput) MC_ASSERT_PARAM(DigitalThreshold) MC_ASSERT_PARAM(DigitalRatio)
MC_ASSERT_PARAM(DigitalKnee) MC_ASSERT_PARAM(DigitalAttack) MC_ASSERT_PARAM(DigitalRelease)
MC_ASSERT_PARAM(DigitalLookahead) MC_ASSERT_PARAM(DigitalMix) MC_ASSERT_PARAM(DigitalOutput)
MC_ASSERT_PARAM(DigitalAdaptive) MC_ASSERT_PARAM(Crossover1) MC_ASSERT_PARAM(Crossover2)
MC_ASSERT_PARAM(Crossover3) MC_ASSERT_PARAM(GlobalSidechainListen) MC_ASSERT_PARAM(MbMix)
MC_ASSERT_PARAM(MbOutput) MC_ASSERT_PARAM(NoiseEnable) MC_ASSERT_PARAM(ScLowFreq)
MC_ASSERT_PARAM(ScLowGain) MC_ASSERT_PARAM(ScHighFreq) MC_ASSERT_PARAM(ScHighGain)
MC_ASSERT_PARAM(StereoLinkMode)
#undef MC_ASSERT_PARAM
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
        case 1: return {"mb_ratio", names[field], units[field], 1, 20, 4, duskaudio::MultiCompDSP::MultibandParameter::Ratio, false, 0.1f, 0.5f};
        case 2: return {"mb_attack", names[field], units[field], 0.1f, 100, 10, duskaudio::MultiCompDSP::MultibandParameter::Attack, false, 0.1f, 0.4f};
        case 3: return {"mb_release", names[field], units[field], 10, 1000, 100, duskaudio::MultiCompDSP::MultibandParameter::Release, false, 1, 0.4f};
        case 4: return {"mb_makeup", names[field], units[field], -12, 12, 0, duskaudio::MultiCompDSP::MultibandParameter::Makeup, false};
        case 5: return {"mb_bypass", names[field], units[field], 0, 1, 0, duskaudio::MultiCompDSP::MultibandParameter::Bypass, true};
        case 6: return {"mb_solo", names[field], units[field], 0, 1, 0, duskaudio::MultiCompDSP::MultibandParameter::Solo, true};
        default:return {"mb_enabled", names[field], units[field], 0, 1, 1, duskaudio::MultiCompDSP::MultibandParameter::Enabled, true};
    }
}

template <class GlobalFn, class BandFn>
decltype(auto) resolveParameter(int index, GlobalFn&& globalFn, BandFn&& bandFn)
{
    if (index < kParamCount)
        return std::forward<GlobalFn>(globalFn)(kParams[static_cast<size_t>(index)]);

    const int relative = index - kBandBase;
    const int band = relative / 8;
    const int field = relative % 8;
    return std::forward<BandFn>(bandFn)(bandParam(field, band), band);
}

template <class Descriptor>
inline bool hasSkew(const Descriptor& d) noexcept { return d.skew != 1.0f; }

template <class Descriptor>
inline float hostMin(const Descriptor& d) noexcept { return hasSkew(d) ? 0.0f : d.min; }

template <class Descriptor>
inline float hostMax(const Descriptor& d) noexcept { return hasSkew(d) ? 1.0f : d.max; }

template <class Descriptor>
inline float snapPlainValue(const Descriptor& d, float value) noexcept
{
    value = std::clamp(value, d.min, d.max);
    if (d.interval > 0.0f)
        value = d.min + d.interval * std::floor((value - d.min) / d.interval + 0.5f);
    return std::clamp(value, d.min, d.max);
}

template <class Descriptor>
inline float plainToHost(const Descriptor& d, float value) noexcept
{
    value = snapPlainValue(d, value);
    if (!hasSkew(d)) return value;
    const float proportion = (value - d.min) / (d.max - d.min);
    return std::pow(proportion, d.skew);
}

template <class Descriptor>
inline float hostToPlain(const Descriptor& d, float value) noexcept
{
    value = std::clamp(value, hostMin(d), hostMax(d));
    if (!hasSkew(d)) return snapPlainValue(d, value);
    const float proportion = std::pow(value, 1.0f / d.skew);
    return snapPlainValue(d, d.min + (d.max - d.min) * proportion);
}

template <class Descriptor>
inline float hostDefault(const Descriptor& d) noexcept { return plainToHost(d, d.def); }

inline constexpr const char* const kModes[8] = {"Opto","FET","VCA","Bus","Studio FET","Studio VCA","Digital","Multiband"};
inline constexpr const char* const kOnOff[2] = {"Off","On"};
inline constexpr const char* const kRatios[5] = {"4:1","8:1","12:1","20:1","All"};
inline constexpr const char* const kOversampling[3] = {"Off","2x","4x"};
inline constexpr const char* const kDistortion[4] = {"Off","Soft","Hard","Clip"};
inline constexpr const char* const kTruePeakQuality[2] = {"4x (Standard)","8x (High)"};
inline constexpr const char* const kFetCurve[2] = {"Modern", "Measured"};
inline constexpr const char* const kVcaDetector[2] = {"Adaptive", "Classic"};
inline constexpr const char* const kBusRatios[3] = {"2:1", "4:1", "10:1"};
inline constexpr const char* const kBusAttack[6] = {"0.1ms", "0.3ms", "1ms", "3ms", "10ms", "30ms"};
inline constexpr const char* const kBusRelease[5] = {"0.1s", "0.3s", "0.6s", "1.2s", "Auto"};
inline constexpr const char* const kLinkMode[3] = {"Stereo","Mid-Side","Dual Mono"};

// State serialization, shared so the plugin and the UI cannot drift apart the
// way their preset application once did.
//
// Version 1 wrote decimal floats. It never shipped in any build a user could
// install, and reading it back needs the C++17 floating-point std::from_chars,
// which libc++ marks "introduced in macOS 13.3", above our deployment target.
// Version 2 therefore carried the float's bit pattern through the integer
// overloads. Version 3 removes two inert fields and stores tapered controls in
// their new normalized host domain. Both are exact and locale-independent.
inline constexpr int kStateVersion = 3;

// A version we do not understand must be refused outright. Decoding it anyway
// makes every value fail to parse and silently restores defaults, which reads
// to the user as a session that quietly lost its settings.
inline bool stateVersionSupported(std::string_view state) noexcept
{
    const size_t end = state.find(';');
    const std::string_view token = state.substr(0, end == std::string_view::npos ? state.size() : end);
    if (token.size() < 3 || token.substr(0, 2) != "v=") return false;

    int version = 0;
    const std::string_view digits = token.substr(2);
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), version);
    return parsed.ec == std::errc() && parsed.ptr == digits.data() + digits.size()
        && version == kStateVersion;
}

inline bool decodeStateFloat(std::string_view text, float& out) noexcept
{
    std::uint32_t bits = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()) return false;
    std::memcpy(&out, &bits, sizeof(out));
    return std::isfinite(out);
}

inline void appendStateFloat(std::string& out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char number[16];
    const auto result = std::to_chars(number, number + sizeof(number), bits, 16);
    if (result.ec == std::errc()) out.append(number, result.ptr);
}

using StateValues = std::array<float, static_cast<size_t>(kMeterMaster)>;

inline int stateIndexForId(std::string_view id) noexcept
{
    for (int i = 0; i < kParamCount; ++i)
        if (id == kParams[static_cast<size_t>(i)].id) return i;

    for (int field = 0; field < 8; ++field)
    {
        const std::string_view base = bandParam(field, 0).id;
        if (id.size() != base.size() + 2 || id.substr(0, base.size()) != base
            || id[base.size()] != '_' || id[base.size() + 1] < '0'
            || id[base.size() + 1] >= static_cast<char>('0' + duskaudio::kMultiCompBands))
            continue;
        const int band = id[base.size() + 1] - '0';
        return kBandBase + band * 8 + field;
    }
    return -1;
}

// Clamp a host value into its descriptor's range, and snap it to an integer
// when the descriptor is integral.
//
// Snapping matters beyond tidiness. getState() serialises the stored host values
// verbatim, and decodeState() rejects the WHOLE state if an integer-valued
// parameter carries a fraction. A host ramping an integer parameter delivers
// fractional intermediates, so without snapping a project saved mid-ramp stores
// something like mode=0.6 and fails to load in its entirety.
//
// Rejecting fractional values here instead would be worse: the plugin would
// ignore legitimate automation. Snapping keeps automation working and keeps
// every stored value loadable. Rounding rather than truncating also handles a
// host that delivers 0.9999999 for 1.
template <class Descriptor>
inline float snapHostValue(const Descriptor& d, float value) noexcept
{
    const float clamped = duskaudio::clampFinite(value, hostMin(d), hostMax(d), hostDefault(d));
    return d.integer ? std::round(clamped) : clamped;
}

inline bool hostValueInRange(int index, float value) noexcept
{
    if (!std::isfinite(value)) return false;
    return resolveParameter(index,
        [value](const Param& d) {
            return value >= hostMin(d) && value <= hostMax(d)
                && (!d.integer || std::trunc(value) == value);
        },
        [value](const BandParam& d, int) {
            return value >= hostMin(d) && value <= hostMax(d)
                && (!d.integer || std::trunc(value) == value);
        });
}

// Complete-set transactional decoder. The caller's output is untouched unless
// every required key appears exactly once with a finite in-range value.
inline bool decodeState(std::string_view state, StateValues& out) noexcept
{
    if (state.empty() || state.back() == ';' || !stateVersionSupported(state)) return false;
    const size_t versionEnd = state.find(';');
    if (versionEnd == std::string_view::npos) return false;

    StateValues decoded{};
    std::array<bool, static_cast<size_t>(kMeterMaster)> seen{};
    size_t begin = versionEnd + 1;
    while (begin < state.size())
    {
        const size_t end = state.find(';', begin);
        const std::string_view token = state.substr(
            begin, end == std::string_view::npos ? state.size() - begin : end - begin);
        const size_t equal = token.find('=');
        if (equal == std::string_view::npos || equal == 0
            || equal + 1 == token.size() || token.find('=', equal + 1) != std::string_view::npos)
            return false;

        const int index = stateIndexForId(token.substr(0, equal));
        if (index < 0 || seen[static_cast<size_t>(index)]) return false;
        float value = 0.0f;
        if (!decodeStateFloat(token.substr(equal + 1), value)
            || !hostValueInRange(index, value))
            return false;
        decoded[static_cast<size_t>(index)] = value;
        seen[static_cast<size_t>(index)] = true;

        if (end == std::string_view::npos) { begin = state.size(); break; }
        begin = end + 1;
    }
    if (begin != state.size()) return false;
    for (bool present : seen)
        if (!present) return false;
    out = decoded;
    return true;
}

inline std::string encodeState(const StateValues& values)
{
    std::string state = "v=" + std::to_string(kStateVersion);
    state.reserve(32 + static_cast<size_t>(kMeterMaster) * 28);
    for (int i = 0; i < kMeterMaster; ++i)
    {
        state.push_back(';');
        resolveParameter(i,
            [&state](const Param& d) { state += d.id; },
            [&state](const BandParam& d, int band) {
                state += d.id;
                state.push_back('_');
                state.push_back(static_cast<char>('0' + band));
            });
        state.push_back('=');
        appendStateFloat(state, values[static_cast<size_t>(i)]);
    }
    return state;
}
}

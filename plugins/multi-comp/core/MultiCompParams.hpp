// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Framework-free parameter and enum definitions for Multi-Comp.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "../../shared-daf/dsp/DuskFilters.hpp"

namespace duskaudio
{

enum class MultiCompMode : int { Opto = 0, FET, VCA, Bus, StudioFET, StudioVCA, Digital, Multiband };
enum class DistortionType : int { Off = 0, Soft, Hard, Clip };

constexpr int kMultiCompModes = 8;
constexpr int kMultiCompBands = 4;

inline float clampFinite(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

struct OptoGainPoint
{
    float knob;
    float gainDb;
};

inline constexpr float kOptoGainSilentKnobMax = 8.5f;
inline constexpr float kOptoGainMuteDb = -160.0f;
inline constexpr float kOptoGainUnityKnob =
    20.0f + 3.017f / (3.017f + 0.214f) * 3.75f;
inline constexpr std::array<OptoGainPoint, 15> kOptoGainTaper{{
    {kOptoGainSilentKnobMax, kOptoGainMuteDb},
    {10.0f, -21.951f}, {15.0f, -8.777f}, {20.0f, -3.017f},
    {23.75f, 0.214f}, {30.0f, 4.191f}, {35.0f, 7.823f},
    {40.0f, 10.615f}, {50.0f, 14.501f}, {60.0f, 18.813f},
    {70.0f, 26.646f}, {80.0f, 33.269f}, {90.0f, 37.271f},
    {95.0f, 37.702f}, {100.0f, 37.702f}
}};

// The measured hardware taper is interpolated in dB between observed knob
// positions. It mutes below about 0.085 and its gain element, independently of
// the output ceiling, has reached its +37.702 dB plateau by 0.95.
inline float optoKnobToGainDb(float knob) noexcept
{
    const float clamped = clampFinite(knob, 0.0f, 100.0f, kOptoGainUnityKnob);
    if (clamped <= kOptoGainSilentKnobMax) return kOptoGainMuteDb;
    for (size_t upper = 1; upper < kOptoGainTaper.size(); ++upper)
    {
        if (clamped > kOptoGainTaper[upper].knob) continue;
        const auto& lo = kOptoGainTaper[upper - 1];
        const auto& hi = kOptoGainTaper[upper];
        const float fraction = (clamped - lo.knob) / (hi.knob - lo.knob);
        return lo.gainDb + fraction * (hi.gainDb - lo.gainDb);
    }
    return kOptoGainTaper.back().gainDb;
}

inline float optoKnobToLinearGain(float knob) noexcept
{
    if (clampFinite(knob, 0.0f, 100.0f, kOptoGainUnityKnob)
        <= kOptoGainSilentKnobMax)
        return 0.0f;
    return decibelsToGain(optoKnobToGainDb(knob));
}

inline float optoGainDbToKnob(float db) noexcept
{
    const float clamped = clampFinite(db, kOptoGainMuteDb,
        kOptoGainTaper.back().gainDb, 0.0f);
    if (clamped <= kOptoGainMuteDb) return 0.0f;
    for (size_t upper = 1; upper < kOptoGainTaper.size(); ++upper)
    {
        if (clamped > kOptoGainTaper[upper].gainDb) continue;
        const auto& lo = kOptoGainTaper[upper - 1];
        const auto& hi = kOptoGainTaper[upper];
        const float range = hi.gainDb - lo.gainDb;
        if (range <= 0.0f) return hi.knob;
        const float fraction = (clamped - lo.gainDb) / range;
        return lo.knob + fraction * (hi.knob - lo.knob);
    }
    return 100.0f;
}

class LinearRamp
{
public:
    void prepare(double sampleRate, float seconds) noexcept
    {
        steps = std::max(1, static_cast<int>(seconds * sampleRate));
        countdown = 0;
        current = target;
    }
    void snap(float value) noexcept { current = target = value; countdown = 0; }
    void setTarget(float value) noexcept
    {
        if (value == target) return;
        target = value;
        countdown = steps;
        increment = (target - current) / static_cast<float>(steps);
    }
    float next() noexcept
    {
        if (countdown <= 0) return target;
        current += increment;
        if (--countdown == 0) current = target;
        return current;
    }
    float value() const noexcept { return current; }

private:
    float current = 0.0f, target = 0.0f, increment = 0.0f;
    int countdown = 0, steps = 1;
};

struct MultiCompParameterState
{
    std::atomic<int> mode{0};
    std::atomic<bool> bypass{false};
    std::atomic<float> stereoLink{100.0f};
    std::atomic<float> mix{100.0f};
    std::atomic<float> sidechainHP{0.0f};
    std::atomic<int> envelopeCurve{0};
    std::atomic<bool> globalSidechainListen{false};
    std::atomic<float> mbMix{100.0f}, mbOutput{0.0f};
    std::atomic<bool> noiseEnable{true};
    std::atomic<int> saturationMode{0};
    std::atomic<float> scLowFreq{100.0f}, scLowGain{0.0f}, scHighFreq{8000.0f}, scHighGain{0.0f};
    std::atomic<int> stereoLinkMode{0};
    std::atomic<bool> truePeakEnable{false};
    std::atomic<int> truePeakQuality{0};
    std::atomic<bool> externalSidechain{false};
    std::atomic<bool> autoMakeup{false};
    std::atomic<int> distortion{0};
    std::atomic<float> distortionAmount{50.0f};
    std::atomic<int> oversampling{1}; // 0=off, 1=2x, 2=4x
    std::atomic<float> globalLookahead{0.0f};

    std::atomic<float> optoPeakReduction{0.0f}, optoGain{kOptoGainUnityKnob};
    std::atomic<bool> optoLimit{false};

    std::atomic<float> fetInput{0.0f}, fetOutput{0.0f}, fetAttack{0.2f}, fetRelease{400.0f};
    std::atomic<int> fetRatio{0}, fetCurve{0};
    std::atomic<float> fetTransient{0.0f}, fetThreshold{-10.0f};

    std::atomic<float> vcaThreshold{0.0f}, vcaRatio{4.0f}, vcaAttack{1.0f}, vcaRelease{100.0f}, vcaOutput{0.0f};
    std::atomic<bool> vcaOverEasy{false}, vcaClassicDetector{false};

    std::atomic<float> busThreshold{0.0f}, busMakeup{0.0f}, busMix{100.0f};
    std::atomic<int> busRatio{0}, busAttack{2}, busRelease{1};

    std::atomic<float> studioVcaThreshold{-10.0f}, studioVcaRatio{3.0f}, studioVcaAttack{10.0f}, studioVcaRelease{300.0f}, studioVcaOutput{0.0f};

    std::atomic<float> digitalThreshold{-20.0f}, digitalRatio{4.0f}, digitalKnee{6.0f}, digitalAttack{10.0f}, digitalRelease{100.0f}, digitalLookahead{0.0f}, digitalMix{100.0f}, digitalOutput{0.0f};
    std::atomic<bool> digitalAdaptive{false};

    std::atomic<float> crossover1{200.0f}, crossover2{2000.0f}, crossover3{8000.0f};
    std::array<std::atomic<float>, kMultiCompBands> mbThreshold{{-20.0f, -20.0f, -20.0f, -20.0f}};
    std::array<std::atomic<float>, kMultiCompBands> mbRatio{{4.0f, 4.0f, 4.0f, 4.0f}};
    std::array<std::atomic<float>, kMultiCompBands> mbAttack{{10.0f, 10.0f, 10.0f, 10.0f}};
    std::array<std::atomic<float>, kMultiCompBands> mbRelease{{100.0f, 100.0f, 100.0f, 100.0f}};
    std::array<std::atomic<float>, kMultiCompBands> mbMakeup{{0.0f, 0.0f, 0.0f, 0.0f}};
    std::array<std::atomic<bool>, kMultiCompBands> mbBypass{{false, false, false, false}};
    std::array<std::atomic<bool>, kMultiCompBands> mbSolo{{false, false, false, false}};
    std::array<std::atomic<bool>, kMultiCompBands> mbEnabled{{true, true, true, true}};
};

} // namespace duskaudio

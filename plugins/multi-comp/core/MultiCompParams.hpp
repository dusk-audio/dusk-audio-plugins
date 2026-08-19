// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Framework-free parameter and enum definitions for Multi-Comp.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "../../shared-dpf/dsp/DuskFilters.hpp"

namespace duskaudio
{

enum class MultiCompMode : int { Opto = 0, FET, VCA, Bus, StudioFET, StudioVCA, Digital, Multiband };
enum class DistortionType : int { Off = 0, Soft, Hard, Clip };

constexpr int kMultiCompModes = 8;
constexpr int kMultiCompBands = 4;

namespace MultiCompConstants
{
constexpr float kT4BAttack = 0.002f, kT4BFastRelease = 0.060f, kT4BPhosphorDecay = 1.5f;
constexpr float kT4BAfterglowDecay = 5.0f, kT4BAfterglowAttackRatio = 0.25f, kT4BAfterglowCoupling = 0.12f;
constexpr float kT4BPhosphorAttackRatio = 0.3f, kT4BGamma = 0.7f, kT4BConductanceK = 3.0f;
constexpr float kT4BPhosphorCoupling = 0.40f, kT4BChargeRate = 0.15f, kT4BDischargeRate = 0.12f;
constexpr float kT4BReleaseScale = 5.0f, kT4BPhosphorScale = 3.0f;
constexpr float kSidechainDriverSaturation = 0.8f, kSidechainDriverOutput = 1.0f, kSidechainDriverThreshold = 0.03f;
constexpr float kPeakReductionMaxSidechainGain = 14.0f, kT4BMaxConductance = 6.0f, kT4BMaxReleaseRate = 10.0f;
constexpr float kFetThresholdDb = -10.0f, kFetMaxReductionDb = 30.0f, kVcaReleaseRate = 120.0f, kVcaMaxReductionDb = 60.0f;
constexpr float kBusSidechainHz = 60.0f, kBusMaxReductionDb = 20.0f, kBusKneeDb = 10.0f, kBusRmsSeconds = 0.005f;
constexpr float kStudioFetThresholdDb = -10.0f, kStudioFetHarmonicScale = 0.3f;
constexpr float kStudioVcaMaxReductionDb = 40.0f, kStudioVcaKneeDb = 6.0f;
constexpr float kSidechainHpMin = 20.0f, kSidechainHpMax = 500.0f, kSidechainHpDefault = 80.0f;
constexpr float kOutputHardLimit = 2.0f, kEpsilon = 0.0001f;
}

inline float clampFinite(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

// The Opto control is a hardware-style 0..100 dial.  50 is unity and each
// division is 0.8 dB, matching OptoGainMapping.h exactly.
inline float optoKnobToGainDb(float knob) noexcept
{
    const float clamped = std::clamp(knob, 0.0f, 100.0f);
    return std::clamp((clamped - 50.0f) * 0.8f, -40.0f, 40.0f);
}

inline float optoGainDbToKnob(float db) noexcept
{
    return std::clamp(50.0f + std::clamp(db, -40.0f, 40.0f) / 0.8f, 0.0f, 100.0f);
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

    std::atomic<float> optoPeakReduction{0.0f}, optoGain{50.0f};
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

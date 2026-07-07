// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQParams.hpp — framework-free enums, band configs, Butterworth-cascade Q
// table and Q-coupling, ported verbatim from EQBand.h (which pulls JuceHeader).
// Values and math are byte-identical to the JUCE build; only juce::String param
// IDs and juce::Colour (UI-only) are dropped.

#pragma once

#include <array>
#include <cmath>

namespace duskaudio
{

inline constexpr int kMultiQNumBands = 8;

enum class BandType
{
    HighPass = 0, LowShelf, Parametric, HighShelf, LowPass, Notch, BandPass
};

// dB/octave slope options for the HPF/LPF bands.
enum class FilterSlope
{
    Slope6dB = 0, Slope12dB, Slope18dB, Slope24dB, Slope36dB, Slope48dB, Slope72dB, Slope96dB
};

enum class QCoupleMode
{
    Off = 0, Proportional, Light, Medium, Strong,
    AsymmetricLight, AsymmetricMedium, AsymmetricStrong, Vintage
};

enum class ProcessingMode { Stereo = 0, Left, Right, Mid, Side };

enum class EQType { Digital = 0, Match = 1, British = 2, Tube = 3 };

// Butterworth Q per cascaded 2nd-order stage (verbatim from EQBand.h).
namespace ButterworthQ
{
    inline constexpr float value = 0.7071067811865476f; // 1/sqrt(2)

    inline constexpr float order2[]  = { 0.7071f };
    inline constexpr float order4[]  = { 0.5412f, 1.3066f };
    inline constexpr float order6[]  = { 0.5176f, 0.7071f, 1.9319f };
    inline constexpr float order8[]  = { 0.5098f, 0.6013f, 0.9000f, 2.5629f };
    inline constexpr float order12[] = { 0.5024f, 0.5412f, 0.6313f, 0.7071f, 1.0000f, 1.9319f };
    inline constexpr float order16[] = { 0.5006f, 0.5176f, 0.5612f, 0.6013f, 0.7071f, 0.9000f, 1.3066f, 2.5629f };

    inline float getStageQ(int totalSecondOrderStages, int stageIndex, float userQ)
    {
        const float* qValues = nullptr;
        switch (totalSecondOrderStages)
        {
            case 1: qValues = order2;  break;
            case 2: qValues = order4;  break;
            case 3: qValues = order6;  break;
            case 4: qValues = order8;  break;
            case 6: qValues = order12; break;
            case 8: qValues = order16; break;
            default: return userQ;
        }
        if (stageIndex < 0 || stageIndex >= totalSecondOrderStages)
            return userQ;
        float butterworthQ = qValues[stageIndex];
        float qScale = userQ / value;
        return butterworthQ * qScale;
    }
}

// Q-coupling (verbatim from EQBand.h getQCoupledValue).
inline float getQCoupledValue(float baseQ, float gainDB, QCoupleMode mode)
{
    if (mode == QCoupleMode::Off)
        return baseQ;

    float absGain = std::abs(gainDB);
    float strength = 0.0f;
    bool asymmetric = false;

    switch (mode)
    {
        case QCoupleMode::Off:              return baseQ;
        case QCoupleMode::Proportional:     strength = 0.15f; break;
        case QCoupleMode::Light:            strength = 0.05f; break;
        case QCoupleMode::Medium:           strength = 0.10f; break;
        case QCoupleMode::Strong:           strength = 0.20f; break;
        case QCoupleMode::AsymmetricLight:  strength = 0.05f; asymmetric = true; break;
        case QCoupleMode::AsymmetricMedium: strength = 0.10f; asymmetric = true; break;
        case QCoupleMode::AsymmetricStrong: strength = 0.20f; asymmetric = true; break;
        case QCoupleMode::Vintage:
        {
            float k = 0.03f;
            float p = 1.5f;
            return baseQ * (1.0f + k * std::pow(absGain, p));
        }
    }

    if (asymmetric && gainDB < 0)
        strength *= 1.5f;

    return baseQ * (1.0f + strength * absGain);
}

// Per-band default/limit frequencies (from DefaultBandConfigs; UI colour/name dropped).
struct BandConfig { BandType type; float defaultFreq, minFreq, maxFreq; };

inline constexpr std::array<BandConfig, kMultiQNumBands> kDefaultBandConfigs = {{
    { BandType::HighPass,      20.0f, 20.0f, 20000.0f },
    { BandType::LowShelf,     100.0f, 20.0f, 20000.0f },
    { BandType::Parametric,   200.0f, 20.0f, 20000.0f },
    { BandType::Parametric,   500.0f, 20.0f, 20000.0f },
    { BandType::Parametric,  1000.0f, 20.0f, 20000.0f },
    { BandType::Parametric,  2000.0f, 20.0f, 20000.0f },
    { BandType::HighShelf,   4000.0f, 20.0f, 20000.0f },
    { BandType::LowPass,    20000.0f, 20.0f, 20000.0f },
}};

// Linear ramp smoother — matches juce::SmoothedValue<float> (linear) semantics
// used for per-band enable crossfades. reset(sr, seconds) sets the ramp length;
// setTargetValue restarts the ramp; getNextValue advances one sample.
class LinearSmoothedValue
{
public:
    void reset(double sampleRate, double rampSeconds)
    {
        stepsToTarget = (int)(rampSeconds * sampleRate);
        if (stepsToTarget < 1) stepsToTarget = 1;
        // snap to target (JUCE resets to the current target with counter 0)
        countdown = 0;
        current = target;
    }
    void setCurrentAndTargetValue(float v) { current = target = v; countdown = 0; }
    void setTargetValue(float v)
    {
        if (v == target) return;
        target = v;
        countdown = stepsToTarget;
        increment = (target - current) / (float)stepsToTarget;
    }
    bool isSmoothing() const { return countdown > 0; }
    float getNextValue()
    {
        if (countdown <= 0) return target;
        current += increment;
        if (--countdown == 0) current = target;
        return current;
    }
    float getCurrentValue() const { return current; }

private:
    float current = 0.0f, target = 0.0f, increment = 0.0f;
    int countdown = 0, stepsToTarget = 1;
};

} // namespace duskaudio

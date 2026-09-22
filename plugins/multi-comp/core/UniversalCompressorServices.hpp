// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// UniversalCompressorServices.hpp — framework-free (no-JUCE) port of the shared
// DSP services used by the Opto / FET / VCA / Bus compressor modes, extracted
// from plugins/multi-comp/multicomp.cpp. Every constant, coefficient and
// sample-level op order is preserved from the JUCE original. Substitutions and
// judgment calls are recorded in core/PORT_NOTES.md.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace duskaudio
{

//==============================================================================
// Math constants + helpers replacing juce::MathConstants / juce::jlimit /
// juce::Decibels. Kept as free functions so the mode transcriptions read the
// same as the JUCE source (juce::jlimit(a,b,v) -> duskaudio::jlimit(a,b,v)).
//==============================================================================
constexpr float  kPiF    = 3.14159265358979323846f;
constexpr double kPiD    = 3.14159265358979323846;
constexpr float  kTwoPiF = 6.28318530717958647692f;
constexpr double kTwoPiD = 6.28318530717958647692;

template <typename T> inline T jmin (T a, T b) noexcept { return a < b ? a : b; }
template <typename T> inline T jmax (T a, T b) noexcept { return a > b ? a : b; }

// juce::jlimit(lowerBound, upperBound, value) — note the JUCE argument order.
template <typename T> inline T jlimit (T lo, T hi, T v) noexcept
{
    return v < lo ? lo : (hi < v ? hi : v);
}

// juce::Decibels::decibelsToGain (default minus-infinity dB == -100).
inline float dbToGain (float db) noexcept { return db > -100.0f ? std::pow (10.0f, db * 0.05f) : 0.0f; }

// juce::Decibels::gainToDecibels (default minus-infinity dB == -100).
inline float gainToDb (float gain) noexcept
{
    return gain > 0.0f ? jmax (-100.0f, std::log10 (gain) * 20.0f) : -100.0f;
}

// juce::approximatelyEqual(a, b) with the library-default tolerances
// (absolute = FLT_MIN, relative = FLT_EPSILON). Used by MultiplicativeSmoothedValue.
inline bool approximatelyEqual (float a, float b) noexcept
{
    if (! (std::isfinite (a) && std::isfinite (b)))
        return a == b;
    const float diff = std::abs (a - b);
    return diff <= std::numeric_limits<float>::min()
        || diff <= std::numeric_limits<float>::epsilon() * std::max (std::abs (a), std::abs (b));
}

//==============================================================================
// Verbatim port of the multicomp.cpp file-scope Constants namespace. The mode
// transcriptions reference these by the same names; keep the values identical.
//==============================================================================
namespace Constants
{
    // T4B Optical Cell — CdS photoresistor + electroluminescent panel
    constexpr float T4B_ATTACK_TIME = 0.002f;
    constexpr float T4B_FAST_RELEASE_TIME = 0.060f;
    constexpr float T4B_PHOSPHOR_BASE_DECAY = 1.5f;
    constexpr float T4B_PHOSPHOR_AFTERGLOW_DECAY = 5.0f;
    constexpr float T4B_PHOSPHOR_AFTERGLOW_ATTACK_RATIO = 0.25f;
    constexpr float T4B_PHOSPHOR_AFTERGLOW_COUPLING = 0.12f;
    constexpr float T4B_PHOSPHOR_ATTACK_RATIO = 0.3f;
    constexpr float T4B_GAMMA = 0.7f;
    constexpr float T4B_CONDUCTANCE_K = 3.0f;
    constexpr float T4B_PHOSPHOR_COUPLING = 0.40f;
    constexpr float T4B_PROG_DEP_CHARGE_RATE = 0.15f;
    constexpr float T4B_PROG_DEP_DISCHARGE_RATE = 0.12f;
    constexpr float T4B_PROG_DEP_RELEASE_SCALE = 5.0f;
    constexpr float T4B_PROG_DEP_PHOSPHOR_SCALE = 3.0f;
    constexpr float SC_DRIVER_SATURATION = 0.8f;
    constexpr float SC_DRIVER_OUTPUT_SCALE = 1.0f;
    constexpr float T4B_EL_PANEL_ATTACK_FREQ = 150.0f;
    constexpr float T4B_EL_PANEL_RELEASE_FREQ = 5.0f;
    constexpr float T4B_CONDUCTANCE_ATTACK_FREQ = 150.0f;
    constexpr float T4B_CONDUCTANCE_RELEASE_FREQ = 4.0f;
    constexpr float SC_DRIVER_THRESHOLD = 0.03f;
    constexpr float SC_LEVEL_SMOOTH_FREQ = 800.0f;
    constexpr float PEAK_REDUCTION_MAX_SC_GAIN = 14.0f;
    constexpr float T4B_MAX_CONDUCTANCE = 6.0f;
    constexpr float T4B_MAX_GAIN_RELEASE_RATE = 10.0f;

    // Vintage FET constants
    constexpr float FET_THRESHOLD_DB = -10.0f;
    constexpr float FET_MAX_REDUCTION_DB = 30.0f;
    constexpr float FET_ALLBUTTONS_MIN_ATTACK = 0.0002f;
    constexpr float FET_ALLBUTTONS_MAX_ATTACK = 0.002f;

    // Classic VCA constants
    constexpr float VCA_RMS_TIME_CONSTANT = 0.003f;
    constexpr float VCA_RELEASE_RATE = 120.0f;
    constexpr float VCA_MAX_REDUCTION_DB = 60.0f;

    // Bus Compressor constants
    constexpr float BUS_SIDECHAIN_HP_FREQ = 60.0f;
    constexpr float BUS_MAX_REDUCTION_DB = 20.0f;
    constexpr float BUS_OVEREASY_KNEE_WIDTH = 10.0f;
    constexpr float BUS_RMS_TIME = 0.005f;

    // Studio FET constants (out of port scope; kept for constant parity)
    constexpr float STUDIO_FET_THRESHOLD_DB = -10.0f;
    constexpr float STUDIO_FET_HARMONIC_SCALE = 0.3f;

    // Studio VCA constants (out of port scope; kept for constant parity)
    constexpr float STUDIO_VCA_MAX_REDUCTION_DB = 40.0f;
    constexpr float STUDIO_VCA_SOFT_KNEE_DB = 6.0f;

    // Global sidechain highpass filter frequency (user-adjustable)
    constexpr float SIDECHAIN_HP_MIN = 20.0f;
    constexpr float SIDECHAIN_HP_MAX = 500.0f;
    constexpr float SIDECHAIN_HP_DEFAULT = 80.0f;

    // Anti-aliasing (internal OS is out of scope; kept for constant parity)
    constexpr float NYQUIST_SAFETY_FACTOR = 0.4f;
    constexpr float MAX_CUTOFF_FREQ = 20000.0f;

    // Safety limits
    constexpr float OUTPUT_HARD_LIMIT = 2.0f;
    constexpr float EPSILON = 0.0001f;
}

//==============================================================================
// MultiplicativeSmoothedValue — verbatim behavioural port of
// juce::SmoothedValue<float, ValueSmoothingTypes::Multiplicative>.
//
// Drives the GR-based auto-makeup gain in UniversalCompressorDSP::processBlock.
// Ramp semantics (step = geometric factor, countdown in samples) match JUCE
// exactly so the smoothed makeup curve is bit-identical.
//==============================================================================
class MultiplicativeSmoothedValue
{
public:
    MultiplicativeSmoothedValue() = default;
    explicit MultiplicativeSmoothedValue (float initial) noexcept : currentValue (initial), target (initial) {}

    void reset (double sampleRate, double rampLengthInSeconds) noexcept
    {
        reset ((int) std::floor (rampLengthInSeconds * sampleRate));
    }

    void reset (int numSteps) noexcept
    {
        stepsToTarget = numSteps;
        setCurrentAndTargetValue (target);
    }

    void setCurrentAndTargetValue (float newValue) noexcept
    {
        target = currentValue = newValue;
        countdown = 0;
    }

    void setTargetValue (float newValue) noexcept
    {
        if (approximatelyEqual (newValue, target))
            return;

        if (stepsToTarget <= 0)
        {
            setCurrentAndTargetValue (newValue);
            return;
        }

        target = newValue;
        countdown = stepsToTarget;
        step = std::exp ((std::log (std::abs (target)) - std::log (std::abs (currentValue))) / (float) countdown);
    }

    bool  isSmoothing()     const noexcept { return countdown > 0; }
    float getCurrentValue() const noexcept { return currentValue; }
    float getTargetValue()  const noexcept { return target; }

    float getNextValue() noexcept
    {
        if (! isSmoothing())
            return target;

        --countdown;

        if (isSmoothing())
            currentValue *= step;
        else
            currentValue = target;

        return currentValue;
    }

private:
    float currentValue = 1.0f;
    float target = 1.0f;
    float step = 0.0f;
    int   countdown = 0;
    int   stepsToTarget = 0;
};

//==============================================================================
// SidechainFilter — Butterworth (Q = 0.707) highpass, transposed Direct-Form-II.
// Verbatim port of UniversalCompressor::SidechainFilter (multicomp.cpp L442).
// Prepared at the NATIVE sample rate. Engaged only when the sidechain_hp
// slider is >= 1 Hz; below that the caller bypasses it with a memcpy.
//==============================================================================
class SidechainFilter
{
public:
    void prepare (double sr, int numChannels)
    {
        sampleRate = sr;
        filterStates.resize ((size_t) numChannels);
        for (auto& state : filterStates) { state.z1 = 0.0f; state.z2 = 0.0f; }
        updateCoefficients (Constants::SIDECHAIN_HP_DEFAULT);
    }

    void reset()
    {
        for (auto& state : filterStates) { state.z1 = 0.0f; state.z2 = 0.0f; }
    }

    void setFrequency (float freq)
    {
        freq = jlimit (Constants::SIDECHAIN_HP_MIN, Constants::SIDECHAIN_HP_MAX, freq);
        if (std::abs (freq - currentFreq) > 0.1f)
            updateCoefficients (freq);
    }

    float process (float input, int channel)
    {
        if (channel < 0 || channel >= (int) filterStates.size())
            return input;

        auto& state = filterStates[(size_t) channel];
        float output = b0 * input + state.z1;
        state.z1 = b1 * input - a1 * output + state.z2;
        state.z2 = b2 * input - a2 * output;
        return output;
    }

    void processBlock (const float* input, float* output, int numSamples, int channel)
    {
        if (channel < 0 || channel >= (int) filterStates.size())
        {
            if (input != output)
                std::memcpy (output, input, (size_t) numSamples * sizeof (float));
            return;
        }

        auto& state = filterStates[(size_t) channel];
        const float lb0 = b0, lb1 = b1, lb2 = b2;
        const float la1 = a1, la2 = a2;
        float z1 = state.z1, z2 = state.z2;

        int i = 0;
        for (; i + 4 <= numSamples; i += 4)
        {
            float out0 = lb0 * input[i] + z1;
            z1 = lb1 * input[i] - la1 * out0 + z2;
            z2 = lb2 * input[i] - la2 * out0;
            output[i] = out0;

            float out1 = lb0 * input[i+1] + z1;
            z1 = lb1 * input[i+1] - la1 * out1 + z2;
            z2 = lb2 * input[i+1] - la2 * out1;
            output[i+1] = out1;

            float out2 = lb0 * input[i+2] + z1;
            z1 = lb1 * input[i+2] - la1 * out2 + z2;
            z2 = lb2 * input[i+2] - la2 * out2;
            output[i+2] = out2;

            float out3 = lb0 * input[i+3] + z1;
            z1 = lb1 * input[i+3] - la1 * out3 + z2;
            z2 = lb2 * input[i+3] - la2 * out3;
            output[i+3] = out3;
        }
        for (; i < numSamples; ++i)
        {
            float out = lb0 * input[i] + z1;
            z1 = lb1 * input[i] - la1 * out + z2;
            z2 = lb2 * input[i] - la2 * out;
            output[i] = out;
        }

        state.z1 = z1;
        state.z2 = z2;
    }

    float getFrequency() const { return currentFreq; }

private:
    void updateCoefficients (float freq)
    {
        currentFreq = freq;
        if (sampleRate <= 0.0) return;

        const float omega = 2.0f * kPiF * freq / (float) sampleRate;
        const float cosOmega = std::cos (omega);
        const float sinOmega = std::sin (omega);
        const float alpha = sinOmega / (2.0f * 0.707f);
        const float a0_inv = 1.0f / (1.0f + alpha);

        b0 = ((1.0f + cosOmega) / 2.0f) * a0_inv;
        b1 = -(1.0f + cosOmega) * a0_inv;
        b2 = b0;
        a1 = (-2.0f * cosOmega) * a0_inv;
        a2 = (1.0f - alpha) * a0_inv;
    }

    struct FilterState { float z1 = 0.0f; float z2 = 0.0f; };

    std::vector<FilterState> filterStates;
    double sampleRate = 44100.0;
    float currentFreq = Constants::SIDECHAIN_HP_DEFAULT;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

//==============================================================================
// TransientShaper — verbatim port of UniversalCompressor::TransientShaper
// (multicomp.cpp L901). Prepared at the NATIVE sample rate. Only the FET mode
// consumes it (via a pointer); at fet_transient == 0 it is a no-op modifier.
//==============================================================================
class TransientShaper
{
public:
    void prepare (double sr, int numChannels)
    {
        sampleRate = sr;
        channels.resize ((size_t) numChannels);
        for (auto& ch : channels) { ch.fastEnvelope = 0.0f; ch.slowEnvelope = 0.0f; ch.peakHold = 0.0f; ch.holdCounter = 0; }

        fastAttackCoeff  = std::exp (-1.0f / (0.0005f * (float) sampleRate));
        fastReleaseCoeff = std::exp (-1.0f / (0.020f  * (float) sampleRate));
        slowAttackCoeff  = std::exp (-1.0f / (0.010f  * (float) sampleRate));
        slowReleaseCoeff = std::exp (-1.0f / (0.100f  * (float) sampleRate));
        holdSamples = (int) (0.005f * (float) sampleRate);
    }

    float process (float input, int channel, float sensitivity)
    {
        if (channel < 0 || channel >= (int) channels.size())
            return 1.0f;

        auto& ch = channels[(size_t) channel];
        float absInput = std::abs (input);

        if (absInput > ch.fastEnvelope)
            ch.fastEnvelope = fastAttackCoeff * ch.fastEnvelope + (1.0f - fastAttackCoeff) * absInput;
        else
            ch.fastEnvelope = fastReleaseCoeff * ch.fastEnvelope + (1.0f - fastReleaseCoeff) * absInput;

        if (absInput > ch.slowEnvelope)
            ch.slowEnvelope = slowAttackCoeff * ch.slowEnvelope + (1.0f - slowAttackCoeff) * absInput;
        else
            ch.slowEnvelope = slowReleaseCoeff * ch.slowEnvelope + (1.0f - slowReleaseCoeff) * absInput;

        if (absInput > ch.peakHold)
        {
            ch.peakHold = absInput;
            ch.holdCounter = holdSamples;
        }
        else if (ch.holdCounter > 0)
        {
            ch.holdCounter--;
        }
        else
        {
            ch.peakHold = ch.peakHold * 0.9995f;
        }

        float transientRatio = 1.0f;
        if (ch.slowEnvelope > 0.0001f)
            transientRatio = ch.fastEnvelope / ch.slowEnvelope;

        float normalizedSensitivity = sensitivity / 100.0f;
        float transientModifier = 1.0f;

        if (transientRatio > 1.0f)
        {
            float transientAmount = jmin ((transientRatio - 1.0f) * 2.0f, 2.0f);
            transientModifier = 1.0f + transientAmount * normalizedSensitivity;
        }

        return transientModifier;
    }

    void reset()
    {
        for (auto& ch : channels) { ch.fastEnvelope = 0.0f; ch.slowEnvelope = 0.0f; ch.peakHold = 0.0f; ch.holdCounter = 0; }
    }

private:
    struct Channel { float fastEnvelope = 0.0f; float slowEnvelope = 0.0f; float peakHold = 0.0f; int holdCounter = 0; };

    std::vector<Channel> channels;
    double sampleRate = 44100.0;
    float fastAttackCoeff = 0.0f, fastReleaseCoeff = 0.0f;
    float slowAttackCoeff = 0.0f, slowReleaseCoeff = 0.0f;
    int holdSamples = 0;
};

//==============================================================================
// LookupTables — verbatim port of UniversalCompressor::LookupTables
// (declaration in UniversalCompressor.h L304; initialize()/fastExp/fastLog/
// getAllButtonsReduction bodies at multicomp.cpp L4834). Consumed by the FET
// mode (fast exp/log + all-buttons transfer curve). Rate-independent — build
// once in prepare().
//==============================================================================
class LookupTables
{
public:
    static constexpr int TABLE_SIZE = 4096;
    static constexpr int ALLBUTTONS_TABLE_SIZE = 512;

    std::array<float, TABLE_SIZE> expTable {};
    std::array<float, TABLE_SIZE> logTable {};
    std::array<float, ALLBUTTONS_TABLE_SIZE> allButtonsModernCurve {};
    std::array<float, ALLBUTTONS_TABLE_SIZE> allButtonsMeasuredCurve {};

    void initialize()
    {
        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = -4.0f + (4.0f * i / (float) (TABLE_SIZE - 1));
            expTable[(size_t) i] = std::exp (x);
        }

        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = 0.0001f + (0.9999f * i / (float) (TABLE_SIZE - 1));
            logTable[(size_t) i] = std::log (x);
        }

        constexpr float measuredPoints[][2] = {
            {0.0f, 0.0f}, {1.0f, 0.9f}, {2.0f, 1.9f}, {4.0f, 3.85f}, {6.0f, 5.8f},
            {8.0f, 7.8f}, {10.0f, 9.8f}, {15.0f, 14.8f}, {20.0f, 19.7f}, {30.0f, 29.5f}
        };
        constexpr int numPoints = sizeof (measuredPoints) / sizeof (measuredPoints[0]);

        for (int i = 0; i < ALLBUTTONS_TABLE_SIZE; ++i)
        {
            float overThreshDb = 30.0f * (float) i / (float) (ALLBUTTONS_TABLE_SIZE - 1);

            if (overThreshDb < 1.0f)
            {
                float t = overThreshDb;
                allButtonsModernCurve[(size_t) i] = overThreshDb * (0.7f + t * 0.28f);
            }
            else
            {
                allButtonsModernCurve[(size_t) i] = 0.98f + (overThreshDb - 1.0f) * 0.99f;
            }
            allButtonsModernCurve[(size_t) i] = jmin (allButtonsModernCurve[(size_t) i], 30.0f);

            float measuredReduction = 0.0f;
            for (int p = 0; p < numPoints - 1; ++p)
            {
                if (overThreshDb >= measuredPoints[p][0] && overThreshDb <= measuredPoints[p + 1][0])
                {
                    float t = (overThreshDb - measuredPoints[p][0]) /
                              (measuredPoints[p + 1][0] - measuredPoints[p][0]);
                    measuredReduction = measuredPoints[p][1] + t * (measuredPoints[p + 1][1] - measuredPoints[p][1]);
                    break;
                }
            }
            if (overThreshDb > measuredPoints[numPoints - 1][0])
                measuredReduction = measuredPoints[numPoints - 1][1];
            allButtonsMeasuredCurve[(size_t) i] = measuredReduction;
        }
    }

    float fastExp (float x) const
    {
        x = jlimit (-4.0f, 0.0f, x);
        int index = (int) ((x + 4.0f) * (TABLE_SIZE - 1) / 4.0f);
        index = jlimit (0, TABLE_SIZE - 1, index);
        return expTable[(size_t) index];
    }

    float fastLog (float x) const
    {
        x = jlimit (0.0001f, 1.0f, x);
        int index = (int) ((x - 0.0001f) * (TABLE_SIZE - 1) / 0.9999f);
        index = jlimit (0, TABLE_SIZE - 1, index);
        return logTable[(size_t) index];
    }

    float getAllButtonsReduction (float overThreshDb, bool useMeasuredCurve) const
    {
        overThreshDb = jlimit (0.0f, 30.0f, overThreshDb);
        float indexFloat = overThreshDb * (float) (ALLBUTTONS_TABLE_SIZE - 1) / 30.0f;
        int index0 = (int) indexFloat;
        int index1 = jmin (index0 + 1, ALLBUTTONS_TABLE_SIZE - 1);
        float frac = indexFloat - (float) index0;
        const auto& curve = useMeasuredCurve ? allButtonsMeasuredCurve : allButtonsModernCurve;
        return curve[(size_t) index0] + frac * (curve[(size_t) index1] - curve[(size_t) index0]);
    }
};

} // namespace duskaudio

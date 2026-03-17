/*
  ==============================================================================

    TubeEQProcessor.h

    Vintage tube EQ processor for Multi-Q's Tube mode.

    Passive LC network topology with tube makeup gain stage.
    Boost/cut interaction creates characteristic frequency response curves.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../shared/AnalogEmulation/AnalogEmulation.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include "SafeFloat.h"
#include <random>

// Helper for LC filter pre-warping (clamp omega to avoid tan() blowup near Nyquist)
inline float tubeEQPreWarpFrequency(float freq, double sampleRate)
{
    float omega = juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
    omega = std::min(omega, juce::MathConstants<float>::halfPi - 0.001f);
    return static_cast<float>(sampleRate) / juce::MathConstants<float>::pi * std::tan(omega);
}

/** Inductor model for LC network emulation with frequency-dependent Q,
    core saturation, and hysteresis. */
class InductorModel
{
public:
    void prepare(double sampleRate, uint32_t characterSeed = 0)
    {
        this->sampleRate = sampleRate;
        computeDecayCoefficients(sampleRate);
        reset();

        // Random variation of ±5% on Q and ±2% on saturation threshold
        // Use deterministic seed for reproducibility across sessions
        // Default seed based on sample rate for consistent character
        uint32_t seed = (characterSeed != 0) ? characterSeed
                                             : static_cast<uint32_t>(sampleRate * 1000.0);
        std::mt19937 gen(seed);
        std::uniform_real_distribution<float> qDist(0.95f, 1.05f);
        std::uniform_real_distribution<float> satDist(0.98f, 1.02f);
        componentQVariation = qDist(gen);
        componentSatVariation = satDist(gen);
    }

    void reset()
    {
        prevInput = 0.0f;
        prevOutput = 0.0f;
        hysteresisState = 0.0f;
        coreFlux = 0.0f;
        rmsLevel = 0.0f;
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread.
        Does NOT regenerate component variations (seed-dependent, not rate-dependent). */
    void updateSampleRate(double newRate)
    {
        sampleRate = newRate;
        computeDecayCoefficients(newRate);
        reset();
    }

    /** Get frequency-dependent Q (models core losses at LF, skin effect at HF). */
    float getFrequencyDependentQ(float frequency, float baseQ) const
    {
        float qMultiplier;

        // Piecewise linear interpolation of measured Q curve
        if (frequency < 20.0f)
        {
            qMultiplier = 0.5f;  // Very lossy at subsonic
        }
        else if (frequency < 60.0f)
        {
            // 20 Hz (0.5) to 60 Hz (0.75) - core losses dominate
            float t = (frequency - 20.0f) / 40.0f;
            qMultiplier = 0.5f + t * 0.25f;
        }
        else if (frequency < 100.0f)
        {
            // 60 Hz (0.75) to 100 Hz (0.9)
            float t = (frequency - 60.0f) / 40.0f;
            qMultiplier = 0.75f + t * 0.15f;
        }
        else if (frequency < 300.0f)
        {
            // 100 Hz (0.9) to 300 Hz (1.0) - optimal range
            float t = (frequency - 100.0f) / 200.0f;
            qMultiplier = 0.9f + t * 0.1f;
        }
        else if (frequency < 1000.0f)
        {
            // 300 Hz (1.0) to 1 kHz (0.85) - gentle rolloff
            float t = (frequency - 300.0f) / 700.0f;
            qMultiplier = 1.0f - t * 0.15f;
        }
        else if (frequency < 3000.0f)
        {
            // 1 kHz (0.85) to 3 kHz (0.7) - skin effect begins
            float t = (frequency - 1000.0f) / 2000.0f;
            qMultiplier = 0.85f - t * 0.15f;
        }
        else if (frequency < 10000.0f)
        {
            // 3 kHz (0.7) to 10 kHz (0.5)
            float t = (frequency - 3000.0f) / 7000.0f;
            qMultiplier = 0.7f - t * 0.2f;
        }
        else
        {
            // Above 10 kHz - significant losses
            float t = std::min((frequency - 10000.0f) / 10000.0f, 1.0f);
            qMultiplier = 0.5f - t * 0.2f;
        }

        return baseQ * qMultiplier * componentQVariation;
    }

    /** Process inductor non-linearity: B-H curve saturation + hysteresis. */
    float processNonlinearity(float input, float driveLevel)
    {
        if (!safeIsFinite(input))
            return 0.0f;

        // Track RMS level for program-dependent behavior (45ms time constant)
        rmsLevel = rmsLevel * rmsDecay + input * input * (1.0f - rmsDecay);
        float rmsValue = std::sqrt(rmsLevel);

        // Adjust saturation threshold based on program level
        // Hot signals cause more compression (core heating simulation)
        float dynamicThreshold = (0.65f - rmsValue * 0.15f) * componentSatVariation;
        dynamicThreshold = std::max(dynamicThreshold, 0.35f);

        float saturatedInput = input;
        float absInput = std::abs(input);

        if (absInput > dynamicThreshold)
        {
            float excess = (absInput - dynamicThreshold) / (1.0f - dynamicThreshold);
            float langevin = std::tanh(excess * 2.5f * (1.0f + driveLevel));

            // Blend original with saturated
            float compressed = dynamicThreshold + langevin * (1.0f - dynamicThreshold) * 0.7f;
            saturatedInput = std::copysign(compressed, input);

            // Add 2nd harmonic (core asymmetry)
            float h2Amount = 0.03f * driveLevel * excess;
            saturatedInput += h2Amount * input * absInput;

            // Add subtle 3rd harmonic at high drive
            float h3Amount = 0.008f * driveLevel * driveLevel * excess;
            saturatedInput += h3Amount * input * input * input;
        }

        // Hysteresis
        float deltaInput = saturatedInput - prevInput;
        float hysteresisCoeff = 0.08f * driveLevel;

        // Core flux integration with decay (0.75ms time constant)
        coreFlux = coreFlux * fluxDecay + deltaInput * hysteresisCoeff;
        coreFlux = std::clamp(coreFlux, -0.15f, 0.15f);

        // Hysteresis adds slight asymmetry based on flux direction (0.28ms time constant)
        hysteresisState = hysteresisState * hystDecay + coreFlux * (1.0f - hystDecay);
        float output = saturatedInput + hysteresisState * 0.5f;

        prevInput = input;
        prevOutput = output;

        return output;
    }

    float getRmsLevel() const { return std::sqrt(rmsLevel); }

private:
    void computeDecayCoefficients(double sr)
    {
        rmsDecay = std::exp(-1.0f / (0.045f * static_cast<float>(sr)));       // 45ms RMS integration
        fluxDecay = std::exp(-1.0f / (0.00075f * static_cast<float>(sr)));    // 0.75ms core flux
        hystDecay = std::exp(-1.0f / (0.00028f * static_cast<float>(sr)));    // 0.28ms hysteresis
    }

    double sampleRate = 44100.0;
    float prevInput = 0.0f;
    float prevOutput = 0.0f;
    float hysteresisState = 0.0f;
    float coreFlux = 0.0f;
    float rmsLevel = 0.0f;

    // Sample-rate-dependent decay coefficients
    float rmsDecay = 0.9995f;
    float fluxDecay = 0.97f;
    float hystDecay = 0.92f;

    // Component tolerance variation (vintage unit character)
    float componentQVariation = 1.0f;
    float componentSatVariation = 1.0f;
};

/** Tube stage model: triode gain stage + cathode follower output. */
class TubeEQTubeStage
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        for (auto& dc : dcBlockers)
            dc.prepare(sampleRate, 8.0f);

        // Slew rate limiting coefficient
        maxSlewRate = static_cast<float>(150000.0 / sampleRate);  // ~150V/ms typical

        computeDecayCoefficients(sampleRate);
        reset();
    }

    void reset()
    {
        prevSamples.fill(0.0f);
        cathodeVoltages.fill(0.0f);
        gridCurrents.fill(0.0f);
        for (auto& dc : dcBlockers)
            dc.reset();
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread. */
    void updateSampleRate(double newRate)
    {
        sampleRate = newRate;
        maxSlewRate = static_cast<float>(150000.0 / newRate);
        computeDecayCoefficients(newRate);
        reset();
    }

    void setDrive(float newDrive)
    {
        drive = juce::jlimit(0.0f, 1.0f, newDrive);
    }

    float processSample(float input, int channel)
    {
        if (drive < 0.01f)
            return input;

        int ch = std::clamp(channel, 0, maxChannels - 1);
        float& prevSample = prevSamples[static_cast<size_t>(ch)];
        float& cathodeVoltage = cathodeVoltages[static_cast<size_t>(ch)];
        float& gridCurrent = gridCurrents[static_cast<size_t>(ch)];

        float driveGain = 1.0f + drive * 4.0f;
        float drivenSignal = input * driveGain;

        // Grid current limiting
        float gridBias = -1.5f;
        float effectiveGrid = drivenSignal + gridBias;

        float gridCurrentAmount = std::max(0.0f, effectiveGrid + 1.5f) * 0.15f;
        gridCurrent = gridCurrent * gridDecay + gridCurrentAmount * (1.0f - gridDecay);

        float compressionFactor = 1.0f / (1.0f + gridCurrent * drive * 2.0f);

        // Triode transfer curve (asymmetric)
        float Vg = drivenSignal;
        float plateVoltage;
        if (Vg >= 0.0f)
        {
            // Positive half: grid loading and soft saturation
            float x = Vg * compressionFactor;
            if (x < 0.4f)
                plateVoltage = x * 1.05f;  // Slight gain in linear region
            else if (x < 0.8f)
            {
                // Gentle saturation with 2nd harmonic generation
                float t = (x - 0.4f) / 0.4f;
                plateVoltage = 0.42f + 0.38f * (t - 0.15f * t * t);
            }
            else
            {
                // Plate saturation region
                float t = x - 0.8f;
                plateVoltage = 0.78f + 0.15f * std::tanh(t * 2.0f);
            }
        }
        else
        {
            // Negative half: cutoff region behavior
            float x = -Vg * compressionFactor;
            if (x < 0.3f)
                plateVoltage = -x * 0.95f;  // Slightly less gain
            else if (x < 0.7f)
            {
                // Earlier saturation on negative half (asymmetric bias)
                float t = (x - 0.3f) / 0.4f;
                plateVoltage = -(0.285f + 0.35f * (t - 0.2f * t * t));
            }
            else
            {
                // Approaching cutoff
                float t = x - 0.7f;
                plateVoltage = -(0.62f + 0.2f * std::tanh(t * 3.0f));
            }
        }

        // Cathode follower output
        float cathodeBypassFreq = 20.0f;
        float cathodeAlpha = static_cast<float>(
            1.0 - std::exp(-juce::MathConstants<double>::twoPi * cathodeBypassFreq / sampleRate));

        cathodeVoltage = cathodeVoltage + (plateVoltage - cathodeVoltage) * cathodeAlpha;
        float cfOutput = plateVoltage * 0.95f + cathodeVoltage * 0.05f;

        // Cathode follower asymmetry (grid-cathode diode effect)
        if (cfOutput > 0.9f)
        {
            float excess = cfOutput - 0.9f;
            cfOutput = 0.9f + 0.08f * std::tanh(excess * 3.0f);
        }

        // Harmonic content
        float h2 = 0.04f * drive * cfOutput * std::abs(cfOutput);
        float h3 = 0.015f * drive * cfOutput * cfOutput * cfOutput;  // 3rd harmonic
        float h4 = 0.005f * drive * std::abs(cfOutput * cfOutput * cfOutput * cfOutput)
                   * std::copysign(1.0f, cfOutput);  // 4th harmonic

        float output = cfOutput + h2 + h3 + h4;

        // Slew rate limiting
        float deltaV = output - prevSample;
        if (std::abs(deltaV) > maxSlewRate)
        {
            output = prevSample + std::copysign(maxSlewRate, deltaV);
        }

        // Makeup gain
        output *= (1.0f / driveGain) * (1.0f + drive * 0.4f);

        // DC blocking
        output = dcBlockers[static_cast<size_t>(ch)].processSample(output);

        prevSample = output;

        return output;
    }

private:
    void computeDecayCoefficients(double sr)
    {
        gridDecay = std::exp(-1.0f / (0.00021f * static_cast<float>(sr)));  // 0.21ms grid current
    }

    static constexpr int maxChannels = 8;
    double sampleRate = 44100.0;
    float drive = 0.3f;
    float maxSlewRate = 0.003f;
    float gridDecay = 0.9f;  // Sample-rate-dependent grid current decay

    // Per-channel state (indexed by channel)
    std::array<float, maxChannels> prevSamples{};
    std::array<float, maxChannels> cathodeVoltages{};
    std::array<float, maxChannels> gridCurrents{};

    std::array<AnalogEmulation::DCBlocker, maxChannels> dcBlockers;
};

/** Vintage passive EQ style LF section with dual-biquad boost/cut interaction.
    Peak filter for boost + low shelf for attenuation, with inductor nonlinearity
    applied between stages for authentic passive LC network behavior. */
class PultecLFSection
{
public:
    // Tunable interaction constants (named for easy adjustment)
    static constexpr float kPeakGainScale = 1.4f;
    static constexpr float kPeakInteraction = 0.08f;
    static constexpr float kBaseQ = 0.55f;
    static constexpr float kQInteraction = 0.015f;
    static constexpr float kDipFreqBase = 0.55f;
    static constexpr float kDipFreqRange = 0.15f;
    static constexpr float kDipGainScale = 1.6f;
    static constexpr float kDipInteraction = 0.06f;
    static constexpr float kDipBaseQ = 0.5f;
    static constexpr float kDipQScale = 0.03f;

    void prepare(double sampleRate, uint32_t characterSeed = 0)
    {
        currentSampleRate = sampleRate;
        inductor.prepare(sampleRate, characterSeed);
        reset();
    }

    void reset()
    {
        for (auto& ch : channels)
        {
            ch.peakZ1 = ch.peakZ2 = 0.0f;
            ch.dipZ1 = ch.dipZ2 = 0.0f;
        }
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread. */
    void updateSampleRate(double newRate)
    {
        currentSampleRate = newRate;
        inductor.updateSampleRate(newRate);
        reset();
    }

    void updateCoefficients(float boostGain, float attenGain, float frequency,
                            double sampleRate)
    {
        cachedBoost = boostGain;
        cachedAtten = attenGain;
        cachedFreq = frequency;

        float maxFreq = static_cast<float>(sampleRate) * 0.45f;
        frequency = std::clamp(frequency, 10.0f, maxFreq);

        const float twoPi = juce::MathConstants<float>::twoPi;

        // ---- Peak filter (resonant boost) ----
        if (boostGain > 0.01f)
        {
            float peakGainDB = boostGain * kPeakGainScale + attenGain * boostGain * kPeakInteraction;
            float effectiveQ = inductor.getFrequencyDependentQ(frequency, kBaseQ);
            effectiveQ *= (1.0f + attenGain * kQInteraction);
            effectiveQ = std::max(effectiveQ, 0.2f);

            float A = std::pow(10.0f, peakGainDB / 40.0f);
            float w0 = twoPi * frequency / static_cast<float>(sampleRate);
            float cosw0 = std::cos(w0);
            float sinw0 = std::sin(w0);
            float alpha = sinw0 / (2.0f * effectiveQ);

            float b0 = 1.0f + alpha * A;
            float b1 = -2.0f * cosw0;
            float b2 = 1.0f - alpha * A;
            float a0 = 1.0f + alpha / A;
            float a1 = -2.0f * cosw0;
            float a2 = 1.0f - alpha / A;

            peakB0 = b0/a0; peakB1 = b1/a0; peakB2 = b2/a0;
            peakA1 = a1/a0; peakA2 = a2/a0;
        }
        else
        {
            peakB0 = 1.0f; peakB1 = 0.0f; peakB2 = 0.0f;
            peakA1 = 0.0f; peakA2 = 0.0f;
        }

        // ---- Dip shelf (attenuation with interaction) ----
        if (attenGain > 0.01f)
        {
            float dipFreqRatio = kDipFreqBase + kDipFreqRange * (1.0f - attenGain / 10.0f);
            float dipFreq = frequency * dipFreqRatio;
            dipFreq = std::clamp(dipFreq, 10.0f, maxFreq);

            float dipGainDB = -(attenGain * kDipGainScale + boostGain * attenGain * kDipInteraction);
            float dipQ = kDipBaseQ + attenGain * kDipQScale;

            float A = std::pow(10.0f, dipGainDB / 40.0f);
            float w0 = twoPi * dipFreq / static_cast<float>(sampleRate);
            float cosw0 = std::cos(w0);
            float sinw0 = std::sin(w0);
            float alpha = sinw0 / (2.0f * dipQ);
            float sqrtA = std::sqrt(A);

            float b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
            float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
            float b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
            float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
            float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
            float a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha;

            dipB0 = b0/a0; dipB1 = b1/a0; dipB2 = b2/a0;
            dipA1 = a1/a0; dipA2 = a2/a0;
        }
        else
        {
            dipB0 = 1.0f; dipB1 = 0.0f; dipB2 = 0.0f;
            dipA1 = 0.0f; dipA2 = 0.0f;
        }
    }

    float processSample(float input, int channel)
    {
        int ch = std::clamp(channel, 0, maxChannels - 1);
        auto& s = channels[ch];

        // Peak filter (Direct Form II Transposed)
        float peakOut = peakB0 * input + s.peakZ1;
        s.peakZ1 = peakB1 * input - peakA1 * peakOut + s.peakZ2;
        s.peakZ2 = peakB2 * input - peakA2 * peakOut;

        // Inductor nonlinearity between stages (subtle saturation)
        float interStage = (cachedBoost > 0.01f)
            ? inductor.processNonlinearity(peakOut, cachedBoost * 0.3f)
            : peakOut;

        // Dip shelf filter
        float dipOut = dipB0 * interStage + s.dipZ1;
        s.dipZ1 = dipB1 * interStage - dipA1 * dipOut + s.dipZ2;
        s.dipZ2 = dipB2 * interStage - dipA2 * dipOut;

        // State clamping for safety
        auto clampState = [](float& v) { v = std::clamp(v, -8.0f, 8.0f); };
        clampState(s.peakZ1); clampState(s.peakZ2);
        clampState(s.dipZ1); clampState(s.dipZ2);

        return safeIsFinite(dipOut) ? dipOut : input;
    }

    float getMagnitudeDB(float freqHz, double sampleRate) const
    {
        double omega = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        double cosw = std::cos(omega);
        double sinw = std::sin(omega);
        double cos2w = std::cos(2.0 * omega);
        double sin2w = std::sin(2.0 * omega);

        auto biquadMag = [&](float b0, float b1, float b2, float a1, float a2) -> double
        {
            double numR = b0 + b1 * cosw + b2 * cos2w;
            double numI = -(b1 * sinw + b2 * sin2w);
            double denR = 1.0 + a1 * cosw + a2 * cos2w;
            double denI = -(a1 * sinw + a2 * sin2w);
            double numMag2 = numR * numR + numI * numI;
            double denMag2 = denR * denR + denI * denI;
            return (denMag2 > 1e-20) ? std::sqrt(numMag2 / denMag2) : 1.0;
        };

        double peakMag = biquadMag(peakB0, peakB1, peakB2, peakA1, peakA2);
        double dipMag = biquadMag(dipB0, dipB1, dipB2, dipA1, dipA2);
        double combined = peakMag * dipMag;
        return static_cast<float>(20.0 * std::log10(combined + 1e-10));
    }

    InductorModel& getInductor() { return inductor; }

    // Get inductor RMS level for program-dependent metering
    float getInductorRmsLevel() const { return inductor.getRmsLevel(); }

private:
    static constexpr int maxChannels = 8;

    float peakB0 = 1.0f, peakB1 = 0.0f, peakB2 = 0.0f;
    float peakA1 = 0.0f, peakA2 = 0.0f;
    float dipB0 = 1.0f, dipB1 = 0.0f, dipB2 = 0.0f;
    float dipA1 = 0.0f, dipA2 = 0.0f;

    struct ChannelState {
        float peakZ1 = 0.0f, peakZ2 = 0.0f;
        float dipZ1 = 0.0f, dipZ2 = 0.0f;
    };
    ChannelState channels[maxChannels] = {};

    InductorModel inductor;
    double currentSampleRate = 44100.0;
    float cachedBoost = 0.0f, cachedAtten = 0.0f, cachedFreq = 60.0f;
};

class TubeEQProcessor
{
public:
    // Parameter structure for Tube EQ
    struct Parameters
    {
        // Low Frequency Section
        float lfBoostGain = 0.0f;      // 0-10 (maps to 0-14 dB)
        float lfBoostFreq = 60.0f;     // 20, 30, 60, 100 Hz (4 positions)
        float lfAttenGain = 0.0f;      // 0-10 (maps to 0-16 dB cut)

        // High Frequency Boost Section
        float hfBoostGain = 0.0f;      // 0-10 (maps to 0-16 dB)
        float hfBoostFreq = 8000.0f;   // 3k, 4k, 5k, 8k, 10k, 12k, 16k Hz
        float hfBoostBandwidth = 0.5f; // Sharp to Broad (Q control)

        // High Frequency Attenuation (shelf)
        float hfAttenGain = 0.0f;      // 0-10 (maps to 0-20 dB cut)
        float hfAttenFreq = 10000.0f;  // 5k, 10k, 20k Hz (3 positions)

        // Mid Dip/Peak Section
        bool midEnabled = true;           // Section bypass
        float midLowFreq = 500.0f;        // 0.2, 0.3, 0.5, 0.7, 1.0 kHz
        float midLowPeak = 0.0f;          // 0-10 (maps to 0-12 dB boost)
        float midDipFreq = 700.0f;        // 0.2, 0.3, 0.5, 0.7, 1.0, 1.5, 2.0 kHz
        float midDip = 0.0f;              // 0-10 (maps to 0-10 dB cut)
        float midHighFreq = 3000.0f;      // 1.5, 2.0, 3.0, 4.0, 5.0 kHz
        float midHighPeak = 0.0f;         // 0-10 (maps to 0-12 dB boost)

        // Global controls
        float inputGain = 0.0f;        // -12 to +12 dB
        float outputGain = 0.0f;       // -12 to +12 dB
        float tubeDrive = 0.3f;        // 0-1 (tube saturation amount)
        bool bypass = false;
    };

    TubeEQProcessor()
    {
        // Initialize with default tube type
    }

    void prepare(double sampleRate, int samplesPerBlock, int numChannels)
    {
        // Tube stage and LC network support max 8 channels; channels beyond 8
        // will alias to channel 7's state.
        jassert(numChannels <= maxProcessChannels);

        currentSampleRate = sampleRate;
        this->numChannels = std::min(numChannels, maxProcessChannels);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 1;

        // Prepare HF boost filters (resonant peak with bandwidth)
        hfBoostFilterL.prepare(spec);
        hfBoostFilterR.prepare(spec);

        // Prepare HF atten filters (shelf)
        hfAttenFilterL.prepare(spec);
        hfAttenFilterR.prepare(spec);

        // Prepare Mid section filters
        midLowPeakFilterL.prepare(spec);
        midLowPeakFilterR.prepare(spec);
        midDipFilterL.prepare(spec);
        midDipFilterR.prepare(spec);
        midHighPeakFilterL.prepare(spec);
        midHighPeakFilterR.prepare(spec);

        // Prepare enhanced analog stages
        // Use deterministic seed based on sample rate for reproducible vintage character
        characterSeed = static_cast<uint32_t>(sampleRate * 1000.0);
        tubeStage.prepare(sampleRate, numChannels);
        pultecLF.prepare(sampleRate, characterSeed);
        hfInductor.prepare(sampleRate, characterSeed + 1);  // Offset for variation between inductors
        hfQInductor.prepare(sampleRate, characterSeed + 1);

        // Prepare transformers
        inputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.prepare(sampleRate, numChannels);

        // Set up transformer profiles
        setupTransformerProfiles();

        // Initialize analog emulation library
        AnalogEmulation::initializeLibrary();

        // Create initial passthrough coefficients for all IIR filters (off audio thread)
        // so that updateFilters() can modify them in-place without heap allocations
        initFilterCoefficients(hfBoostFilterL);
        initFilterCoefficients(hfBoostFilterR);
        initFilterCoefficients(hfAttenFilterL);
        initFilterCoefficients(hfAttenFilterR);
        initFilterCoefficients(midLowPeakFilterL);
        initFilterCoefficients(midLowPeakFilterR);
        initFilterCoefficients(midDipFilterL);
        initFilterCoefficients(midDipFilterR);
        initFilterCoefficients(midHighPeakFilterL);
        initFilterCoefficients(midHighPeakFilterR);

        reset();
    }

    void reset()
    {
        hfBoostFilterL.reset();
        hfBoostFilterR.reset();
        hfAttenFilterL.reset();
        hfAttenFilterR.reset();
        midLowPeakFilterL.reset();
        midLowPeakFilterR.reset();
        midDipFilterL.reset();
        midDipFilterR.reset();
        midHighPeakFilterL.reset();
        midHighPeakFilterR.reset();
        tubeStage.reset();
        pultecLF.reset();
        hfInductor.reset();
        inputTransformer.reset();
        outputTransformer.reset();
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread.
        Resets filter state and marks parameters dirty for coefficient recalculation.
        NOTE: Transformer sample rates are deferred to the next full prepare(). */
    void updateSampleRate(double newRate)
    {
        currentSampleRate = newRate;

        // Lightweight rate updates (no allocation, safe for audio thread)
        tubeStage.updateSampleRate(newRate);
        pultecLF.updateSampleRate(newRate);
        hfInductor.updateSampleRate(newRate);
        hfQInductor.updateSampleRate(newRate);
        // Transformers deferred to next full prepare() (TransformerEmulation::prepare may allocate)

        parametersNeedUpdate.store(true, std::memory_order_release);
        reset();
    }

    void setParameters(const Parameters& newParams)
    {
        {
            juce::SpinLock::ScopedLockType lock(paramLock);
            pendingParams = newParams;
        }
        parametersNeedUpdate.store(true, std::memory_order_release);
    }

    Parameters getParameters() const
    {
        juce::SpinLock::ScopedLockType lock(paramLock);
        return params;
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        juce::ScopedNoDenormals noDenormals;

        // Apply pending parameter updates (deferred from setParameters for thread safety)
        if (parametersNeedUpdate.exchange(false, std::memory_order_acquire))
        {
            {
                juce::SpinLock::ScopedLockType lock(paramLock);
                params = pendingParams;
            }
            updateFilters();
            tubeStage.setDrive(params.tubeDrive);
        }

        if (params.bypass)
            return;

        const int numSamples = buffer.getNumSamples();
        const int channels = std::min(buffer.getNumChannels(), maxProcessChannels);

        // Apply input gain
        if (std::abs(params.inputGain) > 0.01f)
        {
            float inputGainLinear = juce::Decibels::decibelsToGain(params.inputGain);
            buffer.applyGain(inputGainLinear);
        }

        // Process each channel
        for (int ch = 0; ch < channels; ++ch)
        {
            float* channelData = buffer.getWritePointer(ch);
            bool isLeft = (ch % 2 == 0);  // L/R pairs for stereo and surround

            for (int i = 0; i < numSamples; ++i)
            {
                float sample = channelData[i];

                // NaN/Inf protection - skip processing if input is invalid
                if (!safeIsFinite(sample))
                {
                    channelData[i] = 0.0f;
                    continue;
                }

                // Input transformer coloration
                sample = inputTransformer.processSample(sample, ch);

                // === LF Section: dual-biquad boost/cut interaction ===
                sample = pultecLF.processSample(sample, ch);

                // === HF Section with inductor characteristics ===
                if (params.hfBoostGain > 0.01f)
                {
                    // Apply inductor nonlinearity before HF boost
                    sample = hfInductor.processNonlinearity(sample, params.hfBoostGain * 0.2f);

                    sample = isLeft ? hfBoostFilterL.processSample(sample)
                                    : hfBoostFilterR.processSample(sample);
                }

                // HF Attenuation (shelf)
                if (params.hfAttenGain > 0.01f)
                {
                    sample = isLeft ? hfAttenFilterL.processSample(sample)
                                    : hfAttenFilterR.processSample(sample);
                }

                // === Mid Dip/Peak Section ===
                if (params.midEnabled)
                {
                    // Mid Low Peak
                    if (params.midLowPeak > 0.01f)
                    {
                        sample = isLeft ? midLowPeakFilterL.processSample(sample)
                                        : midLowPeakFilterR.processSample(sample);
                    }

                    // Mid Dip (cut)
                    if (params.midDip > 0.01f)
                    {
                        sample = isLeft ? midDipFilterL.processSample(sample)
                                        : midDipFilterR.processSample(sample);
                    }

                    // Mid High Peak
                    if (params.midHighPeak > 0.01f)
                    {
                        sample = isLeft ? midHighPeakFilterL.processSample(sample)
                                        : midHighPeakFilterR.processSample(sample);
                    }
                }

                // Tube makeup gain stage
                if (params.tubeDrive > 0.01f)
                {
                    sample = tubeStage.processSample(sample, ch);
                }

                // Output transformer
                sample = outputTransformer.processSample(sample, ch);

                // NaN/Inf protection - zero output if processing produced invalid result
                if (!safeIsFinite(sample))
                    sample = 0.0f;

                channelData[i] = sample;
            }
        }

        // Apply output gain
        if (std::abs(params.outputGain) > 0.01f)
        {
            float outputGainLinear = juce::Decibels::decibelsToGain(params.outputGain);
            buffer.applyGain(outputGainLinear);
        }
    }

    /** Get LF section magnitude at a frequency (for curve display). */
    float getPultecLFMagnitudeDB(float frequencyHz) const
    {
        return pultecLF.getMagnitudeDB(frequencyHz, currentSampleRate);
    }

    // Get frequency response magnitude at a specific frequency (for curve display)
    float getFrequencyResponseMagnitude(float frequencyHz) const
    {
        // Snapshot parameters and coefficient Ptrs under the lock. The underlying
        // coefficient values may be updated in-place by the audio thread (benign
        // data race) — torn reads just cause a brief visual glitch in the curve
        // display, self-correcting on the next repaint frame.
        Parameters localParams;
        juce::dsp::IIR::Coefficients<float>::Ptr localHfBoost, localHfAtten;
        juce::dsp::IIR::Coefficients<float>::Ptr localMidLowPeak, localMidDip, localMidHighPeak;
        {
            juce::SpinLock::ScopedLockType lock(paramLock);
            localParams = params;
            localHfBoost = hfBoostFilterL.coefficients;
            localHfAtten = hfAttenFilterL.coefficients;
            localMidLowPeak = midLowPeakFilterL.coefficients;
            localMidDip = midDipFilterL.coefficients;
            localMidHighPeak = midHighPeakFilterL.coefficients;
        }

        if (localParams.bypass)
            return 0.0f;

        float magnitudeDB = 0.0f;

        // Calculate contribution from each filter
        double omega = juce::MathConstants<double>::twoPi * frequencyHz / currentSampleRate;

        // LF Section (dual-biquad: combined boost + atten interaction)
        if (localParams.lfBoostGain > 0.01f || localParams.lfAttenGain > 0.01f)
        {
            magnitudeDB += pultecLF.getMagnitudeDB(frequencyHz, currentSampleRate);
        }

        // HF Boost contribution
        if (localParams.hfBoostGain > 0.01f && localHfBoost != nullptr)
        {
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            auto& coeffs = *localHfBoost;

            std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
            std::complex<double> den = static_cast<double>(coeffs.coefficients[3]) + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

            float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
            magnitudeDB += filterMag;
        }

        // HF Atten contribution
        if (localParams.hfAttenGain > 0.01f && localHfAtten != nullptr)
        {
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            auto& coeffs = *localHfAtten;

            std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
            std::complex<double> den = static_cast<double>(coeffs.coefficients[3]) + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

            float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
            magnitudeDB += filterMag;
        }

        // ===== MID SECTION CONTRIBUTIONS =====
        if (localParams.midEnabled)
        {
            // Mid Low Peak contribution
            if (localParams.midLowPeak > 0.01f && localMidLowPeak != nullptr)
            {
                std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
                auto& coeffs = *localMidLowPeak;

                std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
                std::complex<double> den = static_cast<double>(coeffs.coefficients[3]) + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

                float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
                magnitudeDB += filterMag;
            }

            // Mid Dip contribution
            if (localParams.midDip > 0.01f && localMidDip != nullptr)
            {
                std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
                auto& coeffs = *localMidDip;

                std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
                std::complex<double> den = static_cast<double>(coeffs.coefficients[3]) + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

                float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
                magnitudeDB += filterMag;
            }

            // Mid High Peak contribution
            if (localParams.midHighPeak > 0.01f && localMidHighPeak != nullptr)
            {
                std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
                auto& coeffs = *localMidHighPeak;

                std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
                std::complex<double> den = static_cast<double>(coeffs.coefficients[3]) + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

                float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
                magnitudeDB += filterMag;
            }
        }

        return magnitudeDB;
    }

private:
    Parameters params;
    Parameters pendingParams;
    std::atomic<bool> parametersNeedUpdate{false};
    mutable juce::SpinLock paramLock;
    double currentSampleRate = 44100.0;
    int numChannels = 2;
    uint32_t characterSeed = 0;

    // HF Boost: Resonant peak with bandwidth
    juce::dsp::IIR::Filter<float> hfBoostFilterL, hfBoostFilterR;

    // HF Atten: High shelf cut
    juce::dsp::IIR::Filter<float> hfAttenFilterL, hfAttenFilterR;

    // Mid Section filters
    juce::dsp::IIR::Filter<float> midLowPeakFilterL, midLowPeakFilterR;
    juce::dsp::IIR::Filter<float> midDipFilterL, midDipFilterR;
    juce::dsp::IIR::Filter<float> midHighPeakFilterL, midHighPeakFilterR;

    // Enhanced analog stages
    TubeEQTubeStage tubeStage;
    PultecLFSection pultecLF;
    InductorModel hfInductor;

    // Persistent inductor model for HF Q computation (avoids mt19937 allocation on audio thread)
    InductorModel hfQInductor;

    static constexpr int maxProcessChannels = 8;

    // Transformers
    AnalogEmulation::TransformerEmulation inputTransformer;
    AnalogEmulation::TransformerEmulation outputTransformer;

    void setupTransformerProfiles()
    {
        // Input transformer profile
        AnalogEmulation::TransformerProfile inputProfile;
        inputProfile.hasTransformer = true;
        inputProfile.saturationAmount = 0.15f;
        inputProfile.lowFreqSaturation = 1.3f;  // LF saturation boost
        inputProfile.highFreqRolloff = 22000.0f;
        inputProfile.dcBlockingFreq = 10.0f;
        inputProfile.harmonics = { 0.02f, 0.005f, 0.001f };  // Primarily 2nd harmonic

        inputTransformer.setProfile(inputProfile);
        inputTransformer.setEnabled(true);

        // Output transformer - slightly more color
        AnalogEmulation::TransformerProfile outputProfile;
        outputProfile.hasTransformer = true;
        outputProfile.saturationAmount = 0.12f;
        outputProfile.lowFreqSaturation = 1.2f;
        outputProfile.highFreqRolloff = 20000.0f;
        outputProfile.dcBlockingFreq = 8.0f;
        outputProfile.harmonics = { 0.015f, 0.004f, 0.001f };

        outputTransformer.setProfile(outputProfile);
        outputTransformer.setEnabled(true);
    }

    void updateFilters()
    {
        // LF Section: dual-biquad with boost/cut interaction
        pultecLF.updateCoefficients(params.lfBoostGain, params.lfAttenGain,
                                     params.lfBoostFreq, currentSampleRate);
        updateHFBoost();
        updateHFAtten();
        updateMidLowPeak();
        updateMidDip();
        updateMidHighPeak();
    }

    void updateHFBoost()
    {
        // HF resonant peak with variable bandwidth
        float freq = tubeEQPreWarpFrequency(params.hfBoostFreq, currentSampleRate);
        float gainDB = params.hfBoostGain * 1.6f;  // 0-10 maps to ~0-16 dB

        // Bandwidth control: Sharp (high Q) to Broad (low Q)
        // Inverted mapping: 0 = sharp (high Q), 1 = broad (low Q)
        float baseQ = juce::jmap(params.hfBoostBandwidth, 0.0f, 1.0f, 2.5f, 0.5f);

        // Frequency-dependent Q from inductor model
        float effectiveQ = hfQInductor.getFrequencyDependentQ(params.hfBoostFreq, baseQ);

        setTubeEQPeakCoeffs(hfBoostFilterL, currentSampleRate, freq, effectiveQ, gainDB);
        setTubeEQPeakCoeffs(hfBoostFilterR, currentSampleRate, freq, effectiveQ, gainDB);
    }

    void updateHFAtten()
    {
        // HF high shelf cut
        float freq = tubeEQPreWarpFrequency(params.hfAttenFreq, currentSampleRate);
        float gainDB = -params.hfAttenGain * 2.0f;  // 0-10 maps to ~0-20 dB cut

        setHighShelfCoeffs(hfAttenFilterL, currentSampleRate, freq, 0.6f, gainDB);
        setHighShelfCoeffs(hfAttenFilterR, currentSampleRate, freq, 0.6f, gainDB);
    }

    void updateMidLowPeak()
    {
        // Mid Low Peak: Resonant boost in low-mid range
        float freq = tubeEQPreWarpFrequency(params.midLowFreq, currentSampleRate);
        float gainDB = params.midLowPeak * 1.2f;  // 0-10 maps to ~0-12 dB

        // Moderate Q for musical character
        float q = 1.2f;

        setTubeEQPeakCoeffs(midLowPeakFilterL, currentSampleRate, freq, q, gainDB);
        setTubeEQPeakCoeffs(midLowPeakFilterR, currentSampleRate, freq, q, gainDB);
    }

    void updateMidDip()
    {
        // Mid Dip: Cut in mid range
        float freq = tubeEQPreWarpFrequency(params.midDipFreq, currentSampleRate);
        float gainDB = -params.midDip * 1.0f;  // 0-10 maps to ~0-10 dB cut

        // Broader Q for natural sounding cut
        float q = 0.8f;

        setTubeEQPeakCoeffs(midDipFilterL, currentSampleRate, freq, q, gainDB);
        setTubeEQPeakCoeffs(midDipFilterR, currentSampleRate, freq, q, gainDB);
    }

    void updateMidHighPeak()
    {
        // Mid High Peak: Resonant boost in upper-mid range
        float freq = tubeEQPreWarpFrequency(params.midHighFreq, currentSampleRate);
        float gainDB = params.midHighPeak * 1.2f;  // 0-10 maps to ~0-12 dB

        // Moderate Q for presence
        float q = 1.4f;

        setTubeEQPeakCoeffs(midHighPeakFilterL, currentSampleRate, freq, q, gainDB);
        setTubeEQPeakCoeffs(midHighPeakFilterR, currentSampleRate, freq, q, gainDB);
    }

    // Create initial passthrough coefficients for a filter (called from prepare(), off audio thread)
    static void initFilterCoefficients(juce::dsp::IIR::Filter<float>& filter)
    {
        filter.coefficients = new juce::dsp::IIR::Coefficients<float>(
            1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    }

    // Assign biquad coefficients in-place (no heap allocation)
    static void setFilterCoeffs(juce::dsp::IIR::Filter<float>& filter,
                                float b0, float b1, float b2, float a1, float a2)
    {
        if (filter.coefficients == nullptr)
            return;
        auto* c = filter.coefficients->coefficients.getRawDataPointer();
        c[0] = b0; c[1] = b1; c[2] = b2; c[3] = 1.0f; c[4] = a1; c[5] = a2;
    }

    // Tube EQ style peak filter with inductor characteristics (in-place, no allocation)
    void setTubeEQPeakCoeffs(juce::dsp::IIR::Filter<float>& filter,
                             double sampleRate, float freq, float q, float gainDB) const
    {
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);

        // Inductor-style Q modification - broader, more musical
        float tubeEQQ = q * 0.85f;
        float alpha = sinw0 / (2.0f * tubeEQQ);

        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cosw0;
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha / A;

        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
        setFilterCoeffs(filter, b0, b1, b2, a1, a2);
    }

    void setLowShelfCoeffs(juce::dsp::IIR::Filter<float>& filter,
                           double sampleRate, float freq, float q, float gainDB) const
    {
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * q);

        float b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha);
        float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
        float b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha);
        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
        float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
        float a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha;

        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
        setFilterCoeffs(filter, b0, b1, b2, a1, a2);
    }

    void setHighShelfCoeffs(juce::dsp::IIR::Filter<float>& filter,
                            double sampleRate, float freq, float q, float gainDB) const
    {
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * q);

        float b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha);
        float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
        float b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha);
        float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
        float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
        float a2 = (A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha;

        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
        setFilterCoeffs(filter, b0, b1, b2, a1, a2);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TubeEQProcessor)
};

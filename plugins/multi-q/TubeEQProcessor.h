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
        if (!std::isfinite(input))
            return 0.0f;

        // Track RMS level for program-dependent behavior
        const float rmsCoeff = 0.9995f;  // ~50ms integration
        rmsLevel = rmsLevel * rmsCoeff + input * input * (1.0f - rmsCoeff);
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

        // Core flux integration with decay
        coreFlux = coreFlux * 0.97f + deltaInput * hysteresisCoeff;
        coreFlux = std::clamp(coreFlux, -0.15f, 0.15f);

        // Hysteresis adds slight asymmetry based on flux direction
        hysteresisState = hysteresisState * 0.92f + coreFlux * 0.08f;
        float output = saturatedInput + hysteresisState * 0.5f;

        prevInput = input;
        prevOutput = output;

        return output;
    }

    float getRmsLevel() const { return std::sqrt(rmsLevel); }

private:
    double sampleRate = 44100.0;
    float prevInput = 0.0f;
    float prevOutput = 0.0f;
    float hysteresisState = 0.0f;
    float coreFlux = 0.0f;
    float rmsLevel = 0.0f;

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
        gridCurrent = gridCurrent * 0.9f + gridCurrentAmount * 0.1f;

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
    static constexpr int maxChannels = 8;
    double sampleRate = 44100.0;
    float drive = 0.3f;
    float maxSlewRate = 0.003f;

    // Per-channel state (indexed by channel)
    std::array<float, maxChannels> prevSamples{};
    std::array<float, maxChannels> cathodeVoltages{};
    std::array<float, maxChannels> gridCurrents{};

    std::array<AnalogEmulation::DCBlocker, maxChannels> dcBlockers;
};

/** Passive LC Network model for boost/cut interaction.
    Shared LC topology creates characteristic boost peak + shelf cut interaction. */
class PassiveLCNetwork
{
public:
    void prepare(double sampleRate, uint32_t characterSeed = 0)
    {
        this->sampleRate = sampleRate;
        inductor.prepare(sampleRate, characterSeed);
        reset();
    }

    void reset()
    {
        inductor.reset();
        for (int i = 0; i < maxChannels; ++i)
        {
            interactionStateHP[i] = 0.0f;
            interactionStateLP[i] = 0.0f;
            lfShelfState[i] = 0.0f;
        }
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread. */
    void updateSampleRate(double newRate)
    {
        sampleRate = newRate;
        inductor.updateSampleRate(newRate);
        reset();
    }

    /** Process LF section with boost/cut interaction. */
    float processLFSection(float input, float boostGain, float attenGain, float frequency,
                           float& boostState1, float& boostState2, float& attenState, int channel)
    {
        if (boostGain < 0.01f && attenGain < 0.01f)
            return input;

        if (!std::isfinite(input))
            input = 0.0f;

        // Clamp frequency to safe range
        float maxFreq = static_cast<float>(sampleRate) * 0.1f;
        frequency = std::clamp(frequency, 10.0f, maxFreq);

        // Broad Q from inductor model
        float baseQ = 0.55f;
        float effectiveQ = inductor.getFrequencyDependentQ(frequency, baseQ);
        effectiveQ = std::max(effectiveQ, 0.2f);

        // Frequency relationships: cut shelf at 0.7x, interaction at 1.5x
        float boostFreq = frequency;
        float cutShelfFreq = frequency * 0.7f;
        float interactionFreq = frequency * 1.5f;

        float output = input;

        // === LC Tank Resonant Boost ===
        if (boostGain > 0.01f)
        {
            float omega = juce::MathConstants<float>::twoPi * boostFreq / static_cast<float>(sampleRate);
            omega = std::min(omega, 0.45f);
            float sinOmega = std::sin(omega);

            // State variable filter for resonant boost
            float alpha = sinOmega / (2.0f * effectiveQ);
            alpha = std::clamp(alpha, 0.01f, 0.95f);

            // SVF implementation
            float invQ = 1.0f / effectiveQ;
            float hp = input - boostState1 * invQ - boostState2;
            float bp = hp * alpha + boostState1;
            float lp = bp * alpha + boostState2;

            // State update with limiting
            boostState1 = std::clamp(bp, -8.0f, 8.0f);
            boostState2 = std::clamp(lp, -8.0f, 8.0f);

            // Boost amount: 0-10 maps to 0-14 dB
            float boostDB = boostGain * 1.4f;
            float boostLinear = juce::Decibels::decibelsToGain(boostDB) - 1.0f;

            // Resonant boost from bandpass response
            output = input + bp * boostLinear;

            // Apply inductor saturation (adds harmonics and compression)
            output = inductor.processNonlinearity(output, boostGain * 0.3f);
        }

        // === Low Shelf Attenuation ===
        if (attenGain > 0.01f)
        {
            // One-pole low shelf for attenuation
            float wc = juce::MathConstants<float>::twoPi * cutShelfFreq / static_cast<float>(sampleRate);
            wc = std::min(wc, 0.35f);
            float g = std::tan(wc * 0.5f);
            float G = g / (1.0f + g);
            G = std::clamp(G, 0.01f, 0.99f);

            // LP content extraction
            attenState = attenState + G * (output - attenState);
            attenState = std::clamp(attenState, -8.0f, 8.0f);

            // Attenuation amount: 0-10 maps to 0-16 dB cut
            float attenDB = attenGain * 1.6f;
            float attenFactor = juce::Decibels::decibelsToGain(-attenDB);

            // Apply attenuation to low frequencies only
            output = output - attenState * (1.0f - attenFactor);
        }

        // Boost/cut interaction (shared LC network)
        if (boostGain > 0.01f && attenGain > 0.01f)
        {
            float interactionStrength = std::min(boostGain, attenGain) * 0.15f;

            // The interaction creates an additional resonant bump above the boost freq
            float omega = juce::MathConstants<float>::twoPi * interactionFreq / static_cast<float>(sampleRate);
            omega = std::min(omega, 0.4f);

            // Clamp channel index to valid range for safety
            int ch = std::clamp(channel, 0, maxChannels - 1);

            // Simple one-pole HP to extract interaction frequency content
            float intAlpha = 0.02f;
            // Use per-channel state for proper stereo processing
            interactionStateHP[ch] = interactionStateHP[ch] * (1.0f - intAlpha) + input * intAlpha;
            interactionStateLP[ch] = interactionStateLP[ch] * 0.99f + (input - interactionStateHP[ch]) * 0.01f;

            // Add subtle resonant enhancement
            float interactionBoost = interactionStateLP[ch] * interactionStrength * std::sin(omega);
            interactionBoost = std::clamp(interactionBoost, -0.3f, 0.3f);
            output += interactionBoost;

            // Also add the characteristic "scooped" low-mid response
            // This is where the cut extends into the boost region
            float scoopFreq = frequency * 0.5f;
            float scoopOmega = juce::MathConstants<float>::twoPi * scoopFreq / static_cast<float>(sampleRate);
            scoopOmega = std::min(scoopOmega, 0.3f);

            lfShelfState[ch] = lfShelfState[ch] * 0.995f + input * 0.005f;
            float scoopAmount = interactionStrength * 0.5f;
            output -= lfShelfState[ch] * scoopAmount * std::sin(scoopOmega);
        }

        if (!std::isfinite(output))
            output = input;

        return output;
    }

    // Get inductor RMS level for program-dependent metering
    float getInductorRmsLevel() const { return inductor.getRmsLevel(); }

private:
    double sampleRate = 44100.0;
    InductorModel inductor;

    static constexpr int maxChannels = 8;

    // Boost/cut interaction state (per-channel)
    float interactionStateHP[maxChannels] = {};
    float interactionStateLP[maxChannels] = {};
    float lfShelfState[maxChannels] = {};
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

        // Prepare LF boost filters (resonant peak)
        lfBoostFilterL.prepare(spec);
        lfBoostFilterR.prepare(spec);

        // Prepare LF atten filters (shelf)
        lfAttenFilterL.prepare(spec);
        lfAttenFilterR.prepare(spec);

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
        lfNetwork.prepare(sampleRate, characterSeed);
        hfInductor.prepare(sampleRate, characterSeed + 1);  // Offset for variation between inductors
        lfQInductor.prepare(sampleRate, characterSeed);
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
        initFilterCoefficients(lfBoostFilterL);
        initFilterCoefficients(lfBoostFilterR);
        initFilterCoefficients(lfAttenFilterL);
        initFilterCoefficients(lfAttenFilterR);
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
        lfBoostFilterL.reset();
        lfBoostFilterR.reset();
        lfAttenFilterL.reset();
        lfAttenFilterR.reset();
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
        lfNetwork.reset();
        hfInductor.reset();
        inputTransformer.reset();
        outputTransformer.reset();

        // Reset LC network states
        for (int i = 0; i < maxProcessChannels; ++i)
        {
            lfBoostState1[i] = 0.0f;
            lfBoostState2[i] = 0.0f;
            lfAttenStateLC[i] = 0.0f;
        }
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread.
        Resets filter state and marks parameters dirty for coefficient recalculation.
        NOTE: Transformer sample rates are deferred to the next full prepare(). */
    void updateSampleRate(double newRate)
    {
        currentSampleRate = newRate;

        // Lightweight rate updates (no allocation, safe for audio thread)
        tubeStage.updateSampleRate(newRate);
        lfNetwork.updateSampleRate(newRate);
        hfInductor.updateSampleRate(newRate);
        lfQInductor.updateSampleRate(newRate);
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
                if (!std::isfinite(sample))
                {
                    channelData[i] = 0.0f;
                    continue;
                }

                // Input transformer coloration
                sample = inputTransformer.processSample(sample, ch);

                // === Passive LC Network: LF Section with true boost/cut interaction ===
                int chIdx = std::clamp(ch, 0, maxProcessChannels - 1);
                sample = lfNetwork.processLFSection(
                    sample,
                    params.lfBoostGain,
                    params.lfAttenGain,
                    params.lfBoostFreq,
                    lfBoostState1[chIdx],
                    lfBoostState2[chIdx],
                    lfAttenStateLC[chIdx],
                    chIdx  // Pass channel index for per-channel interaction state
                );

                // Also apply the standard filter for more accurate response
                if (params.lfBoostGain > 0.01f)
                {
                    float filtered = isLeft ? lfBoostFilterL.processSample(sample)
                                            : lfBoostFilterR.processSample(sample);
                    // Blend LC network with standard filter
                    sample = sample * 0.4f + filtered * 0.6f;
                }

                if (params.lfAttenGain > 0.01f)
                {
                    sample = isLeft ? lfAttenFilterL.processSample(sample)
                                    : lfAttenFilterR.processSample(sample);
                }

                // === HF Section with inductor characteristics ===
                if (params.hfBoostGain > 0.01f)
                {
                    // Apply inductor nonlinearity before HF boost
                    float hfSample = hfInductor.processNonlinearity(sample, params.hfBoostGain * 0.2f);

                    float filtered = isLeft ? hfBoostFilterL.processSample(hfSample)
                                            : hfBoostFilterR.processSample(hfSample);

                    // Blend for natural sound
                    sample = sample * 0.3f + filtered * 0.7f;
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
                if (!std::isfinite(sample))
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

    // Get frequency response magnitude at a specific frequency (for curve display)
    float getFrequencyResponseMagnitude(float frequencyHz) const
    {
        // Snapshot parameters and coefficient Ptrs under the lock. The underlying
        // coefficient values may be updated in-place by the audio thread (benign
        // data race) — torn reads just cause a brief visual glitch in the curve
        // display, self-correcting on the next repaint frame.
        Parameters localParams;
        juce::dsp::IIR::Coefficients<float>::Ptr localLfBoost, localLfAtten;
        juce::dsp::IIR::Coefficients<float>::Ptr localHfBoost, localHfAtten;
        juce::dsp::IIR::Coefficients<float>::Ptr localMidLowPeak, localMidDip, localMidHighPeak;
        {
            juce::SpinLock::ScopedLockType lock(paramLock);
            localParams = params;
            localLfBoost = lfBoostFilterL.coefficients;
            localLfAtten = lfAttenFilterL.coefficients;
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

        // LF Boost contribution
        if (localParams.lfBoostGain > 0.01f && localLfBoost != nullptr)
        {
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            auto& coeffs = *localLfBoost;

            std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
            std::complex<double> den = static_cast<double>(coeffs.coefficients[3]) + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

            float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
            magnitudeDB += filterMag;

            // Add interaction effect when both boost and atten are engaged
            if (localParams.lfAttenGain > 0.01f)
            {
                // The "Tube EQ trick" creates a bump above the cut frequency
                float interactionFreq = localParams.lfBoostFreq * 1.5f;
                if (frequencyHz > localParams.lfBoostFreq && frequencyHz < interactionFreq * 1.5f)
                {
                    float interactionAmount = localParams.lfBoostGain * localParams.lfAttenGain * 0.02f;
                    float relativePos = (frequencyHz - localParams.lfBoostFreq) / (interactionFreq - localParams.lfBoostFreq);
                    magnitudeDB += interactionAmount * std::sin(relativePos * juce::MathConstants<float>::pi);
                }
            }
        }

        // LF Atten contribution
        if (localParams.lfAttenGain > 0.01f && localLfAtten != nullptr)
        {
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            auto& coeffs = *localLfAtten;

            std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
            std::complex<double> den = static_cast<double>(coeffs.coefficients[3]) + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

            float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
            magnitudeDB += filterMag;
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

    // LF Boost: Resonant peak filter
    juce::dsp::IIR::Filter<float> lfBoostFilterL, lfBoostFilterR;

    // LF Atten: Low shelf cut
    juce::dsp::IIR::Filter<float> lfAttenFilterL, lfAttenFilterR;

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
    PassiveLCNetwork lfNetwork;
    InductorModel hfInductor;

    // Persistent inductor models for Q computation (avoids mt19937 allocation on audio thread)
    InductorModel lfQInductor;
    InductorModel hfQInductor;

    // LC network state variables for boost/cut interaction (per-channel, up to 8)
    // boostState has 2 floats per channel (state1, state2 for the state variable filter)
    // attenStateLC has 1 float per channel (for the one-pole shelf in LC network)
    static constexpr int maxProcessChannels = 8;
    float lfBoostState1[maxProcessChannels] = {};
    float lfBoostState2[maxProcessChannels] = {};
    float lfAttenStateLC[maxProcessChannels] = {};

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
        updateLFBoost();
        updateLFAtten();
        updateHFBoost();
        updateHFAtten();
        updateMidLowPeak();
        updateMidDip();
        updateMidHighPeak();
    }

    void updateLFBoost()
    {
        // LF resonant peak with broad Q
        float freq = tubeEQPreWarpFrequency(params.lfBoostFreq, currentSampleRate);
        float gainDB = params.lfBoostGain * 1.4f;  // 0-10 maps to ~0-14 dB

        float baseQ = 0.5f;
        float effectiveQ = lfQInductor.getFrequencyDependentQ(params.lfBoostFreq, baseQ);

        setTubeEQPeakCoeffs(lfBoostFilterL, currentSampleRate, freq, effectiveQ, gainDB);
        setTubeEQPeakCoeffs(lfBoostFilterR, currentSampleRate, freq, effectiveQ, gainDB);
    }

    void updateLFAtten()
    {
        // LF shelf cut (interacts with boost)
        float freq = tubeEQPreWarpFrequency(params.lfBoostFreq, currentSampleRate);
        float gainDB = -params.lfAttenGain * 1.6f;  // 0-10 maps to ~0-16 dB cut

        // The attenuation is a shelf, not a peak
        setLowShelfCoeffs(lfAttenFilterL, currentSampleRate, freq, 0.7f, gainDB);
        setLowShelfCoeffs(lfAttenFilterR, currentSampleRate, freq, 0.7f, gainDB);
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

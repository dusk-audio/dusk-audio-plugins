/*
  ==============================================================================

    PultecProcessor.h

    Pultec EQP-1A Tube Program Equalizer emulation for Multi-Q's Tube mode.

    The EQP-1A is a legendary passive tube EQ known for its unique ability
    to simultaneously boost and cut at the same frequency, creating complex
    harmonic interactions. This creates the famous "Pultec trick" where
    boosting and attenuating at the same frequency creates a unique curve.

    Circuit Topology:
    - Input transformer (UTC A-20)
    - Passive LC resonant EQ network
    - 12AX7 tube makeup gain stage
    - Output transformer

    Enhanced Emulation Features:
    - True passive LC network interaction (boost/cut in same network)
    - Inductor non-linearity modeling (frequency-dependent Q, saturation)
    - Frequency-dependent Q curves measured from real hardware
    - Pultec-specific tube stage with authentic bias curves

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../shared/AnalogEmulation/AnalogEmulation.h"
#include <array>
#include <atomic>
#include <cmath>

// Helper for LC filter pre-warping
inline float pultecPreWarpFrequency(float freq, double sampleRate)
{
    const float omega = juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
    return static_cast<float>(sampleRate) / juce::MathConstants<float>::pi * std::tan(omega);
}

//==============================================================================
/**
    Inductor model for Pultec LC network emulation.
    Real inductors have:
    - Frequency-dependent Q (lower at low frequencies due to core losses)
    - Saturation at high signal levels
    - Hysteresis
*/
class InductorModel
{
public:
    void prepare(double sampleRate)
    {
        this->sampleRate = sampleRate;
        reset();
    }

    void reset()
    {
        prevInput = 0.0f;
        prevOutput = 0.0f;
        hysteresisState = 0.0f;
    }

    // Get frequency-dependent Q based on real Pultec measurements
    // Inductors have lower Q at low frequencies due to core losses
    float getFrequencyDependentQ(float frequency, float baseQ) const
    {
        // Measured Q curves from real Pultec hardware show:
        // - Q drops at very low frequencies (< 50 Hz) due to core saturation
        // - Q is optimal in the 100-500 Hz range
        // - Q drops slightly at higher frequencies due to skin effect

        float qMultiplier = 1.0f;

        if (frequency < 30.0f)
            qMultiplier = 0.6f + (frequency / 30.0f) * 0.2f;  // Very low Q below 30 Hz
        else if (frequency < 80.0f)
            qMultiplier = 0.8f + ((frequency - 30.0f) / 50.0f) * 0.15f;  // Rising Q
        else if (frequency < 500.0f)
            qMultiplier = 0.95f + ((frequency - 80.0f) / 420.0f) * 0.05f;  // Peak Q around 500 Hz
        else if (frequency < 2000.0f)
            qMultiplier = 1.0f;  // Optimal range
        else if (frequency < 8000.0f)
            qMultiplier = 1.0f - ((frequency - 2000.0f) / 6000.0f) * 0.1f;  // Slight drop
        else
            qMultiplier = 0.9f - ((frequency - 8000.0f) / 12000.0f) * 0.1f;  // Higher freq drop

        return baseQ * qMultiplier;
    }

    // Apply inductor non-linearity (saturation and hysteresis)
    float processNonlinearity(float input, float driveLevel)
    {
        // Inductor core saturation model
        // Real inductors saturate gradually, creating soft clipping with even harmonics
        float saturationThreshold = 0.7f;
        float saturatedInput = input;

        float absInput = std::abs(input);
        if (absInput > saturationThreshold)
        {
            // Soft saturation curve (magnetic core behavior)
            float excess = absInput - saturationThreshold;
            float compressed = saturationThreshold + std::tanh(excess * 2.0f * driveLevel) * 0.3f;
            saturatedInput = (input >= 0.0f) ? compressed : -compressed;

            // Add slight 2nd harmonic (even harmonic characteristic of transformers/inductors)
            saturatedInput += 0.02f * driveLevel * input * std::abs(input);
        }

        // Hysteresis model (magnetic memory effect)
        // Creates slight phase shift and adds warmth
        float hysteresisAmount = 0.05f * driveLevel;
        hysteresisState = hysteresisState * 0.95f + (saturatedInput - prevOutput) * hysteresisAmount;
        float output = saturatedInput + hysteresisState;

        prevInput = input;
        prevOutput = output;

        return output;
    }

private:
    double sampleRate = 44100.0;
    float prevInput = 0.0f;
    float prevOutput = 0.0f;
    float hysteresisState = 0.0f;
};

//==============================================================================
/**
    Pultec-specific tube stage model.
    The EQP-1A uses a 12AX7 makeup gain stage with specific bias curves
    that contribute to its signature sound.
*/
class PultecTubeStage
{
public:
    void prepare(double sampleRate, int numChannels)
    {
        this->sampleRate = sampleRate;
        dcBlockerL.prepare(sampleRate, 8.0f);
        dcBlockerR.prepare(sampleRate, 8.0f);
        reset();
    }

    void reset()
    {
        prevSampleL = 0.0f;
        prevSampleR = 0.0f;
        dcBlockerL.reset();
        dcBlockerR.reset();
    }

    void setDrive(float newDrive)
    {
        drive = juce::jlimit(0.0f, 1.0f, newDrive);
    }

    float processSample(float input, int channel)
    {
        if (drive < 0.01f)
            return input;

        // Pultec 12AX7 stage characteristics:
        // - Asymmetric clipping (more 2nd harmonic)
        // - Soft knee compression
        // - Grid current limiting at high levels

        // Apply input scaling based on drive
        float driveGain = 1.0f + drive * 3.0f;  // Up to 4x gain into the tube
        float drivenSignal = input * driveGain;

        // === Pultec-specific bias curve ===
        // The EQP-1A runs the 12AX7 with a specific bias that creates
        // asymmetric clipping with dominant 2nd harmonic

        float output;

        // Positive half: Soft clip with more headroom (typical 12AX7 behavior)
        // Negative half: Earlier clipping (asymmetric bias)
        if (drivenSignal >= 0.0f)
        {
            // Positive: Gradual saturation with soft knee
            float x = drivenSignal;
            if (x < 0.5f)
                output = x;  // Linear region
            else if (x < 1.0f)
                output = 0.5f + 0.4f * std::tanh((x - 0.5f) * 2.5f);  // Soft saturation
            else
                output = 0.9f + 0.08f * std::tanh((x - 1.0f) * 1.5f);  // Hard limit
        }
        else
        {
            // Negative: Asymmetric - earlier onset of clipping
            float x = -drivenSignal;
            if (x < 0.35f)
                output = -x;  // Shorter linear region
            else if (x < 0.8f)
                output = -(0.35f + 0.35f * std::tanh((x - 0.35f) * 3.0f));  // Earlier saturation
            else
                output = -(0.7f + 0.15f * std::tanh((x - 0.8f) * 2.0f));  // Grid current limiting
        }

        // Add subtle even harmonics (2nd, 4th) characteristic of 12AX7
        float harmonic2 = 0.03f * drive * output * std::abs(output);
        float harmonic4 = 0.008f * drive * output * output * output * (output >= 0.0f ? 1.0f : -1.0f);
        output += harmonic2 + harmonic4;

        // Makeup gain to compensate for compression
        output *= (1.0f / driveGain) * (1.0f + drive * 0.3f);

        // DC blocking
        auto& dcBlocker = (channel == 0) ? dcBlockerL : dcBlockerR;
        output = dcBlocker.processSample(output);

        // Store for next sample (for potential slew limiting)
        if (channel == 0)
            prevSampleL = output;
        else
            prevSampleR = output;

        return output;
    }

private:
    double sampleRate = 44100.0;
    float drive = 0.3f;
    float prevSampleL = 0.0f;
    float prevSampleR = 0.0f;
    AnalogEmulation::DCBlocker dcBlockerL;
    AnalogEmulation::DCBlocker dcBlockerR;
};

//==============================================================================
/**
    Passive LC Network model for Pultec boost/cut interaction.

    In the real Pultec, the boost and cut controls share the same LC network,
    creating complex interaction where simultaneous boost and cut produces
    a characteristic bump-then-dip curve (the "Pultec trick").
*/
class PassiveLCNetwork
{
public:
    void prepare(double sampleRate)
    {
        this->sampleRate = sampleRate;
        inductor.prepare(sampleRate);
        reset();
    }

    void reset()
    {
        inductor.reset();
        interactionState = 0.0f;
    }

    // Process LF section with true boost/cut interaction
    // All state is passed in by reference to ensure consistent ownership
    float processLFSection(float input, float boostGain, float attenGain, float frequency,
                           float& boostState1, float& boostState2, float& attenState)
    {
        if (boostGain < 0.01f && attenGain < 0.01f)
            return input;

        // Sanitize input
        if (!std::isfinite(input))
            input = 0.0f;

        // Clamp frequency to safe range (well below Nyquist/4 for stability)
        float maxFreq = static_cast<float>(sampleRate) * 0.1f;
        frequency = std::min(frequency, maxFreq);
        frequency = std::max(frequency, 10.0f);

        // Get frequency-dependent Q (inductor characteristic)
        float baseQ = 0.5f;  // Pultec's characteristic broad Q
        float effectiveQ = inductor.getFrequencyDependentQ(frequency, baseQ);
        effectiveQ = std::max(effectiveQ, 0.1f);  // Prevent division by zero

        // Calculate the interaction coefficient
        // When both boost and cut are engaged at the same frequency,
        // the resulting curve has a boost peak followed by a dip below
        float interactionCoeff = boostGain * attenGain * 0.3f;

        // === Boost path (resonant peak) ===
        float boostOutput = input;
        if (boostGain > 0.01f)
        {
            // Apply resonant boost with inductor Q
            float omega = juce::MathConstants<float>::twoPi * frequency / static_cast<float>(sampleRate);
            omega = std::min(omega, 0.5f);  // Keep omega stable
            float alpha = std::sin(omega) / (2.0f * effectiveQ);
            alpha = std::clamp(alpha, 0.001f, 0.99f);  // Clamp for stability

            // State variable filter for resonant boost
            float boostAmount = boostGain * 1.4f;  // Scale to dB

            // Simple resonant boost using state variable approach
            float invQ = 1.0f / effectiveQ;
            float hp = input - boostState1 * invQ - boostState2;
            float bp = hp * alpha + boostState1;
            float lp = bp * alpha + boostState2;

            // Clamp state to prevent runaway
            boostState1 = std::clamp(bp, -10.0f, 10.0f);
            boostState2 = std::clamp(lp, -10.0f, 10.0f);

            // Boost is primarily the band-pass response
            boostOutput = input + bp * boostAmount * 0.5f;

            // Apply inductor nonlinearity
            boostOutput = inductor.processNonlinearity(boostOutput, boostGain * 0.3f);
        }

        // === Atten path (shelf cut) ===
        float attenOutput = boostOutput;
        if (attenGain > 0.01f)
        {
            // Attenuation is a low shelf cut
            // In the real Pultec, it's slightly offset from the boost frequency
            float attenFreq = frequency * 0.85f;  // Atten centered slightly lower

            float attenAmount = attenGain * 1.6f;

            // Simple one-pole shelf approximation
            float wc = juce::MathConstants<float>::twoPi * attenFreq / static_cast<float>(sampleRate);
            wc = std::min(wc, 0.4f);  // Keep stable
            float g = std::tan(wc * 0.5f);
            g = std::clamp(g, 0.001f, 10.0f);
            float G = g / (1.0f + g);

            // Low frequencies get attenuated - use passed-in state
            float lowContent = attenOutput * G + attenState * (1.0f - G);
            attenState = std::clamp(lowContent, -10.0f, 10.0f);

            // Subtract attenuated low frequencies
            float attenFactor = juce::Decibels::decibelsToGain(-attenAmount);
            attenOutput = attenOutput - attenState * (1.0f - attenFactor);
        }

        // === Boost/Cut Interaction ===
        // When both are engaged, create the characteristic overshoot
        if (interactionCoeff > 0.01f)
        {
            // The interaction creates a slight boost overshoot above the cut frequency
            // This is the essence of the "Pultec trick"
            float interactionFreq = frequency * 1.3f;  // Overshoot appears above boost freq
            float omega = juce::MathConstants<float>::twoPi * interactionFreq / static_cast<float>(sampleRate);
            omega = std::min(omega, 0.5f);

            // Add subtle resonant bump at interaction frequency
            float resonantBoost = std::sin(omega * 0.5f) * interactionCoeff * 0.15f;
            interactionState = interactionState * 0.98f + resonantBoost * 0.02f;
            interactionState = std::clamp(interactionState, -1.0f, 1.0f);
            attenOutput += input * interactionState;
        }

        // Final output sanitization
        if (!std::isfinite(attenOutput))
            attenOutput = input;

        return attenOutput;
    }

private:
    double sampleRate = 44100.0;
    InductorModel inductor;
    float interactionState = 0.0f;
};

//==============================================================================
class PultecProcessor
{
public:
    // Parameter structure for Pultec EQ
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

        // Mid Dip/Peak Section (MEQ-5 style)
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

    PultecProcessor()
    {
        // Initialize with default tube type
    }

    void prepare(double sampleRate, int samplesPerBlock, int numChannels)
    {
        currentSampleRate = sampleRate;
        this->numChannels = numChannels;

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
        tubeStage.prepare(sampleRate, numChannels);
        lfNetwork.prepare(sampleRate);
        hfInductor.prepare(sampleRate);

        // Prepare transformers
        inputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.prepare(sampleRate, numChannels);

        // Set up transformer profiles for Pultec character
        setupTransformerProfiles();

        // Initialize analog emulation library
        AnalogEmulation::initializeLibrary();

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
        for (int i = 0; i < 2; ++i)
        {
            lfBoostState1[i] = 0.0f;
            lfBoostState2[i] = 0.0f;
            lfAttenStateLC[i] = 0.0f;
        }
    }

    void setParameters(const Parameters& newParams)
    {
        params = newParams;
        updateFilters();
        tubeStage.setDrive(params.tubeDrive);
    }

    const Parameters& getParameters() const { return params; }

    void process(juce::AudioBuffer<float>& buffer)
    {
        juce::ScopedNoDenormals noDenormals;

        if (params.bypass)
            return;

        const int numSamples = buffer.getNumSamples();
        const int channels = buffer.getNumChannels();

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
            bool isLeft = (ch == 0);

            for (int i = 0; i < numSamples; ++i)
            {
                float sample = channelData[i];

                // Input transformer coloration
                sample = inputTransformer.processSample(sample, ch);

                // === Passive LC Network: LF Section with true boost/cut interaction ===
                int chIdx = isLeft ? 0 : 1;
                sample = lfNetwork.processLFSection(
                    sample,
                    params.lfBoostGain,
                    params.lfAttenGain,
                    params.lfBoostFreq,
                    lfBoostState1[chIdx],
                    lfBoostState2[chIdx],
                    lfAttenStateLC[chIdx]
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

                // === Pultec-specific Tube Makeup Gain Stage ===
                if (params.tubeDrive > 0.01f)
                {
                    sample = tubeStage.processSample(sample, ch);
                }

                // Output transformer
                sample = outputTransformer.processSample(sample, ch);

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
        if (params.bypass)
            return 0.0f;

        float magnitudeDB = 0.0f;

        // Calculate contribution from each filter
        double omega = juce::MathConstants<double>::twoPi * frequencyHz / currentSampleRate;

        // LF Boost contribution
        if (params.lfBoostGain > 0.01f && lfBoostFilterL.coefficients != nullptr)
        {
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            auto& coeffs = *lfBoostFilterL.coefficients;

            std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
            std::complex<double> den = 1.0 + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

            float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
            magnitudeDB += filterMag;

            // Add interaction effect when both boost and atten are engaged
            if (params.lfAttenGain > 0.01f)
            {
                // The "Pultec trick" creates a bump above the cut frequency
                float interactionFreq = params.lfBoostFreq * 1.5f;
                if (frequencyHz > params.lfBoostFreq && frequencyHz < interactionFreq * 1.5f)
                {
                    float interactionAmount = params.lfBoostGain * params.lfAttenGain * 0.02f;
                    float relativePos = (frequencyHz - params.lfBoostFreq) / (interactionFreq - params.lfBoostFreq);
                    magnitudeDB += interactionAmount * std::sin(relativePos * juce::MathConstants<float>::pi);
                }
            }
        }

        // LF Atten contribution
        if (params.lfAttenGain > 0.01f && lfAttenFilterL.coefficients != nullptr)
        {
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            auto& coeffs = *lfAttenFilterL.coefficients;

            std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
            std::complex<double> den = 1.0 + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

            float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
            magnitudeDB += filterMag;
        }

        // HF Boost contribution
        if (params.hfBoostGain > 0.01f && hfBoostFilterL.coefficients != nullptr)
        {
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            auto& coeffs = *hfBoostFilterL.coefficients;

            std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
            std::complex<double> den = 1.0 + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

            float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
            magnitudeDB += filterMag;
        }

        // HF Atten contribution
        if (params.hfAttenGain > 0.01f && hfAttenFilterL.coefficients != nullptr)
        {
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            auto& coeffs = *hfAttenFilterL.coefficients;

            std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
            std::complex<double> den = 1.0 + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

            float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
            magnitudeDB += filterMag;
        }

        // ===== MID SECTION CONTRIBUTIONS =====
        if (params.midEnabled)
        {
            // Mid Low Peak contribution
            if (params.midLowPeak > 0.01f && midLowPeakFilterL.coefficients != nullptr)
            {
                std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
                auto& coeffs = *midLowPeakFilterL.coefficients;

                std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
                std::complex<double> den = 1.0 + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

                float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
                magnitudeDB += filterMag;
            }

            // Mid Dip contribution
            if (params.midDip > 0.01f && midDipFilterL.coefficients != nullptr)
            {
                std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
                auto& coeffs = *midDipFilterL.coefficients;

                std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
                std::complex<double> den = 1.0 + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

                float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
                magnitudeDB += filterMag;
            }

            // Mid High Peak contribution
            if (params.midHighPeak > 0.01f && midHighPeakFilterL.coefficients != nullptr)
            {
                std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
                auto& coeffs = *midHighPeakFilterL.coefficients;

                std::complex<double> num = static_cast<double>(coeffs.coefficients[0]) + static_cast<double>(coeffs.coefficients[1]) / z + static_cast<double>(coeffs.coefficients[2]) / (z * z);
                std::complex<double> den = 1.0 + static_cast<double>(coeffs.coefficients[4]) / z + static_cast<double>(coeffs.coefficients[5]) / (z * z);

                float filterMag = static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
                magnitudeDB += filterMag;
            }
        }

        return magnitudeDB;
    }

private:
    Parameters params;
    double currentSampleRate = 44100.0;
    int numChannels = 2;

    // LF Boost: Resonant peak filter
    juce::dsp::IIR::Filter<float> lfBoostFilterL, lfBoostFilterR;

    // LF Atten: Low shelf cut
    juce::dsp::IIR::Filter<float> lfAttenFilterL, lfAttenFilterR;

    // HF Boost: Resonant peak with bandwidth
    juce::dsp::IIR::Filter<float> hfBoostFilterL, hfBoostFilterR;

    // HF Atten: High shelf cut
    juce::dsp::IIR::Filter<float> hfAttenFilterL, hfAttenFilterR;

    // Mid Section filters (MEQ-5 style)
    juce::dsp::IIR::Filter<float> midLowPeakFilterL, midLowPeakFilterR;
    juce::dsp::IIR::Filter<float> midDipFilterL, midDipFilterR;
    juce::dsp::IIR::Filter<float> midHighPeakFilterL, midHighPeakFilterR;

    // Enhanced analog stages
    PultecTubeStage tubeStage;
    PassiveLCNetwork lfNetwork;
    InductorModel hfInductor;

    // LC network state variables for boost/cut interaction
    // boostState has 2 floats per channel (state1, state2 for the state variable filter)
    // attenStateLC has 1 float per channel (for the one-pole shelf in LC network)
    float lfBoostState1[2] = {0.0f, 0.0f};
    float lfBoostState2[2] = {0.0f, 0.0f};
    float lfAttenStateLC[2] = {0.0f, 0.0f};

    // Transformers (UTC A-20 style)
    AnalogEmulation::TransformerEmulation inputTransformer;
    AnalogEmulation::TransformerEmulation outputTransformer;

    void setupTransformerProfiles()
    {
        // Create Pultec-style transformer profiles
        // UTC A-20 input transformer characteristics
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
        // Pultec LF boost: Resonant peak at selected frequency
        // The EQP-1A has a unique broad, musical low boost
        float freq = pultecPreWarpFrequency(params.lfBoostFreq, currentSampleRate);
        float gainDB = params.lfBoostGain * 1.4f;  // 0-10 maps to ~0-14 dB

        // Get frequency-dependent Q from inductor model
        InductorModel tempInductor;
        tempInductor.prepare(currentSampleRate);
        float baseQ = 0.5f;  // Very broad (Pultec characteristic)
        float effectiveQ = tempInductor.getFrequencyDependentQ(params.lfBoostFreq, baseQ);

        auto coeffs = makePultecPeak(currentSampleRate, freq, effectiveQ, gainDB);
        lfBoostFilterL.coefficients = coeffs;
        lfBoostFilterR.coefficients = coeffs;
    }

    void updateLFAtten()
    {
        // Pultec LF atten: Shelf cut that interacts with boost
        // When both are engaged at the same frequency, creates the "Pultec trick"
        float freq = pultecPreWarpFrequency(params.lfBoostFreq, currentSampleRate);
        float gainDB = -params.lfAttenGain * 1.6f;  // 0-10 maps to ~0-16 dB cut

        // The attenuation is a shelf, not a peak
        auto coeffs = makeLowShelf(currentSampleRate, freq, 0.7f, gainDB);
        lfAttenFilterL.coefficients = coeffs;
        lfAttenFilterR.coefficients = coeffs;
    }

    void updateHFBoost()
    {
        // Pultec HF boost: Resonant peak with variable bandwidth
        float freq = pultecPreWarpFrequency(params.hfBoostFreq, currentSampleRate);
        float gainDB = params.hfBoostGain * 1.6f;  // 0-10 maps to ~0-16 dB

        // Bandwidth control: Sharp (high Q) to Broad (low Q)
        // Inverted mapping: 0 = sharp (high Q), 1 = broad (low Q)
        float baseQ = juce::jmap(params.hfBoostBandwidth, 0.0f, 1.0f, 2.5f, 0.5f);

        // Apply frequency-dependent Q modification
        InductorModel tempInductor;
        tempInductor.prepare(currentSampleRate);
        float effectiveQ = tempInductor.getFrequencyDependentQ(params.hfBoostFreq, baseQ);

        auto coeffs = makePultecPeak(currentSampleRate, freq, effectiveQ, gainDB);
        hfBoostFilterL.coefficients = coeffs;
        hfBoostFilterR.coefficients = coeffs;
    }

    void updateHFAtten()
    {
        // Pultec HF atten: High shelf cut
        float freq = pultecPreWarpFrequency(params.hfAttenFreq, currentSampleRate);
        float gainDB = -params.hfAttenGain * 2.0f;  // 0-10 maps to ~0-20 dB cut

        auto coeffs = makeHighShelf(currentSampleRate, freq, 0.6f, gainDB);
        hfAttenFilterL.coefficients = coeffs;
        hfAttenFilterR.coefficients = coeffs;
    }

    void updateMidLowPeak()
    {
        // Mid Low Peak: Resonant boost in low-mid range
        float freq = pultecPreWarpFrequency(params.midLowFreq, currentSampleRate);
        float gainDB = params.midLowPeak * 1.2f;  // 0-10 maps to ~0-12 dB

        // Moderate Q for musical character
        float q = 1.2f;

        auto coeffs = makePultecPeak(currentSampleRate, freq, q, gainDB);
        midLowPeakFilterL.coefficients = coeffs;
        midLowPeakFilterR.coefficients = coeffs;
    }

    void updateMidDip()
    {
        // Mid Dip: Cut in mid range
        float freq = pultecPreWarpFrequency(params.midDipFreq, currentSampleRate);
        float gainDB = -params.midDip * 1.0f;  // 0-10 maps to ~0-10 dB cut

        // Broader Q for natural sounding cut
        float q = 0.8f;

        auto coeffs = makePultecPeak(currentSampleRate, freq, q, gainDB);
        midDipFilterL.coefficients = coeffs;
        midDipFilterR.coefficients = coeffs;
    }

    void updateMidHighPeak()
    {
        // Mid High Peak: Resonant boost in upper-mid range
        float freq = pultecPreWarpFrequency(params.midHighFreq, currentSampleRate);
        float gainDB = params.midHighPeak * 1.2f;  // 0-10 maps to ~0-12 dB

        // Moderate Q for presence
        float q = 1.4f;

        auto coeffs = makePultecPeak(currentSampleRate, freq, q, gainDB);
        midHighPeakFilterL.coefficients = coeffs;
        midHighPeakFilterR.coefficients = coeffs;
    }

    // Pultec-style peak filter with inductor characteristics
    juce::dsp::IIR::Coefficients<float>::Ptr makePultecPeak(
        double sampleRate, float freq, float q, float gainDB) const
    {
        // The Pultec uses inductors which have a more gradual slope than
        // typical parametric EQs, especially on the low end
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);

        // Inductor-style Q modification - broader, more musical
        float pultecQ = q * 0.85f;  // Slightly broader than specified
        float alpha = sinw0 / (2.0f * pultecQ);

        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cosw0;
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha / A;

        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

        return juce::dsp::IIR::Coefficients<float>::Ptr(
            new juce::dsp::IIR::Coefficients<float>(b0, b1, b2, 1.0f, a1, a2));
    }

    juce::dsp::IIR::Coefficients<float>::Ptr makeLowShelf(
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

        return juce::dsp::IIR::Coefficients<float>::Ptr(
            new juce::dsp::IIR::Coefficients<float>(b0, b1, b2, 1.0f, a1, a2));
    }

    juce::dsp::IIR::Coefficients<float>::Ptr makeHighShelf(
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

        return juce::dsp::IIR::Coefficients<float>::Ptr(
            new juce::dsp::IIR::Coefficients<float>(b0, b1, b2, 1.0f, a1, a2));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PultecProcessor)
};

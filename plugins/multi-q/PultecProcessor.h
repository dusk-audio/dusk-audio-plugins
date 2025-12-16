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

    Features:
    - Low Frequency Boost (20-100 Hz) with bandwidth control
    - Low Frequency Atten (20-100 Hz) - separate from boost
    - High Frequency Boost (3-16 kHz) with bandwidth control
    - High Frequency Atten (5-20 kHz) - shelf attenuation
    - Authentic tube harmonic generation
    - Transformer coloration (input and output)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../shared/AnalogEmulation/AnalogEmulation.h"
#include <array>
#include <atomic>

// Helper for LC filter pre-warping
inline float pultecPreWarpFrequency(float freq, double sampleRate)
{
    const float omega = juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
    return static_cast<float>(sampleRate) / juce::MathConstants<float>::pi * std::tan(omega);
}

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

        // Global controls
        float inputGain = 0.0f;        // -12 to +12 dB
        float outputGain = 0.0f;       // -12 to +12 dB
        float tubeDrive = 0.3f;        // 0-1 (tube saturation amount)
        bool bypass = false;
    };

    PultecProcessor()
    {
        tubeStage.setTubeType(AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
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

        // Prepare tube stage
        tubeStage.prepare(sampleRate, numChannels);

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
        tubeStage.reset();
        inputTransformer.reset();
        outputTransformer.reset();
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

                // === Passive EQ Network ===
                // The Pultec's unique character comes from the passive LC network
                // where boost and cut interact in interesting ways

                // LF Boost (resonant peak at selected frequency)
                if (params.lfBoostGain > 0.01f)
                {
                    sample = isLeft ? lfBoostFilterL.processSample(sample)
                                    : lfBoostFilterR.processSample(sample);
                }

                // LF Attenuation (shelf, interacts with boost)
                if (params.lfAttenGain > 0.01f)
                {
                    sample = isLeft ? lfAttenFilterL.processSample(sample)
                                    : lfAttenFilterR.processSample(sample);
                }

                // HF Boost (resonant peak with bandwidth control)
                if (params.hfBoostGain > 0.01f)
                {
                    sample = isLeft ? hfBoostFilterL.processSample(sample)
                                    : hfBoostFilterR.processSample(sample);
                }

                // HF Attenuation (shelf)
                if (params.hfAttenGain > 0.01f)
                {
                    sample = isLeft ? hfAttenFilterL.processSample(sample)
                                    : hfAttenFilterR.processSample(sample);
                }

                // === Tube Makeup Gain Stage ===
                // 12AX7 tube stage adds harmonics and gentle compression
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

    // Tube makeup gain stage (12AX7)
    AnalogEmulation::TubeEmulation tubeStage;

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
    }

    void updateLFBoost()
    {
        // Pultec LF boost: Resonant peak at selected frequency
        // The EQP-1A has a unique broad, musical low boost
        float freq = pultecPreWarpFrequency(params.lfBoostFreq, currentSampleRate);
        float gainDB = params.lfBoostGain * 1.4f;  // 0-10 maps to ~0-14 dB

        // Pultec LF boost has a very broad Q (low Q value = wide bandwidth)
        // This is what makes it sound so musical
        float q = 0.5f;  // Very broad

        auto coeffs = makePultecPeak(currentSampleRate, freq, q, gainDB);
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
        float q = juce::jmap(params.hfBoostBandwidth, 0.0f, 1.0f, 2.5f, 0.5f);

        auto coeffs = makePultecPeak(currentSampleRate, freq, q, gainDB);
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
        float pultecQ = q * 0.8f;  // Slightly broader than specified
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

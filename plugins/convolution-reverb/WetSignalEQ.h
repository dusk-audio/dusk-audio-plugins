/*
  ==============================================================================

    Convolution Reverb - Wet Signal EQ
    4-band parametric EQ for the reverb wet signal
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>

class WetSignalEQ
{
public:
    WetSignalEQ() = default;
    ~WetSignalEQ() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        // Prepare biquad state for all channels
        lowShelfCoeffs.prepare(spec.numChannels);
        lowMidCoeffs.prepare(spec.numChannels);
        highMidCoeffs.prepare(spec.numChannels);
        highShelfCoeffs.prepare(spec.numChannels);

        // Prepare all filters
        lowShelfFilter.prepare(spec);
        lowMidFilter.prepare(spec);
        highMidFilter.prepare(spec);
        highShelfFilter.prepare(spec);

        // Set filter types
        lowShelfFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        highShelfFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);

        updateAllFilters();
    }

    void reset()
    {
        lowShelfCoeffs.reset();
        lowMidCoeffs.reset();
        highMidCoeffs.reset();
        highShelfCoeffs.reset();

        lowShelfFilter.reset();
        lowMidFilter.reset();
        highMidFilter.reset();
        highShelfFilter.reset();
    }

    // Low shelf (20-500 Hz, ±12 dB)
    void setLowShelf(float freq, float gainDb)
    {
        lowShelfFreq = juce::jlimit(20.0f, 500.0f, freq);
        lowShelfGain = juce::jlimit(-12.0f, 12.0f, gainDb);
        updateLowShelf();
    }

    // Low-mid peak (200-2000 Hz, ±12 dB)
    void setLowMid(float freq, float gainDb)
    {
        lowMidFreq = juce::jlimit(200.0f, 2000.0f, freq);
        lowMidGain = juce::jlimit(-12.0f, 12.0f, gainDb);
        updateLowMid();
    }

    // High-mid peak (1000-8000 Hz, ±12 dB)
    void setHighMid(float freq, float gainDb)
    {
        highMidFreq = juce::jlimit(1000.0f, 8000.0f, freq);
        highMidGain = juce::jlimit(-12.0f, 12.0f, gainDb);
        updateHighMid();
    }

    // High shelf (2000-20000 Hz, ±12 dB)
    void setHighShelf(float freq, float gainDb)
    {
        highShelfFreq = juce::jlimit(2000.0f, 20000.0f, freq);
        highShelfGain = juce::jlimit(-12.0f, 12.0f, gainDb);
        updateHighShelf();
    }

    void processBlock(juce::AudioBuffer<float>& buffer)
    {
        // Process each channel with the 4-band EQ
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            float* data = buffer.getWritePointer(channel);
            int numSamples = buffer.getNumSamples();

            for (int i = 0; i < numSamples; ++i)
            {
                float sample = data[i];

                // Apply low shelf (using biquad)
                if (std::abs(lowShelfGain) > 0.1f)
                    sample = processLowShelf(sample, channel);

                // Apply low-mid peak
                if (std::abs(lowMidGain) > 0.1f)
                    sample = processLowMid(sample, channel);

                // Apply high-mid peak
                if (std::abs(highMidGain) > 0.1f)
                    sample = processHighMid(sample, channel);

                // Apply high shelf
                if (std::abs(highShelfGain) > 0.1f)
                    sample = processHighShelf(sample, channel);

                data[i] = sample;
            }
        }
    }

private:
    double sampleRate = 44100.0;

    // Filter parameters
    float lowShelfFreq = 100.0f;
    float lowShelfGain = 0.0f;
    float lowMidFreq = 600.0f;
    float lowMidGain = 0.0f;
    float highMidFreq = 3000.0f;
    float highMidGain = 0.0f;
    float highShelfFreq = 8000.0f;
    float highShelfGain = 0.0f;

    // Biquad coefficients for each band
    struct BiquadCoeffs
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;

        // State for multi-channel processing
        std::vector<float> z1;
        std::vector<float> z2;

        void prepare(size_t numChannels)
        {
            z1.resize(numChannels, 0.0f);
            z2.resize(numChannels, 0.0f);
        }

        float process(float input, size_t channel)
        {
            if (channel >= z1.size())
                return input; // Safety check

            float output = b0 * input + z1[channel];
            z1[channel] = b1 * input - a1 * output + z2[channel];
            z2[channel] = b2 * input - a2 * output;
            return output;
        }

        void reset()
        {
            std::fill(z1.begin(), z1.end(), 0.0f);
            std::fill(z2.begin(), z2.end(), 0.0f);
        }
    };

    BiquadCoeffs lowShelfCoeffs;
    BiquadCoeffs lowMidCoeffs;
    BiquadCoeffs highMidCoeffs;
    BiquadCoeffs highShelfCoeffs;

    // State variable filters (for smoother response)
    juce::dsp::StateVariableTPTFilter<float> lowShelfFilter;
    juce::dsp::StateVariableTPTFilter<float> highShelfFilter;
    juce::dsp::IIR::Filter<float> lowMidFilter;
    juce::dsp::IIR::Filter<float> highMidFilter;

    void updateAllFilters()
    {
        updateLowShelf();
        updateLowMid();
        updateHighMid();
        updateHighShelf();
    }

    void updateLowShelf()
    {
        // Calculate low shelf biquad coefficients
        float A = std::pow(10.0f, lowShelfGain / 40.0f);
        float w0 = 2.0f * juce::MathConstants<float>::pi * lowShelfFreq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / 0.707f - 1.0f) + 2.0f);

        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
        lowShelfCoeffs.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha)) / a0;
        lowShelfCoeffs.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        lowShelfCoeffs.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha)) / a0;
        lowShelfCoeffs.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        lowShelfCoeffs.a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha) / a0;
    }

    void updateLowMid()
    {
        // Calculate peaking EQ biquad coefficients
        float A = std::pow(10.0f, lowMidGain / 40.0f);
        float w0 = 2.0f * juce::MathConstants<float>::pi * lowMidFreq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float Q = 1.0f; // Q factor
        float alpha = sinw0 / (2.0f * Q);

        float a0 = 1.0f + alpha / A;
        lowMidCoeffs.b0 = (1.0f + alpha * A) / a0;
        lowMidCoeffs.b1 = (-2.0f * cosw0) / a0;
        lowMidCoeffs.b2 = (1.0f - alpha * A) / a0;
        lowMidCoeffs.a1 = (-2.0f * cosw0) / a0;
        lowMidCoeffs.a2 = (1.0f - alpha / A) / a0;
    }

    void updateHighMid()
    {
        // Calculate peaking EQ biquad coefficients
        float A = std::pow(10.0f, highMidGain / 40.0f);
        float w0 = 2.0f * juce::MathConstants<float>::pi * highMidFreq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float Q = 1.0f;
        float alpha = sinw0 / (2.0f * Q);

        float a0 = 1.0f + alpha / A;
        highMidCoeffs.b0 = (1.0f + alpha * A) / a0;
        highMidCoeffs.b1 = (-2.0f * cosw0) / a0;
        highMidCoeffs.b2 = (1.0f - alpha * A) / a0;
        highMidCoeffs.a1 = (-2.0f * cosw0) / a0;
        highMidCoeffs.a2 = (1.0f - alpha / A) / a0;
    }

    void updateHighShelf()
    {
        // Calculate high shelf biquad coefficients
        float A = std::pow(10.0f, highShelfGain / 40.0f);
        float w0 = 2.0f * juce::MathConstants<float>::pi * highShelfFreq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / 0.707f - 1.0f) + 2.0f);

        float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
        highShelfCoeffs.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha)) / a0;
        highShelfCoeffs.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        highShelfCoeffs.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha)) / a0;
        highShelfCoeffs.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        highShelfCoeffs.a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha) / a0;
    }

    float processLowShelf(float sample, int channel)
    {
        return lowShelfCoeffs.process(sample, channel);
    }

    float processLowMid(float sample, int channel)
    {
        return lowMidCoeffs.process(sample, channel);
    }

    float processHighMid(float sample, int channel)
    {
        return highMidCoeffs.process(sample, channel);
    }

    float processHighShelf(float sample, int channel)
    {
        return highShelfCoeffs.process(sample, channel);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WetSignalEQ)
};

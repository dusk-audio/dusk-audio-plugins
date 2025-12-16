/*
  ==============================================================================

    BritishEQProcessor.h

    SSL 4000 Series Console EQ processor for Multi-Q's British mode.
    Based on the standalone 4K-EQ plugin DSP code.

    Features:
    - 4-band parametric EQ (LF, LM, HM, HF)
    - High-pass and low-pass filters
    - Brown/Black knob variants (E-Series/G-Series)
    - SSL saturation modeling
    - Transformer phase shift (E-Series)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SSLSaturation.h"
#include <array>
#include <atomic>

// Helper function to prevent frequency cramping at high frequencies
inline float britishPreWarpFrequency(float freq, double sampleRate)
{
    const float nyquist = static_cast<float>(sampleRate * 0.5);
    const float omega = juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
    float warpedFreq = static_cast<float>(sampleRate) / juce::MathConstants<float>::pi * std::tan(omega);

    if (freq > 3000.0f)
    {
        float ratio = freq / nyquist;
        float compensation = 1.0f;
        if (ratio < 0.3f)
            compensation = 1.0f + (ratio - 0.136f) * 0.15f;
        else if (ratio < 0.5f)
            compensation = 1.0f + (ratio - 0.3f) * 0.4f;
        else
            compensation = 1.0f + (ratio - 0.5f) * 0.6f;
        warpedFreq = freq * compensation;
    }

    return std::min(warpedFreq, nyquist * 0.99f);
}

class BritishEQProcessor
{
public:
    // Parameter structure for British EQ
    struct Parameters
    {
        // HPF/LPF
        float hpfFreq = 20.0f;
        bool hpfEnabled = false;
        float lpfFreq = 20000.0f;
        bool lpfEnabled = false;

        // 4-band EQ
        float lfGain = 0.0f;
        float lfFreq = 100.0f;
        bool lfBell = false;

        float lmGain = 0.0f;
        float lmFreq = 600.0f;
        float lmQ = 0.7f;

        float hmGain = 0.0f;
        float hmFreq = 2000.0f;
        float hmQ = 0.7f;

        float hfGain = 0.0f;
        float hfFreq = 8000.0f;
        bool hfBell = false;

        // Global
        bool isBlackMode = false;  // false = Brown (E-Series), true = Black (G-Series)
        float saturation = 0.0f;   // 0-100%
        float inputGain = 0.0f;    // dB
        float outputGain = 0.0f;   // dB
    };

    BritishEQProcessor() = default;

    void prepare(double sampleRate, int samplesPerBlock, int numChannels)
    {
        currentSampleRate = sampleRate;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 1;

        // Prepare HPF stages
        hpfStage1L.prepare(spec);
        hpfStage1R.prepare(spec);
        hpfStage2L.prepare(spec);
        hpfStage2R.prepare(spec);

        // Prepare LPF
        lpfL.prepare(spec);
        lpfR.prepare(spec);

        // Prepare 4-band EQ
        lfFilterL.prepare(spec);
        lfFilterR.prepare(spec);
        lmFilterL.prepare(spec);
        lmFilterR.prepare(spec);
        hmFilterL.prepare(spec);
        hmFilterR.prepare(spec);
        hfFilterL.prepare(spec);
        hfFilterR.prepare(spec);

        // Prepare phase shift
        phaseShiftL.prepare(spec);
        phaseShiftR.prepare(spec);
        updatePhaseShift();

        // SSL saturation
        sslSaturation.setSampleRate(sampleRate);
        sslSaturation.reset();

        reset();
    }

    void reset()
    {
        hpfStage1L.reset();
        hpfStage1R.reset();
        hpfStage2L.reset();
        hpfStage2R.reset();
        lpfL.reset();
        lpfR.reset();
        lfFilterL.reset();
        lfFilterR.reset();
        lmFilterL.reset();
        lmFilterR.reset();
        hmFilterL.reset();
        hmFilterR.reset();
        hfFilterL.reset();
        hfFilterR.reset();
        phaseShiftL.reset();
        phaseShiftR.reset();
        sslSaturation.reset();
    }

    void setParameters(const Parameters& newParams)
    {
        params = newParams;
        updateFilters();
        sslSaturation.setConsoleType(params.isBlackMode
            ? SSLSaturation::ConsoleType::GSeries
            : SSLSaturation::ConsoleType::ESeries);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // Apply input gain
        if (std::abs(params.inputGain) > 0.01f)
        {
            float inputGainLinear = juce::Decibels::decibelsToGain(params.inputGain);
            buffer.applyGain(inputGainLinear);
        }

        // Process each channel
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* channelData = buffer.getWritePointer(ch);
            bool isLeft = (ch == 0);

            for (int i = 0; i < numSamples; ++i)
            {
                float sample = channelData[i];

                // HPF (18 dB/oct)
                if (params.hpfEnabled)
                {
                    sample = isLeft ? hpfStage1L.processSample(sample) : hpfStage1R.processSample(sample);
                    sample = isLeft ? hpfStage2L.processSample(sample) : hpfStage2R.processSample(sample);
                }

                // 4-band EQ
                sample = isLeft ? lfFilterL.processSample(sample) : lfFilterR.processSample(sample);
                sample = isLeft ? lmFilterL.processSample(sample) : lmFilterR.processSample(sample);
                sample = isLeft ? hmFilterL.processSample(sample) : hmFilterR.processSample(sample);
                sample = isLeft ? hfFilterL.processSample(sample) : hfFilterR.processSample(sample);

                // LPF (12 dB/oct)
                if (params.lpfEnabled)
                {
                    sample = isLeft ? lpfL.processSample(sample) : lpfR.processSample(sample);
                }

                // Transformer phase shift (E-Series only)
                if (!params.isBlackMode)
                {
                    sample = isLeft ? phaseShiftL.processSample(sample) : phaseShiftR.processSample(sample);
                }

                // SSL saturation
                if (params.saturation > 0.1f)
                {
                    float satAmount = params.saturation * 0.01f;
                    sample = sslSaturation.processSample(sample, satAmount, isLeft);
                }

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

private:
    Parameters params;
    double currentSampleRate = 44100.0;

    // HPF: 3rd order (1st + 2nd order = 18 dB/oct)
    juce::dsp::IIR::Filter<float> hpfStage1L, hpfStage1R;
    juce::dsp::IIR::Filter<float> hpfStage2L, hpfStage2R;

    // LPF: 2nd order (12 dB/oct)
    juce::dsp::IIR::Filter<float> lpfL, lpfR;

    // 4-band EQ
    juce::dsp::IIR::Filter<float> lfFilterL, lfFilterR;
    juce::dsp::IIR::Filter<float> lmFilterL, lmFilterR;
    juce::dsp::IIR::Filter<float> hmFilterL, hmFilterR;
    juce::dsp::IIR::Filter<float> hfFilterL, hfFilterR;

    // Phase shift for E-Series transformer emulation
    juce::dsp::IIR::Filter<float> phaseShiftL, phaseShiftR;

    // SSL saturation
    SSLSaturation sslSaturation;

    void updateFilters()
    {
        updateHPF();
        updateLPF();
        updateLFBand();
        updateLMBand();
        updateHMBand();
        updateHFBand();
    }

    void updatePhaseShift()
    {
        // All-pass filter for transformer phase rotation at ~200Hz
        float freq = 200.0f;
        float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(currentSampleRate);
        float tan_w0 = std::tan(w0 / 2.0f);

        float a0 = 1.0f + tan_w0;
        float a1 = (1.0f - tan_w0) / a0;
        float b0 = a1;
        float b1 = 1.0f;

        auto coeffs = juce::dsp::IIR::Coefficients<float>::Ptr(
            new juce::dsp::IIR::Coefficients<float>(b0, b1, 0.0f, 1.0f, a1, 0.0f));
        phaseShiftL.coefficients = coeffs;
        phaseShiftR.coefficients = coeffs;
    }

    void updateHPF()
    {
        float freq = params.hpfFreq;

        // Stage 1: 1st-order highpass
        auto coeffs1st = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass(currentSampleRate, freq);
        if (coeffs1st)
        {
            hpfStage1L.coefficients = coeffs1st;
            hpfStage1R.coefficients = coeffs1st;
        }

        // Stage 2: 2nd-order highpass with SSL Q
        const float sslHPFQ = 0.54f;
        auto coeffs2nd = juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSampleRate, freq, sslHPFQ);
        if (coeffs2nd)
        {
            hpfStage2L.coefficients = coeffs2nd;
            hpfStage2R.coefficients = coeffs2nd;
        }
    }

    void updateLPF()
    {
        float freq = params.lpfFreq;

        float processFreq = freq;
        if (freq > currentSampleRate * 0.3f)
            processFreq = britishPreWarpFrequency(freq, currentSampleRate);

        float q = params.isBlackMode ? 0.8f : 0.707f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSampleRate, processFreq, q);
        if (coeffs)
        {
            lpfL.coefficients = coeffs;
            lpfR.coefficients = coeffs;
        }
    }

    void updateLFBand()
    {
        if (params.isBlackMode && params.lfBell)
        {
            auto coeffs = makeSSLPeak(currentSampleRate, params.lfFreq, 0.7f, params.lfGain, params.isBlackMode);
            lfFilterL.coefficients = coeffs;
            lfFilterR.coefficients = coeffs;
        }
        else
        {
            auto coeffs = makeSSLShelf(currentSampleRate, params.lfFreq, 0.7f, params.lfGain, false, params.isBlackMode);
            lfFilterL.coefficients = coeffs;
            lfFilterR.coefficients = coeffs;
        }
    }

    void updateLMBand()
    {
        float q = params.lmQ;
        if (params.isBlackMode)
            q = calculateDynamicQ(params.lmGain, q);

        auto coeffs = makeSSLPeak(currentSampleRate, params.lmFreq, q, params.lmGain, params.isBlackMode);
        lmFilterL.coefficients = coeffs;
        lmFilterR.coefficients = coeffs;
    }

    void updateHMBand()
    {
        float freq = params.hmFreq;
        float q = params.hmQ;

        if (params.isBlackMode)
        {
            q = calculateDynamicQ(params.hmGain, q);
        }
        else
        {
            if (freq > 7000.0f)
                freq = 7000.0f;
        }

        float processFreq = freq;
        if (freq > 3000.0f)
            processFreq = britishPreWarpFrequency(freq, currentSampleRate);

        auto coeffs = makeSSLPeak(currentSampleRate, processFreq, q, params.hmGain, params.isBlackMode);
        hmFilterL.coefficients = coeffs;
        hmFilterR.coefficients = coeffs;
    }

    void updateHFBand()
    {
        float warpedFreq = britishPreWarpFrequency(params.hfFreq, currentSampleRate);

        if (params.isBlackMode && params.hfBell)
        {
            auto coeffs = makeSSLPeak(currentSampleRate, warpedFreq, 0.7f, params.hfGain, params.isBlackMode);
            hfFilterL.coefficients = coeffs;
            hfFilterR.coefficients = coeffs;
        }
        else
        {
            auto coeffs = makeSSLShelf(currentSampleRate, warpedFreq, 0.7f, params.hfGain, true, params.isBlackMode);
            hfFilterL.coefficients = coeffs;
            hfFilterR.coefficients = coeffs;
        }
    }

    float calculateDynamicQ(float gain, float baseQ) const
    {
        float absGain = std::abs(gain);
        float scale = (gain >= 0.0f) ? 2.0f : 1.5f;
        float dynamicQ = baseQ * (1.0f + (absGain / 20.0f) * scale);
        return juce::jlimit(0.5f, 8.0f, dynamicQ);
    }

    juce::dsp::IIR::Coefficients<float>::Ptr makeSSLShelf(
        double sampleRate, float freq, float q, float gainDB, bool isHighShelf, bool isBlackMode) const
    {
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);

        float sslQ = q;
        if (isBlackMode)
            sslQ *= 1.4f;
        else
            sslQ *= 0.65f;

        float alpha = sinw0 / (2.0f * sslQ);

        float b0, b1, b2, a0, a1, a2;

        if (isHighShelf)
        {
            b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha);
            a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
            a2 = (A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha;
        }
        else
        {
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha);
            a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
            a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha;
        }

        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

        return juce::dsp::IIR::Coefficients<float>::Ptr(
            new juce::dsp::IIR::Coefficients<float>(b0, b1, b2, 1.0f, a1, a2));
    }

    juce::dsp::IIR::Coefficients<float>::Ptr makeSSLPeak(
        double sampleRate, float freq, float q, float gainDB, bool isBlackMode) const
    {
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);

        float sslQ = q;

        if (isBlackMode && std::abs(gainDB) > 0.1f)
        {
            float gainFactor = std::abs(gainDB) / 15.0f;
            if (gainDB > 0.0f)
                sslQ *= (1.0f + gainFactor * 1.2f);
            else
                sslQ *= (1.0f + gainFactor * 0.6f);
        }

        sslQ = juce::jlimit(0.1f, 10.0f, sslQ);
        float alpha = sinw0 / (2.0f * sslQ);

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BritishEQProcessor)
};

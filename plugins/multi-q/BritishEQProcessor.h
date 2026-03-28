/*
  ==============================================================================

    BritishEQProcessor.h

    British console EQ processor for Multi-Q's British mode.
    Based on the standalone 4K-EQ plugin DSP code.

    Features:
    - 4-band parametric EQ (LF, LM, HM, HF)
    - High-pass and low-pass filters
    - Brown/Black knob variants (E-Series/G-Series)
    - Console saturation modeling
    - Transformer phase shift (E-Series)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ConsoleSaturation.h"
#include <array>
#include <atomic>

// Standard bilinear transform pre-warping: ensures the filter's cutoff/center
// frequency lands at the correct position in the digital domain.
// Previous implementation used a piecewise-linear heuristic for f > 3kHz that
// was less accurate than the standard tan() formula and could mismatch the
// actual IIR filter response.
inline float britishPreWarpFrequency(float freq, double sampleRate)
{
    const float nyquist = static_cast<float>(sampleRate * 0.5);
    // Clamp freq below Nyquist to prevent tan() from producing negative/extreme values
    float safeFreq = std::min(freq, nyquist * 0.98f);
    safeFreq = std::max(safeFreq, 1.0f);
    const float omega = juce::MathConstants<float>::pi * safeFreq / static_cast<float>(sampleRate);
    float warpedFreq = static_cast<float>(sampleRate) / juce::MathConstants<float>::pi * std::tan(omega);
    return std::min(std::max(warpedFreq, 1.0f), nyquist * 0.99f);
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

    void prepare(double sampleRate, int samplesPerBlock, int /*numChannels*/)
    {
        currentSampleRate.store(sampleRate, std::memory_order_release);

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

        // Pre-allocate coefficients for all filters (off audio thread)
        // so that updateFilters() can modify them in-place without heap allocations
        initFilterCoefficients(lfFilterL);
        initFilterCoefficients(lfFilterR);
        initFilterCoefficients(lmFilterL);
        initFilterCoefficients(lmFilterR);
        initFilterCoefficients(hmFilterL);
        initFilterCoefficients(hmFilterR);
        initFilterCoefficients(hfFilterL);
        initFilterCoefficients(hfFilterR);
        initFilterCoefficients(hpfStage1L);
        initFilterCoefficients(hpfStage1R);
        initFilterCoefficients(hpfStage2L);
        initFilterCoefficients(hpfStage2R);
        initFilterCoefficients(lpfL);
        initFilterCoefficients(lpfR);
        initFilterCoefficients(phaseShiftL);
        initFilterCoefficients(phaseShiftR);

        updatePhaseShift();

        // Console saturation
        consoleSaturation.setSampleRate(sampleRate);
        consoleSaturation.reset();

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
        consoleSaturation.reset();
    }

    /** Sample-rate update. Called from processBlock when rate changes.
        Resets filter state, updates the cached rate, and refreshes rate-dependent
        components (saturation, phase shift coefficients). Caller must call
        setParameters() or otherwise trigger updateFilters() so that filter coefficients
        are recalculated for the new rate. */
    void updateSampleRate(double newRate)
    {
        currentSampleRate.store(newRate, std::memory_order_release);
        consoleSaturation.setSampleRate(newRate);
        updatePhaseShift();
        reset();
    }

    void setParameters(const Parameters& newParams)
    {
        params = newParams;
        updateFilters();
        consoleSaturation.setConsoleType(params.isBlackMode
            ? ConsoleSaturation::ConsoleType::GSeries
            : ConsoleSaturation::ConsoleType::ESeries);
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

                // NaN/Inf protection - skip processing if input is invalid
                if (!safeIsFinite(sample))
                {
                    channelData[i] = 0.0f;
                    continue;
                }

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

                // Console saturation
                if (params.saturation > 0.1f)
                {
                    float satAmount = params.saturation * 0.01f;
                    sample = consoleSaturation.processSample(sample, satAmount, isLeft);
                }

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

private:
    Parameters params;
    std::atomic<double> currentSampleRate{44100.0};

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

    // Console saturation
    ConsoleSaturation consoleSaturation;

    void updateFilters()
    {
        double sr = currentSampleRate.load(std::memory_order_acquire);
        updateHPF(sr);
        updateLPF(sr);
        updateLFBand(sr);
        updateLMBand(sr);
        updateHMBand(sr);
        updateHFBand(sr);
    }

    void updatePhaseShift()
    {
        // All-pass filter for transformer phase rotation at ~200Hz
        // Writes coefficients in-place (no heap allocation)
        float freq = 200.0f;
        double sr = currentSampleRate.load(std::memory_order_acquire);
        float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sr);
        float tan_w0 = std::tan(w0 / 2.0f);

        float a0 = 1.0f + tan_w0;
        float a1 = (1.0f - tan_w0) / a0;
        float b0 = a1;
        float b1 = 1.0f;

        setFilterCoeffs(phaseShiftL, b0, b1, 0.0f, a1, 0.0f);
        setFilterCoeffs(phaseShiftR, b0, b1, 0.0f, a1, 0.0f);
    }

    void updateHPF(double sr)
    {
        float freq = params.hpfFreq;

        // Stage 1: 1st-order highpass, pre-warped at fc
        {
            double k    = std::tan(juce::MathConstants<double>::pi * freq / sr);
            double norm = 1.0 / (1.0 + k);
            setFilterCoeffs(hpfStage1L, (float)norm, (float)-norm, 0.0f, (float)((k-1.0)*norm), 0.0f);
            setFilterCoeffs(hpfStage1R, (float)norm, (float)-norm, 0.0f, (float)((k-1.0)*norm), 0.0f);
        }

        // Stage 2: 2nd-order highpass, pre-warped at fc (Q=1.0 for 3rd-order Butterworth cascade)
        {
            const double consoleHPFQ = 1.0;
            double k    = std::tan(juce::MathConstants<double>::pi * freq / sr);
            double norm = 1.0 / (k*k + k/consoleHPFQ + 1.0);
            float b0 = (float)norm;
            float b1 = (float)(-2.0 * norm);
            float b2 = b0;
            float a1 = (float)(2.0 * (k*k - 1.0) * norm);
            float a2 = (float)((k*k - k/consoleHPFQ + 1.0) * norm);
            setFilterCoeffs(hpfStage2L, b0, b1, b2, a1, a2);
            setFilterCoeffs(hpfStage2R, b0, b1, b2, a1, a2);
        }
    }

    void updateLPF(double sr)
    {
        double freq = std::max(1.0, std::min((double)params.lpfFreq, sr * 0.4998));
        double q    = params.isBlackMode ? 0.8 : 0.707;

        // 2nd-order lowpass, pre-warped at fc
        double k    = std::tan(juce::MathConstants<double>::pi * freq / sr);
        double k2   = k * k;
        double norm = 1.0 / (k2 + k/q + 1.0);
        float b0 = (float)(k2 * norm);
        float b1 = (float)(2.0 * k2 * norm);
        float b2 = b0;
        float a1 = (float)(2.0 * (k2 - 1.0) * norm);
        float a2 = (float)((k2 - k/q + 1.0) * norm);
        setFilterCoeffs(lpfL, b0, b1, b2, a1, a2);
        setFilterCoeffs(lpfR, b0, b1, b2, a1, a2);
    }

    void updateLFBand(double sr)
    {
        if (params.isBlackMode && params.lfBell)
        {
            applyConsolePeakCoeffs(lfFilterL, sr, params.lfFreq, 0.7f, params.lfGain, params.isBlackMode);
            applyConsolePeakCoeffs(lfFilterR, sr, params.lfFreq, 0.7f, params.lfGain, params.isBlackMode);
        }
        else
        {
            applyConsoleShelfCoeffs(lfFilterL, sr, params.lfFreq, 0.7f, params.lfGain, false, params.isBlackMode);
            applyConsoleShelfCoeffs(lfFilterR, sr, params.lfFreq, 0.7f, params.lfGain, false, params.isBlackMode);
        }
    }

    void updateLMBand(double sr)
    {
        float q = params.lmQ;
        if (params.isBlackMode)
            q = calculateDynamicQ(params.lmGain, q);

        applyConsolePeakCoeffs(lmFilterL, sr, params.lmFreq, q, params.lmGain, params.isBlackMode);
        applyConsolePeakCoeffs(lmFilterR, sr, params.lmFreq, q, params.lmGain, params.isBlackMode);
    }

    void updateHMBand(double sr)
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

        applyConsolePeakCoeffs(hmFilterL, sr, freq, q, params.hmGain, params.isBlackMode);
        applyConsolePeakCoeffs(hmFilterR, sr, freq, q, params.hmGain, params.isBlackMode);
    }

    void updateHFBand(double sr)
    {
        float freq = params.hfFreq;

        if (params.isBlackMode && params.hfBell)
        {
            applyConsolePeakCoeffs(hfFilterL, sr, freq, 0.7f, params.hfGain, params.isBlackMode);
            applyConsolePeakCoeffs(hfFilterR, sr, freq, 0.7f, params.hfGain, params.isBlackMode);
        }
        else
        {
            applyConsoleShelfCoeffs(hfFilterL, sr, freq, 0.7f, params.hfGain, true, params.isBlackMode);
            applyConsoleShelfCoeffs(hfFilterR, sr, freq, 0.7f, params.hfGain, true, params.isBlackMode);
        }
    }

    float calculateDynamicQ(float gain, float baseQ) const
    {
        float absGain = std::abs(gain);
        float scale = (gain >= 0.0f) ? 2.0f : 1.5f;
        float dynamicQ = baseQ * (1.0f + (absGain / 20.0f) * scale);
        return juce::jlimit(0.5f, 8.0f, dynamicQ);
    }

    // Create initial passthrough coefficients for a filter (called from prepare(), off audio thread)
    static void initFilterCoefficients(juce::dsp::IIR::Filter<float>& filter)
    {
        filter.coefficients = new juce::dsp::IIR::Coefficients<float>(
            1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    }

    // Assign biquad coefficients in-place (no heap allocation)
    // JUCE IIR::Coefficients stores 5 elements: {b0/a0, b1/a0, b2/a0, a1/a0, a2/a0}
    // (a0 is divided out during construction, not stored as a separate element)
    static void setFilterCoeffs(juce::dsp::IIR::Filter<float>& filter,
                                float b0, float b1, float b2, float a1, float a2)
    {
        if (filter.coefficients == nullptr)
            return;
        auto* c = filter.coefficients->coefficients.getRawDataPointer();
        c[0] = b0; c[1] = b1; c[2] = b2; c[3] = a1; c[4] = a2;
    }

    // Console shelf filter — writes coefficients in-place, cramping-free.
    // Derives cosW/sinW from k=tan(π·fc/sr) so the turnover lands at exactly fc.
    void applyConsoleShelfCoeffs(juce::dsp::IIR::Filter<float>& filter,
        double sampleRate, float freq, float q, float gainDB, bool isHighShelf, bool isBlackMode) const
    {
        double consoleQ = q;
        if (isBlackMode) consoleQ *= 1.4;
        else             consoleQ *= 0.65;
        consoleQ = std::max(0.01, consoleQ);

        double fc  = std::max(1.0, std::min((double)freq, sampleRate * 0.4998));
        double A   = std::pow(10.0, (double)gainDB / 40.0);
        double sqA = std::sqrt(A);
        double k   = std::tan(juce::MathConstants<double>::pi * fc / sampleRate);
        double k2  = k * k;
        // Derive cosW and sinW from k (pre-warped) to avoid double-warping
        double cosW  = (1.0 - k2) / (1.0 + k2);
        double sinW  = 2.0 * k   / (1.0 + k2);
        double alpha = sinW / 2.0 * std::sqrt((A + 1.0/A) * (1.0/consoleQ - 1.0) + 2.0);

        double b0, b1, b2, a0, a1, a2;
        if (isHighShelf)
        {
            b0 =  A * ((A+1.0) + (A-1.0)*cosW + 2.0*sqA*alpha);
            b1 = -2.0*A * ((A-1.0) + (A+1.0)*cosW);
            b2 =  A * ((A+1.0) + (A-1.0)*cosW - 2.0*sqA*alpha);
            a0 = (A+1.0) - (A-1.0)*cosW + 2.0*sqA*alpha;
            a1 =  2.0 * ((A-1.0) - (A+1.0)*cosW);
            a2 = (A+1.0) - (A-1.0)*cosW - 2.0*sqA*alpha;
        }
        else
        {
            b0 =  A * ((A+1.0) - (A-1.0)*cosW + 2.0*sqA*alpha);
            b1 =  2.0*A * ((A-1.0) - (A+1.0)*cosW);
            b2 =  A * ((A+1.0) - (A-1.0)*cosW - 2.0*sqA*alpha);
            a0 = (A+1.0) + (A-1.0)*cosW + 2.0*sqA*alpha;
            a1 = -2.0 * ((A-1.0) + (A+1.0)*cosW);
            a2 = (A+1.0) + (A-1.0)*cosW - 2.0*sqA*alpha;
        }

        setFilterCoeffs(filter, (float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                                (float)(a1/a0), (float)(a2/a0));
    }

    // Console peak filter — writes coefficients in-place, cramping-free.
    // Pre-warps the bandwidth (not just fc): kbw=tan(π·bw/sr), center via cos(2π·fc/sr).
    void applyConsolePeakCoeffs(juce::dsp::IIR::Filter<float>& filter,
        double sampleRate, float freq, float q, float gainDB, bool isBlackMode) const
    {
        double consoleQ = q;
        if (isBlackMode && std::abs(gainDB) > 0.1f)
        {
            double gainFactor = std::abs((double)gainDB) / 15.0;
            if (gainDB > 0.0f)
                consoleQ *= (1.0 + gainFactor * 1.2);
            else
                consoleQ *= (1.0 + gainFactor * 0.6);
        }
        consoleQ = std::max(0.1, std::min(consoleQ, 10.0));

        double fc  = std::max(1.0, std::min((double)freq, sampleRate * 0.4998));
        double bw  = fc / consoleQ;
        double kbw = std::tan(juce::MathConstants<double>::pi * std::min(bw, sampleRate * 0.4998) / sampleRate);
        double A   = std::pow(10.0, (double)gainDB / 40.0);
        double cosW = std::cos(2.0 * juce::MathConstants<double>::pi * fc / sampleRate);

        // A = 10^(gainDB/40) < 1 for cuts — no branch needed, formula handles both.
        const double b0 = 1.0 + kbw * A,  b2 = 1.0 - kbw * A;
        const double a0 = 1.0 + kbw / A,  a2 = 1.0 - kbw / A;
        const double b1 = -2.0 * cosW;
        const double a1 = -2.0 * cosW;

        setFilterCoeffs(filter, (float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                                (float)(a1/a0), (float)(a2/a0));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BritishEQProcessor)
};

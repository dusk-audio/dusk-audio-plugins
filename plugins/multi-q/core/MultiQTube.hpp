// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQTube.hpp — framework-free (ZERO JUCE) port of the Multi-Q "Tube"
// character. Verbatim transcription of TubeEQProcessor.h (InductorModel,
// TubeEQTubeStage, PultecLFSection, TubeEQProcessor) with the JUCE facade
// removed. Every magic number and the sample-level op-order are preserved.
//
// JUCE → framework-free substitutions (see file header of this port branch):
//   juce::dsp::IIR::Filter<float>  → TubeBiquad (DF2T mono biquad, per-channel)
//   juce::MathConstants            → kTube{Pi,TwoPi,HalfPi}{F,D} (exact values)
//   juce::jlimit(lo,hi,x)          → std::clamp(x,lo,hi)
//   juce::jmap(v,0,1,a,b)          → a + (b-a)*v
//   juce::Decibels::decibelsToGain → std::pow(10.f, db/20.f)
//   juce::ScopedNoDenormals        → duskaudio::ScopedFlushDenormals
//   juce::AudioBuffer<float>       → raw float* per channel
//   juce::SpinLock/pendingParams   → removed; per-block change detection instead
//                                     (preserves the JUCE "updateFilters only on
//                                      change" semantics so the end-of-block HF
//                                      inductor-Q remodulation persists — matters
//                                      for the A/B). RNG seeds kept EXACTLY.
//   AnalogEmulation DCBlocker / TransformerEmulation / initializeLibrary and the
//   local JUCE-free ADAASaturation.h are reused as-is.

#pragma once

#include "../SafeFloat.h"
#include "../ADAASaturation.h"
#include "../../shared/AnalogEmulation/AnalogEmulation.h"

#include "../../shared-dpf/dsp/DuskDenormals.hpp"
#include "../../shared-dpf/dsp/DuskOversampler.hpp"   // 2x/4x nonlinear-stage OS

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <random>

namespace duskaudio
{

// Exact reproductions of juce::MathConstants<T>::{pi,twoPi,halfPi} (computed in
// long double then cast, matching JUCE's definitions bit-for-bit).
inline constexpr float  kTubePiF     = static_cast<float>(3.141592653589793238L);
inline constexpr float  kTubeTwoPiF  = static_cast<float>(2.0L * 3.141592653589793238L);
inline constexpr float  kTubeHalfPiF = static_cast<float>(3.141592653589793238L / 2.0L);
inline constexpr double kTubePiD     = static_cast<double>(3.141592653589793238L);
inline constexpr double kTubeTwoPiD  = static_cast<double>(2.0L * 3.141592653589793238L);

// Helper for LC filter pre-warping (clamp omega to avoid tan() blowup near Nyquist).
inline float tubeEQPreWarpFrequency(float freq, double sampleRate)
{
    float omega = kTubePiF * freq / static_cast<float>(sampleRate);
    omega = std::min(omega, kTubeHalfPiF - 0.001f);
    return static_cast<float>(sampleRate) / kTubePiF * std::tan(omega);
}

// Mono Direct-Form-II-Transposed biquad, byte-for-byte equivalent to
// juce::dsp::IIR::Filter<float> for a 2nd-order section. Coefficients are
// {b0,b1,b2,a1,a2} with a0 already divided out (as JUCE stores them).
struct TubeBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = 0.0f; z2 = 0.0f; }

    void setCoeffs(float nb0, float nb1, float nb2, float na1, float na2)
    {
        b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2;
    }

    inline float processSample(float x)
    {
        // JUCE IIR::Filter order-2: y = b0*x + z1; z1 = b1*x - a1*y + z2;
        //                          z2 = b2*x - a2*y;
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

//==============================================================================
/** Inductor model for LC network emulation with frequency-dependent Q,
    core saturation, and hysteresis. (Verbatim from TubeEQProcessor.h.) */
class TubeInductorModel
{
public:
    void prepare(double sampleRate, uint32_t characterSeed = 0)
    {
        this->sampleRate = sampleRate;
        computeDecayCoefficients(sampleRate);
        reset();

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
        currentSaturationLevel = 0.0f;
    }

    void updateSampleRate(double newRate)
    {
        sampleRate = newRate;
        computeDecayCoefficients(newRate);
        reset();
    }

    float getFrequencyDependentQ(float frequency, float baseQ) const
    {
        float logFreq = std::log10(std::max(frequency, 10.0f));
        float centered = logFreq - 2.4f;
        float qMultiplier = 1.0f - 0.22f * centered * centered - 0.04f * centered * centered * centered * centered;
        qMultiplier = std::max(qMultiplier, 0.25f);

        float rmsReduction = 1.0f - rmsLevel * 0.15f;
        rmsReduction = std::max(rmsReduction, 0.75f);

        return baseQ * qMultiplier * rmsReduction * componentQVariation;
    }

    float processNonlinearity(float input, float driveLevel)
    {
        if (!safeIsFinite(input))
            return 0.0f;

        rmsLevel = rmsLevel * rmsDecay + input * input * (1.0f - rmsDecay);
        float rmsValue = std::sqrt(rmsLevel);

        float dynamicThreshold = (0.65f - rmsValue * 0.15f) * componentSatVariation;
        dynamicThreshold = std::max(dynamicThreshold, 0.35f);

        float saturatedInput = input;
        float absInput = std::abs(input);

        if (absInput > dynamicThreshold)
        {
            float excess = (absInput - dynamicThreshold) / (1.0f - dynamicThreshold);
            float langevin = std::tanh(excess * 2.5f * (1.0f + driveLevel));

            float compressed = dynamicThreshold + langevin * (1.0f - dynamicThreshold) * 0.7f;
            saturatedInput = std::copysign(compressed, input);

            float h2Amount = 0.012f * driveLevel * excess;
            saturatedInput += h2Amount * input * absInput;

            float h3Amount = 0.018f * driveLevel * driveLevel * excess;
            saturatedInput += h3Amount * input * input * input;

            currentSaturationLevel = currentSaturationLevel * 0.95f + excess * 0.05f;
        }
        else
        {
            currentSaturationLevel *= 0.95f;
        }

        float deltaInput = saturatedInput - prevInput;
        float hysteresisCoeff = 0.08f * driveLevel;

        coreFlux = coreFlux * fluxDecay + deltaInput * hysteresisCoeff;
        coreFlux = std::clamp(coreFlux, -0.15f, 0.15f);

        hysteresisState = hysteresisState * hystDecay + coreFlux * (1.0f - hystDecay);
        float output = saturatedInput + hysteresisState * 0.5f;

        prevInput = input;
        prevOutput = output;

        return output;
    }

    float getSaturationLevel() const { return std::min(currentSaturationLevel, 1.0f); }
    float getRmsLevel() const { return std::sqrt(rmsLevel); }

private:
    void computeDecayCoefficients(double sr)
    {
        rmsDecay = std::exp(-1.0f / (0.045f * static_cast<float>(sr)));
        fluxDecay = std::exp(-1.0f / (0.00075f * static_cast<float>(sr)));
        hystDecay = std::exp(-1.0f / (0.00028f * static_cast<float>(sr)));
    }

    double sampleRate = 44100.0;
    float prevInput = 0.0f;
    float prevOutput = 0.0f;
    float hysteresisState = 0.0f;
    float coreFlux = 0.0f;
    float rmsLevel = 0.0f;
    float currentSaturationLevel = 0.0f;

    float rmsDecay = 0.9995f;
    float fluxDecay = 0.97f;
    float hystDecay = 0.92f;

    float componentQVariation = 1.0f;
    float componentSatVariation = 1.0f;
};

//==============================================================================
/** Tube stage: polynomial waveshaper + LF transformer H3 (ADAA). Verbatim. */
class TubeStage
{
public:
    void prepare(double sampleRate, int /*numChannels*/)
    {
        this->sampleRate = sampleRate;
        for (auto& dc : dcBlockers)
            dc.prepare(sampleRate, 8.0f);
        computeCoefficients(sampleRate);
        reset();
    }

    void reset()
    {
        lfStates.fill(0.0f);
        prevXd.fill(0.0f);
        for (auto& dc : dcBlockers)
            dc.reset();
    }

    void updateSampleRate(double newRate)
    {
        sampleRate = newRate;
        computeCoefficients(newRate);
        reset();
    }

    void setDrive(float newDrive)
    {
        drive = std::clamp(newDrive, 0.0f, 1.0f);
    }

    float processSample(float input, int channel)
    {
        if (!safeIsFinite(input))
            return 0.0f;

        int ch = std::clamp(channel, 0, maxChannels - 1);
        float& lfState = lfStates[static_cast<size_t>(ch)];

        lfState += lfCoeff * (input - lfState);

        const float DRIVE_SCALE = 2.0f;
        const float driveAmount = drive * DRIVE_SCALE;
        const float xd_raw = input * driveAmount;
        const float xd = xd_raw / std::sqrt(1.0f + xd_raw * xd_raw * 0.25f);

        const float b = 0.015f;
        const float c = 0.002f;
        const float d = 0.0005f;

        float& prevXdVal = prevXd[static_cast<size_t>(ch)];
        float distortion = ADAASaturation::process(
            xd, prevXdVal,
            [b,c,d](float v) { return ADAASaturation::polyWaveshaper(v, b, c, d, 0.0f); },
            [b,c,d](float v) { return ADAASaturation::polyAntideriv(v, b, c, d, 0.0f); });
        prevXdVal = xd;

        const float xd_lf_raw = lfState * driveAmount;
        const float xd_lf = xd_lf_raw / std::sqrt(1.0f + xd_lf_raw * xd_lf_raw * 0.25f);
        const float transformerH3 = 0.025f * xd_lf * xd_lf * xd_lf;

        distortion += transformerH3;
        float y = input + distortion / driveAmount;

        y = dcBlockers[static_cast<size_t>(ch)].processSample(y);

        return safeIsFinite(y) ? y : input;
    }

private:
    void computeCoefficients(double sr)
    {
        lfCoeff = 1.0f - std::exp(-kTubeTwoPiF * 300.0f / static_cast<float>(sr));
    }

    static constexpr int maxChannels = 8;
    double sampleRate = 44100.0;
    float drive = 0.3f;
    std::array<float, maxChannels> prevXd{};
    float lfCoeff = 0.04f;

    std::array<float, maxChannels> lfStates{};
    std::array<AnalogEmulation::DCBlocker, maxChannels> dcBlockers;
};

//==============================================================================
/** Passive-EQ-style LF section: dual-biquad boost/cut with inductor
    nonlinearity between stages. Verbatim from PultecLFSection. */
class TubePultecLFSection
{
public:
    static constexpr float kPeakGainScale = 1.4f;
    static constexpr float kPeakInteraction = 0.08f;
    static constexpr float kBaseQ = 0.55f;
    static constexpr float kQInteraction = 0.015f;
    static constexpr float kDipFreqBase = 1.0f;
    static constexpr float kDipFreqRange = 0.0f;
    static constexpr float kDipGainScale = 1.75f;
    static constexpr float kDipInteraction = 0.06f;
    static constexpr float kDipBaseQ = 0.65f;
    static constexpr float kDipQScale = 0.03f;

    void prepare(double sampleRate, uint32_t characterSeed = 0)
    {
        currentSampleRate = sampleRate;
        for (size_t i = 0; i < maxChannels; ++i)
            inductors[i].prepare(sampleRate, characterSeed + static_cast<uint32_t>(i));
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

    void updateSampleRate(double newRate)
    {
        currentSampleRate = newRate;
        for (auto& ind : inductors)
            ind.updateSampleRate(newRate);
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

        // ---- Peak filter (resonant boost) ----
        if (boostGain > 0.01f)
        {
            float peakGainDB = boostGain * kPeakGainScale + attenGain * boostGain * kPeakInteraction;
            float effectiveQ = inductors[0].getFrequencyDependentQ(frequency, kBaseQ);
            effectiveQ *= (1.0f + attenGain * kQInteraction);

            float satLevel = inductors[0].getSaturationLevel();
            effectiveQ *= (1.0f - satLevel * 0.25f);

            effectiveQ = std::max(effectiveQ, 0.2f);

            double fc  = std::max(1.0, std::min((double)frequency, sampleRate * 0.4998));
            double bw  = fc / std::max(0.01, (double)effectiveQ);
            double kbw = std::tan(kTubePiD * std::min(bw, sampleRate * 0.4998) / sampleRate);
            double A   = std::pow(10.0, (double)peakGainDB / 40.0);
            double cosW = std::cos(2.0 * kTubePiD * fc / sampleRate);

            double pb0 = 1.0 + kbw * A,  pb2 = 1.0 - kbw * A;
            double pa0 = 1.0 + kbw / A,  pa2 = 1.0 - kbw / A;
            double pb1 = -2.0 * cosW;
            double pa1 = -2.0 * cosW;

            peakB0 = (float)(pb0/pa0); peakB1 = (float)(pb1/pa0); peakB2 = (float)(pb2/pa0);
            peakA1 = (float)(pa1/pa0); peakA2 = (float)(pa2/pa0);
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

            double dfc  = std::max(1.0, std::min((double)dipFreq, sampleRate * 0.4998));
            double dA   = std::pow(10.0, (double)dipGainDB / 40.0);
            double dk   = std::tan(kTubePiD * dfc / sampleRate);
            double dk2  = dk * dk;
            double dsqA = std::sqrt(dA);
            double dcq  = std::max(0.01, (double)dipQ);
            double dcosW = (1.0 - dk2) / (1.0 + dk2);
            double dsinW = 2.0 * dk / (1.0 + dk2);
            double dalpha = dsinW / 2.0 * std::sqrt((dA + 1.0/dA) * (1.0/dcq - 1.0) + 2.0);

            double db0 = dA * ((dA+1.0) - (dA-1.0)*dcosW + 2.0*dsqA*dalpha);
            double db1 = 2.0*dA * ((dA-1.0) - (dA+1.0)*dcosW);
            double db2 = dA * ((dA+1.0) - (dA-1.0)*dcosW - 2.0*dsqA*dalpha);
            double da0 = (dA+1.0) + (dA-1.0)*dcosW + 2.0*dsqA*dalpha;
            double da1 = -2.0 * ((dA-1.0) + (dA+1.0)*dcosW);
            double da2 = (dA+1.0) + (dA-1.0)*dcosW - 2.0*dsqA*dalpha;

            dipB0 = (float)(db0/da0); dipB1 = (float)(db1/da0); dipB2 = (float)(db2/da0);
            dipA1 = (float)(da1/da0); dipA2 = (float)(da2/da0);
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

        float peakOut = peakB0 * input + s.peakZ1;
        s.peakZ1 = peakB1 * input - peakA1 * peakOut + s.peakZ2;
        s.peakZ2 = peakB2 * input - peakA2 * peakOut;

        float interStage = (cachedBoost > 0.01f)
            ? inductors[static_cast<size_t>(ch)].processNonlinearity(peakOut, cachedBoost * 0.3f)
            : peakOut;

        float dipOut = dipB0 * interStage + s.dipZ1;
        s.dipZ1 = dipB1 * interStage - dipA1 * dipOut + s.dipZ2;
        s.dipZ2 = dipB2 * interStage - dipA2 * dipOut;

        auto clampState = [](float& v) { v = std::clamp(v, -8.0f, 8.0f); };
        clampState(s.peakZ1); clampState(s.peakZ2);
        clampState(s.dipZ1); clampState(s.dipZ2);

        return safeIsFinite(dipOut) ? dipOut : input;
    }

    TubeInductorModel& getInductor() { return inductors[0]; }
    float getInductorRmsLevel() const { return inductors[0].getRmsLevel(); }

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

    std::array<TubeInductorModel, maxChannels> inductors;
    double currentSampleRate = 44100.0;
    float cachedBoost = 0.0f, cachedAtten = 0.0f, cachedFreq = 60.0f;
};

//==============================================================================
/** Framework-free port of TubeEQProcessor. Applies the full Tube signal path to
    a set of channel buffers in place. Parameters mirror
    TubeEQProcessor::Parameters exactly (resolved Hz for stepped freqs — the DPF
    shell applies the choice-index → Hz LUTs before filling this, matching the
    JUCE MultiQ shell at MultiQ.cpp:784-842). */
class MultiQTube
{
public:
    struct Parameters
    {
        float lfBoostGain = 0.0f;
        float lfBoostFreq = 60.0f;
        float lfAttenGain = 0.0f;

        float hfBoostGain = 0.0f;
        float hfBoostFreq = 8000.0f;
        float hfBoostBandwidth = 0.5f;

        float hfAttenGain = 0.0f;
        float hfAttenFreq = 10000.0f;

        bool  midEnabled = true;
        float midLowFreq = 500.0f;
        float midLowPeak = 0.0f;
        float midDipFreq = 700.0f;
        float midDip = 0.0f;
        float midHighFreq = 3000.0f;
        float midHighPeak = 0.0f;

        float inputGain = 0.0f;
        float outputGain = 0.0f;
        float tubeDrive = 0.3f;
        bool  bypass = false;

        bool operator==(const Parameters& o) const
        {
            return lfBoostGain == o.lfBoostGain && lfBoostFreq == o.lfBoostFreq
                && lfAttenGain == o.lfAttenGain && hfBoostGain == o.hfBoostGain
                && hfBoostFreq == o.hfBoostFreq && hfBoostBandwidth == o.hfBoostBandwidth
                && hfAttenGain == o.hfAttenGain && hfAttenFreq == o.hfAttenFreq
                && midEnabled == o.midEnabled && midLowFreq == o.midLowFreq
                && midLowPeak == o.midLowPeak && midDipFreq == o.midDipFreq
                && midDip == o.midDip && midHighFreq == o.midHighFreq
                && midHighPeak == o.midHighPeak && inputGain == o.inputGain
                && outputGain == o.outputGain && tubeDrive == o.tubeDrive
                && bypass == o.bypass;
        }
        bool operator!=(const Parameters& o) const { return !(*this == o); }
    };

    void prepare(double sampleRate, int samplesPerBlock, int numChannels)
    {
        baseSampleRate = sampleRate;
        currentSampleRate = sampleRate;
        this->numChannels = std::min(numChannels, maxProcessChannels);

        // Nonlinear-stage oversampling: start at 1x (transparent passthrough).
        // setOversampling() re-prepares the internal DSP at the elevated rate when
        // hq_enabled changes; at 1x the per-sample chain is called directly, so the
        // Tube path is bit-identical to the pre-oversampling build when hq is Off.
        osFactor = 1;
        for (auto& os : oversamplers) { os.setFactor(1); os.reset(); }

        // deterministic character seed (kept EXACTLY as the JUCE build). Seeded off
        // the BASE rate so the character is stable regardless of the hq setting.
        characterSeed = static_cast<uint32_t>(sampleRate * 1000.0);
        tubeStage.prepare(sampleRate, numChannels);
        pultecLF.prepare(sampleRate, characterSeed);
        hfInductorL.prepare(sampleRate, characterSeed + 1);
        hfInductorR.prepare(sampleRate, characterSeed + 2);
        hfQInductor.prepare(sampleRate, characterSeed + 1);

        inputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.prepare(sampleRate, numChannels);

        setupTransformerProfiles();
        AnalogEmulation::initializeLibrary();

        // Filters default to identity (passthrough) — equivalent to the JUCE
        // build's initFilterCoefficients(1,0,0,1,0,0).
        needsUpdate = true;   // force updateFilters on the first process block
        reset();
        (void)samplesPerBlock;
    }

    void reset()
    {
        hfBoostFilterL.reset(); hfBoostFilterR.reset();
        hfAttenFilterL.reset(); hfAttenFilterR.reset();
        midLowPeakFilterL.reset(); midLowPeakFilterR.reset();
        midDipFilterL.reset(); midDipFilterR.reset();
        midHighPeakFilterL.reset(); midHighPeakFilterR.reset();
        tubeStage.reset();
        pultecLF.reset();
        hfInductorL.reset();
        hfInductorR.reset();
        inputTransformer.reset();
        outputTransformer.reset();
    }

    // Apply immediately, but only mark filters dirty when the values actually
    // changed. This replicates the JUCE build's parametersNeedUpdate gate so the
    // end-of-block HF inductor-Q remodulation persists across identical-param
    // blocks (critical for a matching A/B).
    void setParameters(const Parameters& newParams)
    {
        if (newParams != params)
        {
            params = newParams;
            needsUpdate = true;
        }
    }

    const Parameters& getParameters() const { return params; }

    // Nonlinear-stage oversampling. mode: 0=Off(1x), 1=2x, 2=4x. When the factor
    // changes, the whole internal Tube DSP is re-prepared at the elevated rate
    // (base*factor) — RT-safe: every component exposes an alloc-free
    // updateSampleRate(). The per-sample chain then runs inside the polyphase
    // halfband oversampler, so the drive polynomial + transformer H3 (the tube's
    // dominant aliasing sources) are band-limited. Only re-prepares on change, so
    // steady-state blocks pay nothing.
    void setOversampling(int mode)
    {
        int f = (mode == 2) ? 4 : (mode == 1) ? 2 : 1;
        if (f == osFactor)
            return;
        osFactor = f;
        updateSampleRate(baseSampleRate * (double)f);
        for (auto& os : oversamplers) { os.setFactor(f); os.reset(); }
    }

    // Alloc-free sample-rate update (recomputes every coefficient + resets state at
    // the new rate). Used by setOversampling; mirrors MultiQ::…updateSampleRate.
    void updateSampleRate(double newRate)
    {
        currentSampleRate = newRate;
        tubeStage.updateSampleRate(newRate);
        pultecLF.updateSampleRate(newRate);
        hfInductorL.updateSampleRate(newRate);
        hfInductorR.updateSampleRate(newRate);
        hfQInductor.updateSampleRate(newRate);
        inputTransformer.updateSampleRate(newRate);
        outputTransformer.updateSampleRate(newRate);
        hfBoostFilterL.reset();    hfBoostFilterR.reset();
        hfAttenFilterL.reset();    hfAttenFilterR.reset();
        midLowPeakFilterL.reset(); midLowPeakFilterR.reset();
        midDipFilterL.reset();     midDipFilterR.reset();
        midHighPeakFilterL.reset();midHighPeakFilterR.reset();
        needsUpdate = true;   // recompute biquad coeffs at the new rate
    }

    // Added latency (base-rate samples) of the nonlinear-stage oversampler. 0 at 1x.
    int getLatencySamples() const noexcept
    {
        return osFactor <= 1 ? 0 : (int)std::lround(oversamplers[0].latency());
    }

    // Process channel buffers in place. Mirrors TubeEQProcessor::process(buffer).
    void process(float* const* channelData, int numChans, int numSamples)
    {
        ScopedFlushDenormals noDenormals;

        if (needsUpdate)
        {
            updateFilters();
            tubeStage.setDrive(params.tubeDrive);
            needsUpdate = false;
        }

        if (params.bypass)
            return;

        const int channels = std::min(numChans, maxProcessChannels);

        // Apply input gain (buffer-wide, at base rate)
        if (std::abs(params.inputGain) > 0.01f)
        {
            float inputGainLinear = std::pow(10.0f, params.inputGain / 20.0f);
            for (int ch = 0; ch < channels; ++ch)
            {
                float* d = channelData[ch];
                for (int i = 0; i < numSamples; ++i) d[i] *= inputGainLinear;
            }
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            float* cd = channelData[ch];
            bool isLeft = (ch % 2 == 0);
            auto& os = oversamplers[static_cast<size_t>(ch)];

            for (int i = 0; i < numSamples; ++i)
            {
                float in = cd[i];
                if (!safeIsFinite(in))
                {
                    cd[i] = 0.0f;
                    continue;
                }
                // The whole per-sample Tube chain runs at the oversampled rate.
                cd[i] = os.processSample(in, [&](float sample) noexcept
                {
                    return processOneSample(sample, ch, isLeft);
                });
            }
        }

        // Per-block inductor Q modulation for HF boost (updates coeffs for the
        // NEXT block — persists because updateFilters only runs on param change).
        if (params.hfBoostGain > 0.01f)
        {
            float baseQ = 2.0f + (0.3f - 2.0f) * params.hfBoostBandwidth; // jmap(bw,0,1,2,0.3)
            float freq = params.hfBoostFreq;

            float satLevelL = hfInductorL.getSaturationLevel();
            if (satLevelL > 0.01f)
            {
                float effectiveQL = hfQInductor.getFrequencyDependentQ(params.hfBoostFreq, baseQ);
                effectiveQL *= (1.0f - satLevelL * 0.20f);
                setTubeEQPeakCoeffs(hfBoostFilterL, currentSampleRate, freq, effectiveQL, params.hfBoostGain * 1.8f);
            }

            float satLevelR = hfInductorR.getSaturationLevel();
            if (satLevelR > 0.01f)
            {
                float effectiveQR = hfQInductor.getFrequencyDependentQ(params.hfBoostFreq, baseQ);
                effectiveQR *= (1.0f - satLevelR * 0.20f);
                setTubeEQPeakCoeffs(hfBoostFilterR, currentSampleRate, freq, effectiveQR, params.hfBoostGain * 1.8f);
            }
        }

        // Apply output gain (buffer-wide)
        if (std::abs(params.outputGain) > 0.01f)
        {
            float outputGainLinear = std::pow(10.0f, params.outputGain / 20.0f);
            for (int ch = 0; ch < channels; ++ch)
            {
                float* d = channelData[ch];
                for (int i = 0; i < numSamples; ++i) d[i] *= outputGainLinear;
            }
        }
    }

private:
    // The whole per-sample Tube signal path (input transformer → passive LF → HF
    // inductor+boost → HF atten → mid section → tube drive stage → output
    // transformer). Extracted from process() so it can be driven by the polyphase
    // oversampler at the elevated rate. Byte-for-byte the same op order as before.
    inline float processOneSample(float sample, int ch, bool isLeft) noexcept
    {
        sample = inputTransformer.processSample(sample, ch);
        sample = pultecLF.processSample(sample, ch);

        if (params.hfBoostGain > 0.01f)
        {
            sample = isLeft ? hfInductorL.processNonlinearity(sample, params.hfBoostGain * 0.2f)
                            : hfInductorR.processNonlinearity(sample, params.hfBoostGain * 0.2f);

            sample = isLeft ? hfBoostFilterL.processSample(sample)
                            : hfBoostFilterR.processSample(sample);
        }

        if (params.hfAttenGain > 0.01f)
        {
            sample = isLeft ? hfAttenFilterL.processSample(sample)
                            : hfAttenFilterR.processSample(sample);
        }

        if (params.midEnabled)
        {
            if (params.midLowPeak > 0.01f)
                sample = isLeft ? midLowPeakFilterL.processSample(sample)
                                : midLowPeakFilterR.processSample(sample);

            if (params.midDip > 0.01f)
                sample = isLeft ? midDipFilterL.processSample(sample)
                                : midDipFilterR.processSample(sample);

            if (params.midHighPeak > 0.01f)
                sample = isLeft ? midHighPeakFilterL.processSample(sample)
                                : midHighPeakFilterR.processSample(sample);
        }

        if (params.tubeDrive > 0.01f)
            sample = tubeStage.processSample(sample, ch);

        sample = outputTransformer.processSample(sample, ch);

        return safeIsFinite(sample) ? sample : 0.0f;
    }

    Parameters params;
    bool needsUpdate = true;
    double baseSampleRate = 44100.0;    // rate passed to prepare (pre-oversampling)
    double currentSampleRate = 44100.0; // effective rate (base * osFactor)
    int numChannels = 2;
    int osFactor = 1;                   // 1 / 2 / 4 (nonlinear-stage oversampling)
    uint32_t characterSeed = 0;

    std::array<Oversampler, 8> oversamplers; // per-channel nonlinear-stage OS

    TubeBiquad hfBoostFilterL, hfBoostFilterR;
    TubeBiquad hfAttenFilterL, hfAttenFilterR;
    TubeBiquad midLowPeakFilterL, midLowPeakFilterR;
    TubeBiquad midDipFilterL, midDipFilterR;
    TubeBiquad midHighPeakFilterL, midHighPeakFilterR;

    TubeStage tubeStage;
    TubePultecLFSection pultecLF;
    TubeInductorModel hfInductorL;
    TubeInductorModel hfInductorR;
    TubeInductorModel hfQInductor;

    static constexpr int maxProcessChannels = 8;

    AnalogEmulation::TransformerEmulation inputTransformer;
    AnalogEmulation::TransformerEmulation outputTransformer;

    void setupTransformerProfiles()
    {
        AnalogEmulation::TransformerProfile inputProfile;
        inputProfile.hasTransformer = true;
        inputProfile.saturationAmount = 0.15f;
        inputProfile.lowFreqSaturation = 1.3f;
        inputProfile.highFreqRolloff = 1e7f;
        inputProfile.dcBlockingFreq = 10.0f;
        inputProfile.harmonics = { 0.005f, 0.015f, 0.002f };

        inputTransformer.setProfile(inputProfile);
        inputTransformer.setEnabled(true);

        AnalogEmulation::TransformerProfile outputProfile;
        outputProfile.hasTransformer = true;
        outputProfile.saturationAmount = 0.12f;
        outputProfile.lowFreqSaturation = 1.2f;
        outputProfile.highFreqRolloff = 1e7f;
        outputProfile.dcBlockingFreq = 8.0f;
        outputProfile.harmonics = { 0.004f, 0.012f, 0.001f };

        outputTransformer.setProfile(outputProfile);
        outputTransformer.setEnabled(true);
    }

    void updateFilters()
    {
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
        float freq = params.hfBoostFreq;
        float gainDB = params.hfBoostGain * 1.8f;
        float baseQ = 2.0f + (0.3f - 2.0f) * params.hfBoostBandwidth; // jmap(bw,0,1,2,0.3)
        float effectiveQ = hfQInductor.getFrequencyDependentQ(params.hfBoostFreq, baseQ);

        setTubeEQPeakCoeffs(hfBoostFilterL, currentSampleRate, freq, effectiveQ, gainDB);
        setTubeEQPeakCoeffs(hfBoostFilterR, currentSampleRate, freq, effectiveQ, gainDB);
    }

    void updateHFAtten()
    {
        float freq = params.hfAttenFreq;
        float gainDB = -params.hfAttenGain * 1.6f;
        setHighShelfCoeffs(hfAttenFilterL, currentSampleRate, freq, 0.6f, gainDB);
        setHighShelfCoeffs(hfAttenFilterR, currentSampleRate, freq, 0.6f, gainDB);
    }

    void updateMidLowPeak()
    {
        float freq = params.midLowFreq;
        float gainDB = params.midLowPeak * 1.2f;
        float q = 1.2f;
        setTubeEQPeakCoeffs(midLowPeakFilterL, currentSampleRate, freq, q, gainDB);
        setTubeEQPeakCoeffs(midLowPeakFilterR, currentSampleRate, freq, q, gainDB);
    }

    void updateMidDip()
    {
        float freq = params.midDipFreq;
        float gainDB = -params.midDip * 1.0f;
        float q = 0.8f;
        setTubeEQPeakCoeffs(midDipFilterL, currentSampleRate, freq, q, gainDB);
        setTubeEQPeakCoeffs(midDipFilterR, currentSampleRate, freq, q, gainDB);
    }

    void updateMidHighPeak()
    {
        float freq = params.midHighFreq;
        float gainDB = params.midHighPeak * 1.2f;
        float q = 1.4f;
        setTubeEQPeakCoeffs(midHighPeakFilterL, currentSampleRate, freq, q, gainDB);
        setTubeEQPeakCoeffs(midHighPeakFilterR, currentSampleRate, freq, q, gainDB);
    }

    // Tube EQ peak filter (cramping-free). Verbatim math from setTubeEQPeakCoeffs.
    void setTubeEQPeakCoeffs(TubeBiquad& filter, double sampleRate, float freq,
                             float q, float gainDB) const
    {
        double tubeEQQ = std::max(0.01, (double)q * 0.85);
        double fc  = std::max(1.0, std::min((double)freq, sampleRate * 0.4998));
        double bw  = fc / tubeEQQ;
        double kbw = std::tan(kTubePiD * std::min(bw, sampleRate * 0.4998) / sampleRate);
        double A   = std::pow(10.0, (double)gainDB / 40.0);
        double cosW = std::cos(2.0 * kTubePiD * fc / sampleRate);

        const double b0 = 1.0 + kbw * A,  b2 = 1.0 - kbw * A;
        const double a0 = 1.0 + kbw / A,  a2 = 1.0 - kbw / A;
        const double b1 = -2.0 * cosW;
        const double a1 = -2.0 * cosW;
        filter.setCoeffs((float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                         (float)(a1/a0), (float)(a2/a0));
    }

    void setLowShelfCoeffs(TubeBiquad& filter, double sampleRate, float freq,
                           float q, float gainDB) const
    {
        double fc  = std::max(1.0, std::min((double)freq, sampleRate * 0.4998));
        double cq  = std::max(0.01, (double)q);
        double A   = std::pow(10.0, (double)gainDB / 40.0);
        double sqA = std::sqrt(A);
        double k   = std::tan(kTubePiD * fc / sampleRate);
        double k2  = k * k;
        double cosW  = (1.0 - k2) / (1.0 + k2);
        double sinW  = 2.0 * k   / (1.0 + k2);
        double alpha = sinW / 2.0 * std::sqrt((A + 1.0/A) * (1.0/cq - 1.0) + 2.0);

        double b0 =  A * ((A+1.0) - (A-1.0)*cosW + 2.0*sqA*alpha);
        double b1 =  2.0*A * ((A-1.0) - (A+1.0)*cosW);
        double b2 =  A * ((A+1.0) - (A-1.0)*cosW - 2.0*sqA*alpha);
        double a0 = (A+1.0) + (A-1.0)*cosW + 2.0*sqA*alpha;
        double a1 = -2.0 * ((A-1.0) + (A+1.0)*cosW);
        double a2 = (A+1.0) + (A-1.0)*cosW - 2.0*sqA*alpha;
        filter.setCoeffs((float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                         (float)(a1/a0), (float)(a2/a0));
    }

    void setHighShelfCoeffs(TubeBiquad& filter, double sampleRate, float freq,
                            float q, float gainDB) const
    {
        double fc  = std::max(1.0, std::min((double)freq, sampleRate * 0.4998));
        double cq  = std::max(0.01, (double)q);
        double A   = std::pow(10.0, (double)gainDB / 40.0);
        double sqA = std::sqrt(A);
        double k   = std::tan(kTubePiD * fc / sampleRate);
        double k2  = k * k;
        double cosW  = (1.0 - k2) / (1.0 + k2);
        double sinW  = 2.0 * k   / (1.0 + k2);
        double alpha = sinW / 2.0 * std::sqrt((A + 1.0/A) * (1.0/cq - 1.0) + 2.0);

        double b0 =  A * ((A+1.0) + (A-1.0)*cosW + 2.0*sqA*alpha);
        double b1 = -2.0*A * ((A-1.0) + (A+1.0)*cosW);
        double b2 =  A * ((A+1.0) + (A-1.0)*cosW - 2.0*sqA*alpha);
        double a0 = (A+1.0) - (A-1.0)*cosW + 2.0*sqA*alpha;
        double a1 =  2.0 * ((A-1.0) - (A+1.0)*cosW);
        double a2 = (A+1.0) - (A-1.0)*cosW - 2.0*sqA*alpha;
        filter.setCoeffs((float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                         (float)(a1/a0), (float)(a2/a0));
    }
};

} // namespace duskaudio

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQDynamics.hpp — framework-free per-band dynamic EQ, ported from
// DynamicEQProcessor.h. Each band is an independent downward compressor whose
// gain-reduction drives a Cytomic dyn-gain SVF at the band's frequency.
//
// The JUCE original carries SeqLock/atomic UI->audio parameter transfer and an
// optional lookahead peak buffer. Both are dropped here: the DPF shell snapshots
// parameters once per block (no cross-thread transfer needed) and Multi-Q never
// engages lookahead (no lookahead parameter exists → 0 samples). The envelope,
// soft-knee gain law, detection bandpass and 2 ms anti-zipper smoother are
// byte-for-byte the JUCE math (kneeWidth fixed at the JUCE default 6 dB, soft
// knee on, matching Multi-Q's dynParams which set only enabled/threshold/attack/
// release/range/ratio).

#pragma once

#include "MultiQFilters.hpp"   // duskaudio::kMultiQPi, safeIsFinite
#include <array>
#include <cmath>
#include <algorithm>

namespace duskaudio
{

class MultiQDynamics
{
public:
    static constexpr int NUM_BANDS = 8;

    struct BandParameters
    {
        float threshold = -20.0f;
        float attack = 10.0f;
        float release = 100.0f;
        float range = 12.0f;
        float kneeWidth = 6.0f;   // JUCE default; Multi-Q doesn't expose it
        float ratio = 4.0f;
        bool  enabled = false;
    };

    void prepare(double newSampleRate, int channelCount)
    {
        sampleRate = newSampleRate > 0 ? newSampleRate : 44100.0;
        numChannels = channelCount < 1 ? 1 : channelCount;
        reset();
        for (auto& c : detCoeffs) c = DetCoeffs{};
    }

    void reset()
    {
        for (auto& ch : channelStates)
            for (auto& b : ch.bands) { b.envelope = -96.0f; b.currentGainDb = 0.0f; b.smoothedGainDb = 0.0f; }
        for (auto& ch : biquadStates)
            for (auto& b : ch) b.reset();
        for (auto& m : dynamicGainMeters) m = 0.0f;
    }

    void updateSampleRate(double newRate) { sampleRate = newRate; reset(); }

    void setBandParameters(int band, const BandParameters& p)
    {
        if (band < 0 || band >= NUM_BANDS) return;
        BandParameters v = p;
        v.ratio = std::clamp(p.ratio, 1.0f, 100.0f);
        bandParams[(size_t)band] = v;
    }

    void updateDetectionFilter(int band, float frequency, float q)
    {
        if (band < 0 || band >= NUM_BANDS) return;
        detCoeffs[(size_t)band] = computeBandPassCoeffs(sampleRate, frequency, q);
    }

    // Bandpass-filter input for sidechain detection (DF2T), return |output|.
    float processDetection(int band, float input, int channel)
    {
        if (band < 0 || band >= NUM_BANDS || channel < 0 || channel >= numChannels)
            return std::abs(input);
        auto& state = biquadStates[(size_t)channel][(size_t)band];
        const auto& c = detCoeffs[(size_t)band].c;
        float output = c[0] * input + state.z1;
        state.z1 = c[1] * input - c[4] * output + state.z2;
        state.z2 = c[2] * input - c[5] * output;
        return std::abs(output);
    }

    // Envelope-follow the detection level and return the smoothed dynamic gain (dB, <=0).
    float processBand(int band, float detectionLevel, int channel)
    {
        if (band < 0 || band >= NUM_BANDS || channel < 0 || channel >= numChannels)
            return 0.0f;
        const auto& params = bandParams[(size_t)band];
        if (!params.enabled) return 0.0f;

        auto& state = channelStates[(size_t)channel].bands[(size_t)band];

        float inputDb = gainToDb(detectionLevel, -96.0f);
        float attackCoeff = calcCoefficient(params.attack);
        float releaseCoeff = calcCoefficient(params.release);

        if (inputDb > state.envelope)
            state.envelope = attackCoeff * state.envelope + (1.0f - attackCoeff) * inputDb;
        else
            state.envelope = releaseCoeff * state.envelope + (1.0f - releaseCoeff) * inputDb;

        state.currentGainDb = calculateDynamicGain(state.envelope, params);

        float smoothCoeff = calcCoefficient(2.0f); // 2 ms anti-zipper
        state.smoothedGainDb = smoothCoeff * state.smoothedGainDb + (1.0f - smoothCoeff) * state.currentGainDb;

        dynamicGainMeters[(size_t)band] = state.smoothedGainDb;
        return state.smoothedGainDb;
    }

    float getCurrentDynamicGain(int band) const
    {
        return (band >= 0 && band < NUM_BANDS) ? dynamicGainMeters[(size_t)band] : 0.0f;
    }

private:
    float calcCoefficient(float timeMs) const
    {
        if (timeMs <= 0.0f) return 0.0f;
        float tau = timeMs / 1000.0f;
        return std::exp(-1.0f / (tau * (float)sampleRate));
    }

    // juce::Decibels::gainToDecibels(gain, minusInfDb)
    static float gainToDb(float gain, float minusInf)
    {
        return gain > 0.0f ? std::max(minusInf, 20.0f * std::log10(gain)) : minusInf;
    }

    float calculateDynamicGain(float envelopeDb, const BandParameters& params) const
    {
        float threshold = params.threshold;
        float kneeWidth = softKneeEnabled ? params.kneeWidth : 0.0f;
        float halfKnee = kneeWidth / 2.0f;
        float ratio = std::max(1.0f, params.ratio);
        float reduction = 0.0f;

        if (envelopeDb < threshold - halfKnee)
            reduction = 0.0f;
        else if (envelopeDb > threshold + halfKnee || kneeWidth <= 0.0f)
        {
            float overThreshold = envelopeDb - threshold;
            reduction = overThreshold * (1.0f - 1.0f / ratio);
        }
        else
        {
            float x = envelopeDb - threshold + halfKnee;
            float kneeGain = (x * x) / (2.0f * kneeWidth);
            reduction = kneeGain * (1.0f - 1.0f / ratio);
        }

        reduction = std::min(reduction, params.range);
        return -reduction;
    }

    struct DetCoeffs { float c[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}; };

    // Audio EQ Cookbook bandpass (RBJ) — verbatim.
    static DetCoeffs computeBandPassCoeffs(double sr, float freq, float q)
    {
        DetCoeffs dc;
        if (sr <= 0.0) return dc;
        float safeFreq = std::clamp(freq, 20.0f, (float)(sr * 0.499));
        float safeQ = std::max(0.01f, q);
        double w0 = 2.0 * kMultiQPi * (double)safeFreq / sr;
        double alpha = std::sin(w0) / (2.0 * (double)safeQ);
        double a0 = 1.0 + alpha;
        dc.c[0] = (float)(alpha / a0);
        dc.c[1] = 0.0f;
        dc.c[2] = (float)(-alpha / a0);
        dc.c[3] = 1.0f;
        dc.c[4] = (float)(-2.0 * std::cos(w0) / a0);
        dc.c[5] = (float)((1.0 - alpha) / a0);
        return dc;
    }

    struct BiquadState { float z1 = 0.0f, z2 = 0.0f; void reset() { z1 = z2 = 0.0f; } };
    struct BandState { float envelope = -96.0f, currentGainDb = 0.0f, smoothedGainDb = 0.0f; };
    struct ChannelState { std::array<BandState, NUM_BANDS> bands; };

    double sampleRate = 44100.0;
    int numChannels = 2;
    bool softKneeEnabled = true;
    std::array<ChannelState, 2> channelStates;
    std::array<std::array<BiquadState, NUM_BANDS>, 2> biquadStates;
    std::array<BandParameters, NUM_BANDS> bandParams;
    std::array<DetCoeffs, NUM_BANDS> detCoeffs;
    std::array<float, NUM_BANDS> dynamicGainMeters{};
};

} // namespace duskaudio

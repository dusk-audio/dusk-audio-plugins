// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// MultiQDynamics.hpp — framework-free per-band dynamic EQ, ported from
// DynamicEQProcessor.h. Each band is an independent downward compressor whose
// gain-reduction drives a Cytomic dyn-gain SVF at the band's frequency.
//
// The JUCE original carries SeqLock/atomic UI->audio parameter transfer and an
// optional lookahead peak buffer. Both are dropped here: the DAF shell snapshots
// parameters once per block (no cross-thread transfer needed) and Multi-Q never
// engages lookahead (no lookahead parameter exists → 0 samples). The envelope,
// soft-knee gain law, detection bandpass, linked stereo detection, RMS mode and
// 2 ms anti-zipper smoother are the JUCE math (kneeWidth fixed at the JUCE
// default 6 dB, soft knee on, matching Multi-Q's dynParams which set only
// enabled/threshold/attack/release/range/ratio).
//
// Detection is stereo-linked: each channel's band-passed signal is followed by
// its own envelope and the louder envelope sets one gain for both channels, so
// the reduction does not depend on the phase between them (a band routed to one
// channel or to Mid/Side measures just that signal). Peak mode follows the
// rectified band signal; RMS mode the louder channel's RMS level over
// kRmsWindowSeconds.

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

    // RMS detection window (one-pole time constant). Long enough to average the
    // ripple of a steady tone in any band above about 100 Hz (the detected level
    // ripples by about +/-0.35 dB at 100 Hz and +/-0.035 dB at 1 kHz), short
    // enough that the band's own Attack and Release keep their meaning.
    static constexpr double kRmsWindowSeconds = 0.010;

    // Largest band-passed level the detectors take (+80 dBFS). A larger finite
    // input would overflow the RMS mean square to infinity, which never decays
    // and would hold the band at full range.
    static constexpr float kMaxDetectionLevel = 1.0e4f;

    void prepare(double newSampleRate, int channelCount)
    {
        sampleRate = newSampleRate > 0 ? newSampleRate : 44100.0;
        numChannels = channelCount < 1 ? 1 : channelCount;
        updateRmsCoefficient();
        reset();
        for (auto& c : detCoeffs) c = DetCoeffs{};
    }

    void reset()
    {
        for (auto& ch : channelStates)
            for (auto& b : ch.bands) { b.envelope = -96.0f; b.currentGainDb = 0.0f; b.smoothedGainDb = 0.0f; }
        for (auto& ch : biquadStates)
            for (auto& b : ch) b.reset();
        for (auto& ms : meanSquare) ms.fill(0.0f);
        for (auto& m : dynamicGainMeters) m = 0.0f;
    }

    void updateSampleRate(double newRate) { sampleRate = newRate; updateRmsCoefficient(); reset(); }

    // Dynamics Detection Mode: false = Peak (default), true = RMS.
    void setRmsDetection(bool rms) { rmsDetection = rms; }

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

    // Bandpass-filter input for sidechain detection (DF2T), return |output|,
    // at most kMaxDetectionLevel. A non-finite output (a NaN or infinite input)
    // clears this channel's filter and reads as silence, so one bad sample
    // cannot stop the band's dynamics.
    float processDetection(int band, float input, int channel)
    {
        if (band < 0 || band >= NUM_BANDS || channel < 0 || channel >= numChannels)
            return std::abs(input);
        auto& state = biquadStates[(size_t)channel][(size_t)band];
        const auto& c = detCoeffs[(size_t)band].c;
        float output = c[0] * input + state.z1;
        state.z1 = c[1] * input - c[4] * output + state.z2;
        state.z2 = c[2] * input - c[5] * output;
        if (!safeIsFinite(output))
        {
            state.reset();
            return 0.0f;
        }
        return std::min(std::abs(output), kMaxDetectionLevel);
    }

    // One sample of a band's stereo-linked dynamics. Band-passes each channel's
    // detection input, follows each with its own envelope (the band's attack and
    // release), and turns both channels down by one gain from the louder
    // envelope. Returns that gain, smoothed against zipper noise (dB, <= 0), and
    // updates the band's meter. Peak follows each channel's rectified band
    // signal; RMS the louder channel's RMS level over kRmsWindowSeconds, with
    // the right envelope tracking the left so a mode switch does not jump. Both
    // mean squares run in either mode for the same reason. For a band routed to
    // one channel, pass the other as 0; for Mid or Side, pass that signal twice.
    float processBandLinked(int band, float inL, float inR)
    {
        if (band < 0 || band >= NUM_BANDS)
            return 0.0f;
        const float dl = processDetection(band, inL, 0);
        const float dr = processDetection(band, inR, 1);
        auto& ms = meanSquare[(size_t)band];
        ms[0] = rmsCoeff * ms[0] + (1.0f - rmsCoeff) * (dl * dl);
        ms[1] = rmsCoeff * ms[1] + (1.0f - rmsCoeff) * (dr * dr);

        const auto& params = bandParams[(size_t)band];
        if (!params.enabled) return 0.0f;

        auto& left = channelStates[0].bands[(size_t)band];
        auto& right = channelStates[1].bands[(size_t)band];
        const float attackCoeff = calcCoefficient(params.attack);
        const float releaseCoeff = calcCoefficient(params.release);
        if (rmsDetection)
        {
            followEnvelope(left.envelope, std::sqrt(std::max(ms[0], ms[1])), attackCoeff, releaseCoeff);
            right.envelope = left.envelope;
        }
        else
        {
            followEnvelope(left.envelope, dl, attackCoeff, releaseCoeff);
            followEnvelope(right.envelope, dr, attackCoeff, releaseCoeff);
        }
        left.currentGainDb = calculateDynamicGain(std::max(left.envelope, right.envelope), params);

        const float smoothCoeff = calcCoefficient(2.0f); // 2 ms anti-zipper
        left.smoothedGainDb = smoothCoeff * left.smoothedGainDb + (1.0f - smoothCoeff) * left.currentGainDb;

        dynamicGainMeters[(size_t)band] = left.smoothedGainDb;
        return left.smoothedGainDb;
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

    void updateRmsCoefficient()
    {
        rmsCoeff = (float)std::exp(-1.0 / (kRmsWindowSeconds * sampleRate));
    }

    // juce::Decibels::gainToDecibels(gain, minusInfDb)
    static float gainToDb(float gain, float minusInf)
    {
        return gain > 0.0f ? std::max(minusInf, 20.0f * std::log10(gain)) : minusInf;
    }

    // dB-domain attack/release follower (the JUCE envelope).
    static void followEnvelope(float& envelope, float level, float attackCoeff, float releaseCoeff)
    {
        const float levelDb = gainToDb(level, -96.0f);
        if (levelDb > envelope)
            envelope = attackCoeff * envelope + (1.0f - attackCoeff) * levelDb;
        else
            envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * levelDb;
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
    bool rmsDetection = false;
    float rmsCoeff = 0.0f;                                 // RMS window pole, set from the rate
    std::array<ChannelState, 2> channelStates;
    std::array<std::array<BiquadState, NUM_BANDS>, 2> biquadStates;
    std::array<std::array<float, 2>, NUM_BANDS> meanSquare{}; // per band: L, R mean square
    std::array<BandParameters, NUM_BANDS> bandParams;
    std::array<DetCoeffs, NUM_BANDS> detCoeffs;
    std::array<float, NUM_BANDS> dynamicGainMeters{};
};

} // namespace duskaudio

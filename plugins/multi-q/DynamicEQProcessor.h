#pragma once

#include <JuceHeader.h>
#include "SafeFloat.h"
#include <array>
#include <atomic>
#include <vector>

// Per-band dynamic EQ: envelope followers with soft-knee compression and lookahead.
// Each band acts as an independent compressor at its frequency.
//
// Detection is stereo-linked: each channel's band-passed signal is followed by
// its own envelope and the louder envelope sets one gain for both channels, so
// the reduction does not depend on the phase between them (a band routed to one
// channel or to Mid/Side measures just that signal). Peak mode follows the
// rectified band signal; RMS mode the louder channel's RMS level over
// kRmsWindowSeconds. Multi-Q 2 (core/MultiQDynamics.hpp) does the same.
class DynamicEQProcessor
{
public:
    static constexpr int NUM_BANDS = 8;
    static constexpr int MAX_LOOKAHEAD_SAMPLES = 512;  // ~11ms at 44.1kHz

    // RMS detection window (one-pole time constant). Long enough to average the
    // ripple of a steady tone in any band above about 100 Hz (the detected level
    // ripples by about +/-0.35 dB at 100 Hz and +/-0.035 dB at 1 kHz), short
    // enough that the band's own Attack and Release keep their meaning.
    static constexpr double kRmsWindowSeconds = 0.010;

    // Largest band-passed level the detectors take (+80 dBFS). A larger finite
    // input would overflow the RMS mean square to infinity, which never decays
    // and would hold the band at full range.
    static constexpr float kMaxDetectionLevel = 1.0e4f;

    struct AudioDelayLine
    {
        std::vector<float> buffer;
        int writeIndex = 0;
    };

    struct BandParameters
    {
        float threshold = -20.0f;    // dB (-48 to 0) - lower = more sensitive
        float attack = 10.0f;        // ms (0.1 to 500)
        float release = 100.0f;      // ms (10 to 5000)
        float range = 12.0f;         // dB (0 to 24) - max gain change
        float kneeWidth = 6.0f;      // dB (0 to 12) - soft knee width, 0 = hard knee
        float ratio = 4.0f;          // Compression ratio (1:1 to 20:1)
        bool enabled = false;        // Per-band dynamics on/off
    };

    struct GlobalSettings
    {
        float lookaheadMs = 0.0f;    // 0 to 10ms lookahead
        bool softKneeEnabled = true;
    };

    DynamicEQProcessor() = default;

    void prepare(double newSampleRate, int channelCount)
    {
        sampleRate.store(newSampleRate, std::memory_order_release);
        numChannels = channelCount;
        updateRmsCoefficient();
        for (auto& ms : meanSquare)
            ms.fill(0.0f);

        channelStates.resize(static_cast<size_t>(numChannels));
        for (auto& ch : channelStates)
            for (auto& band : ch.bands)
                band = {};

        biquadStates.resize(static_cast<size_t>(numChannels));
        for (auto& ch : biquadStates)
            for (auto& band : ch)
                band.reset();

        activeDetCoeffsPerCh.resize(static_cast<size_t>(numChannels));
        for (auto& ch : activeDetCoeffsPerCh)
            for (auto& band : ch)
                band = DetCoeffs{};

        lastAppliedSeqPerCh.resize(static_cast<size_t>(numChannels));
        for (auto& ch : lastAppliedSeqPerCh)
            ch.fill(0);

        lookaheadBuffers.resize(static_cast<size_t>(numChannels));
        for (auto& ch : lookaheadBuffers)
        {
            ch.resize(NUM_BANDS);
            for (auto& band : ch)
            {
                band.buffer.resize(MAX_LOOKAHEAD_SAMPLES, 0.0f);
                band.writeIndex = 0;
                band.peakValue = 0.0f;
            }
        }

        audioDelayLines.resize(static_cast<size_t>(numChannels));
        for (auto& ch : audioDelayLines)
        {
            ch.resize(NUM_BANDS);
            for (auto& dl : ch)
            {
                dl.buffer.assign(MAX_LOOKAHEAD_SAMPLES + 1, 0.0f);
                dl.writeIndex = 0;
            }
        }

        for (auto& meter : dynamicGainMeters)
            meter.store(0.0f, std::memory_order_relaxed);

        // Reset coefficient transfer state so detection filters recompute for new sample rate
        for (auto& ct : coeffTransfers)
        {
            ct.sequence.store(0, std::memory_order_relaxed);
            for (auto& p : ct.pending)
                p.store(0.0f, std::memory_order_relaxed);
            // Reset to passthrough: b0=1, a0=1
            ct.pending[0].store(1.0f, std::memory_order_relaxed);
            ct.pending[3].store(1.0f, std::memory_order_relaxed);
        }

        // Force audio thread to re-read band parameters from transfers
        lastBandParamSeq.fill(0);

        int samples = static_cast<int>(globalSettings.lookaheadMs * sampleRate.load(std::memory_order_acquire) / 1000.0);
        samples = juce::jlimit(0, MAX_LOOKAHEAD_SAMPLES - 1, samples);
        lookaheadSamples.store(samples, std::memory_order_release);
    }

    void setGlobalSettings(const GlobalSettings& settings)
    {
        globalSettings = settings;
        softKneeEnabled.store(settings.softKneeEnabled, std::memory_order_release);
        int samples = static_cast<int>(settings.lookaheadMs * sampleRate.load(std::memory_order_acquire) / 1000.0);
        samples = juce::jlimit(0, MAX_LOOKAHEAD_SAMPLES - 1, samples);
        lookaheadSamples.store(samples, std::memory_order_release);
    }

    int getLookaheadSamples() const { return lookaheadSamples.load(std::memory_order_acquire); }

    float processAudioDelay(int bandIndex, int channel, float audioSample)
    {
        int la = lookaheadSamples.load(std::memory_order_acquire);
        if (la <= 0)
            return audioSample;

        if (channel < 0 || bandIndex < 0 ||
            channel >= static_cast<int>(audioDelayLines.size()) ||
            bandIndex >= static_cast<int>(audioDelayLines[0].size()))
            return audioSample;

        auto& dl = audioDelayLines[static_cast<size_t>(channel)][static_cast<size_t>(bandIndex)];
        int bufSize = static_cast<int>(dl.buffer.size());
        if (bufSize <= 0) return audioSample;

        la = std::min(la, bufSize - 1);
        int readIndex = (dl.writeIndex + bufSize - la) % bufSize;
        float delayed = dl.buffer[static_cast<size_t>(readIndex)];
        dl.buffer[static_cast<size_t>(dl.writeIndex)] = audioSample;
        dl.writeIndex = (dl.writeIndex + 1) % bufSize;
        return delayed;
    }

    void resetDelayLines()
    {
        for (auto& ch : audioDelayLines)
            for (auto& dl : ch)
            {
                std::fill(dl.buffer.begin(), dl.buffer.end(), 0.0f);
                dl.writeIndex = 0;
            }
    }

    void reset()
    {
        for (auto& ch : channelStates)
        {
            for (auto& band : ch.bands)
            {
                band.envelope = -96.0f;
                band.currentGainDb = 0.0f;
                band.smoothedGainDb = 0.0f;
            }
        }

        for (auto& ch : biquadStates)
        {
            for (auto& band : ch)
                band.reset();
        }

        resetDelayLines();

        for (auto& ms : meanSquare)
            ms.fill(0.0f);

        for (auto& meter : dynamicGainMeters)
            meter.store(0.0f, std::memory_order_relaxed);
    }

    /** Lightweight sample-rate update (no allocation). Safe to call from audio thread.
        Resets envelope state and updates the cached rate. Caller must call
        updateDetectionFilter() for each band so that detection filter coefficients
        are recalculated for the new sample rate. */
    void updateSampleRate(double newRate)
    {
        sampleRate.store(newRate, std::memory_order_release);
        updateRmsCoefficient();
        reset();
    }

    /** Dynamics Detection Mode: false = Peak (default), true = RMS. Audio thread. */
    void setRmsDetection(bool rms) { rmsDetection = rms; }

    // Lock-free SPSC band parameter transfer via SeqLock (UI thread writes, audio thread reads).
    // Torn reads are avoided: the audio thread caches a consistent snapshot and falls back
    // to the previous snapshot if a write is in progress.
    void setBandParameters(int bandIndex, const BandParameters& params)
    {
        if (bandIndex >= 0 && bandIndex < NUM_BANDS)
        {
            BandParameters validatedParams = params;
            // Clamp ratio to valid range (1.0 to 100.0) to avoid division issues
            validatedParams.ratio = juce::jlimit(1.0f, 100.0f, params.ratio);
            bandParamTransfers[static_cast<size_t>(bandIndex)].publish(validatedParams);
        }
    }

    const BandParameters& getBandParameters(int bandIndex) const
    {
        return bandParamTransfers[static_cast<size_t>(juce::jlimit(0, NUM_BANDS - 1, bandIndex))].data;
    }

    // Lock-free: publishes bandpass coefficients via SeqLock
    void updateDetectionFilter(int bandIndex, float frequency, float q)
    {
        if (bandIndex < 0 || bandIndex >= NUM_BANDS)
            return;

        auto dc = computeBandPassCoeffs(sampleRate.load(std::memory_order_acquire), frequency, q);
        coeffTransfers[static_cast<size_t>(bandIndex)].publish(dc);
    }

    // One sample of a band's stereo-linked dynamics. Band-passes each channel's
    // detection input, follows each with its own envelope (the band's attack and
    // release), and turns both channels down by one gain from the louder
    // envelope. Returns that gain, smoothed against zipper noise (dB, <= 0), and
    // updates the band's meter. Peak follows each channel's rectified band
    // signal; RMS the louder channel's RMS level over kRmsWindowSeconds, with
    // one envelope for both channels that starts from the louder channel's, so
    // a mode switch does not jump. Both mean squares run in either mode for the
    // same reason. For a band routed to one channel, pass the other as 0; for
    // Mid or Side, pass that signal twice.
    float processBandLinked(int bandIndex, float inL, float inR)
    {
        if (bandIndex < 0 || bandIndex >= NUM_BANDS || channelStates.size() < 2)
            return 0.0f;
        const auto bi = static_cast<size_t>(bandIndex);
        const float dl = processDetection(bandIndex, inL, 0);
        const float dr = processDetection(bandIndex, inR, 1);
        auto& ms = meanSquare[bi];
        ms[0] = rmsCoeff * ms[0] + (1.0f - rmsCoeff) * (dl * dl);
        ms[1] = rmsCoeff * ms[1] + (1.0f - rmsCoeff) * (dr * dr);

        // SeqLock read: pick up new parameters into audio thread's cached copy
        {
            auto& transfer = bandParamTransfers[bi];
            uint32_t seq = transfer.sequence.load(std::memory_order_acquire);
            if (seq != lastBandParamSeq[bi] && (seq & 1) == 0)
            {
                BandParameters temp = transfer.data;
                std::atomic_thread_fence(std::memory_order_acquire);
                if (transfer.sequence.load(std::memory_order_acquire) == seq)
                {
                    activeBandParams[bi] = temp;
                    lastBandParamSeq[bi] = seq;
                }
            }
        }
        const auto& params = activeBandParams[bi];
        if (!params.enabled)
            return 0.0f;

        auto& left = channelStates[0].bands[bi];
        auto& right = channelStates[1].bands[bi];
        const float attackCoeff = calcCoefficient(params.attack);
        const float releaseCoeff = calcCoefficient(params.release);
        if (rmsDetection)
        {
            // Entering RMS from Peak, start from the louder envelope; from then
            // on the two are equal and this changes nothing.
            left.envelope = juce::jmax(left.envelope, right.envelope);
            followEnvelope(left.envelope, lookaheadPeak(bandIndex, 0, std::sqrt(std::max(ms[0], ms[1]))),
                           attackCoeff, releaseCoeff);
            right.envelope = left.envelope;
        }
        else
        {
            followEnvelope(left.envelope, lookaheadPeak(bandIndex, 0, dl), attackCoeff, releaseCoeff);
            followEnvelope(right.envelope, lookaheadPeak(bandIndex, 1, dr), attackCoeff, releaseCoeff);
        }
        left.currentGainDb = calculateDynamicGain(juce::jmax(left.envelope, right.envelope), params);

        float smoothCoeff = calcCoefficient(2.0f);  // 2ms anti-zipper
        left.smoothedGainDb = smoothCoeff * left.smoothedGainDb +
                              (1.0f - smoothCoeff) * left.currentGainDb;

        // Always update meter to track current gain (decays naturally with envelope)
        dynamicGainMeters[bi].store(left.smoothedGainDb, std::memory_order_relaxed);

        return left.smoothedGainDb;
    }

    // Bandpass-filter input for sidechain detection (lock-free coefficient updates)
    // Each channel maintains its own copy of detection coefficients to avoid data races.
    // Returns |output|, at most kMaxDetectionLevel. A non-finite output (a NaN or
    // infinite input) clears this channel's filter and reads as silence, so one
    // bad sample cannot stop the band's dynamics.
    float processDetection(int bandIndex, float input, int channel)
    {
        if (bandIndex < 0 || bandIndex >= NUM_BANDS ||
            channel < 0 || channel >= static_cast<int>(biquadStates.size()))
            return std::abs(input);

        auto bi = static_cast<size_t>(bandIndex);
        auto ch = static_cast<size_t>(channel);

        // SeqLock read: pick up new coefficients into this channel's copy
        auto& transfer = coeffTransfers[bi];
        uint32_t seq = transfer.sequence.load(std::memory_order_acquire);
        uint32_t appliedSeq = lastAppliedSeqPerCh[ch][bi];
        if (seq != appliedSeq && (seq & 1) == 0)
        {
            DetCoeffs temp;
            for (int k = 0; k < 6; ++k)
                temp.c[k] = transfer.pending[static_cast<size_t>(k)].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t seq2 = transfer.sequence.load(std::memory_order_acquire);
            if (seq == seq2)
            {
                activeDetCoeffsPerCh[ch][bi] = temp;
                lastAppliedSeqPerCh[ch][bi] = seq;
            }
        }

        auto& state = biquadStates[ch][bi];
        const auto& c = activeDetCoeffsPerCh[ch][bi].c;

        // Direct Form II Transposed
        float output = c[0] * input + state.z1;
        state.z1 = c[1] * input - c[4] * output + state.z2;
        state.z2 = c[2] * input - c[5] * output;

        if (!safeIsFinite(output))
        {
            state.reset();
            return 0.0f;
        }
        return juce::jmin(std::abs(output), kMaxDetectionLevel);
    }

    float getCurrentDynamicGain(int bandIndex) const
    {
        if (bandIndex >= 0 && bandIndex < NUM_BANDS)
            return dynamicGainMeters[static_cast<size_t>(bandIndex)]
                .load(std::memory_order_relaxed);
        return 0.0f;
    }

    void decayMeters(float decayAmount = 0.5f)
    {
        for (auto& meter : dynamicGainMeters)
        {
            float current = meter.load(std::memory_order_relaxed);
            if (std::abs(current) > 0.01f)
                meter.store(current * (1.0f - decayAmount), std::memory_order_relaxed);
            else
                meter.store(0.0f, std::memory_order_relaxed);
        }
    }

private:
    float calcCoefficient(float timeMs) const
    {
        if (timeMs <= 0.0f)
            return 0.0f;
        float tau = timeMs / 1000.0f;
        return std::exp(-1.0f / (tau * static_cast<float>(sampleRate.load(std::memory_order_acquire))));
    }

    void updateRmsCoefficient()
    {
        rmsCoeff = static_cast<float>(std::exp(-1.0 / (kRmsWindowSeconds * sampleRate.load(std::memory_order_acquire))));
    }

    // dB-domain attack/release follower.
    static void followEnvelope(float& envelope, float level, float attackCoeff, float releaseCoeff)
    {
        const float levelDb = juce::Decibels::gainToDecibels(level, -96.0f);
        if (levelDb > envelope)
            envelope = attackCoeff * envelope + (1.0f - attackCoeff) * levelDb;
        else
            envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * levelDb;
    }

    // Detection level held at its peak over the lookahead window (the level
    // itself when lookahead is off, as it always is in Multi-Q).
    float lookaheadPeak(int bandIndex, int channel, float level)
    {
        const int currentLookahead = lookaheadSamples.load(std::memory_order_acquire);
        if (currentLookahead <= 0 ||
            channel >= static_cast<int>(lookaheadBuffers.size()) ||
            bandIndex >= static_cast<int>(lookaheadBuffers[static_cast<size_t>(channel)].size()))
            return level;

        auto& lookahead = lookaheadBuffers[static_cast<size_t>(channel)][static_cast<size_t>(bandIndex)];
        lookahead.buffer[static_cast<size_t>(lookahead.writeIndex)] = level;

        float peak = 0.0f;
        int bufSize = static_cast<int>(lookahead.buffer.size());
        for (int i = 0; i < currentLookahead; ++i)
        {
            int idx = (lookahead.writeIndex - i + bufSize) % bufSize;
            peak = juce::jmax(peak, lookahead.buffer[static_cast<size_t>(idx)]);
        }
        lookahead.writeIndex = (lookahead.writeIndex + 1) % bufSize;
        return peak;
    }

    float calculateDynamicGain(float envelopeDb, const BandParameters& params) const
    {
        float threshold = params.threshold;
        float kneeWidth = softKneeEnabled.load(std::memory_order_acquire) ? params.kneeWidth : 0.0f;
        float halfKnee = kneeWidth / 2.0f;
        float ratio = juce::jmax(1.0f, params.ratio);
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
            // Soft knee: quadratic interpolation
            float x = envelopeDb - threshold + halfKnee;
            float kneeGain = (x * x) / (2.0f * kneeWidth);
            reduction = kneeGain * (1.0f - 1.0f / ratio);
        }

        reduction = juce::jmin(reduction, params.range);
        return -reduction;
    }

    // Lock-free SPSC coefficient transfer via SeqLock

    // Biquad coefficients: b0,b1,b2, a0(=1), a1, a2. Default = passthrough.
    struct DetCoeffs
    {
        float c[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    };

    struct CoeffTransfer
    {
        std::atomic<uint32_t> sequence{0};
        std::array<std::atomic<float>, 6> pending;

        // Runtime init for C++17 portability (aggregate init of std::atomic is non-portable pre-C++20)
        CoeffTransfer()
        {
            pending[0].store(1.0f, std::memory_order_relaxed);  // b0
            pending[1].store(0.0f, std::memory_order_relaxed);  // b1
            pending[2].store(0.0f, std::memory_order_relaxed);  // b2
            pending[3].store(1.0f, std::memory_order_relaxed);  // a0
            pending[4].store(0.0f, std::memory_order_relaxed);  // a1
            pending[5].store(0.0f, std::memory_order_relaxed);  // a2
        }

        void publish(const DetCoeffs& newCoeffs)
        {
            sequence.fetch_add(1, std::memory_order_acq_rel); // odd = writing; acq prevents stores moving up
            for (int i = 0; i < 6; ++i)
                pending[static_cast<size_t>(i)].store(newCoeffs.c[i], std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release); // ensure data written before even
            sequence.fetch_add(1, std::memory_order_release); // even = done
        }
    };

    struct BiquadState
    {
        float z1 = 0.0f;
        float z2 = 0.0f;
        void reset() { z1 = z2 = 0.0f; }
    };

    // Audio EQ Cookbook bandpass
    static DetCoeffs computeBandPassCoeffs(double sr, float freq, float q)
    {
        DetCoeffs dc;
        if (sr <= 0.0)
            return dc;
        float safeFreq = juce::jlimit(20.0f, static_cast<float>(sr * 0.499), freq);
        float safeQ = juce::jmax(0.01f, q);

        double w0 = 2.0 * juce::MathConstants<double>::pi * static_cast<double>(safeFreq) / sr;
        double alpha = std::sin(w0) / (2.0 * static_cast<double>(safeQ));
        double a0 = 1.0 + alpha;

        dc.c[0] = static_cast<float>(alpha / a0);
        dc.c[1] = 0.0f;
        dc.c[2] = static_cast<float>(-alpha / a0);
        dc.c[3] = 1.0f;
        dc.c[4] = static_cast<float>(-2.0 * std::cos(w0) / a0);
        dc.c[5] = static_cast<float>((1.0 - alpha) / a0);
        return dc;
    }

    struct BandState
    {
        float envelope = -96.0f;      // Current envelope level (dB), starts at silence
        float currentGainDb = 0.0f;   // Current dynamic gain (dB)
        float smoothedGainDb = 0.0f;  // Smoothed gain for output
    };

    struct ChannelState
    {
        std::array<BandState, NUM_BANDS> bands;
    };

    std::vector<ChannelState> channelStates;

    // Band parameter transfer: UI thread publishes via SeqLock, audio thread caches
    struct BandParamTransfer
    {
        std::atomic<uint32_t> sequence{0};
        BandParameters data;

        void publish(const BandParameters& p)
        {
            sequence.fetch_add(1, std::memory_order_acq_rel);
            data = p;
            std::atomic_thread_fence(std::memory_order_release);
            sequence.fetch_add(1, std::memory_order_release);
        }
    };
    std::array<BandParamTransfer, NUM_BANDS> bandParamTransfers;
    std::array<BandParameters, NUM_BANDS> activeBandParams;    // Audio thread's cached copy
    std::array<uint32_t, NUM_BANDS> lastBandParamSeq{};

    std::array<CoeffTransfer, NUM_BANDS> coeffTransfers;
    // Per-channel coefficient and sequence tracking to avoid cross-channel data races
    std::vector<std::array<DetCoeffs, NUM_BANDS>> activeDetCoeffsPerCh;
    std::vector<std::array<uint32_t, NUM_BANDS>> lastAppliedSeqPerCh;
    std::vector<std::array<BiquadState, NUM_BANDS>> biquadStates;

    std::array<std::atomic<float>, NUM_BANDS> dynamicGainMeters;

    struct LookaheadBuffer
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        float peakValue = 0.0f;
    };
    std::vector<std::vector<LookaheadBuffer>> lookaheadBuffers;
    std::vector<std::vector<AudioDelayLine>> audioDelayLines; // [channel][band]

    GlobalSettings globalSettings;
    std::atomic<int> lookaheadSamples{0};
    std::atomic<bool> softKneeEnabled{true};
    int numChannels = 2;

    // RMS detection (audio thread only)
    bool rmsDetection = false;
    float rmsCoeff = 0.0f;                                     // RMS window pole, set from the rate
    std::array<std::array<float, 2>, NUM_BANDS> meanSquare{};  // per band: L, R mean square

    std::atomic<double> sampleRate{44100.0};
};

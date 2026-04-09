#pragma once

#include "DspUtils.h"

#include <cmath>
#include <cstring>
#include <vector>

// Lightweight stereo chorus for amplitude modulation on the reverb tail.
// Uses 4 short modulated delay lines (1-10ms) mixed with the input to create
// the shimmer/chorus character that VV imparts on its reverb output.
//
// Design:
//   - 4 voices with independent sine LFOs at slightly different rates (0.5-3Hz)
//   - Each voice reads from a short delay line with LFO-modulated read position
//   - Voices are panned: 2 voices per channel, with cross-feed for stereo width
//   - depth=0 completely bypasses (no CPU cost beyond the branch)
//   - No additional latency: the dry path is pass-through, chorus is additive
//
// Typical usage for reverb tail shimmer:
//   depth = 0.3-0.7, rate = 1.0-2.5 Hz
class TailChorus
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;

        // Max delay: 12ms at current sample rate (enough for 10ms center + 2ms sweep)
        int maxDelaySamples = static_cast<int> (std::ceil (0.012 * sampleRate));
        bufferSize_ = DspUtils::nextPowerOf2 (maxDelaySamples + 4); // +4 for cubic interpolation
        bufferMask_ = bufferSize_ - 1;

        for (int v = 0; v < kNumVoices; ++v)
        {
            delayBufL_[v].assign (static_cast<size_t> (bufferSize_), 0.0f);
            delayBufR_[v].assign (static_cast<size_t> (bufferSize_), 0.0f);
        }

        writePos_ = 0;

        // Initialize per-voice LFO phases: spread evenly across the cycle
        // to decorrelate the modulation and avoid periodic pumping
        for (int v = 0; v < kNumVoices; ++v)
            lfoPhase_[v] = static_cast<float> (v) / static_cast<float> (kNumVoices);

        updateLFOIncrements();
        updateDelayParameters();
    }

    // Set chorus depth: 0.0 = fully bypassed, 1.0 = maximum chorus effect.
    // The depth controls both the LFO sweep range and the wet/dry blend
    // of the chorus voices into the signal.
    void setDepth (float depth)
    {
        depth_ = std::max (0.0f, std::min (1.0f, depth));
    }

    // Set base LFO rate in Hz. Individual voices are offset from this base
    // by small ratios (0.8x to 1.3x) to create natural beating patterns.
    void setRate (float hz)
    {
        baseRate_ = std::max (0.1f, std::min (5.0f, hz));
        updateLFOIncrements();
    }

    // Process stereo audio in-place. Only modifies the signal when depth > 0.
    void process (float* left, float* right, int numSamples)
    {
        if (depth_ < 1e-6f)
            return;

        // Blend factor: how much chorus signal is mixed in.
        // Scaled so depth=0.5 gives a moderate effect, depth=1.0 is full.
        float wetGain = depth_ * 0.35f; // Max 35% wet blend at full depth

        // LFO sweep range in samples: at depth=1, sweep ±2ms around center
        float sweepSamples = depth_ * kMaxSweepMs * 0.001f
                           * static_cast<float> (sampleRate_);

        for (int i = 0; i < numSamples; ++i)
        {
            auto wp = static_cast<size_t> (writePos_);
            float inL = left[i];
            float inR = right[i];

            // Write input into all voice delay lines
            for (int v = 0; v < kNumVoices; ++v)
            {
                delayBufL_[v][wp] = inL;
                delayBufR_[v][wp] = inR;
            }

            // Accumulate modulated reads from each voice
            float chorusL = 0.0f;
            float chorusR = 0.0f;

            for (int v = 0; v < kNumVoices; ++v)
            {
                // Sine LFO: simple and band-limited (no harmonics = no aliasing)
                float lfo = std::sin (lfoPhase_[v] * 6.283185307179586f);

                // Modulated delay time: center delay + LFO sweep
                float delaySamples = centerDelaySamples_[v] + lfo * sweepSamples;
                delaySamples = std::max (1.0f, delaySamples); // clamp minimum

                // Fractional read position with cubic interpolation
                float readPosF = static_cast<float> (writePos_) - delaySamples;
                int readPosI = static_cast<int> (std::floor (readPosF));
                float frac = readPosF - static_cast<float> (readPosI);

                float sL = DspUtils::cubicHermite (delayBufL_[v].data(),
                                                   bufferMask_, readPosI, frac);
                float sR = DspUtils::cubicHermite (delayBufR_[v].data(),
                                                   bufferMask_, readPosI, frac);

                // Pan voices: voices 0,2 lean left; voices 1,3 lean right
                // with moderate cross-feed for a natural stereo spread
                float panL = kVoicePanL[v];
                float panR = kVoicePanR[v];
                chorusL += sL * panL + sR * (1.0f - panL) * 0.3f;
                chorusR += sR * panR + sL * (1.0f - panR) * 0.3f;

                // Advance LFO phase
                lfoPhase_[v] += lfoIncrement_[v];
                if (lfoPhase_[v] >= 1.0f)
                    lfoPhase_[v] -= 1.0f;
            }

            // Normalize by voice count and mix into output
            float scale = wetGain * (1.0f / static_cast<float> (kNumVoices));
            float outL = inL + chorusL * scale;
            float outR = inR + chorusR * scale;

            // Amplitude modulation: multiply signal by slow LFO.
            // This creates the envelope variation that the mod_depth metric measures.
            // Uses 2 LFOs at different rates for L/R stereo decorrelation.
            float amDepth = depth_ * 0.70f; // ±70% amplitude swing at full depth (~3.5dB)
            float amL = 1.0f + amDepth * std::sin (amPhaseL_ * 6.283185307179586f);
            float amR = 1.0f + amDepth * std::sin (amPhaseR_ * 6.283185307179586f);
            left[i]  = outL * amL;
            right[i] = outR * amR;

            amPhaseL_ += amIncrementL_;
            amPhaseR_ += amIncrementR_;
            if (amPhaseL_ >= 1.0f) amPhaseL_ -= 1.0f;
            if (amPhaseR_ >= 1.0f) amPhaseR_ -= 1.0f;

            writePos_ = (writePos_ + 1) & bufferMask_;
        }
    }

    void reset()
    {
        for (int v = 0; v < kNumVoices; ++v)
        {
            std::memset (delayBufL_[v].data(), 0,
                         static_cast<size_t> (bufferSize_) * sizeof (float));
            std::memset (delayBufR_[v].data(), 0,
                         static_cast<size_t> (bufferSize_) * sizeof (float));
            lfoPhase_[v] = static_cast<float> (v) / static_cast<float> (kNumVoices);
        }
        writePos_ = 0;
    }

private:
    static constexpr int kNumVoices = 4;

    // Center delay times in ms per voice: offset from each other to decorrelate
    // comb filter notches and create a denser modulation pattern.
    // Primes-ish ratios avoid coincident notches.
    static constexpr float kCenterDelayMs[kNumVoices] = { 3.7f, 5.3f, 7.1f, 4.3f };

    // Per-voice rate multipliers relative to baseRate_:
    // spread across 0.8x-1.3x to create natural beating between voices
    static constexpr float kRateMultiplier[kNumVoices] = { 0.83f, 1.0f, 1.17f, 1.31f };

    // Stereo panning per voice (L channel gain; R = 1-L for complement)
    // Voices alternate between left-leaning and right-leaning
    static constexpr float kVoicePanL[kNumVoices] = { 0.85f, 0.15f, 0.75f, 0.25f };
    static constexpr float kVoicePanR[kNumVoices] = { 0.15f, 0.85f, 0.25f, 0.75f };

    // Maximum LFO sweep range in ms (at depth = 1.0)
    static constexpr float kMaxSweepMs = 2.0f;

    void updateLFOIncrements()
    {
        if (sampleRate_ <= 0.0)
            return;

        float invSr = 1.0f / static_cast<float> (sampleRate_);
        for (int v = 0; v < kNumVoices; ++v)
            lfoIncrement_[v] = baseRate_ * kRateMultiplier[v] * invSr;

        // AM LFOs at slower rates than chorus (0.7x and 1.1x of base)
        amIncrementL_ = baseRate_ * 0.7f * invSr;
        amIncrementR_ = baseRate_ * 1.1f * invSr;
    }

    void updateDelayParameters()
    {
        for (int v = 0; v < kNumVoices; ++v)
            centerDelaySamples_[v] = kCenterDelayMs[v] * 0.001f
                                   * static_cast<float> (sampleRate_);
    }

    double sampleRate_ = 44100.0;
    int bufferSize_ = 0;
    int bufferMask_ = 0;
    int writePos_ = 0;

    float depth_ = 0.0f;
    float baseRate_ = 1.5f; // Default: 1.5 Hz

    std::vector<float> delayBufL_[kNumVoices];
    std::vector<float> delayBufR_[kNumVoices];

    float lfoPhase_[kNumVoices] = {};
    float lfoIncrement_[kNumVoices] = {};
    float centerDelaySamples_[kNumVoices] = {};

    // Amplitude modulation LFOs (L/R at different rates for stereo)
    float amPhaseL_ = 0.0f;
    float amPhaseR_ = 0.33f;  // 1/3 cycle offset from L
    float amIncrementL_ = 0.0f;
    float amIncrementR_ = 0.0f;
};

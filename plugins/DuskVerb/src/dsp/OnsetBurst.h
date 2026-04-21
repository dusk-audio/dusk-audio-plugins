#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Onset noise-burst generator. On transient trigger, plays a short burst of
// band-limited pseudo-random noise through an RBJ bandpass biquad, shaped by a
// linear attack/hold/release envelope. Mixed additively into the wet output.
//
// Purpose: flatten onset spectrum for presets whose FDN tank produces tonal
// ringing at the onset (onsetRing / tailRing metrics). Broadband noise fills
// spectral gaps that no envelope-shaped tank output can cover.
//
// Design:
//   - Per-channel xorshift32 PRNG (independent L/R for stereo decorrelation)
//   - Shared RBJ bandpass coefficients (computed once at algorithm switch)
//   - Per-channel biquad state
//   - Linear attack / flat hold / linear release envelope
//   - burstLevelLinear = 10^(peakDb / 20), scaled at runtime
class OnsetBurst
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        reset();
    }

    void reset()
    {
        phaseSamples_ = -1;
        bpX1L_ = bpX2L_ = bpY1L_ = bpY2L_ = 0.0f;
        bpX1R_ = bpX2R_ = bpY1R_ = bpY2R_ = 0.0f;
        prngL_ = 0x12345678u;
        prngR_ = 0x87654321u;
    }

    // Compute RBJ bandpass biquad coefficients (constant-skirt gain, peak Q)
    // and cache envelope timing + level. Called at algorithm switch.
    void configure (float peakDb, float delayMs, float attackMs, float holdMs, float releaseMs,
                    float bandLoHz, float bandHiHz, float bandQ)
    {
        enabled_ = peakDb > -59.9f;
        if (! enabled_)
            return;

        burstLevelLinear_ = std::pow (10.0f, peakDb / 20.0f);
        const float sr = static_cast<float> (sampleRate_);
        delaySamples_   = static_cast<int> (std::max (0.0f, delayMs)   * 0.001f * sr);
        attackSamples_  = static_cast<int> (std::max (1.0f, attackMs)  * 0.001f * sr);
        holdSamples_    = static_cast<int> (std::max (0.0f, holdMs)    * 0.001f * sr);
        releaseSamples_ = static_cast<int> (std::max (1.0f, releaseMs) * 0.001f * sr);

        // Centre frequency = geometric mean of band edges.
        const float fc = std::sqrt (bandLoHz * bandHiHz);
        const float twoPi = 6.283185307179586f;
        const float w0 = twoPi * fc / sr;
        const float cs = std::cos (w0);
        const float sn = std::sin (w0);
        const float alpha = sn / (2.0f * std::max (bandQ, 0.1f));

        // RBJ bandpass, peak gain = Q (constant-skirt). Normalised to peak=1.
        const float a0 = 1.0f + alpha;
        b0_ =  alpha / a0;
        b1_ =  0.0f;
        b2_ = -alpha / a0;
        a1_ = (-2.0f * cs) / a0;
        a2_ = (1.0f - alpha) / a0;
    }

    // Trigger from the shared input-transient detector. Resets phase only;
    // does not reset biquad state so an already-ringing filter decays
    // naturally rather than clicking to zero.
    void trigger() { phaseSamples_ = 0; }

    bool isEnabled() const { return enabled_; }

    // Process one stereo sample, additively mixing the shaped noise burst into
    // scratchL / scratchR. Caller advances the phase; we just compute one
    // sample and return whether the envelope is still active.
    inline bool processSample (float& scratchL, float& scratchR)
    {
        if (! enabled_ || phaseSamples_ < 0)
            return false;

        const int total = delaySamples_ + attackSamples_ + holdSamples_ + releaseSamples_;
        if (phaseSamples_ >= total)
        {
            phaseSamples_ = -1;
            return false;
        }

        // Silence during delay → linear attack → flat hold → linear release.
        float env;
        if (phaseSamples_ < delaySamples_)
            env = 0.0f;
        else if (phaseSamples_ < delaySamples_ + attackSamples_)
        {
            const int ap = phaseSamples_ - delaySamples_;
            env = static_cast<float> (ap) / static_cast<float> (attackSamples_);
        }
        else if (phaseSamples_ < delaySamples_ + attackSamples_ + holdSamples_)
            env = 1.0f;
        else
        {
            const int rp = phaseSamples_ - delaySamples_ - attackSamples_ - holdSamples_;
            env = 1.0f - static_cast<float> (rp) / static_cast<float> (releaseSamples_);
        }

        // xorshift32 → [-1, 1)
        const float noiseL = xorshift (prngL_);
        const float noiseR = xorshift (prngR_);
        const float gainSample = burstLevelLinear_ * env;

        // RBJ bandpass biquad, Direct Form I.
        const float xL = noiseL * gainSample;
        const float yL = b0_ * xL + b1_ * bpX1L_ + b2_ * bpX2L_
                       - a1_ * bpY1L_ - a2_ * bpY2L_;
        bpX2L_ = bpX1L_; bpX1L_ = xL; bpY2L_ = bpY1L_; bpY1L_ = yL;

        const float xR = noiseR * gainSample;
        const float yR = b0_ * xR + b1_ * bpX1R_ + b2_ * bpX2R_
                       - a1_ * bpY1R_ - a2_ * bpY2R_;
        bpX2R_ = bpX1R_; bpX1R_ = xR; bpY2R_ = bpY1R_; bpY1R_ = yR;

        scratchL += yL;
        scratchR += yR;
        ++phaseSamples_;
        return true;
    }

private:
    // xorshift32 PRNG, returns float in [-1, 1).
    static inline float xorshift (uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // Map uint32 → [-1, 1) with uniform distribution.
        return static_cast<float> (static_cast<int32_t> (state))
             * (1.0f / 2147483648.0f);
    }

    double sampleRate_ = 44100.0;
    bool enabled_ = false;

    // Envelope.
    int phaseSamples_ = -1;
    int delaySamples_ = 0, attackSamples_ = 0, holdSamples_ = 0, releaseSamples_ = 0;
    float burstLevelLinear_ = 0.0f;

    // Biquad coefficients (shared L/R).
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;

    // Biquad state (Direct Form I) — independent per channel.
    float bpX1L_ = 0.0f, bpX2L_ = 0.0f, bpY1L_ = 0.0f, bpY2L_ = 0.0f;
    float bpX1R_ = 0.0f, bpX2R_ = 0.0f, bpY1R_ = 0.0f, bpY2R_ = 0.0f;

    // Per-channel PRNG state.
    uint32_t prngL_ = 0x12345678u;
    uint32_t prngR_ = 0x87654321u;
};

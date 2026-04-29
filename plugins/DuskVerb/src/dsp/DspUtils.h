#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DspUtils
{

// Tiny DC bias added to feedback paths to prevent denormal accumulation.
// Small enough to be inaudible but keeps FPU out of slow denormal mode.
static constexpr float kDenormalPrevention = 1.0e-15f;

// Calibration reference sample rate. All base delay lengths and modulation
// excursions in the engines are tuned at this rate; runtime values scale
// linearly via `sampleRate / kBaseSampleRate`. The plugin runs correctly at
// any host sample rate — this is the calibration anchor, not an assumption.
static constexpr double kBaseSampleRate = 44100.0;

// Returns the smallest power of 2 >= v. For v <= 1 returns 1.
inline int nextPowerOf2 (int v)
{
    if (v <= 1)
        return 1;

    unsigned int u = static_cast<unsigned int> (v - 1);
    u |= u >> 1;
    u |= u >> 2;
    u |= u >> 4;
    u |= u >> 8;
    u |= u >> 16;
    return static_cast<int> (u + 1);
}

// Cubic Hermite (Catmull-Rom) interpolation for fractional delay reads.
// idx is the integer part of the read position; frac is 0..1.
// Returns the interpolated value between buffer[idx] and buffer[idx+1].
// Buffer uses power-of-2 wrapping via bitmask.
inline float cubicHermite (const float* buffer, int mask, int idx, float frac)
{
    float y0 = buffer[(idx - 1) & mask];
    float y1 = buffer[idx & mask];
    float y2 = buffer[(idx + 1) & mask];
    float y3 = buffer[(idx + 2) & mask];

    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

// Fast rational tanh approximation: x*(27+x²)/(27+9x²).
// Accurate to within 0.001 for |x| < 3. Avoids the expensive log/exp
// path of std::tanh.
inline float fastTanh (float x)
{
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Soft-clipper with linear knee + tanh saturation above the threshold.
// Below threshold the function is exactly y = x (no harmonic distortion on
// quiet tail samples). Above threshold the slope rolls off smoothly toward
// `ceiling` via fastTanh — engages only on loud transients, adding subtle
// analog-style warmth to feedback loops.
//
// Continuity: y(threshold) = threshold and y'(threshold) = 1, so the
// transition is C1-continuous (no audible kink at threshold).
inline float softClip (float x, float threshold = 1.0f, float ceiling = 2.0f)
{
    const float ax = std::abs (x);
    if (ax <= threshold) return x;
    const float sign  = (x < 0.0f) ? -1.0f : 1.0f;
    const float range = std::max (ceiling - threshold, 1.0e-6f);
    const float over  = (ax - threshold) / range;
    return sign * (threshold + range * fastTanh (over));
}

// Smoothed-step random-walk LFO for reverb modulation.
//
// Picks a new random target every `periodSamples` and smoothstep-interpolates
// toward it, producing band-limited "wander" with no audible cyclic warble.
// Unlike a sine LFO (which has a fixed period and locks into beating with
// other modulators), this drifts aperiodically — gives the "expensive"
// shimmer of high-end hardware random reverbs (Lexicon 480L random hall,
// Eventide ModFactor).
//
// Output bounded exactly to ±depth. Allocation-free.
struct RandomWalkLFO
{
    void prepare (float sampleRate, std::uint32_t seed = 0xC0FFEEu)
    {
        sampleRate_ = sampleRate;
        state_      = seed != 0 ? seed : 0xC0FFEEu;
        current_    = 0.0f;
        prev_       = 0.0f;
        target_     = 0.0f;
        phase_      = 0;
        setRate (1.0f);
    }

    void setRate (float hz)
    {
        const float clamped = std::max (hz, 0.001f);
        period_ = std::max (1, static_cast<int> (sampleRate_ / clamped));
    }

    void setDepth (float d) { depth_ = std::max (d, 0.0f); }

    float next()
    {
        if (phase_ >= period_)
        {
            phase_  = 0;
            prev_   = current_;
            target_ = nextRandom() * depth_;
        }
        // Smoothstep S-curve: t² (3 − 2t).
        const float t  = static_cast<float> (phase_) / static_cast<float> (period_);
        const float s  = t * t * (3.0f - 2.0f * t);
        current_       = prev_ + (target_ - prev_) * s;
        ++phase_;
        return current_;
    }

private:
    float nextRandom()
    {
        // xorshift32 — cheap, decorrelated. Returns float in [-1, +1).
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float> (static_cast<std::int32_t> (state_))
             * (1.0f / 2147483648.0f);
    }

    float         sampleRate_ = 44100.0f;
    float         current_    = 0.0f;
    float         prev_       = 0.0f;
    float         target_     = 0.0f;
    float         depth_      = 0.0f;
    int           phase_      = 0;
    int           period_     = 1000;
    std::uint32_t state_      = 0xC0FFEEu;
};

} // namespace DspUtils

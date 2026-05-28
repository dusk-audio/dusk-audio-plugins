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

// Phase 2 modulation topology selector. Engines can be in legacy random-
// walk mode (independent per-line LFOs) or coherent-loop mode (single
// master sine with phase-paired per-line offsets). Per-preset field,
// flowed through FactoryPreset and applied via the engine glue.
enum class ModulationTopology : int
{
    RandomWalk   = 0,    // legacy — independent per-line random walks
    CoherentLoop = 1,    // Phase 2 — single master sine, phase-paired lines
};


// Smoothed-step random-walk LFO for reverb modulation.
//
// Picks a new random target every `periodSamples` and smoothstep-interpolates
// toward it, producing band-limited "wander" with no audible cyclic warble.
// Unlike a sine LFO (which has a fixed period and locks into beating with
// other modulators), this drifts aperiodically — gives the "expensive"
// shimmer of high-end hardware random reverbs (1980s flagship hall hardware random hall,
// modern modulation pedal).
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


// =====================================================================
// CoherentSineLFO — single master sine generator with phase-tap readout.
//
// Phase 2 modulation topology (2026-05-28). The existing RandomWalkLFO
// gives smooth aperiodic wander but per-line LFOs are statistically
// independent — across many delay lines the modulation envelope sums to
// ~zero, killing the macro-rhythmic envelope ripple that vintage
// hardware reverbs produce (osc P2P ±22 dB on VVV).
//
// CoherentLoop topology: ONE master sine LFO, sampled at PER-LINE phase
// offsets. Pair (line_i, line_j) sample the sine 180° apart so when
// line_i runs short, line_j runs long — coherent macro motion that
// creates the rhythmic pumping ripple. Each line still gets a small
// phase spread inside its pair-half so the full chorus of lines doesn't
// collapse into mono.
//
// Allocation-free. Per-sample cost = 1× std::sin + 1× phase wrap.
struct CoherentSineLFO
{
    static constexpr float kTwoPi = 6.283185307179586f;

    void prepare (float sampleRate, std::uint32_t seed = 0xC0FFEEu)
    {
        sampleRate_ = sampleRate;
        // Seed → starting phase in [0, 2π). Stays deterministic per render.
        phase_ = (static_cast<float> (seed & 0xFFFFu) / 65535.0f) * kTwoPi;
        phaseInc_ = 0.0f;
    }

    void setRate (float hz)
    {
        const float clamped = std::max (hz, 0.001f);
        phaseInc_ = kTwoPi * clamped / sampleRate_;
    }

    // Advance the master phase by one sample. Call ONCE per sample
    // (NOT per delay line) — all readers tap the same master.
    void advance()
    {
        phase_ += phaseInc_;
        if (phase_ >= kTwoPi) phase_ -= kTwoPi;
    }

    // Sample the sine at master phase + per-line offset. The pairOffset
    // separates pair-mates by π so they oppose; the intra-pair spread
    // gives each line its own decorrelated trail.
    float read (float pairOffset) const
    {
        return std::sin (phase_ + pairOffset);
    }

    float phase() const { return phase_; }
    void  reset()       { phase_ = 0.0f; }

private:
    float sampleRate_ = 44100.0f;
    float phase_      = 0.0f;
    float phaseInc_   = 0.0f;
};

} // namespace DspUtils

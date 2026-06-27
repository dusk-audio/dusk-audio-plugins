#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

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
    // fastTanh is a rational approximation that only saturates near |over| <= 3
    // (fastTanh(3) == 1 exactly) and DIVERGES past that — for large drive it
    // grows ~over/9, so the raw result would push output beyond `ceiling`.
    // Clamp the drive to [0,1] so the output can never exceed threshold + range
    // (== ceiling); the clamp only engages for over > 3 (inputs already well
    // above ceiling), leaving the in-knee curve unchanged.
    float drive = fastTanh (over);
    drive = std::min (std::max (drive, 0.0f), 1.0f);
    return sign * (threshold + range * drive);
}

// Phase 2 modulation topology selector. Engines can be in legacy random-
// walk mode (independent per-line LFOs) or coherent-loop mode (single
// master sine with phase-paired per-line offsets). Per-preset field,
// flowed through FactoryPreset and applied via the engine glue.
enum class ModulationTopology : int
{
    RandomWalk       = 0,    // legacy — independent per-line random walks
    CoherentLoop     = 1,    // Phase 2 — single master sine, phase-paired lines
    ModulatedDamping = 2,    // Phase 3 — STATIC delay lines (zero Doppler) +
                             // slow master LFO that lerps per-line ThreeBand
                             // damping coefficients between dark/bright sets.
                             // No pitch warble, no harmonic stack — mimics
                             // VVV's tank-coupled mod character on vocal
                             // material where any line-length LFO produces
                             // audible Doppler sidebands.
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

// =============================================================================
// ParametricBand — 2nd-order RBJ peaking/bell biquad
//
// One band of a parametric EQ. Stereo state (separate L/R direct-form-I delays).
// Coefficient design is offline (prepare / setBand); the audio thread only
// runs the 5-mul/4-add biquad per channel — zero transcendentals.
//
// Default state (gainDb = 0): designs a unity-gain coefficient set so the
// biquad output equals input bit-identically. This is the bypass guarantee
// PostTankEQ relies on for legacy presets that don't configure bands.
//
// Cookbook (Robert Bristow-Johnson):
//   w0 = 2π f0 / Fs
//   A  = 10^(gainDb / 40)
//   α  = sin(w0) / (2 Q)
//   b0 = 1 + α A,   b1 = -2 cos(w0),   b2 = 1 - α A
//   a0 = 1 + α / A, a1 = -2 cos(w0),   a2 = 1 - α / A
//   normalize all by a0
// =============================================================================
struct ParametricBand
{
    void prepare (float sampleRate) noexcept
    {
        sampleRate_ = std::max (sampleRate, 8000.0f);
        designUnity();
        reset();
    }

    void reset() noexcept
    {
        x1L_ = x2L_ = y1L_ = y2L_ = 0.0f;
        x1R_ = x2R_ = y1R_ = y2R_ = 0.0f;
    }

    // freqHz: corner; qFactor: bandwidth (higher Q = narrower); gainDb: boost/cut
    // gainDb == 0 → unity-coefficient design → bit-identical bypass.
    void setBand (float freqHz, float qFactor, float gainDb) noexcept
    {
        const float clampedDb = std::clamp (gainDb, -24.0f, 24.0f);
        if (std::fabs (clampedDb) < 1.0e-6f)
        {
            designUnity();
            return;
        }
        const float fc = std::clamp (freqHz, 20.0f, sampleRate_ * 0.49f);
        const float Q  = std::max (qFactor, 0.10f);

        const float kTwoPi = 6.283185307179586f;
        const float w0    = kTwoPi * fc / sampleRate_;
        const float cosw  = std::cos (w0);
        const float sinw  = std::sin (w0);
        const float A     = std::pow (10.0f, clampedDb / 40.0f);
        const float alpha = sinw / (2.0f * Q);

        const float a0   = 1.0f + alpha / A;
        const float inv0 = 1.0f / a0;

        b0_ = (1.0f + alpha * A) * inv0;
        b1_ = (-2.0f * cosw)     * inv0;
        b2_ = (1.0f - alpha * A) * inv0;
        a1_ = (-2.0f * cosw)     * inv0;
        a2_ = (1.0f - alpha / A) * inv0;
    }

    inline float processL (float x) noexcept
    {
        const float y = b0_ * x + b1_ * x1L_ + b2_ * x2L_ - a1_ * y1L_ - a2_ * y2L_;
        x2L_ = x1L_; x1L_ = x;
        y2L_ = y1L_; y1L_ = y;
        return y;
    }

    inline float processR (float x) noexcept
    {
        const float y = b0_ * x + b1_ * x1R_ + b2_ * x2R_ - a1_ * y1R_ - a2_ * y2R_;
        x2R_ = x1R_; x1R_ = x;
        y2R_ = y1R_; y1R_ = y;
        return y;
    }

private:
    void designUnity() noexcept
    {
        b0_ = 1.0f; b1_ = 0.0f; b2_ = 0.0f;
        a1_ = 0.0f; a2_ = 0.0f;
    }

    float sampleRate_ = 44100.0f;
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float x1L_ = 0.0f, x2L_ = 0.0f, y1L_ = 0.0f, y2L_ = 0.0f;
    float x1R_ = 0.0f, x2R_ = 0.0f, y1R_ = 0.0f, y2R_ = 0.0f;
};


// =============================================================================
// PostTankEQ — 4-band parametric EQ stage
//
// Series chain of 4 ParametricBands. Sits AFTER the FDN feedback loop's
// Hi Cut Shelf, BEFORE the dry/wet mix matrix. Lets a preset hand-shape an
// inverse-mirror of measured spectral deviations vs reference anchor, with
// surgical Q-resolution the 3-band feedback damping (Bass/Mid/Treble) cannot.
//
// Default state: all 4 bands at gainDb = 0 → ParametricBand designUnity()
// → output == input bit-identically. Presets that don't opt in to the
// post-tank EQ are unaffected.
// =============================================================================
class PostTankEQ
{
public:
    static constexpr int kNumBands = 4;

    void prepare (float sampleRate) noexcept
    {
        for (auto& b : bands_) b.prepare (sampleRate);
    }

    void reset() noexcept
    {
        for (auto& b : bands_) b.reset();
    }

    // gainDb == 0 designs unity coefficients → bit-identical bypass on
    // that band. Setting all 4 bands to gainDb=0 makes the entire stage
    // bypass at sample level.
    void setBand (int index, float freqHz, float qFactor, float gainDb) noexcept
    {
        if (index < 0 || index >= kNumBands) return;
        bands_[index].setBand (freqHz, qFactor, gainDb);
    }

    inline float processL (float x) noexcept
    {
        for (auto& b : bands_) x = b.processL (x);
        return x;
    }

    inline float processR (float x) noexcept
    {
        for (auto& b : bands_) x = b.processR (x);
        return x;
    }

private:
    ParametricBand bands_[kNumBands];
};

// =============================================================================
// HighShelfBand — 2nd-order RBJ high-shelf biquad
//
// Stereo state. Coefficient design offline (prepare / setShelf); audio thread
// runs only the 5-mul/4-add biquad per channel — zero transcendentals.
//
// gainDb = 0 → designs unity coefficients so output equals input bit-
// identically. PostTankBandTrim relies on this guarantee for the all-zero-
// gain bypass case (legacy preset path).
//
// RBJ cookbook (high-shelf):
//   A = 10^(gainDb / 40)
//   w0 = 2π fc / Fs;  cosw = cos(w0);  sinw = sin(w0)
//   alpha = sinw / 2 × sqrt((A + 1/A)(1/S - 1) + 2),  S = 1 (default shelf slope)
//   b0 =    A((A+1) + (A-1)cosw + 2√A · α)
//   b1 = -2A((A-1) + (A+1)cosw)
//   b2 =    A((A+1) + (A-1)cosw − 2√A · α)
//   a0 =    (A+1) − (A-1)cosw + 2√A · α
//   a1 =  2((A-1) − (A+1)cosw)
//   a2 =    (A+1) − (A-1)cosw − 2√A · α
// =============================================================================
struct HighShelfBand
{
    void prepare (float sampleRate) noexcept
    {
        sampleRate_ = std::max (sampleRate, 8000.0f);
        designUnity();
        reset();
    }

    void reset() noexcept
    {
        x1L_ = x2L_ = y1L_ = y2L_ = 0.0f;
        x1R_ = x2R_ = y1R_ = y2R_ = 0.0f;
    }

    // gainDb == 0 → unity-coefficient design → bit-identical bypass.
    void setShelf (float freqHz, float gainDb, float slope = 1.0f) noexcept
    {
        const float clampedDb = std::clamp (gainDb, -24.0f, 24.0f);
        if (std::fabs (clampedDb) < 1.0e-6f)
        {
            designUnity();
            return;
        }
        const float fc = std::clamp (freqHz, 20.0f, sampleRate_ * 0.49f);
        const float S  = std::max (slope, 0.10f);

        const float kTwoPi = 6.283185307179586f;
        const float w0    = kTwoPi * fc / sampleRate_;
        const float cosw  = std::cos (w0);
        const float sinw  = std::sin (w0);
        const float A     = std::pow (10.0f, clampedDb / 40.0f);
        const float Aplus = A + 1.0f;
        const float Aminus = A - 1.0f;
        const float beta  = std::sqrt ((A * A + 1.0f) * (1.0f / S - 1.0f) + 2.0f * A);
        const float alpha = sinw * 0.5f * beta / std::max (A, 1.0e-6f);

        // The RBJ shelf formula simplifies with alpha = sinw/2 × sqrt((A+1/A)(1/S-1)+2);
        // beta above factors out √A. Use the standard form:
        const float two_sqrtA_alpha = 2.0f * std::sqrt (A) * (sinw * 0.5f
                                       * std::sqrt ((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f));

        const float a0 = Aplus - Aminus * cosw + two_sqrtA_alpha;
        const float inv0 = 1.0f / a0;

        b0_ = ( A * (Aplus + Aminus * cosw + two_sqrtA_alpha) ) * inv0;
        b1_ = (-2.0f * A * (Aminus + Aplus * cosw))            * inv0;
        b2_ = ( A * (Aplus + Aminus * cosw - two_sqrtA_alpha) ) * inv0;
        a1_ = ( 2.0f * (Aminus - Aplus * cosw))                * inv0;
        a2_ = ( Aplus - Aminus * cosw - two_sqrtA_alpha)       * inv0;
        (void) alpha;  // alpha kept above for derivation clarity
    }

    inline float processL (float x) noexcept
    {
        const float y = b0_ * x + b1_ * x1L_ + b2_ * x2L_ - a1_ * y1L_ - a2_ * y2L_;
        x2L_ = x1L_; x1L_ = x;
        y2L_ = y1L_; y1L_ = y;
        return y;
    }

    inline float processR (float x) noexcept
    {
        const float y = b0_ * x + b1_ * x1R_ + b2_ * x2R_ - a1_ * y1R_ - a2_ * y2R_;
        x2R_ = x1R_; x1R_ = x;
        y2R_ = y1R_; y1R_ = y;
        return y;
    }

private:
    void designUnity() noexcept
    {
        b0_ = 1.0f; b1_ = 0.0f; b2_ = 0.0f;
        a1_ = 0.0f; a2_ = 0.0f;
    }

    float sampleRate_ = 44100.0f;
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float x1L_ = 0.0f, x2L_ = 0.0f, y1L_ = 0.0f, y2L_ = 0.0f;
    float x1R_ = 0.0f, x2R_ = 0.0f, y1R_ = 0.0f, y2R_ = 0.0f;
};


// =============================================================================
// LowShelfBand — RBJ low-shelf biquad
//
// Mirror of HighShelfBand. Used by VintageTankEngine's 3-band damping matrix
// to give Bass Multiply direct control over the per-loop-pass bass shelf
// gain. gainDb == 0 → unity-coefficient design → bit-identical bypass.
// =============================================================================
struct LowShelfBand
{
    void prepare (float sampleRate) noexcept
    {
        sampleRate_ = std::max (sampleRate, 8000.0f);
        designUnity();
        reset();
    }

    void reset() noexcept
    {
        x1L_ = x2L_ = y1L_ = y2L_ = 0.0f;
        x1R_ = x2R_ = y1R_ = y2R_ = 0.0f;
    }

    void setShelf (float freqHz, float gainDb, float slope = 1.0f) noexcept
    {
        const float clampedDb = std::clamp (gainDb, -24.0f, 24.0f);
        if (std::fabs (clampedDb) < 1.0e-6f)
        {
            designUnity();
            return;
        }
        const float fc = std::clamp (freqHz, 20.0f, sampleRate_ * 0.49f);
        const float S  = std::max (slope, 0.10f);

        const float kTwoPi = 6.283185307179586f;
        const float w0    = kTwoPi * fc / sampleRate_;
        const float cosw  = std::cos (w0);
        const float sinw  = std::sin (w0);
        const float A     = std::pow (10.0f, clampedDb / 40.0f);
        const float Aplus = A + 1.0f;
        const float Aminus = A - 1.0f;
        const float two_sqrtA_alpha = 2.0f * std::sqrt (A) * (sinw * 0.5f
                                       * std::sqrt ((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f));

        const float a0 = Aplus + Aminus * cosw + two_sqrtA_alpha;
        const float inv0 = 1.0f / a0;

        b0_ = ( A * (Aplus - Aminus * cosw + two_sqrtA_alpha) ) * inv0;
        b1_ = ( 2.0f * A * (Aminus - Aplus * cosw))            * inv0;
        b2_ = ( A * (Aplus - Aminus * cosw - two_sqrtA_alpha) ) * inv0;
        a1_ = (-2.0f * (Aminus + Aplus * cosw))                * inv0;
        a2_ = ( Aplus + Aminus * cosw - two_sqrtA_alpha)       * inv0;
    }

    inline float processL (float x) noexcept
    {
        const float y = b0_ * x + b1_ * x1L_ + b2_ * x2L_ - a1_ * y1L_ - a2_ * y2L_;
        x2L_ = x1L_; x1L_ = x;
        y2L_ = y1L_; y1L_ = y;
        return y;
    }

    inline float processR (float x) noexcept
    {
        const float y = b0_ * x + b1_ * x1R_ + b2_ * x2R_ - a1_ * y1R_ - a2_ * y2R_;
        x2R_ = x1R_; x1R_ = x;
        y2R_ = y1R_; y1R_ = y;
        return y;
    }

private:
    void designUnity() noexcept
    {
        b0_ = 1.0f; b1_ = 0.0f; b2_ = 0.0f;
        a1_ = 0.0f; a2_ = 0.0f;
    }

    float sampleRate_ = 44100.0f;
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float x1L_ = 0.0f, x2L_ = 0.0f, y1L_ = 0.0f, y2L_ = 0.0f;
    float x1R_ = 0.0f, x2R_ = 0.0f, y1R_ = 0.0f, y2R_ = 0.0f;
};


// =============================================================================
// PostTankBandTrim — decoupled per-band linear gain post-tank
//
// Acts on 4 contiguous frequency regions defined by 3 fixed crossovers:
//   Sub        ≤ fLow
//   Low-Mid    [fLow, fMid]
//   Mid-High   [fMid, fHi]
//   Air        ≥ fHi
//
// Realized as a cascade of 3 high-shelves where each shelf gain encodes the
// *step* between adjacent region gains plus a final makeup multiplier for the
// air region. Sum-flat to bit-identical when all 4 region gains == 0 dB
// (each HighShelfBand designs unity coefficients at gainDb==0).
//
// Independent of the FDN loop damping — sits AFTER the tank's output, BEFORE
// the dry/wet mix matrix. Lets a preset trim EDT band-shape and late bass
// boom WITHOUT warping the in-loop damping coefficients (which affect both
// decay length AND transient response).
//
// Default state: all 4 region gains = 0 dB → 3 unity-coefficient shelves +
// output multiplier 1.0 → bit-identical bypass.
// =============================================================================
class PostTankBandTrim
{
public:
    static constexpr int kNumRegions = 4;

    void prepare (float sampleRate) noexcept
    {
        sampleRate_ = std::max (sampleRate, 8000.0f);
        for (auto& s : shelves_) s.prepare (sampleRate_);
        recomputeShelves();
    }

    void reset() noexcept
    {
        for (auto& s : shelves_) s.reset();
    }

    // Set the 3 region crossovers (must be strictly ascending).
    void setCrossovers (float fLow, float fMid, float fHi) noexcept
    {
        fLow_ = std::clamp (fLow,  20.0f, sampleRate_ * 0.49f);
        fMid_ = std::clamp (fMid,  fLow_ + 1.0f, sampleRate_ * 0.49f);
        fHi_  = std::clamp (fHi,   fMid_ + 1.0f, sampleRate_ * 0.49f);
        recomputeShelves();
    }

    // Set the linear gain (dB) for region 0..3 (Sub/LowMid/MidHi/Air).
    // Range clamped to [-24, +24] dB by the underlying shelf design.
    void setRegionGainDb (int region, float gainDb) noexcept
    {
        if (region < 0 || region >= kNumRegions) return;
        // Clamp to the same ±24 dB limit the underlying shelves enforce. Region 0
        // flows straight into outGain_ (the base multiplier) in recomputeShelves(),
        // which is NOT clamped by setShelf() — without this, an out-of-range region-0
        // gain would bypass the intended limit.
        regionGainDb_[region] = std::clamp (gainDb, -24.0f, 24.0f);
        recomputeShelves();
    }

    inline float processL (float x) noexcept
    {
        for (auto& s : shelves_) x = s.processL (x);
        return x * outGain_;
    }

    inline float processR (float x) noexcept
    {
        for (auto& s : shelves_) x = s.processR (x);
        return x * outGain_;
    }

private:
    // Cascade math: signal starts at unity. Each high-shelf at f_i with
    // gainDb (G_{i+1} - G_i) lifts everything above f_i by that step.
    //
    // Pre-cascade gain for f ≤ fLow:  base
    // Between fLow and fMid:           base × shelf1 ≈ base × 10^(s1/20)
    // Between fMid and fHi:            base × shelf1 × shelf2
    // Above fHi:                       base × shelf1 × shelf2 × shelf3
    //
    // For region gains (G0..G3) we need:
    //   base                      = 10^(G0/20)
    //   shelf1 (at fLow)  step dB = G1 - G0
    //   shelf2 (at fMid)  step dB = G2 - G1
    //   shelf3 (at fHi)   step dB = G3 - G2
    // The post-cascade gain (above fHi) is built up cumulatively; no separate
    // makeup needed. outGain_ acts as the BASE gain (region 0) so all
    // regions land at their target dB without extra multiplications.
    void recomputeShelves() noexcept
    {
        const float s1 = regionGainDb_[1] - regionGainDb_[0];
        const float s2 = regionGainDb_[2] - regionGainDb_[1];
        const float s3 = regionGainDb_[3] - regionGainDb_[2];
        shelves_[0].setShelf (fLow_, s1);
        shelves_[1].setShelf (fMid_, s2);
        shelves_[2].setShelf (fHi_,  s3);
        outGain_ = std::pow (10.0f, regionGainDb_[0] / 20.0f);
    }

    float sampleRate_ = 44100.0f;
    float fLow_ = 200.0f, fMid_ = 800.0f, fHi_ = 3000.0f;
    float regionGainDb_[kNumRegions] {0.0f, 0.0f, 0.0f, 0.0f};
    float outGain_ = 1.0f;
    HighShelfBand shelves_[3];
};

// =============================================================================
// AttackRamp — envelope-relative per-band gain that sculpts early-decay-time
//
// Tracks the band's signal envelope with a slow-release peak follower
// (trackingPeak_ rises with envelope, decays slowly with rateDecay). At each
// sample, returns a gain in dB scaled by how far the current envelope has
// FALLEN from its tracking peak:
//
//   gainDb = attackDb × (1 − env / trackingPeak)
//
// When the band is at its peak energy: gain = 0 dB (bypass). When the band
// has decayed: gain approaches attackDb. Positive attackDb → boosts the
// band's tail (slows perceived early decay → LONGER EDT). Negative attackDb
// → cuts the band's tail (faster early decay → SHORTER EDT).
//
// tauMs controls the trackingPeak's slow-release time constant — how long
// it takes for the gain to taper back toward 0 dB after the envelope has
// fully decayed. A short tau (50 ms) only shapes the very early tail; a
// long tau (300 ms) shapes the full EDT window.
//
// Default state (attackDb = 0): gain stays at 0 dB regardless of envelope
// state → bit-identical bypass.
// =============================================================================
struct AttackRamp
{
    void prepare (float sampleRate) noexcept
    {
        sampleRate_   = std::max (sampleRate, 8000.0f);
        recomputeCoeffs();
        reset();
    }

    void reset() noexcept
    {
        envFollower_  = 0.0f;
        trackingPeak_ = 0.0f;
        gainSmooth_   = 1.0f;
    }

    void setShape (float attackDb, float tauMs) noexcept
    {
        attackDb_ = std::clamp (attackDb, -24.0f, 24.0f);
        tauMs_    = std::clamp (tauMs, 5.0f, 2000.0f);
        if (std::fabs (attackDb_) < 1.0e-6f)
            gainSmooth_ = 1.0f;   // bypass-equivalent shape → prime the smoothed gain
        recomputeCoeffs();
    }

    inline float tickGain (float absLR) noexcept
    {
        if (std::fabs (attackDb_) < 1.0e-6f)
        {
            gainSmooth_ = 1.0f;   // prime so a later re-enable doesn't slew from a stale gain
            return 1.0f;          // bypass
        }

        // Fast attack, moderate release envelope follower over the band
        // signal magnitude. Gives the trackingPeak something to track.
        // (Slowing this to track the sub-band envelope broke the working
        // low/low_mid bands — the fast follower is required there. Sub <100 Hz
        // edt stays immovable by this shaper: a known limit.)
        // Coeffs are sample-rate-corrected (recomputeCoeffs) so the follower
        // tracks at the same wall-clock speed at any host rate.
        if (absLR > envFollower_)
            envFollower_ += envAttCoeff_ * (absLR - envFollower_);
        else
            envFollower_ += envRelCoeff_ * (absLR - envFollower_);

        // Slow-release peak tracker. Captures the recent loudness peak;
        // decays per sample so the gain modulator re-arms after each burst.
        if (envFollower_ > trackingPeak_)
            trackingPeak_ = envFollower_;
        else
            trackingPeak_ *= peakDecayPerSamp_;

        // Avoid division by zero — when there's no signal yet, gain = 0 dB.
        if (trackingPeak_ < 1.0e-6f)
        {
            gainSmooth_ = 1.0f;   // prime so the first onset doesn't slew from a stale gain
            return 1.0f;
        }

        // Fractional decay: 0 = at peak, 1 = fully decayed (envelope == 0).
        float dec = 1.0f - envFollower_ / trackingPeak_;
        dec = std::clamp (dec, 0.0f, 1.0f);
        // Energy-conserving offset: CUT at/near the sustain peak (dec < kPivot)
        // and BOOST as the band decays (dec > kPivot). A pure +attackDb boost
        // of the decayed tail raised that band's steady-state energy → broke
        // spec_L1/ss; pivoting around kPivot trades sustain level for tail hold
        // so edt lengthens without inflating total band energy. attackDb_ == 0
        // → gainDb == 0 regardless of dec → bit-identical bypass preserved.
        constexpr float kPivot = 0.40f;
        const float gainDb = attackDb_ * (dec - kPivot);
        const float gainTarget = std::pow (10.0f, gainDb / 20.0f);

        // Slew-limit the gain. `dec` ripples at the carrier's 2× rate (the fast
        // envelope follower needed for low-band EDT tracks per-cycle |signal|),
        // so applying gainTarget directly amplitude-modulates the audio →
        // 3rd-harmonic distortion (a sustained 1 kHz tone with a large attackDb
        // produced ~2 % THD — audible grit on Vocal Hall). EDT shaping is a SLOW
        // process (tau 100-300 ms), so a ~10 ms one-pole on the gain rejects the
        // audio-rate ripple (~-40 dB at 2 kHz) while passing the intended slow
        // envelope intact. gainSmooth_ rests at 1.0; the attackDb==0 early-return
        // above never reaches here, so bit-identical bypass is preserved.
        gainSmooth_ += gainSlew_ * (gainTarget - gainSmooth_);
        return gainSmooth_;
    }

private:
    void recomputeCoeffs() noexcept
    {
        peakDecayPerSamp_ = std::exp (-1.0f / std::max (tauMs_ * 0.001f * sampleRate_, 1.0f));
        // Envelope-follower attack/release as sample-rate-independent time
        // constants. The ms values are chosen to reproduce the legacy fixed
        // per-sample steps (0.05 attack / 0.005 release) at 48 kHz — the preset
        // calibration rate — so 48 k renders are bit-identical and only other
        // host rates change (correctly, toward SR-independence).
        constexpr float kEnvAttackMs  = 0.4062f;   // → 0.05 step at 48 kHz
        constexpr float kEnvReleaseMs = 4.156f;    // → 0.005 step at 48 kHz
        envAttCoeff_ = 1.0f - std::exp (-1.0f / std::max (kEnvAttackMs  * 0.001f * sampleRate_, 1.0f));
        envRelCoeff_ = 1.0f - std::exp (-1.0f / std::max (kEnvReleaseMs * 0.001f * sampleRate_, 1.0f));
        // ~3 ms gain slew (cutoff ~53 Hz): rejects the 2 kHz carrier-rate AM
        // ripple (~-32 dB → kills the 3rd-harmonic distortion) while staying
        // well inside the EDT shaping window (tau 100-300 ms) so the early-decay
        // sculpt is preserved. Swept: 10 ms over-smoothed (neutralized shaping,
        // n_fail 16); 2 ms too fast (n_fail 17); 3 ms is the optimum (n_fail 15,
        // THD 0.13 % vs the 2.10 % bug — well under the 0.5 % distortion gate).
        gainSlew_ = 1.0f - std::exp (-1.0f / std::max (0.003f * sampleRate_, 1.0f));
    }

    float sampleRate_       = 44100.0f;
    float attackDb_         = 0.0f;
    float tauMs_            = 100.0f;
    float peakDecayPerSamp_ = 0.99979f;
    float envAttCoeff_      = 0.05f;
    float envRelCoeff_      = 0.005f;
    float envFollower_      = 0.0f;
    float trackingPeak_     = 0.0f;
    float gainSmooth_       = 1.0f;
    float gainSlew_         = 0.0f;     // set by recomputeCoeffs() in prepare()
};


// =============================================================================
// PerBandEDTShape — 4-band time-varying gain via constant-coeff 1-pole split
//
// 4 contiguous regions (Sub / LowMid / MidHi / Air) defined by 3 crossovers.
// Split uses 3 cascaded 1-pole low-pass + complementary high-pass (LP + HP
// where HP = input − LP). Allocation- and transcendental-free on the audio
// thread (all 1-pole α values precomputed at prepare / setCrossovers).
//
// Per region: an AttackRamp produces a time-varying linear gain that
// multiplies that band's signal. Output = sum of 4 gained band signals.
// At all 4 attackDb == 0 → all AttackRamps return 1.0 → sum equals input
// EXACTLY because LP + HP = input by construction → bit-identical bypass.
//
// Decoupled from FDN damping. Sits AFTER PostTankBandTrim and BEFORE the
// dry/wet mix matrix. Shapes the first 10 dB drop per band (EDT) without
// disturbing RT60-region decay rates.
// =============================================================================
class PerBandEDTShape
{
public:
    static constexpr int kNumRegions = 4;

    void prepare (float sampleRate) noexcept
    {
        sampleRate_ = std::max (sampleRate, 8000.0f);
        for (auto& r : rampsL_) r.prepare (sampleRate_);
        for (auto& r : rampsR_) r.prepare (sampleRate_);
        recomputeSplitCoeffs();
        reset();
    }

    void reset() noexcept
    {
        lpStateL_[0] = lpStateL_[1] = lpStateL_[2] = 0.0f;
        lpStateR_[0] = lpStateR_[1] = lpStateR_[2] = 0.0f;
        for (auto& r : rampsL_) r.reset();
        for (auto& r : rampsR_) r.reset();
    }

    void setCrossovers (float fLow, float fMid, float fHi) noexcept
    {
        fLow_ = std::clamp (fLow,  20.0f, sampleRate_ * 0.49f);
        fMid_ = std::clamp (fMid,  fLow_ + 1.0f, sampleRate_ * 0.49f);
        fHi_  = std::clamp (fHi,   fMid_ + 1.0f, sampleRate_ * 0.49f);
        recomputeSplitCoeffs();
    }

    void setRegionShape (int region, float attackDb, float tauMs) noexcept
    {
        if (region < 0 || region >= kNumRegions) return;
        rampsL_[region].setShape (attackDb, tauMs);
        rampsR_[region].setShape (attackDb, tauMs);
        regionAttackDb_[region] = attackDb;
        anyActive_ = false;
        for (auto v : regionAttackDb_)
            if (std::fabs (v) > 1.0e-6f) { anyActive_ = true; break; }
    }

    inline float processL (float x) noexcept
    {
        if (! anyActive_) return x;     // exact bit-identical bypass
        return processChannel (x, lpStateL_, rampsL_);
    }

    inline float processR (float x) noexcept
    {
        if (! anyActive_) return x;     // exact bit-identical bypass
        return processChannel (x, lpStateR_, rampsR_);
    }

private:
    inline float processChannel (float x, float (&state)[3],
                                 AttackRamp (&ramps)[kNumRegions]) noexcept
    {
        // 3 cascaded 1-pole LPs split the input into 4 contiguous bands.
        // Each band is the difference between consecutive LP outputs:
        //   band0 (sub)    = LP1(x)
        //   band1 (lowMid) = LP2(LP1(x))    [actually LP1(x) - LP2(LP1(x)) is the
        //                                     above-fLow-but-below-fMid slice]
        //
        // Convention: lp_k smooths what survives prior LPs. We compute LPs
        // cumulatively then subtract to peel off each band.
        //
        // 1-pole LP: y[n] = α * (x[n] − y[n−1]) + y[n−1] with α = (1 − e^(−2π fc / fs))
        // For HP at the same fc: hp = x − lp.

        // LP1 (fLow)
        state[0] += alpha_[0] * (x - state[0]);
        const float lp1 = state[0];

        // LP2 (fMid) on the HP-of-LP1 path so we split THAT band
        const float hp_after_lp1 = x - lp1;
        state[1] += alpha_[1] * (hp_after_lp1 - state[1]);
        const float lp2_mid = state[1];

        // LP3 (fHi) on the HP-of-LP2 path
        const float hp_after_lp2 = hp_after_lp1 - lp2_mid;
        state[2] += alpha_[2] * (hp_after_lp2 - state[2]);
        const float lp3_midhi = state[2];

        const float bandSub    = lp1;
        const float bandLowMid = lp2_mid;
        const float bandMidHi  = lp3_midhi;
        const float bandAir    = hp_after_lp2 - lp3_midhi;

        // Per-region time-varying gain (1.0 when all attackDb==0 → bypass).
        // Drive each AttackRamp with ITS OWN band magnitude (it is documented
        // to track "the band's signal envelope"); feeding the full-band |x| to
        // all four coupled the regions — a loud sub transient would re-arm the
        // air-band ramp and vice-versa.
        const float gSub    = ramps[0].tickGain (std::fabs (bandSub));
        const float gLowMid = ramps[1].tickGain (std::fabs (bandLowMid));
        const float gMidHi  = ramps[2].tickGain (std::fabs (bandMidHi));
        const float gAir    = ramps[3].tickGain (std::fabs (bandAir));

        return bandSub * gSub + bandLowMid * gLowMid
             + bandMidHi * gMidHi + bandAir * gAir;
    }

    void recomputeSplitCoeffs() noexcept
    {
        const float kTwoPi = 6.283185307179586f;
        alpha_[0] = 1.0f - std::exp (-kTwoPi * fLow_ / sampleRate_);
        alpha_[1] = 1.0f - std::exp (-kTwoPi * fMid_ / sampleRate_);
        alpha_[2] = 1.0f - std::exp (-kTwoPi * fHi_  / sampleRate_);
        for (auto& a : alpha_)
            a = std::clamp (a, 0.0f, 1.0f);
    }

    float sampleRate_ = 44100.0f;
    float fLow_ = 200.0f, fMid_ = 800.0f, fHi_ = 3000.0f;
    float alpha_[3] {0.0f, 0.0f, 0.0f};
    float lpStateL_[3] {0.0f, 0.0f, 0.0f};
    float lpStateR_[3] {0.0f, 0.0f, 0.0f};
    float regionAttackDb_[kNumRegions] {0.0f, 0.0f, 0.0f, 0.0f};
    bool  anyActive_ = false;
    // Per-channel ramp state — the AttackRamp envelope/peak followers are
    // mutated in tickGain(), so L and R must not share them or processing one
    // channel would corrupt the other's gain (order-dependent stereo output).
    AttackRamp rampsL_[kNumRegions];
    AttackRamp rampsR_[kNumRegions];
};

// =============================================================================
// DualTimeConstantBassShelf — per-line 2-pole bass shelf with separate
// fast/slow envelope-aware time constants
//
// Phase η (2026-05-29). Single 1-pole bass shelf inside the FDN damping
// stage can only scale overall bass decay timeline monotonically — it
// cannot decouple the first 500 ms of bass energy from the late tail.
// User audition verdict on Bright Hall identified this as the "sustained-
// pink early decay rate" architectural ceiling.
//
// Architecture: TWO parallel 1-pole low-shelves with distinct cutoffs and
// gains. The FAST shelf shapes the early transient bass response (steeper
// attenuation, lower cutoff); the SLOW shelf shapes the late-tail sustain
// (gentler attenuation, higher cutoff). Output = (1−mix) × fast + mix × slow,
// where mix is driven by an envelope follower of the band signal: high
// envelope (active input) → fast shelf dominates; low envelope (decay phase)
// → slow shelf dominates.
//
// Default state (fastGainDb = slowGainDb = 0): both shelves design unity
// coefficients via the underlying HighShelfBand bypass guard → output =
// input bit-identically.
// =============================================================================
struct DualTimeConstantBassShelf
{
    void prepare (float sampleRate) noexcept
    {
        sampleRate_  = std::max (sampleRate, 8000.0f);
        recomputeShelves();
        recomputeEnvCoeffs();
        reset();
    }

    void reset() noexcept
    {
        envFollowerL_ = 0.0f;
        envFollowerR_ = 0.0f;
        fastStateL_   = 0.0f;
        fastStateR_   = 0.0f;
        slowStateL_   = 0.0f;
        slowStateR_   = 0.0f;
    }

    // fastFc: cutoff of the fast-acting low-shelf (200..600 Hz typ.)
    // slowFc: cutoff of the slow-acting low-shelf (100..400 Hz typ.)
    // fastGainDb: shelf gain for fast path (negative = attenuation of bass)
    // slowGainDb: shelf gain for slow path
    // transitionMs: envelope time constant — how fast the mix slides from
    //               fast→slow when input level drops (typical 50..300 ms).
    void setShape (float fastFc, float slowFc,
                   float fastGainDb, float slowGainDb,
                   float transitionMs) noexcept
    {
        fastFc_       = std::clamp (fastFc, 20.0f, sampleRate_ * 0.49f);
        slowFc_       = std::clamp (slowFc, 20.0f, sampleRate_ * 0.49f);
        fastGainDb_   = std::clamp (fastGainDb, -24.0f, 24.0f);
        slowGainDb_   = std::clamp (slowGainDb, -24.0f, 24.0f);
        transitionMs_ = std::clamp (transitionMs, 5.0f, 2000.0f);
        anyActive_ = std::fabs (fastGainDb_) > 1.0e-6f
                  || std::fabs (slowGainDb_) > 1.0e-6f;
        recomputeShelves();
        recomputeEnvCoeffs();
    }

    inline float processL (float x) noexcept
    {
        if (! anyActive_) return x;
        return processChannel (x, fastStateL_, slowStateL_, envFollowerL_);
    }

    inline float processR (float x) noexcept
    {
        if (! anyActive_) return x;
        return processChannel (x, fastStateR_, slowStateR_, envFollowerR_);
    }

private:
    // Implementation is a pair of 1-pole low-pass / high-pass band splits
    // that produce a "shelf below fc" effect: output = bass_band × gain +
    // (input − bass_band). Cheap, allocation-free, no transcendentals on
    // the audio thread.
    //
    // 1-pole LP: y[n] = α*(x[n] − y[n−1]) + y[n−1] with α = 1 − e^(−2π fc / fs)
    //   At gain=0 dB, the bass_band × 1.0 + (x − bass_band) = x → bit-
    //   identical bypass. At gain=−6 dB, bass_band scaled by 0.5 → 6 dB
    //   shelf attenuation below fc.
    inline float processChannel (float x, float& fastState, float& slowState,
                                 float& env) noexcept
    {
        // Envelope follower on the band signal — fast attack, slow release.
        // Per-channel (env passed by ref) so L and R don't share/corrupt it.
        const float absX = std::fabs (x);
        if (absX > env)
            env += envAttCoeff_ * (absX - env);
        else
            env += envRelCoeff_ * (absX - env);

        // mix: 0 when envelope is high (use fast shelf), 1 when low (use slow).
        // env normalized against a slow-decaying peak tracker would give true
        // "transient vs sustain" detection; here we use the envelope itself
        // against a fixed threshold — onset-followed signals stay near fast,
        // input-off tails drift toward slow.
        const float thr = envThreshold_;
        const float ratio = (thr > 1.0e-6f) ? (env / thr) : 0.0f;
        const float mix = std::clamp (1.0f - ratio, 0.0f, 1.0f);

        // Fast band LP
        fastState += fastAlpha_ * (x - fastState);
        const float bassFast = fastState * fastGainLin_;
        const float fastOut  = bassFast + (x - fastState);

        // Slow band LP
        slowState += slowAlpha_ * (x - slowState);
        const float bassSlow = slowState * slowGainLin_;
        const float slowOut  = bassSlow + (x - slowState);

        return (1.0f - mix) * fastOut + mix * slowOut;
    }

    void recomputeShelves() noexcept
    {
        const float kTwoPi = 6.283185307179586f;
        fastAlpha_ = 1.0f - std::exp (-kTwoPi * fastFc_ / sampleRate_);
        slowAlpha_ = 1.0f - std::exp (-kTwoPi * slowFc_ / sampleRate_);
        fastGainLin_ = std::pow (10.0f, fastGainDb_ / 20.0f);
        slowGainLin_ = std::pow (10.0f, slowGainDb_ / 20.0f);
    }

    void recomputeEnvCoeffs() noexcept
    {
        envAttCoeff_ = 1.0f - std::exp (-1.0f / (0.005f * sampleRate_));   // 5 ms attack
        envRelCoeff_ = 1.0f - std::exp (-1.0f / (transitionMs_ * 0.001f * sampleRate_));
    }

    float sampleRate_   = 44100.0f;
    float fastFc_       = 400.0f;
    float slowFc_       = 200.0f;
    float fastGainDb_   = 0.0f;
    float slowGainDb_   = 0.0f;
    float transitionMs_ = 100.0f;
    bool  anyActive_    = false;

    float fastAlpha_    = 0.0f;
    float slowAlpha_    = 0.0f;
    float fastGainLin_  = 1.0f;
    float slowGainLin_  = 1.0f;

    float envFollowerL_ = 0.0f;
    float envFollowerR_ = 0.0f;
    float envAttCoeff_  = 0.0f;
    float envRelCoeff_  = 0.0f;
    float envThreshold_ = 0.05f;     // ~-26 dBFS — typical band-signal level
                                      // during sustained-pink input. Below this,
                                      // slow shelf dominates.

    float fastStateL_   = 0.0f;
    float fastStateR_   = 0.0f;
    float slowStateL_   = 0.0f;
    float slowStateR_   = 0.0f;
};

} // namespace DspUtils

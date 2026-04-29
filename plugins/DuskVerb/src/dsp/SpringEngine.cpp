#include "SpringEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr float kTwoPi = 6.283185307179586f;

    // Per-spring base dispersion coefficient *magnitude*. The sign is always
    // negative (chirps with HF arriving later, the canonical 6G15 character).
    // Slight per-spring variation gives the parallel sum its characteristic
    // shimmer instead of a coherent single-spring chirp.
    constexpr float kPerSpringChirpScale[3] = { 0.65f, 0.70f, 0.75f };
}

void SpringEngine::Spring::allocate (int maxSamples)
{
    const int size = DspUtils::nextPowerOf2 (std::max (maxSamples + 8, 64));
    delayBuf.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    writePos = 0;
}

void SpringEngine::Spring::clear()
{
    std::fill (delayBuf.begin(), delayBuf.end(), 0.0f);
    writePos = 0;
    dampState = 0.0f;
    for (auto& ap : dispersionAPs)
        ap.clear();
}

float SpringEngine::Spring::process (float input, float lfoOffset) noexcept
{
    // 1) Dispersion cascade — feed the input through 24 1st-order all-passes.
    //    Each AP rotates phase frequency-dependently; cascading them builds a
    //    quadratic-ish group-delay curve (the chirp).
    float disp = input;
    for (auto& ap : dispersionAPs)
        disp = ap.process (disp, dispersionA);

    // 2) Read from delay line at the modulated position. lfoOffset is in
    //    samples (signed); we use linear interpolation between two reads
    //    for sub-sample positioning.
    const float readPosF = static_cast<float> (writePos)
                         - static_cast<float> (delaySamples)
                         - lfoOffset;
    const int   intIdx   = static_cast<int> (std::floor (readPosF));
    const float frac     = readPosF - static_cast<float> (intIdx);
    const int   idx0     = intIdx & mask;
    const int   idx1     = (intIdx + 1) & mask;
    const float read = delayBuf[static_cast<size_t> (idx0)] * (1.0f - frac)
                     + delayBuf[static_cast<size_t> (idx1)] *         frac;

    // 3) Write input + feedback × read into the buffer (Karplus-Strong style).
    //    Feedback gain scales per-spring so all springs hit the same RT60.
    const float bufIn = disp + feedback * read;
    delayBuf[static_cast<size_t> (writePos)] = bufIn + DspUtils::kDenormalPrevention;
    writePos = (writePos + 1) & mask;

    // 4) HF damping — 1-pole LP on the read path emulates the spring's high-
    //    frequency roll-off (real Fender 6G15 dies above ~4-5 kHz).
    dampState = (1.0f - dampCoeff) * read + dampCoeff * dampState;
    return dampState;
}

void SpringEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    const float rateRatio = static_cast<float> (sampleRate / 44100.0);
    // Worst-case length = base × max-size-scale × rateRatio + LFO headroom.
    constexpr float kMaxSizeScale = 1.6f;     // clamp slightly above the normal 1.5×
    constexpr int   kLfoHeadroom  = 32;       // samples of room for LFO read-position offset

    auto setupSpring = [&] (Spring& s, int baseDelay, float chirpScale)
    {
        const int reserve = static_cast<int> (
            static_cast<float> (baseDelay) * kMaxSizeScale * rateRatio + kLfoHeadroom + 4.0f);
        s.allocate (reserve);
        s.dispersionA = -chirpAmount_ * chirpScale;
    };

    for (int i = 0; i < kNumSprings; ++i)
    {
        setupSpring (leftSprings_ [i], kLeftBaseDelays [i], kPerSpringChirpScale[i]);
        setupSpring (rightSprings_[i], kRightBaseDelays[i], kPerSpringChirpScale[i]);
    }

    // Independent random-walk LFOs per channel — the read-position wobble
    // that gives a real Fender tank its constantly-quivering character.
    lfoL_.prepare (static_cast<float> (sampleRate), 0xC0FFEEu);
    lfoR_.prepare (static_cast<float> (sampleRate), 0xBADBEEFu);

    updateSpringLengths();
    updateFeedback();
    updateDamping();
    updateDispersion();
    updateLFO();

    prepared_ = true;
}

void SpringEngine::clearBuffers()
{
    for (auto& s : leftSprings_)  s.clear();
    for (auto& s : rightSprings_) s.clear();
}

// ============================================================================
// Universal setters — each one updates the cached value, then re-runs the
// affected per-spring update. Cheap (~12 multiplications) so safe to call
// at parameter-change rate.
// ============================================================================

void SpringEngine::setDecayTime (float seconds)
{
    decayTime_ = std::max (0.05f, seconds);
    if (prepared_) updateFeedback();
}

void SpringEngine::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateSpringLengths();
        updateFeedback();          // feedback depends on loop period
    }
}

void SpringEngine::setBassMultiply  (float mult) { bassMult_ = std::clamp (mult, 0.1f, 4.0f); }

// TODO: midMult_ is accepted for cross-engine setter parity but not yet wired
// into the spring tank. Implementing it requires a band-split (using
// crossoverHz_ / highCrossoverHz_) so the mid band can be scaled independently
// of bassMult_/trebleMult_. Until then this is a no-op cache.
void SpringEngine::setMidMultiply   (float mult) { midMult_  = std::clamp (mult, 0.1f, 4.0f); }

void SpringEngine::setTrebleMultiply (float mult)
{
    trebleMult_ = std::clamp (mult, 0.05f, 4.0f);
    if (prepared_) updateDamping();
}

// TODO: crossoverHz_ / highCrossoverHz_ are accepted for cross-engine setter
// parity. The spring tank currently uses a single 1-pole HF damper per spring
// (see updateDamping); proper multi-band damping driven by these crossovers
// would need filters added to Spring::process and corresponding update hooks.
void SpringEngine::setCrossoverFreq     (float hz) { crossoverHz_     = std::clamp (hz,  100.0f,  8000.0f); }
void SpringEngine::setHighCrossoverFreq (float hz) { highCrossoverHz_ = std::clamp (hz, 1000.0f, 12000.0f); }

void SpringEngine::setSaturation (float amount) { saturationAmount_ = std::clamp (amount, 0.0f, 1.0f); }

void SpringEngine::setModDepth (float depth)
{
    modDepthRaw_ = std::clamp (depth, 0.0f, 1.0f);
    if (prepared_) updateLFO();
}

void SpringEngine::setModRate (float hz)
{
    modRateRaw_ = std::clamp (hz, 0.05f, 12.0f);
    if (prepared_) updateLFO();
}

void SpringEngine::setTankDiffusion (float amount)
{
    chirpAmount_ = std::clamp (amount, 0.0f, 1.0f);
    if (prepared_) updateDispersion();
}

void SpringEngine::setFreeze (bool frozen)
{
    frozen_ = frozen;
    if (prepared_) updateFeedback();
}

// ============================================================================
// Update helpers
// ============================================================================

void SpringEngine::updateSpringLengths()
{
    // size 0 → 0.5×, size 1 → 1.5×. Same shape as the other engines'
    // size-scale curve so users get consistent muscle memory.
    const float sizeScale = 0.5f + sizeParam_ * 1.0f;
    const float rateRatio = static_cast<float> (sampleRate_ / 44100.0);

    for (int i = 0; i < kNumSprings; ++i)
    {
        leftSprings_ [i].delaySamples = std::min (
            static_cast<int> (static_cast<float> (kLeftBaseDelays [i]) * sizeScale * rateRatio),
            leftSprings_ [i].mask - 8);
        rightSprings_[i].delaySamples = std::min (
            static_cast<int> (static_cast<float> (kRightBaseDelays[i]) * sizeScale * rateRatio),
            rightSprings_[i].mask - 8);
    }
}

void SpringEngine::updateFeedback()
{
    // Per-spring feedback to hit the requested RT60 across all 3 springs
    // (different lengths → different feedback values for matched decay).
    //   gain^N = 10^(-60/20)   where N = RT60 / loopPeriod
    //   gain   = 10^(-3 × loopPeriod / RT60)
    // Freeze pins feedback at 1.0 for infinite sustain.
    auto computeFb = [this] (int delaySamples) -> float
    {
        if (frozen_) return 1.0f;
        const float loopPeriod = static_cast<float> (delaySamples) / static_cast<float> (sampleRate_);
        const float fb = std::pow (10.0f, -3.0f * loopPeriod / std::max (decayTime_, 0.05f));
        return std::clamp (fb, 0.0f, 0.97f);   // hard cap below 1.0 for stability
    };

    for (int i = 0; i < kNumSprings; ++i)
    {
        leftSprings_ [i].feedback = computeFb (leftSprings_ [i].delaySamples);
        rightSprings_[i].feedback = computeFb (rightSprings_[i].delaySamples);
    }
}

void SpringEngine::updateDamping()
{
    // trebleMult = 1.0  → fc ≈ 5000 Hz (canonical Fender spring rolloff)
    // trebleMult = 0.1  → fc ≈ 500 Hz  (very dark)
    // trebleMult = 1.5  → fc ≈ 7500 Hz (open / bright)
    const float fc = std::clamp (5000.0f * trebleMult_, 200.0f,
                                 0.4f * static_cast<float> (sampleRate_));
    const float coeff = std::exp (-kTwoPi * fc / static_cast<float> (sampleRate_));

    for (auto& s : leftSprings_)  s.dampCoeff = coeff;
    for (auto& s : rightSprings_) s.dampCoeff = coeff;
}

void SpringEngine::updateDispersion()
{
    // chirpAmount 0 → a = 0 (no dispersion → plain delay-with-feedback).
    // chirpAmount 1 → a = -0.85 × per-spring scale (full chirp).
    // Per-spring scale (0.65, 0.70, 0.75) gives each spring a slightly
    // different chirp shape so the summed output has natural variation.
    constexpr float kMaxA = 0.85f;
    for (int i = 0; i < kNumSprings; ++i)
    {
        const float a = -chirpAmount_ * kMaxA * kPerSpringChirpScale[i];
        leftSprings_ [i].dispersionA = a;
        rightSprings_[i].dispersionA = a;
    }
}

void SpringEngine::updateLFO()
{
    // LFO rate range: 0.1 Hz (slow shimmer) to 12 Hz (fast warble).
    // Depth in samples: modDepthRaw 0..1 → 0..6 sample peak excursion.
    // 6 samples ≈ ±0.13 ms at 48k → subtle "drip" without audible pitch shift.
    const float rateHz = modRateRaw_;
    const float depthSamples = modDepthRaw_ * 6.0f;
    lfoL_.setRate (rateHz);
    lfoR_.setRate (rateHz * 1.13f);    // small offset — independent L/R drift
    lfoL_.setDepth (depthSamples);
    lfoR_.setDepth (depthSamples);
}

// ============================================================================
// Process
// ============================================================================

void SpringEngine::process (const float* inL, const float* inR,
                            float* outL, float* outR, int numSamples)
{
    if (! prepared_)
    {
        std::memset (outL, 0, sizeof (float) * static_cast<size_t> (numSamples));
        std::memset (outR, 0, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    // Drive-style soft clip on input — matches the saturation knob on the
    // other engines. Subtle harmonic warmth on hot transients.
    const float satThreshold = 1.0f - saturationAmount_ * 0.6f;
    const float satCeiling   = 2.0f;

    // Energy normalisation across the 3 parallel springs (sum amplitude
    // scales as √N for uncorrelated signals; alternating polarity prevents
    // comb-filtering when all 3 fire at the impulse moment).
    constexpr float kSpringNorm = 0.57735026919f;   // 1/√3
    constexpr float kSpringPolarity[3] = { +1.0f, -1.0f, +1.0f };

    // Bass post-shelf — extremely cheap 1-shelf approximation: just scale
    // the output. Real bass shelving would need a 1-pole LF filter, but
    // for a spring tank a flat low-end multiplier is character-correct
    // (no interaction with the spring's own resonance which lives mid-band).
    const float bassPostGain = bassMult_;

    for (int n = 0; n < numSamples; ++n)
    {
        const float lfoOffsetL = lfoL_.next();
        const float lfoOffsetR = lfoR_.next();

        const float xL = DspUtils::softClip (inL[n], satThreshold, satCeiling);
        const float xR = DspUtils::softClip (inR[n], satThreshold, satCeiling);

        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < kNumSprings; ++i)
        {
            sumL += kSpringPolarity[i] * leftSprings_ [i].process (xL, lfoOffsetL);
            sumR += kSpringPolarity[i] * rightSprings_[i].process (xR, lfoOffsetR);
        }

        outL[n] = sumL * kSpringNorm * bassPostGain;
        outR[n] = sumR * kSpringNorm * bassPostGain;
    }
}

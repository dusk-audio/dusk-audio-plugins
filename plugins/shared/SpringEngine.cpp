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
    fwdDelay.assign (static_cast<size_t> (size), 0.0f);
    bwdDelay.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    writePos = 0;
}

void SpringEngine::Spring::clear()
{
    std::fill (fwdDelay.begin(), fwdDelay.end(), 0.0f);
    std::fill (bwdDelay.begin(), bwdDelay.end(), 0.0f);
    writePos = 0;
    dampState = 0.0f;
    for (auto& ap : dispersionAPs)
        ap.clear();
}

float SpringEngine::Spring::process (float input, float lfoOffset) noexcept
{
    // ---- Bidirectional digital waveguide -------------------------------
    //
    // The spring is modelled as a 1-D waveguide of length lengthSamples
    // carrying two travelling waves. Excitation enters at the left end
    // and adds to the rightward wave; the rightward wave reflects off the
    // right end (with damping + dispersion + sign inversion for clamped
    // physics); that reflection becomes a leftward wave; the leftward
    // wave reflects off the left end (sign inversion) to become a new
    // rightward wave, and so on. Pickup is the post-damping forward
    // wave arriving at the right end.
    //
    //   left end   [ input ]                       right end  [ pickup ]
    //                 │                                          │
    //                 ▼                                          ▼
    //   wRight: ────────────────── fwdDelay (length L) ──────────────► fwdAtRight
    //                                                                  │
    //                                                                  ▼
    //                                                          dispersion + damping
    //                                                                  │
    //                                                                  ▼ (sign-flip × reflR)
    //   wLeft:  bwdAtLeft ◄────── bwdDelay (length L) ───────── bwdNew ◄─
    //                 │
    //                 ▼ (sign-flip × reflL)
    //              fwdNew = -reflLeft × bwdAtLeft + input
    //
    // This gives arrivals at the pickup at L, 3L, 5L… with sign alternation
    // (real clamped-string physics), instead of single-loop's L, 2L, 3L…
    // pattern that collapses two reflections into one round trip.

    // 1) Read arriving waves at the far ends. lfoOffset modulates the
    //    forward-arrival position only — modulating both would double-
    //    track and mute the audible drip.
    const float readPosFwd = static_cast<float> (writePos)
                           - static_cast<float> (lengthSamples)
                           - lfoOffset;
    const int   idxFwd     = static_cast<int> (std::floor (readPosFwd));
    const float fracFwd    = readPosFwd - static_cast<float> (idxFwd);
    const float fwdAtRight = fwdDelay[static_cast<size_t> ( idxFwd      & mask)] * (1.0f - fracFwd)
                           + fwdDelay[static_cast<size_t> ((idxFwd + 1) & mask)] *         fracFwd;

    const int   idxBwd  = (writePos - lengthSamples) & mask;
    const float bwdAtLeft = bwdDelay[static_cast<size_t> (idxBwd)];

    // 2) Right-end junction: apply dispersion + HF damping to the arriving
    //    forward wave, then reflect (sign inversion + magnitude loss) to
    //    become the new leftward wave departing the right end.
    float fwdProcessed = fwdAtRight;
    for (auto& ap : dispersionAPs)
        fwdProcessed = ap.process (fwdProcessed, dispersionA);
    dampState = (1.0f - dampCoeff) * fwdProcessed + dampCoeff * dampState;
    fwdProcessed = dampState;
    const float bwdNewAtRight = -reflRight * fwdProcessed;

    // 3) Left-end junction: arriving backward wave reflects (sign + loss)
    //    and the input is injected into the rightward wave.
    const float fwdNewAtLeft = -reflLeft * bwdAtLeft + input;

    // 4) Write the new wave amplitudes into their respective delay lines.
    fwdDelay[static_cast<size_t> (writePos)] = fwdNewAtLeft   + DspUtils::kDenormalPrevention;
    bwdDelay[static_cast<size_t> (writePos)] = bwdNewAtRight  + DspUtils::kDenormalPrevention;
    writePos = (writePos + 1) & mask;

    // 5) Pickup at the right end — the post-damping forward wave (the
    //    same value that's about to reflect into the bwd line). Returning
    //    fwdProcessed instead of fwdAtRight gives the user the spring's
    //    HF-rolled tone at every arrival.
    return fwdProcessed;
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
        leftSprings_ [i].lengthSamples = std::min (
            static_cast<int> (static_cast<float> (kLeftBaseDelays [i]) * sizeScale * rateRatio),
            leftSprings_ [i].mask - 8);
        rightSprings_[i].lengthSamples = std::min (
            static_cast<int> (static_cast<float> (kRightBaseDelays[i]) * sizeScale * rateRatio),
            rightSprings_[i].mask - 8);
    }
}

void SpringEngine::updateFeedback()
{
    // Per-spring reflection coefficients to hit the requested RT60.
    // For amplitude to decay by 60 dB (factor of 10^-3) over RT60 seconds:
    //   N round trips in RT60 = RT60·sr / (2L)
    //   each round trip applies R² gain (one reflection at each end)
    //   (R²)^N = 10^-3
    //   R = 10^(-3·L / (sr·RT60))
    // Loss is split evenly between the two ends (R_left = R_right) so
    // neither dominates the energy balance. Freeze pins both reflections
    // at 1.0 for infinite sustain.
    auto computeRefl = [this] (int lengthSamples) -> float
    {
        if (frozen_) return 1.0f;
        const float oneWay = static_cast<float> (lengthSamples) / static_cast<float> (sampleRate_);
        const float r = std::pow (10.0f, -3.0f * oneWay / std::max (decayTime_, 0.05f));
        return std::clamp (r, 0.0f, 0.985f); // hard cap below 1.0 for stability
    };

    for (int i = 0; i < kNumSprings; ++i)
    {
        const float rL = computeRefl (leftSprings_ [i].lengthSamples);
        const float rR = computeRefl (rightSprings_[i].lengthSamples);
        leftSprings_ [i].reflLeft  = rL;
        leftSprings_ [i].reflRight = rL;
        rightSprings_[i].reflLeft  = rR;
        rightSprings_[i].reflRight = rR;
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

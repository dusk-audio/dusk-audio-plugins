#include "RingReverb.h"

#include <algorithm>
#include <cmath>

namespace duskverb::dsp
{

// 1-pole LP coefficient: alpha = 1 − exp(−2π · fc / sr).
static inline float computeOnepoleAlpha (float fcHz, double sr)
{
    const float fc = std::max (20.0f, std::min (fcHz, static_cast<float> (sr) * 0.49f));
    const float twoPiFcOverSr = 6.283185307179586f * fc
                              / static_cast<float> (std::max (sr, 1000.0));
    return 1.0f - std::exp (-twoPiFcOverSr);
}

// RT-safe per-channel LCG (Numerical Recipes). Mirrors HallSubTank's
// random-walk generator for consistency across engines.
static inline uint32_t lcgNext (uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}
static inline float lcgSigned (uint32_t& state)
{
    return static_cast<float> (lcgNext (state)) * (2.0f / 4294967296.0f) - 1.0f;
}

// Constexpr static array defs.
constexpr int RingReverb::kPreDiffPrimesL[RingReverb::kPreDiffStages];
constexpr int RingReverb::kPreDiffPrimesR[RingReverb::kPreDiffStages];
constexpr int RingReverb::kRingBaseDelays[RingReverb::kRingStages];
constexpr int RingReverb::kRingDelayOffsetR[RingReverb::kRingStages];
constexpr int RingReverb::kEmbeddedPrimes[RingReverb::kRingStages][RingReverb::kEmbeddedStages];

void RingReverb::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = maxBlockSize;
    const float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
    constexpr float kMaxSizeScale = 2.0f;
    constexpr int   kHeadroom     = 128;

    // ─── Allocate pre-diffuser buffers ────────────────────────────
    for (int s = 0; s < kPreDiffStages; ++s)
    {
        for (int side = 0; side < 2; ++side)
        {
            PreDiffStage& ps = (side == 0) ? preDiffL_[s] : preDiffR_[s];
            const int basePrime = (side == 0) ? kPreDiffPrimesL[s] : kPreDiffPrimesR[s];
            // Pre-diff capped at spreadMult max 2.0× to size buffer.
            const int target = static_cast<int> (std::ceil (
                static_cast<float> (basePrime) * rateRatio * kMaxSizeScale)) + 4;
            const int bufSize = ::DspUtils::nextPowerOf2 (std::max (target, 16));
            ps.buf.assign (static_cast<size_t> (bufSize), 0.0f);
            ps.mask     = bufSize - 1;
            ps.writePos = 0;
            ps.delaySamples = static_cast<int> (std::round (
                static_cast<float> (basePrime) * rateRatio));
            if (ps.delaySamples >= bufSize) ps.delaySamples = bufSize - 1;
        }
    }

    // ─── Allocate ring stage buffers ──────────────────────────────
    for (int i = 0; i < kRingStages; ++i)
    {
        for (int side = 0; side < 2; ++side)
        {
            RingStage& rs = (side == 0) ? stagesL_[i] : stagesR_[i];
            const int baseDelay = kRingBaseDelays[i]
                                + (side == 1 ? kRingDelayOffsetR[i] : 0);
            // Main delay buffer — sized for max size scale + mod headroom.
            const int target = static_cast<int> (std::ceil (
                static_cast<float> (baseDelay) * rateRatio * kMaxSizeScale))
                + kHeadroom;
            const int bufSize = ::DspUtils::nextPowerOf2 (std::max (target, 16));
            rs.buf.assign (static_cast<size_t> (bufSize), 0.0f);
            rs.mask     = bufSize - 1;
            rs.writePos = 0;
            rs.baseDelaySamples = static_cast<int> (std::round (
                static_cast<float> (baseDelay) * rateRatio));
            if (rs.baseDelaySamples >= bufSize - kHeadroom)
                rs.baseDelaySamples = bufSize - kHeadroom - 1;

            // Embedded AP chain — 3 stages per ring stage.
            for (int e = 0; e < kEmbeddedStages; ++e)
            {
                const int apPrime = kEmbeddedPrimes[i][e];
                const int apLen = static_cast<int> (std::ceil (
                    static_cast<float> (apPrime) * rateRatio)) + 4;
                const int apBufSize = ::DspUtils::nextPowerOf2 (
                    std::max (apLen, 16));
                auto& es = rs.embeddedDiff.stages[e];
                es.buf.assign (static_cast<size_t> (apBufSize), 0.0f);
                es.mask     = apBufSize - 1;
                es.writePos = 0;
                es.delaySamples = static_cast<int> (std::round (
                    static_cast<float> (apPrime) * rateRatio));
                if (es.delaySamples >= apBufSize)
                    es.delaySamples = apBufSize - 1;
            }

            // Deterministic per-stage LFO seed — mixes stage index with
            // side (L=0, R=1) so the 12 LFOs (6 stages × 2 sides) are
            // mutually decorrelated yet reproducible across renders.
            rs.rwSeed = (static_cast<uint32_t> (i)    * 2654435761u)
                      ^ (static_cast<uint32_t> (side) * 1597334677u)
                      ^ 0xC0FFEE15u;
            if (rs.rwSeed == 0u) rs.rwSeed = 1u;
            rs.rwState = 0.0f;
            rs.dampState = 0.0f;
        }
    }

    prepared_ = true;
    recomputeRingDelays();
    recomputeStageGains();
    recomputeLFORates();
    recomputeDamping();
    recomputeOutputTaps();
}

void RingReverb::clearBuffers()
{
    for (int s = 0; s < kPreDiffStages; ++s)
    {
        preDiffL_[s].clear();
        preDiffR_[s].clear();
    }
    for (int i = 0; i < kRingStages; ++i)
    {
        stagesL_[i].clear();
        stagesR_[i].clear();
    }
    ringFeedbackL_ = 0.0f;
    ringFeedbackR_ = 0.0f;
}

void RingReverb::recomputeRingDelays()
{
    if (! prepared_) return;
    const float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    constexpr int kHeadroom = 128;
    for (int i = 0; i < kRingStages; ++i)
    {
        for (int side = 0; side < 2; ++side)
        {
            RingStage& rs = (side == 0) ? stagesL_[i] : stagesR_[i];
            const int baseDelay = kRingBaseDelays[i]
                                + (side == 1 ? kRingDelayOffsetR[i] : 0);
            int d = static_cast<int> (std::round (
                static_cast<float> (baseDelay) * rateRatio * sizeScale_));
            if (d < 4) d = 4;
            const int cap = static_cast<int> (rs.buf.size()) - kHeadroom;
            if (d > cap) d = cap;
            rs.baseDelaySamples = d;
        }
    }
}

void RingReverb::recomputeStageGains()
{
    if (! prepared_) return;
    if (frozen_)
    {
        for (int i = 0; i < kRingStages; ++i)
            stagesL_[i].stageGain = stagesR_[i].stageGain = 1.0f;
        return;
    }
    // Griesinger formula:
    //   total_loop_time = sum(baseDelaySamples) / sr
    //   aggregate_loop_gain = 10^(-3 · loop_time / RT60)
    //   per_stage_gain = aggregate^(1/N)
    int totalSamples = 0;
    for (int i = 0; i < kRingStages; ++i)
        totalSamples += stagesL_[i].baseDelaySamples;
    const float loopSec = static_cast<float> (totalSamples)
                        / static_cast<float> (sampleRate_);
    const float decayClamped = std::max (decayTime_, 0.05f);
    const float aggregateGain = std::pow (10.0f, -3.0f * loopSec / decayClamped);
    float stageGain = std::pow (aggregateGain, 1.0f / static_cast<float> (kRingStages));
    if (stageGain > 0.999f) stageGain = 0.999f;
    for (int i = 0; i < kRingStages; ++i)
    {
        stagesL_[i].stageGain = stageGain;
        stagesR_[i].stageGain = stageGain;
    }
}

void RingReverb::recomputeLFORates()
{
    if (! prepared_) return;
    const float baseAlpha = computeOnepoleAlpha (spinHz_, sampleRate_);
    // Per-stage scatter [0.85, 1.15] via deterministic LCG using stage
    // index — mirrors the HallSubTank P13 approach. Independent rates
    // mean the 6 ring LFOs never lock in phase together.
    for (int i = 0; i < kRingStages; ++i)
    {
        for (int side = 0; side < 2; ++side)
        {
            RingStage& rs = (side == 0) ? stagesL_[i] : stagesR_[i];
            uint32_t seed = (static_cast<uint32_t> (i)    * 3266489917u)
                          ^ (static_cast<uint32_t> (side) * 374761393u)
                          ^ 0xDEADBEEFu;
            if (seed == 0u) seed = 1u;
            const float xi = static_cast<float> (lcgNext (seed))
                           * (1.0f / 4294967296.0f);
            const float scatter = 0.85f + 0.30f * xi;
            // Alpha scales near-linearly with rate at small angles.
            rs.rwAlpha = std::min (0.99f, baseAlpha * scatter);
        }
    }
}

void RingReverb::recomputeDamping()
{
    if (! prepared_) return;
    const float alpha = computeOnepoleAlpha (dampingFcHz_, sampleRate_);
    const float mix   = std::clamp (dampingMix_, 0.0f, 0.95f);
    for (int i = 0; i < kRingStages; ++i)
    {
        stagesL_[i].dampAlpha = alpha;
        stagesL_[i].dampMix   = mix;
        stagesR_[i].dampAlpha = alpha;
        stagesR_[i].dampMix   = mix;
    }
}

void RingReverb::recomputePreDiffStages()
{
    if (! prepared_) return;
    const float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    for (int s = 0; s < kPreDiffStages; ++s)
    {
        for (int side = 0; side < 2; ++side)
        {
            PreDiffStage& ps = (side == 0) ? preDiffL_[s] : preDiffR_[s];
            const int basePrime = (side == 0) ? kPreDiffPrimesL[s]
                                              : kPreDiffPrimesR[s];
            int d = static_cast<int> (std::round (
                static_cast<float> (basePrime) * rateRatio * spreadMult_));
            if (d < 2) d = 2;
            const int cap = static_cast<int> (ps.buf.size()) - 2;
            if (d > cap) d = cap;
            ps.delaySamples = d;
        }
    }
}

void RingReverb::recomputeOutputTaps()
{
    // Signed multi-tap output — L taps even stages with alternating signs,
    // R taps odd stages with alternating signs. Same trick that keeps
    // HallSubTank's L/R outputs decorrelated by construction.
    static constexpr float kTapPattern[kRingStages] = {
        +1.0f, -1.0f, +1.0f, -1.0f, +1.0f, -1.0f
    };
    for (int i = 0; i < kRingStages; ++i)
    {
        stagesL_[i].outTapL = (i % 2 == 0) ? kTapPattern[i] : 0.0f;
        stagesL_[i].outTapR = (i % 2 == 1) ? kTapPattern[i] : 0.0f;
        stagesR_[i].outTapL = (i % 2 == 1) ? kTapPattern[i] : 0.0f;
        stagesR_[i].outTapR = (i % 2 == 0) ? kTapPattern[i] : 0.0f;
    }
}

void RingReverb::process (const float* inL, const float* inR,
                          float* outL, float* outR, int numSamples)
{
    if (! prepared_ || numSamples <= 0) return;

    // Hoist params out of the per-sample loop.
    const float shape = std::clamp (shapeCoeff_, 0.0f, 0.85f);
    const float embG  = 0.55f;   // embedded AP coefficient (fixed for now)
    const float wander = wanderSamples_;
    const float width  = stereoWidth_;
    // Cross-coupling: width = +1 → straight L→L, R→R.
    //                 width =  0 → mono sum to both.
    //                 width = -1 → swap L↔R fully (max decorrelation).
    const float selfGain  = 0.5f + 0.5f * width;     // 0..1
    const float crossGain = 0.5f - 0.5f * width;     // 0..1
    // Output normalization: 6 stages × ~1 magnitude / sqrt(6) ≈ 0.408
    const float outNorm = 0.408248f;

    for (int n = 0; n < numSamples; ++n)
    {
        // ── Pre-diffuser (6 stages, L + R independent) ──
        float xL = inL[n];
        float xR = inR[n];
        for (int s = 0; s < kPreDiffStages; ++s)
        {
            xL = preDiffL_[s].process (xL, shape);
            xR = preDiffR_[s].process (xR, shape);
        }

        // ── Ring inject: pre-diffused input + cross-coupled feedback ──
        float sigL = xL + selfGain  * ringFeedbackL_
                        + crossGain * ringFeedbackR_;
        float sigR = xR + selfGain  * ringFeedbackR_
                        + crossGain * ringFeedbackL_;

        float outAccumL = 0.0f, outAccumR = 0.0f;

        // ── Traverse the L ring + R ring ──
        for (int i = 0; i < kRingStages; ++i)
        {
            // ===== L ring stage i =====
            // Single-feedback ring math: each stage buffer holds the input
            // signal from the previous stage only. Stage applies delay +
            // damping + AP chain + per-stage attenuation. NO internal
            // recirculation — energy circulates ONCE per sample around the
            // 6-stage ring via stage 5 → ringFeedbackL_/R_ → stage 0 on
            // the next sample. Per-stage gain (product across 6 = aggregate
            // ring loop gain) implements the Griesinger RT60 formula.
            {
                RingStage& rs = stagesL_[i];
                rs.rwState = (1.0f - rs.rwAlpha) * rs.rwState
                           +         rs.rwAlpha  * lcgSigned (rs.rwSeed);
                // Write THIS sample's input from previous stage into the
                // delay buffer BEFORE reading (so a stage with delay = 0
                // would pass through; with delay > 0, we read older input).
                rs.buf[static_cast<size_t> (rs.writePos)] = sigL;
                // Read delayed tap (linear interp, modulated).
                const float modOffset = wander * rs.rwState;
                const float readPos = static_cast<float> (rs.baseDelaySamples)
                                    + modOffset;
                const int readInt = static_cast<int> (readPos);
                const float readFrac = readPos - static_cast<float> (readInt);
                const int ia = (rs.writePos - readInt)     & rs.mask;
                const int ib = (rs.writePos - readInt - 1) & rs.mask;
                const float a = rs.buf[static_cast<size_t> (ia)];
                const float b = rs.buf[static_cast<size_t> (ib)];
                float delayed = a + readFrac * (b - a);
                rs.writePos = (rs.writePos + 1) & rs.mask;
                // Damping + embedded AP chain + per-stage gain.
                rs.dampState = rs.dampAlpha * delayed
                             + (1.0f - rs.dampAlpha) * rs.dampState;
                delayed = (1.0f - rs.dampMix) * delayed
                        +         rs.dampMix  * rs.dampState;
                delayed = rs.embeddedDiff.process (delayed, embG);
                delayed *= rs.stageGain;
                if (delayed >  kSafetyClip) delayed =  kSafetyClip;
                if (delayed < -kSafetyClip) delayed = -kSafetyClip;
                outAccumL += delayed * rs.outTapL;
                outAccumR += delayed * rs.outTapR;
                sigL = delayed;
            }
            // ===== R ring stage i =====
            {
                RingStage& rs = stagesR_[i];
                rs.rwState = (1.0f - rs.rwAlpha) * rs.rwState
                           +         rs.rwAlpha  * lcgSigned (rs.rwSeed);
                rs.buf[static_cast<size_t> (rs.writePos)] = sigR;
                const float modOffset = wander * rs.rwState;
                const float readPos = static_cast<float> (rs.baseDelaySamples)
                                    + modOffset;
                const int readInt = static_cast<int> (readPos);
                const float readFrac = readPos - static_cast<float> (readInt);
                const int ia = (rs.writePos - readInt)     & rs.mask;
                const int ib = (rs.writePos - readInt - 1) & rs.mask;
                const float a = rs.buf[static_cast<size_t> (ia)];
                const float b = rs.buf[static_cast<size_t> (ib)];
                float delayed = a + readFrac * (b - a);
                rs.writePos = (rs.writePos + 1) & rs.mask;
                rs.dampState = rs.dampAlpha * delayed
                             + (1.0f - rs.dampAlpha) * rs.dampState;
                delayed = (1.0f - rs.dampMix) * delayed
                        +         rs.dampMix  * rs.dampState;
                delayed = rs.embeddedDiff.process (delayed, embG);
                delayed *= rs.stageGain;
                if (delayed >  kSafetyClip) delayed =  kSafetyClip;
                if (delayed < -kSafetyClip) delayed = -kSafetyClip;
                outAccumL += delayed * rs.outTapL;
                outAccumR += delayed * rs.outTapR;
                sigR = delayed;
            }
        }

        // ── Final stage output feeds back into next sample's stage 0 ──
        ringFeedbackL_ = sigL;
        ringFeedbackR_ = sigR;

        outL[n] = outAccumL * outNorm;
        outR[n] = outAccumR * outNorm;
    }
}

// ─── Setters ──────────────────────────────────────────────────────

void RingReverb::setDecayTime (float seconds)
{
    decayTime_ = std::max (0.05f, seconds);
    recomputeStageGains();
}

void RingReverb::setSize (float scale)
{
    sizeScale_ = std::clamp (scale, 0.5f, 2.0f);
    recomputeRingDelays();
    recomputeStageGains();
}

void RingReverb::setDamping (float coeff)
{
    dampingMix_ = std::clamp (coeff, 0.0f, 0.95f);
    recomputeDamping();
}

void RingReverb::setDampingFc (float hz)
{
    dampingFcHz_ = std::clamp (hz, 100.0f, 20000.0f);
    recomputeDamping();
}

void RingReverb::setSpread (float multiplier)
{
    spreadMult_ = std::clamp (multiplier, 0.5f, 2.0f);
    recomputePreDiffStages();
}

void RingReverb::setShape (float coeff)
{
    shapeCoeff_ = std::clamp (coeff, 0.0f, 0.85f);
}

void RingReverb::setSpin (float hz)
{
    spinHz_ = std::clamp (hz, 0.01f, 20.0f);
    recomputeLFORates();
}

void RingReverb::setWander (float samples)
{
    wanderSamples_ = std::max (0.0f, samples);
}

void RingReverb::setStereoWidth (float w)
{
    stereoWidth_ = std::clamp (w, -1.0f, 1.0f);
}

void RingReverb::setFreeze (bool frozen)
{
    if (frozen == frozen_) return;
    frozen_ = frozen;
    recomputeStageGains();
}

} // namespace duskverb::dsp

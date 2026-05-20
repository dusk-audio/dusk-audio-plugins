#include "HybridHallReverb.h"

#include <algorithm>
#include <cmath>

namespace duskverb::dsp
{

// Constexpr static array out-of-line defs.
constexpr float HybridHallReverb::kERTapTimesMs[HybridHallReverb::kNumERTaps];
constexpr float HybridHallReverb::kERSignL[HybridHallReverb::kNumERTaps];
constexpr float HybridHallReverb::kERSignR[HybridHallReverb::kNumERTaps];
constexpr int   HybridHallReverb::kPreDiffPrimesL[HybridHallReverb::kPreDiffStages];
constexpr int   HybridHallReverb::kPreDiffPrimesR[HybridHallReverb::kPreDiffStages];

// ─── ERTDL ──────────────────────────────────────────────────────────

void HybridHallReverb::ERTDL::prepare (double sr)
{
    // Buffer sized for max tap (9.79 ms @ sr) × 1.5 safety + powerOf2.
    const float maxTapMs = kERTapTimesMs[kNumERTaps - 1] * 1.5f;
    const int targetLen = static_cast<int> (std::ceil (
        maxTapMs * 0.001f * static_cast<float> (sr))) + 8;
    const int bufSize = ::DspUtils::nextPowerOf2 (std::max (targetLen, 32));
    bufL.assign (static_cast<size_t> (bufSize), 0.0f);
    bufR.assign (static_cast<size_t> (bufSize), 0.0f);
    mask     = bufSize - 1;
    writePos = 0;
    for (int t = 0; t < kNumERTaps; ++t)
    {
        const int samples = static_cast<int> (std::round (
            kERTapTimesMs[t] * 0.001f * static_cast<float> (sr)));
        tapSamples[t] = std::min (samples, bufSize - 1);
    }
}

void HybridHallReverb::ERTDL::clear()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    writePos = 0;
}

void HybridHallReverb::ERTDL::process (float inL, float inR,
                                        float& outL, float& outR)
{
    // Write input first so tap 0 (delaySamples=0) reads back the current
    // sample — that gives the t=0 peak the metric expects.
    bufL[static_cast<size_t> (writePos)] = inL;
    bufR[static_cast<size_t> (writePos)] = inR;

    float accumL = 0.0f, accumR = 0.0f;
    for (int t = 0; t < kNumERTaps; ++t)
    {
        const int r = (writePos - tapSamples[t]) & mask;
        const float sL = bufL[static_cast<size_t> (r)];
        const float sR = bufR[static_cast<size_t> (r)];
        accumL += weights[t] * kERSignL[t] * sL;
        accumR += weights[t] * kERSignR[t] * sR;
    }

    writePos = (writePos + 1) & mask;
    outL = accumL;
    outR = accumR;
}

// ─── HybridHallReverb ───────────────────────────────────────────────

void HybridHallReverb::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = maxBlockSize;
    const float rateRatio = static_cast<float> (sampleRate / 44100.0);

    // ER TDL
    erTDL_.prepare (sampleRate);

    // Pre-diffuser stages — independent L + R.
    for (int s = 0; s < kPreDiffStages; ++s)
    {
        for (int side = 0; side < 2; ++side)
        {
            PreDiffStage& ps = (side == 0) ? preDiffL_[s] : preDiffR_[s];
            const int basePrime = (side == 0) ? kPreDiffPrimesL[s]
                                              : kPreDiffPrimesR[s];
            const int target = static_cast<int> (std::ceil (
                static_cast<float> (basePrime) * rateRatio)) + 4;
            const int bufSize = ::DspUtils::nextPowerOf2 (std::max (target, 16));
            ps.buf.assign (static_cast<size_t> (bufSize), 0.0f);
            ps.mask         = bufSize - 1;
            ps.writePos     = 0;
            ps.delaySamples = static_cast<int> (std::round (
                static_cast<float> (basePrime) * rateRatio));
            if (ps.delaySamples >= bufSize) ps.delaySamples = bufSize - 1;
        }
    }

    // Ring engine. CRITICAL: ring's INTERNAL 6-stage Schroeder preDiff
    // is bypassed (shape=0 → AP coefficient = 0 → identity passthrough)
    // because it would produce impulse spikes at the preDiff primes
    // (1.5-25 ms) that mask the ER taps and break peak_locations_ms.
    // Pre-diffusion in Hybrid is delegated to the ER path itself (the
    // tap pattern + signs IS the early-density network); ring is fed
    // raw input so its tail comes from clean late stages only.
    ring_.prepare (sampleRate, maxBlockSize);
    ring_.setShape (0.0f);

    // Shelves designed at current gain/fc.
    recomputeShelves();
    lowShelfL_.reset();  lowShelfR_.reset();
    highShelfL_.reset(); highShelfR_.reset();

    // Scratch buffers
    const size_t sz = static_cast<size_t> (maxBlockSize);
    erOutL_.assign  (sz, 0.0f); erOutR_.assign  (sz, 0.0f);
    ringInL_.assign (sz, 0.0f); ringInR_.assign (sz, 0.0f);
    ringOutL_.assign(sz, 0.0f); ringOutR_.assign(sz, 0.0f);

    prepared_ = true;
}

void HybridHallReverb::clearBuffers()
{
    erTDL_.clear();
    for (int s = 0; s < kPreDiffStages; ++s)
    {
        preDiffL_[s].clear();
        preDiffR_[s].clear();
    }
    ring_.clearBuffers();
    lowShelfL_.reset();  lowShelfR_.reset();
    highShelfL_.reset(); highShelfR_.reset();
}

void HybridHallReverb::recomputeShelves()
{
    const float sr = static_cast<float> (sampleRate_);
    lowShelfL_.designHighShelf (lowShelfFc_,
        // Negate gain so positive lowShelfGainDb_ boosts LOWS:
        // high-shelf with negative gain attenuates highs (equivalently
        // boosts lows by the same amount relative to flat). Keeps a
        // single biquad type for both shelves.
        -lowShelfGainDb_, sr);
    lowShelfR_.designHighShelf (lowShelfFc_, -lowShelfGainDb_, sr);
    highShelfL_.designHighShelf (highShelfFc_, highShelfGainDb_, sr);
    highShelfR_.designHighShelf (highShelfFc_, highShelfGainDb_, sr);
}

void HybridHallReverb::process (const float* inL, const float* inR,
                                 float* outL, float* outR, int numSamples)
{
    if (! prepared_ || numSamples <= 0) return;
    const int n = std::min (numSamples, maxBlockSize_);

    // 1. ER TDL — capture 4-tap weighted impulse cluster for early peaks.
    for (int i = 0; i < n; ++i)
    {
        float eL, eR;
        erTDL_.process (inL[i], inR[i], eL, eR);
        erOutL_[i] = eL * erLevel_;
        erOutR_[i] = eR * erLevel_;
    }

    // 2. Ring input = raw input (no HybridHall-side preDiff). The
    //    RingReverb already runs its own internal 6-stage Schroeder
    //    pre-diffuser before the ring stages — cascading a second one
    //    here would smear the impulse so heavily that the ER taps get
    //    buried by allpass impulse peaks, breaking peak_locations.
    for (int i = 0; i < n; ++i)
    {
        ringInL_[i] = inL[i];
        ringInR_[i] = inR[i];
    }

    // 3. Ring processes raw input via its own internal pre-diffuser.
    ring_.process (ringInL_.data(), ringInR_.data(),
                   ringOutL_.data(), ringOutR_.data(), n);

    // 4. Independent ER + Ring level multipliers (P16r2 — replaces the
    //    zero-sum crossfade). er_level lives on the ERTDL.weights via
    //    erLevel_; ring_level multiplies ring output independently.
    //    Post-mix shelves shape the combined output spectrum.
    const float ringGain = ringLevel_;
    for (int i = 0; i < n; ++i)
    {
        float mL = erOutL_[i] + ringOutL_[i] * ringGain;
        float mR = erOutR_[i] + ringOutR_[i] * ringGain;
        mL = highShelfL_.process (lowShelfL_.process (mL));
        mR = highShelfR_.process (lowShelfR_.process (mR));
        outL[i] = mL;
        outR[i] = mR;
    }
}

// ─── Setters ────────────────────────────────────────────────────────

void HybridHallReverb::setDecayTime (float seconds)
{
    ring_.setDecayTime (seconds);
}

void HybridHallReverb::setSize (float scale)
{
    ring_.setSize (scale);
}

void HybridHallReverb::setERTapWeight (int tapIdx, float weight)
{
    if (tapIdx < 0 || tapIdx >= kNumERTaps) return;
    erTDL_.weights[tapIdx] = std::clamp (weight, 0.0f, 4.0f);
}

void HybridHallReverb::setERLevel (float level)
{
    erLevel_ = std::clamp (level, 0.0f, 4.0f);
}

void HybridHallReverb::setRingLevel (float level)
{
    ringLevel_ = std::clamp (level, 0.0f, 4.0f);
}

void HybridHallReverb::setLowShelf (float gainDb, float fcHz)
{
    lowShelfGainDb_ = std::clamp (gainDb, -18.0f, 18.0f);
    lowShelfFc_     = std::clamp (fcHz, 60.0f, 2000.0f);
    if (prepared_) recomputeShelves();
}

void HybridHallReverb::setHighShelf (float gainDb, float fcHz)
{
    highShelfGainDb_ = std::clamp (gainDb, -18.0f, 18.0f);
    highShelfFc_     = std::clamp (fcHz, 1000.0f, 16000.0f);
    if (prepared_) recomputeShelves();
}

void HybridHallReverb::setRingDamping     (float c)   { ring_.setDamping     (c); }
void HybridHallReverb::setRingDampingFc   (float hz)  { ring_.setDampingFc   (hz); }
void HybridHallReverb::setRingSpread      (float m)   { ring_.setSpread      (m); }
void HybridHallReverb::setRingShape       (float c)   { ring_.setShape       (c); }
void HybridHallReverb::setRingSpin        (float hz)  { ring_.setSpin        (hz); }
void HybridHallReverb::setRingWander      (float s)   { ring_.setWander      (s); }
void HybridHallReverb::setRingStereoWidth (float w)   { ring_.setStereoWidth (w); }

void HybridHallReverb::setFreeze (bool frozen)
{
    ring_.setFreeze (frozen);
}

} // namespace duskverb::dsp

#include "HallSubTank.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace duskverb::dsp
{

// 1-pole LP coefficient from cutoff frequency + sample rate.
// alpha = 1 − exp(−2π · fc / sr). Clamped fc to [20 Hz, 0.49·sr] to
// keep numerically stable across sample rates the host might give us.
static inline float computeOnepoleAlpha (float fcHz, double sr)
{
    const float fc = std::max (20.0f, std::min (fcHz, static_cast<float> (sr) * 0.49f));
    const float twoPiFcOverSr = 6.283185307179586f * fc / static_cast<float> (std::max (sr, 1000.0));
    return 1.0f - std::exp (-twoPiFcOverSr);
}

// RT-safe per-channel LCG (Numerical Recipes constants). Returns next
// uint32 and advances the state in place. Used to drive the random-walk
// modulator without juce::Random overhead; deterministic given fixed
// seed at prepare().
static inline uint32_t lcgNext (uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

// Map LCG output to a signed unit float in [-1, +1).
static inline float lcgSigned (uint32_t& state)
{
    return static_cast<float> (lcgNext (state)) * (2.0f / 4294967296.0f) - 1.0f;
}

namespace
{
    // Per-channel LFO phase seeds — distinct fractions of 2π so the 8 channels
    // never line up periodically. Acts on top of the SubTank's
    // bandPhaseOffset_ (the per-band offset HallReverb sets to stagger bass /
    // mid / treble).
    constexpr float kChannelPhaseSeeds[HallSubTank::N] = {
        0.000f, 0.785f, 1.571f, 2.356f, 3.142f, 3.927f, 4.712f, 5.498f
    };

    // 1/√8 — kept file-local so the kernel doesn't depend on a private
    // class constant. Identical numerical value to HallSubTank::kHadamardNorm.
    constexpr float kInternalHadamardNorm = 0.353553390593274f;

    // In-place 8-channel Hadamard transform (1/√8 normalized). Same kernel as
    // FDNReverbSIMDKernels::hadamardInPlace8 — duplicated here as a private
    // static so HallSubTank doesn't depend on the FDN-flavoured SIMD header.
    inline void hadamard8 (float* d)
    {
        for (int i = 0; i < 8; i += 2)
        {
            const float a = d[i], b = d[i + 1];
            d[i] = a + b; d[i + 1] = a - b;
        }
        for (int i = 0; i < 8; i += 4)
        {
            float a0 = d[i], a1 = d[i + 1];
            float b0 = d[i + 2], b1 = d[i + 3];
            d[i]     = a0 + b0; d[i + 1] = a1 + b1;
            d[i + 2] = a0 - b0; d[i + 3] = a1 - b1;
        }
        for (int j = 0; j < 4; ++j)
        {
            const float a = d[j], b = d[j + 4];
            d[j]     = (a + b) * kInternalHadamardNorm;
            d[j + 4] = (a - b) * kInternalHadamardNorm;
        }
    }
}

// Out-of-line definitions for the static constexpr arrays so they have one
// canonical address (C++17 lets the in-class declarations serve as definitions
// for static constexpr int[], but we keep these here to be explicit and avoid
// linker issues across older toolchains).
constexpr int   HallSubTank::kLeftTaps  [HallSubTank::kNumOutputTaps];
constexpr int   HallSubTank::kRightTaps [HallSubTank::kNumOutputTaps];
constexpr float HallSubTank::kLeftSigns [HallSubTank::kNumOutputTaps];
constexpr float HallSubTank::kRightSigns[HallSubTank::kNumOutputTaps];
constexpr int   HallSubTank::kInlineAPDelays[HallSubTank::N];

HallSubTank::HallSubTank()
{
    // Default base delays: small primes in the 200-1000 sample range at 44.1k.
    // Caller should override via prepare() with band-appropriate primes.
    constexpr int kDefaults[N] = { 281, 311, 353, 401, 449, 503, 569, 631 };
    std::memcpy (baseDelays_, kDefaults, sizeof (baseDelays_));
}

void HallSubTank::prepare (double sampleRate, const int* baseDelays8,
                           int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    if (baseDelays8 != nullptr)
        std::memcpy (baseDelays_, baseDelays8, sizeof (baseDelays_));

    // Allocate buffers for max-size scale 2.0× plus modulation excursion
    // headroom. Caller's setSize is clamped to [0.5, 2.0] in HallReverb.
    constexpr float kMaxSizeScale = 2.0f;
    constexpr int   kModHeadroom  = 32;     // samples
    const float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);

    for (int i = 0; i < N; ++i)
    {
        const int maxLen = static_cast<int> (std::ceil (
            static_cast<float> (baseDelays_[i]) * rateRatio * kMaxSizeScale))
            + kModHeadroom;
        const int bufSize = DspUtils::nextPowerOf2 (std::max (maxLen, 16));
        delays_[i].buffer.assign (static_cast<size_t> (bufSize), 0.0f);
        delays_[i].mask     = bufSize - 1;
        delays_[i].writePos = 0;

        // Inline allpass sized for max kInlineAPDelay × sr ratio.
        const int apLen = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays[i]) * rateRatio)) + 4;
        const int apBufSize = DspUtils::nextPowerOf2 (std::max (apLen, 16));
        inlineAP_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP_[i].mask     = apBufSize - 1;
        inlineAP_[i].writePos = 0;
        inlineAP_[i].delaySamples = static_cast<int> (std::round (
            static_cast<float> (kInlineAPDelays[i]) * rateRatio));
        if (inlineAP_[i].delaySamples >= apBufSize)
            inlineAP_[i].delaySamples = apBufSize - 1;

        dampState_[i]   = 0.0f;
        lfoPhase_  [i]  = kChannelPhaseSeeds[i] + bandPhaseOffset_;
        if (lfoPhase_[i] >= kTwoPi) lfoPhase_[i] -= kTwoPi;
        // Deterministic per-channel RNG seed. Mixes channel index with a
        // band-distinguishing salt derived from bandPhaseOffset_ so the
        // three SubTanks (bass / mid / treble) get independent sequences
        // without sharing the same per-channel pattern.
        const uint32_t bandSalt = static_cast<uint32_t> (
            static_cast<int> (bandPhaseOffset_ * 1000.0f) & 0xFFFF);
        rwRngState_[i] = (static_cast<uint32_t> (i) * 2654435761u)
                       ^ (bandSalt * 1597334677u)
                       ^ 0xA5A5A5A5u;
        if (rwRngState_[i] == 0u) rwRngState_[i] = 1u;
        rwState_[i] = 0.0f;
    }

    prepared_ = true;
    recomputeDelayLengths();
    recomputeLFORates();
    recomputeFeedbackGains();
    dampingAlpha_ = computeOnepoleAlpha (dampingFcHz_, sampleRate_);
}

void HallSubTank::clear()
{
    for (int i = 0; i < N; ++i)
    {
        std::fill (delays_[i].buffer.begin(), delays_[i].buffer.end(), 0.0f);
        delays_[i].writePos = 0;
        dampState_[i] = 0.0f;
        inlineAP_[i].clear();
        rwState_[i]   = 0.0f;
    }
}

void HallSubTank::recomputeDelayLengths()
{
    if (! prepared_) return;
    const float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    for (int i = 0; i < N; ++i)
    {
        scaledDelay_[i] = static_cast<int> (std::round (
            static_cast<float> (baseDelays_[i]) * rateRatio * sizeScale_));
        if (scaledDelay_[i] < 4) scaledDelay_[i] = 4;
        // Cap to buffer-size minus modulation headroom; should not trip when
        // sizeScale is clamped at the HallReverb level.
        const int cap = static_cast<int> (delays_[i].buffer.size()) - 32;
        if (scaledDelay_[i] > cap) scaledDelay_[i] = cap;
    }
}

void HallSubTank::recomputeLFORates()
{
    if (! prepared_) return;
    const float inc = kTwoPi * modRateHz_ / static_cast<float> (sampleRate_);
    for (int i = 0; i < N; ++i) lfoPhaseInc_[i] = inc;
    // Random-walk autocorrelation alpha uses the same 1-pole form as the
    // damping LP. modRateHz_ controls how quickly the bounded sequence
    // traverses its range; higher rate = faster mode-smear.
    rwAlpha_ = computeOnepoleAlpha (modRateHz_, sampleRate_);
}

void HallSubTank::recomputeFeedbackGains()
{
    // Per-channel gain g_i = 10^(-3 × loopSeconds / decayTime). Gives an
    // RT60 of decayTime seconds when each channel decays independently.
    // Hadamard mixing redistributes energy across channels, but because the
    // matrix is unitary the overall energy decay rate equals the per-loop
    // gain — RT60 measured at the output tracks decayTime closely (within
    // a few percent depending on damping).
    if (! prepared_) return;
    const float decayClamped = std::max (decayTime_, 0.05f);
    for (int i = 0; i < N; ++i)
    {
        const float loopSec = static_cast<float> (scaledDelay_[i])
                            / static_cast<float> (sampleRate_);
        feedbackGain_[i] = std::pow (10.0f, -3.0f * loopSec / decayClamped);
        if (frozen_) feedbackGain_[i] = 1.0f;
    }
}

void HallSubTank::setDecayTime (float seconds)
{
    decayTime_ = std::max (0.05f, seconds);
    recomputeFeedbackGains();
}

void HallSubTank::setDamping (float amount)
{
    dampingCoeff_ = std::clamp (amount, 0.0f, 0.95f);
}

void HallSubTank::setDampingFc (float hz)
{
    dampingFcHz_ = std::clamp (hz, 100.0f, 20000.0f);
    dampingAlpha_ = computeOnepoleAlpha (dampingFcHz_, sampleRate_);
}

void HallSubTank::setModDepth (float samples)
{
    modDepthSamples_ = std::max (0.0f, samples);
}

void HallSubTank::setModRate (float hz)
{
    modRateHz_ = std::clamp (hz, 0.01f, 20.0f);
    recomputeLFORates();
}

void HallSubTank::setModShape (float shape)
{
    modShape_ = std::clamp (shape, 0.0f, 1.0f);
}

void HallSubTank::setSize (float sizeScale)
{
    sizeScale_ = std::clamp (sizeScale, 0.5f, 2.0f);
    recomputeDelayLengths();
    recomputeFeedbackGains();
}

void HallSubTank::setFreeze (bool frozen)
{
    frozen_ = frozen;
    recomputeFeedbackGains();
}

void HallSubTank::setInlineDiffusion (float coeff)
{
    inlineDiffusion_ = std::clamp (coeff, 0.0f, 0.85f);
}

void HallSubTank::setLFOPhaseOffset (float radians)
{
    bandPhaseOffset_ = std::fmod (radians, kTwoPi);
    if (bandPhaseOffset_ < 0.0f) bandPhaseOffset_ += kTwoPi;
    // Re-seed current phases on next prepare; for live changes (Phase 2 has
    // no exposed parameter for this) just leave running phases untouched —
    // the offset takes effect on the next prepare/clear cycle.
}

void HallSubTank::process (const float* inputL, const float* inputR,
                           float* outputL, float* outputR, int numSamples)
{
    if (! prepared_ || numSamples <= 0)
    {
        if (outputL != nullptr) std::fill_n (outputL, numSamples, 0.0f);
        if (outputR != nullptr) std::fill_n (outputR, numSamples, 0.0f);
        return;
    }

    // Per-channel HF damping: parallel mix of dry channel + 1-pole LP.
    // alpha is the LP coefficient derived from dampingFcHz_; the
    // dampingCoeff_ ("amount") sets the wet/dry mix between dry and LP.
    // Together they form a 1-pole high-shelf with independent cutoff
    // freq + depth controls, fixing the prior implementation where the
    // amount-as-forgetting-factor coupled freq and depth into one knob
    // (Phase 6 centroid_drift fix).
    const float dampMix  = dampingCoeff_;             // [0..0.95]
    const float dampDry  = 1.0f - dampMix;
    const float lpAlpha  = dampingAlpha_;             // from setDampingFc
    const float lpComp   = 1.0f - lpAlpha;
    const float modDepth = modDepthSamples_;
    const float modShape = modShape_;                 // 0=sine, 1=random-walk
    const float sineMix  = 1.0f - modShape;
    const float rwMix    = modShape;
    const float rwA      = rwAlpha_;
    const float rwAComp  = 1.0f - rwA;

    for (int n = 0; n < numSamples; ++n)
    {
        // Mono-summed input distributed equally to all 8 channels. Per-channel
        // input scaling stays uniform so the Hadamard matrix's unitary
        // property carries unchanged from input to output.
        const float inMono = 0.5f * ((inputL ? inputL[n] : 0.0f)
                                    + (inputR ? inputR[n] : 0.0f));

        float channelOut[N];

        // ──── Read each channel's delay tap (with LFO modulation) ──────
        // Modulator is a per-channel blend of deterministic sine and a
        // bounded random-walk (OU-style smoothing of uniform draws). Both
        // outputs are in [-1, +1] before being scaled by modDepth.
        for (int i = 0; i < N; ++i)
        {
            const float sine = std::sin (lfoPhase_[i]);
            lfoPhase_[i] += lfoPhaseInc_[i];
            if (lfoPhase_[i] >= kTwoPi) lfoPhase_[i] -= kTwoPi;

            rwState_[i] = rwAComp * rwState_[i] + rwA * lcgSigned (rwRngState_[i]);
            const float lfo = sineMix * sine + rwMix * rwState_[i];
            const float modOffset = modDepth * lfo;
            const float readPos   = static_cast<float> (scaledDelay_[i]) + modOffset;
            // Linear interpolation between two integer taps. Adequate for
            // sub-audio LFO at our modulation depths (typically < 4 samples).
            const int   readInt   = static_cast<int> (readPos);
            const float readFrac  = readPos - static_cast<float> (readInt);

            const int  mask = delays_[i].mask;
            const int  ia   = (delays_[i].writePos - readInt)     & mask;
            const int  ib   = (delays_[i].writePos - readInt - 1) & mask;
            const float a   = delays_[i].buffer[static_cast<size_t> (ia)];
            const float b   = delays_[i].buffer[static_cast<size_t> (ib)];

            channelOut[i] = a + readFrac * (b - a);
        }

        // ──── Per-channel HF damping: parallel dry + 1-pole LP mix ────
        // lp_state ← α·channel + (1−α)·lp_state  (true LP at fc)
        // channel  ← (1−mix)·channel + mix·lp_state  (high-shelf via mix)
        for (int i = 0; i < N; ++i)
        {
            dampState_[i] = lpAlpha * channelOut[i] + lpComp * dampState_[i];
            channelOut[i] = dampDry * channelOut[i] + dampMix * dampState_[i];
        }

        // ──── Inline Schroeder allpass diffusion (modal smoothing) ─────
        // Single per-channel AP with short prime delay coprime with the
        // main delay-line primes. Adds phase decorrelation across modes
        // without altering RT60 (allpass has unity magnitude response).
        // Bypassed when inlineDiffusion_ ≈ 0 — caller can opt out per band.
        const float apG = inlineDiffusion_;
        if (apG > 1e-4f)
        {
            for (int i = 0; i < N; ++i)
                channelOut[i] = inlineAP_[i].process (channelOut[i], apG);
        }

        // ──── Hadamard cross-mixing (in place, normalized) ─────────────
        hadamard8 (channelOut);

        // ──── Write back to each delay line with feedback gain + input ─
        for (int i = 0; i < N; ++i)
        {
            float v = channelOut[i] * feedbackGain_[i] + inMono;
            if (v >  kSafetyClip) v =  kSafetyClip;
            if (v < -kSafetyClip) v = -kSafetyClip;
            delays_[i].buffer[static_cast<size_t> (delays_[i].writePos)] = v;
            delays_[i].writePos = (delays_[i].writePos + 1) & delays_[i].mask;
        }

        // ──── Output: signed tap routing for stereo decorrelation ──────
        float outL = 0.0f, outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
        {
            outL += channelOut[kLeftTaps [t]] * kLeftSigns [t];
            outR += channelOut[kRightTaps[t]] * kRightSigns[t];
        }
        if (outputL != nullptr) outputL[n] = outL;
        if (outputR != nullptr) outputR[n] = outR;
    }
}

} // namespace duskverb::dsp

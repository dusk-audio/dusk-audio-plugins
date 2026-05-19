#include "HallSubTank.h"

#include <algorithm>
#include <cstring>

namespace duskverb::dsp
{

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

        dampState_[i]   = 0.0f;
        lfoPhase_  [i]  = kChannelPhaseSeeds[i] + bandPhaseOffset_;
        if (lfoPhase_[i] >= kTwoPi) lfoPhase_[i] -= kTwoPi;
    }

    prepared_ = true;
    recomputeDelayLengths();
    recomputeLFORates();
    recomputeFeedbackGains();
}

void HallSubTank::clear()
{
    for (int i = 0; i < N; ++i)
    {
        std::fill (delays_[i].buffer.begin(), delays_[i].buffer.end(), 0.0f);
        delays_[i].writePos = 0;
        dampState_[i] = 0.0f;
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

void HallSubTank::setModDepth (float samples)
{
    modDepthSamples_ = std::max (0.0f, samples);
}

void HallSubTank::setModRate (float hz)
{
    modRateHz_ = std::clamp (hz, 0.01f, 20.0f);
    recomputeLFORates();
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

    // Snapshot of damping coefficient (one-pole high-shelf: y = (1-c)·x + c·y_z1).
    // Larger c = darker (more HF rolled into the integrator state).
    const float damp     = dampingCoeff_;
    const float dampComp = 1.0f - damp;
    const float modDepth = modDepthSamples_;

    for (int n = 0; n < numSamples; ++n)
    {
        // Mono-summed input distributed equally to all 8 channels. Per-channel
        // input scaling stays uniform so the Hadamard matrix's unitary
        // property carries unchanged from input to output.
        const float inMono = 0.5f * ((inputL ? inputL[n] : 0.0f)
                                    + (inputR ? inputR[n] : 0.0f));

        float channelOut[N];

        // ──── Read each channel's delay tap (with LFO modulation) ──────
        for (int i = 0; i < N; ++i)
        {
            const float lfo  = std::sin (lfoPhase_[i]);
            lfoPhase_[i] += lfoPhaseInc_[i];
            if (lfoPhase_[i] >= kTwoPi) lfoPhase_[i] -= kTwoPi;

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

        // ──── Per-channel one-pole high-shelf damping in the feedback ─
        for (int i = 0; i < N; ++i)
        {
            const float y = dampComp * channelOut[i] + damp * dampState_[i];
            dampState_[i] = y;
            channelOut[i] = y;
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

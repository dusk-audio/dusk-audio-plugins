#include "FDNReverb.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// In-place fast Walsh-Hadamard transform for N=16, O(N log N).
// Normalization (1/sqrt(N) = 0.25) is folded into the final butterfly
// stage to eliminate a separate scaling pass.
void hadamardInPlace16 (float* data)
{
    constexpr int n = 16;
    constexpr int kLog2N = 4;

    for (int stage = 0; stage < kLog2N - 1; ++stage)
    {
        int len = 1 << stage;
        for (int i = 0; i < n; i += 2 * len)
        {
            for (int j = 0; j < len; ++j)
            {
                float a = data[i + j];
                float b = data[i + j + len];
                data[i + j]       = a + b;
                data[i + j + len] = a - b;
            }
        }
    }

    // Final stage with normalization folded in: 1/sqrt(16) = 0.25
    constexpr float kNorm = 0.25f;
    constexpr int lastLen = 1 << (kLog2N - 1); // 8
    for (int i = 0; i < n; i += 2 * lastLen)
    {
        for (int j = 0; j < lastLen; ++j)
        {
            float a = data[i + j];
            float b = data[i + j + lastLen];
            data[i + j]            = (a + b) * kNorm;
            data[i + j + lastLen]  = (a - b) * kNorm;
        }
    }
}

// In-place fast Walsh-Hadamard transform for N=8.
void hadamardInPlace8 (float* data)
{
    constexpr int n = 8;
    constexpr int kLog2N = 3;

    for (int stage = 0; stage < kLog2N - 1; ++stage)
    {
        int len = 1 << stage;
        for (int i = 0; i < n; i += 2 * len)
        {
            for (int j = 0; j < len; ++j)
            {
                float a = data[i + j];
                float b = data[i + j + len];
                data[i + j]       = a + b;
                data[i + j + len] = a - b;
            }
        }
    }

    constexpr float kNorm = 0.353553f; // 1/sqrt(8)
    constexpr int lastLen = 1 << (kLog2N - 1); // 4
    for (int i = 0; i < n; i += 2 * lastLen)
    {
        for (int j = 0; j < lastLen; ++j)
        {
            float a = data[i + j];
            float b = data[i + j + lastLen];
            data[i + j]            = (a + b) * kNorm;
            data[i + j + lastLen]  = (a - b) * kNorm;
        }
    }
}

// In-place Householder reflection H = I - 2·v·vᵀ with a per-instance,
// randomized unit vector v. Replaces the degenerate v = 1/√N form, which had
// eigenvalue −1 only along the all-ones axis — output[i] = input[i] − 2·mean,
// i.e. a common DC offset, no true cross-channel mixing. The randomized v
// places the −1 axis on an arbitrary direction in N-space so every input
// channel influences every output channel with weight 2·v[i]·v[j] ≠ 0.
// Seed lives on the FDNReverb instance so two reverbs on the same bus see
// different reflector axes (a shared static seed produced correlated tails
// across instances).
inline void householderInPlace16 (float* data, const float* v)
{
    float dot = 0.0f;
    for (int i = 0; i < 16; ++i) dot += data[i] * v[i];
    const float k = 2.0f * dot;
    for (int i = 0; i < 16; ++i) data[i] -= k * v[i];
}

inline void householderInPlace8 (float* data, const float* v)
{
    float dot = 0.0f;
    for (int i = 0; i < 8; ++i) dot += data[i] * v[i];
    const float k = 2.0f * dot;
    for (int i = 0; i < 8; ++i) data[i] -= k * v[i];
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Default Hall-style delay table for the 16-channel FDN. Logarithmically
// spaced primes spanning ~2.49 octaves (6451/1151 = 5.605). All primes →
// mutually coprime → no shared modal alignment. Fits inside kMaxBaseDelay.
namespace {
    constexpr int   kDefaultDelays[16] = {
        1151, 1289, 1447, 1619, 1823, 2039, 2287, 2579,
        2887, 3229, 3631, 4073, 4567, 5119, 5749, 6451
    };
    constexpr int   kDefaultLeftTaps[8]  = { 0, 3, 5, 7, 8, 10, 12, 15 };
    constexpr int   kDefaultRightTaps[8] = { 1, 2, 4, 6, 9, 11, 13, 14 };
    constexpr float kDefaultLeftSigns[8]  = { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr float kDefaultRightSigns[8] = { -1, 1, -1, 1, -1, 1, -1, 1 };
}

FDNReverb::FDNReverb()
{
    std::memcpy (baseDelays_,   kDefaultDelays,    sizeof (baseDelays_));
    std::memcpy (leftTapsIn_,   kDefaultLeftTaps,  sizeof (leftTapsIn_));
    std::memcpy (rightTapsIn_,  kDefaultRightTaps, sizeof (rightTapsIn_));
    std::memcpy (leftSignsIn_,  kDefaultLeftSigns, sizeof (leftSignsIn_));
    std::memcpy (rightSignsIn_, kDefaultRightSigns, sizeof (rightSignsIn_));

    // Seed both slots with the default LiveParams. The atomic pointer is
    // set in prepare() once the snapshot fields have been computed.
    paramSlots_[0] = LiveParams{};
    paramSlots_[1] = LiveParams{};
    std::memcpy (paramSlots_[0].leftTaps,   kDefaultLeftTaps,   sizeof (kDefaultLeftTaps));
    std::memcpy (paramSlots_[0].rightTaps,  kDefaultRightTaps,  sizeof (kDefaultRightTaps));
    std::memcpy (paramSlots_[0].leftSigns,  kDefaultLeftSigns,  sizeof (kDefaultLeftSigns));
    std::memcpy (paramSlots_[0].rightSigns, kDefaultRightSigns, sizeof (kDefaultRightSigns));
    paramSlots_[1] = paramSlots_[0];

    // Per-instance Householder reflector seed. Each FDNReverb gets a distinct
    // axis so eigenmodes don't align across stacked instances on the same bus.
    static std::atomic<uint32_t> kHouseholderSeedCounter { 0x9E3779B9u };
    uint32_t seed = kHouseholderSeedCounter.fetch_add (0x85EBCA77u,
                                                       std::memory_order_relaxed);
    if (seed == 0u) seed = 0x9E3779B9u;
    seedHouseholderVectors (seed);
}

void FDNReverb::seedHouseholderVectors (uint32_t seed)
{
    auto next01 = [&seed]() {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return static_cast<float> (static_cast<int32_t> (seed)) * (1.0f / 2147483648.0f);
    };

    float sumSq = 0.0f;
    for (int i = 0; i < 16; ++i) { householderV16_[i] = next01(); sumSq += householderV16_[i] * householderV16_[i]; }
    const float inv16 = 1.0f / std::sqrt (sumSq);
    for (int i = 0; i < 16; ++i) householderV16_[i] *= inv16;

    sumSq = 0.0f;
    for (int i = 0; i < 8; ++i)  { householderV8_[i]  = next01(); sumSq += householderV8_[i] * householderV8_[i]; }
    const float inv8 = 1.0f / std::sqrt (sumSq);
    for (int i = 0; i < 8;  ++i) householderV8_[i]  *= inv8;
}

// ---------------------------------------------------------------------------
void FDNReverb::publishPending()
{
    // Pointer swap is the atomic publish. Slot pointer is the only thing the
    // RT thread ever loads — biquad coefficients, perturb matrix, multi-tap
    // table all reach the RT thread through this single acquire/release pair.
    liveParams_.store (&paramSlots_[pendingSlot_], std::memory_order_release);

    // Re-seed the new pending slot from the just-published one so subsequent
    // partial setter writes operate on a fresh copy of current state.
    const int newPending = pendingSlot_ ^ 1;
    paramSlots_[newPending] = paramSlots_[pendingSlot_];
    pendingSlot_ = newPending;
}

// ---------------------------------------------------------------------------
void FDNReverb::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Buffer sizing (independent of LiveParams)
    sizeRangeAllocatedMax_ = std::max (sizeRangeAllocatedMax_, std::max (sizeRangeMax_, 1.5f));
    float maxDelay = static_cast<float> (kMaxBaseDelay)
                   * static_cast<float> (sampleRate / kBaseSampleRate) * sizeRangeAllocatedMax_;
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
    float maxModExcursion = 2.0f * 16.0f * rateRatio;
    float maxNoiseExcursion = 20.0f * rateRatio;
    int maxExcursion = static_cast<int> (std::ceil (maxModExcursion + maxNoiseExcursion)) + 4;
    int bufSize = DspUtils::nextPowerOf2 (static_cast<int> (std::ceil (maxDelay)) + maxExcursion);

    for (int i = 0; i < N; ++i)
    {
        delayLines_[i].buffer.assign (static_cast<size_t> (bufSize), 0.0f);
        delayLines_[i].writePos = 0;
        delayLines_[i].mask = bufSize - 1;
        dampFilter_[i].prepare (static_cast<float> (sampleRate));
        dampFilter_[i].reset();
        structHFState_[i] = 0.0f;
        structLFState_[i] = 0.0f;
        antiAliasState_[i] = 0.0f;
        dcX1_[i] = 0.0f;
        dcY1_[i] = 0.0f;
    }

    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAP_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP_[i].writePos = 0;
        inlineAP_[i].mask = apBufSize - 1;
        inlineAP_[i].delaySamples = apDelay;
    }
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays2[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAP2_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP2_[i].writePos = 0;
        inlineAP2_[i].mask = apBufSize - 1;
        inlineAP2_[i].delaySamples = apDelay;
    }
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays3[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAP3_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP3_[i].writePos = 0;
        inlineAP3_[i].mask = apBufSize - 1;
        inlineAP3_[i].delaySamples = apDelay;
    }
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelaysShort[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAPShort_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAPShort_[i].writePos = 0;
        inlineAPShort_[i].mask = apBufSize - 1;
        inlineAPShort_[i].delaySamples = apDelay;
    }

    float antiAliasHz = std::min (17000.0f, static_cast<float> (sampleRate) * 0.45f);
    const float antiAliasCoeff = std::exp (-kTwoPi * antiAliasHz / static_cast<float> (sampleRate));
    const float dcCoeff = 1.0f - (kTwoPi * 5.0f / static_cast<float> (sampleRate));

    std::memset (antiAliasState_, 0, sizeof (antiAliasState_));
    std::memset (dcX1_, 0, sizeof (dcX1_));
    std::memset (dcY1_, 0, sizeof (dcY1_));

    {
        constexpr uint32_t kFixedBaseSeed = 0x5A3C9E71u;
        for (int i = 0; i < N; ++i)
        {
            const uint32_t seed = kFixedBaseSeed + static_cast<uint32_t> (i * 1847);
            lfos_[i].prepare (static_cast<float> (sampleRate), seed);
        }
    }

    // -----------------------------------------------------------------------
    // Seed paramSlots_[0] with the fully-derived snapshot, then mirror to
    // slot 1 so both slots are valid before publishing.
    {
        LiveParams& p = paramSlots_[0];
        // Carry forward fields the constructor / setters have already touched.
        p.antiAliasCoeff = antiAliasCoeff;
        p.dcCoeff        = dcCoeff;

        // Copy raw setter-staged data into the snapshot.
        std::memcpy (p.leftTaps,   leftTapsIn_,   sizeof (leftTapsIn_));
        std::memcpy (p.rightTaps,  rightTapsIn_,  sizeof (rightTapsIn_));
        std::memcpy (p.leftSigns,  leftSignsIn_,  sizeof (leftSignsIn_));
        std::memcpy (p.rightSigns, rightSignsIn_, sizeof (rightSignsIn_));
        std::memcpy (p.multiTapsL, multiTapsLIn_, sizeof (multiTapsLIn_));
        std::memcpy (p.multiTapsR, multiTapsRIn_, sizeof (multiTapsRIn_));
        p.numMultiTapsL      = numMultiTapsLIn_;
        p.numMultiTapsR      = numMultiTapsRIn_;
        p.useMultiPointOutput = useMultiPointIn_;
        std::memcpy (p.perturbMatrix, perturbMatrixIn_, sizeof (perturbMatrixIn_));
        p.usePerturbedMatrix = usePerturbedIn_;

        computeDelayLengths      (p);
        computeDecayCoefficients (p);

        paramSlots_[1] = p;
    }

    pendingSlot_ = 1;
    liveParams_.store (&paramSlots_[0], std::memory_order_release);

    updateModDepth();
    updateLFORates();

    prepared_ = true;
}

// ---------------------------------------------------------------------------
void FDNReverb::process (const float* inputL, const float* inputR,
                         float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    // Acquire the latest snapshot once per block. Every per-sample read
    // below goes through `lp` — no torn reads, no half-updated state.
    const LiveParams& lp = *liveParams_.load (std::memory_order_acquire);

    const float satThreshold = 1.0f - lp.saturationAmount * 0.6f;
    const float satCeiling   = 2.0f;
    const bool  frozen       = lp.frozen;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = inputL[i];
        const float inR = inputR[i];

        // --- 1) Read from all delay lines with LFO-modulated fractional position ---
        float delayOut[N];
        for (int ch = 0; ch < N; ++ch)
        {
            auto& dl = delayLines_[ch];
            float mod    = frozen ? 0.0f : (lfos_[ch].next() * lp.modDepthScale[ch]);
            float readDelay = std::max (lp.delayLength[ch] + mod, 1.0f);
            float readPos = static_cast<float> (dl.writePos) - readDelay;

            int intIdx = static_cast<int> (std::floor (readPos));
            float frac = readPos - static_cast<float> (intIdx);

            delayOut[ch] = DspUtils::cubicHermite (dl.buffer.data(), dl.mask, intIdx, frac);
        }

        // --- 1.5) Inline allpass diffusion ---
        if (lp.inlineDiffCoeff > 0.0f && ! frozen)
        {
            if (lp.useShortInlineAP)
            {
                for (int ch = 0; ch < N; ++ch)
                    delayOut[ch] = inlineAPShort_[ch].process (delayOut[ch], lp.inlineDiffCoeff);
            }
            else
            {
                for (int ch = 0; ch < N; ++ch)
                    delayOut[ch] = inlineAP_[ch].process (delayOut[ch], lp.inlineDiffCoeff);
            }
        }
        if (! lp.useShortInlineAP && lp.inlineDiffCoeff2 > 0.0f && ! frozen)
        {
            for (int ch = 0; ch < N; ++ch)
                delayOut[ch] = inlineAP2_[ch].process (delayOut[ch], lp.inlineDiffCoeff2);
        }
        if (lp.inlineDiffCoeff3 > 0.0f && ! frozen)
        {
            for (int ch = 0; ch < N; ++ch)
                delayOut[ch] = inlineAP3_[ch].process (delayOut[ch], lp.inlineDiffCoeff3);
        }

        // --- 2) Feedback mixing ---
        float feedback[N];
        if (lp.useHouseholder)
        {
            std::memcpy (feedback, delayOut, sizeof (feedback));
            if (lp.stereoSplitEnabled && lp.dualSlopeFastCount == 0)
            {
                householderInPlace8 (feedback,     householderV8_);
                householderInPlace8 (feedback + 8, householderV8_);
                if (lp.stereoCoupling > 0.0f)
                {
                    float sinC = lp.stereoCoupling;
                    float cosC = std::sqrt (1.0f - sinC * sinC);
                    for (int ch = 0; ch < 8; ++ch)
                    {
                        float l = feedback[ch];
                        float r = feedback[ch + 8];
                        feedback[ch]     =  l * cosC + r * sinC;
                        feedback[ch + 8] = -l * sinC + r * cosC;
                    }
                }
            }
            else if (lp.dualSlopeFastCount == 8)
            {
                householderInPlace8 (feedback,     householderV8_);
                householderInPlace8 (feedback + 8, householderV8_);
            }
            else
            {
                householderInPlace16 (feedback, householderV16_);
            }
        }
        else if (lp.stereoSplitEnabled && ! lp.usePerturbedMatrix && lp.dualSlopeFastCount == 0)
        {
            std::memcpy (feedback, delayOut, sizeof (feedback));
            hadamardInPlace8 (feedback);
            hadamardInPlace8 (feedback + 8);

            if (lp.stereoCoupling > 0.0f)
            {
                float sinC = lp.stereoCoupling;
                float cosC = std::sqrt (1.0f - sinC * sinC);
                for (int ch = 0; ch < 8; ++ch)
                {
                    float l = feedback[ch];
                    float r = feedback[ch + 8];
                    feedback[ch]     =  l * cosC + r * sinC;
                    feedback[ch + 8] = -l * sinC + r * cosC;
                }
            }
        }
        else if (lp.usePerturbedMatrix)
        {
            for (int row = 0; row < N; ++row)
            {
                float sum = 0.0f;
                for (int col = 0; col < N; ++col)
                    sum += lp.perturbMatrix[row][col] * delayOut[col];
                feedback[row] = sum;
            }
        }
        else if (lp.dualSlopeFastCount == 8)
        {
            std::memcpy (feedback, delayOut, sizeof (feedback));
            hadamardInPlace8 (feedback);
            hadamardInPlace8 (feedback + 8);
        }
        else
        {
            std::memcpy (feedback, delayOut, sizeof (feedback));
            hadamardInPlace16 (feedback);
        }

        // --- 3) Three-band damping + input injection -> write to delay lines ---
        for (int ch = 0; ch < N; ++ch)
        {
            auto& dl = delayLines_[ch];

            // Snapshot-path damping: coeffs come from lp.damping[ch] by const
            // ref; filter state (z1/z2) lives in dampFilter_[ch]. Zero-tear.
            float filtered = frozen ? feedback[ch]
                                    : dampFilter_[ch].process (feedback[ch], lp.damping[ch]);

            if (lp.structHFEnabled && ! frozen)
            {
                structHFState_[ch] = (1.0f - lp.structHFCoeff) * filtered
                                   + lp.structHFCoeff * structHFState_[ch];
                filtered = structHFState_[ch];
            }

            if (lp.structLFEnabled && ! frozen)
            {
                float hp = filtered - structLFState_[ch];
                structLFState_[ch] = (1.0f - lp.structLFCoeff) * filtered
                                   + lp.structLFCoeff * structLFState_[ch];
                filtered = hp;
            }

            if (! frozen)
            {
                antiAliasState_[ch] = (1.0f - lp.antiAliasCoeff) * filtered
                                    + lp.antiAliasCoeff * antiAliasState_[ch];
                filtered = antiAliasState_[ch];

                float dcOut = filtered - dcX1_[ch] + lp.dcCoeff * dcY1_[ch];
                dcX1_[ch] = filtered;
                dcY1_[ch] = dcOut;
                filtered = dcOut;
            }

            float inputGain = frozen ? 0.0f : 0.25f * lp.inputGainScale[ch];
            float polarity  = (ch & 1) ? -1.0f : 1.0f;
            float inputSample = lp.stereoSplitEnabled ? ((ch < 8) ? inL : inR)
                                                      : ((ch & 1) ? inR : inL);
            const float satFiltered = DspUtils::softClip (filtered, satThreshold, satCeiling);
            dl.buffer[static_cast<size_t> (dl.writePos)] =
                satFiltered + inputSample * polarity * inputGain;

            dl.writePos = (dl.writePos + 1) & dl.mask;
        }

        // --- 4) Tap decorrelated stereo outputs ---
        float outL = 0.0f, outR = 0.0f;

        if (lp.useMultiPointOutput)
        {
            for (int t = 0; t < lp.numMultiTapsL; ++t)
            {
                const auto& tap = lp.multiTapsL[t];
                const auto& dl  = delayLines_[tap.channelIndex];
                const float readDelay = lp.delayLength[tap.channelIndex] * tap.positionFrac;
                const float readPos   = static_cast<float> (dl.writePos) - readDelay;
                const int   intIdx    = static_cast<int> (std::floor (readPos));
                const float frac      = readPos - static_cast<float> (intIdx);
                outL += DspUtils::cubicHermite (dl.buffer.data(), dl.mask, intIdx, frac)
                        * tap.sign;
            }
            for (int t = 0; t < lp.numMultiTapsR; ++t)
            {
                const auto& tap = lp.multiTapsR[t];
                const auto& dl  = delayLines_[tap.channelIndex];
                const float readDelay = lp.delayLength[tap.channelIndex] * tap.positionFrac;
                const float readPos   = static_cast<float> (dl.writePos) - readDelay;
                const int   intIdx    = static_cast<int> (std::floor (readPos));
                const float frac      = readPos - static_cast<float> (intIdx);
                outR += DspUtils::cubicHermite (dl.buffer.data(), dl.mask, intIdx, frac)
                        * tap.sign;
            }
            outL /= static_cast<float> (lp.numMultiTapsL);
            outR /= static_cast<float> (lp.numMultiTapsR);
        }
        else
        {
            for (int t = 0; t < kNumOutputTaps; ++t)
            {
                outL += delayOut[lp.leftTaps[t]]  * lp.leftSigns[t]
                      * lp.outputTapGain[lp.leftTaps[t]]
                      * lp.outputGainScale[lp.leftTaps[t]];
                outR += delayOut[lp.rightTaps[t]] * lp.rightSigns[t]
                      * lp.outputTapGain[lp.rightTaps[t]]
                      * lp.outputGainScale[lp.rightTaps[t]];
            }
        }

        // Fold lateGainScale before clipping; softClip linear below 1.0
        // (no THD on tail), tanh-shaped above; hard clamp catches anything
        // beyond the soft-clip ceiling.
        const float rawL = outL * kOutputLevel * lp.sizeCompensation * lp.lateGainScale;
        const float rawR = outR * kOutputLevel * lp.sizeCompensation * lp.lateGainScale;
        outputL[i] = std::clamp (DspUtils::softClip (rawL, 1.0f, kSafetyClip),
                                 -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (DspUtils::softClip (rawR, 1.0f, kSafetyClip),
                                 -kSafetyClip, kSafetyClip);
    }
}

// ===========================================================================
// Setters — write raw → compute into pending() → publish.
// ===========================================================================
void FDNReverb::setDecayTime (float seconds)
{
    decayTime_ = std::clamp (seconds, 0.2f, 600.0f);
    if (! prepared_) return;
    computeDecayCoefficients (pending());
    publishPending();
}

void FDNReverb::setBassMultiply (float mult)
{
    bassMultiply_ = std::clamp (mult, 0.5f, 2.5f);
    if (! prepared_) return;
    computeDecayCoefficients (pending());
    publishPending();
}

void FDNReverb::setMidMultiply (float mult)
{
    midMultiply_ = std::clamp (mult, 0.1f, 4.0f);
    if (! prepared_) return;
    computeDecayCoefficients (pending());
    publishPending();
}

void FDNReverb::setSaturation (float amount)
{
    pending().saturationAmount = std::clamp (amount, 0.0f, 1.0f);
    if (! prepared_) return;
    publishPending();
}

void FDNReverb::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::clamp (mult, 0.1f, 1.5f);
    if (! prepared_) return;
    computeDecayCoefficients (pending());
    publishPending();
}

void FDNReverb::setCrossoverFreq (float hz)
{
    crossoverFreq_ = std::clamp (hz, 200.0f, 4000.0f);
    if (crossoverFreq_ > highCrossoverFreq_)
        highCrossoverFreq_ = crossoverFreq_;
    if (! prepared_) return;
    computeDecayCoefficients (pending());
    publishPending();
}

void FDNReverb::setModDepth (float depth)
{
    modDepth_ = std::clamp (depth, 0.0f, 2.0f);
    if (prepared_)
        updateModDepth();
}

void FDNReverb::setModRate (float hz)
{
    modRateHz_ = std::max (hz, 0.01f);
    if (prepared_)
        updateLFORates();
}

void FDNReverb::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (! prepared_) return;
    LiveParams& p = pending();
    computeDelayLengths (p);
    computeDecayCoefficients (p);
    publishPending();
}

void FDNReverb::setFreeze (bool frozen)
{
    const bool wasTransition = (frozen != pending().frozen);
    pending().frozen = frozen;
    if (wasTransition)
    {
        // Clear bypassed DSP block state so releasing freeze won't pop
        for (int i = 0; i < N; ++i)
        {
            inlineAP_[i].clear();
            inlineAP2_[i].clear();
            inlineAP3_[i].clear();
            inlineAPShort_[i].clear();
            dampFilter_[i].reset();
            structHFState_[i] = 0.0f;
            structLFState_[i] = 0.0f;
            antiAliasState_[i] = 0.0f;
            dcX1_[i] = 0.0f;
            dcY1_[i] = 0.0f;
        }
    }
    publishPending();
}

void FDNReverb::setBaseDelays (const int* delays)
{
    if (delays == nullptr) return;
    for (int i = 0; i < N; ++i)
        baseDelays_[i] = std::clamp (delays[i], 1, kMaxBaseDelay);
    if (! prepared_) return;
    LiveParams& p = pending();
    computeDelayLengths (p);
    computeDecayCoefficients (p);
    publishPending();
}

void FDNReverb::setOutputTaps (const int* lt, const int* rt,
                                const float* ls, const float* rs)
{
    if (lt == nullptr || rt == nullptr || ls == nullptr || rs == nullptr)
        return;

    for (int i = 0; i < kNumOutputTaps; ++i)
    {
        leftTapsIn_[i]  = std::clamp (lt[i], 0, N - 1);
        rightTapsIn_[i] = std::clamp (rt[i], 0, N - 1);
    }
    std::memcpy (leftSignsIn_,  ls, sizeof (leftSignsIn_));
    std::memcpy (rightSignsIn_, rs, sizeof (rightSignsIn_));

    LiveParams& p = pending();
    std::memcpy (p.leftTaps,   leftTapsIn_,   sizeof (leftTapsIn_));
    std::memcpy (p.rightTaps,  rightTapsIn_,  sizeof (rightTapsIn_));
    std::memcpy (p.leftSigns,  leftSignsIn_,  sizeof (leftSignsIn_));
    std::memcpy (p.rightSigns, rightSignsIn_, sizeof (rightSignsIn_));
    publishPending();
}

void FDNReverb::setLateGainScale (float scale)
{
    pending().lateGainScale = std::max (scale, 0.0f);
    publishPending();
}

void FDNReverb::setSizeRange (float min, float max)
{
    float newMin = std::max (min, 0.0f);
    float newMax = std::max (max, newMin);
    if (prepared_)
    {
        newMin = std::min (newMin, sizeRangeAllocatedMax_);
        newMax = std::min (newMax, sizeRangeAllocatedMax_);
    }
    sizeRangeMin_ = newMin;
    sizeRangeMax_ = std::max (newMax, sizeRangeMin_);
    if (! prepared_) return;
    LiveParams& p = pending();
    computeDelayLengths (p);
    computeDecayCoefficients (p);
    publishPending();
}

void FDNReverb::setInlineDiffusion (float coeff)
{
    LiveParams& p = pending();
    p.inlineDiffCoeff  = std::clamp (coeff, 0.0f, 0.75f);
    p.inlineDiffCoeff2 = p.inlineDiffCoeff * 0.8f;
    p.inlineDiffCoeff3 = 0.0f;
    publishPending();
}

void FDNReverb::setTankDiffusion (float amount)
{
    float a = std::clamp (amount, 0.0f, 1.0f);
    setInlineDiffusion (a * 0.55f);
}

void FDNReverb::setUseShortInlineAP (bool use)
{
    pending().useShortInlineAP = use;
    publishPending();
}

void FDNReverb::setMultiPointOutput (const FDNOutputTap* left, int numL,
                                     const FDNOutputTap* right, int numR)
{
    if (left == nullptr || numL <= 0 || right == nullptr || numR <= 0)
    {
        useMultiPointIn_ = false;
        numMultiTapsLIn_ = 0;
        numMultiTapsRIn_ = 0;
        LiveParams& p = pending();
        p.useMultiPointOutput = false;
        p.numMultiTapsL = 0;
        p.numMultiTapsR = 0;
        publishPending();
        return;
    }
    numMultiTapsLIn_ = std::min (numL, static_cast<int> (kMaxMultiTaps));
    numMultiTapsRIn_ = std::min (numR, static_cast<int> (kMaxMultiTaps));

    for (int i = 0; i < numMultiTapsLIn_; ++i)
    {
        if (left[i].channelIndex < 0 || left[i].channelIndex >= N)
        {
            useMultiPointIn_ = false;
            numMultiTapsLIn_ = 0;
            numMultiTapsRIn_ = 0;
            LiveParams& p = pending();
            p.useMultiPointOutput = false;
            p.numMultiTapsL = 0;
            p.numMultiTapsR = 0;
            publishPending();
            return;
        }
        multiTapsLIn_[i] = left[i];
    }
    for (int i = 0; i < numMultiTapsRIn_; ++i)
    {
        if (right[i].channelIndex < 0 || right[i].channelIndex >= N)
        {
            useMultiPointIn_ = false;
            numMultiTapsLIn_ = 0;
            numMultiTapsRIn_ = 0;
            LiveParams& p = pending();
            p.useMultiPointOutput = false;
            p.numMultiTapsL = 0;
            p.numMultiTapsR = 0;
            publishPending();
            return;
        }
        multiTapsRIn_[i] = right[i];
    }
    useMultiPointIn_ = true;

    LiveParams& p = pending();
    std::memcpy (p.multiTapsL, multiTapsLIn_, sizeof (multiTapsLIn_));
    std::memcpy (p.multiTapsR, multiTapsRIn_, sizeof (multiTapsRIn_));
    p.numMultiTapsL      = numMultiTapsLIn_;
    p.numMultiTapsR      = numMultiTapsRIn_;
    p.useMultiPointOutput = true;
    publishPending();
}

void FDNReverb::setMultiPointDensity (int tapsPerChannel)
{
    int totalTaps = tapsPerChannel * N;
    if (totalTaps <= 0 || totalTaps > kMaxMultiTaps)
    {
        useMultiPointIn_ = false;
        numMultiTapsLIn_ = 0;
        numMultiTapsRIn_ = 0;
        LiveParams& p = pending();
        p.useMultiPointOutput = false;
        p.numMultiTapsL = 0;
        p.numMultiTapsR = 0;
        publishPending();
        return;
    }

    numMultiTapsLIn_ = totalTaps;
    numMultiTapsRIn_ = totalTaps;

    for (int ch = 0; ch < N; ++ch)
    {
        for (int t = 0; t < tapsPerChannel; ++t)
        {
            int idx = ch * tapsPerChannel + t;
            float posL = 0.05f + 0.90f * (static_cast<float> (t) + 0.5f)
                                        / static_cast<float> (tapsPerChannel);
            float posR = 0.05f + 0.90f * (static_cast<float> (t) + 0.3f)
                                        / static_cast<float> (tapsPerChannel);
            float signL = (t % 2 == 0) ? 1.0f : -1.0f;
            float signR = (t % 2 == 1) ? 1.0f : -1.0f;

            multiTapsLIn_[idx] = { ch, posL, signL };
            multiTapsRIn_[idx] = { ch, posR, signR };
        }
    }
    useMultiPointIn_ = true;

    LiveParams& p = pending();
    std::memcpy (p.multiTapsL, multiTapsLIn_, sizeof (multiTapsLIn_));
    std::memcpy (p.multiTapsR, multiTapsRIn_, sizeof (multiTapsRIn_));
    p.numMultiTapsL      = numMultiTapsLIn_;
    p.numMultiTapsR      = numMultiTapsRIn_;
    p.useMultiPointOutput = true;
    publishPending();
}

void FDNReverb::setHadamardPerturbation (float amount)
{
    if (amount <= 0.0f)
    {
        usePerturbedIn_ = false;
        pending().usePerturbedMatrix = false;
        publishPending();
        return;
    }

    // Build Sylvester Hadamard
    float H[N][N];
    H[0][0] = 1.0f;
    for (int size = 1; size < N; size *= 2)
    {
        for (int i = 0; i < size; ++i)
        {
            for (int j = 0; j < size; ++j)
            {
                H[i][j + size]        =  H[i][j];
                H[i + size][j]        =  H[i][j];
                H[i + size][j + size] = -H[i][j];
            }
        }
    }

    float newPerturb[N][N];
    constexpr float kNorm = 0.25f;
    uint32_t seed = 0xDEADBEEF;
    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < N; ++j)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            float noise = static_cast<float> (static_cast<int32_t> (seed))
                        * (1.0f / 2147483648.0f);
            newPerturb[i][j] = (H[i][j] + noise * amount) * kNorm;
        }
    }

    // Polar projection via Newton: M_{k+1} = 0.5 * (M + M^{-T})
    constexpr int kPolarIters = 4;
    bool allIterationsSucceeded = true;
    for (int iter = 0; iter < kPolarIters; ++iter)
    {
        float M[N][N];
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                M[i][j] = newPerturb[i][j];

        float aug[N][2 * N];
        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                aug[i][j] = M[i][j];
                aug[i][j + N] = (i == j) ? 1.0f : 0.0f;
            }
        }

        bool gaussJordanSucceeded = true;
        for (int col = 0; col < N; ++col)
        {
            int pivotRow = col;
            float pivotVal = std::fabs (aug[col][col]);
            for (int row = col + 1; row < N; ++row)
            {
                float val = std::fabs (aug[row][col]);
                if (val > pivotVal)
                {
                    pivotVal = val;
                    pivotRow = row;
                }
            }
            if (pivotRow != col)
            {
                for (int k = 0; k < 2 * N; ++k)
                    std::swap (aug[col][k], aug[pivotRow][k]);
            }

            float diag = aug[col][col];
            if (std::fabs (diag) < 1e-12f)
            {
                gaussJordanSucceeded = false;
                break;
            }

            float invDiag = 1.0f / diag;
            for (int k = 0; k < 2 * N; ++k)
                aug[col][k] *= invDiag;

            for (int row = 0; row < N; ++row)
            {
                if (row == col) continue;
                float factor = aug[row][col];
                for (int k = 0; k < 2 * N; ++k)
                    aug[row][k] -= factor * aug[col][k];
            }
        }

        if (! gaussJordanSucceeded)
        {
            allIterationsSucceeded = false;
            break;
        }

        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                newPerturb[i][j] = 0.5f * (M[i][j] + aug[j][i + N]);
    }

    if (allIterationsSucceeded)
    {
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                perturbMatrixIn_[i][j] = newPerturb[i][j];
        usePerturbedIn_ = true;

        LiveParams& p = pending();
        std::memcpy (p.perturbMatrix, perturbMatrixIn_, sizeof (perturbMatrixIn_));
        p.usePerturbedMatrix = true;
        publishPending();
    }
}

void FDNReverb::setUseHouseholder (bool enable)
{
    pending().useHouseholder = enable;
    publishPending();
}

void FDNReverb::setUseWeightedGains (bool enable)
{
    useWeightedGains_ = enable;
    if (! prepared_) return;
    LiveParams& p = pending();
    computeDelayLengths (p);
    publishPending();
}

void FDNReverb::setDualSlope (float ratio, int fastCount, float fastGain)
{
    dualSlopeRatio_ = std::max (ratio, 0.0f);
    int fastCnt = (fastCount >= 8) ? 8 : 0;

    LiveParams& p = pending();
    p.dualSlopeFastCount = fastCnt;
    for (int i = 0; i < N; ++i)
        p.outputTapGain[i] = (fastCnt > 0 && i < fastCnt) ? fastGain : 1.0f;

    if (prepared_)
        computeDecayCoefficients (p);
    publishPending();
}

void FDNReverb::setStereoCoupling (float amount)
{
    LiveParams& p = pending();
    if (amount < 0.0f)
    {
        p.stereoSplitEnabled = false;
        p.stereoCoupling     = 0.0f;
    }
    else
    {
        p.stereoSplitEnabled = true;
        p.stereoCoupling     = std::clamp (amount, 0.0f, 0.75f);
    }
    publishPending();
}

void FDNReverb::setModDepthFloor (float floor)
{
    modDepthFloor_ = std::clamp (floor, 0.0f, 1.0f);
    if (! prepared_) return;
    LiveParams& p = pending();
    computeDelayLengths (p);
    publishPending();
}

void FDNReverb::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::clamp (hz, 1000.0f, 20000.0f);
    if (highCrossoverFreq_ < crossoverFreq_)
        crossoverFreq_ = highCrossoverFreq_;
    if (! prepared_) return;
    computeDecayCoefficients (pending());
    publishPending();
}

void FDNReverb::setAirTrebleMultiply (float mult)
{
    airTrebleMultiply_ = std::clamp (mult, 0.1f, 1.5f);
    if (! prepared_) return;
    computeDecayCoefficients (pending());
    publishPending();
}

void FDNReverb::setStructuralHFDamping (float baseFreqHz, float trebleMultiply)
{
    structHFBaseFreq_ = baseFreqHz;
    LiveParams& p = pending();
    if (baseFreqHz <= 0.0f)
    {
        p.structHFEnabled = false;
        p.structHFCoeff   = 0.0f;
        publishPending();
        return;
    }
    float effectiveHz = baseFreqHz * (1.5f - std::clamp (trebleMultiply, 0.1f, 1.0f) * 0.5f);
    p.structHFEnabled = true;
    p.structHFCoeff   = std::exp (-kTwoPi * effectiveHz / static_cast<float> (sampleRate_));
    publishPending();
}

void FDNReverb::setStructuralLFDamping (float hz)
{
    LiveParams& p = pending();
    if (hz <= 0.0f)
    {
        p.structLFEnabled = false;
        p.structLFCoeff   = 0.0f;
        publishPending();
        return;
    }
    p.structLFEnabled = true;
    p.structLFCoeff   = std::exp (-kTwoPi * hz / static_cast<float> (sampleRate_));
    publishPending();
}

void FDNReverb::setFeedbackModDepth (float depth)
{
    feedbackModDepth_ = std::clamp (depth, 0.0f, 1.0f);
}

void FDNReverb::setCrossoverModDepth (float depth)
{
    // The previous behaviour pushed the base coefficient back into the
    // legacy setLowCrossoverCoeff path. With the snapshot now owning the
    // damping coefficients, that route is no longer wired — crossover mod
    // is fully driven by the next computeDecayCoefficients() call.
    crossoverModDepth_ = std::clamp (depth, 0.0f, 1.0f);
}

void FDNReverb::setDecayBoost (float boost)
{
    decayBoost_ = std::clamp (boost, 0.3f, 2.0f);
    if (! prepared_) return;
    computeDecayCoefficients (pending());
    publishPending();
}

void FDNReverb::clearBuffers()
{
    for (int i = 0; i < N; ++i)
    {
        std::fill (delayLines_[i].buffer.begin(), delayLines_[i].buffer.end(), 0.0f);
        delayLines_[i].writePos = 0;
        inlineAP_[i].clear();
        inlineAP2_[i].clear();
        inlineAP3_[i].clear();
        inlineAPShort_[i].clear();
        dampFilter_[i].reset();
        structHFState_[i] = 0.0f;
        structLFState_[i] = 0.0f;
        antiAliasState_[i] = 0.0f;
        dcX1_[i] = 0.0f;
        dcY1_[i] = 0.0f;
    }
    constexpr uint32_t kFixedBaseSeed = 0x5A3C9E71u;
    for (int i = 0; i < N; ++i)
    {
        const uint32_t seed = kFixedBaseSeed + static_cast<uint32_t> (i * 1847);
        lfos_[i].prepare (static_cast<float> (sampleRate_), seed);
        lfos_[i].setRate  (modRateHz_);
        lfos_[i].setDepth (modDepthSamples_);
    }
}

// ===========================================================================
// Snapshot derivation
// ===========================================================================
void FDNReverb::computeDelayLengths (LiveParams& p)
{
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);

    p.sizeCompensation = std::pow (sizeScale, 0.4f);

    float maxDelay = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        p.delayLength[i] = static_cast<float> (baseDelays_[i]) * rateRatio * sizeScale;
        if (p.delayLength[i] > maxDelay)
            maxDelay = p.delayLength[i];
    }

    for (int i = 0; i < N; ++i)
    {
        float scale = (maxDelay > 0.0f) ? p.delayLength[i] / maxDelay : 1.0f;
        p.modDepthScale[i] = std::max (scale, modDepthFloor_);
    }

    if (useWeightedGains_)
    {
        float minDelay = p.delayLength[0];
        for (int i = 1; i < N; ++i)
            minDelay = std::min (minDelay, p.delayLength[i]);
        minDelay = std::max (minDelay, 1e-6f);

        float sumSqIn = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            float ratio = std::max (p.delayLength[i], minDelay) / minDelay;
            p.inputGainScale[i] = 1.0f / std::sqrt (ratio);
            sumSqIn += p.inputGainScale[i] * p.inputGainScale[i];
        }
        float normIn = std::sqrt (static_cast<float> (N) / sumSqIn);
        for (int i = 0; i < N; ++i)
            p.inputGainScale[i] *= normIn;

        for (int i = 0; i < N; ++i)
            p.outputGainScale[i] = p.inputGainScale[i];
    }
    else
    {
        for (int i = 0; i < N; ++i)
        {
            p.inputGainScale[i]  = 1.0f;
            p.outputGainScale[i] = 1.0f;
        }
    }
}

void FDNReverb::computeDecayCoefficients (LiveParams& p)
{
    const float sr = static_cast<float> (sampleRate_);

    float lowCrossoverCoeff  = std::exp (-kTwoPi * crossoverFreq_ / sr);
    float highCrossoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_ / sr);

    baseLowCrossoverCoeff_  = lowCrossoverCoeff;
    baseHighCrossoverCoeff_ = highCrossoverCoeff;

    const int dualSlopeFastCount = p.dualSlopeFastCount;

    for (int i = 0; i < N; ++i)
    {
        float channelRT60 = decayTime_;
        if (dualSlopeFastCount > 0 && i < dualSlopeFastCount && dualSlopeRatio_ > 0.0f)
            channelRT60 = std::max (decayTime_ * dualSlopeRatio_, 0.2f);

        float effectiveLength = p.delayLength[i];
        if (p.inlineDiffCoeff > 0.0f)
        {
            float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
            if (p.useShortInlineAP)
                effectiveLength += static_cast<float> (kInlineAPDelaysShort[i]) * rateRatio;
            else
                effectiveLength += static_cast<float> (kInlineAPDelays[i]) * rateRatio;
            if (! p.useShortInlineAP && p.inlineDiffCoeff2 > 0.0f)
                effectiveLength += static_cast<float> (kInlineAPDelays2[i]) * rateRatio;
            if (p.inlineDiffCoeff3 > 0.0f)
                effectiveLength += static_cast<float> (kInlineAPDelays3[i]) * rateRatio;
        }
        float gBase = std::pow (10.0f, -3.0f * effectiveLength / (channelRT60 * sr));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);

        float gLow  = std::pow (gBase, 1.0f / bassMultiply_);
        float gMid  = std::pow (gBase, 1.0f / midMultiply_);
        float gHigh = std::pow (gBase, 1.0f / std::max (airTrebleMultiply_, 0.01f));

        p.damping[i] = ThreeBandDamping::designCoeffs (gLow, gMid, gHigh,
                                                       lowCrossoverCoeff,
                                                       highCrossoverCoeff,
                                                       sr);
    }
}

void FDNReverb::updateModDepth()
{
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    modDepthSamples_ = modDepth_ * 4.0f * rateRatio;
    for (int i = 0; i < N; ++i)
        lfos_[i].setDepth (modDepthSamples_);
}

void FDNReverb::updateLFORates()
{
    static constexpr float kRateFactors[N] = {
        0.801f, 0.857f, 0.919f, 0.953f, 0.991f, 1.031f, 1.063f, 1.097f,
        1.127f, 1.163f, 1.193f, 1.223f, 1.259f, 1.289f, 1.319f, 1.361f
    };
    for (int i = 0; i < N; ++i)
        lfos_[i].setRate (modRateHz_ * kRateFactors[i]);
}

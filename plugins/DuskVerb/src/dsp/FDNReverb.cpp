#include "FDNReverb.h"
#include "AlgorithmConfig.h"
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
// Used in dual-slope mode: two independent 8×8 Hadamards decouple
// fast-decay and slow-decay channel groups so each maintains its own RT60.
// Normalization: 1/sqrt(8) ≈ 0.353553.
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

    // Final stage with normalization: 1/sqrt(8)
    constexpr float kNorm = 0.353553f;
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

} // anonymous namespace

// ---------------------------------------------------------------------------
FDNReverb::FDNReverb()
{
    // Initialize mutable config arrays from Hall defaults
    std::memcpy (baseDelays_, kHall.delayLengths, sizeof (baseDelays_));
    std::memcpy (leftTaps_,   kHall.leftTaps,     sizeof (leftTaps_));
    std::memcpy (rightTaps_,  kHall.rightTaps,    sizeof (rightTaps_));
    std::memcpy (leftSigns_,  kHall.leftSigns,    sizeof (leftSigns_));
    std::memcpy (rightSigns_, kHall.rightSigns,   sizeof (rightSigns_));
}

// ---------------------------------------------------------------------------
void FDNReverb::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    updateDelayLengths();

    // Allocate buffers for worst-case delay across ALL algorithms.
    // kMaxBaseDelay covers the longest line in any algorithm config.
    float maxSizeScale = std::max (sizeRangeMax_, 1.5f);
    float maxDelay = static_cast<float> (kMaxBaseDelay)
                   * static_cast<float> (sampleRate / kBaseSampleRate) * maxSizeScale;

    // +12 covers max modulation depth (modDepth 2.0 -> 8 samples) + cubic interp (2) + safety (2)
    int bufSize = DspUtils::nextPowerOf2 (static_cast<int> (std::ceil (maxDelay)) + 12);

    for (int i = 0; i < N; ++i)
    {
        delayLines_[i].buffer.assign (static_cast<size_t> (bufSize), 0.0f);
        delayLines_[i].writePos = 0;
        delayLines_[i].mask = bufSize - 1;
        dampFilter_[i].reset();
    }

    // Inline allpass diffusers: prime delays scaled by sample rate
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAP_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP_[i].writePos = 0;
        inlineAP_[i].mask = apBufSize - 1;
    }

    // Second inline allpass cascade: longer primes for density multiplication
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays2[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAP2_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP2_[i].writePos = 0;
        inlineAP2_[i].mask = apBufSize - 1;
    }

    // Third inline allpass cascade: even longer primes for ~8x density per cycle
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays3[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAP3_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP3_[i].writePos = 0;
        inlineAP3_[i].mask = apBufSize - 1;
    }

    // Reset feedback lowpass biquad state (both sections for 4th-order capability)
    std::memset (fbLPZ1_, 0, sizeof (fbLPZ1_));
    std::memset (fbLPZ2_, 0, sizeof (fbLPZ2_));
    std::memset (fbLPZ3_, 0, sizeof (fbLPZ3_));
    std::memset (fbLPZ4_, 0, sizeof (fbLPZ4_));

    // Evenly-spaced initial LFO phases
    for (int i = 0; i < N; ++i)
        lfoPhase_[i] = kTwoPi * static_cast<float> (i) / static_cast<float> (N);

    // Seed per-channel PRNG with coprime offsets for LFO drift
    for (int i = 0; i < N; ++i)
        lfoPRNG_[i] = static_cast<uint32_t> (i * 1847 + 2053);

    updateModDepth();
    updateLFORates();
    updateDecayCoefficients();

    prepared_ = true;
}

// ---------------------------------------------------------------------------
void FDNReverb::process (const float* inputL, const float* inputR,
                         float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = inputL[i];
        const float inR = inputR[i];

        // --- 1) Read from all delay lines with LFO-modulated fractional position ---
        float delayOut[N];
        for (int ch = 0; ch < N; ++ch)
        {
            auto& dl = delayLines_[ch];

            float mod = std::sin (lfoPhase_[ch]) * modDepthSamples_ * modDepthScale_[ch];
            // Per-sample random jitter: fast mode blurring complementing the slow LFO.
            // Each channel gets independent noise from its xorshift32 PRNG.
            float jitter = nextDrift (lfoPRNG_[ch]) * noiseModDepth_ * modDepthScale_[ch];
            float readDelay = delayLength_[ch] + mod + jitter;
            float readPos = static_cast<float> (dl.writePos) - readDelay;

            int intIdx = static_cast<int> (std::floor (readPos));
            float frac = readPos - static_cast<float> (intIdx);

            delayOut[ch] = DspUtils::cubicHermite (dl.buffer.data(), dl.mask, intIdx, frac);

            // Advance LFO with Lexicon-style "Wander" drift.
            // Adds ±8% random perturbation to the phase increment each sample
            // so the modulation never exactly repeats — organic, not mechanical.
            float drift = nextDrift (lfoPRNG_[ch]) * lfoPhaseInc_[ch] * 0.08f;
            lfoPhase_[ch] += lfoPhaseInc_[ch] + drift;
            if (lfoPhase_[ch] >= kTwoPi)
                lfoPhase_[ch] -= kTwoPi;
            else if (lfoPhase_[ch] < 0.0f)
                lfoPhase_[ch] += kTwoPi;
        }

        // --- 1.5) Inline allpass diffusion (Dattorro "decay diffusion") ---
        // Two cascaded Schroeder allpasses per channel. The second stage uses
        // a reduced coefficient (0.8x) to prevent over-diffusion/washiness
        // while multiplying echo density ~4x per feedback cycle.
        // Bypassed during freeze: allpasses alter loop magnitude response over time,
        // causing the frozen tail to spectrally evolve rather than truly sustaining.
        if (inlineDiffCoeff_ > 0.0f && ! frozen_)
        {
            for (int ch = 0; ch < N; ++ch)
                delayOut[ch] = inlineAP_[ch].process (delayOut[ch], inlineDiffCoeff_);
        }
        if (inlineDiffCoeff2_ > 0.0f && ! frozen_)
        {
            for (int ch = 0; ch < N; ++ch)
                delayOut[ch] = inlineAP2_[ch].process (delayOut[ch], inlineDiffCoeff2_);
        }
        if (inlineDiffCoeff3_ > 0.0f && ! frozen_)
        {
            for (int ch = 0; ch < N; ++ch)
                delayOut[ch] = inlineAP3_[ch].process (delayOut[ch], inlineDiffCoeff3_);
        }

        // --- 2) Feedback mixing ---
        float feedback[N];
        if (usePerturbedMatrix_)
        {
            // Perturbed matrix: N×N multiply breaks deterministic modal coupling.
            // 256 multiply-adds per sample (vs 64 for fast Hadamard) — negligible overhead.
            for (int row = 0; row < N; ++row)
            {
                float sum = 0.0f;
                for (int col = 0; col < N; ++col)
                    sum += perturbMatrix_[row][col] * delayOut[col];
                feedback[row] = sum;
            }
        }
        else if (dualSlopeFastCount_ == 8)
        {
            // Dual-slope: two independent 8×8 Hadamards.
            // Decouples fast-decay (0-7) and slow-decay (8-15) channel groups
            // so each maintains its own RT60 without cross-contamination.
            std::memcpy (feedback, delayOut, sizeof (feedback));
            hadamardInPlace8 (feedback);
            hadamardInPlace8 (feedback + 8);
        }
        else
        {
            std::memcpy (feedback, delayOut, sizeof (feedback));
            hadamardInPlace16 (feedback);
        }

        // --- 3) Two-band damping + input injection -> write to delay lines ---
        for (int ch = 0; ch < N; ++ch)
        {
            auto& dl = delayLines_[ch];

            // When frozen, bypass damping (unity feedback) to sustain tail indefinitely
            float filtered = frozen_ ? feedback[ch] : dampFilter_[ch].process (feedback[ch]);

            // Structural HF damping: gentle air-absorption LP (per-algorithm)
            if (structHFEnabled_ && ! frozen_)
            {
                structHFState_[ch] = (1.0f - structHFCoeff_) * filtered
                                   + structHFCoeff_ * structHFState_[ch];
                filtered = structHFState_[ch];
            }

            // Feedback lowpass: Butterworth LP applied after damping.
            // 2nd-order (12 dB/oct) or 4th-order L-R (24 dB/oct) per algorithm.
            // Direct Form II Transposed for numerical stability.
            if (fbLPEnabled_ && ! frozen_)
            {
                // Section 1 (always active)
                float in1 = filtered;
                float out1 = fbLP_b0_ * in1 + fbLPZ1_[ch];
                fbLPZ1_[ch] = fbLP_b1_ * in1 - fbLP_a1_ * out1 + fbLPZ2_[ch];
                fbLPZ2_[ch] = fbLP_b2_ * in1 - fbLP_a2_ * out1;
                filtered = out1;

                // Section 2 (identical coefficients, only when 4th-order enabled)
                if (fbLP4thOrder_)
                {
                    float in2 = out1;
                    float out2 = fbLP_b0_ * in2 + fbLPZ3_[ch];
                    fbLPZ3_[ch] = fbLP_b1_ * in2 - fbLP_a1_ * out2 + fbLPZ4_[ch];
                    fbLPZ4_[ch] = fbLP_b2_ * in2 - fbLP_a2_ * out2;
                    filtered = out2;
                }
            }

            // Inject stereo input: even channels get L, odd channels get R.
            // This preserves the stereo decorrelation from input diffusion
            // instead of mono-summing and discarding it.
            // When frozen, mute new input to keep only the existing tail.
            float inputGain = frozen_ ? 0.0f : 0.25f;
            float polarity = (ch & 1) ? -1.0f : 1.0f;
            float inputSample = (ch & 1) ? inR : inL;
            float denormalBias = ((dl.writePos ^ ch) & 1)
                                     ? DspUtils::kDenormalPrevention
                                     : -DspUtils::kDenormalPrevention;
            dl.buffer[static_cast<size_t> (dl.writePos)] =
                filtered + inputSample * polarity * inputGain + denormalBias;

            dl.writePos = (dl.writePos + 1) & dl.mask;
        }

        // --- 4) Tap decorrelated stereo outputs with signed summation ---
        // Per-channel outputTapGain_ enables dual-slope: fast channels are boosted
        // to create a loud initial burst, slow channels at unity form the quiet tail.
        float outL = 0.0f, outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
        {
            outL += delayOut[leftTaps_[t]] * leftSigns_[t] * outputTapGain_[leftTaps_[t]];
            outR += delayOut[rightTaps_[t]] * rightSigns_[t] * outputTapGain_[rightTaps_[t]];
        }

        // Linear output: 1/sqrt(8) normalization + 6dB level match.
        // sizeCompensation_ = sqrt(sizeScale) normalizes steady-state energy
        // across sizes — shorter delays at small sizes pack more recirculations
        // per unit time, producing higher energy density without compensation.
        outputL[i] = std::clamp (outL * kOutputScale * kOutputGain * sizeCompensation_, -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (outR * kOutputScale * kOutputGain * sizeCompensation_, -kSafetyClip, kSafetyClip);
    }
}

// ---------------------------------------------------------------------------
void FDNReverb::setDecayTime (float seconds)
{
    decayTime_ = std::clamp (seconds, 0.2f, 600.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void FDNReverb::setBassMultiply (float mult)
{
    bassMultiply_ = std::clamp (mult, 0.5f, 2.5f);
    if (prepared_)
        updateDecayCoefficients();
}

void FDNReverb::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::clamp (mult, 0.1f, 1.5f);
    if (prepared_)
        updateDecayCoefficients();
}

void FDNReverb::setCrossoverFreq (float hz)
{
    crossoverFreq_ = std::clamp (hz, 200.0f, 4000.0f);
    if (prepared_)
        updateDecayCoefficients();
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
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void FDNReverb::setFreeze (bool frozen)
{
    frozen_ = frozen;
    if (frozen)
    {
        for (int i = 0; i < N; ++i)
            structHFState_[i] = 0.0f;
    }
}

void FDNReverb::setBaseDelays (const int* delays)
{
    if (delays == nullptr)
        return;

    for (int i = 0; i < N; ++i)
        baseDelays_[i] = std::clamp (delays[i], 1, kMaxBaseDelay);

    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void FDNReverb::setOutputTaps (const int* lt, const int* rt,
                                const float* ls, const float* rs)
{
    if (lt == nullptr || rt == nullptr || ls == nullptr || rs == nullptr)
        return;

    for (int i = 0; i < kNumOutputTaps; ++i)
    {
        leftTaps_[i]  = std::clamp (lt[i], 0, N - 1);
        rightTaps_[i] = std::clamp (rt[i], 0, N - 1);
    }
    std::memcpy (leftSigns_,  ls, sizeof (leftSigns_));
    std::memcpy (rightSigns_, rs, sizeof (rightSigns_));
}

void FDNReverb::setLateGainScale (float scale)
{
    lateGainScale_ = std::max (scale, 0.0f);
}

void FDNReverb::setSizeRange (float min, float max)
{
    sizeRangeMin_ = std::clamp (min, 0.0f, 1.5f);
    sizeRangeMax_ = std::clamp (max, sizeRangeMin_, 1.5f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void FDNReverb::setInlineDiffusion (float coeff)
{
    inlineDiffCoeff_ = std::clamp (coeff, 0.0f, 0.75f);
    inlineDiffCoeff2_ = inlineDiffCoeff_ * 0.8f;
    // 3rd cascade disabled: extra feedback-loop phase accumulation worsens
    // ringing for longer-delay modes (Hall, Plate). Density is instead
    // improved via 3-stage output diffusion (post-FDN, no feedback impact).
    inlineDiffCoeff3_ = 0.0f;
}

void FDNReverb::setFeedbackLP4thOrder (bool enable)
{
    fbLP4thOrder_ = enable;
    // Reset section 2 state when disabling to prevent stale state
    if (! enable)
    {
        std::memset (fbLPZ3_, 0, sizeof (fbLPZ3_));
        std::memset (fbLPZ4_, 0, sizeof (fbLPZ4_));
    }
}

void FDNReverb::setHadamardPerturbation (float amount)
{
    if (amount <= 0.0f)
    {
        usePerturbedMatrix_ = false;
        return;
    }

    usePerturbedMatrix_ = true;

    // Build 16x16 Hadamard matrix (Sylvester construction)
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

    // Apply deterministic perturbations (seeded PRNG for reproducibility)
    constexpr float kNorm = 0.25f; // 1/sqrt(16)
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
            perturbMatrix_[i][j] = (H[i][j] + noise * amount) * kNorm;
        }
    }

    // Row-normalize to preserve approximate energy conservation (||row|| = 1)
    for (int i = 0; i < N; ++i)
    {
        float sumSq = 0.0f;
        for (int j = 0; j < N; ++j)
            sumSq += perturbMatrix_[i][j] * perturbMatrix_[i][j];
        float scale = 1.0f / std::sqrt (sumSq);
        for (int j = 0; j < N; ++j)
            perturbMatrix_[i][j] *= scale;
    }
}

void FDNReverb::setDualSlope (float ratio, int fastCount, float fastGain)
{
    dualSlopeRatio_ = std::max (ratio, 0.0f);
    // Only 0 (disabled) and 8 (two independent 8×8 Hadamards) are supported.
    dualSlopeFastCount_ = (fastCount >= 8) ? 8 : 0;

    for (int i = 0; i < N; ++i)
        outputTapGain_[i] = (dualSlopeFastCount_ > 0 && i < dualSlopeFastCount_)
                                ? fastGain : 1.0f;

    if (prepared_)
        updateDecayCoefficients();
}

void FDNReverb::setNoiseModDepth (float samples)
{
    noiseModDepthParam_ = std::max (samples, 0.0f);
    if (prepared_)
        updateModDepth();
}

void FDNReverb::setModDepthFloor (float floor)
{
    modDepthFloor_ = std::clamp (floor, 0.0f, 1.0f);
    if (prepared_)
        updateDelayLengths();
}

void FDNReverb::setStructuralHFDamping (float hz)
{
    if (hz <= 0.0f)
    {
        structHFEnabled_ = false;
        structHFCoeff_ = 0.0f;
        return;
    }
    structHFEnabled_ = true;
    structHFCoeff_ = std::exp (-kTwoPi * hz / static_cast<float> (sampleRate_));
}

void FDNReverb::clearBuffers()
{
    for (int i = 0; i < N; ++i)
    {
        std::fill (delayLines_[i].buffer.begin(), delayLines_[i].buffer.end(), 0.0f);
        inlineAP_[i].clear();
        inlineAP2_[i].clear();
        inlineAP3_[i].clear();
        dampFilter_[i].reset();
        fbLPZ1_[i] = 0.0f;
        fbLPZ2_[i] = 0.0f;
        fbLPZ3_[i] = 0.0f;
        fbLPZ4_[i] = 0.0f;
        structHFState_[i] = 0.0f;
    }
}

void FDNReverb::setFeedbackLP (float hz)
{
    if (hz <= 0.0f || hz >= static_cast<float> (sampleRate_) * 0.5f)
    {
        fbLPEnabled_ = false;
        fbLP_b0_ = 1.0f;
        fbLP_b1_ = fbLP_b2_ = fbLP_a1_ = fbLP_a2_ = 0.0f;
        return;
    }

    // Second-order Butterworth lowpass (12 dB/oct).
    float sr = static_cast<float> (sampleRate_);
    float omega = kTwoPi * hz / sr;
    float sn = std::sin (omega);
    float cs = std::cos (omega);
    float alpha = sn / 1.4142135623730951f; // sqrt(2) for Butterworth Q

    float a0 = 1.0f + alpha;
    fbLP_b0_ = ((1.0f - cs) * 0.5f) / a0;
    fbLP_b1_ = (1.0f - cs) / a0;
    fbLP_b2_ = ((1.0f - cs) * 0.5f) / a0;
    fbLP_a1_ = (-2.0f * cs) / a0;
    fbLP_a2_ = (1.0f - alpha) / a0;

    fbLPEnabled_ = true;
}

// ---------------------------------------------------------------------------
void FDNReverb::updateDelayLengths()
{
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);

    // Size-dependent gain compensation: shorter delays at small sizes pack
    // more feedback recirculations per unit time → higher energy density.
    // Power 0.4 balances attenuation at small sizes (~-2.0 dB at sizeScale=0.5)
    // with modest boost at large sizes (+1.4 dB at sizeScale=1.5).
    sizeCompensation_ = std::pow (sizeScale, 0.4f);

    float maxDelay = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        delayLength_[i] = static_cast<float> (baseDelays_[i]) * rateRatio * sizeScale;
        if (delayLength_[i] > maxDelay)
            maxDelay = delayLength_[i];
    }

    // Per-delay modulation depth scaling: proportional to delay length.
    // Prevents pitch wobble on short delays (Room) while preserving
    // full modulation on long delays (Hall/Ambient).
    // Floor is per-algorithm: Room uses 0.50 for more mode blurring on
    // short delays; others use 0.35 default.
    for (int i = 0; i < N; ++i)
    {
        float scale = (maxDelay > 0.0f) ? delayLength_[i] / maxDelay : 1.0f;
        modDepthScale_[i] = std::max (scale, modDepthFloor_);
    }
}

void FDNReverb::updateDecayCoefficients()
{
    // Crossover lowpass coefficient: c = exp(-2*pi*fc/sr)
    float crossoverCoeff = std::exp (-kTwoPi * crossoverFreq_
                                     / static_cast<float> (sampleRate_));

    for (int i = 0; i < N; ++i)
    {
        // Per-channel RT60: fast channels get shorter decay for dual-slope envelope
        float channelRT60 = decayTime_;
        if (dualSlopeFastCount_ > 0 && i < dualSlopeFastCount_ && dualSlopeRatio_ > 0.0f)
            channelRT60 = std::max (decayTime_ * dualSlopeRatio_, 0.2f);

        // Per-delay feedback gain for desired RT60
        // g_base = 10^(-3 * L / (RT60 * sr)) so that after RT60 seconds, signal is at -60 dB
        // Effective loop length includes inline allpass delays (they add latency to the
        // feedback path but aren't part of delayLength_). Without this, algorithms with
        // inline diffusion (Plate, Chamber) under-decay because the formula assumes a
        // shorter loop than the signal actually traverses.
        float effectiveLength = delayLength_[i];
        if (inlineDiffCoeff_ > 0.0f)
        {
            float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
            effectiveLength += static_cast<float> (kInlineAPDelays[i]) * rateRatio;
            if (inlineDiffCoeff2_ > 0.0f)
                effectiveLength += static_cast<float> (kInlineAPDelays2[i]) * rateRatio;
            if (inlineDiffCoeff3_ > 0.0f)
                effectiveLength += static_cast<float> (kInlineAPDelays3[i]) * rateRatio;
        }
        float gBase = std::pow (10.0f, -3.0f * effectiveLength
                                       / (channelRT60 * static_cast<float> (sampleRate_)));

        // Bass Multiply: g_low = g_base^(1/bassMultiply)
        // bassMultiply > 1.0 → lows sustain longer (g_low > g_base)
        float gLow = std::pow (gBase, 1.0f / bassMultiply_);

        // Treble Multiply: g_high = g_base^(1/trebleMultiply)
        // trebleMultiply < 1.0 → highs decay faster (g_high < g_base)
        float gHigh = std::pow (gBase, 1.0f / trebleMultiply_);

        dampFilter_[i].setCoefficients (gLow, gHigh, crossoverCoeff);
    }
}

void FDNReverb::updateModDepth()
{
    // Scale by sample rate ratio so modulation depth (in time) is consistent
    // across 44.1kHz, 48kHz, 96kHz etc.
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    modDepthSamples_ = modDepth_ * 4.0f * rateRatio;
    noiseModDepth_ = noiseModDepthParam_ * rateRatio;
}

void FDNReverb::updateLFORates()
{
    // Irregularly-spaced rate factors prevent modulation beating.
    // Adjacent ratios avoid simple rational relationships so no two
    // channels ever re-align into audible patterns.
    static constexpr float kRateFactors[N] = {
        0.801f, 0.857f, 0.919f, 0.953f, 0.991f, 1.031f, 1.063f, 1.097f,
        1.127f, 1.163f, 1.193f, 1.223f, 1.259f, 1.289f, 1.319f, 1.361f
    };

    for (int i = 0; i < N; ++i)
    {
        float rateHz = modRateHz_ * kRateFactors[i];
        lfoPhaseInc_[i] = kTwoPi * rateHz / static_cast<float> (sampleRate_);
    }
}

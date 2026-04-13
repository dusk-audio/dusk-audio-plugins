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

// In-place Householder reflection for N=16: H = I - (2/N) * ones * ones^T.
// Provides moderate inter-channel mixing (each output = input - mean),
// avoiding the eigentone clustering that maximum-mixing Hadamard causes.
// O(N) complexity: one sum + N subtracts. Energy-preserving (unitary matrix).
void householderInPlace16 (float* data)
{
    constexpr int n = 16;
    constexpr float kScale = 2.0f / static_cast<float> (n); // 0.125

    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += data[i];

    sum *= kScale;

    for (int i = 0; i < n; ++i)
        data[i] -= sum;
}

// In-place Householder reflection for N=8 (stereo split mode).
void householderInPlace8 (float* data)
{
    constexpr int n = 8;
    constexpr float kScale = 2.0f / static_cast<float> (n); // 0.25

    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += data[i];

    sum *= kScale;

    for (int i = 0; i < n; ++i)
        data[i] -= sum;
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
    sizeRangeAllocatedMax_ = maxSizeScale;
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
        structHFState_[i] = 0.0f;
        structLFState_[i] = 0.0f;
        antiAliasState_[i] = 0.0f;
        dcX1_[i] = 0.0f;
        dcY1_[i] = 0.0f;
    }
    peakRMS_ = 0.0f;
    currentRMS_ = 0.0f;
    terminalDecayActive_ = false;

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
        inlineAP_[i].delaySamples = apDelay;
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
        inlineAP2_[i].delaySamples = apDelay;
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
        inlineAP3_[i].delaySamples = apDelay;
    }

    // Short inline allpass cascade for Hall
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

    // Anti-alias LP coefficient: ~17kHz at any sample rate
    // At 44.1kHz: -3dB at 17kHz, -0.3dB at 12kHz (preserves air)
    // After 50 iterations: -15dB at 17kHz (kills aliasing), -15dB at 20kHz
    float antiAliasHz = std::min (17000.0f, static_cast<float> (sampleRate) * 0.45f);
    antiAliasCoeff_ = std::exp (-kTwoPi * antiAliasHz / static_cast<float> (sampleRate));
    std::memset (antiAliasState_, 0, sizeof (antiAliasState_));

    // DC blocker coefficient and state reset
    dcCoeff_ = 1.0f - (kTwoPi * 5.0f / static_cast<float> (sampleRate));
    std::memset (dcX1_, 0, sizeof (dcX1_));
    std::memset (dcY1_, 0, sizeof (dcY1_));

    // Randomize LFO starting phases so successive IR captures see different
    // modulation state (like VV's stochastic modulation). The evenly-spaced
    // offsets between channels are preserved; only the base phase is random.
    {
        std::random_device rd;
        float basePhase = std::uniform_real_distribution<float> (0.0f, kTwoPi) (rd);
        for (int i = 0; i < N; ++i)
            lfoPhase_[i] = std::fmod (basePhase + kTwoPi * static_cast<float> (i)
                                                         / static_cast<float> (N), kTwoPi);

        // Random PRNG seeds so drift pattern varies per prepare() call
        uint32_t baseSeed = rd();
        for (int i = 0; i < N; ++i)
            lfoPRNG_[i] = baseSeed + static_cast<uint32_t> (i * 1847);
    }

    updateModDepth();
    updateLFORates();
    updateDecayCoefficients();

    // Terminal decay: sample-rate-invariant smoothing coefficients.
    // At 44.1kHz these produce the same behavior as the original constants.
    constexpr float kRmsTauMs     = 45.0f;   // RMS window ~45ms (was 0.9995/0.0005)
    constexpr float kPeakTauMs    = 2270.0f;  // Peak decay ~2.27s (was 0.99999)
    float sr = static_cast<float> (sampleRate);
    rmsAlpha_      = std::exp (-1000.0f / (kRmsTauMs * sr));
    peakDecayAlpha_ = std::exp (-1000.0f / (kPeakTauMs * sr));

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

            // Advance LFO with "Wander" drift (classic reverb technique).
            // Adds ±8% random perturbation to the phase increment each sample
            // so the modulation never exactly repeats — organic, not mechanical.
            float drift = nextDrift (lfoPRNG_[ch]) * lfoPhaseInc_[ch] * 0.08f;
            lfoPhase_[ch] += lfoPhaseInc_[ch] + drift;
            if (lfoPhase_[ch] >= kTwoPi)
                lfoPhase_[ch] -= kTwoPi;
            else if (lfoPhase_[ch] < 0.0f)
                lfoPhase_[ch] += kTwoPi;
        }

        // --- 1.25) Terminal decay: accelerate tail fadeout when below threshold ---
        if (terminalDecayFactor_ < 1.0f && ! frozen_)
        {
            float sampleEnergy = 0.0f;
            for (int ch = 0; ch < N; ++ch)
                sampleEnergy += delayOut[ch] * delayOut[ch];
            sampleEnergy *= (1.0f / static_cast<float> (N));

            currentRMS_ = rmsAlpha_ * currentRMS_ + (1.0f - rmsAlpha_) * sampleEnergy;
            if (currentRMS_ > peakRMS_) peakRMS_ = currentRMS_;
            else peakRMS_ *= peakDecayAlpha_;

            // Compare ratio in linear domain (avoids per-sample log10).
            // terminalDecayThresholdDB_ is stored NEGATIVE (e.g. -40.0f);
            // terminalLinearThreshold_ = 10^4 for -40dB → activates when
            // peak/RMS ratio exceeds threshold (tail has faded).
            float ratio = peakRMS_ / std::max (currentRMS_, 1e-20f);
            terminalDecayActive_ = (ratio > terminalLinearThreshold_)
                                 && (peakRMS_ > 1e-12f);
            if (terminalDecayActive_)
            {
                for (int ch = 0; ch < N; ++ch)
                    delayOut[ch] *= terminalDecayFactor_;
            }
        }

        // --- 1.5) Inline allpass diffusion (Dattorro "decay diffusion") ---
        // Two cascaded Schroeder allpasses per channel. The second stage uses
        // a reduced coefficient (0.8x) to prevent over-diffusion/washiness
        // while multiplying echo density ~4x per feedback cycle.
        // Bypassed during freeze: allpasses alter loop magnitude response over time,
        // causing the frozen tail to spectrally evolve rather than truly sustaining.
        if (inlineDiffCoeff_ > 0.0f && ! frozen_)
        {
            if (useShortInlineAP_)
            {
                for (int ch = 0; ch < N; ++ch)
                    delayOut[ch] = inlineAPShort_[ch].process (delayOut[ch], inlineDiffCoeff_);
            }
            else
            {
                for (int ch = 0; ch < N; ++ch)
                    delayOut[ch] = inlineAP_[ch].process (delayOut[ch], inlineDiffCoeff_);
            }
        }
        if (! useShortInlineAP_ && inlineDiffCoeff2_ > 0.0f && ! frozen_)
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
        if (useHouseholder_)
        {
            // Householder reflection: H = I - (2/N) * ones * ones^T.
            // Moderate mixing avoids eigentone clustering (better for plates).
            std::memcpy (feedback, delayOut, sizeof (feedback));
            if (stereoSplitEnabled_ && dualSlopeFastCount_ == 0)
            {
                householderInPlace8 (feedback);
                householderInPlace8 (feedback + 8);
                if (stereoCoupling_ > 0.0f)
                {
                    float sinC = stereoCoupling_;
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
            else if (dualSlopeFastCount_ == 8)
            {
                householderInPlace8 (feedback);
                householderInPlace8 (feedback + 8);
            }
            else
            {
                householderInPlace16 (feedback);
            }
        }
        else if (stereoSplitEnabled_ && ! usePerturbedMatrix_ && dualSlopeFastCount_ == 0)
        {
            // Stereo split: two independent 8×8 Hadamards for L (0-7) and R (8-15) groups.
            // Each group maintains its own spatial identity with controlled cross-coupling.
            std::memcpy (feedback, delayOut, sizeof (feedback));
            hadamardInPlace8 (feedback);      // L group: channels 0-7
            hadamardInPlace8 (feedback + 8);  // R group: channels 8-15

            // Rotation-based cross-coupling: orthogonal matrix guarantees
            // |L'|²+|R'|² = |L|²+|R|² regardless of signal correlation.
            // stereoCoupling_ = sin(θ), so L→R leakage = 20·log10(c) dB.
            if (stereoCoupling_ > 0.0f)
            {
                float sinC = stereoCoupling_;
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
        else if (usePerturbedMatrix_)
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

        // --- 3) Three-band damping + input injection -> write to delay lines ---
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

            // Structural LF damping: first-order highpass to reduce bass inflation (Room)
            if (structLFEnabled_ && ! frozen_)
            {
                float hp = filtered - structLFState_[ch];
                structLFState_[ch] = (1.0f - structLFCoeff_) * filtered
                                   + structLFCoeff_ * structLFState_[ch];
                filtered = hp;
            }

            // Anti-alias LP + DC blocker: bypass when frozen to prevent tail drift
            if (! frozen_)
            {
                antiAliasState_[ch] = (1.0f - antiAliasCoeff_) * filtered
                                    + antiAliasCoeff_ * antiAliasState_[ch];
                filtered = antiAliasState_[ch];

                float dcOut = filtered - dcX1_[ch] + dcCoeff_ * dcY1_[ch];
                dcX1_[ch] = filtered;
                dcY1_[ch] = dcOut;
                filtered = dcOut;
            }

            // Inject stereo input into delay lines.
            // When stereo split is active: channels 0-7 get L, 8-15 get R (group-based).
            // When disabled (legacy): even channels get L, odd channels get R (interleaved).
            // When frozen, mute new input to keep only the existing tail.
            float inputGain = frozen_ ? 0.0f : 0.25f * inputGainScale_[ch];
            float polarity = (ch & 1) ? -1.0f : 1.0f;
            float inputSample = stereoSplitEnabled_ ? ((ch < 8) ? inL : inR)
                                                    : ((ch & 1) ? inR : inL);
            // Suppress denormal bias when frozen to prevent tail mutation
            float denormalBias = frozen_ ? 0.0f
                                         : (((dl.writePos ^ ch) & 1)
                                                ? DspUtils::kDenormalPrevention
                                                : -DspUtils::kDenormalPrevention);
            dl.buffer[static_cast<size_t> (dl.writePos)] =
                filtered + inputSample * polarity * inputGain + denormalBias;

            dl.writePos = (dl.writePos + 1) & dl.mask;
        }

        // --- 4) Tap decorrelated stereo outputs ---
        float outL = 0.0f, outR = 0.0f;

        if (useMultiPointOutput_)
        {
            // Multi-point output: read from fractional positions within delay lines.
            // Each tap reads at positionFrac * delayLength, producing multiple virtual
            // output paths from the same delay structure — Dattorro-inspired density.
            // Standard multi-point fractional tap reading
            for (int t = 0; t < numMultiTapsL_; ++t)
            {
                const auto& tap = multiTapsL_[t];
                const auto& dl  = delayLines_[tap.channelIndex];
                float readDelay = delayLength_[tap.channelIndex] * tap.positionFrac;
                int   iDelay    = static_cast<int> (readDelay);
                float frac      = readDelay - static_cast<float> (iDelay);
                int   i0        = (dl.writePos - 1 - iDelay) & dl.mask;
                int   i1        = (i0 - 1) & dl.mask;
                float sample    = dl.buffer[static_cast<size_t> (i0)]
                                + frac * (dl.buffer[static_cast<size_t> (i1)]
                                        - dl.buffer[static_cast<size_t> (i0)]);
                outL += sample * tap.sign;
            }
            for (int t = 0; t < numMultiTapsR_; ++t)
            {
                const auto& tap = multiTapsR_[t];
                const auto& dl  = delayLines_[tap.channelIndex];
                float readDelay = delayLength_[tap.channelIndex] * tap.positionFrac;
                int   iDelay    = static_cast<int> (readDelay);
                float frac      = readDelay - static_cast<float> (iDelay);
                int   i0        = (dl.writePos - 1 - iDelay) & dl.mask;
                int   i1        = (i0 - 1) & dl.mask;
                float sample    = dl.buffer[static_cast<size_t> (i0)]
                                + frac * (dl.buffer[static_cast<size_t> (i1)]
                                        - dl.buffer[static_cast<size_t> (i0)]);
                outR += sample * tap.sign;
            }
            // Normalize by 1/N — arithmetic mean of tap reads.
            outL /= static_cast<float> (numMultiTapsL_);
            outR /= static_cast<float> (numMultiTapsR_);
        }
        else
        {
            // Standard 8-tap endpoint output with signed summation.
            // Per-channel outputTapGain_ enables dual-slope: fast channels are boosted
            // to create a loud initial burst, slow channels at unity form the quiet tail.
            for (int t = 0; t < kNumOutputTaps; ++t)
            {
                outL += delayOut[leftTaps_[t]] * leftSigns_[t] * outputTapGain_[leftTaps_[t]] * outputGainScale_[leftTaps_[t]];
                outR += delayOut[rightTaps_[t]] * rightSigns_[t] * outputTapGain_[rightTaps_[t]] * outputGainScale_[rightTaps_[t]];
            }
        }

        // Linear output: 1/sqrt(8) normalization + 6dB level match.
        // sizeCompensation_ = sqrt(sizeScale) normalizes steady-state energy
        // across sizes — shorter delays at small sizes pack more recirculations
        // per unit time, producing higher energy density without compensation.
        // Soft-clip output: fastTanh knee at ~±1.0, scaled by kSafetyClip for headroom.
        // Replaces hard clamp — smoother limiting prevents harsh artifacts on overloads.
        float rawL = outL * kOutputLevel * sizeCompensation_ * lateGainScale_;
        float rawR = outR * kOutputLevel * sizeCompensation_ * lateGainScale_;
        outputL[i] = DspUtils::fastTanh (rawL / kSafetyClip) * kSafetyClip;
        outputR[i] = DspUtils::fastTanh (rawR / kSafetyClip) * kSafetyClip;
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
        {
            structHFState_[i] = 0.0f;
            structLFState_[i] = 0.0f;
        }
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
    float newMin = std::max (min, 0.0f);
    float newMax = std::max (max, newMin);
    if (prepared_)
    {
        newMin = std::min (newMin, sizeRangeAllocatedMax_);
        newMax = std::min (newMax, sizeRangeAllocatedMax_);
    }
    sizeRangeMin_ = newMin;
    sizeRangeMax_ = std::max (newMax, sizeRangeMin_);
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

void FDNReverb::setUseShortInlineAP (bool use)
{
    useShortInlineAP_ = use;
}

void FDNReverb::setMultiPointOutput (const FDNOutputTap* left, int numL,
                                     const FDNOutputTap* right, int numR)
{
    if (left == nullptr || numL <= 0 || right == nullptr || numR <= 0)
    {
        useMultiPointOutput_ = false;
        numMultiTapsL_ = 0;
        numMultiTapsR_ = 0;
        return;
    }
    numMultiTapsL_ = std::min (numL, static_cast<int> (kMaxMultiTaps));
    numMultiTapsR_ = std::min (numR, static_cast<int> (kMaxMultiTaps));
    for (int i = 0; i < numMultiTapsL_; ++i)
    {
        if (left[i].channelIndex < 0 || left[i].channelIndex >= N)
        {
            useMultiPointOutput_ = false;
            numMultiTapsL_ = 0;
            numMultiTapsR_ = 0;
            return;
        }
        multiTapsL_[i] = left[i];
    }
    for (int i = 0; i < numMultiTapsR_; ++i)
    {
        if (right[i].channelIndex < 0 || right[i].channelIndex >= N)
        {
            useMultiPointOutput_ = false;
            numMultiTapsL_ = 0;
            numMultiTapsR_ = 0;
            return;
        }
        multiTapsR_[i] = right[i];
    }
    useMultiPointOutput_ = true;
}

void FDNReverb::setMultiPointDensity (int tapsPerChannel)
{
    int totalTaps = tapsPerChannel * N;
    if (totalTaps <= 0 || totalTaps > kMaxMultiTaps)
    {
        useMultiPointOutput_ = false;
        numMultiTapsL_ = 0;
        numMultiTapsR_ = 0;
        return;
    }

    numMultiTapsL_ = totalTaps;
    numMultiTapsR_ = totalTaps;

    // Generate evenly-spaced fractional positions with alternating signs.
    // L and R use offset positions for stereo decorrelation.
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
            float signR = (t % 2 == 1) ? 1.0f : -1.0f;  // opposite pattern

            multiTapsL_[idx] = { ch, posL, signL };
            multiTapsR_[idx] = { ch, posR, signR };
        }
    }
    useMultiPointOutput_ = true;
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

    // Project to nearest orthogonal matrix via iterative polar decomposition
    // (Newton's method): M_{k+1} = 0.5 * (M_k + M_k^{-T}).
    // This guarantees a unitary mixing matrix, preventing energy drift in
    // freeze mode. Row normalization alone doesn't ensure column orthogonality.
    // For a 16x16 nearly-orthogonal matrix, 4 iterations suffice for convergence.
    constexpr int kPolarIters = 4;

    for (int iter = 0; iter < kPolarIters; ++iter)
    {
        // Copy current matrix M
        float M[N][N];
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                M[i][j] = perturbMatrix_[i][j];

        // Compute M^{-1} via Gauss-Jordan elimination on [M | I]
        float aug[N][2 * N];
        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                aug[i][j] = M[i][j];
                aug[i][j + N] = (i == j) ? 1.0f : 0.0f;
            }
        }

        for (int col = 0; col < N; ++col)
        {
            // Partial pivoting for numerical stability
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
                break; // Singular — bail out (shouldn't happen for perturbed Hadamard)

            float invDiag = 1.0f / diag;
            for (int k = 0; k < 2 * N; ++k)
                aug[col][k] *= invDiag;

            for (int row = 0; row < N; ++row)
            {
                if (row == col)
                    continue;
                float factor = aug[row][col];
                for (int k = 0; k < 2 * N; ++k)
                    aug[row][k] -= factor * aug[col][k];
            }
        }

        // M^{-1} is now in aug[i][j+N]. We need M^{-T} (transpose of inverse).
        // Compute M_{k+1} = 0.5 * (M + M^{-T})
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                perturbMatrix_[i][j] = 0.5f * (M[i][j] + aug[j][i + N]);
    }
}

void FDNReverb::setUseHouseholder (bool enable)
{
    useHouseholder_ = enable;
}

void FDNReverb::setUseWeightedGains (bool enable)
{
    useWeightedGains_ = enable;
    if (prepared_)
        updateDelayLengths();
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

void FDNReverb::setStereoCoupling (float amount)
{
    // Negative values disable stereo split (use full 16×16 Hadamard).
    // Zero or positive enables split; zero coupling = fully independent L/R (no leakage).
    if (amount < 0.0f)
    {
        stereoSplitEnabled_ = false;
        stereoCoupling_ = 0.0f;
    }
    else
    {
        stereoSplitEnabled_ = true;
        stereoCoupling_ = std::clamp (amount, 0.0f, 0.75f);
    }
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

void FDNReverb::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::clamp (hz, 1000.0f, 20000.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void FDNReverb::setAirTrebleMultiply (float mult)
{
    airTrebleMultiply_ = std::clamp (mult, 0.1f, 1.5f);
    if (prepared_)
        updateDecayCoefficients();
}

void FDNReverb::setStructuralHFDamping (float baseFreqHz, float trebleMultiply)
{
    structHFBaseFreq_ = baseFreqHz;
    if (baseFreqHz <= 0.0f)
    {
        structHFEnabled_ = false;
        structHFCoeff_ = 0.0f;
        return;
    }
    // Inverted treble scaling: dark presets (low treble) already have strong TwoBandDamping,
    // so structural damping is reduced (higher effectiveHz). Bright presets (high treble) have
    // weaker TwoBandDamping, so they get full structural damping (effectiveHz = baseFreqHz).
    // At treble=1.0: effectiveHz = baseFreqHz (full structural damping).
    // At treble=0.5: effectiveHz = baseFreqHz * 1.25 (reduced damping for dark presets).
    // At treble=0.1: effectiveHz = baseFreqHz * 1.45 (minimal damping for very dark presets).
    float effectiveHz = baseFreqHz * (1.5f - std::clamp (trebleMultiply, 0.1f, 1.0f) * 0.5f);
    structHFEnabled_ = true;
    structHFCoeff_ = std::exp (-kTwoPi * effectiveHz / static_cast<float> (sampleRate_));
}

void FDNReverb::setStructuralLFDamping (float hz)
{
    if (hz <= 0.0f)
    {
        structLFEnabled_ = false;
        structLFCoeff_ = 0.0f;
        return;
    }
    structLFEnabled_ = true;
    structLFCoeff_ = std::exp (-kTwoPi * hz / static_cast<float> (sampleRate_));
}

void FDNReverb::setCrossoverModDepth (float depth)
{
    crossoverModDepth_ = std::clamp (depth, 0.0f, 1.0f);
}

void FDNReverb::setDecayBoost (float boost)
{
    decayBoost_ = std::clamp (boost, 0.3f, 2.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void FDNReverb::setTerminalDecay (float thresholdDB, float factor)
{
    terminalDecayThresholdDB_ = -std::abs (thresholdDB);
    terminalDecayFactor_ = std::clamp (factor, 0.0f, 1.0f);
    // Precompute linear threshold to avoid per-sample log10
    terminalLinearThreshold_ = std::pow (10.0f, -terminalDecayThresholdDB_ * 0.1f);
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
        // Deterministic LFO/PRNG reset for clean state on algorithm switch.
        // (Wrapper prepare() no longer calls clearBuffers(), so this does not
        // conflict with prepare()'s stochastic randomization.)
        lfoPhase_[i] = 0.0f;
        lfoPRNG_[i] = static_cast<uint32_t> (i + 1) * 2654435761u;
    }
    // Reset terminal decay RMS tracking
    peakRMS_ = 0.0f;
    currentRMS_ = 0.0f;
    terminalDecayActive_ = false;
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

    // Per-channel input/output gain scaling for uniform modal excitation.
    // Weight by 1/sqrt(delay_length / min_delay) so shorter delays (which recirculate
    // more often) get higher gain, compensating for their naturally lower energy
    // accumulation per pass. This flattens the spectral envelope of the reverb tail.
    if (useWeightedGains_)
    {
        float minDelay = delayLength_[0];
        for (int i = 1; i < N; ++i)
            minDelay = std::min (minDelay, delayLength_[i]);

        float sumSqIn = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            inputGainScale_[i] = 1.0f / std::sqrt (delayLength_[i] / minDelay);
            sumSqIn += inputGainScale_[i] * inputGainScale_[i];
        }
        // Normalize so RMS of gain vector equals 1 (preserves overall input energy)
        float normIn = std::sqrt (static_cast<float> (N) / sumSqIn);
        for (int i = 0; i < N; ++i)
            inputGainScale_[i] *= normIn;

        // Output gains: same weighting (symmetry for spectral flatness)
        for (int i = 0; i < N; ++i)
            outputGainScale_[i] = inputGainScale_[i];
    }
    else
    {
        for (int i = 0; i < N; ++i)
        {
            inputGainScale_[i] = 1.0f;
            outputGainScale_[i] = 1.0f;
        }
    }
}

void FDNReverb::updateDecayCoefficients()
{
    // Low crossover: bass/mid split (user-controlled crossover frequency, ~633Hz)
    float lowCrossoverCoeff = std::exp (-kTwoPi * crossoverFreq_
                                        / static_cast<float> (sampleRate_));

    // High crossover: mid/air split (per-algorithm, e.g. Room=6kHz, others=20kHz)
    float highCrossoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_
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
            if (useShortInlineAP_)
                effectiveLength += static_cast<float> (kInlineAPDelaysShort[i]) * rateRatio;
            else
                effectiveLength += static_cast<float> (kInlineAPDelays[i]) * rateRatio;
            if (! useShortInlineAP_ && inlineDiffCoeff2_ > 0.0f)
                effectiveLength += static_cast<float> (kInlineAPDelays2[i]) * rateRatio;
            if (inlineDiffCoeff3_ > 0.0f)
                effectiveLength += static_cast<float> (kInlineAPDelays3[i]) * rateRatio;
        }
        float gBase = std::pow (10.0f, -3.0f * effectiveLength
                                       / (channelRT60 * static_cast<float> (sampleRate_)));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);

        // Bass Multiply: g_low = g_base^(1/bassMultiply)
        // bassMultiply > 1.0 → lows sustain longer (g_low > g_base)
        float gLow = std::pow (gBase, 1.0f / bassMultiply_);

        // Mid band (lowCrossover..highCrossover): same as old gHigh formula.
        // Preserves the matched 4kHz RT60 from TwoBandDamping calibration.
        float gMid = std::pow (gBase, 1.0f / trebleMultiply_);

        // Air band (> highCrossover): independent damping via airTrebleMultiply_.
        // gHigh = gBase^(1/airTrebleMultiply_): lower values = faster decay.
        // At airTrebleMultiply_=1.0: gHigh = gBase (natural rate, no extra damping).
        // At airTrebleMultiply_=0.70: gHigh = gBase^1.43 (significant extra damping).
        float gHigh = std::pow (gBase, 1.0f / std::max (airTrebleMultiply_, 0.01f));

        dampFilter_[i].setCoefficients (gLow, gMid, gHigh, lowCrossoverCoeff, highCrossoverCoeff);
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

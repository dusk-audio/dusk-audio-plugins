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

    // Evenly-spaced initial LFO phases
    for (int i = 0; i < N; ++i)
        lfoPhase_[i] = kTwoPi * static_cast<float> (i) / static_cast<float> (N);

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
        const float monoIn = (inputL[i] + inputR[i]) * 0.5f;

        // --- 1) Read from all delay lines with LFO-modulated fractional position ---
        float delayOut[N];
        for (int ch = 0; ch < N; ++ch)
        {
            auto& dl = delayLines_[ch];

            float mod = std::sin (lfoPhase_[ch]) * modDepthSamples_;
            float readDelay = delayLength_[ch] + mod;
            float readPos = static_cast<float> (dl.writePos) - readDelay;

            int intIdx = static_cast<int> (std::floor (readPos));
            float frac = readPos - static_cast<float> (intIdx);

            delayOut[ch] = DspUtils::cubicHermite (dl.buffer.data(), dl.mask, intIdx, frac);

            // Advance LFO
            lfoPhase_[ch] += lfoPhaseInc_[ch];
            if (lfoPhase_[ch] >= kTwoPi)
                lfoPhase_[ch] -= kTwoPi;
        }

        // --- 2) Hadamard feedback mixing ---
        float feedback[N];
        std::memcpy (feedback, delayOut, sizeof (feedback));
        hadamardInPlace16 (feedback);

        // --- 3) Two-band damping + input injection -> write to delay lines ---
        for (int ch = 0; ch < N; ++ch)
        {
            auto& dl = delayLines_[ch];

            // When frozen, bypass damping (unity feedback) to sustain tail indefinitely
            float filtered = frozen_ ? feedback[ch] : dampFilter_[ch].process (feedback[ch]);

            // Inject input scaled by 1/sqrt(N) with alternating polarity.
            // When frozen, mute new input to keep only the existing tail.
            float inputGain = frozen_ ? 0.0f : 0.25f;
            float polarity = (ch & 1) ? -1.0f : 1.0f;
            float denormalBias = ((dl.writePos ^ ch) & 1)
                                     ? DspUtils::kDenormalPrevention
                                     : -DspUtils::kDenormalPrevention;
            dl.buffer[static_cast<size_t> (dl.writePos)] =
                filtered + monoIn * polarity * inputGain + denormalBias;

            dl.writePos = (dl.writePos + 1) & dl.mask;
        }

        // --- 4) Tap decorrelated stereo outputs with signed summation ---
        float outL = 0.0f, outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
        {
            outL += delayOut[leftTaps_[t]] * leftSigns_[t];
            outR += delayOut[rightTaps_[t]] * rightSigns_[t];
        }

        // Fast tanh soft-clips the normalized output (prevents runaway at long decays),
        // then kOutputGain compensates for the conservative 1/sqrt(8) normalization.
        outputL[i] = DspUtils::fastTanh (outL * kOutputScale) * kOutputGain;
        outputR[i] = DspUtils::fastTanh (outR * kOutputScale) * kOutputGain;
    }
}

// ---------------------------------------------------------------------------
void FDNReverb::setDecayTime (float seconds)
{
    decayTime_ = std::clamp (seconds, 0.2f, 30.0f);
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
    modDepthSamples_ = modDepth_ * 4.0f;
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

// ---------------------------------------------------------------------------
void FDNReverb::updateDelayLengths()
{
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);

    for (int i = 0; i < N; ++i)
        delayLength_[i] = static_cast<float> (baseDelays_[i]) * rateRatio * sizeScale;
}

void FDNReverb::updateDecayCoefficients()
{
    // Crossover lowpass coefficient: c = exp(-2*pi*fc/sr)
    float crossoverCoeff = std::exp (-kTwoPi * crossoverFreq_
                                     / static_cast<float> (sampleRate_));

    for (int i = 0; i < N; ++i)
    {
        // Per-delay feedback gain for desired RT60
        // g_base = 10^(-3 * L / (RT60 * sr)) so that after RT60 seconds, signal is at -60 dB
        float gBase = std::pow (10.0f, -3.0f * delayLength_[i]
                                       / (decayTime_ * static_cast<float> (sampleRate_)));

        // Bass Multiply: g_low = g_base^(1/bassMultiply)
        // bassMultiply > 1.0 → lows sustain longer (g_low > g_base)
        float gLow = std::pow (gBase, 1.0f / bassMultiply_);

        // Treble Multiply: g_high = g_base^(1/trebleMultiply)
        // trebleMultiply < 1.0 → highs decay faster (g_high < g_base)
        float gHigh = std::pow (gBase, 1.0f / trebleMultiply_);

        dampFilter_[i].setCoefficients (gLow, gHigh, crossoverCoeff);
    }
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

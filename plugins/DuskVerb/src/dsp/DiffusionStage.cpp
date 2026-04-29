#include "DiffusionStage.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

// MSVC's <cmath> doesn't expose M_PI by default (it's a GNU extension).
// Define a local constexpr to keep this file portable across MSVC / GCC /
// Clang without forcing every translation unit to set _USE_MATH_DEFINES.
namespace { constexpr float kPi = 3.14159265358979323846f; }

// ==========================================================================
// ModulatedAllpass
// ==========================================================================

void ModulatedAllpass::prepare (int bufferSize, float delayInSamples, float lfoRateHz,
                                float lfoDepthSamples, float lfoStartPhase, double sampleRate)
{
    buffer_.assign (static_cast<size_t> (bufferSize), 0.0f);
    mask_ = bufferSize - 1;
    writePos_ = 0;
    delaySamples_ = delayInSamples;
    baseLfoDepth_ = lfoDepthSamples;   // retain for later setLfoDepthScale()
    lfoDepth_     = lfoDepthSamples;
    lfoPhase_ = lfoStartPhase;
    lfoPhaseInc_ = kTwoPi * lfoRateHz / static_cast<float> (sampleRate);
    sampleRate_ = static_cast<float> (sampleRate);
    jitterDepthFraction_ = 0.0f;
}

void ModulatedAllpass::enableJitter (float fraction, std::uint32_t seed)
{
    jitterDepthFraction_ = std::max (0.0f, fraction);
    if (jitterDepthFraction_ <= 0.0f || delaySamples_ <= 0.0f) return;
    jitterLFO_.prepare (sampleRate_, seed);
    jitterLFO_.setDepth (delaySamples_ * jitterDepthFraction_);
    // Period = 2 × delay so jitter traverses range within one ring period.
    const float period    = 2.0f * delaySamples_;
    const float lfoRateHz = sampleRate_ / period;
    jitterLFO_.setRate (std::min (std::max (lfoRateHz, 5.0f), 200.0f));
}

float ModulatedAllpass::process (float input, float g)
{
    // Modulated read position: sine LFO + optional spin-and-wander jitter.
    // The jitter (per-AP RandomWalkLFO at delay-proportional rate) breaks
    // the AP's modal phase-locking that the slow sine LFO can't disrupt.
    float mod = std::sin (lfoPhase_) * lfoDepth_;
    if (jitterDepthFraction_ > 0.0f)
        mod += jitterLFO_.next();
    float readDelay = delaySamples_ + mod;
    float readPos = static_cast<float> (writePos_) - readDelay;

    int intIdx = static_cast<int> (std::floor (readPos));
    float frac = readPos - static_cast<float> (intIdx);

    // Wrap to non-negative range for safe bitwise masking in cubicHermite
    intIdx = static_cast<int> (static_cast<unsigned int> (intIdx) & static_cast<unsigned int> (mask_));

    // Read delayed value with cubic Hermite interpolation
    float vd = DspUtils::cubicHermite (buffer_.data(), mask_, intIdx, frac);

    // Schroeder allpass: s[n] = x[n] + g*s[n-D],  y[n] = s[n-D] - g*s[n]
    // Alternating-sign bias prevents denormal accumulation without adding DC.
    float vn = input + g * vd;
    float denormalBias = (writePos_ & 1) ? DspUtils::kDenormalPrevention
                                         : -DspUtils::kDenormalPrevention;
    buffer_[static_cast<size_t> (writePos_)] = vn + denormalBias;
    writePos_ = (writePos_ + 1) & mask_;

    float output = vd - g * vn;

    // Advance LFO
    lfoPhase_ += lfoPhaseInc_;
    if (lfoPhase_ >= kTwoPi)
        lfoPhase_ -= kTwoPi;

    return output;
}

void ModulatedAllpass::clear()
{
    std::fill (buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
}

// ==========================================================================
// DiffusionStage
// ==========================================================================

void DiffusionStage::prepare (double sampleRate, int /*maxBlockSize*/)
{
    static constexpr float kTwoPi = 6.283185307179586f;
    float ratio = static_cast<float> (sampleRate / DspUtils::kBaseSampleRate);

    for (int s = 0; s < kNumStages; ++s)
    {
        float delay = static_cast<float> (kBaseDelays[s]) * ratio;
        int bufSize = DspUtils::nextPowerOf2 (static_cast<int> (std::ceil (delay)) + 4);

        // Left channel allpass (index s of 8 total)
        // LFO depth scaled by sample rate ratio so modulation is consistent
        // across 44.1/48/96kHz (depth is in samples, represents a fixed time)
        float phaseL = kTwoPi * static_cast<float> (s) / 8.0f;
        float rateL  = 0.3f + 0.5f * static_cast<float> (s) / 7.0f;
        float depthL = (0.5f + 1.0f * static_cast<float> (s) / 7.0f) * ratio;
        leftAP_[s].prepare (bufSize, delay, rateL, depthL, phaseL, sampleRate);

        // Right channel allpass — SAME rate and depth as left, but offset
        // phase. Original code used fully-asymmetric (rate, depth, phase) per
        // L/R, which let the channel modulators drift at different speeds —
        // measured LR-correlation stddev wandered ~0.066 (vs Arturia 0.028).
        // Locking the rate keeps the modulation cycle in phase between L
        // and R, so the late-field correlation stays stable. The π-offset
        // phase still gives a clear stereo image at any moment in time
        // without introducing slow wander.
        float phaseR = phaseL + kPi;
        rightAP_[s].prepare (bufSize, delay, rateL, depthL, phaseR, sampleRate);

        // Spin-and-wander jitter (1.5 % of delay) — lets us keep the long
        // Dattorro-canonical delays {142, 107, 379, 277} for proper input
        // smear width without the 8.6 ms ringing they produce as static APs.
        const std::uint32_t lJitterSeed = 0xA5A5A5A5u + static_cast<std::uint32_t> (s * 31337);
        const std::uint32_t rJitterSeed = 0x5A5A5A5Au + static_cast<std::uint32_t> (s * 27449);
        leftAP_[s] .enableJitter (0.015f, lJitterSeed);
        rightAP_[s].enableJitter (0.015f, rJitterSeed);
    }
}

void DiffusionStage::process (float* left, float* right, int numSamples)
{
    // Snapshot coefficients for thread safety (setters may write from another thread)
    const float coeff12 = diffusionCoeff12_;
    const float coeff34 = diffusionCoeff34_;

    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i];
        float r = right[i];

        for (int s = 0; s < kNumStages; ++s)
        {
            float g = (s < 2) ? coeff12 : coeff34;
            l = leftAP_[s].process (l, g);
            r = rightAP_[s].process (r, g);
        }

        left[i] = l;
        right[i] = r;
    }
}

void DiffusionStage::setDiffusion (float amount)
{
    float a = std::clamp (amount, 0.0f, 1.0f);
    lastDiffusionAmount_ = a;
    diffusionCoeff12_ = a * maxCoeff12_;
    diffusionCoeff34_ = a * maxCoeff34_;
}

void DiffusionStage::setMaxCoefficients (float max12, float max34)
{
    // Allpass stability requires |g| < 1
    maxCoeff12_ = std::clamp (max12, -0.999f, 0.999f);
    maxCoeff34_ = std::clamp (max34, -0.999f, 0.999f);
    // Re-apply current diffusion amount with new max coefficients
    setDiffusion (lastDiffusionAmount_);
}

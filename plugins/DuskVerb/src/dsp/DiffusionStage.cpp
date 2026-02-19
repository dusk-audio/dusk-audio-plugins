#include "DiffusionStage.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

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
    lfoDepth_ = lfoDepthSamples;
    lfoPhase_ = lfoStartPhase;
    lfoPhaseInc_ = kTwoPi * lfoRateHz / static_cast<float> (sampleRate);
}

float ModulatedAllpass::process (float input, float g)
{
    // Modulated read position
    float mod = std::sin (lfoPhase_) * lfoDepth_;
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
    float ratio = static_cast<float> (sampleRate / 44100.0);

    for (int s = 0; s < kNumStages; ++s)
    {
        float delay = static_cast<float> (kBaseDelays[s]) * ratio;
        int bufSize = DspUtils::nextPowerOf2 (static_cast<int> (std::ceil (delay)) + 4);

        // Left channel allpass (index s of 8 total)
        float phaseL = kTwoPi * static_cast<float> (s) / 8.0f;
        float rateL  = 0.3f + 0.5f * static_cast<float> (s) / 7.0f;
        float depthL = 0.5f + 1.0f * static_cast<float> (s) / 7.0f;
        leftAP_[s].prepare (bufSize, delay, rateL, depthL, phaseL, sampleRate);

        // Right channel allpass (index s+4 of 8 total, different phase/rate/depth)
        int ri = s + kNumStages;
        float phaseR = kTwoPi * static_cast<float> (ri) / 8.0f;
        float rateR  = 0.3f + 0.5f * static_cast<float> (ri) / 7.0f;
        float depthR = 0.5f + 1.0f * static_cast<float> (ri) / 7.0f;
        rightAP_[s].prepare (bufSize, delay, rateR, depthR, phaseR, sampleRate);
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

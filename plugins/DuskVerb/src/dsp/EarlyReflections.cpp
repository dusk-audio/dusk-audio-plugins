#include "EarlyReflections.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

void EarlyReflections::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    int maxSamples = static_cast<int> (std::ceil (kMaxBufferMs * 0.001f
                                                  * static_cast<float> (sampleRate))) + 1;
    int bufSize = DspUtils::nextPowerOf2 (maxSamples);

    bufferL_.assign (static_cast<size_t> (bufSize), 0.0f);
    bufferR_.assign (static_cast<size_t> (bufSize), 0.0f);
    writePos_ = 0;
    mask_ = bufSize - 1;

    double rateScale = sampleRate / DspUtils::kBaseSampleRate;
    decorr_L1_.init (std::max (1, static_cast<int> (139 * rateScale)));
    decorr_L2_.init (std::max (1, static_cast<int> (193 * rateScale)));
    decorr_R1_.init (std::max (1, static_cast<int> (167 * rateScale)));
    decorr_R2_.init (std::max (1, static_cast<int> (211 * rateScale)));

    updateTaps();

    prepared_ = true;
}

void EarlyReflections::process (const float* inputL, const float* inputR,
                                float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    if (tapsNeedUpdate_.exchange (false, std::memory_order_acquire))
        updateTaps();

    float localDecorrCoeff = decorrCoeff_.load (std::memory_order_acquire);

    for (int i = 0; i < numSamples; ++i)
    {
        bufferL_[static_cast<size_t> (writePos_)] = inputL[i];
        bufferR_[static_cast<size_t> (writePos_)] = inputR[i];

        float outL = 0.0f, outR = 0.0f;

        for (int t = 0; t < kNumTaps; ++t)
        {
            int readL = static_cast<int> ((static_cast<unsigned> (writePos_)
                       - static_cast<unsigned> (tapsL_[t].delaySamples))
                       & static_cast<unsigned> (mask_));
            float tapL = bufferL_[static_cast<size_t> (readL)] * tapsL_[t].gain;
            tapsL_[t].lpState = (1.0f - tapsL_[t].lpCoeff) * tapL
                              + tapsL_[t].lpCoeff * tapsL_[t].lpState
                              + DspUtils::kDenormalPrevention;
            outL += tapsL_[t].lpState;

            int readR = static_cast<int> ((static_cast<unsigned> (writePos_)
                       - static_cast<unsigned> (tapsR_[t].delaySamples))
                       & static_cast<unsigned> (mask_));
            float tapR = bufferR_[static_cast<size_t> (readR)] * tapsR_[t].gain;
            tapsR_[t].lpState = (1.0f - tapsR_[t].lpCoeff) * tapR
                              + tapsR_[t].lpCoeff * tapsR_[t].lpState
                              + DspUtils::kDenormalPrevention;
            outR += tapsR_[t].lpState;
        }

        if (localDecorrCoeff > 0.0f)
        {
            outL = decorr_L1_.process (outL, localDecorrCoeff);
            outL = decorr_L2_.process (outL, localDecorrCoeff);
            outR = decorr_R1_.process (outR, localDecorrCoeff);
            outR = decorr_R2_.process (outR, localDecorrCoeff);
        }

        outputL[i] = outL;
        outputR[i] = outR;

        writePos_ = (writePos_ + 1) & mask_;
    }
}

void EarlyReflections::setSize (float size)
{
    erSize_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setTimeScale (float scale)
{
    timeScale_ = std::clamp (scale, 0.1f, 1.5f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setGainExponent (float exponent)
{
    gainExponent_ = std::clamp (exponent, 0.0f, 2.0f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setAirAbsorptionFloor (float hz)
{
    airAbsorptionFloorHz_ = std::clamp (hz, 1000.0f, 12000.0f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setAirAbsorptionCeiling (float hz)
{
    airAbsorptionCeilingHz_ = std::clamp (hz, 8000.0f, 20000.0f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setDecorrCoeff (float coeff)
{
    decorrCoeff_.store (std::clamp (coeff, 0.0f, 0.7f), std::memory_order_release);
}

void EarlyReflections::updateTaps()
{
    static_assert (kNumTaps > 1, "kNumTaps must be > 1 to avoid division by zero");
    float sizeScale = (0.3f + 0.7f * erSize_) * timeScale_;
    float sr = static_cast<float> (sampleRate_);
    float timeRatio = kMaxTimeMs / kMinTimeMs;

    for (int i = 0; i < kNumTaps; ++i)
    {
        float tL = static_cast<float> (i) / static_cast<float> (kNumTaps - 1);
        float timeMsL = kMinTimeMs * std::pow (timeRatio, tL) * sizeScale;

        float tR = (static_cast<float> (i) + 0.37f)
                 / (static_cast<float> (kNumTaps - 1) + 0.37f);
        float timeMsR = kMinTimeMs * std::pow (timeRatio, tR) * sizeScale;

        tapsL_[i].delaySamples = std::max (1, static_cast<int> (timeMsL * 0.001f * sr));
        tapsR_[i].delaySamples = std::max (1, static_cast<int> (timeMsR * 0.001f * sr));

        float normL = timeMsL / kMinTimeMs;
        float normR = timeMsR / kMinTimeMs;
        float attenL = (gainExponent_ > 0.0f) ? std::pow (normL, gainExponent_) : 1.0f;
        float attenR = (gainExponent_ > 0.0f) ? std::pow (normR, gainExponent_) : 1.0f;
        tapsL_[i].gain = kSignsL[i] / attenL;
        tapsR_[i].gain = kSignsR[i] / attenR;

        float cutoffL = airAbsorptionCeilingHz_ * std::pow (airAbsorptionFloorHz_ / airAbsorptionCeilingHz_, tL);
        float cutoffR = airAbsorptionCeilingHz_ * std::pow (airAbsorptionFloorHz_ / airAbsorptionCeilingHz_, tR);
        tapsL_[i].lpCoeff = std::exp (-kTwoPi * cutoffL / sr);
        tapsR_[i].lpCoeff = std::exp (-kTwoPi * cutoffR / sr);
    }

    float sumL = 0.0f, sumR = 0.0f;
    for (int i = 0; i < kNumTaps; ++i)
    {
        sumL += std::abs (tapsL_[i].gain);
        sumR += std::abs (tapsR_[i].gain);
    }
    if (sumL > 0.0f)
        for (int i = 0; i < kNumTaps; ++i)
            tapsL_[i].gain /= sumL;
    if (sumR > 0.0f)
        for (int i = 0; i < kNumTaps; ++i)
            tapsR_[i].gain /= sumR;
}

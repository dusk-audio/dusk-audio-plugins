#include "HallTrueLex16Reverb.h"

#include "DspUtils.h"

#include <algorithm>
#include <cmath>

constexpr float HallTrueLex16Reverb::kERTapTimesMs[HallTrueLex16Reverb::kNumERTaps];
constexpr float HallTrueLex16Reverb::kERSignL[HallTrueLex16Reverb::kNumERTaps];
constexpr float HallTrueLex16Reverb::kERSignR[HallTrueLex16Reverb::kNumERTaps];
constexpr int   HallTrueLex16Reverb::kAPPrimesL[HallTrueLex16Reverb::kAPStages];
constexpr int   HallTrueLex16Reverb::kAPPrimesR[HallTrueLex16Reverb::kAPStages];

void HallTrueLex16Reverb::ERTDL::prepare (double sr)
{
    const int target = static_cast<int> (std::round (
        (kERTapTimesMs[kNumERTaps - 1] + 10.0f) * sr / 1000.0f));
    const int bufSize = DspUtils::nextPowerOf2 (std::max (target, 64));
    bufL.assign (static_cast<size_t> (bufSize), 0.0f);
    bufR.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
    for (int i = 0; i < kNumERTaps; ++i)
        tapSamples[i] = std::min (
            static_cast<int> (std::round (kERTapTimesMs[i] * sr / 1000.0f)),
            bufSize - 1);
}

void HallTrueLex16Reverb::ERTDL::clear()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    writePos = 0;
}

void HallTrueLex16Reverb::ERTDL::process (float inL, float inR,
                                          float& outL, float& outR)
{
    bufL[static_cast<size_t> (writePos)] = inL;
    bufR[static_cast<size_t> (writePos)] = inR;

    float sL = 0.0f, sR = 0.0f;
    for (int i = 0; i < kNumERTaps; ++i)
    {
        const int r = (writePos - tapSamples[i]) & mask;
        const float vL = bufL[static_cast<size_t> (r)];
        const float vR = bufR[static_cast<size_t> (r)];
        sL += vL * weights[i] * kERSignL[i];
        sR += vR * weights[i] * kERSignR[i];
    }

    writePos = (writePos + 1) & mask;
    outL = sL;
    outR = sR;
}

void HallTrueLex16Reverb::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = std::max (1, maxBlockSize);

    tank.prepare (sampleRate, maxBlockSize_);
    erTDL_.prepare (sampleRate);

    auto prepAP = [sampleRate] (APStage& ap, int basePrime)
    {
        const float rateRatio = static_cast<float> (sampleRate / 44100.0);
        const int target = static_cast<int> (std::round (
            static_cast<float> (basePrime) * rateRatio));
        const int bufSize = DspUtils::nextPowerOf2 (std::max (target + 4, 16));
        ap.buf.assign (static_cast<size_t> (bufSize), 0.0f);
        ap.mask = bufSize - 1;
        ap.writePos = 0;
        ap.delaySamples = std::min (target, bufSize - 1);
    };
    for (int s = 0; s < kAPStages; ++s)
    {
        prepAP (apL_[s], kAPPrimesL[s]);
        prepAP (apR_[s], kAPPrimesR[s]);
    }

    erOutL_.assign (static_cast<size_t> (maxBlockSize_), 0.0f);
    erOutR_.assign (static_cast<size_t> (maxBlockSize_), 0.0f);
    tankOutL_.assign (static_cast<size_t> (maxBlockSize_), 0.0f);
    tankOutR_.assign (static_cast<size_t> (maxBlockSize_), 0.0f);

    prepared_ = true;
}

void HallTrueLex16Reverb::clearBuffers()
{
    tank.clearBuffers();
    erTDL_.clear();
    for (int s = 0; s < kAPStages; ++s)
    {
        apL_[s].clear();
        apR_[s].clear();
    }
    std::fill (erOutL_.begin(), erOutL_.end(), 0.0f);
    std::fill (erOutR_.begin(), erOutR_.end(), 0.0f);
    std::fill (tankOutL_.begin(), tankOutL_.end(), 0.0f);
    std::fill (tankOutR_.begin(), tankOutR_.end(), 0.0f);
}

void HallTrueLex16Reverb::process (const float* inL, const float* inR,
                                   float* outL, float* outR, int numSamples)
{
    if (! prepared_ || numSamples <= 0) return;
    if (numSamples > maxBlockSize_)
    {
        std::fill (outL, outL + numSamples, 0.0f);
        std::fill (outR, outR + numSamples, 0.0f);
        return;
    }

    // 1) ER taps — parallel branch direct from input.
    for (int n = 0; n < numSamples; ++n)
        erTDL_.process (inL[n], inR[n], erOutL_[(size_t) n], erOutR_[(size_t) n]);

    // 2) Tank (16-ch Hadamard FDN). No specular taps inside FDNReverb
    //    by design (Engine 4 doesn't ship them) — ER above is the sole
    //    early-energy carrier.
    tank.process (inL, inR, tankOutL_.data(), tankOutR_.data(), numSamples);

    // 3) AP cascade routed to tank only (Engine 13 pattern). Smears
    //    modal crest without touching ER tap peaks.
    const float erG   = erLevel_;
    const float tankG = tankLevel_;
    const float g     = apG_;
    for (int n = 0; n < numSamples; ++n)
    {
        float smearedTankL = tankOutL_[(size_t) n] * tankG;
        float smearedTankR = tankOutR_[(size_t) n] * tankG;
        for (int s = 0; s < kAPStages; ++s)
        {
            smearedTankL = apL_[s].process (smearedTankL, g);
            smearedTankR = apR_[s].process (smearedTankR, g);
        }

        outL[n] = erOutL_[(size_t) n] * erG + smearedTankL;
        outR[n] = erOutR_[(size_t) n] * erG + smearedTankR;
    }
}

void HallTrueLex16Reverb::setERWeight (int idx, float w)
{
    if (idx < 0 || idx >= kNumERTaps) return;
    erTDL_.weights[idx] = std::clamp (w, 0.0f, 4.0f);
}

void HallTrueLex16Reverb::setERLevel (float level)
{
    erLevel_ = std::clamp (level, 0.0f, 4.0f);
}

void HallTrueLex16Reverb::setTankLevel (float level)
{
    tankLevel_ = std::clamp (level, 0.0f, 4.0f);
}

void HallTrueLex16Reverb::setAPCoeff (float g)
{
    apG_ = std::clamp (g, -0.85f, 0.85f);
}

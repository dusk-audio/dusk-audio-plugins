#include "OutputDiffusion.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

void OutputDiffusion::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    prepared_   = true;
    buildStages();
}

void OutputDiffusion::buildStages()
{
    static constexpr float kTwoPi = 6.283185307179586f;
    float ratio = static_cast<float> (sampleRate_ / DspUtils::kBaseSampleRate);

    float totalAPs = static_cast<float> (kNumStages * 2);

    for (int s = 0; s < kNumStages; ++s)
    {
        // delayScale_ spreads the base allpass delays so the smearing reaches
        // the closely-spaced HF modes (longer delay = finer frequency comb).
        float delay = static_cast<float> (kBaseDelays[s]) * ratio * delayScale_;
        int bufSize = DspUtils::nextPowerOf2 (static_cast<int> (std::ceil (delay)) + 4);

        // Light LFO modulation: depth 0.3-0.5 samples @44.1kHz, rate 0.2-0.5 Hz
        // Depth scaled by sample rate ratio for consistent modulation across rates
        // 4 allpasses total (2L + 2R), phases spread evenly across 2*pi
        float phaseL = kTwoPi * static_cast<float> (s) / totalAPs;
        float rateL  = 0.2f + 0.3f * static_cast<float> (s) / (totalAPs - 1.0f);
        float depthL = (0.3f + 0.2f * static_cast<float> (s) / (totalAPs - 1.0f)) * ratio;
        leftAP_[s].prepare (bufSize, delay, rateL, depthL, phaseL, sampleRate_);

        int ri = s + kNumStages;
        float phaseR = kTwoPi * static_cast<float> (ri) / totalAPs;
        float rateR  = 0.2f + 0.3f * static_cast<float> (ri) / (totalAPs - 1.0f);
        float depthR = (0.3f + 0.2f * static_cast<float> (ri) / (totalAPs - 1.0f)) * ratio;
        rightAP_[s].prepare (bufSize, delay, rateR, depthR, phaseR, sampleRate_);
    }
    // Re-apply the LFO-depth scale (prepare reset baseLfoDepth_ on each stage).
    setLfoDepthScale (lfoScale_);
}

void OutputDiffusion::process (float* left, float* right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i];
        float r = right[i];

        for (int s = 0; s < kNumStages; ++s)
        {
            l = leftAP_[s].process (l, diffusionCoeff_);
            r = rightAP_[s].process (r, diffusionCoeff_);
        }

        left[i] = l;
        right[i] = r;
    }
}

void OutputDiffusion::setDiffusion (float amount)
{
    // Maps 0.0-1.0 to coefficient 0.0-0.62. The deeper 8-stage cascade needs
    // a stronger per-stage coefficient to fully decorrelate the FDN's sparse
    // HF modes into a dense wash (the old 4-stage ×0.3 cap topped out at
    // kurtosis ~18 vs the anchor's 12). 0.62 stays well inside allpass
    // stability and below the point where the cascade rings on its own.
    diffusionCoeff_ = std::clamp (amount, 0.0f, 1.0f) * 0.62f;
}

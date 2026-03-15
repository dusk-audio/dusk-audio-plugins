#include "OutputDiffusion.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

void OutputDiffusion::prepare (double sampleRate, int /*maxBlockSize*/)
{
    static constexpr float kTwoPi = 6.283185307179586f;
    float ratio = static_cast<float> (sampleRate / 44100.0);

    float totalAPs = static_cast<float> (kNumStages * 2);

    for (int s = 0; s < kNumStages; ++s)
    {
        float delay = static_cast<float> (kBaseDelays[s]) * ratio;
        int bufSize = DspUtils::nextPowerOf2 (static_cast<int> (std::ceil (delay)) + 4);

        // Light LFO modulation: depth 0.3-0.5 samples @44.1kHz, rate 0.2-0.5 Hz
        // Depth scaled by sample rate ratio for consistent modulation across rates
        // 4 allpasses total (2L + 2R), phases spread evenly across 2*pi
        float phaseL = kTwoPi * static_cast<float> (s) / totalAPs;
        float rateL  = 0.2f + 0.3f * static_cast<float> (s) / (totalAPs - 1.0f);
        float depthL = (0.3f + 0.2f * static_cast<float> (s) / (totalAPs - 1.0f)) * ratio;
        leftAP_[s].prepare (bufSize, delay, rateL, depthL, phaseL, sampleRate);

        int ri = s + kNumStages;
        float phaseR = kTwoPi * static_cast<float> (ri) / totalAPs;
        float rateR  = 0.2f + 0.3f * static_cast<float> (ri) / (totalAPs - 1.0f);
        float depthR = (0.3f + 0.2f * static_cast<float> (ri) / (totalAPs - 1.0f)) * ratio;
        rightAP_[s].prepare (bufSize, delay, rateR, depthR, phaseR, sampleRate);
    }
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
    // Maps 0.0-1.0 to coefficient 0.0-0.3
    // 4 stages at g=0.3 gives smoother frequency response than 2 stages at g=0.5,
    // with reduced density for cleaner tail character
    diffusionCoeff_ = std::clamp (amount, 0.0f, 1.0f) * 0.3f;
}

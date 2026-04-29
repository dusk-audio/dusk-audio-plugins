#pragma once

#include "DspUtils.h"

#include <algorithm>
#include <vector>

// PARALLEL all-pass input diffuser, sized strictly for the 6-AP
// (SixAPTank) engine.
//
// Why parallel and not series (the global DiffusionStage is series):
//   • A series Schroeder cascade has cumulative sum-of-delays peaks. With 4
//     stages at {142, 107, 211, 167} samples it produces a discrete +20 dB
//     event 14 ms after the input — exactly the "first echo" the 6-AP
//     engine could not hide because its output buffer happened to read
//     just before the density cascade.
//   • Parallel APs each see the input directly, then their outputs are
//     summed with alternating polarities (+AP1 -AP2 +AP3 -AP4 ...). No
//     stage is downstream of another, so there is NO cumulative peak —
//     just the union of each AP's individual impulse response.
//   • Each AP scatters the input into a sparse train of (g, g·(1-g²),
//     g²·(1-g²)·g …) impulses spaced by its delay. Six APs with
//     mutually-prime delays (487, 691, 1009, 1429, 2003, 2657 samples at
//     44.1 k base) produce a union train of ≈40 distinct impulses in the
//     first 60 ms — dense enough to perceptually "shatter" a transient
//     before it ever reaches the tank. The tank's 6-AP density cascade
//     then turns this dense input into a continuous wash with no audible
//     discrete events.
//
// Per-channel: stereo with slightly different prime sets so the L and R
// outputs decorrelate naturally.
class ParallelDiffuser
{
public:
    void prepare (double sampleRate)
    {
        const float rateRatio = static_cast<float> (sampleRate / 44100.0);

        for (int s = 0; s < kNumStages; ++s)
        {
            leftAPs_[s] .prepare (kLeftBaseDelays [s], rateRatio);
            rightAPs_[s].prepare (kRightBaseDelays[s], rateRatio);
        }
    }

    void clear()
    {
        for (int s = 0; s < kNumStages; ++s)
        {
            leftAPs_[s] .clear();
            rightAPs_[s].clear();
        }
    }

    // In-place stereo processing. Replaces each input sample with the
    // diffused parallel-sum output.
    void process (float* left, float* right, int numSamples)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float inL = left[n];
            const float inR = right[n];

            float sumL = 0.0f, sumR = 0.0f;
            for (int s = 0; s < kNumStages; ++s)
            {
                const float yL = leftAPs_[s] .process (inL, kCoeffs[s]);
                const float yR = rightAPs_[s].process (inR, kCoeffs[s]);
                sumL += kPolarity[s] * yL;
                sumR += kPolarity[s] * yR;
            }
            left [n] = sumL * kNorm;
            right[n] = sumR * kNorm;
        }
    }

private:
    static constexpr int kNumStages = 6;

    // Mutually-prime delays at 44.1 k base. Span 11 → 60 ms. Two distinct
    // sets so left and right tanks see decorrelated diffused content.
    static constexpr int kLeftBaseDelays [kNumStages] = { 487,  691, 1009, 1429, 2003, 2657 };
    static constexpr int kRightBaseDelays[kNumStages] = { 521,  743, 1093, 1499, 2069, 2729 };

    // Per-stage allpass coefficients. Slightly decreasing so each AP's
    // recirculation amplitude differs — prevents cascade ring artefacts.
    static constexpr float kCoeffs[kNumStages] = { 0.70f, 0.65f, 0.60f, 0.60f, 0.55f, 0.50f };

    // Alternating ± polarity sum prevents comb filtering when summed.
    static constexpr float kPolarity[kNumStages] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };

    // Energy-preserving normaliser. Six unit-magnitude polarities
    // → 1 / √6 ≈ 0.4082.
    static constexpr float kNorm = 0.40824829f;

    struct Allpass
    {
        std::vector<float> buf;
        int writePos = 0;
        int mask = 0;
        int delaySamples = 0;

        void prepare (int baseSamples, float rateRatio)
        {
            delaySamples = static_cast<int> (static_cast<float> (baseSamples) * rateRatio);
            const int size = DspUtils::nextPowerOf2 (delaySamples + 4);
            buf.assign (static_cast<size_t> (size), 0.0f);
            mask = size - 1;
            writePos = 0;
        }

        void clear()
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            writePos = 0;
        }

        // Schroeder allpass: H(z) = (z^-D - g) / (1 - g·z^-D).
        float process (float input, float g)
        {
            const float vd = buf[static_cast<size_t> ((writePos - delaySamples) & mask)];
            const float vn = input + g * vd;
            buf[static_cast<size_t> (writePos)] = vn;
            writePos = (writePos + 1) & mask;
            return vd - g * vn;
        }
    };

    Allpass leftAPs_ [kNumStages];
    Allpass rightAPs_[kNumStages];
};

#pragma once

#include "DspUtils.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Post-FDN stereo decorrelator. Mid/side encoding with a pure delay applied to
// the SIDE channel. Delaying the side by a few samples breaks L/R correlation
// (time offset between the difference and sum signals) without introducing
// feedback memory tails. Magnitude-flat — preserves RMS / decay / envelope.
//
// Applied post-FDN output scaling, before tail floor gate.
//
// amount: 0 = disabled (passthrough), 1 = full delayed-side replacement.
// baseDelayMs: pure-sample delay on side channel (1-5ms typical).
class StereoDecorrelator
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        const int maxDelay = static_cast<int> (std::ceil (0.010 * sampleRate));
        const int size = DspUtils::nextPowerOf2 (maxDelay + 4);
        mask_ = size - 1;
        bufSide_.assign (static_cast<size_t> (size), 0.0f);
        writePos_ = 0;
        delaySamples_ = 0;
    }

    void reset()
    {
        std::fill (bufSide_.begin(), bufSide_.end(), 0.0f);
        writePos_ = 0;
    }

    void configure (float amount, float baseDelayMs)
    {
        amount_ = std::clamp (amount, 0.0f, 1.0f);
        const float sr = static_cast<float> (sampleRate_);
        delaySamples_ = std::clamp (static_cast<int> (baseDelayMs * 0.001f * sr),
                                    1, mask_);
    }

    // M/S: mid = (L+R)/2, side = (L-R)/2.
    // side_delayed = side[n-D].
    // side_out = side * (1 - amount) + side_delayed * amount.
    // L_out = mid + side_out, R_out = mid - side_out.
    // Since delayed side differs from current side by time offset, the
    // reconstructed L/R have a phase-skewed difference signal → lower
    // correlation. No feedback → no ghost tail.
    inline void processSample (float& L, float& R)
    {
        const float mid  = 0.5f * (L + R);
        const float side = 0.5f * (L - R);

        bufSide_[static_cast<size_t> (writePos_)] = side;
        const int readPos = (writePos_ - delaySamples_) & mask_;
        const float sideDelayed = bufSide_[static_cast<size_t> (readPos)];

        const float sideOut = side * (1.0f - amount_) + sideDelayed * amount_;

        writePos_ = (writePos_ + 1) & mask_;

        L = mid + sideOut;
        R = mid - sideOut;
    }

    float amount() const { return amount_; }

private:
    double sampleRate_ = 44100.0;
    int writePos_ = 0;
    int mask_ = 0;
    int delaySamples_ = 0;

    std::vector<float> bufSide_;

    float amount_ = 0.0f;
};

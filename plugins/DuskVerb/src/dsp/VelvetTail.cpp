// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#include "VelvetTail.h"

namespace
{
// The shipping GCC/SSE release sums four interleaved tap sequences and
// folds them pairwise. Scalar builds sum in tap order. Preserve that
// rounding while batching across independent output samples.
#if defined(__GNUC__) && !defined(__clang__) && defined(__SSE2__) && defined(__FAST_MATH__)
constexpr int kTapLanes = 4;
#else
constexpr int kTapLanes = 1;
#endif
}

float VelvetTail::Biquad::process (float x)
{
    const float y = b0*x + z1;
#if defined(__FAST_MATH__)
    // Keep the rounded feed-forward sum used by the original Release loop.
    const volatile float feed = b1*x + z2;
    z1 = feed - a1*y;
#else
    z1 = b1*x - a1*y + z2;
#endif
    z2 = b2*x - a2*y;
    return y;
}

void VelvetTail::addField (const Field& field, const float* ring, float* out, int count) const noexcept
{
    float partial[kTapLanes][kBatchSize] {};
    const int grouped = field.n - field.n % kTapLanes;
    for (int t = 0; t < grouped; ++t)
    {
        const float* delayed = ring + ((writePos_ - field.pos[t]) & mask_);
        const float gain = field.gain[t];
        float* lane = partial[t % kTapLanes];
        for (int i = 0; i < count; ++i)
            lane[i] += gain * delayed[i];
    }
    if constexpr (kTapLanes == 4)
    {
        for (int i = 0; i < count; ++i)
        {
            const volatile float even = partial[0][i] + partial[2][i];
            const volatile float odd = partial[1][i] + partial[3][i];
            partial[0][i] = even + odd;
        }
        for (int t = grouped; t < field.n; ++t)
        {
            const float* delayed = ring + ((writePos_ - field.pos[t]) & mask_);
            for (int i = 0; i < count; ++i)
                partial[0][i] += field.gain[t] * delayed[i];
        }
    }
    for (int i = 0; i < count; ++i)
        out[i] += partial[0][i];
}

void VelvetTail::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    if (! prepared_)
    {
        std::fill_n (outL, n, 0.0f);
        std::fill_n (outR, n, 0.0f);
        return;
    }
    for (int offset = 0; offset < n;)
    {
        const int count = std::min ({ kBatchSize, n - offset, mask_ + 1 - writePos_ });
        for (int i = 0; i < count; ++i)
        {
            // Preserve the rounded mono input to the crossover in fast-math builds.
            const volatile float x = 0.5f * (inL[offset + i] + inR[offset + i]);
            const float lo = lp500_.process (x);
            const float hi = x - lo;
            const float b0 = lp250_.process (lo);
            const float b2 = lp2k_.process (hi);
            const int pos = writePos_ + i;
            rings_[0][pos] = b0;
            rings_[1][pos] = lo - b0;
            rings_[2][pos] = b2;
            rings_[3][pos] = hi - b2;
        }

        float* left = outL + offset;
        float* right = outR + offset;
        std::fill_n (left, count, 0.0f);
        std::fill_n (right, count, 0.0f);
        for (int b = 0; b < kBands; ++b)
        {
            float* ring = rings_[b].data();
            if (writePos_ < kBatchSize - 1)
                std::copy_n (ring + writePos_, std::min (count, kBatchSize - 1 - writePos_),
                             ring + mask_ + 1 + writePos_);
            // The original stereo writes left then right if the outputs alias.
            if (left != right)
                addField (fieldL_[b], ring, left, count);
            addField (fieldR_[b], ring, right, count);
        }
        writePos_ = (writePos_ + count) & mask_;
        offset += count;
    }
}

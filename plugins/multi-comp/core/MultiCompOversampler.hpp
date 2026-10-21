// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// Polyphase form of DuskOversampler's halfband 2x / 4x oversampler for Multi-Comp 2.
//
// Same tap sets, latency and history semantics as OversamplerT (the 2x stage is
// the 127-tap wide set or the 47-tap classic set, the 4x inner stage the 15-tap
// set), and the same arithmetic: every output is OversamplerT's, bit for bit
// (the plugin builds with -ffp-contract=off). What differs is the work:
//   - zero-stuffing is implicit. Each stage keeps the dense histories it reads
//     (host-rate inputs going up; the two phase streams coming down), so the
//     zero slots are never read. The terms they contributed to
//     HalfbandFIR::out()'s running sum were +0 or -0 and never changed it;
//   - over a block, four consecutive outputs run in the lanes of one register,
//     each lane performing out()'s running sum in its order.
#pragma once

#include "../../shared-daf/dsp/DuskOversampler.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace duskaudio
{
// One halfband's dense history, kept twice so that both halves of the
// symmetric side-tap sum read memory upward: `forward` holds the samples in
// time order and `backward` in reverse. Each copy writes every sample at two
// mirrored positions (w and w + kRing), so a 2 * NSide window never wraps.
template <int NSide>
class HalfbandHistory
{
public:
    static constexpr int kSpan = 2 * NSide;
    static constexpr int kRing = kSpan <= 8 ? 8 : kSpan <= 16 ? 16 : kSpan <= 32 ? 32 : kSpan <= 64 ? 64 : 128;
    static_assert(kSpan <= kRing, "halfband history too short");

    void reset() noexcept { forward.fill(0.0f); backward.fill(0.0f); write = 0; backWrite = 0; }
    void push(float x) noexcept
    {
        write = (write + 1) & (kRing - 1);
        forward[static_cast<size_t>(write)] = x;
        forward[static_cast<size_t>(write + kRing)] = x;
        backWrite = (backWrite - 1) & (kRing - 1);
        backward[static_cast<size_t>(backWrite)] = x;
        backward[static_cast<size_t>(backWrite + kRing)] = x;
    }
    // Samples NSide - 1 back to the newest, in time order: the near half of the window.
    const float* nearHalf() const noexcept { return forward.data() + write + kRing - (NSide - 1); }
    // Samples NSide back to 2 NSide - 1 back, newest first: the far half, mirrored.
    const float* farHalf() const noexcept { return backward.data() + backWrite + NSide; }
    // The sample `age` pushes back (0 = newest), age < kSpan.
    float at(int age) const noexcept { return forward[static_cast<size_t>(write + kRing - age)]; }

private:
    std::array<float, 2 * kRing> forward{}, backward{};
    int write = 0, backWrite = 0;
};

// HalfbandFIR::out()'s running sum over a dense stream x (x[k] = the sample k back):
//   start, then + taps[i] * (x[NSide - 1 - i] + x[NSide + i]) for i = 0 .. NSide - 1,
// the pair at distances (2i + 1) / 2 either side of the centre. start is the centre
// term: 0.5 x the centre sample, or +0 where the centre is a stuffed zero.
// near[i] = x[NSide - 1 - i] (HalfbandHistory::nearHalf), far[i] = x[NSide + i] (farHalf).
template <int NSide>
inline float halfbandSum(float start, const float* near, const float* far, const float* taps) noexcept
{
    float acc = start;
    for (int i = 0; i < NSide; ++i) acc += taps[i] * (near[i] + far[i]);
    return acc;
}

// halfbandSum() for n consecutive outputs of a linear stream: output k's
// centre start[k], near half at line + NSide + 1 + k (upward) and far half at
// line + NSide + k (downward), i.e. sample k is line[2 NSide + k]. Four outputs
// share each instruction, each in its own lane, in halfbandSum()'s order.
template <int NSide>
inline void halfbandSums(const float* start, const float* line, int n, const float* taps, float* out) noexcept
{
    int k = 0;
#if defined(__aarch64__)
    for (; k + 8 <= n; k += 8)   // two independent groups of four, so the two running sums overlap
    {
        float32x4_t acc0 = vld1q_f32(start + k);
        float32x4_t acc1 = vld1q_f32(start + k + 4);
        for (int i = 0; i < NSide; ++i)
        {
            const float32x4_t tap = vdupq_n_f32(taps[i]);
            acc0 = vaddq_f32(acc0, vmulq_f32(tap, vaddq_f32(vld1q_f32(line + NSide + 1 + k + i),
                                                            vld1q_f32(line + NSide + k - i))));
            acc1 = vaddq_f32(acc1, vmulq_f32(tap, vaddq_f32(vld1q_f32(line + NSide + 5 + k + i),
                                                            vld1q_f32(line + NSide + 4 + k - i))));
        }
        vst1q_f32(out + k, acc0);
        vst1q_f32(out + k + 4, acc1);
    }
    for (; k + 4 <= n; k += 4)
    {
        float32x4_t acc = vld1q_f32(start + k);
        for (int i = 0; i < NSide; ++i)
            acc = vaddq_f32(acc, vmulq_f32(vdupq_n_f32(taps[i]),
                                           vaddq_f32(vld1q_f32(line + NSide + 1 + k + i),
                                                     vld1q_f32(line + NSide + k - i))));
        vst1q_f32(out + k, acc);
    }
#endif
    for (; k < n; ++k)
    {
        float acc = start[k];
        for (int i = 0; i < NSide; ++i) acc += taps[i] * (line[NSide + 1 + k + i] + line[NSide + k - i]);
        out[k] = acc;
    }
}

template <int NSide>
class PolyphaseHalfband
{
public:
    void reset() noexcept { upHistory.reset(); downEven.reset(); downOdd.reset(); }

    // Upsample one sample: the two output phases, already scaled by 2.
    void up(float x, const float* taps, float& phase0, float& phase1) noexcept
    {
        upHistory.push(x);
        // The real phase's centre is a stuffed zero: out() starts from 0.5 x 0 = +0.
        phase0 = 2.0f * halfbandSum<NSide>(0.0f, upHistory.nearHalf(), upHistory.farHalf(), taps);
        // The stuffed phase is the centre tap alone: 0.5 x[n - (NSide - 1)].
        phase1 = 2.0f * (0.5f * upHistory.at(NSide - 1) + 0.0f);
    }

    // Decimate one pair of phases (pushed in order) to one sample.
    float down(float phase0, float phase1, const float* taps) noexcept
    {
        downEven.push(phase0);
        downOdd.push(phase1);
        return halfbandSum<NSide>(0.5f * downEven.at(NSide - 1), downOdd.nearHalf(), downOdd.farHalf(), taps);
    }

    // up() for each of n samples into phases[2i], phases[2i + 1], and down()
    // for each of n interleaved pairs: the same sums on the same values, read
    // from linear copies of the history and the block (halfbandSums()), so no
    // window load waits on a sample just stored. Scratch: blockScratch(n) floats.
    static constexpr int kSpan = 2 * NSide;
    static constexpr int blockScratch(int n) noexcept { return 3 * (n + kSpan); }

    void upBlock(const float* x, int n, const float* taps, float* phases, float* scratch) noexcept
    {
        const int m = n + kSpan;
        float* forward = scratch;            // history then block, oldest first
        float* start = scratch + m;          // the real phases' centre terms: +0
        float* sums = scratch + m + n;
        fillLinear(upHistory, x, n, forward);
        std::fill(start, start + n, 0.0f);
        halfbandSums<NSide>(start, forward, n, taps, sums);
        for (int i = 0; i < n; ++i)
        {
            phases[2 * i] = 2.0f * sums[i];
            phases[2 * i + 1] = 2.0f * (0.5f * forward[kSpan + i - (NSide - 1)] + 0.0f);
        }
        keepTail(upHistory, x, n);
    }

    void downBlock(const float* pairs, int n, const float* taps, float* out, float* scratch) noexcept
    {
        const int m = n + kSpan;
        float* even = scratch;
        float* odd = scratch + m;
        float* start = scratch + 2 * m;      // the centre terms, 0.5 x the even phase
        for (int i = 0; i < n; ++i)
        {
            even[kSpan + i] = pairs[2 * i];
            odd[kSpan + i] = pairs[2 * i + 1];
        }
        fillHistory(downEven, even);
        fillHistory(downOdd, odd);
        for (int i = 0; i < n; ++i) start[i] = 0.5f * even[kSpan + i - (NSide - 1)];
        halfbandSums<NSide>(start, odd, n, taps, out);
        keepTail(downEven, even + kSpan, n);
        keepTail(downOdd, odd + kSpan, n);
    }

private:
    static void fillHistory(const HalfbandHistory<NSide>& history, float* linear) noexcept
    {
        for (int age = kSpan - 1; age >= 0; --age) linear[kSpan - 1 - age] = history.at(age);
    }
    static void fillLinear(const HalfbandHistory<NSide>& history, const float* x, int n, float* linear) noexcept
    {
        fillHistory(history, linear);
        std::memcpy(linear + kSpan, x, static_cast<size_t>(n) * sizeof(float));
    }
    // Only the last kSpan samples are ever read again.
    static void keepTail(HalfbandHistory<NSide>& history, const float* x, int n) noexcept
    {
        for (int i = n > kSpan ? n - kSpan : 0; i < n; ++i) history.push(x[i]);
    }

    HalfbandHistory<NSide> upHistory, downEven, downOdd;
};

// Drop-in for OversamplerT<Stage> (factor 1 / 2 / 4, processSample, the split
// upsampleSample / downsampleSample form, latency()).
template <class Stage>
class PolyphaseOversampler
{
public:
    void setFactor(int f) noexcept { factor = (f == 4) ? 4 : (f == 2 ? 2 : 1); }
    int getFactor() const noexcept { return factor; }
    void reset() noexcept { outer.reset(); inner.reset(); }

    static constexpr float kStageALatency = static_cast<float>(Stage::L - 1) / 2.0f;
    float latency() const noexcept
    {
        if (factor == 4) return kStageALatency + 3.5f;
        if (factor == 2) return kStageALatency;
        return 0.0f;
    }

    template <class Fn>
    float processSample(float x, Fn&& f) noexcept
    {
        if (factor == 1) return f(x);
        float a0, a1;
        outer.up(x, Stage::taps, a0, a1);
        if (factor == 2)
        {
            const float y0 = f(a0);
            const float y1 = f(a1);
            return outer.down(y0, y1, Stage::taps);
        }
        const float y0 = processInner(a0, f);
        const float y1 = processInner(a1, f);
        return outer.down(y0, y1, Stage::taps);
    }

    void upsampleSample(float x, float* phases) noexcept
    {
        if (factor == 1)
        {
            phases[0] = x;
            return;
        }
        float a0, a1;
        outer.up(x, Stage::taps, a0, a1);
        if (factor == 2)
        {
            phases[0] = a0;
            phases[1] = a1;
            return;
        }
        inner.up(a0, hbtaps::kB, phases[0], phases[1]);
        inner.up(a1, hbtaps::kB, phases[2], phases[3]);
    }

    // upsampleSample() / downsampleSample() over a block (phases interleaved
    // per sample). Scratch: blockScratch(n) floats.
    static constexpr int blockScratch(int n) noexcept
    {
        return 2 * n + PolyphaseHalfband<Stage::NSide>::blockScratch(n)
             + PolyphaseHalfband<4>::blockScratch(2 * n);
    }
    void upsampleBlock(const float* x, int n, float* phases, float* scratch) noexcept
    {
        if (factor == 1)
        {
            std::memcpy(phases, x, static_cast<size_t>(n) * sizeof(float));
            return;
        }
        if (factor == 2)
        {
            outer.upBlock(x, n, Stage::taps, phases, scratch);
            return;
        }
        float* stream = scratch;   // the 2x samples, in order
        outer.upBlock(x, n, Stage::taps, stream, scratch + 2 * n);
        inner.upBlock(stream, 2 * n, hbtaps::kB, phases, scratch + 2 * n);
    }
    void downsampleBlock(const float* phases, int n, float* out, float* scratch) noexcept
    {
        if (factor == 1)
        {
            std::memcpy(out, phases, static_cast<size_t>(n) * sizeof(float));
            return;
        }
        if (factor == 2)
        {
            outer.downBlock(phases, n, Stage::taps, out, scratch);
            return;
        }
        float* stream = scratch;   // the decimated 2x samples, in order
        inner.downBlock(phases, 2 * n, hbtaps::kB, stream, scratch + 2 * n);
        outer.downBlock(stream, n, Stage::taps, out, scratch + 2 * n);
    }

    float downsampleSample(const float* phases) noexcept
    {
        if (factor == 1) return phases[0];
        if (factor == 2) return outer.down(phases[0], phases[1], Stage::taps);
        const float a0 = inner.down(phases[0], phases[1], hbtaps::kB);
        const float a1 = inner.down(phases[2], phases[3], hbtaps::kB);
        return outer.down(a0, a1, Stage::taps);
    }

private:
    static_assert(Stage::L == 4 * Stage::NSide - 1, "halfband length must be 4 NSide - 1");

    template <class Fn>
    float processInner(float s, Fn&& f) noexcept
    {
        float b0, b1;
        inner.up(s, hbtaps::kB, b0, b1);
        const float y0 = f(b0);
        const float y1 = f(b1);
        return inner.down(y0, y1, hbtaps::kB);
    }

    int factor = 2;
    PolyphaseHalfband<Stage::NSide> outer;   // base <-> 2x
    PolyphaseHalfband<4> inner;              // 2x <-> 4x, the 15-tap kB set
};

using MultiCompOversampler = PolyphaseOversampler<hbtaps::StageA>;           // 47-tap, 23-sample stage
using MultiCompOversamplerWide = PolyphaseOversampler<hbtaps::StageAwide>;   // 127-tap, 63-sample stage
} // namespace duskaudio

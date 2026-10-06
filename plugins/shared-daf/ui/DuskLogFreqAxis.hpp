// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskLogFreqAxis.hpp — the log-frequency horizontal axis every analyser,
// response graph and spectrum display in the fleet draws.
//
// 4K EQ 2 and Multi-Q 2 each carry a private flog() plus a hand-typed grid
// table; this is that pair as one object so a new graph gets the same mapping,
// the same 1-2-5 grid and the same label format without copying either. The
// mapping is pure (no ImGui), so it is also what a UI test or a hit-test uses to
// turn a mouse x back into a frequency.
//
// Nothing here draws: a graph owns its own rectangle, colours and clip rect.
// gridLines() hands back the frequencies to rule and label; the caller maps them
// with toNorm().
#pragma once

#include <cmath>
#include <cstdio>

namespace duskdaf
{

class LogFreqAxis
{
public:
    LogFreqAxis(float minHz = 20.0f, float maxHz = 20000.0f) noexcept { setRange(minHz, maxHz); }

    // The range and the mapping cache are set together, so they cannot drift.
    void setRange(float minHz, float maxHz) noexcept
    {
        fMin_ = minHz > 1.0e-6f ? minHz : 1.0e-6f;
        fMax_ = maxHz > fMin_ ? maxHz : fMin_ * 2.0f;
        logMin_ = std::log10(fMin_);
        invSpan_ = 1.0f / (std::log10(fMax_) - logMin_);
    }

    float minHz() const noexcept { return fMin_; }
    float maxHz() const noexcept { return fMax_; }

    // 0 at minHz, 1 at maxHz (not clamped: a caller may want to know a point is
    // off the axis). One log10 per call; the endpoints are cached, since an
    // analyser calls this once per bin per frame.
    float toNorm(float freq) const noexcept
    {
        return (std::log10(freq > 1.0e-6f ? freq : 1.0e-6f) - logMin_) * invSpan_;
    }

    float fromNorm(float x) const noexcept
    {
        return std::pow(10.0f, logMin_ + x / invSpan_);
    }

    // Standard rule positions: 1-2-5 per decade plus the decade itself, clipped
    // to the axis. Returns the count written (at most `cap`).
    int gridLines(float* out, int cap) const noexcept
    {
        static constexpr float kMantissa[] = { 1.0f, 2.0f, 5.0f };
        int n = 0;
        for (float decade = 1.0f; decade <= 100000.0f && n < cap; decade *= 10.0f)
            for (const float m : kMantissa)
            {
                const float f = m * decade;
                if (f >= fMin_ * 0.999f && f <= fMax_ * 1.001f && n < cap)
                    out[n++] = f;
            }
        return n;
    }

    // "24", "100", "1k", "1k2", "12k5": the compact console form, which is what
    // the reference analysers print and what fits between two rules.
    static void label(float freq, char* buf, int cap) noexcept
    {
        if (freq < 1000.0f)
        {
            std::snprintf(buf, (size_t)cap, "%d", (int)(freq + 0.5f));
            return;
        }
        // Round once, to hundreds of hertz, then split: rounding the remainder
        // on its own would print 1999 Hz as "1k10".
        const int hundredsTotal = (int)(freq / 100.0f + 0.5f);
        const int k = hundredsTotal / 10;
        const int hundreds = hundredsTotal % 10;
        if (hundreds == 0)
            std::snprintf(buf, (size_t)cap, "%dk", k);
        else
            std::snprintf(buf, (size_t)cap, "%dk%d", k, hundreds);
    }

private:
    float fMin_ = 20.0f;
    float fMax_ = 20000.0f;
    float logMin_ = 0.0f;
    float invSpan_ = 1.0f;
};

} // namespace duskdaf

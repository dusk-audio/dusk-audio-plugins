// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskFft.hpp — self-contained iterative radix-2 complex FFT with precomputed
// twiddles and bit-reversal. Operates in-place on separate real/imag arrays.
// inverse() applies the 1/N normalisation (matching juce::dsp::FFT's
// performInverse family).
//
// Promoted VERBATIM from plugins/multi-q/core/MultiQMatch.{hpp,cpp} so the
// fleet has one DSP-grade FFT: Multi-Q's Match engine consumes it through its
// original duskaudio::FFTr2 name, and the Spectrum Analyzer 2 port builds its
// analysis core on it (docs/daf-migration/10-spectrum-analyzer.md, step 1).
// Verbatim matters: Multi-Q's JUCE-vs-DAF A/B harness gates Match behaviour,
// so this move must not change a single arithmetic operation.
//
// This is deliberately NOT duskdaf::RealFFT (ui/DuskImGuiWidgets.hpp). That
// one is a display helper: recurrence-computed twiddles that accumulate float
// error across stages, a baked-in Hann window, magnitude-only output. Fine for
// pixels, wrong for calibration. This class has precomputed twiddles, forward
// and inverse transforms, and no windowing policy; measurement code windows
// explicitly.
#pragma once

#include <cmath>
#include <vector>

namespace duskaudio
{

class FFTr2
{
public:
    void prepare(int size)
    {
        n = size;
        rev.assign((size_t)n, 0);
        int logn = 0; while ((1 << logn) < n) ++logn;
        for (int i = 0; i < n; ++i)
        {
            int r = 0, x = i;
            for (int b = 0; b < logn; ++b) { r = (r << 1) | (x & 1); x >>= 1; }
            rev[(size_t)i] = r;
        }
        twR.assign((size_t)(n / 2), 0.0f);
        twI.assign((size_t)(n / 2), 0.0f);
        constexpr double kPi = 3.14159265358979323846;
        const double twoPi = 2.0 * kPi;
        for (int j = 0; j < n / 2; ++j)
        {
            const double a = -twoPi * (double)j / (double)n;
            twR[(size_t)j] = (float)std::cos(a);
            twI[(size_t)j] = (float)std::sin(a);
        }
    }

    void forward(float* re, float* im) const { transform(re, im, false); }
    void inverse(float* re, float* im) const { transform(re, im, true); }
    int size() const noexcept { return n; }

private:
    void transform(float* re, float* im, bool inv) const
    {
        // bit-reversal permutation
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t)i];
            if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const int ti = k * step;
                    float wr = twR[(size_t)ti];
                    float wi = twI[(size_t)ti];
                    if (inv) wi = -wi;                    // conjugate twiddle for inverse
                    const int a = i + k, b = i + k + half;
                    const float vr = re[b] * wr - im[b] * wi;
                    const float vi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - vr; im[b] = im[a] - vi;
                    re[a] += vr;        im[a] += vi;
                }
            }
        }
        if (inv)
        {
            const float s = 1.0f / (float)n;
            for (int i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
        }
    }

    int n = 0;
    std::vector<int>   rev;      // bit-reversal permutation
    std::vector<float> twR, twI; // twiddles W[j] = exp(-2*pi*i*j / n), j in 0..n/2-1
};

} // namespace duskaudio

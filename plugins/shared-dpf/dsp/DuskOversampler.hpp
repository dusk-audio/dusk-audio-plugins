// DuskOversampler.hpp — streaming 2x / 4x polyphase-halfband oversampler.
//
// Framework-free replacement for juce::dsp::Oversampling, for running a
// stateful per-sample processing chain (e.g. an IIR EQ + saturation) at an
// elevated rate. The halfband FIR and its scipy-remez tap sets are lifted
// verbatim from plugins/tape-echo/core/TapeEchoDSP.{hpp,cpp}; this generalizes
// tape-echo's fixed 4x preamp path into a functor-driven up/down wrapper.
//
// Usage (in-order over a block keeps filter state continuous):
//     for (int n = 0; n < numSamples; ++n)
//         out[n] = os.processSample(in[n], [&](float s){ return chain(s); });
//
// The functor is called `factor` times per input sample, at the oversampled
// rate, and must return the processed sample. Latency (fixed group delay of
// the up+down FIR round trip) is reported in base-rate samples by latency().

#pragma once

namespace duskaudio
{

// Ring-convolution halfband FIR (center tap 0.5, even offsets zero). L = full
// tap length, NSide = number of nonzero one-sided taps. Verbatim from tape-echo.
template <int L, int NSide>
class HalfbandFIR
{
public:
    void reset() noexcept
    {
        for (float& v : buf) v = 0.0f;
        pos = 0;
    }
    void push(float x) noexcept { pos = (pos + 1) & 63; buf[pos] = x; }
    float out(const float* taps) const noexcept
    {
        constexpr int C = L / 2;
        float acc = 0.5f * buf[(pos - C) & 63];
        for (int i = 0; i < NSide; ++i)
        {
            const int k = 2 * i + 1;
            acc += taps[i] * (buf[(pos - (C - k)) & 63] + buf[(pos - (C + k)) & 63]);
        }
        return acc;
    }

private:
    float buf[64] = {};
    int   pos = 0;
};

// Halfband tap sets (scipy remez; halfband-exact).
namespace hbtaps
{
    // stage A: 47-tap halfband, transition 0.08, stopband -67 dB.
    static constexpr float kA[12] = {
        0.3168690344f, -0.1018442627f, 0.0567777617f, -0.0362614803f,
        0.0242159187f, -0.0162814078f, 0.0107858313f, -0.0069217143f,
        0.0042343916f, -0.0024153268f, 0.0012438004f, -0.0006166386f,
    };
    // stage B: 15-tap halfband, transition 0.26, stopband -75 dB.
    static constexpr float kB[4] = {
        0.3048934958f, -0.0712879483f, 0.0197218961f, -0.0034083969f,
    };
}

class Oversampler
{
public:
    // factor must be 1, 2, or 4. factor 1 is a transparent passthrough.
    void setFactor(int f) noexcept { factor = (f == 4) ? 4 : (f == 2 ? 2 : 1); }
    int  getFactor() const noexcept { return factor; }

    void reset() noexcept
    {
        upA.reset(); downA.reset();
        upB.reset(); downB.reset();
    }

    // Fixed group delay of the up+down FIR round trip, in base-rate samples.
    // 2x stage (47-tap): 46 samples @2x = 23 base. 4x stage (15-tap): 14 @4x = 3.5 base.
    float latency() const noexcept
    {
        if (factor == 4) return 23.0f + 3.5f;
        if (factor == 2) return 23.0f;
        return 0.0f;
    }

    template <class Fn>
    float processSample(float x, Fn&& f) noexcept
    {
        if (factor == 1)
            return f(x);
        if (factor == 2)
            return process2x(x, static_cast<Fn&&>(f));

        // 4x = 2x nested inside 2x. Outer works base<->2x; inner works 2x<->4x.
        return process2x(x, [this, &f](float s) noexcept { return process4xInner(s, f); });
    }

private:
    // base <-> 2x via stage A.
    template <class Fn>
    float process2x(float x, Fn&& f) noexcept
    {
        upA.push(x);        const float a0 = 2.0f * upA.out(hbtaps::kA);
        upA.push(0.0f);     const float a1 = 2.0f * upA.out(hbtaps::kA);
        downA.push(f(a0));
        downA.push(f(a1));
        return downA.out(hbtaps::kA);
    }

    // one 2x-rate sample -> 4x, process, -> back to 2x, via stage B.
    template <class Fn>
    float process4xInner(float s, Fn&& f) noexcept
    {
        upB.push(s);        const float b0 = 2.0f * upB.out(hbtaps::kB);
        upB.push(0.0f);     const float b1 = 2.0f * upB.out(hbtaps::kB);
        downB.push(f(b0));
        downB.push(f(b1));
        return downB.out(hbtaps::kB);
    }

    int factor = 2;
    HalfbandFIR<47, 12> upA, downA;   // base <-> 2x
    HalfbandFIR<15, 4>  upB, downB;   // 2x  <-> 4x
};

} // namespace duskaudio

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
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
    void push(float x) noexcept { pos = (pos + 1) & 127; buf[pos] = x; }
    float out(const float* taps) const noexcept
    {
        constexpr int C = L / 2;
        float acc = 0.5f * buf[(pos - C) & 127];
        for (int i = 0; i < NSide; ++i)
        {
            const int k = 2 * i + 1;
            acc += taps[i] * (buf[(pos - (C - k)) & 127] + buf[(pos - (C + k)) & 127]);
        }
        return acc;
    }

private:
    float buf[128] = {};
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
    // stage A-deep: 71-tap halfband, transition 0.05, stopband -116 dB. Used for the
    // OUTER decimation of LocalAAStage so a hot near-Nyquist harmonic doesn't fold to LF
    // through the ordinary halfband's shallow stopband edge (see LocalAAStage below).
    static constexpr float kAdeep[18] = {
        0.3170305596f, -0.1023212374f, 0.0575401133f, -0.0372667917f, 0.0254042771f,
        -0.0175876005f, 0.0121372392f, -0.0082512865f, 0.0054777043f, -0.0035240612f,
        0.0021806595f, -0.0012869948f, 0.0007170541f, -0.0003719507f, 0.0001760559f,
        -0.0000735499f, 0.0000254803f, -0.0000063102f,
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

//==============================================================================
// Local 2x wrapper for ONE memoryless nonlinearity: a single (non-nested)
// halfband up/down stage so the nonlinearity runs at twice the surrounding
// rate. High-order harmonics that would fold in-band at the surrounding rate
// land below the doubled Nyquist instead, and the down-halfband removes them.
// Use it to anti-alias a waveshaper / saturator / limiter cheaply, without
// raising the whole plugin's oversampling factor:
//     y = stage.process(x, [](float s){ return myShaper(s); });
// Stage-B taps: passband edge ~0.12*fs (far above audio at any musical rate);
// stopband -75 dB. ~4 taps up + 4 down + 2 nonlinearity evals per sample.
// Unlike ADAA (DuskADAA.hpp) it needs no antiderivative and does not interact
// with a surrounding nested-halfband oversampler's group delay.
//==============================================================================
class Local2xStage
{
public:
    void reset() noexcept { up.reset(); down.reset(); }

    template <class Fn>
    float process (float x, Fn&& f) noexcept
    {
        up.push (x);        const float u0 = 2.0f * up.out (hbtaps::kB);
        up.push (0.0f);     const float u1 = 2.0f * up.out (hbtaps::kB);
        down.push (f (u0));
        down.push (f (u1));
        return down.out (hbtaps::kB);
    }

private:
    HalfbandFIR<15, 4> up, down;
};

//==============================================================================
// LocalAAStage — local 4x oversampler for ONE memoryless nonlinearity, with a DEEP
// (-116 dB, 71-tap `kAdeep`) OUTER decimation halfband. The weak spot of a local NL
// stage is that final 2x->1x decimation: a hot near-Nyquist harmonic (a 19 kHz tone's
// 5th at 95 kHz) folds to LF through an ordinary halfband's ~-75 dB stopband, and more
// oversampling can't help because that last stage always sees the harmonic at the
// stopband edge. The deep set drops the fold below -110 dB, so the surrounding core can
// stay at a cheap 2x while the NL is alias-free (the mixed-rate: filters at 2x, NL clean).
// Group delay (in surrounding-rate samples) is reported by latency() so the host can
// compensate it — unlike the shallow Local2xStage its 47+71-tap round trip is large.
class LocalAAStage
{
public:
    void reset() noexcept { upA.reset(); downA.reset(); upB.reset(); downB.reset(); }

    // Round-trip group delay in SURROUNDING-rate samples: outer up(47)+down(71) center
    // taps = (23+35)=58 @2x-local = 29; inner up(15)+down(15) = (7+7)=14 @4x-local = 3.5.
    static constexpr float latency() noexcept { return 29.0f + 3.5f; }

    template <class Fn>
    float process (float x, Fn&& f) noexcept
    {
        // local 4x (outer 2x + inner 2x) so harmonics land high, then the OUTER
        // decimation uses the deep -116 dB set to kill the near-Nyquist fold.
        upA.push (x);       const float a0 = 2.0f * upA.out (hbtaps::kA);
        upA.push (0.0f);    const float a1 = 2.0f * upA.out (hbtaps::kA);
        downA.push (inner (a0, f));
        downA.push (inner (a1, f));
        return downA.out (hbtaps::kAdeep);
    }

private:
    template <class Fn>
    float inner (float s, Fn&& f) noexcept
    {
        upB.push (s);       const float b0 = 2.0f * upB.out (hbtaps::kB);
        upB.push (0.0f);    const float b1 = 2.0f * upB.out (hbtaps::kB);
        downB.push (f (b0));
        downB.push (f (b1));
        return downB.out (hbtaps::kB);
    }
    HalfbandFIR<47, 12> upA;
    HalfbandFIR<71, 18> downA;   // deep outer decimation (the fold-critical stage)
    HalfbandFIR<15, 4>  upB, downB;
};

} // namespace duskaudio

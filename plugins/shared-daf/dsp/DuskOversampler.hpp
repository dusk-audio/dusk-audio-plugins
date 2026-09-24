// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
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
    // Power-of-two ring so the index wrap is a mask. Sized for the deepest tap set
    // in use (kAwide, L=127/NSide=32 -> 126 samples of lookback).
    static constexpr int kRing = 128;
    static constexpr int kMask = kRing - 1;

    // The furthest-back sample `out()` reads is pos-(C + kMaxOdd) where C = L/2 and
    // kMaxOdd = 2*NSide-1. Once that exceeds the ring, the mask silently aliases it
    // onto a NEWER sample instead of the intended history and the filter quietly
    // computes the wrong thing -- exactly the failure a 64-entry ring would have
    // produced for the 71-tap set. Fail the build instead.
    static_assert(L / 2 + 2 * NSide - 1 < kRing,
                  "HalfbandFIR tap set is too long for the ring buffer: "
                  "L/2 + 2*NSide - 1 must stay below kRing. Grow kRing (keep it a "
                  "power of two) rather than letting the index mask alias.");
    static_assert((kRing & kMask) == 0, "kRing must be a power of two");

    void reset() noexcept
    {
        for (float& v : buf) v = 0.0f;
        pos = 0;
    }
    void push(float x) noexcept { pos = (pos + 1) & kMask; buf[pos] = x; }
    float out(const float* taps) const noexcept
    {
        constexpr int C = L / 2;
        float acc = 0.5f * buf[(pos - C) & kMask];
        for (int i = 0; i < NSide; ++i)
        {
            const int k = 2 * i + 1;
            acc += taps[i] * (buf[(pos - (C - k)) & kMask] + buf[(pos - (C + k)) & kMask]);
        }
        return acc;
    }

private:
    float buf[kRing] = {};
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
    // stage A-wide: 127-tap halfband, transition 0.042, stopband -90 dB. Passband
    // flat to within 0.001 dB up to 20 kHz at a 44.1 kHz base rate (kA sags 1.18 dB
    // there: its passband ends at 0.86 Nyquist). Costs 63 base samples of latency
    // instead of 23; use it where the host compensates latency and the reference
    // being matched is flat to 20 kHz (Multi-Comp 2). Round trip -11.6 dB at Nyquist.
    static constexpr float kAwide[32] = {
        0.3180152602f, -0.1052223122f, 0.0622032639f, -0.0434505883f,
        0.0328014530f, -0.0258512876f, 0.0209085984f, -0.0171858670f,
        0.0142674138f, -0.0119133357f, 0.0099754677f, -0.0083571881f,
        0.0069925296f, -0.0058345845f, 0.0048487434f, -0.0040085704f,
        0.0032932013f, -0.0026856879f, 0.0021718778f, -0.0017396661f,
        0.0013785235f, -0.0010791554f, 0.0008332916f, -0.0006335074f,
        0.0004730913f, -0.0003460624f, 0.0002470644f, -0.0001712807f,
        0.0001145247f, -0.0000730276f, 0.0000436856f, -0.0000314876f,
    };

    // Stage-A tap-set descriptors for OversamplerT. L = full tap length, NSide =
    // one-sided nonzero taps, taps = the set.
    struct StageA     { static constexpr int L = 47;  static constexpr int NSide = 12; static constexpr const float* taps = kA; };
    struct StageAwide { static constexpr int L = 127; static constexpr int NSide = 32; static constexpr const float* taps = kAwide; };
}

// Streaming oversampler parameterised on its base<->2x halfband (stage A). The
// 2x<->4x inner stage is always the 15-tap kB set. `Oversampler` below is the
// original 47-tap instantiation and is byte-identical to the pre-template class.
template <class StageA>
class OversamplerT
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
    // 2x stage (L taps): L-1 samples @2x = (L-1)/2 base (23 for the 47-tap set,
    // 63 for the 127-tap set). 4x stage (15-tap): 14 @4x = 3.5 base.
    static constexpr float kStageALatency = static_cast<float>(StageA::L - 1) / 2.0f;
    float latency() const noexcept
    {
        if (factor == 4) return kStageALatency + 3.5f;
        if (factor == 2) return kStageALatency;
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

    // Split form for a caller that must process multiple channels in lockstep.
    // `phases` has room for four samples; only getFactor() entries are used.
    // Calling upsampleSample() followed by downsampleSample() advances exactly
    // the same filter state as processSample().
    void upsampleSample(float x, float* phases) noexcept
    {
        if (factor == 1)
        {
            phases[0] = x;
            return;
        }

        upA.push(x);
        const float a0 = 2.0f * upA.out(StageA::taps);
        upA.push(0.0f);
        const float a1 = 2.0f * upA.out(StageA::taps);
        if (factor == 2)
        {
            phases[0] = a0;
            phases[1] = a1;
            return;
        }

        upB.push(a0);
        phases[0] = 2.0f * upB.out(hbtaps::kB);
        upB.push(0.0f);
        phases[1] = 2.0f * upB.out(hbtaps::kB);
        upB.push(a1);
        phases[2] = 2.0f * upB.out(hbtaps::kB);
        upB.push(0.0f);
        phases[3] = 2.0f * upB.out(hbtaps::kB);
    }

    float downsampleSample(const float* phases) noexcept
    {
        if (factor == 1) return phases[0];
        if (factor == 2)
        {
            downA.push(phases[0]);
            downA.push(phases[1]);
            return downA.out(StageA::taps);
        }

        downB.push(phases[0]);
        downB.push(phases[1]);
        const float a0 = downB.out(hbtaps::kB);
        downB.push(phases[2]);
        downB.push(phases[3]);
        const float a1 = downB.out(hbtaps::kB);
        downA.push(a0);
        downA.push(a1);
        return downA.out(StageA::taps);
    }

private:
    // base <-> 2x via stage A.
    template <class Fn>
    float process2x(float x, Fn&& f) noexcept
    {
        upA.push(x);        const float a0 = 2.0f * upA.out(StageA::taps);
        upA.push(0.0f);     const float a1 = 2.0f * upA.out(StageA::taps);
        downA.push(f(a0));
        downA.push(f(a1));
        return downA.out(StageA::taps);
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
    HalfbandFIR<StageA::L, StageA::NSide> upA, downA;   // base <-> 2x
    HalfbandFIR<15, 4>                    upB, downB;   // 2x  <-> 4x
};

using Oversampler     = OversamplerT<hbtaps::StageA>;       // 47-tap, 23-sample stage
using OversamplerWide = OversamplerT<hbtaps::StageAwide>;   // 127-tap, 63-sample stage

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

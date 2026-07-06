// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// FourKEQDSP.hpp — framework-free 4K console EQ core (C++17, no JUCE/DPF).
//
// Port of plugins/4k-eq/FourKEQ.{h,cpp}. The DSP is identical in structure to
// the JUCE version; juce::dsp::IIR bands become shared duskaudio::Biquad
// instances (bit-for-bit coefficient math), juce::dsp::Oversampling becomes the
// shared streaming halfband Oversampler, and ConsoleSaturation becomes the
// de-JUCE'd ConsoleSaturationCore. The whole filter + saturation chain runs
// oversampled (>=2x) per the project "no EQ cramping" rule.
//
// Signal flow (reproduces FourKEQ::processBlock):
//   in-meter -> input gain -> [pre-EQ spectrum tap] -> (M/S encode) ->
//   OVERSAMPLE{ HPF(3rd) -> LF -> LM -> HM -> HF -> LPF -> transformer allpass
//   (Brown only) -> console saturation } -> crosstalk -> (M/S decode) ->
//   output gain*autogain -> [post-EQ spectrum tap] -> bypass crossfade -> out.
//
// Contract: prepare()/reset() on the main thread; processBlock() RT-safe
// (no alloc/lock/IO); set*() atomic from any thread. In-place safe.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "../../shared-dpf/dsp/DuskDenormals.hpp"
#include "../../shared-dpf/dsp/DuskSmoothed.hpp"
#include "../../shared-dpf/dsp/DuskFilters.hpp"
#include "../../shared-dpf/dsp/DuskOversampler.hpp"
#include "ConsoleSaturationCore.h"

namespace duskaudio
{

// Lock-free single-producer / single-consumer sample ring for UI spectrum taps.
// The audio thread push()es; the UI snapshot()s the most recent kSize samples.
class SpectrumRing
{
public:
    static constexpr int kSize = 4096; // power of two
    void reset() noexcept
    {
        for (auto& v : buf) v.store(0.0f, std::memory_order_relaxed);
        writePos.store(0, std::memory_order_relaxed);
    }
    void push(float x) noexcept
    {
        const std::uint32_t w = writePos.load(std::memory_order_relaxed);
        buf[(size_t)(w & (kSize - 1))].store(x, std::memory_order_relaxed);
        writePos.store(w + 1, std::memory_order_release); // unsigned wrap is well-defined
    }
    // Copy the most recent n samples (oldest-first) into dst. UI thread.
    void snapshot(float* dst, int n) const noexcept
    {
        if (n > kSize) n = kSize;
        const std::uint32_t w = writePos.load(std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dst[i] = buf[(size_t)((w - (std::uint32_t)n + (std::uint32_t)i) & (kSize - 1))].load(std::memory_order_relaxed);
    }

private:
    // Per-element atomics (relaxed). The writePos release/acquire still orders
    // index coordination; making each slot atomic removes the formal data race
    // on the float storage when the audio push() overlaps a UI snapshot() (a
    // torn read is UB even though it degrades to a benign spectrum glitch).
    std::atomic<float> buf[kSize] = {};
    // Unsigned so the monotonic increment wraps with well-defined modular
    // semantics on long runs (a signed int would overflow into UB after
    // ~2^31 pushes, ~12 h at 48 kHz). The power-of-two mask indexing is
    // unchanged — two's-complement masking gave the same indices.
    std::atomic<std::uint32_t> writePos { 0 };
};

class FourKEQDSP
{
public:
    static constexpr int kMaxChannels = 2;

    FourKEQDSP() = default;

    //--- lifecycle (main thread; allocates) ----------------------------------
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    //--- processing (RT-safe) -------------------------------------------------
    void processBlock(const float* const* inputs, float* const* outputs,
                      int numChannels, int numSamples) noexcept;

    // Reported latency in base-rate samples. 0 only once the bypass crossfade
    // has fully SETTLED to passthrough (a bit-exact, undelayed dry path); during
    // the ~30 ms fade, and whenever active, the oversampler latency is reported.
    // Gating on the settled smoothed power (not the raw flag) avoids a latency
    // flip mid-crossfade.
    int getLatencySamples() const noexcept
    {
        return lastSmoothedPower.load(std::memory_order_relaxed) <= 0.001f ? 0 : reportedLatency;
    }

    //--- parameters (atomic, any thread) --------------------------------------
    void setHpfFreq(float hz)     noexcept { pHpfFreq.store(hz, R); }
    void setHpfEnabled(bool on)   noexcept { pHpfEnabled.store(on ? 1.f : 0.f, R); }
    void setLpfFreq(float hz)     noexcept { pLpfFreq.store(hz, R); }
    void setLpfEnabled(bool on)   noexcept { pLpfEnabled.store(on ? 1.f : 0.f, R); }
    void setLfGain(float db)      noexcept { pLfGain.store(db, R); }
    void setLfFreq(float hz)      noexcept { pLfFreq.store(hz, R); }
    void setLfBell(bool on)       noexcept { pLfBell.store(on ? 1.f : 0.f, R); }
    void setLmGain(float db)      noexcept { pLmGain.store(db, R); }
    void setLmFreq(float hz)      noexcept { pLmFreq.store(hz, R); }
    void setLmQ(float q)          noexcept { pLmQ.store(q, R); }
    void setHmGain(float db)      noexcept { pHmGain.store(db, R); }
    void setHmFreq(float hz)      noexcept { pHmFreq.store(hz, R); }
    void setHmQ(float q)          noexcept { pHmQ.store(q, R); }
    void setHfGain(float db)      noexcept { pHfGain.store(db, R); }
    void setHfFreq(float hz)      noexcept { pHfFreq.store(hz, R); }
    void setHfBell(bool on)       noexcept { pHfBell.store(on ? 1.f : 0.f, R); }
    void setEqType(int brown0black1) noexcept { pEqType.store((float)brown0black1, R); }
    void setBypass(bool on)       noexcept { pBypass.store(on ? 1.f : 0.f, R); }
    void setInputGainDb(float db) noexcept { pInputGain.store(db, R); }
    void setOutputGainDb(float db)noexcept { pOutputGain.store(db, R); }
    void setSaturation(float pct) noexcept { pSaturation.store(pct, R); }
    void setOversampling(int mode_1x2x4x) noexcept { pOversampling.store((float)mode_1x2x4x, R); } // 0=1x,1=2x,2=4x
    void setMsMode(bool on)       noexcept { pMsMode.store(on ? 1.f : 0.f, R); }
    void setAutoGain(bool on)     noexcept { pAutoGain.store(on ? 1.f : 0.f, R); }

    //--- metering (linear peak, ~300ms release; read from any thread) ---------
    float getInputPeakL()  const noexcept { return inPeakL.load(R); }
    float getInputPeakR()  const noexcept { return inPeakR.load(R); }
    float getOutputPeakL() const noexcept { return outPeakL.load(R); }
    float getOutputPeakR() const noexcept { return outPeakR.load(R); }

    //--- spectrum taps (UI thread) --------------------------------------------
    const SpectrumRing& preSpectrum()  const noexcept { return preRing; }
    const SpectrumRing& postSpectrum() const noexcept { return postRing; }

    //--- parallel-summing EQ, shared with the UI response curve ---------------
    // The real 82E242 EQ sums fixed-Q band-pass / shelf blocks with the dry
    // signal at one node: H = 1 + sum_i K_i * F_i, K_i = 10^(G_i/20) - 1. This
    // makes band interaction, constant-Q (E) and asymmetric cut fall out of the
    // structure instead of being faked. F_i come straight from Biquad designers
    // (bandPassConstantPeak / firstOrderLowPass / firstOrderHighPass / lowPass /
    // highPass); the two helpers below are the only band-specific voicing.
    //
    // Mid-band Q voicing: E-series (Brown) is constant-Q; G-series (Black) is
    // proportional-Q (narrows on boost/cut). Single application.
    static float voicedMidQ(float gainDb, float baseQ, bool black) noexcept;
    // Parallel summing gain for a band: K = 10^(gainDb/20) - 1 (0 at unity,
    // negative for cut -> the block subtracts, giving the SSL asymmetric cut).
    static float bandK(float gainDb) noexcept { return std::pow(10.0f, 0.05f * gainDb) - 1.0f; }
    static float preWarp(float freq, double fs) noexcept;
    static int   chooseFactor(double baseSampleRate, int mode) noexcept; // mode 0=1x,1=2x,2=4x

private:
    static constexpr std::memory_order R = std::memory_order_relaxed;

    struct ChannelFilters
    {
        Biquad hpf1, hpf2, lf, lm, hm, hf, lpf, allpass;
        void reset() noexcept { hpf1.reset(); hpf2.reset(); lf.reset(); lm.reset(); hm.reset(); hf.reset(); lpf.reset(); allpass.reset(); }
    };

    void recomputeCoeffs(double osRate) noexcept; // sets both channels from a snapshot
    float calcAutoGainCompensation() const noexcept;
    // Processes up to maxBlock samples; processBlock() chunks oversized host
    // buffers through this so every output sample is written.
    void processChunk(const float* const* inputs, float* const* outputs,
                      int numChannels, int numSamples) noexcept;

    //--- config ---------------------------------------------------------------
    double baseSampleRate = 44100.0;
    int    maxBlock = 512;
    int    curFactor = 2;
    int    reportedLatency = 0;

    std::array<ChannelFilters, kMaxChannels> ch;
    Oversampler          os[kMaxChannels];
    ConsoleSaturationCore consoleSat;

    // Parallel summing gains per band (shared by both channels), set in
    // recomputeCoeffs alongside the band filter coefficients.
    float kLf = 0.f, kLm = 0.f, kHm = 0.f, kHf = 0.f;

    std::vector<float> scratchL, scratchR;

    SmoothedValue powerSmoother; // bypass crossfade
    std::atomic<float> lastSmoothedPower{ 1.0f }; // settled crossfade state for latency gating
    bool lastHpfEnabled = false;
    bool lastLpfEnabled = false;

    // Cached auto-gain. calcAutoGainCompensation() is a ~28-point complex
    // response scan — too costly to run every block. It only moves when a
    // band/filter param moves, so cache it keyed on a snapshot of those raw
    // params and re-scan only on change (invalidated in reset() so a re-prepare
    // at a new sample rate re-scans). No extra smoothing needed: the core steps
    // coefficients per block (no coeff interpolation), so auto-gain stepping in
    // lock-step with them introduces no new discontinuity.
    struct AutoGainSnapshot
    {
        float p[18] = {};
        bool operator!=(const AutoGainSnapshot& o) const noexcept
        { for (int i = 0; i < 18; ++i) if (p[i] != o.p[i]) return true; return false; }
    };
    AutoGainSnapshot autoGainSnap_;
    float autoCompCached_ = 1.0f;
    bool  autoCompValid_  = false;

    // E-series transformer core saturation (Brown only): the iron saturates
    // where flux is highest (LF), so the added odd harmonics come from an LF
    // flux estimate. Ported from Multi-Q's British mode.
    float xfmrFlux[kMaxChannels] = { 0.0f, 0.0f }; // per-channel LF flux state
    float xfmrLpCoef = 0.0f;                        // one-pole LF estimate coeff @ osRate

    //--- metering -------------------------------------------------------------
    std::atomic<float> inPeakL{0.f}, inPeakR{0.f}, outPeakL{0.f}, outPeakR{0.f};
    float meterDecay = 1.0f;

    SpectrumRing preRing, postRing;

    //--- parameter atomics ----------------------------------------------------
    std::atomic<float> pHpfFreq{20.f}, pHpfEnabled{0.f}, pLpfFreq{20000.f}, pLpfEnabled{0.f};
    std::atomic<float> pLfGain{0.f}, pLfFreq{100.f}, pLfBell{0.f};
    std::atomic<float> pLmGain{0.f}, pLmFreq{600.f}, pLmQ{0.7f};
    std::atomic<float> pHmGain{0.f}, pHmFreq{2000.f}, pHmQ{0.7f};
    std::atomic<float> pHfGain{0.f}, pHfFreq{8000.f}, pHfBell{0.f};
    std::atomic<float> pEqType{0.f}, pBypass{0.f};
    std::atomic<float> pInputGain{0.f}, pOutputGain{0.f}, pSaturation{0.f};
    std::atomic<float> pOversampling{0.f}, pMsMode{0.f}, pAutoGain{1.f};
};

} // namespace duskaudio

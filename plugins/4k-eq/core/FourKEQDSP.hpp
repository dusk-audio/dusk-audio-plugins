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
        for (auto& v : buf) v = 0.0f;
        writePos.store(0, std::memory_order_relaxed);
    }
    void push(float x) noexcept
    {
        const int w = writePos.load(std::memory_order_relaxed);
        buf[(size_t)(w & (kSize - 1))] = x;
        writePos.store(w + 1, std::memory_order_release);
    }
    // Copy the most recent n samples (oldest-first) into dst. UI thread.
    void snapshot(float* dst, int n) const noexcept
    {
        if (n > kSize) n = kSize;
        const int w = writePos.load(std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dst[i] = buf[(size_t)((w - n + i) & (kSize - 1))];
    }

private:
    float buf[kSize] = {};
    std::atomic<int> writePos { 0 };
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

    // Reported latency in base-rate samples. 0 while bypass is engaged so a
    // settled bypass is a bit-exact, undelayed passthrough (host PDC off).
    int getLatencySamples() const noexcept
    {
        return pBypass.load(std::memory_order_relaxed) > 0.5f ? 0 : reportedLatency;
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
    void setOversampling(int x2_0_x4_1) noexcept { pOversampling.store((float)x2_0_x4_1, R); }
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

    //--- coefficient designers, shared with the UI response curve -------------
    // Console-voiced band coefficients (mode-dependent Q). Exposed so the UI
    // can evaluate the exact same magnitude the audio path produces.
    static BiquadCoeffs consolePeak (double fs, float freq, float q, float gainDb, bool black) noexcept;
    static BiquadCoeffs consoleShelf(double fs, float freq, float q, float gainDb, bool high, bool black) noexcept;
    static float dynamicQ(float gainDb, float baseQ) noexcept;
    static float preWarp(float freq, double fs) noexcept;
    static int   chooseFactor(double baseSampleRate, bool want4x) noexcept;

private:
    static constexpr std::memory_order R = std::memory_order_relaxed;

    struct ChannelFilters
    {
        Biquad hpf1, hpf2, lf, lm, hm, hf, lpf, allpass;
        void reset() noexcept { hpf1.reset(); hpf2.reset(); lf.reset(); lm.reset(); hm.reset(); hf.reset(); lpf.reset(); allpass.reset(); }
    };

    void recomputeCoeffs(double osRate) noexcept; // sets both channels from a snapshot
    float calcAutoGainCompensation() const noexcept;

    //--- config ---------------------------------------------------------------
    double baseSampleRate = 44100.0;
    int    maxBlock = 512;
    int    curFactor = 2;
    int    reportedLatency = 0;

    std::array<ChannelFilters, kMaxChannels> ch;
    Oversampler          os[kMaxChannels];
    ConsoleSaturationCore consoleSat;

    std::vector<float> scratchL, scratchR;

    SmoothedValue powerSmoother; // bypass crossfade
    bool lastHpfEnabled = false;
    bool lastLpfEnabled = false;

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

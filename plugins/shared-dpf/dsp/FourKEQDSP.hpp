// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// FourKEQDSP.hpp — framework-free 4K console EQ core (C++17, no JUCE/DPF).
//
// Reference-calibrated DPF implementation. Isolated band/filter laws, shared
// LF/LM and HM/HF stage interactions, native nonlinear residue, and overload
// rails are fitted from hosted SSL E-channel measurements. The EQ and
// saturation chain is oversampled (>=2x) per the project "no EQ cramping" rule.
//
// Signal flow (reproduces FourKEQ::processBlock):
//   in-meter -> input gain -> [pre-EQ spectrum tap] -> (M/S encode) ->
//   calibrated HPF -> OVERSAMPLE{ LF -> LM -> HM -> HF -> LPF ->
//   native EQ-stage color } -> measured rail -> (M/S decode) ->
//   output gain*autogain -> [post-EQ spectrum tap] -> bypass crossfade -> out.
//
// Contract: prepare()/reset() on the main thread; processBlock() RT-safe
// (no alloc/lock/IO); set*() atomic from any thread. In-place safe.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "DuskDenormals.hpp"
#include "DuskSmoothed.hpp"
#include "DuskFilters.hpp"
#include "DuskOversampler.hpp"
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
        return lastSmoothedPower.load(std::memory_order_relaxed) <= 0.001f
                   ? 0 : reportedLatency.load(std::memory_order_relaxed);
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

    //--- measured EQ calibration, shared with the UI response curve -----------
    enum class Band { LF = 0, LM, HM, HF };
    static float calibratedEqFrequency(float controlHz, float controlGainDb, Band band,
                                       bool black, bool bell) noexcept;
    // Inverse of calibratedEqFrequency(). Factory/user presets are authored in
    // audible Hz, while the shipped host parameter remains the original
    // control coordinate for session/automation compatibility.
    static float controlForCalibratedEqFrequency(float frequencyHz, float controlGainDb,
                                                 Band band, bool black, bool bell) noexcept;
    static float calibratedEqGain(float controlDb, Band band,
                                  bool black, bool bell) noexcept;
    static float calibratedEqQ(float controlQ, float controlHz,
                               float controlGainDb, Band band,
                               bool black, bool bell) noexcept;
    // The original circuit shares one active stage between LF/LM and another
    // between HM/HF. These two small correction sections reproduce the
    // measured interaction left after the isolated-band biquads. For the low
    // pair, firstShape is LF bell (0/1) and secondShape is LM Q. For the high
    // pair, firstShape is HM Q and secondShape is HF bell (0/1).
    static std::array<BiquadCoeffs, 3> calibratedPairCorrection(
        double sampleRate, bool highPair, bool black,
        float firstGainDb, float firstControlHz, float firstShape,
        float secondGainDb, float secondControlHz, float secondShape) noexcept;
    static float calibratedFilterFrequency(float controlHz, bool highPass,
                                           bool black) noexcept;
    static float controlForCalibratedFilterFrequency(float frequencyHz, bool highPass,
                                                     bool black) noexcept;
    static float calibratedHpfTrimDb(float controlHz, bool black) noexcept;
    static float calibratedFilterQ(bool highPass, bool black) noexcept;

    // Control-coordinate snapshot for the response curve: the values the UI
    // shows on its knobs, NOT calibrated units. calibratedResponseDb() applies
    // every calibration itself, so the two UIs cannot drift apart by applying a
    // different subset.
    //
    // The shape fields (lfBell/hfBell/oversampling) stay FLOAT rather than
    // bool/int because that is what the parameter arrays hold, and both the
    // `> 0.5f` tests and calibratedPairCorrection's firstShape/secondShape take
    // the raw value. Narrowing them here would change what the correction
    // sections receive.
    struct CurveControls
    {
        double baseSampleRate = 48000.0; // HOST rate; oversampling applied below
        float  oversampling   = 0.0f;    // DSP mode: 0=1x, 1=2x, 2=4x
        bool   black          = false;   // Brown(false) / Black(true) voicing
        bool   hpfEnabled     = false;
        bool   lpfEnabled     = false;
        float  hpfFreq = 0.0f, lpfFreq = 0.0f;
        float  lfGain  = 0.0f, lfFreq  = 0.0f, lfBell = 0.0f;
        float  lmGain  = 0.0f, lmFreq  = 0.0f, lmQ    = 1.0f;
        float  hmGain  = 0.0f, hmFreq  = 0.0f, hmQ    = 1.0f;
        float  hfGain  = 0.0f, hfFreq  = 0.0f, hfBell = 0.0f;
    };

    // The ONE implementation of the drawn response, mirroring recomputeCoeffs().
    // Both FourKEQUI and Multi-Q's British-mode preview delegate here; they used
    // to carry byte-identical copies of this body, which is how Multi-Q's curve
    // silently stayed on the pre-calibration model after the core was rewritten
    // (GH #160). A future model change has exactly one place to land.
    //
    // voicedMidQ/bandK used to live here too, so that preview could draw the
    // older PARALLEL topology (summed bandK-weighted blocks) independently of
    // 4K EQ 2. Both are gone with it: keeping them would only leave a second,
    // wrong model for a future reader to wire back up.
    static float calibratedResponseDb(const CurveControls& c, float freq) noexcept;

    static int   chooseFactor(double baseSampleRate, int mode) noexcept; // mode 0=1x,1=2x,2=4x

private:
    static constexpr std::memory_order R = std::memory_order_relaxed;

    struct ChannelFilters
    {
        Biquad hpf1, hpf2, lf, lm, lowCorrection1, lowCorrection2, lowCorrection3;
        Biquad hm, hf, highCorrection1, highCorrection2, highCorrection3, lpf;
        void reset() noexcept
        {
            hpf1.reset(); hpf2.reset();
            lf.reset(); lm.reset(); lowCorrection1.reset(); lowCorrection2.reset(); lowCorrection3.reset();
            hm.reset(); hf.reset(); highCorrection1.reset(); highCorrection2.reset(); highCorrection3.reset();
            lpf.reset();
        }
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
    // Read by getLatencySamples() (host/main thread), written in prepare()/processChunk()
    // (audio thread) → atomic to avoid a cross-thread data race, like lastSmoothedPower.
    std::atomic<int> reportedLatency{0};

    std::array<ChannelFilters, kMaxChannels> ch;
    Oversampler          os[kMaxChannels];
    ConsoleSaturationCore consoleSat;

    float hpfTrimGain = 1.0f;

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

    //--- metering -------------------------------------------------------------
    std::atomic<float> inPeakL{0.f}, inPeakR{0.f}, outPeakL{0.f}, outPeakR{0.f};
    float meterDecay = 1.0f;

    SpectrumRing preRing, postRing;

    //--- parameter atomics ----------------------------------------------------
    std::atomic<float> pHpfFreq{16.f}, pHpfEnabled{0.f}, pLpfFreq{15201.f}, pLpfEnabled{0.f};
    std::atomic<float> pLfGain{0.f}, pLfFreq{200.f}, pLfBell{0.f};
    std::atomic<float> pLmGain{0.f}, pLmFreq{1000.f}, pLmQ{1.5f};
    std::atomic<float> pHmGain{0.f}, pHmFreq{3000.f}, pHmQ{1.5f};
    std::atomic<float> pHfGain{0.f}, pHfFreq{8000.f}, pHfBell{0.f};
    std::atomic<float> pEqType{0.f}, pBypass{0.f};
    std::atomic<float> pInputGain{0.f}, pOutputGain{0.f}, pSaturation{0.f};
    std::atomic<float> pOversampling{2.f}, pMsMode{0.f}, pAutoGain{0.f};
};

} // namespace duskaudio

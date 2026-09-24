// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// FourKEQDSP.hpp — framework-free 4K console EQ core (C++17, no JUCE/DAF).
//
// Reference-calibrated DAF implementation. Isolated band/filter laws, shared
// LF/LM and HM/HF stage interactions, native nonlinear residue, and overload
// rails are fitted from hosted British console E-series channel measurements. The EQ and
// saturation chain can be oversampled; below kReferenceDesignRate the EQ
// sections are matched-magnitude designs, so no rate cramps them.
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
    void setHpfEnabled(bool on)   noexcept { pHpfEnabled.store(on ? 1.f : 0.f, R); }
    void setLpfEnabled(bool on)   noexcept { pLpfEnabled.store(on ? 1.f : 0.f, R); }

    // Filter frequency as a hardware DIAL position, mapped onto the filter's
    // design frequency by the measured law (calibratedFilterFrequency), which
    // runs flat past the end of each table. A Brown HPF dial at 80 is 3 dB
    // down at 26 Hz.
    void setHpfFreq(float dial) noexcept { pHpfFreq.store(dial, R); pHpfFreqHz.store(0.f, R); }
    void setLpfFreq(float dial) noexcept { pLpfFreq.store(dial, R); pLpfFreqHz.store(0.f, R); }

    // Filter frequency in Hz: the filter's -3 dB point against its own
    // passband, so the HPF's flat insertion trim does not move it. That is
    // how a filter frequency is quoted, and it is the one definition both HPF
    // voicings share: Black's is a first-order and a second-order section at
    // two different corners, so it has no single natural corner to name. The
    // measured shape is kept (each filter's Q, and Black's first-order split,
    // do not vary along the dial), so hz sets the design frequency through a
    // fixed ratio per filter (filterCornerRatio). The HPF's insertion trim is
    // read along the measured table at that design frequency, held at the
    // table's ends. Inside the table this plays what the dial API plays at the
    // position with that corner, and past its ends the filter keeps moving
    // with the trim where the measured law leaves it. The -3 dB point is the
    // calibrated analog response's; the sections hold it as they hold the rest
    // of the curve, so a 1x LPF corner above a quarter of the rate lands
    // within the matched design's accuracy (0.7 dB in level). The last setter
    // called for a filter wins. hz is clamped to kMaxFilterHz and from below
    // to kMinLpfHz, the float limit at the oversampled rate the LPF runs at
    // (as kMinBandHz), or kMinHpfHz, under the lowest corner the dial API
    // reaches (Brown's 9.4 Hz) so every dial position has an Hz equivalent.
    static constexpr float kMinHpfHz = 5.0f;
    static constexpr float kMinLpfHz = 20.0f;
    static constexpr float kMaxFilterHz = 40000.0f;
    void setHpfFreqHz(float hz) noexcept { pHpfFreqHz.store(sanitizeFilterHz(hz, true), R); }
    void setLpfFreqHz(float hz) noexcept { pLpfFreqHz.store(sanitizeFilterHz(hz, false), R); }

    void setLfGain(float db)      noexcept { pLfGain.store(db, R); }
    void setLfBell(bool on)       noexcept { pLfBell.store(on ? 1.f : 0.f, R); }
    void setLmGain(float db)      noexcept { pLmGain.store(db, R); }
    void setLmQ(float q)          noexcept { pLmQ.store(q, R); }
    void setHmGain(float db)      noexcept { pHmGain.store(db, R); }
    void setHmQ(float q)          noexcept { pHmQ.store(q, R); }
    void setHfGain(float db)      noexcept { pHfGain.store(db, R); }
    void setHfBell(bool on)       noexcept { pHfBell.store(on ? 1.f : 0.f, R); }

    // Band frequency as a hardware DIAL position: the value printed on the
    // reference's frequency knob, mapped onto the played frequency by the
    // measured dial law (calibratedEqFrequency). That law is uneven and runs
    // flat past the end of each measured table.
    void setLfFreq(float dial) noexcept { pLfFreq.store(dial, R); pLfFreqHz.store(0.f, R); }
    void setLmFreq(float dial) noexcept { pLmFreq.store(dial, R); pLmFreqHz.store(0.f, R); }
    void setHmFreq(float dial) noexcept { pHmFreq.store(dial, R); pHmFreqHz.store(0.f, R); }
    void setHfFreq(float dial) noexcept { pHfFreq.store(dial, R); pHfFreqHz.store(0.f, R); }

    // Band frequency in Hz (dusk-audio-plugins#288). The band sits where hz
    // says rather than where the dial law puts it:
    //   - a bell (LM, HM, and LF/HF in bell mode) is centred on hz;
    //   - a shelf's hz is its corner, the pole frequency: the RBJ design
    //     (half-gain) frequency times sqrt(A) for HF, divided by sqrt(A) for
    //     LF, A = 10^(gainDb / 40). From about 3 dB of boost or cut, roughly
    //     half to two-thirds of it is in there and nearly all of it two octaves
    //     further out.
    // Both hold exactly at kEqReferenceGainDb. Gain moves the band the way the
    // dial API does and no further: the design frequency is scaled by the
    // measured frequencyAtGain ratio, which moves a bell's centre by -0.4% to
    // +2.3% over 0..15 dB and holds a shelf's corner within -3.5% to +3.7%
    // from 3 to 15 dB (up to 8% below 3 dB, where a shelf barely has one).
    // Q and the pair interaction come from the dial position that plays hz,
    // clamped to the ends of the measured table, so inside the table this
    // plays exactly what the dial API plays at that position, and past its
    // ends the band keeps moving while its Q and the interaction corrections
    // stay where the measured law leaves them. The last setter called for a
    // band wins. hz is clamped to kMinBandHz..kMaxBandHz.
    static constexpr float kMinBandHz = 20.0f;
    static constexpr float kMaxBandHz = 40000.0f;
    void setLfFreqHz(float hz) noexcept { pLfFreqHz.store(sanitizeBandHz(hz), R); }
    void setLmFreqHz(float hz) noexcept { pLmFreqHz.store(sanitizeBandHz(hz), R); }
    void setHmFreqHz(float hz) noexcept { pHmFreqHz.store(sanitizeBandHz(hz), R); }
    void setHfFreqHz(float hz) noexcept { pHfFreqHz.store(sanitizeBandHz(hz), R); }
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

    // Sections designed at or above this rate use the RBJ bilinear designs the
    // reference captures were fitted with: 4x at 44.1/48 kHz, 2x at 88.2/96 kHz,
    // 1x from 176.4 kHz. That path is bit-identical to the calibrated core, so
    // the reference parity holds there. The frozen copy of that core the tests
    // compare against shares the live Biquad designers, oversampler and console
    // saturation, so the comparison proves only this file unchanged; the 4x
    // golden aggregates in FourKEQDSPTests pin the shared pieces. Below this
    // rate every band, the LPF and the pair-correction sections use Biquad's
    // matched-magnitude designs, which keep the same analog curves up to
    // Nyquist instead of cramping (dusk-audio-plugins#289). The HPF is
    // untouched: it sits far below any Nyquist it runs at.
    static constexpr double kReferenceDesignRate = 176400.0;

    // The gain the dense frequency sweep was measured at (the frequency[] and
    // qAtFrequency[] tables), where the measured frequencyAtGain ratio is 1.
    // The Hz API's frequency holds exactly here.
    static constexpr float kEqReferenceGainDb = 7.5f;

    static float calibratedEqFrequency(float controlHz, float controlGainDb, Band band,
                                       bool black, bool bell) noexcept;
    // RBJ design frequency the Hz API (setXxFreqHz) runs a band at, for the
    // requested hz at controlGainDb. Equal to hz for a bell at the reference
    // gain; see setLfFreqHz for the shelf corner.
    static float calibratedEqFrequencyForHz(float hz, float controlGainDb, Band band,
                                            bool black, bool bell) noexcept;
    // The Hz that plays what dial position controlHz plays: migrates a stored
    // dial position to the Hz API. Gain-independent, since both APIs apply the
    // same gain law. Inside the measured table the round trip through the Hz
    // API reproduces the dial API's band exactly.
    static float hzForCalibratedEqControl(float controlHz, Band band,
                                          bool black, bool bell) noexcept;
    // Inverse of calibratedEqFrequency(): the dial position whose measured
    // frequency at controlGainDb is frequencyHz, clamped to the dial's ends.
    // 4K EQ 2 user presets saved before #288 stored that frequency.
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
    // One EQ section as the calibration defines it: an RBJ-cookbook analog
    // prototype, before it is realized as coefficients at a sample rate. The
    // calibration is rate-independent; only the realization looks at the rate
    // (see kReferenceDesignRate).
    struct SectionDesign
    {
        enum class Shape { Peak, LowShelf, HighShelf, LowPass };
        Shape shape = Shape::Peak;
        float freq = 1000.0f, gainDb = 0.0f, q = 0.70710678f;
    };
    struct SectionDesigns
    {
        SectionDesign bands[4];   // LF, LM, HM, HF
        std::array<SectionDesign, 3> lowCorrection, highCorrection;
        SectionDesign lpf;
    };

    static float calibratedFilterFrequency(float controlHz, bool highPass,
                                           bool black) noexcept;
    static float controlForCalibratedFilterFrequency(float frequencyHz, bool highPass,
                                                     bool black) noexcept;
    // The -3 dB point of a filter over its design frequency, from the analog
    // prototype: 1/sqrt(x) for the Brown HPF and sqrt(x) for the LPF, with x
    // the root of x^2 - (2 - 1/Q^2) x - 1 = 0, and the root of the cubic the
    // two Black HPF sections give.
    static float filterCornerRatio(bool highPass, bool black) noexcept;
    // Design frequency the Hz API (setHpfFreqHz / setLpfFreqHz) runs a filter
    // at for the requested hz.
    static float calibratedFilterFrequencyForHz(float hz, bool highPass,
                                                bool black) noexcept;
    // The Hz that plays what dial position controlHz plays: migrates a stored
    // filter dial to the Hz API.
    static float hzForCalibratedFilterControl(float controlHz, bool highPass,
                                              bool black) noexcept;
    static float calibratedHpfTrimDb(float controlHz, bool black) noexcept;
    static float calibratedFilterQ(bool highPass, bool black) noexcept;

    // Control-coordinate snapshot for the response curve: the values the UI
    // shows on its knobs, NOT calibrated units. designCurve() applies
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
        // false: hpfFreq / lpfFreq are dial positions (setHpfFreq).
        // true:  they are Hz, drawn the way setHpfFreqHz / setLpfFreqHz play.
        bool   hpfFreqInHz = false, lpfFreqInHz = false;
        float  lfGain  = 0.0f, lfFreq  = 0.0f, lfBell = 0.0f;
        float  lmGain  = 0.0f, lmFreq  = 0.0f, lmQ    = 1.0f;
        float  hmGain  = 0.0f, hmFreq  = 0.0f, hmQ    = 1.0f;
        float  hfGain  = 0.0f, hfFreq  = 0.0f, hfBell = 0.0f;
        // false: lfFreq..hfFreq are dial positions (setLfFreq).
        // true:  they are Hz, drawn the way setLfFreqHz plays them, except
        //        the bands set in dialBands (bit 0 LF .. bit 3 HF), which
        //        stay dial positions. A band's Hz equivalent designs the same
        //        band but not always the same pair correction, so a plugin
        //        that keeps some bands on the dial API draws them on it.
        bool   bandFrequenciesInHz = false;
        unsigned dialBands = 0;
        // Saturation knob percent, 0..100, as setSaturation() receives it.
        // Feeds the console saturator's broadband insertion loss into the drawn
        // curve (GH #169). Defaulting to 0 is the SAFE default rather than an
        // arbitrary one: 0 still yields the always-on native loss, which is the
        // only state the standalone 4K EQ can be in.
        float  saturation = 0.0f;
    };

    // Console saturator drive actually presented to ConsoleSaturationCore, and
    // the response that drive costs. Both processBlock() and designCurve() call
    // consoleSatAmount(), so the audio path and the drawn curve cannot disagree,
    // the same reason designCurve() itself is the one response model.
    //
    // The loss is NOT a nonlinearity artefact, which is why a closed form works
    // at every setting rather than only at the saturation floor. It is a linear
    // dry/wet mix around a linear wet branch, all of it at the end of
    // ConsoleSaturationCore::processSample:
    //     y = dcBlocker(y)                          // 5 Hz, one pole one zero
    //     y *= 1 / (1 + drive * 0.15)               // makeup trim
    //     result = input * (1 - wetMix) + y * wetMix, wetMix = min(1, drive * 1.4)
    //
    // The DC blocker is why this is a complex sum and not a scalar trim. Mixing
    // a phase-shifted wet branch against a flat dry path is not a magnitude
    // product, and the difference lands inside the drawn range: modelling only
    // the flat term left the curve 0.16 dB optimistic at 20 Hz in Brown and
    // 0.28 dB at full saturation. Measured against the processed output, the
    // flat term alone is good to 0.02 dB above 100 Hz and wrong below it. GH
    // #169's own "constant from 30 Hz" reading does not survive contact with a
    // sine sweep: 30 Hz is already 0.08 dB down on 1 kHz.
    //
    // Deliberately NOT modelled, because none of it belongs on a magnitude plot:
    // the ADAA waveshaper term (harmonics, not fundamental gain), the
    // pre/de-emphasis shelf pair, and the hard rail clamp (inactive below about
    // +3.5 dBFS). What they leave behind is the residual between this model and
    // a measured sine sweep: worst case 0.0145 dB, at 20 Hz and full
    // saturation, against 0.28 dB before the DC blocker was modelled.
    struct ConsoleSatResponse
    {
        double dry     = 1.0;   // 1 - wetMix
        double wet     = 0.0;   // wetMix * makeup trim
        double dcCoeff = 0.0;   // DC blocker pole, at the OVERSAMPLED rate
    };

    // The ONE implementation of the drawn response, mirroring recomputeCoeffs().
    // Both FourKEQUI and Multi-Q's British-mode preview delegate here; they used
    // to carry byte-identical copies of the model, which is how Multi-Q's curve
    // silently stayed on the pre-calibration model after the core was rewritten
    // (GH #160). A future model change has exactly one place to land.
    //
    // voicedMidQ/bandK used to live here too, so that preview could draw the
    // older PARALLEL topology (summed bandK-weighted blocks) independently of
    // 4K EQ 2. Both are gone with it: keeping them would only leave a second,
    // wrong model for a future reader to wire back up.
    //
    // Split into design and evaluate because the graph walks a few hundred
    // frequencies per repaint and the design half does not vary across them:
    // four calibrated band designs, up to two three-section pair corrections,
    // the two filters and the HPF trim, all identical at every point.
    struct CurveCoeffs
    {
        double sampleRate     = 48000.0; // oversampled; bands and LPF live here
        double baseSampleRate = 48000.0; // host rate; the core designs the HPF here
        BiquadCoeffs hpfFirstOrder{}, hpf{}, lpf{};
        bool hasHpfFirstOrder = false;   // Black voicing only
        bool hasHpf = false, hasLpf = false;
        double hpfTrimLinear = 1.0;
        // Always-on console saturator. Its mix coefficients and DC-blocker pole
        // do not vary with frequency, so they are designed here; the frequency
        // sweep happens in consoleSatMagnitude().
        ConsoleSatResponse saturation{};
        BiquadCoeffs bands[4]{};         // LF, LM, HM, HF
        bool hasBand[4] = { false, false, false, false };
        std::array<BiquadCoeffs, 3> lowCorrection{}, highCorrection{};
        bool hasLowCorrection = false, hasHighCorrection = false;
    };
    // There is deliberately NO one-shot calibratedResponseDb(controls, freq)
    // convenience wrapper. It existed briefly and was the obvious thing for the
    // next caller to loop over, which is the redesign-per-point cost this split
    // exists to remove. A single probe is one readable line:
    //     curveDbAt(designCurve(c), freq)
    static CurveCoeffs designCurve(const CurveControls& c) noexcept;
    static float curveDbAt(const CurveCoeffs& designed, float freq) noexcept;

    static float consoleSatAmount(bool black, float saturationPercent) noexcept;
    static ConsoleSatResponse consoleSatResponse(float satAmt, double oversampledRate) noexcept;
    // omega is normalised to the oversampled rate, matching the band sections.
    static double consoleSatMagnitude(const ConsoleSatResponse& r, double omega) noexcept;

    static int   chooseFactor(double baseSampleRate, int mode) noexcept; // mode 0=1x,1=2x,2=4x

private:
    friend struct FourKEQDSPTestAccess; // tests/FourKEQDSPTests.cpp reads the running coefficients

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

    // Every parameter the section coefficients depend on, read once per block.
    // All floats, so the struct has no padding and compares with memcmp: NaN
    // compares equal to itself, which keeps a NaN parameter from forcing a
    // redesign every block.
    struct CoeffInputs
    {
        float hpfFreq, hpfFreqHz, lpfFreq, lpfFreqHz;
        float lfGain, lfFreq, lfFreqHz, lfBell;
        float lmGain, lmFreq, lmFreqHz, lmQ;
        float hmGain, hmFreq, hmFreqHz, hmQ;
        float hfGain, hfFreq, hfFreqHz, hfBell;
        float eqType;
    };
    static_assert(sizeof(CoeffInputs) == 21 * sizeof(float), "CoeffInputs must stay padding-free");

    static float sanitizeBandHz(float hz) noexcept
    {
        // > 0 selects the Hz API for the band, so every input, NaN included,
        // lands on a positive finite frequency. The 20 Hz floor is the float
        // limit, not a voicing choice: at the 4x rate a section much below it
        // rounds to a pole pair within ~1e-8 of z = 1, a DC integrator.
        return hz > kMinBandHz ? (hz < kMaxBandHz ? hz : kMaxBandHz) : kMinBandHz;
    }

    // > 0 selects the Hz API for the filter; see sanitizeBandHz.
    static float sanitizeFilterHz(float hz, bool highPass) noexcept
    {
        const float lo = highPass ? kMinHpfHz : kMinLpfHz;
        return hz > lo ? (hz < kMaxFilterHz ? hz : kMaxFilterHz) : lo;
    }

    CoeffInputs loadCoeffInputs() const noexcept;
    static CoeffInputs coeffInputsFor(const CurveControls& c) noexcept;
    // The calibration: every band, pair-correction and LPF section, rate-free.
    static SectionDesigns designSections(const CoeffInputs& in) noexcept;
    // The realization at fs: RBJ at kReferenceDesignRate and up, matched below.
    static BiquadCoeffs realize(const SectionDesign& d, double fs) noexcept;
    // Designs every section at osRate and sets both channels.
    void recomputeCoeffs(const CoeffInputs& in, double osRate) noexcept;
    float calcAutoGainCompensation(const CoeffInputs& in, bool hpfEn, bool lpfEn) const noexcept;
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

    // Cached section coefficients. The matched designs cost about 100 ns a
    // section, so the sections are redesigned only when a CoeffInputs field or
    // the oversampling factor moved since the block that last designed them
    // (prepare() invalidates, which covers a base-rate change). The
    // coefficients a block runs are the same either way, so this is output
    // identical to redesigning every block; the core has never interpolated
    // coefficients, and a parameter move still lands on the next block
    // boundary exactly as before. The first block after prepare() designs
    // under processBlock's flush-to-zero mode, as every block used to.
    CoeffInputs coeffInputs_{};
    int  coeffFactor_ = 0;
    bool coeffsValid_ = false;

    // Cached auto-gain. calcAutoGainCompensation() is a ~28-point complex
    // response scan — too costly to run every block. It only moves when a
    // band/filter param moves, so cache it keyed on a snapshot of those raw
    // params and re-scan only on change (invalidated in reset() so a re-prepare
    // at a new sample rate re-scans). No extra smoothing needed: the core steps
    // coefficients per block (no coeff interpolation), so auto-gain stepping in
    // lock-step with them introduces no new discontinuity.
    struct AutoGainSnapshot
    {
        CoeffInputs coeffs;
        float hpfEnabled, lpfEnabled, factor;
    };
    static_assert(sizeof(AutoGainSnapshot) == 24 * sizeof(float), "AutoGainSnapshot must stay padding-free");
    AutoGainSnapshot autoGainSnap_{};
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
    // Hz API values; 0 = the band or filter follows its dial position above.
    std::atomic<float> pLfFreqHz{0.f}, pLmFreqHz{0.f}, pHmFreqHz{0.f}, pHfFreqHz{0.f};
    std::atomic<float> pHpfFreqHz{0.f}, pLpfFreqHz{0.f};
    std::atomic<float> pEqType{0.f}, pBypass{0.f};
    std::atomic<float> pInputGain{0.f}, pOutputGain{0.f}, pSaturation{0.f};
    std::atomic<float> pOversampling{2.f}, pMsMode{0.f}, pAutoGain{0.f};
};

} // namespace duskaudio

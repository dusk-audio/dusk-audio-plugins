// TapeEchoDSP.hpp
// Component-modeled vintage three-head tape echo with spring reverb.
//
// Framework-free: standard C++17 only. No JUCE, no host dependencies.
// Designed to be wrapped by DAF, raw CLAP, or any other plugin shell.
//
// Signal flow (stereo input, mono wet paths, stereo dry path):
//
//   L+R ─► record preamp (odd soft clip + DC block) ───────┬──► reverb send
//                                                         │
//        ┌────────────────────────────────────────────────┘
//        ▼
//   [+]──► record EQ (HP + speed-dependent LP) ──► tape saturation ──► TAPE
//    ▲                                                                │
//    │                                                     3 read heads (Hermite)
//    │                                              T · {1.00, 1.9117, 2.7612}
//    └── intensity · softClip(head sum)  ◄─────────────────┴──┐
//                                                             ▼
//                                    bass/treble shelves (echo path only)
//                                                             ▼
//   out L/R = dry·input L/R + pan(echoLevel·echoEQ)
//                           + pan(reverbLevel·spring(reverb send))
//
// The record EQ and tape saturation live INSIDE the feedback loop, so each
// repeat progressively darkens and compresses — this is what lets high
// Intensity settings bloom into stable, warm self-oscillation instead of
// exploding numerically.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

// Shared DSP primitives (namespace duskaudio) — these replace the building
// blocks that used to be defined locally here. All lifted from this very file
// during the shared-daf extraction, so consuming them is bit-identical.
#include "DuskSmoothed.hpp"    // SmoothedValue
#include "DuskFilters.hpp"     // OnePoleLP/HP, DCBlocker, Biquad (shelves)
#include "DuskOversampler.hpp" // HalfbandFIR, hbtaps::kA/kB
#include "../daf-plugin/TapeEchoParams.hpp" // shared discrete-value helpers

namespace duskaudio
{

//==============================================================================
// Small framework-free building blocks now live in plugins/shared-daf/dsp:
//   SmoothedValue            -> DuskSmoothed.hpp
//   OnePoleLP / OnePoleHP    -> DuskFilters.hpp
//   DCBlocker                -> DuskFilters.hpp (default R = 0.9975, ~20 Hz @ 48k;
//                               tape-echo keeps the default, so it is unchanged)
//   ShelfFilter              -> DuskFilters.hpp Biquad + Biquad::shelfSlope1()
//   HalfbandFIR + hb taps    -> DuskOversampler.hpp (HalfbandFIR, hbtaps::kA/kB)
// The shared versions were lifted from this file, so behavior is bit-identical.
//==============================================================================

//==============================================================================
// Polyphase IIR halfband for 2x resampling: two parallel chains of first-order
// allpasses in z^2 (the classic two-path elliptic halfband), H(z) =
// 0.5 * (A0(z^2) + z^-1 A1(z^2)), even coefficients in A0, odd in A1. Unlike a
// linear-phase FIR halfband it is causal with no pre-ringing -- the spring
// tank's first arrival must not be preceded by any output (the reference's
// tank is digital silence until its arrival) -- and it costs one multiply per
// coefficient at the low rate. Coefficients come from the standard elliptic
// halfband design for a given transition width and stopband attenuation.
//==============================================================================
template <int N>
class IirHalfband
{
public:
    explicit IirHalfband(const float* coefficients) noexcept : coef(coefficients) {}

    void reset() noexcept
    {
        for (int i = 0; i < N; ++i) { x[i] = 0.0f; y[i] = 0.0f; }
    }

    // Decimate: the older and newer of two input samples -> one output.
    float down(float older, float newer) noexcept
    {
        float a = newer, b = older;
        for (int i = 0; i < N; i += 2) a = section(i, a);
        for (int i = 1; i < N; i += 2) b = section(i, b);
        return 0.5f * (a + b);
    }

    // Interpolate: one input -> the two outputs at twice the rate, in order.
    void up(float in, float& first, float& second) noexcept
    {
        float a = in, b = in;
        for (int i = 0; i < N; i += 2) a = section(i, a);
        for (int i = 1; i < N; i += 2) b = section(i, b);
        first = a;
        second = b;
    }

    bool isQuiet(float floor) const noexcept
    {
        for (int i = 0; i < N; ++i)
            if (std::abs(x[i]) >= floor || std::abs(y[i]) >= floor)
                return false;
        return true;
    }

    // Low-frequency group delay in samples of the faster (input) rate:
    // 2 (1 - a) / (1 + a) per section of the undelayed path.
    static double dcGroupDelay(const float* coefficients) noexcept
    {
        double d = 0.0;
        for (int i = 0; i < N; i += 2)
            d += 2.0 * (1.0 - (double)coefficients[i])
                     / (1.0 + (double)coefficients[i]);
        return d;
    }

private:
    float section(int i, float in) noexcept
    {
        const float out = (in - y[i]) * coef[i] + x[i];
        x[i] = in;
        y[i] = out;
        return out;
    }

    const float* coef;
    float x[N] {};
    float y[N] {};
};

// 100 dB stopband, transition 0.02 of the stage input rate: the stage next to
// the internal rate, flat to 0.92 of the internal Nyquist.
inline constexpr float kSpringFinalHalfband[10] = {
    0.038198144521f, 0.141848414467f, 0.284326749234f,
    0.436500581449f, 0.577049051805f, 0.695524100512f,
    0.790200596392f, 0.864465799905f, 0.923995927877f,
    0.975286561376f,
};
// 100 dB stopband, transition 0.10: the earlier stages only keep images out of
// the band the final stage passes.
inline constexpr float kSpringEarlyHalfband[6] = {
    0.039151597734f, 0.147377113601f, 0.302646848328f,
    0.482468542770f, 0.674615918547f, 0.883005025769f,
};

//==============================================================================
// Spring reverb — three parallel bidirectional dispersive waveguides.
//
// The tank runs at a reduced internal rate, as the reference's does: the host
// signal is decimated by a power of two to about 11-12 kHz (fs/4 at 44.1 and
// 48 kHz, fs/8 at 88.2 and 96 kHz), the tank and its voicing run there, and
// the result is interpolated back. Measured on the reference, its spring output
// falls off a cliff at exactly half that rate (5.5 kHz at 44.1 kHz, 6 kHz at
// 48 and 96 kHz), so the resampling filters ARE the tank's top end, not a cost
// paid to save CPU. The resampler's group delay is taken out of the outgoing
// transit and added to the returning one, so the arrivals and the round trip
// stay where they were fitted (each direction is rounded to whole
// internal-rate samples on its own; the transits were fitted with exactly
// that rounding at 44.1 and 48 kHz).
//
// Each spring explicitly propagates an outgoing and returning torsional wave.
// A fitted cascade of second-order allpasses is flat below about 1.8 kHz,
// peaks near +21 ms of one-way group delay at 3.6 kHz, and collapses by
// 4.2 kHz (an upward chirp).
// Reflection losses darken every round trip, while slightly unequal spring
// lengths keep the physical modal beating of a multi-spring tank.
//==============================================================================
class SpringReverb
{
public:
    void prepare(double sampleRate, float detune /* per-channel length scale */);
    void reset();
    float process(float in) noexcept;
    // True when every piece of state is below floor in magnitude.
    bool isSilent(float floor) const noexcept;
    // Idle skip support: clear the tank but keep the resampler's frame phase,
    // and advance that phase over samples that were not processed, so a skip
    // leaves the frame grid exactly where processing every sample would have.
    void clearKeepingPhase() noexcept;
    void advanceIdle(int samples) noexcept
    {
        resamplePhase = (resamplePhase + samples) & (resampleFactor - 1);
    }

private:
    // Section count is bounded from below by physics, not taste: each
    // second-order allpass contributes exactly 2*pi of group-delay area, so
    // realizing the reference's measured hump needs at least ~23. Fitting at
    // 24 beat both 26 and 28 on max error and on residual delay in the bands
    // that must stay flat (fit_spring_dispersion.py). Changing this requires
    // re-running the fitter -- the coefficient tables in Spring::prepare are
    // sized by it.
    static constexpr int kNumDispersionSections = 24;

    struct Spring
    {
        std::vector<float> outgoingDelay;
        std::vector<float> returningDelay;
        int outgoingWriteIdx = 0;
        int returningWriteIdx = 0;
        float driverReflection = -0.90f;
        float pickupReflection = -0.90f;
        float outputGain = 0.25f;
        std::array<Biquad, kNumDispersionSections> outgoingDispersion;
        std::array<Biquad, kNumDispersionSections> returningDispersion;
        Biquad reflectionHighPass;
        Biquad reflectionLowLoss;
        Biquad reflectionLowPass;

        // advanceSeconds moves that much transit time from the outgoing
        // to the returning path (round trip unchanged) so the resampler's
        // latency does not delay the arrivals.
        void  prepare(double fs, float oneWaySeconds,
                      float dispersionScale, float reflectionAmount,
                      float gain, double advanceSeconds,
                      const BiquadCoeffs& reflectionLoss);
        void  reset();
        float process(float x) noexcept;
        bool  isSilent(float floor) const noexcept;
    };

    // THREE springs, not four, and deliberately far apart.
    //
    // Each pair of springs beats at the difference of their round-trip rates,
    // and that beating IS the tank's audible amplitude modulation -- there is
    // no LFO anywhere in here. Four springs bunched between 22.0 and 24.2 ms
    // gave six beat pairs, all below 2 Hz (measured peaks at 0.87 and 1.40 Hz,
    // exactly the pairwise rate differences), which reads as slow warble and
    // measured 14.25 dB of low-band envelope ripple against the reference's
    // 3.12. The reference is three springs whose transits span 5.6 ms, so its
    // beats are fewer and faster (1.6 / 2.7 / 4.3 Hz) and read as texture.
    // That same 5.6 ms arrival spread is also what softens its attack.
    std::array<Spring, 3> springs;
    OnePoleHP inputHP;   // springs don't transmit deep lows
    OnePoleLP inputLP;   // dark transducer voicing
    DCBlocker dcBlock;
    std::array<Biquad, 2> outputTransducerHP;
    Biquad outputHP;
    Biquad outputVoiceLP;
    Biquad outputLowContour;
    Biquad outputBody;
    Biquad outputFourKhzSeat;
    Biquad outputLowMidBalance;
    Biquad outputOneKhzSeat;
    // The cliff below the internal Nyquist (see processInternal()).
    std::array<Biquad, 2> outputCliff;

    // Runs one internal-rate sample: the decimated input in, the tank and its
    // voicing, one internal-rate sample out.
    float processInternal(float in) noexcept;

    // Resampling between the host rate and the internal rate: factor 2^stages.
    // Every stage but the one next to the internal rate is a loose halfband
    // (it only has to keep images out of the band the last stage keeps); the
    // last stage is the steep one that, with outputCliff, sets the top end.
    static constexpr int kMaxResampleStages = 6; // factor 64 at 705.6/768 kHz
    static constexpr int kMaxResampleFactor = 1 << kMaxResampleStages;
    int resampleStages = 0;
    int resampleFactor = 1;
    int resamplePhase = 0;
    using EarlyHalfband = IirHalfband<6>;
    using FinalHalfband = IirHalfband<10>;
    std::array<EarlyHalfband, kMaxResampleStages - 1> earlyDown {
        EarlyHalfband(kSpringEarlyHalfband), EarlyHalfband(kSpringEarlyHalfband),
        EarlyHalfband(kSpringEarlyHalfband), EarlyHalfband(kSpringEarlyHalfband),
        EarlyHalfband(kSpringEarlyHalfband) };
    std::array<EarlyHalfband, kMaxResampleStages - 1> earlyUp {
        EarlyHalfband(kSpringEarlyHalfband), EarlyHalfband(kSpringEarlyHalfband),
        EarlyHalfband(kSpringEarlyHalfband), EarlyHalfband(kSpringEarlyHalfband),
        EarlyHalfband(kSpringEarlyHalfband) };
    FinalHalfband finalDown { kSpringFinalHalfband };
    FinalHalfband finalUp { kSpringFinalHalfband };
    // Decimator inputs waiting for their pair, one per stage.
    std::array<float, kMaxResampleStages> pendingDown {};
    // The interpolator produces a whole frame of host samples per internal
    // sample; it is played out over the next frame.
    std::array<float, kMaxResampleFactor> outputFrame {};

public:
    // Host-rate samples of low-frequency group delay through the decimator,
    // the frame buffer and the interpolator (the tank itself excluded).
    static double resamplerLatencySamples(int stages) noexcept;
};

//==============================================================================
// The record preamp's 4x oversampled saturation, a sub-block at a time.
//
// Same arithmetic as running two cascaded zero-stuffing halfband upsamplers
// (hbtaps::kA 47 taps, then kB 15), the shaper at 4x and the mirror
// decimators one host sample at a time: every output is the same sum of the
// same products in the same tap order, and the phases that follow a stuffed
// zero take HalfbandFIR::outAfterStuffedZeroExact()'s shortcut. Only the
// loop order differs -- stage by stage over the sub-block, which lets each
// stage vectorise across outputs -- so the results are bit-identical
// (te2_corebench pins this against the per-sample version it replaces).
//==============================================================================
class BlockPreamp4x
{
public:
    static constexpr int kMaxBlock = 128;

    void reset() noexcept
    {
        std::fill(std::begin(x), std::end(x), 0.0f);
        std::fill(std::begin(a), std::end(a), 0.0f);
        std::fill(std::begin(c), std::end(c), 0.0f);
        std::fill(std::begin(d), std::end(d), 0.0f);
    }

    // in[0..n) -> out[0..n), n <= kMaxBlock. shape(v) is applied at 4x.
    // Any n: longer runs are processed kMaxBlock samples at a time.
    template <typename Shape>
    void process(const float* in, float* out, int n, Shape shape) noexcept
    {
        while (n > kMaxBlock)
        {
            processChunk(in, out, kMaxBlock, shape);
            in += kMaxBlock;
            out += kMaxBlock;
            n -= kMaxBlock;
        }
        if (n > 0)
            processChunk(in, out, n, shape);
    }

private:
    template <typename Shape>
    void processChunk(const float* in, float* out, int n, Shape shape) noexcept
    {
        float acc[2 * kMaxBlock]; // per-output sums of the stage being run
        // Histories: kA reaches 23 samples back at its input rate, kB 7, and
        // the decimators read their full spans (14 and 46).
        constexpr int HX = 23, HA = 7, HC = 14, HD = 46;
        for (int j = 0; j < n; ++j)
            x[HX + j] = in[j];

        // Every stage accumulates tap by tap across all of its outputs (taps
        // outer, outputs inner): each output still adds the same products in
        // the same tap order as the per-sample FIR, but the inner loop is now
        // independent across outputs and vectorises.

        // 1x -> 2x through kA: even outputs are the full sum over the real
        // samples (the centre tap reads a stuffed zero), odd outputs the
        // halved centre only.
        for (int j = 0; j < n; ++j)
            acc[j] = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            const float t = hbtaps::kA[i];
            const float* newer = x + HX - (11 - i);
            const float* older = x + HX - (12 + i);
            for (int j = 0; j < n; ++j)
                acc[j] += t * (newer[j] + older[j]);
        }
        for (int j = 0; j < n; ++j)
        {
            const float centre = 0.5f * x[HX + j - 11];
            a[HA + 2 * j] = 2.0f * acc[j];
            a[HA + 2 * j + 1] = 2.0f * (centre == 0.0f ? 0.0f : centre);
        }

        // 2x -> 4x through kB, then the shaper at 4x.
        const int n2 = 2 * n;
        for (int m = 0; m < n2; ++m)
            acc[m] = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float t = hbtaps::kB[i];
            const float* newer = a + HA - (3 - i);
            const float* older = a + HA - (4 + i);
            for (int m = 0; m < n2; ++m)
                acc[m] += t * (newer[m] + older[m]);
        }
        for (int m = 0; m < n2; ++m)
        {
            const float centre = 0.5f * a[HA + m - 3];
            c[HC + 2 * m] = 2.0f * acc[m];
            c[HC + 2 * m + 1] = 2.0f * (centre == 0.0f ? 0.0f : centre);
        }
        for (int q = 0; q < 4 * n; ++q)
            c[HC + q] = shape(c[HC + q]);

        // 4x -> 2x through kB: a full sum at every second 4x sample.
        for (int r = 0; r < n2; ++r)
            acc[r] = 0.5f * c[HC + 2 * r + 1 - 7];
        for (int i = 0; i < 4; ++i)
        {
            const int k = 2 * i + 1;
            const float t = hbtaps::kB[i];
            const float* newer = c + HC + 1 - (7 - k);
            const float* older = c + HC + 1 - (7 + k);
            for (int r = 0; r < n2; ++r)
                acc[r] += t * (newer[2 * r] + older[2 * r]);
        }
        for (int r = 0; r < n2; ++r)
            d[HD + r] = acc[r];

        // 2x -> 1x through kA.
        for (int u = 0; u < n; ++u)
            acc[u] = 0.5f * d[HD + 2 * u + 1 - 23];
        for (int i = 0; i < 12; ++i)
        {
            const int k = 2 * i + 1;
            const float t = hbtaps::kA[i];
            const float* newer = d + HD + 1 - (23 - k);
            const float* older = d + HD + 1 - (23 + k);
            for (int u = 0; u < n; ++u)
                acc[u] += t * (newer[2 * u] + older[2 * u]);
        }
        for (int u = 0; u < n; ++u)
            out[u] = acc[u];

        // Carry the histories into the next sub-block.
        std::copy(x + n, x + n + HX, x);
        std::copy(a + 2 * n, a + 2 * n + HA, a);
        std::copy(c + 4 * n, c + 4 * n + HC, c);
        std::copy(d + 2 * n, d + 2 * n + HD, d);
    }

private:
    float x[23 + kMaxBlock] {};
    float a[7 + 2 * kMaxBlock] {};
    float c[14 + 4 * kMaxBlock] {};
    float d[46 + 2 * kMaxBlock] {};

};

//==============================================================================
// TapeEchoDSP — the complete tape echo core.
//==============================================================================
class TapeEchoDSP
{
public:
    static constexpr int kNumModes    = 12;
    static constexpr int kMaxChannels = 2;

    // Hosted head-1 arrival endpoints: the motor law's values at the fast and
    // slow stops (delayMsForRepeatRate(1) and (0)). They include the
    // record/playback path delay so rendered repeats align with the reference,
    // not just the nominal motor labels.
    static constexpr float kMinDelayMs = 68.7542f;
    static constexpr float kMaxDelayMs = 176.83522f;

    // Effective head spacing and fixed playback-electronics offsets: a least
    // squares fit of the reference's head-2 and head-3 arrivals against its
    // head-1 arrival over 21 motor positions (latency-compensated, 40-impulse
    // averages). The fit leaves residuals inside the measurement noise
    // (rms 0.06 / 0.13 ms). A ratio alone left the far heads late because each
    // pickup has a small fixed phase delay.
    static constexpr float kHeadRatio[3] =
        { 1.0f, 1.905322f, 2.759947f };
    static constexpr float kHeadOffsetMs[3] =
        { 0.0f, -0.6929f, -0.9351f };

    TapeEchoDSP() = default;

    //--- lifecycle -----------------------------------------------------------
    // Allocates. Call from the main/setup thread, never the audio thread.
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    //--- processing ----------------------------------------------------------
    // inputs/outputs: arrays of channel pointers; in-place (inputs == outputs)
    // is supported. numChannels is clamped to [1, 2]. Real-time safe: no
    // allocation, no locks, no I/O.
    void processBlock(const float* const* inputs, float* const* outputs,
                      int numChannels, int numSamples) noexcept;

    //--- parameters (thread-safe: callable from any thread) -------------------
    void setMode(int mode1to12) noexcept          { pMode.store(clampInt(mode1to12, 1, 12), std::memory_order_relaxed); }
    void setRepeatRate(float v01) noexcept        { pRepeatRate.store(clamp01(v01), std::memory_order_relaxed); }  // 0 = slow motor (177 ms), 1 = fast (69 ms)
    void setIntensity(float v01) noexcept         { pIntensity.store(clamp01(v01), std::memory_order_relaxed); }   // > ~0.75 self-oscillates
    void setEchoLevel(float v01) noexcept         { pEchoLevel.store(clamp01(v01), std::memory_order_relaxed); }
    void setReverbLevel(float v01) noexcept       { pReverbLevel.store(clamp01(v01), std::memory_order_relaxed); }
    void setDryLevel(float v01) noexcept          { pDryLevel.store(clamp01(v01), std::memory_order_relaxed); }
    void setBass(float vMinus1to1) noexcept       { pBass.store(clampF(vMinus1to1, -1.0f, 1.0f), std::memory_order_relaxed); }
    void setTreble(float vMinus1to1) noexcept     { pTreble.store(clampF(vMinus1to1, -1.0f, 1.0f), std::memory_order_relaxed); }
    void setInputGain(float v01) noexcept         { pInputGain.store(clamp01(v01), std::memory_order_relaxed); }   // preamp drive
    void setWowFlutter(float v01) noexcept        { pWowFlutter.store(clamp01(v01), std::memory_order_relaxed); }
    void setBypass(bool bypassed) noexcept
    {
        const float next = bypassed ? 1.0f : 0.0f;
        const float previous =
            pBypass.exchange(next, std::memory_order_relaxed);
        if (next > 0.5f && previous <= 0.5f)
            pClearRequest.fetch_add(1u, std::memory_order_relaxed);
    }
    void setTapeAge(float v01) noexcept
    {
        // The three cartridge states use normalized 0/.5/1 host values so
        // existing sessions and decoded reference states retain their meaning.
        pTapeAge.store(teQuantizeTapeAge(v01), std::memory_order_relaxed);
    }
    void setOutputVolume(float v01) noexcept      { pOutputVolume.store(clamp01(v01), std::memory_order_relaxed); }
    void setEchoPan(float v01) noexcept           { pEchoPan.store(clamp01(v01), std::memory_order_relaxed); }
    void setReverbPan(float v01) noexcept         { pReverbPan.store(clamp01(v01), std::memory_order_relaxed); }
    void setInputSend(bool enabled) noexcept      { pInputSend.store(enabled ? 1.0f : 0.0f, std::memory_order_relaxed); }
    void setMix(float v01) noexcept               { pMix.store(clamp01(v01), std::memory_order_relaxed); }
    // Repro noise bed level above the captured reference floor: 0 leaves the
    // bed exactly as measured, 1 raises hum, bed and hiss together by
    // kNoiseRangeDb. Tape Age's 1:2:4 law still multiplies on top.
    void setNoise(float v01) noexcept             { pNoise.store(clamp01(v01), std::memory_order_relaxed); }
    // Mains hum fundamental: 60 Hz as captured, or 50 Hz for 50 Hz regions.
    void setHumFrequency50(bool fiftyHz) noexcept { pHum50.store(fiftyHz ? 1.0f : 0.0f, std::memory_order_relaxed); }
    float getTapeAge() const noexcept             { return pTapeAge.load(std::memory_order_relaxed); }

    // The motor control is intentionally nonlinear. The inverse is used by
    // tempo sync so a requested musical delay still lands at the right time.
    static float delayMsForRepeatRate(float v01) noexcept;
    static float repeatRateForDelayMs(float delayMs) noexcept;
    static float leadingHeadRatioForMode(int mode1to12) noexcept;
    static float leadingHeadOffsetMsForMode(int mode1to12) noexcept;

    //--- metering (thread-safe: read from any thread) --------------------------
    // The meter is in the record path after Input Volume. It remains live when
    // Input Send is muted and also sees feedback immediately before the tape
    // record chain. Both readings are dead while POWER is off.
    float getRecordVuLevel() const noexcept       { return recordVu.load(std::memory_order_relaxed); }
    float getRecordPeakLevel() const noexcept     { return recordPeak.load(std::memory_order_relaxed); }

private:
    friend struct TapeEchoDSPTestAccess;
    //--- helpers -------------------------------------------------------------
    static float clamp01(float v) noexcept  { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float clampF(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static int   clampInt(int v, int lo, int hi) noexcept     { return v < lo ? lo : (v > hi ? hi : v); }

    // Bounded cubic soft clip, tanh-like, branch-light. Monotonic on the
    // clamped range, |out| <= 1. Used for tape saturation and the feedback
    // limiter — this is what stabilizes self-oscillation.
    static float softClip(float x) noexcept
    {
        x = clampF(x, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // Input-stage limiting is odd-symmetric. Hosted harmonic ladders show the
    // even orders at the noise floor across the full input-control range.
    static float preampShape(float x) noexcept
    {
        x = clampF(x, -2.5f, 2.5f);
        return softClip(x);
    }

    // Measured front-panel input taper. It is shared by the direct and effect
    // paths, reaches unity at the midpoint, provides about +8.6 dB at maximum,
    // and mutes exactly at zero.
    static float inputGainFromControl(float x) noexcept
    {
        return x * (1.29212f + 1.40165f * x);
    }

    // Measured effect-volume taper. The linear term is important at low
    // settings; a pure square law undershoots the lower half substantially.
    static float echoGainFromControl(float x) noexcept
    {
        return x * (0.30355f + 0.69645f * x);
    }

    // Hard ceiling with a quadratic knee: identity below `knee`, exactly
    // `ceiling` at and above knee + 2(ceiling - knee). Monotonic, and C1 at
    // both joins (unit slope entering the knee, zero slope leaving it).
    static float hardKnee(float x, float knee, float ceiling) noexcept
    {
        const float magnitude = x < 0.0f ? -x : x;
        if (magnitude <= knee)
            return x;
        const float width = 2.0f * (ceiling - knee);
        const float sign  = x < 0.0f ? -1.0f : 1.0f;
        if (magnitude >= knee + width)
            return sign * ceiling;
        const float under = 1.0f - (magnitude - knee) / width;
        return sign * (knee + (ceiling - knee) * (1.0f - under * under));
    }

    // Record-amplifier overload, seen only by the regeneration path. The
    // intensity control feeds the record amp, and that amplifier runs out of
    // rail long before anything else in the loop does; the program arrives at
    // the same summing node having already been through its own preamp shaper,
    // so this ceiling is applied to the regenerated signal alone. That
    // placement is also what makes it safe: it is identically zero with the
    // intensity control at minimum, so every harmonic and level calibration
    // measured there is bit-identical.
    //
    // The knee is 22 dB above the loop level anywhere in the calibrated repeat
    // ladder and 15 dB above the marginal-oscillation level at intensity 0.7,
    // so nothing at or below the matched settings can reach it.
    //
    // A ceiling at the tape write instead - which is demonstrably where the
    // reference's is, since its loudest program peak (-22.15 dBFS wet) and its
    // runaway peak (-22.11) agree to 0.05 dB - was built and measured and then
    // rejected. This record chain reaches the reference's program fundamental
    // and THD with a waveform 0.76 dB peakier (its loudest program peak is
    // 0.2614 where the reference's is 0.2396), so any tape-write ceiling low
    // enough to shape a runaway also clips program material. Measured: at
    // 0.2396 it cost 0.62 dB of fundamental and 6.4% of THD at -3 dBFS and
    // broke both transfer gates (0.64 / 0.87 dB against 0.50 / 0.75 limits);
    // raised to 0.2620 and then 0.3000 to clear those, it still broke the
    // factory octave-band gate (2.81 and 2.68 dB against a 2.50 limit, from a
    // 2.13 baseline) because sustained tones in the factory presets reach that
    // flux too. The cost of this placement instead is crest: the loop clip
    // sits ahead of the playback roll-off, so a slammed loop reads back at
    // 2.3-2.6 dB crest against the reference's 0.35-0.77. Closing that means
    // reshaping the record stage's peak factor, which re-opens the
    // already-matched harmonic calibration and is its own campaign.
    static constexpr float kLoopCeilingKnee = 0.2000f;
    static constexpr float kLoopCeiling     = 0.2940f;

    // The hosted feedback pot has a pronounced dead-band immediately before
    // regeneration. Installed states at 0.640 and 0.669 match our former
    // 0.624 and 0.630 behavior, while the hosted 0.700 anchor genuinely jumps
    // to our 0.700 runaway onset. Shape only that narrow interval; <= 0.62
    // and >= 0.70 remain identity, preserving the calibrated repeat ladder
    // and runaway anchors.
    //
    // This curve now drives ONLY the read amplifier and its loop blend. The
    // loop GAIN is driven from the raw Intensity (feedbackGainFromControl):
    // measured repeat by repeat, the reference's loop gain has no dead band
    // (-2.84 / -1.30 / -0.44 / +0.09 dB per repeat at 0.60 / 0.65 / 0.68 /
    // 0.70), so the "dead band" those installed states showed was our loop
    // running 0.4-0.5 dB per repeat short, not the pot.
    static float feedbackKneeControl(float x) noexcept
    {
        if (x <= 0.62f || x >= 0.70f)
            return x;
        if (x <= 0.67f)
        {
            const float t = (x - 0.62f) / 0.05f;
            const float smooth = t * t * (3.0f - 2.0f * t);
            return 0.62f + 0.01f * smooth;
        }
        const float t = (x - 0.67f) / 0.03f;
        const float smooth = t * t * (3.0f - 2.0f * t);
        return 0.63f + 0.07f * smooth;
    }

    // Intensity also drives the playback/read amplifier. Hosted impulses prove
    // this is not regeneration alone: the FIRST repeat rises about 2.6 dB at
    // 0.7 and reaches the same fixed ceiling as the runaway tail by 0.8. A
    // feedback-only multiplier cannot reproduce that observation because no
    // returned signal exists before the first playback-head arrival.
    //
    // Keep the stage exactly transparent through 0.62 so the calibrated 0.5
    // repeat ladder and every feedback-off transfer measurement are unchanged.
    // Above it, the measured gain enters a read-amplifier rail at 0.240. Placing
    // this ceiling after tape playback also closes the reference's very low
    // runaway crest factor without clipping ordinary program material at the
    // tape-write point.
    static float feedbackReadAmplifier(float headSignal,
                                       float intensity) noexcept
    {
        if (intensity <= 0.62f)
            return headSignal;

        constexpr float kX[5] =
            { 0.62f, 0.70f, 0.80f, 0.90f, 1.00f };
        constexpr float kG[5] =
            { 1.00f, 1.35f, 3.80f, 4.00f, 4.00f };
        // At the onset point the hosted loop has a lower intermediate rail;
        // it then jumps to the full runaway rail by 0.8. The 0.8 knee is very
        // narrow (repeat peaks are fixed but the tail keeps ~2.6 dB crest),
        // while 0.9 and above are fully compressed to sub-1 dB crest.
        constexpr float kKnee[5] =
            { 0.280f, 0.094f, 0.227f, 0.200f, 0.200f };
        constexpr float kCeiling[5] =
            { 0.300f, 0.096f, 0.235f, 0.240f, 0.240f };
        int segment = 3;
        if (intensity <= kX[1])
            segment = 0;
        else if (intensity <= kX[2])
            segment = 1;
        else if (intensity <= kX[3])
            segment = 2;
        const float t = (intensity - kX[segment])
                      / (kX[segment + 1] - kX[segment]);
        const float smooth = t * t * (3.0f - 2.0f * t);
        const float gain = kG[segment]
                         + (kG[segment + 1] - kG[segment]) * smooth;
        const float knee = kKnee[segment]
                         + (kKnee[segment + 1] - kKnee[segment]) * smooth;
        const float ceiling = kCeiling[segment]
                            + (kCeiling[segment + 1]
                               - kCeiling[segment]) * smooth;
        const float limited = hardKnee(gain * headSignal, knee, ceiling);
        if (segment > 0)
            return limited;
        // Fade the limiter in across the onset window rather than switching it
        // on at full strength. The gain/knee/ceiling interpolation alone is not
        // enough: segment 0 opens at knee 0.280, which is BELOW the loudest
        // reachable head sum (tape content is bounded near kLoopCeiling and the
        // multi-head modes sum two reads), so crossing 0.62 stepped loud wet
        // peaks down by 1-3 dB in one sample -- and this stage feeds the echo
        // output directly, not just the loop. `smooth` is already the 0->1
        // smoothstep over exactly 0.62..0.70, so reuse it: the result is
        // identity at 0.62, fully limited from 0.70 up (runaway anchors
        // untouched), and bit-identical at every level at or below the knee,
        // where hardKnee is identity and the blend has nothing to move.
        return headSignal + smooth * (limited - headSignal);
    }

    // Around the self-oscillation threshold the read amplifier bootstraps the
    // record return, then falls out of that path as the main loop reaches its
    // full rail. The blend is narrow and triangular so 0.8 never gets the
    // overly dense, sub-1 dB crest of a permanently read-amplified feedback
    // bus.
    //
    // PEAK 0.10, NOT 1.0 -- this is a measured anchor, do not raise it.
    // The read amplifier applies 1.35x gain at intensity 0.70. Feeding that
    // multiplier into the loop at full strength puts the loop over unity where
    // the reference is still decaying: probe_feedback measured our 0.700 tail
    // RMS at -35.19 dBFS against the reference's -56.80, i.e. +21.61 dB, while
    // 0.000 / 0.250 / 0.500 / 0.800 / 0.900 / 1.000 all matched within 1 dB.
    // An earlier campaign had 0.7 "holding unity +/-0.3 dB", so full-strength
    // blending was a regression against an already-matched anchor, not a new
    // behaviour. Swept against a fresh render at each step:
    //     peak 0.00 -> -8.07 dB    0.05 -> -3.50    0.10 -> +0.93
    //          0.15 -> +5.42       0.25 -> +13.06   1.00 -> +21.61
    // 0.10 puts 0.700 on the same small positive bias the whole rest of the
    // ladder carries (+0.92 at 0.800, +0.93 at 0.000/0.250).
    //
    // Note this scales ONLY the loop contribution. feedbackReadAmplifier still
    // reaches the echo OUTPUT at full strength, so the measured ~2.6 dB rise of
    // the first repeat at 0.7 -- the observation the stage exists for, and one
    // no feedback-only multiplier can produce -- is preserved intact.
    static float feedbackReadLoopBlend(float intensity) noexcept
    {
        if (intensity <= 0.62f || intensity >= 0.80f)
            return 0.0f;
        const float t = intensity <= 0.70f
            ? (intensity - 0.62f) / 0.08f
            : (0.80f - intensity) / 0.10f;
        const float clamped = clamp01(t);
        return 0.10f * clamped * clamped * (3.0f - 2.0f * clamped);
    }

    // Runaway shaping above the onset knee. The reference crosses unity loop
    // gain near 0.7 and is fully slammed into its tape ceiling by 0.8, holding
    // that ceiling to maximum. The measured taper below only grazes unity, so
    // the loop equilibrated shallow and clean instead of dense and saturated.
    // This multiplier is exactly 1.0 with zero slope at and below 0.62, so
    // every calibration measured at or below feedback 0.5 - the whole repeat
    // ladder and all harmonic gates - is bit-identical. Piecewise smoothstep
    // between anchors, hence C1 continuous with no zipper for the 30 ms
    // intensity smoother to chase.
    static float feedbackRunawayLift(float x) noexcept
    {
        constexpr float kX[5] = { 0.62f, 0.70f, 0.80f, 0.90f, 1.00f };
        // Regeneration has its own measured law. The threshold bootstrap above
        // supplies the missing 0.7 return; 0.8 keeps the softer main loop that
        // preserves the reference's 2.6 dB crest, then the rail grows denser.
        constexpr float kL[5] = { 1.0f, 1.0225f, 1.4500f, 2.0500f, 2.7500f };
        if (x <= kX[0])
            return kL[0];
        int i = 3;
        if (x <= kX[1])      i = 0;
        else if (x <= kX[2]) i = 1;
        else if (x <= kX[3]) i = 2;
        const float t = (x - kX[i]) / (kX[i + 1] - kX[i]);
        const float smooth = t * t * (3.0f - 2.0f * t);
        return kL[i] + (kL[i + 1] - kL[i]) * smooth;
    }

    static float feedbackGainFromControl(float x) noexcept
    {
        x = clamp01(x);
        float normalized;
        if (x <= 0.7f)
        {
            // Measured loop gain per repeat (head 1, Repeat Rate 0.5, new
            // tape, impulse, median of repeats 2-6) against the reference at
            // Intensity 0.40-0.68: the former cubic ran 0.36-0.57 dB short
            // per repeat below the knee and 1.1-1.2 dB short at 0.65-0.68,
            // where the knee curve held it flat. Short per repeat compounds:
            // ten repeats at 0.5 ended 5 dB quiet, and the multi-head Full Wash
            // state fell below unity loop gain where the reference runs away.
            // The cubic is kept and corrected in dB by a smooth curve fitted
            // to those readings (within 0.07 dB of each), held at its 0.40
            // value below (0.30 and under are floor-limited) and pinned at 0.70
            // to the start of the branch below, so the law stays continuous.
            // Driven from the raw Intensity: the reference's gain rises
            // smoothly through 0.62-0.70, with no dead band.
            const float cubic = 0.00063492f * x
                              + 0.95619048f * x * x
                              + 0.96507937f * x * x * x;
            const float u = std::max(x - 0.40f, 0.0f);
            const float correctionDb = 0.589331f
                + u * u * (-23.489727f + u * (201.436561f - 520.370029f * u));
            normalized = cubic * std::pow(10.0f, correctionDb * (1.0f / 20.0f));
        }
        else
        {
            float loX, hiX, loGain, hiGain;
            if (x <= 0.8f)
            {
                loX = 0.7f; hiX = 0.8f;
                loGain = 0.77271764f; hiGain = 0.82074074f;
            }
            else if (x <= 0.9f)
            {
                loX = 0.8f; hiX = 0.9f;
                loGain = 0.82074074f; hiGain = 0.85925926f;
            }
            else
            {
                loX = 0.9f; hiX = 1.0f;
                loGain = 0.85925926f; hiGain = 0.88f;
            }
            const float t = (x - loX) / (hiX - loX);
            const float smooth = t * t * (3.0f - 2.0f * t);
            normalized = loGain + (hiGain - loGain) * smooth;
        }
        return 1.30f * normalized * feedbackRunawayLift(x);
    }

    // Group delay of the 4x oversampling chain in base-rate samples
    // (2x stage: 46 samples at 2x; 4x stage: 14 at 4x). Compensated in the
    // tape delay so head timing stays exact.
    static constexpr float kPreampLatencySamples = 23.0f + 3.5f;

    float readTape(const std::vector<float>& buf, float delaySamples) const noexcept;

    // Last input and result of per-sample laws on smoothed controls, seeded
    // in prepare() from the snapped controls.
    float memoAge = 0.0f;
    float memoAgeNoiseScale = 0.0f;
    float memoOutputVolume = 0.0f;
    float memoOutputGain = 1.0f;

    // Per-sample values carried between the passes of one sub-block. Kept on
    // the stack in processBlock (about 19 KB): as a member it measured 3-5 %
    // slower (48 kHz core, default state), and 19 KB is far inside the audio
    // thread stacks hosts give plug-ins (512 KB and up on macOS, 1 MB on
    // Windows, 8 MB on Linux).
    struct SubBlockScratch
    {
        static constexpr int kSize = 128;
        float d1[kSize];
        float d2[kSize];
        float d3[kSize];
        float drivenInput[kSize];
        float dryLvl[kSize];
        float dryMixGain[kSize];
        float echoLvl[kSize];
        float fbGain[kSize];
        float feedbackCurve[kSize];
        float g1[kSize];
        float g2[kSize];
        float g3[kSize];
        float inL[kSize];
        float inR[kSize];
        float inputGain[kSize];
        float outputGain[kSize];
        float power[kSize];
        float pre[kSize];
        float reproAgeNoise[kSize];
        float reproNoiseGate[kSize];
        float revLvl[kSize];
        float revSend[kSize];
        float springPre[kSize];
        float wetMixGain[kSize];
        float wobble[kSize];
        float echoWet[kSize];
        float rev[kSize];
        float recordEnvelopeNow[kSize];
        float loopReadBlend[kSize];
        float preIn[kSize];
        float preOut[kSize];
        float postDriveGain[kSize];
        float splice1[kSize];
        float splice2[kSize];
        float splice3[kSize];
    };
    float memoIntensity = 0.0f;
    float memoFeedbackCurve = 0.0f;
    float memoFbGain = 0.0f;
    float memoLoopReadBlend = 0.0f;

    // The spring is skipped while its send and every piece of its state have
    // decayed below kSpringIdleFloor (see processBlock).
    bool springIdle = false;
    void  refreshBlockRateControls();

    //--- per-channel state ----------------------------------------------------
    struct Channel
    {
        std::vector<float> tape;
        DCBlocker          preampDC;
        // base <-> 2x kA (tight, -67 dB), 2x <-> 4x kB (loose, -75 dB)
        BlockPreamp4x preamp;
        // Low end of the tape loop, split where the reference splits it (see
        // kRecordLowShape in the .cpp): the record-side shape on the program
        // feed AHEAD of the preamp, an input-only shelf ahead of that, and the
        // same shape on the regeneration feed. Nothing low-cut sits after a
        // saturating stage.
        Biquad             inputLowShelf;   // program feed only
        Biquad             inputLowShape;   // program feed, ahead of the preamp
        Biquad             loopLowShape;    // regeneration feed
        Biquad             speedLP;    // tape-speed-dependent playback bandwidth
        Biquad             ageContour; // age-only HF shoulder, inside the loop
        std::array<Biquad, 3> antiAliasLP; // sixth-order fixed record/repro ceiling
        Biquad             bassShelf;  // echo output path only
        Biquad             trebleShelf;
        Biquad             dryLowShelf;  // direct-path coupling/amp bandwidth
        Biquad             dryHighShelf;
        std::array<float, 32> springCleanDelay {};
        int                springCleanDelayWriteIdx = 0;
        float              recordEnvelope = 0.0f;   // program drive only
        float              loopEnvelope = 0.0f;     // regeneration drive only
        float              magneticEnvelope = 0.0f;
    };

    std::array<Channel, kMaxChannels> channels;
    SpringReverb spring; // shared mono wet path

    //--- tape transport --------------------------------------------------------
    int    writeIdx   = 0;
    int    mask       = 0;      // tape length is a power of two
    float  maxDelaySamples = 0.0f;
    double fs         = 44100.0;

    //--- metering ---------------------------------------------------------------
    std::atomic<float> recordVu { 0.0f };
    std::atomic<float> recordPeak { 0.0f };
    float meterVu = 0.0f;
    float meterVuStage1 = 0.0f;   // first pole of the critically damped movement
    float meterPeak = 0.0f;
    float meterVuCoeff = 1.0f;
    float meterPeakDecayPerSample = 1.0f;
    float recordEnvelopeAttack = 1.0f;
    float recordEnvelopeRelease = 1.0f;

    //--- modulation (shared across channels: one motor, one tape) --------------
    // Two-pole state-variable band-pass, TPT (Zavalishin) topology. A direct-form
    // biquad is unusable here: at a 6 Hz corner and 48 kHz the pole pair sits
    // 7.9e-4 rad off the real axis, so a1 ~ -1.9996 and a2 ~ 0.9996 lose the
    // centre frequency to float cancellation (~10% drift). The TPT form stores
    // g = tan(pi*fc/fs) directly and stays well conditioned at any fc/fs.
    struct BandPassSVF
    {
        void set(float hz, float Q, double sampleRate) noexcept
        {
            const float nyquistGuard = 0.2f * (float)sampleRate;
            const float f = hz < nyquistGuard ? hz : nyquistGuard;
            const float g = std::tan(kDuskPi * f / (float)sampleRate);
            const float k = 1.0f / Q;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() noexcept { s1 = s2 = 0.0f; }
        // Band-pass output; peak gain is Q at fc, -> 0 at DC and Nyquist.
        float process(float x) noexcept
        {
            const float v3 = x - s2;
            const float v1 = a1 * s1 + a2 * v3;
            const float v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            return v1;
        }
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, s1 = 0.0f, s2 = 0.0f;
    };

    float     wowPhase = 0.0f, wowInc = 0.0f;
    float     flutterPhase = 0.0f, flutterInc = 0.0f;
    OnePoleLP noiseLP;
    uint32_t  rngState = 0x9E3779B9u;

    // Stochastic scrape flutter: band noise around 6 Hz. Its own RNG stream so
    // the wow noise realization (and therefore the measured wow depth) is
    // untouched by this addition.
    BandPassSVF flutterBand;
    OnePoleLP   flutterBandLP;                    // steepens the upper skirt
    uint32_t    flutterRngState = 0x2545F491u;

    // Every modulator here is white noise through a fixed-Hz filter, whose
    // output RMS scales as sqrt(fc / fs) -- i.e. the SAME filter fed the SAME
    // noise gets quieter as the sample rate rises (measured: 0.707x at 96 kHz,
    // 0.500x at 192 kHz). All four depths were voiced against the reference at
    // 48 kHz, so without this the wow/flutter/hiss specs would only hold there.
    // Pre-scaling the noise by sqrt(fs / 48k) restores the calibrated RMS at
    // any rate. Exactly 1.0f at 48 kHz, so the calibrated path is bit-identical.
    static constexpr double kNoiseCalibrationFs = 48000.0;
    float noiseRateComp = 1.0f;

    float frand() noexcept  // uniform [-1, 1)
    {
        rngState = rngState * 1664525u + 1013904223u;
        return (float)(int32_t)rngState * (1.0f / 2147483648.0f);
    }

    float flutterRand() noexcept  // uniform [-1, 1)
    {
        flutterRngState = flutterRngState * 1664525u + 1013904223u;
        return (float)(int32_t)flutterRngState * (1.0f / 2147483648.0f);
    }

    //--- atomic parameter inputs -----------------------------------------------
    std::atomic<int>   pMode        { 1 };
    std::atomic<float> pRepeatRate  { 0.0f };
    std::atomic<float> pIntensity   { 0.0f };
    std::atomic<float> pEchoLevel   { 0.5f };
    std::atomic<float> pReverbLevel { 0.0f };
    std::atomic<float> pDryLevel    { 1.0f };
    std::atomic<float> pBass        { 0.0f };
    std::atomic<float> pTreble      { 0.0f };
    std::atomic<float> pInputGain   { 0.5f };
    std::atomic<float> pWowFlutter  { 0.0f };
    std::atomic<float> pBypass      { 0.0f };
    std::atomic<float> pTapeAge     { 0.5f };
    std::atomic<float> pOutputVolume{ 0.5f };
    std::atomic<float> pEchoPan     { 0.5f };
    std::atomic<float> pReverbPan   { 0.5f };
    std::atomic<float> pInputSend   { 1.0f };
    std::atomic<float> pMix         { 0.5f };
    std::atomic<float> pNoise       { 0.0f };
    std::atomic<float> pHum50       { 0.0f };
    std::atomic<uint32_t> pClearRequest { 0u };

    //--- smoothed control signals ----------------------------------------------
    SmoothedValue delaySmoother;                 // motor inertia -> pitch glide
    SmoothedValue intensitySmoother;
    SmoothedValue headGain[3];                   // mode switching, click-free
    SmoothedValue loopHeadNormSmoother;          // multi-head loop-gain scale
    SmoothedValue reverbSendSmoother;
    SmoothedValue echoLevelSmoother, reverbLevelSmoother, dryLevelSmoother;
    SmoothedValue outputVolumeSmoother;
    SmoothedValue echoPanSmoother, reverbPanSmoother;
    SmoothedValue inputSendSmoother;
    SmoothedValue mixSmoother;
    SmoothedValue driveSmoother, wowFlutterSmoother;
    SmoothedValue powerSmoother;                 // bypass crossfade, click-free
    SmoothedValue ageSmoother;                   // tape age morph
    SmoothedValue noiseGainSmoother;             // linear gain on the repro noise bed

    // Tape-age noise has its own RNG and oscillator state so it never changes
    // the separately calibrated transport-noise realization.
    uint32_t  ageRngState = 0x1F123BB5u;
    Biquad    hissVoice;                           // dark broadband repro bed
    Biquad    hissCeiling;                         // near-Nyquist noise rolloff
    Biquad    ageBedHighPass, ageBedLowPass;       // low-mid machine bed
    OnePoleLP wobbleLP;                           // slow playback-level wobble
    uint32_t  ageBedRngState = 0xB5297A4Du;
    float     ageHumSin = 0.0f, ageHumCos = 1.0f; // mains fundamental phasor
    float     ageHumRotSin = 0.0f, ageHumRotCos = 1.0f;
    // Per-sample rotations for 60 Hz [0] and 50 Hz [1]. Switching only swaps
    // the rotation, so the phasor stays continuous and the change is click-free.
    float     ageHumRateSin[2] = { 0.0f, 0.0f };
    float     ageHumRateCos[2] = { 1.0f, 1.0f };
    uint32_t  ageHumRenormalize = 0u;
    float     lastPlaybackCutoff = -1.0f;         // block-rate speed/age guard
    float     lastAntiAliasCutoff = -1.0f;
    float     lastAgeContourDb = 999.0f;
    bool      ageContourActive = false;
    uint32_t  lastClearRequest = 0u;
    float     spliceSamplesToHead1 = 0.0f;
    bool      spliceClockStarted = false;

    float ageRand() noexcept
    {
        ageRngState = ageRngState * 1664525u + 1013904223u;
        return (float)(int32_t)ageRngState * (1.0f / 2147483648.0f);
    }

    float ageBedRand() noexcept
    {
        ageBedRngState = ageBedRngState * 1664525u + 1013904223u;
        return (float)(int32_t)ageBedRngState * (1.0f / 2147483648.0f);
    }

    // cached to detect shelf-coefficient changes at block rate
    float lastBass = -999.0f, lastTreble = -999.0f;
    bool bassShelfActive = false, trebleShelfActive = false;
};

} // namespace duskaudio

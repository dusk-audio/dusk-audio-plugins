// TapeEchoDSP.hpp
// Component-modeled vintage three-head tape echo with spring reverb.
//
// Framework-free: standard C++17 only. No JUCE, no host dependencies.
// Designed to be wrapped by DAF, raw CLAP, or any other plugin shell.
//
// Signal flow (stereo input, mono wet paths, stereo dry path):
//
//   L+R ─► input shelf + record low shape ─► DC block ─────────┬──► reverb send
//                                                             │
//   program: band-limit (outer LP, speed LP, ceiling) ─► preamp curve ─────────┐
//                                                                             [+]─► magnetic stage ─► TAPE
//   regen:   band-limit (low shape, speed LP, ceiling) ─► loopLimit(softClip) ─┘   (x4, from the  │
//    ▲                                                                             preamp curve on)
//    │                                                                        3 read heads (Hermite)
//    │                                                  T · {1, 1.905322, 2.759947} + offsets
//    └── intensity · (head sum + noise bed)  ◄────────────────────────────────────────┴──┐
//                                                                            ▼
//                                               bass/treble shelves (echo path only)
//                                                                            ▼
//   out L/R = dry·input L/R + pan(echoLevel·echoEQ)
//                           + pan(reverbLevel·spring(reverb send))
//
// The band-limiting and tape saturation of the regeneration feed live INSIDE
// the feedback loop, so each repeat progressively darkens and compresses --
// this is what lets high Intensity settings bloom into stable, warm
// self-oscillation instead of exploding numerically.

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
#include "DuskHalfbandIIR.hpp" // record saturation x4 stages
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
//   (new, not lifted) polyphase IIR halfband x2 stages -> DuskHalfbandIIR.hpp
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

    // Every spring's two dispersion chains (returning wave in lane 2k,
    // outgoing in 2k + 1; lanes 6 and 7 idle) run as lanes of one bank.
    static constexpr int kDispersionLanes = 8;
    using DispersionBank = BiquadLanes<kNumDispersionSections, kDispersionLanes>;

    struct Spring
    {
        std::vector<float> outgoingDelay;
        std::vector<float> returningDelay;
        int outgoingWriteIdx = 0;
        int returningWriteIdx = 0;
        float driverReflection = -0.90f;
        float pickupReflection = -0.90f;
        float outputGain = 0.25f;
        Biquad reflectionHighPass;
        Biquad reflectionLowLoss;
        Biquad reflectionLowPass;

        // advanceSeconds moves that much transit time from the outgoing
        // to the returning path (round trip unchanged) so the resampler's
        // latency does not delay the arrivals.
        // Sets this spring's two chains (lanes 2 index and 2 index + 1)
        // in the dispersion bank.
        void  prepare(double fs, float oneWaySeconds,
                      float dispersionScale, float reflectionAmount,
                      float gain, double advanceSeconds,
                      const BiquadCoeffs& reflectionLoss,
                      DispersionBank& dispersion, int index);
        void  reset();
        // The waves entering the dispersion chains: arriving back at the
        // driver, and arriving at the pickup end.
        float returningIn() const noexcept { return returningDelay[(size_t)returningWriteIdx]; }
        float outgoingIn() const noexcept  { return outgoingDelay[(size_t)outgoingWriteIdx]; }
        // The rest of one sample, from both chains' outputs: the pickup's
        // output.
        float finish(float x, float returned, float farEnd) noexcept;
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
    DispersionBank dispersion;
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
    void setIntensity(float v01) noexcept         { pIntensity.store(clamp01(v01), std::memory_order_relaxed); }   // > ~0.69 self-oscillates
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

    void updateTapeBandwidth(float age01, float delayMs) noexcept;

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

    // Loop limiter: identity below kLoopLimitKnee, above it a soft approach to
    // kLoopLimitCeiling shaped by softClip (unit slope at the knee, so C1 there;
    // softClip reaches its bound at 3, so the curve meets the ceiling flat at
    // knee + 3 (ceiling - knee)). Odd-symmetric and monotonic. Both sides of
    // the knee are computed and one is selected, so a pass of it vectorizes
    // (it runs at x4 in the record stage).
    static float loopLimit(float x) noexcept
    {
        const float magnitude = std::abs(x);
        constexpr float span = kLoopLimitCeiling - kLoopLimitKnee;
        const float limited = kLoopLimitKnee
            + span * softClip((magnitude - kLoopLimitKnee) * (1.0f / span));
        return magnitude <= kLoopLimitKnee ? x : std::copysign(limited, x);
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
    // The knee is 16 dB above the loop level anywhere in the calibrated repeat
    // ladder, and the curve stays within 0.1 dB of identity well past it.
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
    // flux too.
    //
    // This is the runaway's rail (the read amplifier that used to sit on the
    // echo output is gone, see feedbackGainFromControl), and its SHAPE sets how
    // the rail moves with Intensity: a loop settles where the limiter's
    // compression cancels the excess loop gain, so a hard ceiling holds the
    // rail flat whatever the gain, and a soft approach lets it climb. The
    // reference's runaway tail (probe_feedback, RMS from 3 s) climbs 3.3 dB
    // from 0.8 to 1.0: -25.9 / -23.2 / -22.6 dBFS at 0.8 / 0.9 / 1.0. The
    // former hard knee (0.200 into 0.294) gave -25.5 / -24.0 / -23.8, flat
    // within 1.7 dB. Knee and ceiling swept on a lab copy of the DSP that
    // reproduced the plug-in's tails to 0.1 dB:
    //     knee 0.20, ceiling 0.40  ->  -25.1 / -23.1 / -22.7
    //     knee 0.15, ceiling 0.40  ->  -25.4 / -23.3 / -22.9
    //     knee 0.10, ceiling 0.45  ->  -25.5 / -23.3 / -22.8   <- chosen
    //     knee 0.08, ceiling 0.42  ->  -25.8 / -23.6 / -23.1
    // (worst error 0.4 dB, was 1.2).
    static constexpr float kLoopLimitKnee    = 0.10f;
    static constexpr float kLoopLimitCeiling = 0.45f;

    // Loop gain per repeat against Intensity, MEASURED against the reference:
    // head 1, Repeat Rate 0.5, new tape, energy in the 1 kHz third octave.
    //   0.40-0.72  impulse, median of the packet-to-packet steps 2-6
    //   0.75-0.95  growth of the loop's own noise from silence (the
    //              reference self-starts there, so an impulse lands in an
    //              oscillation that is already running)
    // The reference, dB per repeat:
    //   0.40 -10.50  0.50 -6.05  0.60 -2.45  0.65 -0.90  0.68 -0.04
    //   0.70  +0.49  0.72 +0.98  0.75 +1.94  0.80 +3.06  0.85 +3.99
    //   0.90  +5.28  0.95 +5.84
    // Read at 1 kHz on purpose: the tape bandwidth law (kTapeBandwidth in the
    // .cpp) owns the loop's shape re 1 kHz, so the level is fitted where that
    // shape is pinned. A wider band (500 Hz-4 kHz) is dominated by the loop's
    // strongest region and folds the bandwidth law's 2-4 kHz residual into
    // the level, which ran the low and mid repeats hot.
    // The law is the cubic below corrected in dB by kFeedbackLawDb, iterated
    // through the full DSP until every knot read within 0.03 dB of the
    // reference. Below 0.40 the reference's repeats reach the render floor in
    // two or three steps; the one clean step at 0.30 sits on the same offset,
    // so the correction is held at its 0.40 value. At 1.00 the reference is
    // already oscillating when the render starts and no growth is readable;
    // that knot continues the 0.90-0.95 slope.
    //
    // This replaces a knee curve, a read amplifier and a runaway lift. Fresh
    // renders falsified the read amplifier's premise ("the FIRST repeat rises
    // about 2.6 dB at 0.7"): the reference's first echo is the same from 0.60
    // to 0.85 (-33.1 dBFS peak, -68.0 dB band energy) while the amplifier lifted
    // ours by up to 10.7 dB and sat it on its own rail from the first repeat,
    // so our runaway started where the reference's ends. Its loop blend was
    // also 0.6 dB per repeat hot at 0.75. The runaway's level and density come
    // from the loop instead: loopLimit and the noise bed circulating with
    // the signal (see the loop tap in processBlock).
    static constexpr int kFeedbackLawKnots = 15;
    static constexpr float kFeedbackLawX[kFeedbackLawKnots] =
        { 0.40f, 0.45f, 0.50f, 0.55f, 0.60f, 0.65f, 0.68f, 0.70f,
          0.72f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f };
    static constexpr float kFeedbackLawDb[kFeedbackLawKnots] =
        { 0.7755f, 0.7663f, 0.7390f, 0.6897f, 0.6201f, 0.5487f, 0.4858f,
          0.4531f, 0.3977f, 0.3464f, 0.1117f, -0.2505f, -0.2176f, -0.8297f,
          -1.3880f };
    // Fritsch-Carlson slopes (dB per unit Intensity): shape-preserving, so
    // the correction never overshoots between knots. Zero at 0.40 to join
    // the constant below C1.
    static constexpr float kFeedbackLawSlope[kFeedbackLawKnots] =
        { 0.0000f, -0.2758f, -0.7030f, -1.1543f, -1.4096f, -1.7266f,
          -1.8240f, -2.0576f, -2.1491f, -2.4155f, -5.6960f, 0.0000f, 0.0000f,
          -11.6791f, -10.6254f };

    static float feedbackLawCorrectionDb(float x) noexcept
    {
        if (x <= kFeedbackLawX[0])
            return kFeedbackLawDb[0];
        int i = 0;
        while (i < kFeedbackLawKnots - 2 && x > kFeedbackLawX[i + 1])
            ++i;
        const float h = kFeedbackLawX[i + 1] - kFeedbackLawX[i];
        const float t = (x - kFeedbackLawX[i]) / h;
        const float t2 = t * t, t3 = t2 * t;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * kFeedbackLawDb[i]
             + (t3 - 2.0f * t2 + t) * h * kFeedbackLawSlope[i]
             + (-2.0f * t3 + 3.0f * t2) * kFeedbackLawDb[i + 1]
             + (t3 - t2) * h * kFeedbackLawSlope[i + 1];
    }

    static float feedbackGainFromControl(float x) noexcept
    {
        x = clamp01(x);
        const float cubic = 0.00063492f * x
                          + 0.95619048f * x * x
                          + 0.96507937f * x * x * x;
        return 1.30f * cubic
             * std::pow(10.0f, feedbackLawCorrectionDb(x) * (1.0f / 20.0f));
    }

    // The clean spring feed's delay: 26.5 samples at 48 kHz, held as a time
    // at every rate (springFeedDelaySamples, set in prepare()). It once
    // matched a record-path oversampler's group delay; the spring's measured
    // first arrival includes it, so it stays.
    static constexpr double kSpringFeedDelaySamplesAt48k = 26.5;
    float springFeedDelaySamples = 26.5f;

    float readTape(const std::vector<float>& buf, double delaySamples) const noexcept;

    // Last input and result of per-sample laws on smoothed controls, seeded
    // in prepare() from the snapped controls.
    float memoAge = 0.0f;
    float memoAgeNoiseScale = 0.0f;
    float memoDriftDepth = 0.0f;
    float memoFlutterH2 = 0.0f;
    float memoFlutterH3 = 0.0f;
    float memoSpliceDepthDb = 0.0f;
    float memoSpliceNarrowWeight = 0.0f;
    float memoSpliceNarrowSeconds = 0.0f; // at the fastest motor; scaled per sample
    void  updateAgeMemos(float age) noexcept;
    float memoOutputVolume = 0.0f;
    float memoOutputGain = 1.0f;

    // Per-sample values carried between the passes of one sub-block. Kept on
    // the stack in processBlock (about 27 KB): as a member it measured 3-5 %
    // slower (48 kHz core, default state), and 27 KB is far inside the audio
    // thread stacks hosts give plug-ins (512 KB and up on macOS, 1 MB on
    // Windows, 8 MB on Linux).
    struct SubBlockScratch
    {
        static constexpr int kSize = 128;
        double d1[kSize];
        double d2[kSize];
        double d3[kSize];
        float drivenInput[kSize];
        float dryLvl[kSize];
        float dryMixGain[kSize];
        float echoLvl[kSize];
        float fbGain[kSize];
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
        float tapeInput[kSize];
        float preIn[kSize];
        float postDriveGain[kSize];
        float splice1[kSize];
        float splice2[kSize];
        float splice3[kSize];
        // Record saturation, run in its own passes after the head reads.
        float programFeed[kSize];
        float loopFeed[kSize];
        float loopDrive[kSize];
        float loopLinear[kSize];
        float loopGain[kSize];
        float loopHeadSum[kSize];
        float lowFloorFade[kSize];
        float programGain[kSize];
        float c3[kSize];
        float c5[kSize];
        float makeupGain[kSize];
        float programOs[kSize][4];
        float loopOs[kSize][4];
        float tapeOs[kSize][4];
        float magneticAmplitude[kSize][4];
    };
    float memoIntensity = 0.0f;
    float memoFbGain = 0.0f;

    // The spring is skipped while its send and every piece of its state have
    // decayed below kSpringIdleFloor (see processBlock).
    bool springIdle = false;
    void  refreshBlockRateControls();

    // Two chains of four second-order allpass sections (in z^-2) whose outputs
    // stay 90 degrees apart, within about 0.35 degrees from 20 Hz to 22 kHz at
    // 44.1 / 48 kHz, so sqrt(i^2 + q^2) is a sinusoid's amplitude at any phase
    // (ripple +-0.6 % at twice its frequency). Chain A runs one sample late.
    // Coefficients: the widely published 4 + 4 section design (Niemitalo).
    struct HilbertPair
    {
        float x1[4][2] {}, x2[4][2] {}, y1[4][2] {}, y2[4][2] {};  // per section, per chain
        float aLate = 0.0f;
        void reset() noexcept { *this = HilbertPair{}; }
        void process(float x, float& i, float& q) noexcept
        {
            // The two chains run as lanes of one loop (lane 0: A, lane 1: B).
            static constexpr float kCoef[4][2] = {
                { 0.6923878f * 0.6923878f, 0.4021921162426f * 0.4021921162426f },
                { 0.9360654322959f * 0.9360654322959f, 0.8561710882420f * 0.8561710882420f },
                { 0.9882295226860f * 0.9882295226860f, 0.9722909545651f * 0.9722909545651f },
                { 0.9987488452737f * 0.9987488452737f, 0.9952884791278f * 0.9952884791278f } };
            float v[2] = { x, x };
            for (int k = 0; k < 4; ++k)
                for (int l = 0; l < 2; ++l)
                {
                    const float y = kCoef[k][l] * (v[l] + y2[k][l]) - x2[k][l];
                    x2[k][l] = x1[k][l]; x1[k][l] = v[l];
                    y2[k][l] = y1[k][l]; y1[k][l] = y;
                    v[l] = y;
                }
            i = aLate; aLate = v[0];
            q = v[1];
        }
    };

    //--- per-channel state ----------------------------------------------------
    struct Channel
    {
        std::vector<float> tape;
        DCBlocker          preampDC;
        // Low end of the tape loop, split where the reference splits it (see
        // kRecordLowShape in the .cpp): the record-side shape on the program
        // feed AHEAD of the preamp, an input-only shelf ahead of that, and the
        // same shape on the regeneration feed. Nothing low-cut sits after a
        // saturating stage.
        Biquad             inputLowShelf;   // program feed only
        Biquad             inputLowShape;   // program feed, ahead of the preamp
        Biquad             loopLowShape;    // regeneration feed
        // High end, split where the reference splits it, and ahead of every
        // saturating stage (see kTapeBandwidth in the .cpp). The program feed
        // and the regeneration feed are band-limited separately so the preamp
        // curve can act on the program alone: the same sections, the same
        // coefficients, so the linear response is that of one shared chain.
        Biquad             speedLP;    // playback bandwidth, regeneration feed
        std::array<Biquad, 3> loopCeilingLP; // sixth-order ceiling, regeneration feed
        Biquad             programSpeedLP;   // playback bandwidth, program feed
        std::array<Biquad, 3> programCeilingLP; // sixth-order ceiling, program feed
        Biquad             programOuterLP;   // program feed only: outside the loop
        Biquad             bassShelf;  // echo output path only
        Biquad             trebleShelf;
        Biquad             dryLowShelf;  // direct-path coupling/amp bandwidth
        Biquad             dryHighShelf;
        std::vector<float> springCleanDelay;   // sized in prepare()
        int                springCleanDelayWriteIdx = 0;
        float              recordEnvelope = 0.0f;   // program drive only
        // Record-compressor detector: phase-independent amplitude of the band
        // above kDetectorBandHz (see processBlock).
        Biquad             detectorHighPass;
        HilbertPair        detectorHilbert;
        float              detectorSmooth1 = 0.0f;
        float              detectorSmooth2 = 0.0f;
        float              loopEnvelope = 0.0f;     // regeneration drive only
        float              magneticEnvelope = 0.0f;
        int                magneticHold = 0;        // samples at 4x
        // Record saturation runs at 4x (see processBlock): both feeds are
        // interpolated twice, the tape signal decimated twice.
        HalfbandUpsampler2xPair<5> feedUp1;   // program and loop feeds, 1x -> 2x
        HalfbandUpsampler2xPair<3> feedUp2;   // 2x -> 4x
        HalfbandDownsampler2x<3>   tapeDown2; // 4x -> 2x
        HalfbandDownsampler2x<5>   tapeDown1; // 2x -> 1x
    };
    // True while every regeneration-feed section's state is exactly zero.
    static bool loopFeedSectionsAtRest(const Channel& ch) noexcept;

    std::array<Channel, kMaxChannels> channels;
    SpringReverb spring; // shared mono wet path

    //--- tape transport --------------------------------------------------------
    int    writeIdx   = 0;
    int    mask       = 0;      // tape length is a power of two
    float  maxDelaySamples = 0.0f;
    // Running sum of the transport's speed deviation, one entry per sample:
    // a head's delay is its path plus this sum over a window as long as the
    // path (see the head delays). Its own power-of-two length, sized in
    // prepare() for head 3's window at the slowest motor speed.
    std::vector<double> transportSum;
    double transportSumNow = 0.0;
    int    transportIdx    = 0;
    int    transportMask   = 0;
    int    transportWritten = 0;   // entries written since reset(), saturating
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
    float detectorSmoothCoeff = 1.0f;
    float magneticReleaseOs = 1.0f;   // the record release per 4x sample
    int   magneticHoldSamplesOs = 0;
    // Longest sub-block whose tape writes can wait for its head reads: half
    // the shortest head delay (see processBlock), at most the scratch size.
    int   subBlockLimit = 1;

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

    // Flutter oscillator as a unit vector rotated once per sample (no
    // per-sample sinf). Its step follows the motor, so it turns by a
    // small-angle polynomial of flutterInc.
    float     flutterSin = 0.0f, flutterCos = 1.0f, flutterInc = 0.0f;
    // Slow transport drift: white noise through one pole, scaled to unit rms
    // by driftNorm (set in prepare()).
    OnePoleLP driftLP;
    float     driftNorm = 1.0f;
    uint32_t  rngState = 0x9E3779B9u;

    // Stochastic scrape flutter: band noise around 6 Hz. Its own RNG stream so
    // the drift noise realization (and therefore the measured drift depth) is
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
    float     lastBandwidthAge = -1.0f;      // updateTapeBandwidth guards
    float     lastBandwidthDelayMs = -1.0f;
    uint32_t  lastClearRequest = 0u;
    double    spliceLoopPosition = 0.0; // tape travel since the last head-1 splice, in loop lengths

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

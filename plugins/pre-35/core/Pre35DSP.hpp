// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Pre35DSP.hpp — one PRE-35 channel, end to end. Framework-free C++17: zero
// JUCE, zero DPF, zero external dependencies beyond the standard library.
//
// PRE-35 is Dusk Audio's model of a Tascam M-35 mixer channel's mic preamp,
// measured rather than guessed: every number it runs on came off the bench and
// travels here through Pre35Coefficients.hpp, which the emitter in the private
// tools repo generates from the fitted model.
//
// ============================== SIGNAL CHAIN ================================
// The order is load-bearing and is the reference renderer's, not a convenient
// rearrangement of it (fit/render_ref.py):
//
//     pad  ->  iron  ->  input-referred noise  ->  amp gain  ->  response
//
//   * The PAD sits ahead of the input transformer, so it sets the flux and
//     therefore the iron drive. Engaging 20 dB of pad really does buy 20 dB less
//     transformer distortion — that is a modelled behaviour, not a side effect.
//   * The NOISE is referred to the amplifier input, which is why it does NOT
//     fall when the pad is engaged and DOES rise with trim.
//   * The RESPONSE is unity at midband by construction, so the amp gain stage
//     carries the whole calibrated gain and the two never double-count.
//
// Everything between the resamplers runs oversampled. See Resampler.hpp for why
// that is a requirement and not a quality setting.
//
// ================================ LATENCY ===================================
// latencySamples() host samples, from the two linear-phase resampler stages. It
// is 20 whenever the core oversamples — the stage delay is 10 * factor
// oversampled samples, which is 10 HOST samples at every factor — and 0 when it
// does not, i.e. at factor 1. Factor 1 is reachable: the automatic rule leaves it
// at 1 for host rates at or above ~344 kHz, and tests may ask for it explicitly.
// Read latencySamples(), do not assume 20.
//
// ============================== THREADING ===================================
// prepare() and reset() are setup-thread calls. Every PARAMETER setter is a
// relaxed atomic store and may be called from anywhere; process() snapshots them
// once per internal chunk. setNoiseSeed() is the exception and is documented as
// such. Nothing on the process path allocates, locks, or does I/O — the
// static_asserts below hold the "no locks" half of that to account.
//
// ============================== NOT MODELLED ================================
// Read this before trusting the output at the extremes:
//   * The iron law is a CONSTANT PERCENTAGE fitted over 20-100 Hz inside a ~16 dB
//     drive window. Below that window the real core is in its Rayleigh region and
//     distorts measurably less; above it the amplifier clips. Neither end exists
//     here, so the layer extrapolates rather than predicts outside that box.
//   * The amp's supply rails ARE modelled (RailClip, measured 2026-08-12), but
//     only as the memoryless hard clip the hardware measured as. There is no
//     supply sag, no recovery time and no rail modulation, so sustained heavy
//     clipping stays cleaner and more static than a real desk would.
//   * h2 depth is a measured lower bound, not an identification (see the
//     kIronH2OffsetDb note in the generated header).

#pragma once

#include "Filters.hpp"
#include "IronLayer.hpp"
#include "Noise.hpp"
#include "Pre35Coefficients.hpp"
#include "Pre35Model.hpp"
#include "RailClip.hpp"
#include "Resampler.hpp"

#include <atomic>
#include <cstdint>

namespace pre35
{

// The parameter surface is plain atomics precisely so the audio thread never
// blocks on a control change. On a platform where any of these is not lock-free
// that claim is false and the "no locks in process()" contract quietly breaks,
// so it fails to compile instead.
static_assert(std::atomic<int>::is_always_lock_free,
              "Pre35DSP parameter atomics must be lock-free (std::atomic<int>)");
static_assert(std::atomic<bool>::is_always_lock_free,
              "Pre35DSP parameter atomics must be lock-free (std::atomic<bool>)");
static_assert(std::atomic<double>::is_always_lock_free,
              "Pre35DSP parameter atomics must be lock-free (std::atomic<double>)");

//==============================================================================
/** One-pole parameter smoother, stepped at control rate.

    Deliberately not per-sample: the trim knob retunes filter coefficients, and
    rebuilding a cascade 48000 times a second to chase a knob nobody is turning
    would be absurd. Values that reach the signal as plain gains are linearly
    interpolated ACROSS the chunk instead, so the audio still sees a continuous
    ramp — see Pre35DSP::process.
*/
struct ControlSmoother
{
    double value = 0.0;
    double coeff = 0.0;

    void setTimeConstant(double tauSeconds, double stepSeconds) noexcept
    {
        coeff = (tauSeconds > 0.0) ? std::exp(-stepSeconds / tauSeconds) : 0.0;
    }

    void snap(double v) noexcept { value = v; }

    double step(double target) noexcept
    {
        value = target + coeff * (value - target);
        // Land exactly on the target rather than approaching it forever: an
        // asymptote is what makes a "settled" comparison drift in the last digit.
        if (std::abs(value - target) < 1.0e-12)
            value = target;
        return value;
    }
};

//==============================================================================
class Pre35DSP
{
public:
    /** Host samples per control-rate step. 32 at 48 kHz is 0.67 ms — fast enough
        that a coefficient retune is inaudible, slow enough that rebuilding the
        response cascade costs nothing measurable. */
    static constexpr int kChunk = 32;

    /** Parameter smoothing time constant. */
    static constexpr double kSmoothingTauSeconds = 0.02;

    Pre35DSP();

    //==========================================================================
    /** @param sampleRate        host rate
        @param oversampleFactor  0 (default) picks the smallest power of two that
                                 puts the internal rate above ~344 kHz, capped at
                                 8: 8x at 44.1/48 kHz, 4x at 88.2/96 kHz, 2x at
                                 176.4/192 kHz. Pass an explicit factor only for
                                 tests and null comparisons.
        Allocates nothing (every buffer is a fixed-size member) but is still a
        setup-thread call: it recomputes every filter in the chain. */
    void prepare(double sampleRate, int oversampleFactor = 0);

    /** Clears every filter, detector and resampler state and snaps all smoothers
        to their current targets, so the next sample is steady-state.

        ONE EXCEPTION, deliberate: the noise generator's RNG stream is NOT
        rewound. reset() is also the recovery path for a non-finite sample
        (process() calls it), and a fault that rewinds the hiss to the same
        starting sequence would turn a transient glitch into a repeating one.
        Rewinding the stream is what setNoiseSeed() + prepare() are for. A
        warm-vs-fresh comparison therefore only matches with noise disabled. */
    void reset();

    /** In-place, mono, host rate. */
    void process(float* buffer, int numSamples);

    //==========================================================================
    // Parameters. Relaxed atomic stores; safe from any thread at any time.
    //
    // Pad, trim, iron, output and AUTO GAIN reach the signal through a ~20 ms
    // smoother, because each of them is a step the signal would otherwise take in
    // one control tick: a bare auto-gain flip is up to 58 dB and a bare pad
    // change 39 dB. Auto gain is smoothed as a 0..1 BLEND rather than a branch —
    // see autoGainSm.
    //
    // Noise is the exception: it is not smoothed. It is snapshotted once per
    // control step into noiseActive and applied at chunk boundaries, so a toggle
    // lands on a chunk edge instead of on whatever buffer edge the host chose.

    /** 0 = no pad, 1 = 20 dB, 2 = 40 dB (see coeffs::kPads for the labels). */
    void setPadIndex(int padIndex);
    /** Trim knob position, 0-100 %. */
    void setTrimPercent(double percent);
    /** 0 bypasses the transformer layer, 1 is the measured device, 2 is twice as
        much of it than anything on the bench justifies. */
    void setIronAmount(double amount);
    void setNoiseEnabled(bool enabled);
    /** Cancels the modelled taper+pad gain at the output so trim and pad change
        the CHARACTER without changing the level. Takes exclusive control of the
        output stage while enabled, fading any legacy manual Output trim out. */
    void setAutoGain(bool enabled);
    /** Compatibility-only manual output trim. It is active while Auto Gain is off
        and smoothly ignored while Auto Gain is on. */
    void setOutputGainDb(double db);

    //==========================================================================
    /** NOT thread-safe and NOT a parameter: a plain member write, read only by
        prepare(). Call it before prepare() (a stereo pair decorrelates its two
        channels this way). Calling it while process() is running races. */
    void setNoiseSeed(uint64_t seed);

    //==========================================================================
    // Observers. These are NOT synchronised — see each one.

    /** Fixed between prepare() calls; safe to read from anywhere. */
    int    latencySamples()   const noexcept { return latency; }
    double getSampleRate()    const noexcept { return hostRate; }
    int    oversampleFactor() const noexcept { return factor; }
    double oversampledRate()  const noexcept { return hostRate * factor; }

    /** Modelled cal gain (taper + pad) at the CURRENT SMOOTHED setting, in dB.
        AUDIO-THREAD ONLY: reads non-atomic smoother state that process() writes,
        and process() itself calls it once per control step. */
    double currentGainCalDb() const noexcept;
    /** Total output scaling the chain is applying, in dB, including auto-gain.
        AUDIO-THREAD ONLY, same reason. */
    double currentOutputGainDb() const noexcept;

    /** TEST-ONLY. Live references to audio-thread-owned objects: reading them
        while process() runs is a data race. They exist so the gates can inspect
        realised coefficients without a second copy of the model algebra. */
    const FirstOrderCascade& responseFilter() const noexcept { return response; }
    const IronLayer&         ironLayer()      const noexcept { return iron; }

private:
    void rebuildResponse(int padIndex, double trimPercent);
    void updateControls();

    //==========================================================================
    // Targets, written by setters from any thread.
    std::atomic<int>    padTarget    { 0 };
    std::atomic<double> trimTarget   { 0.0 };
    std::atomic<double> ironTarget   { 1.0 };
    std::atomic<double> outputDbTarget { 0.0 };
    std::atomic<bool>   noiseOn      { false };
    std::atomic<bool>   autoGainOn   { false };

    // Smoothed, control-rate.
    ControlSmoother trimSm;
    ControlSmoother padDbSm;
    ControlSmoother ironSm;
    ControlSmoother outputDbSm;
    /** Auto-gain is a BLEND, 0..1, not a branch: flipping the flag would
        otherwise move the output stage by the whole cal gain (up to 58 dB) in one
        control step, which is a full-scale bang. Smoothed, it crossfades over the
        same ~20 ms the pad switch gets, and still lands exactly on 0 or 1. */
    ControlSmoother autoGainSm;

    // Per-chunk endpoints for the in-chunk linear ramps.
    double padGainPrev    = 1.0;
    double taperGainPrev  = 1.0;
    double ironAmountPrev = 1.0;
    double outGainPrev    = 1.0;
    double padGainNext    = 1.0;
    double taperGainNext  = 1.0;
    double ironAmountNext = 1.0;
    double outGainNext    = 1.0;

    // Chain.
    Upsampler         upsampler;
    Downsampler       downsampler;
    IronLayer         iron;
    NoiseGenerator    noise;
    RailClip          rail;
    FirstOrderCascade response;

    double osBuffer[kChunk * kMaxOversampleFactor] {};

    double   hostRate   = 0.0;
    int      factor     = 1;
    int      latency    = 0;
    int      chunkPhase = 0;    ///< host samples since the last control step
    int      coeffPad   = -1;
    double   coeffTrim  = -1.0;
    /** Snapshotted at control rate like every other parameter, so a noise toggle
        lands on a chunk boundary and not on whatever buffer edge the host chose. */
    bool     noiseActive = false;
    uint64_t noiseSeed = 0x50524533ull;   // 'PRE3'
    bool     prepared  = false;
};

} // namespace pre35

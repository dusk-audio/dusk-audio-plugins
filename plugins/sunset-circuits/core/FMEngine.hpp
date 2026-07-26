// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FMEngine.hpp — Prism: a single-voice 4-operator FM engine (classic 4-op).
//
// One `FMVoiceEngine` = one polyphonic voice's oscillator section. Phase 3 slots
// it inside SynthVoice as an alternative to the analog oscillator bank; the host
// filter, amp envelope, unison and FX all stay OUTSIDE — this class produces the
// raw FM voice signal only. It renders at whatever sample rate it is given (the
// voice hands it the internal oversampled rate) and does no oversampling itself.
//
// Design (see docs/dpf-migration/09-multi-synth.md "Prism"):
//   * 4 sine operators, each a phase accumulator + per-op ADSR (core ADSREnvelope).
//   * 8 routing algorithms from the shared kPrismAlgos table (FMAlgorithms.hpp),
//     which the UI diagram widget renders from too.
//   * Phase modulation: a modulator adds `env * level² · sin(...)` TURNS into its
//     target's phase (square-law level — the classic FM level feel; one turn is
//     the 2π radians the depth used to be expressed in). A carrier contributes
//     `env · level · sin(...)` to the output bus.
//   * Op 4 self-feedback 0..1 with the classic 2-sample-average damping.
//   * Per op: ratio, fine cents, level, velocity sensitivity, key-level scaling.
//
// Real-time safe: no allocation, no locks, no I/O in noteOn/processSample; all
// operator/envelope storage is fixed-size arrays.

#pragma once

#include "SynthCommon.hpp"
#include "Envelope.hpp"
#include "FMAlgorithms.hpp"

namespace msynth
{

// sin(2π·x) with x in TURNS — the operator sine, replacing libm sinf in the FM
// hot loop (4 ops × up to 16 unison banks × the internal rate; at 4x
// oversampling that is millions of evaluations a second, and it measures ~14
// points of real time in the worst-case Prism scenario).
//
// TURNS, NOT RADIANS, is the load-bearing choice, for three separate reasons:
//
//   1. The range reduction is EXACT. `x - round(x)` subtracts an integer from a
//      float, and the result — being a multiple of ulp(x) no larger than 0.5 —
//      is always representable, so the reduction loses no bits at all. Reducing
//      in radians instead means dividing by 2π and eating a rounding error that
//      grows with the argument, exactly where FM arguments are largest.
//   2. The wrap is SILENT. The x·(1−4x²) factor pins exact zeros at x = 0 and
//      x = ±0.5, so however the polynomial residual falls, the approximation is
//      bit-exactly zero at the wrap point. A plain minimax polynomial leaves a
//      small step there, and a step once per cycle is broadband spray at the
//      carrier's own rate.
//   3. It deletes a multiply. Depths are carried in turns end to end (see
//      updateGain / modDepth), so the hot loop adds phase and modulation
//      directly instead of scaling one of them by 2π first.
//
// THIS IS A LATENCY PROBLEM, NOT A THROUGHPUT ONE, and both remaining shapes
// below follow from that. Ops are evaluated 3→0 with each one's output feeding
// the next one's phase, so the four sines of a sample sit on one serial
// dependency chain and only the chain's LENGTH matters. Measured in situ on the
// worst-case scenario (f), starting from 100.7% of real time with libm:
//
//     libm sinf                                        100.7%   (baseline)
//     cvttss2si reduction + Horner polynomial          100.0%   -0.7
//     cvttss2si reduction + Estrin polynomial           96.0%   -4.7
//     add/subtract reduction + Horner polynomial        94.0%   -6.7
//     add/subtract reduction + Estrin polynomial        93.3%   -7.4  <- this
//     sine removed entirely (unreachable floor)         86.3%  -14.4
//
//   * The REDUCTION avoids int at all. (x + 2^23·1.5) − 2^23·1.5 rounds x to the
//     nearest integer purely in the FP domain; the obvious `(float)(int)` round
//     trip costs two conversions plus a register-domain crossing, and that alone
//     is 2.7 points. Valid for |x| < 2^22, which the reachable range below
//     clears by five orders of magnitude.
//   * The POLYNOMIAL is evaluated Estrin rather than Horner. Same coefficients,
//     same result to the bit in almost every case, two fewer links in the chain.
//
// The reduction assumes ROUND-TO-NEAREST, the default and the only mode a plugin
// host may leave the FPU in (changing it would break libm for everything else in
// the process too). It also assumes the compiler does not algebraically cancel
// `(x + K) − K` back to `x`, which is legal only under -ffast-math and which
// this project does not enable. Both assumptions are load-bearing, and sin_gate
// fails outright if either breaks — verified by building sin_test with
// -ffast-math, which collapses the reduction and takes the measured error from
// 6.7e-6 to 1.0.
//
// Accuracy, measured in float32 in this operation order by the fm suite's
// sin_gate: max abs error 6.7e-6 = −103.5 dB, over one period and over the full
// ±34-turn reachable range alike. That is 23 dB inside the −80 dB (1e-4) design
// target, and below the float32 quantisation of the argument itself at deep
// modulation (ulp(33 turns)/2 = 1e-6 turns = 6.2e-6 in sine value). The error is
// a smooth function of phase, so on a steady tone it lands on the signal's own
// harmonics rather than creating new non-harmonic bins: measured on a bare
// carrier, THD goes from 0.000027% to 0.000656% against a 1% gate.
//
// REACHABLE RANGE. The argument is phase + modAccum. modAccum is bounded
// structurally: updateGain clamps the effective level to 4, so modDepth ≤ 16
// turns; kPrismAlgos has a maximum in-degree of 2 (algorithms #2 and #3); and
// op-4 feedback adds at most 1 turn. That is 2·16 + 1 + 1 = 34 turns, and a
// swept-to-the-rails render over all 8 algorithms at feedback 1 measures 33.0.
// A non-finite argument is unreachable (every term is bounded and the output of
// this function is bounded by construction), but if one ever arrived it would
// propagate NaN rather than trap, and the isBad() guard at the end of
// processSample would catch it.
inline float sinTurns(float x) noexcept
{
    // 1.5 · 2^23: adding it pushes x into the binade where ulp == 1, so the
    // add rounds away every fractional bit; subtracting it back leaves exactly
    // round(x). Two dependent adds, no conversions, no branches.
    constexpr float kRoundMagic = 12582912.0f;
    const float w  = x - ((x + kRoundMagic) - kRoundMagic);   // [-0.5, 0.5]
    const float u  = w * w;
    const float u2 = u * u;
    // g(u) fitted by IRLS toward the minimax of the FINAL value error, not of g.
    const float g  = (6.2830423f + u * -16.1981858f) + u2 * (16.5597331f + u * -8.15233665f);
    return w * (1.0f - 4.0f * u) * g;
}

class FMVoiceEngine
{
public:
    static constexpr int kNumOps = 4;

    // ---- operator-envelope control rate ----------------------------------
    // The four operator ADSRs used to tick once per INTERNAL sample, which at 4x
    // oversampling means 192 kHz — four envelopes, each with a divide, evaluated
    // 192000 times a second per unison bank to describe a signal whose fastest
    // possible move is the 1 ms attack floor. They tick at a decimated CONTROL
    // rate instead, with the per-sample value linearly interpolated between
    // control points.
    //
    // The divisor is derived from the sample rate to hold the control rate near
    // this target rather than being a fixed ratio, and the difference is not
    // cosmetic. A fixed /4 would put the control rate at 12 kHz when the engine
    // runs at 1x/48 kHz, and a fixed /8 at 5.5 kHz at 1x/44.1 kHz — a slope
    // corner every 5.5 kHz, in band, on a signal that multiplies the modulation
    // index. Tying it to absolute time instead makes envelope fidelity
    // independent of the oversampling switch, and puts the decimation exactly
    // where the CPU problem is: /4 at 4x, /2 at 2x, and /1 at 1x, where the
    // engine is nowhere near the wall and the result is bit-identical to
    // ticking every sample.
    //
    // 48 kHz was chosen by measurement, against the same code with the divisor
    // forced to 1, at 192 kHz internal, on the 1 ms attack floor that three of
    // the five Prism factory presets sit on for every operator:
    //
    //                     10-90% rise err   carrier attack   modulator attack
    //     /4  48 kHz ctrl        +0.01%         -91.2 dB          -84.7 dB
    //     /8  24 kHz ctrl        +0.02%         -79.1 dB          -72.7 dB
    //
    // Timing is untouched either way — the interpolant is exact at every control
    // point, so decimation costs curvature, not rise time. What it does cost is
    // the attack's fine structure, and halving the control rate costs 12 dB more
    // of it in both the amplitude and the phase-modulation case. A 24 kHz target
    // measured 0.3 to 0.8 points of real time better across the Prism scenarios,
    // which does not buy 12 dB, and it would drop 1x/48 kHz to a 24 kHz control
    // rate as well — paying the fidelity everywhere to save CPU only at 4x.
    //
    // WHAT THE ERROR ACTUALLY IS, since "control rate" invites the wrong worry.
    // There is no stair-step and there can be no click: envValue is a running
    // accumulator that is never assigned a jump, so the delivered envelope is
    // continuous by construction and its per-sample slope is bounded by the
    // envelope's own. What is left is the curvature the straight line misses,
    // which for the p² attack curve is delta²/4 with delta the phase advanced per
    // control tick — 1.1e-4 at the 1 ms floor, matching the measured 1.097e-4 —
    // plus the same order of corner-rounding at stage transitions (worst measured
    // across the attack range, 1.4e-4 at the attack→decay corner of a 50 ms
    // attack). So the operator envelope is within 1.4e-4 of its undecimated self
    // everywhere, which is a sub-4 µs error along its own trajectory. On a real
    // preset that shows as at most 0.63 dB in one third-octave band over the
    // first 5 ms of a note, and 0.03 dB once the note is sounding.
    static constexpr float kEnvControlRate = 48000.0f;
    static constexpr int   kMaxEnvDivisor  = 8;

    FMVoiceEngine() noexcept
    {
        for (int i = 0; i < kNumOps; ++i)
        {
            op[i].env.setCurve(EnvelopeCurve::Exponential);
            op[i].env.setParameters(0.005f, 0.4f, 0.7f, 0.4f);
            updateGain(i);
        }
    }

    // ---- lifecycle -------------------------------------------------------
    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        invSr = sr > 0.0f ? 1.0f / sr : 0.0f;
        recomputeEnvControlRate();
        recomputeIncrements();
        reset();
    }

    // Rate change without dropping the current note (oversampling-factor swap).
    void setSampleRate(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        invSr = sr > 0.0f ? 1.0f / sr : 0.0f;
        recomputeEnvControlRate();
        recomputeIncrements();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kNumOps; ++i)
        {
            op[i].phase = 0.0f;
            op[i].env.reset();
            op[i].envValue = 0.0f;
            op[i].envSlope = 0.0f;
        }
        envCountdown = 0;
        fbZ1 = fbZ2 = 0.0f;
    }

    // ---- note control ----------------------------------------------------
    void noteOn(float freqHz, float velocity) noexcept
    {
        baseFreq = maxf(0.0f, freqHz);
        // Derive the MIDI note for key scaling (level tilt about note 60).
        note = baseFreq > 0.0f ? (69.0f + 12.0f * std::log2(baseFreq / 440.0f)) : 60.0f;
        vel  = clampf(velocity, 0.0f, 1.0f);

        recomputeIncrements();
        for (int i = 0; i < kNumOps; ++i)
        {
            updateGain(i);          // vel/keyScale depend on the new note+velocity
            op[i].phase = 0.0f;     // deterministic attack transient
            op[i].env.noteOn();
        }
        // Tick on the very next sample rather than finishing the interpolation
        // period this note-on landed in: an attack must not spend up to a whole
        // control period coasting on the OLD slope. envValue is deliberately
        // left where it is — the next tick derives the new slope from it, which
        // is what keeps a retrigger continuous (the same reason ADSREnvelope
        // seeds its attack phase from the current level).
        envCountdown = 0;
        fbZ1 = fbZ2 = 0.0f;
    }

    void noteOff() noexcept
    {
        for (int i = 0; i < kNumOps; ++i)
            op[i].env.noteOff();
        envCountdown = 0;   // same reasoning as noteOn: release starts now
    }

    // Per-sample base-frequency update WITHOUT retriggering: pitch bend, master
    // tune, portamento, drift, vibrato and unison detune flow through here while
    // the note sustains. Key-level scaling stays pinned to the note-on note.
    void setFrequency(float freqHz) noexcept
    {
        baseFreq = maxf(0.0f, freqHz);
        recomputeIncrements();
    }

    // Voice is alive while ANY carrier envelope is still running. Reads the
    // CONTROL-rate stage, so it can go false while the interpolant still has up
    // to one control period left to slide down to zero. Nothing calls this —
    // SynthVoice gates Prism on its own amp envelope — and a caller that did
    // would be truncating at most 20 µs of an already-inaudible tail; the note
    // is here so anyone who wires it up knows which of the two it is reading.
    bool isActive() const noexcept
    {
        const uint8_t mask = kPrismAlgos[algo].carrierMask;
        for (int i = 0; i < kNumOps; ++i)
            if ((mask & (1u << i)) && op[i].env.isActive())
                return true;
        return false;
    }

    // ---- setters (host/UI) ----------------------------------------------
    void setAlgorithm(int a) noexcept { algo = clampi(a, 0, 7); }
    int  getAlgorithm() const noexcept { return algo; }

    void setOpRatio(int i, float ratio) noexcept
    {
        op[idx(i)].ratio = clampf(ratio, 0.0f, 32.0f);
        recomputeIncrement(idx(i));
    }
    void setOpFine(int i, float cents) noexcept
    {
        op[idx(i)].fineMult = std::pow(2.0f, clampf(cents, -100.0f, 100.0f) / 1200.0f);
        recomputeIncrement(idx(i));
    }
    void setOpLevel(int i, float level) noexcept
    {
        op[idx(i)].level = clampf(level, 0.0f, 1.0f);
        updateGain(idx(i));
    }
    void setOpVelSens(int i, float v) noexcept
    {
        op[idx(i)].velSens = clampf(v, 0.0f, 1.0f);
        updateGain(idx(i));
    }
    void setOpKeyScale(int i, float k) noexcept
    {
        op[idx(i)].keyScale = clampf(k, -1.0f, 1.0f);
        updateGain(idx(i));
    }
    void setOpADSR(int i, float a, float d, float s, float r) noexcept
    {
        op[idx(i)].env.setParameters(a, d, s, r);
    }

    // Op-4 self-feedback amount, 0..1.
    void setFeedback(float fb) noexcept { feedback = clampf(fb, 0.0f, 1.0f); }

    // ---- audio -----------------------------------------------------------
    // Produces one mono sample of the raw FM voice. Ops are evaluated 3->0;
    // because every edge has from > to (kPrismAlgos invariant), each target's
    // modulators are already computed and their contributions accumulated before
    // the target itself is evaluated.
    float processSample() noexcept
    {
        const PrismAlgo& A = kPrismAlgos[algo];
        const uint8_t carrierMask = A.carrierMask;

        float modAccum[kNumOps] = { 0.0f, 0.0f, 0.0f, 0.0f };

        // Operator envelopes tick at the decimated control rate (see
        // kEnvControlRate) and are linearly interpolated in between. The slope
        // is chosen so envValue arrives at the new control point EXACTLY on the
        // next tick, which makes the interpolant continuous across ticks and
        // keeps it time-aligned with an undecimated envelope at every tick — the
        // decimation costs curvature between control points, not timing.
        if (envCountdown == 0)
        {
            envCountdown = envDivisor;
            for (int i = 0; i < kNumOps; ++i)
            {
                Op& o = op[i];
                o.envSlope = (o.env.processSample() - o.envValue) * invEnvDivisor;
            }
        }
        --envCountdown;

        // Distribute op-4 self-feedback into its own phase first (classic
        // 2-sample average damping keeps the loop from screaming).
        if (feedback > 0.0f)
        {
            const float fbDepth = feedback * feedback;  // square-law, up to 1 turn
            modAccum[A.fbOp] += fbDepth * 0.5f * (fbZ1 + fbZ2);
        }

        float out = 0.0f;

        for (int i = kNumOps - 1; i >= 0; --i)
        {
            Op& o = op[i];
            const float env = o.envValue;
            o.envValue += o.envSlope;
            // Phase and modulation are both in turns, so this is a bare add.
            const float s   = sinTurns(o.phase + modAccum[i]);

            if (i == A.fbOp)
            {
                fbZ2 = fbZ1;
                fbZ1 = s;               // pre-level output feeds the feedback tap
            }

            // Feed-forward into the ops this one modulates.
            const float modContribution = env * o.modDepth * s;
            for (int e = 0; e < A.nEdges; ++e)
                if (A.edges[e].from == i)
                    modAccum[A.edges[e].to] += modContribution;

            // Carrier contribution to the output bus.
            if (carrierMask & (1u << i))
                out += env * o.carrierGain * s;

            // Advance phase. Full wrap via floor handles a pathological
            // inc >= 1 (very high ratio x OS); identical to a single subtraction
            // for the normal inc < 1 case (floor is exactly 1 when 1 <= phase < 2). (C3)
            o.phase += o.inc;
            if (o.phase >= 1.0f) o.phase -= std::floor(o.phase);
            else if (o.phase < 0.0f) o.phase += 1.0f;
        }

        if (isBad(out)) out = 0.0f;
        return out;
    }

private:
    struct Op
    {
        // params
        float ratio    = 1.0f;
        float fineMult = 1.0f;   // 2^(cents/1200)
        float level    = 0.0f;
        float velSens  = 0.0f;
        float keyScale = 0.0f;
        // derived
        float inc         = 0.0f;   // phase increment per sample
        float modDepth    = 0.0f;   // effLevel², in TURNS (as a modulator)
        float carrierGain = 0.0f;   // effLevel          (as a carrier)
        // state
        float phase = 0.0f;
        ADSREnvelope env;           // ticked at the CONTROL rate, not per sample
        float envValue = 0.0f;      // interpolated envelope for the current sample
        float envSlope = 0.0f;      // per-sample step toward the next control point
    };

    static int idx(int i) noexcept { return clampi(i, 0, kNumOps - 1); }

    // Pick the envelope control divisor for the current rate and re-prepare the
    // four ADSRs at the resulting control rate — they own the time constants, so
    // "tick a quarter as often" has to mean "tick at a quarter of the rate" or
    // every attack, decay and release would come out four times too long.
    //
    // setSampleRate (the oversampling swap) reaches here too, and must not drop
    // the note: ADSREnvelope::setSampleRate moves the per-sample constants and
    // leaves stage and phase alone, and envValue is likewise kept so the
    // interpolant is continuous across the switch. Only the cadence restarts.
    void recomputeEnvControlRate() noexcept
    {
        const int d = clampi((int)(sr / kEnvControlRate + 0.5f), 1, kMaxEnvDivisor);
        envDivisor    = d;
        invEnvDivisor = 1.0f / (float)d;
        envCountdown  = 0;
        const double controlRate = (double)sr / (double)d;
        for (int i = 0; i < kNumOps; ++i)
            op[i].env.setSampleRate(controlRate);
    }

    void recomputeIncrement(int i) noexcept
    {
        op[i].inc = baseFreq * op[i].ratio * op[i].fineMult * invSr;
    }
    void recomputeIncrements() noexcept
    {
        for (int i = 0; i < kNumOps; ++i) recomputeIncrement(i);
    }

    // Effective level after velocity sensitivity and key-level scaling, then the
    // two output gains derived from it (square-law depth for modulators, linear
    // for carriers). The modulator depth is in TURNS — see sinTurns.
    void updateGain(int i) noexcept
    {
        Op& o = op[i];
        // velSens=0 -> always full; velSens=1 -> scales straight with velocity.
        const float velFactor = 1.0f - o.velSens * (1.0f - vel);
        // keyScale -1..+1: ±1 halves/doubles the level every two octaves from
        // note 60 (gentle enough for e-piano tine roll-off).
        const float keyFactor = std::pow(2.0f, o.keyScale * (note - 60.0f) / 24.0f);
        const float eff = clampf(o.level * velFactor * keyFactor, 0.0f, 4.0f);
        o.carrierGain = eff;
        o.modDepth    = eff * eff;
    }

    Op  op[kNumOps];
    int algo = 4;              // default: dual-stack (e-piano) — see #5 in table

    float sr    = 44100.0f;
    float invSr = 1.0f / 44100.0f;
    float baseFreq = 440.0f;
    float note = 60.0f;
    float vel  = 1.0f;

    float feedback = 0.0f;
    float fbZ1 = 0.0f, fbZ2 = 0.0f;   // op-4 feedback history (pre-level sines)

    // Operator-envelope control-rate decimation (see kEnvControlRate).
    int   envDivisor    = 1;      // internal samples per envelope tick
    float invEnvDivisor = 1.0f;
    int   envCountdown  = 0;      // samples left before the next tick
};

} // namespace msynth

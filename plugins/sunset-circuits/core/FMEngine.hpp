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
// Design ("Prism" mode):
//   * 4 sine operators, each a phase accumulator + stepped digital rate envelope.
//   * 8 routing algorithms from the shared kPrismAlgos table (FMAlgorithms.hpp),
//     which the UI diagram widget renders from too.
//   * Phase modulation: the normalized operator control follows the DX27 /
//     DX100 output-level law, 0.75 dB per 1/99 of the range (74.25 dB in all).
//     A full-level modulator reaches four turns (8 pi rad) of deviation, as on
//     that hardware; carriers use the same law as output gain.
//   * Op 4 self-feedback 0..1 in seven steps with the classic 2-sample-average
//     damping; each step's depth is measured on the DX27 / DX100 (kFeedbackTurns),
//     and the loop taps the operator's output, so it follows level and envelope.
//   * Per op: ratio, fine cents, level, velocity sensitivity, key-level scaling.
//
// Real-time safe: no allocation, no locks, no I/O in noteOn/processSample; all
// operator/envelope storage is fixed-size arrays.

#pragma once

#include "SynthCommon.hpp"
#include "FMAlgorithms.hpp"

#if defined(__FAST_MATH__)
#error "sinTurns() range reduction relies on strict FP; it is invalid under -ffast-math (see sin_test)"
#endif

namespace msynth
{

// Four-operator hardware uses quantised rate/level envelope generators rather
// than a continuously parameterised analogue ADSR.  Prism keeps the familiar
// ADSR UI, but maps it onto stepped logarithmic times and the hardware's sustain
// law before running this exponential digital EG.
class FMRateEnvelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        updateCoefficients();
        reset();
    }

    void setSampleRate(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        updateCoefficients();
    }

    // Decay 2's top value: the sustain level holds, as it always did before the
    // second decay existed. Anything above kDecay2HoldFrom (the geometric middle
    // between the slowest step, 86.3 s, and the top) reads as Hold, so a host's
    // normalised value a hair under the maximum still holds.
    static constexpr float kDecay2Hold = 100.0f;
    static constexpr float kDecay2HoldFrom = 92.9f;

    // decay2 is Decay 2's time to fall 60 dB (kDecay2Hold = hold). rateMul speeds
    // every stage up by that factor (rate scaling, see FMVoiceEngine); 1 leaves
    // the displayed times exact.
    void setParameters(float attack, float decay, float sustain, float release,
                       float decay2 = kDecay2Hold, float rateMul = 1.0f) noexcept
    {
        a = quantizeTime(attack);
        d = quantizeTime(decay, kLongTimeSteps);
        d2Hold = !(decay2 < kDecay2HoldFrom);   // NaN holds
        d2 = d2Hold ? 0.0f : quantizeTime(decay2, kLongTimeSteps);
        m = rateMul > 0.0f ? rateMul : 1.0f;
        // Sustain follows the DX27 / DX100's sustain level: 3.01 dB per 1/15 of
        // the range (measured: steps 14, 8 and 1 sit 3.01, 21.10 and 42.54 dB
        // below full), 45.15 dB over it, and silence at 0. Continuous, not in
        // the hardware's 3 dB steps: a modulator's sustain can sit on a sharp
        // spectral notch (Prism Talking Keys swings 11 dB across one 0.72 dB
        // step), so a stepped knob could not carry a sound over exactly.
        const float sc = clampf(sustain, 0.0f, 1.0f);
        s = sc > 0.0f ? std::exp2((sc - 1.0f) * 7.49925f) : 0.0f;
        r = quantizeTime(release, kLongTimeSteps);
        updateCoefficients();
    }

    void noteOn() noexcept
    {
        // A retrigger, or a re-press during the release, climbs on from where
        // the envelope is rather than restarting from silence.
        attackW = attackStartFor(value);
        stage = Stage::Attack;
    }
    void noteOff() noexcept { if (stage != Stage::Idle) stage = Stage::Release; }
    bool isActive() const noexcept { return stage != Stage::Idle; }

    float processSample() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: return 0.0f;
            case Stage::Attack:
                // The DX27 / DX100 attack climbs along an exponential in dB, fast
                // out of silence and easing into full level. Measured on
                // chipsynth OPMZ at attack rates 20, 22 and 24 (one shape at every
                // speed): across its rise from -40 to -0.2 dB it crosses -30, -20,
                // -10, -6, -3 and -1 dB at 0.074, 0.174, 0.345, 0.491, 0.646 and
                // 0.859 of the way. dB(pos) = -60 (e^(-4.3 pos) - e^(-4.3)) /
                // (1 - e^(-4.3)) crosses them within 0.015. attackW carries
                // e^(-4.3 pos), so each sample is one multiply and one exp2.
                attackW *= attackMul;
                if (attackW <= kAttackEndW)
                {
                    attackW = kAttackEndW;
                    value = 1.0f;
                    stage = Stage::Decay;
                }
                else
                {
                    value = std::exp2((attackW - kAttackEndW) * kAttackLog2PerW);
                }
                break;
            case Stage::Decay:
                // The DX27 / DX100 decays in a straight line in dB and stops at the
                // sustain level. Measured on chipsynth OPMZ: decay rate 16 to
                // sustain step 8 falls 9.7 dB per 100 ms and sits at -21.2 dB from
                // about 220 ms. An approach toward the sustain level, as this
                // was, slows as it nears it; to a sustain of zero the two are the
                // same, and this form is bit-identical there.
                value -= value * cd;
                if (value <= maxf(s, 0.0005f)) { value = s; stage = Stage::Sustain; }
                break;
            case Stage::Sustain:
                // The DX27 / DX100's second decay (D2R) carries on from the
                // sustain level in a straight line in dB at its own rate, down to
                // silence while the key is held. It runs on the first decay's
                // rate table (measured on chipsynth OPMZ: second over first decay
                // slope 1.00 to 1.02 at five steps), so Decay 2 is a time on the
                // same 60 dB law and grid as Decay. At Hold the level stays.
                if (d2Hold) value = s;
                else
                {
                    value -= value * cd2;
                    if (value <= 0.0001f) value = 0.0f;
                }
                break;
            case Stage::Release:
                value += (0.0f - value) * cr;
                if (value <= 0.0001f) { value = 0.0f; stage = Stage::Idle; }
                break;
        }
        if (isBad(value)) { value = 0.0f; stage = Stage::Idle; }
        return value;
    }

    void reset() noexcept { value = 0.0f; attackW = 1.0f; stage = Stage::Idle; }

    // Rate scaling: every stage runs rateMul times faster than its displayed
    // time (1 = as displayed). Set per note by FMVoiceEngine::noteOn.
    void setRateMultiplier(float rateMul) noexcept
    {
        const float nm = rateMul > 0.0f ? rateMul : 1.0f;
        if (nm != m) { m = nm; updateCoefficients(); }
    }

    // The time grid: logarithmic steps of 10^(4/47) from 1 ms. Attack stops at
    // step 47 (10 s; the DX27 / DX100's slowest attack reaches full level in
    // about 9 s). Decay, Decay 2 and Release continue to step 58, 86.3 s: the
    // hardware's slowest first and second decays fall 60 dB in about 80 s and
    // its slowest release in about 40 s (measured on chipsynth OPMZ).
    static constexpr int kTimeSteps = 47;
    static constexpr int kLongTimeSteps = 58;

private:
    // The attack curve: attackW = e^(-4.3 pos) runs from 1 down to e^(-4.3), and
    // the level is 2^((attackW - e^(-3)) x kAttackLog2PerW), -60 dB to 0 dB.
    static constexpr float kAttackEndW = 0.0135685590f;        // e^(-4.3)
    static constexpr float kAttackLog2PerW = -10.1028656f;     // -60 dB / (1 - e^(-4.3)), in powers of two

    // Where on the attack curve the level is v (1 = silence, the start).
    static float attackStartFor(float v) noexcept
    {
        if (v <= 0.001f) return 1.0f;
        if (v >= 1.0f) return kAttackEndW;
        return kAttackEndW + std::log2(v) / kAttackLog2PerW;
    }

    // The upper clamp sits far above the grid's top so the step clamp decides
    // the longest time; every time up to 10 s quantizes exactly as it did when
    // all four stages clamped at 10 s.
    static float quantizeTime(float seconds, int maxStep = kTimeSteps) noexcept
    {
        const float t = clampf(seconds, 0.001f, 1000.0f);
        constexpr float lo = -9.96578428f; // log2(0.001)
        constexpr float hi =  3.32192809f; // log2(10)
        constexpr float steps = 47.0f;
        const float q = std::round((std::log2(t) - lo) * steps / (hi - lo));
        return std::exp2(lo + clampf(q, 0.0f, (float)maxStep) * (hi - lo) / steps);
    }

    float coeff(float seconds) const noexcept
    {
        // Reach within -60 dB of the target in the displayed time.
        return 1.0f - std::exp(-6.90775528f / maxf(1.0f, seconds * sr));
    }

    // Dividing by m == 1 is exact, so with no rate scaling these are the
    // coefficients the envelope always had.
    void updateCoefficients() noexcept
    {
        attackMul = std::exp(-4.3f / maxf(1.0f, a * sr / m));
        cd = coeff(d / m);
        cd2 = d2Hold ? 0.0f : coeff(d2 / m);
        cr = coeff(r / m);
    }

    float sr = 44100.0f;
    float a = 0.005f, d = 0.4f, s = 0.7f, r = 0.4f;
    float d2 = 0.0f, m = 1.0f;
    bool  d2Hold = true;
    float attackMul = 0.0f, cd = 1.0f, cd2 = 0.0f, cr = 1.0f;
    float value = 0.0f, attackW = 1.0f;
    Stage stage = Stage::Idle;
};

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
// sin_gate: max abs error 6.7e-6 = −103.5 dB, over one period and over ±34
// turns (well past the reachable range below) alike. That is 23 dB inside the
// −80 dB (1e-4) design target, and below the float32 quantisation of the
// argument itself at 33 turns (ulp(33)/2 = 1e-6 turns = 6.2e-6 in sine value).
// The error is a smooth function of phase, so on a steady tone it lands on the
// signal's own harmonics rather than creating new non-harmonic bins: measured
// on a bare carrier, THD goes from 0.000027% to 0.000656% against a 1% gate.
//
// REACHABLE RANGE. The argument is phase + modAccum. modAccum is bounded
// structurally: modDepth is at most four turns, the largest target in-degree is
// two, and operator-4 feedback adds under half a turn. Including the phase, that
// is under 10 turns. The ±34-turn sin_gate deliberately exercises over three times the
// reachable range as a guard against future depth changes.
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

// Operator-4 feedback depth per step 1..7, in turns of phase per unit of the
// two-sample average of the feedback tap. Fitted to chipsynth OPMZ in DX27 /
// DX100 mode (tools repo, tools/sunset-circuits/opmz-parity): with this loop at
// the default 2x internal rate (96 kHz), each value reproduces that step's
// harmonics 2 to 8 within 1.1 dB rms. The steps nearly double, like the chip's
// 1/128 turn x 2^(k-1); step 6 sits 1.5 dB under that rule, where the chip's
// own rate (55.9 kHz) shapes the loop most.
static constexpr float kFeedbackTurns[8] = {
    0.0f, 0.00816f, 0.01606f, 0.02975f, 0.06153f, 0.12026f, 0.21019f, 0.47662f
};

// Key level scaling at full Key Scale (scaling level 99), in dB at middle C:
// 0.0602 dB per unit of scaling level, measured (see updateGain).
static constexpr float kKeyScaleDbAtC4 = 0.0602f * 99.0f;

class FMVoiceEngine
{
public:
    static constexpr int kNumOps = 4;

    FMVoiceEngine() noexcept
    {
        for (int i = 0; i < kNumOps; ++i)
        {
            op[i].env.setParameters(0.005f, 0.4f, 0.7f, 0.4f);
            updateGain(i);
        }
    }

    // ---- lifecycle -------------------------------------------------------
    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        invSr = sr > 0.0f ? 1.0f / sr : 0.0f;
        for (int i = 0; i < kNumOps; ++i)
            op[i].env.prepare(sampleRate);
        recomputeIncrements();
        reset();
    }

    // Rate change without dropping the current note (oversampling-factor swap).
    void setSampleRate(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        invSr = sr > 0.0f ? 1.0f / sr : 0.0f;
        for (int i = 0; i < kNumOps; ++i)
            op[i].env.setSampleRate(sampleRate);
        recomputeIncrements();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kNumOps; ++i)
        {
            op[i].phase = 0.0f;
            op[i].env.reset();
        }
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
        const int key = (int)std::lround(note);
        for (int i = 0; i < kNumOps; ++i)
        {
            updateGain(i);          // vel/keyScale depend on the new note+velocity
            op[i].env.setRateMultiplier(rateMultiplier(key, op[i].rateScale));
            op[i].phase = 0.0f;     // deterministic attack transient
            op[i].env.noteOn();
        }
        fbZ1 = fbZ2 = 0.0f;
    }

    void noteOff() noexcept
    {
        for (int i = 0; i < kNumOps; ++i)
            op[i].env.noteOff();
    }

    // Per-sample base-frequency update WITHOUT retriggering: pitch bend, master
    // tune, portamento, drift, vibrato and unison detune flow through here while
    // the note sustains. Key-level scaling stays pinned to the note-on note.
    void setFrequency(float freqHz) noexcept
    {
        baseFreq = maxf(0.0f, freqHz);
        recomputeIncrements();
    }

    // Voice is alive while ANY carrier envelope is still running.
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
        // Ratios are resolved to 1/4096: whole and simple ratios stay exact, and
        // the DX27 / DX100's inharmonic ones land within half a cent. Those sit a
        // fixed interval above the coarse ratio (measured on chipsynth OPMZ: x1.4143,
        // x1.5702, x1.7306, i.e. +600, +781, +950 cents); at the earlier 1/64 they
        // came out up to 9.3 cents off (1.4143 -> 91/64).
        op[idx(i)].ratio = std::round(clampf(ratio, 0.0f, 32.0f) * 4096.0f) / 4096.0f;
        recomputeIncrement(idx(i));
    }
    void setOpFine(int i, float cents) noexcept
    {
        const float stepped = std::round(clampf(cents, -100.0f, 100.0f));
        op[idx(i)].fineMult = std::pow(2.0f, stepped / 1200.0f);
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
    // Operator envelope: attack, decay, sustain, release, Decay 2 (the second
    // decay; FMRateEnvelope::kDecay2Hold holds) and Rate Scale 0..3.
    void setOpEnvelope(int i, float a, float d, float s, float r, float d2, int rateScale) noexcept
    {
        Op& o = op[idx(i)];
        o.rateScale = clampi(rateScale, 0, 3);
        o.env.setParameters(a, d, s, r, d2, rateMultiplier((int)std::lround(note), o.rateScale));
    }

    // The DX27 / DX100's key code for envelope rate scaling: 4 per octave plus a
    // note group of three keys, each octave running C# to C (the YM2151 family's
    // KC >> 2). Measured on chipsynth OPMZ: at Rate Scale 0 the envelope speeds
    // up one step between C2 and C3, C4 and C5, and C6 and C7, which places the
    // octave boundaries between C and C#.
    static int rateKeyCode(int midiNote) noexcept
    {
        const int k = midiNote - 13;    // C#0 = 0
        if (k < 0) return 0;
        const int code = 4 * (k / 12) + (k % 12) / 3;
        return code > 31 ? 31 : code;
    }
    // How much faster than displayed the envelope runs on this key: the hardware
    // adds key code >> (3 - Rate Scale) to every rate, and four rate steps double
    // the speed (measured: 0.5, 1 and 2 steps per octave at Rate Scale 0, 1 and
    // 2). Taken against middle C, where Prism shows its times, so Rate Scale 0
    // keeps the hardware's slight tracking (one step slower, /1.19, at C2 and
    // below; as displayed from C#2 to C4; x1.19 from C#4 to C6; x1.41 from C#6)
    // and middle C plays every stage exactly as displayed. Each step is 2^(1/4): the
    // chip's own steps run x1.25, x1.2, x1.17, x1.14 within each doubling.
    static float rateMultiplier(int midiNote, int rateScale) noexcept
    {
        const int shift = 3 - clampi(rateScale, 0, 3);
        const int steps = (rateKeyCode(midiNote) >> shift) - (rateKeyCode(60) >> shift);
        return std::exp2(0.25f * (float)steps);
    }

    // Op-4 self-feedback amount, 0..1, in seven steps (k/7).
    void setFeedback(float fb) noexcept
    {
        fbDepth = kFeedbackTurns[(int)std::lround(clampf(fb, 0.0f, 1.0f) * 7.0f)];
    }

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

        // Distribute op-4 self-feedback into its own phase first (classic
        // 2-sample average damping keeps the loop from screaming).
        if (fbDepth > 0.0f)
            modAccum[A.fbOp] += fbDepth * 0.5f * (fbZ1 + fbZ2);

        float out = 0.0f;

        for (int i = kNumOps - 1; i >= 0; --i)
        {
            Op& o = op[i];
            const float env = o.env.processSample();
            // Phase and modulation are both in turns, so this is a bare add.
            const float s   = sinTurns(o.phase + modAccum[i]);

            if (i == A.fbOp)
            {
                // The tap is the operator's output after its envelope and level,
                // as on the DX27 / DX100, whose feedback brightness falls with both
                // (measured: 10.5 dB less level gives 10.5 dB less second harmonic,
                // and a decay 24 dB down takes it down 23.9 dB).
                fbZ2 = fbZ1;
                fbZ1 = env * o.carrierGain * s;
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
        int   rateScale = 0;
        // derived
        float inc         = 0.0f;   // phase increment per sample
        float modDepth    = 0.0f;   // log control mapped to turns (modulator)
        float carrierGain = 0.0f;   // log control mapped to gain (carrier output, op-4 feedback tap)
        // state
        float phase = 0.0f;
        FMRateEnvelope env;
    };

    static int idx(int i) noexcept { return clampi(i, 0, kNumOps - 1); }

    void recomputeIncrement(int i) noexcept
    {
        op[i].inc = baseFreq * op[i].ratio * op[i].fineMult * invSr;
    }
    void recomputeIncrements() noexcept
    {
        for (int i = 0; i < kNumOps; ++i) recomputeIncrement(i);
    }

    // Effective level after velocity sensitivity and key-level scaling, through
    // the DX27 / DX100 output-level law: the normalized control is the output
    // level 0..99 over 99, and each step is 0.75 dB, so the range spans 74.25 dB.
    // Measured on chipsynth OPMZ in DX27/DX100 mode (tools repo,
    // tools/sunset-circuits/opmz-parity): a lone carrier falls 0.75 dB per step
    // from level 99 down to 40, and a modulator's index follows the same 0.75 dB
    // per step from level 27 to 84. Below 40 the carrier measures up to 2.3 dB
    // lower still while the index does not, so that extra sag is in the chip's
    // output stage, not in the level law.
    // A full-level modulator reaches four turns (8 pi rad) of phase deviation:
    // the measured index at levels 70 to 84, carried up the 0.75 dB law, puts
    // level 99 at 24.9 to 25.2 rad. Carriers use the same amplitude law directly.
    void updateGain(int i) noexcept
    {
        Op& o = op[i];
        // velSens=0 -> always full; velSens=1 -> scales straight with velocity.
        const float velFactor = 1.0f - o.velSens * (1.0f - vel);
        const float control = clampf(o.level * velFactor, 0.0f, 1.0f);
        if (control <= 0.0f)
        {
            o.carrierGain = 0.0f;
            o.modDepth = 0.0f;
            return;
        }
        // Key Scale follows the DX27 / DX100's key level scaling: an attenuation
        // in dB proportional to the note's frequency, added to the output level
        // whatever that is, on carriers and modulators alike. Measured on
        // chipsynth OPMZ: scaling level 50 takes 3.01 dB off at C4, 6.02 at C5,
        // 12.05 at C6 and 24.9 at C7, the same at output levels 99, 80 and 65, so
        // 0.0602 dB per unit of scaling level (0..99) at middle C. Key Scale -1..0
        // is that law at scaling level 99..0. The hardware has no positive side;
        // 0..+1 mirrors it, attenuating toward the low keys instead.
        const float ksDb = o.keyScale < 0.0f
            ? -o.keyScale * kKeyScaleDbAtC4 * std::exp2((note - 60.0f) / 12.0f)
            :  o.keyScale * kKeyScaleDbAtC4 * std::exp2((60.0f - note) / 12.0f);
        // 74.25 dB over the level range; ksDb / 6.0206 turns dB into powers of two.
        const float linear = std::exp2((control - 1.0f) * 12.3326581f - ksDb * 0.166096405f);
        o.carrierGain = linear;
        o.modDepth = linear * 4.0f;
    }

    Op  op[kNumOps];
    int algo = 4;              // default: dual-stack (e-piano) — see #5 in table

    float sr    = 44100.0f;
    float invSr = 1.0f / 44100.0f;
    float baseFreq = 440.0f;
    float note = 60.0f;
    float vel  = 1.0f;

    float fbDepth = 0.0f;              // turns per unit of tap, from kFeedbackTurns
    float fbZ1 = 0.0f, fbZ2 = 0.0f;   // op-4 feedback history (its output after envelope and level)
};

} // namespace msynth

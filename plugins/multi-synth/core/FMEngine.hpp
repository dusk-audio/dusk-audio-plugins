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
//   * Phase modulation: a modulator adds `env * (level² · 2π) · sin(...)` radians
//     into its target's phase (square-law level — the classic FM level feel).
//     A carrier contributes `env · level · sin(...)` to the output bus.
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

class FMVoiceEngine
{
public:
    static constexpr int kNumOps = 4;

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
        for (int i = 0; i < kNumOps; ++i)
        {
            updateGain(i);          // vel/keyScale depend on the new note+velocity
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
    void setOpCurve(int i, EnvelopeCurve c) noexcept { op[idx(i)].env.setCurve(c); }

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

        // Distribute op-4 self-feedback into its own phase first (classic
        // 2-sample average damping keeps the loop from screaming).
        if (feedback > 0.0f)
        {
            const float fbDepth = feedback * feedback * kTwoPi; // square-law, up to 2π
            modAccum[A.fbOp] += fbDepth * 0.5f * (fbZ1 + fbZ2);
        }

        float out = 0.0f;

        for (int i = kNumOps - 1; i >= 0; --i)
        {
            Op& o = op[i];
            const float env = o.env.processSample();
            const float s   = std::sin(o.phase * kTwoPi + modAccum[i]);

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
        float modDepth    = 0.0f;   // effLevel² · 2π   (as a modulator)
        float carrierGain = 0.0f;   // effLevel         (as a carrier)
        // state
        float phase = 0.0f;
        ADSREnvelope env;
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

    // Effective level after velocity sensitivity and key-level scaling, then the
    // two output gains derived from it (square-law depth for modulators, linear
    // for carriers).
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
        o.modDepth    = eff * eff * kTwoPi;
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
};

} // namespace msynth

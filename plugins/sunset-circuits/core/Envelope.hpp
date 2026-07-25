// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Envelope.hpp — phase-based ADSR (4 curve shapes) and multi-shape LFO.
//
// Framework-free port of the envelope/LFO classes from the JUCE ModMatrix.h.
// Curves, stage logic, fade-in and retrigger are all carried over verbatim;
// juce::Random is replaced by the SynthCommon xorshift PRNG. Both are
// prepared at the INTERNAL (oversampled) rate so attack/decay/release and LFO
// rate stay correct at every oversampling factor.

#pragma once

#include "SynthCommon.hpp"

namespace msynth
{

enum class EnvelopeCurve
{
    Linear = 0,
    Exponential,   // x^2
    Logarithmic,   // sqrt(x)
    AnalogRC       // 1 - exp(-t/tau), tau = 1/3 -> ~95% at phase 1
};

class ADSREnvelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void prepare(double sampleRate) noexcept { sr = (float)sampleRate; }
    // Change rate WITHOUT resetting the current stage/phase.
    void setSampleRate(double sampleRate) noexcept { sr = (float)sampleRate; }

    void setParameters(float attack, float decay, float sustain, float release) noexcept
    {
        attackTime   = maxf(0.001f, attack);
        decayTime    = maxf(0.001f, decay);
        sustainLevel = clampf(sustain, 0.0f, 1.0f);
        releaseTime  = maxf(0.001f, release);
    }

    void setCurve(EnvelopeCurve c) noexcept { curve = c; }

    void noteOn() noexcept
    {
        stage = Stage::Attack;
        // Seed the attack phase from the CURRENT level (curve inverse) so a
        // retrigger/voice-steal resumes the attack from currentValue instead
        // of snapping back to 0. Without this, stealing a voice mid-sustain
        // produced a render-proven ~0.6 -> 0.0 discontinuity (a click).
        attackPhase = (currentValue > 0.0f) ? curveInverse(currentValue) : 0.0f;
    }
    void noteOff() noexcept
    {
        if (stage != Stage::Idle)
        {
            stage = Stage::Release;
            releaseStartLevel = currentValue;
            releasePhase = 0.0f;
        }
    }

    float processSample() noexcept
    {
        switch (stage)
        {
            case Stage::Idle:
                currentValue = 0.0f;
                break;

            case Stage::Attack:
                attackPhase += 1.0f / (attackTime * sr);
                if (attackPhase >= 1.0f) { attackPhase = 1.0f; stage = Stage::Decay; decayPhase = 0.0f; }
                currentValue = applyCurve(attackPhase);
                break;

            case Stage::Decay:
            {
                decayPhase += 1.0f / (decayTime * sr);
                if (decayPhase >= 1.0f) { decayPhase = 1.0f; stage = Stage::Sustain; }
                const float decayCurve = 1.0f - applyCurve(decayPhase);
                currentValue = sustainLevel + (1.0f - sustainLevel) * decayCurve;
                break;
            }

            case Stage::Sustain:
                currentValue = sustainLevel;
                break;

            case Stage::Release:
            {
                releasePhase += 1.0f / (releaseTime * sr);
                if (releasePhase >= 1.0f) { releasePhase = 1.0f; stage = Stage::Idle; }
                const float relCurve = 1.0f - applyCurve(releasePhase);
                currentValue = releaseStartLevel * relCurve;
                break;
            }
        }
        return currentValue;
    }

    bool isActive() const noexcept    { return stage != Stage::Idle; }
    Stage getStage() const noexcept   { return stage; }
    float getCurrentValue() const noexcept { return currentValue; }
    void reset() noexcept { stage = Stage::Idle; currentValue = 0.0f; attackPhase = decayPhase = releasePhase = 0.0f; }

private:
    float applyCurve(float p) const noexcept
    {
        switch (curve)
        {
            case EnvelopeCurve::Exponential: return p * p;
            case EnvelopeCurve::Logarithmic: return std::sqrt(p);
            case EnvelopeCurve::AnalogRC:
            {
                constexpr float tau = 1.0f / 3.0f;
                // Normalize so the RC curve reaches exactly 1 at p==1 (raw form
                // reaches only 1-e^-3 = 0.9502, causing a ~5% jump into decay).
                constexpr float norm = 1.0f / (1.0f - 0.049787068f); // 0.049787068 = e^-3
                return (1.0f - std::exp(-p / tau)) * norm;
            }
            case EnvelopeCurve::Linear:
            default: return p;
        }
    }

    // Inverse of applyCurve: given a value v in [0,1], return the attack phase
    // p such that applyCurve(p) == v. Each branch inverts the matching forward
    // curve; all branches are monotonically increasing on [0,1] so the inverse
    // is well defined. v is clamped to [0, 0.999] before the log/sqrt to keep
    // AnalogRC's argument strictly positive and avoid a degenerate p at v~=1.
    float curveInverse(float v) const noexcept
    {
        v = clampf(v, 0.0f, 0.999f);
        switch (curve)
        {
            case EnvelopeCurve::Exponential: return std::sqrt(v);          // forward p*p
            case EnvelopeCurve::Logarithmic: return v * v;                 // forward sqrt(p)
            case EnvelopeCurve::AnalogRC:
            {
                constexpr float tau = 1.0f / 3.0f;
                constexpr float oneMinusE3 = 1.0f - 0.049787068f;          // 1 - e^-3
                // forward: v = (1 - exp(-p/tau)) / (1 - e^-3)
                return -tau * std::log(1.0f - v * oneMinusE3);
            }
            case EnvelopeCurve::Linear:
            default: return v;                                             // forward p
        }
    }

    float sr = 44100.0f;
    Stage stage = Stage::Idle;
    float currentValue = 0.0f;
    float attackTime = 0.01f, decayTime = 0.1f, sustainLevel = 0.7f, releaseTime = 0.3f;
    float attackPhase = 0.0f, decayPhase = 0.0f, releasePhase = 0.0f, releaseStartLevel = 0.0f;
    EnvelopeCurve curve = EnvelopeCurve::Exponential;
};

enum class LFOShape { Sine = 0, Triangle, Square, SampleAndHold, RandomSmooth };

// LFO phase source
// ----------------
// FREE-RUN (default): the phase accumulates at rate/sr per sample.
//
// HOST-LOCKED (tempo sync on for this LFO + transport playing + valid host song
// position): the phase is DERIVED from the song position, phase = frac(songBeat
// / beatsPerCycle), exactly the way the arpeggiator derives its step index
// (Arpeggiator::advanceLocked). Scaling the free-run RATE by bpm/120 -- which is
// all tempo sync used to do -- keeps the average speed right but leaves the
// phase wherever the last note-on happened to leave it: the LFO drifts against
// the bar line and no two transport passes over the same bar sound the same.
// Deriving the phase fixes both at once, and a loop wrap needs no handling at
// all (the phase is a pure function of the song position, so a loop whose length
// is a whole number of LFO cycles is continuous straight through the wrap).
//
// Acquiring the lock would STEP the phase, and a step in a modulation signal is
// a click on every route it feeds. It is SLEWED instead: the distance between
// the free-running and the derived phase is captured as an offset at the moment
// of lock -- so the first locked sample continues exactly where the free run
// left off -- and then bled out at no more than the grid's own rate. The
// effective LFO rate therefore stays inside [0, 2x] nominal (it hurries or it
// waits, it never jumps), the correction always finishes within half a cycle,
// and the offset lands on EXACTLY zero, after which the phase is the derived
// value bit for bit -- which is what makes two transport passes reproducible.
//
// Re-seeding at the next cycle boundary instead was the alternative. It does not
// actually remove the step: both clocks run at the same rate, so their phase
// distance is CONSTANT and the boundary is merely where the jump gets moved to.
// It also defers the lock by up to a full cycle -- 20 s at the 0.01 Hz end of
// the rate range.
class LFO
{
public:
    void prepare(double sampleRate) noexcept { sr = (float)sampleRate; phase = 0.0f; lastPhase = 0.0f; }
    void setSampleRate(double sampleRate) noexcept { sr = (float)sampleRate; }

    void setRate(float rateHz) noexcept   { rate = rateHz; }
    void setShape(LFOShape s) noexcept    { shape = s; }
    void setFadeIn(float seconds) noexcept { fadeInTime = maxf(0.0f, seconds); }
    void setTempoSync(bool enabled) noexcept { tempoSync = enabled; }
    // Cycle length in quarter-note beats; only read while host-locked. The engine
    // derives it from the rate knob so the knob keeps its musical meaning at
    // every tempo (see MultiSynthDSP::snapshotParameters).
    void setBeatsPerCycle(float beats) noexcept { beatsPerCycle = maxf(1.0e-4f, beats); }
    void seed(uint32_t s) noexcept { rng.seed(s); }

    // Host song position in beats for the CURRENT host sample, pushed once per
    // host sample by the engine ahead of that sample's oversampled renders.
    // hostLocked is the engine's transport-playing + valid-song-position flag;
    // this LFO locks only if tempo sync is also on for it.
    void setSongBeat(double songBeat, bool hostLocked) noexcept
    {
        if (!(hostLocked && tempoSync))
        {
            // Transport stopped, song position lost, or sync switched off: drop
            // the lock and leave the phase exactly where it is. The free-run
            // accumulator carries on from there, so the release is continuous
            // and needs no slew of its own.
            locked = false;
            syncOffset = 0.0f;
            return;
        }

        const double cycles = songBeat / (double)beatsPerCycle;
        const float derived = (float)(cycles - std::floor(cycles));
        const bool acquiring = !locked;

        if (acquiring)
        {
            // Lock acquired: start from the free-running phase, then slew.
            locked = true;
            syncOffset = wrapSigned(phase - derived);
        }
        else
        {
            // Bleed the offset out at the grid's own rate, so this host sample's
            // phase advance stays in [0, 2x grid] -- never backwards, never more
            // than double speed -- and reaches zero EXACTLY (the <= test, not an
            // asymptotic decay) within half a cycle.
            //
            // A backward jump in the song position (loop wrap or seek) shows up
            // here as a large step, which clears the offset in one sample: a hard
            // re-sync, matching the arpeggiator's loop-wrap behaviour. That is the
            // right call -- the timeline jumped, so the grid jumps with it.
            float step = derived - lastDerived;
            if (step < 0.0f) step += 1.0f;                       // grid wrapped
            if (std::abs(syncOffset) <= step) syncOffset = 0.0f; // locked, exactly
            else syncOffset += (syncOffset > 0.0f) ? -step : step;
        }

        lastDerived = derived;
        phase = derived + syncOffset;   // offset is in [-0.5, 0.5], so one wrap does it
        if (phase >= 1.0f) phase -= 1.0f;
        else if (phase < 0.0f) phase += 1.0f;
        // derived + (phase - derived) can land an ulp under the free-run phase it
        // is meant to reproduce; without this the first locked sample would read
        // as a cycle wrap and re-roll the S&H value.
        if (acquiring) lastPhase = phase;
    }

    void retrigger() noexcept
    {
        // Host-locked, the grid owns the phase: a note-on must not restart it --
        // that is the whole point of syncing to the host -- and a note that
        // starts DURING a lock slew starts already on the grid, because only the
        // voice that was already sounding when the transport rolled has a
        // discontinuity to hide. The per-note fade-in and the random targets
        // restart either way.
        if (locked) { syncOffset = 0.0f; phase = lastDerived; }
        else        { phase = 0.0f; }
        lastPhase = phase;
        fadeInPhase = 0.0f;
        smoothTarget = randomValue();
    }

    float processSample() noexcept
    {
        const float dt = rate / sr;
        // Host-locked: setSongBeat() has already written this host sample's phase
        // and the free-run accumulator is idle, so a cycle wrap is detected
        // against the previous sample instead of the free-run heuristics below
        // (which would fire once per OVERSAMPLED sample, the grid phase being
        // held for the whole host sample).
        const bool wrapped = locked && (phase < lastPhase);
        float raw = 0.0f;

        switch (shape)
        {
            case LFOShape::Sine:
                raw = std::sin(phase * kTwoPi);
                break;
            case LFOShape::Triangle:
                raw = 2.0f * std::abs(2.0f * (phase - std::floor(phase + 0.5f))) - 1.0f;
                break;
            case LFOShape::Square:
                raw = phase < 0.5f ? 1.0f : -1.0f;
                break;
            case LFOShape::SampleAndHold:
                if (locked ? wrapped : (phase < dt)) // wrapped (matches original heuristic)
                    currentSHValue = randomValue();
                raw = currentSHValue;
                break;
            case LFOShape::RandomSmooth:
                if (locked ? wrapped : (phase + dt >= 1.0f))
                    smoothTarget = randomValue();
                currentSmoothValue += (smoothTarget - currentSmoothValue) * dt * 6.0f;
                raw = currentSmoothValue;
                break;
        }

        if (!locked)
        {
            phase += dt;
            if (phase >= 1.0f)
                phase -= 1.0f;
        }
        lastPhase = phase;

        float fadeGain = 1.0f;
        if (fadeInTime > 0.0f && fadeInPhase < 1.0f)
        {
            fadeGain = fadeInPhase;
            fadeInPhase += 1.0f / (fadeInTime * sr);
            if (fadeInPhase > 1.0f) fadeInPhase = 1.0f;
        }

        return raw * fadeGain;
    }

    void reset() noexcept
    {
        phase = 0.0f; lastPhase = 0.0f; fadeInPhase = 1.0f;
        currentSHValue = 0.0f; currentSmoothValue = 0.0f;
        locked = false; syncOffset = 0.0f; lastDerived = 0.0f;
    }

private:
    float randomValue() noexcept { return rng.nextBipolar(); }

    // Signed phase distance in [-0.5, 0.5) — the SHORT way round, so the slew
    // never takes the long path through most of a cycle.
    static float wrapSigned(float d) noexcept { return d - std::floor(d + 0.5f); }

    float sr = 44100.0f;
    float rate = 1.0f;
    float phase = 0.0f;
    float lastPhase = 0.0f;   // previous sample's phase (host-locked wrap detect)
    float fadeInTime = 0.0f;
    float fadeInPhase = 1.0f; // 1 = fully faded in
    bool  tempoSync = false;
    LFOShape shape = LFOShape::Sine;
    float currentSHValue = 0.0f;
    float smoothTarget = 0.0f;
    float currentSmoothValue = 0.0f;
    Xorshift rng;

    // Host-locked (song-position) phase state.
    bool  locked = false;         // sync on AND host transport locked
    float beatsPerCycle = 2.0f;   // cycle length in quarter-note beats
    float syncOffset = 0.0f;      // decaying free->locked continuity offset
    float lastDerived = 0.0f;     // previous host sample's grid phase
};

} // namespace msynth

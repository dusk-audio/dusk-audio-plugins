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
// Deriving the phase fixes both at once, and a loop whose length is a whole
// number of LFO cycles is continuous straight through the wrap for free (the
// phase being a pure function of the song position).
//
// Acquiring the lock would STEP the phase, and a step in a modulation signal is
// a click on every route it feeds. It is SLEWED instead: the distance between
// the free-running and the derived phase is captured as an offset at the moment
// of lock -- so the first locked sample continues exactly where the free run
// left off -- and then bled out, after which the phase is the derived value bit
// for bit, which is what makes two transport passes reproducible.
//
// The bleed rate is the FASTER of the grid's own advance and a wall-clock floor
// of one cycle per kSlewSeconds. The grid-rate term alone would keep the
// effective LFO rate inside [0, 2x] nominal, but on its own it also means a slow
// LFO corrects at its own crawl -- and the correction eats the whole of its
// advance, so it does not just run slow, it STOPS: measured, a 0.1 Hz LFO froze
// for 1069 ms after acquiring the lock, and half a cycle at 0.01 Hz is 50 s of
// it. Above 1/kSlewSeconds = 2 Hz the grid term wins and the [0, 2x] guarantee
// holds. Below it the floor wins and that guarantee is deliberately given up:
// the correction can exceed the LFO's own advance, so for the length of the slew
// the phase moves at up to 1/(kSlewSeconds * rate) times nominal, backwards if
// the correction is negative. Every slew is over inside kSlewSeconds/2 either
// way -- the same 0.1 Hz case measures 20 ms. A moment of hurry beats a stall.
//
// Re-seeding at the next cycle boundary instead was the alternative. It does not
// actually remove the step: both clocks run at the same rate, so their phase
// distance is CONSTANT and the boundary is merely where the jump gets moved to.
// It also defers the lock by up to a full cycle -- 2/0.01 = 200 beats = 100 s at
// 120 BPM at the bottom of the rate range, against 0.25 s worst case here.
//
// Two things DO step the phase, because following the host is the whole point:
// a discontinuity in the song position (loop wrap onto a non-whole number of
// cycles, seek, or a host reporting a block-quantised position), and a change of
// cycle length, which moves a grid whose phase offset grows with the song
// position. The first re-anchors hard and hides the step by crossfading the LFO
// OUTPUT over kSeekFadeSeconds -- modulation is a signal, not an event, so
// unlike the arpeggiator it cannot simply re-fire. The second re-captures the
// slew offset against the new grid and glides onto it.
class LFO
{
public:
    // A slew is over inside kSlewSeconds/2 (worst case, half a cycle out); the
    // seek crossfade is short enough to be inaudible on a modulation signal and
    // long enough to swallow a full-scale step.
    static constexpr float kSlewSeconds = 0.5f;
    static constexpr float kSeekFadeSeconds = 0.005f;

    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        slewStep = 1.0f / (kSlewSeconds * sr);
        seekFadeStep = 1.0f / (kSeekFadeSeconds * sr);
        phase = 0.0f; lastPhase = 0.0f;
        resetSyncState();
    }
    // Rate change WITHOUT resetting musical state (oversampling switch): the
    // per-sample constants move, the lock does not.
    void setSampleRate(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        slewStep = 1.0f / (kSlewSeconds * sr);
        seekFadeStep = 1.0f / (kSeekFadeSeconds * sr);
    }

    void setRate(float rateHz) noexcept   { rate = rateHz; }
    void setShape(LFOShape s) noexcept    { shape = s; }
    void setFadeIn(float seconds) noexcept { fadeInTime = maxf(0.0f, seconds); }
    void setTempoSync(bool enabled) noexcept { tempoSync = enabled; }
    // Cycle length in quarter-note beats; only read while host-locked. The engine
    // derives it from the rate knob so the knob keeps its musical meaning at
    // every tempo (see MultiSynthDSP::snapshotParameters).
    void setBeatsPerCycle(float beats) noexcept
    {
        const float b = maxf(1.0e-4f, beats);
        if (b == beatsPerCycle) return;
        beatsPerCycle = b;
        invBeatsPerCycle = 1.0 / (double)b;
        // frac(songBeat / beatsPerCycle) is DISCONTINUOUS in beatsPerCycle, and
        // by a margin that grows with the song position: 400 beats in, nudging
        // the rate knob 2.00 -> 2.01 Hz moves the grid by a third of a cycle.
        // Writing the new length alone would hard-step the phase every time the
        // rate is automated or a preset lands under sync. Flag it instead and let
        // the next push re-capture the slew offset against the new grid.
        reanchor = true;
    }
    void seed(uint32_t s) noexcept { rng.seed(s); }

    // Host song position in beats for the CURRENT host sample, pushed once per
    // host sample by the engine ahead of that sample's oversampled renders.
    // beatsPerHostSample is the nominal advance between two such pushes -- the
    // yardstick that separates a normal step from a jump in the timeline.
    // hostLocked is the engine's transport-playing + valid-song-position flag;
    // this LFO locks only if tempo sync is also on for it.
    void setSongBeat(double songBeat, double beatsPerHostSample, bool hostLocked) noexcept
    {
        if (!(hostLocked && tempoSync))
        {
            // Transport stopped, song position lost, or sync switched off: drop
            // the lock and leave the phase exactly where it is. The free-run
            // accumulator carries on from there, so the release is continuous
            // and needs no slew of its own.
            locked = false;
            syncOffset = 0.0f;
            reanchor = false;
            return;
        }

        const double cycles = songBeat * invBeatsPerCycle;
        const float derived = (float)(cycles - std::floor(cycles));

        if (!locked || reanchor)
        {
            // Lock acquired, or the cycle length just changed under lock. Both
            // want the same thing: hold the phase where it is, measure the
            // distance to the (new) grid, and slew that away from here.
            locked = true;
            reanchor = false;
            syncOffset = wrapSigned(phase - derived);
        }
        else
        {
            const double dBeats = songBeat - lastSongBeat;
            const double window = 8.0 * beatsPerHostSample;
            if (dBeats < -window || dBeats > window)
            {
                // The timeline itself moved: loop wrap onto a non-whole number of
                // cycles, a seek, or a host whose reported position is quantised
                // coarser than a block. The phase FOLLOWS it -- deriving is the
                // whole point, and re-anchoring hard is what keeps a loop
                // reproducible -- so the offset is dropped and the resulting step,
                // which can be half a cycle, is hidden by crossfading the OUTPUT
                // rather than by refusing to move. The window is in host samples
                // rather than beats so it means the same thing at every tempo and
                // buffer size, and it is generous enough that ordinary rounding
                // between a host's block anchor and our own cursor stays "normal".
                syncOffset = 0.0f;
                seekHold = lastRaw;
                seekFade = 1.0f;
            }
            else
            {
                // Bleed the offset out at the faster of the grid's own advance
                // and the wall-clock floor (slewCredit, accrued per INTERNAL
                // sample in processSample so it is exact at every oversampling
                // factor without this class having to know the factor). It lands
                // on zero EXACTLY -- the <= test, not an asymptotic decay -- after
                // which the phase is the derived value bit for bit.
                float step = (float)(dBeats * invBeatsPerCycle);
                if (step < 0.0f) step = 0.0f;   // sub-window backward rounding
                const float bleed = maxf(step, slewCredit);
                if (std::abs(syncOffset) <= bleed) syncOffset = 0.0f;
                else syncOffset += (syncOffset > 0.0f) ? -bleed : bleed;
            }
        }

        slewCredit = 0.0f;
        lastSongBeat = songBeat;
        lastDerived = derived;
        phase = derived + syncOffset;   // offset is in [-0.5, 0.5], so one wrap does it
        if (phase >= 1.0f) phase -= 1.0f;
        else if (phase < 0.0f) phase += 1.0f;
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
        // held for the whole host sample). Only a move of more than half a cycle
        // counts: a slew running ahead of the grid walks the phase backwards by a
        // hair, and that is not a wrap.
        const bool wrapped = locked && ((lastPhase - phase) > 0.5f);
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
        else if (syncOffset != 0.0f)
        {
            // Accrue the slew's wall-clock budget while a correction is in
            // flight; setSongBeat spends it and zeroes it once per host sample.
            slewCredit += slewStep;
        }
        lastPhase = phase;

        // Seek crossfade: glide from the value held at the jump onto the new
        // trajectory, so a re-anchor is a short ramp rather than a step (the
        // step measures 30x the >12 kHz noise floor, the ramp 0.8x).
        //
        // Smoothstep rather than the bare linear ramp, matching the engine's
        // other fades -- though here it is a tie, not a win: a linear ramp still
        // has a slope corner at each end, but at 5 ms those corners already
        // measure the same 0.81x as smoothstep's, because a corner's spray falls
        // off with the ramp length far faster than a step's. Kept for
        // consistency with modeFade / retireGain at the cost of two multiplies.
        if (seekFade > 0.0f)
        {
            const float b = seekFade * seekFade * (3.0f - 2.0f * seekFade);
            raw += (seekHold - raw) * b;
            seekFade -= seekFadeStep;
            if (seekFade < 0.0f) seekFade = 0.0f;
        }
        lastRaw = raw;

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
        resetSyncState();
    }

private:
    float randomValue() noexcept { return rng.nextBipolar(); }

    // Every host-lock field, in one place: prepare() and reset() clearing
    // different subsets is how a sample-rate change would resume mid-slew
    // against a grid the LFO no longer has the anchor for.
    void resetSyncState() noexcept
    {
        locked = false; reanchor = false;
        syncOffset = 0.0f; slewCredit = 0.0f;
        lastDerived = 0.0f; lastSongBeat = 0.0;
        seekFade = 0.0f; seekHold = 0.0f; lastRaw = 0.0f;
    }

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
    bool   locked = false;         // sync on AND host transport locked
    bool   reanchor = false;       // cycle length changed; re-capture the offset
    float  beatsPerCycle = 2.0f;   // cycle length in quarter-note beats
    double invBeatsPerCycle = 0.5; // reciprocal, so the push path has no divide
    float  syncOffset = 0.0f;      // decaying free->locked continuity offset
    float  slewCredit = 0.0f;      // wall-clock bleed budget, per host sample
    float  slewStep = 0.0f;        // credit accrued per internal sample
    double lastSongBeat = 0.0;     // previous push, for timeline-jump detection
    float  lastDerived = 0.0f;     // previous host sample's grid phase
    float  seekFade = 0.0f;        // 1 -> 0 output crossfade across a jump
    float  seekFadeStep = 0.0f;
    float  seekHold = 0.0f;        // output value held from just before the jump
    float  lastRaw = 0.0f;         // previous sample's (blended) shape output
};

} // namespace msynth

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// AcidEngine.hpp — the "classic acid box" voice + 16-step pattern sequencer.
//
// Framework-free C++17 (zero JUCE/DAF includes). Three classes:
//
//   * AcidFilter     — 3-pole (18 dB/oct) diode-ladder-flavoured resonant lowpass.
//   * AcidVoice      — mono voice: one dedicated band-limited oscillator, a
//                      single fast-decay envelope shared by amp + filter, the
//                      two-rate accent circuit and logarithmic pitch slide.
//   * AcidSequencer  — 16 steps × {on, pitchOffset, accent, slide}, clocked from
//                      bpm + note division + swing + gate length, transposing the
//                      pattern from the held root note. Emits note events.
//
// All three run at the engine's INTERNAL (oversampled) rate — sampleRate is taken
// as given (Phase 3 wires this in as SynthMode::Acid, mono). The per-sample path
// allocates nothing, locks nothing, does no I/O; fixed arrays only.
//
// Trademark rule (hard): the hardware inspiration is described generically as the
// "classic acid box" — no third-party brand or model names appear anywhere.

#pragma once

#include "SynthCommon.hpp"
#include "Oscillator.hpp"
#include "Envelope.hpp"

#include <cstddef>

namespace msynth
{

// =============================================================================
//  AcidFilter — nonlinear TPT 3-pole acid ladder (18 dB/oct)
// =============================================================================
//
// The three TPT integrators are solved with the same zero-delay state expansion
// used by FilterEngine's four-pole models.  An asymmetric diode transfer sits at
// the input and inside every pole.  This keeps the 18 dB slope, makes resonance
// track cutoff cleanly, and produces even as well as odd harmonics when driven.
//
class AcidFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        dcCoeff = 1.0f - (kTwoPi * 5.0f / sr); // ~5 Hz DC blocker
        reset();
    }

    // Rate change for oversampling-factor switches (recomputes constants).
    void setSampleRate(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        dcCoeff = 1.0f - (kTwoPi * 5.0f / sr);
        setParameters(lastCutoff, res, drive);
    }

    void setParameters(float cutoffHz, float resonance, float driveAmount = 1.0f) noexcept
    {
        lastCutoff = cutoffHz;
        const float fc = clampf(cutoffHz, 10.0f, sr * 0.45f);
        const float gw = std::tan(kPi * fc / sr);
        G = gw / (1.0f + gw);
        res = clampf(resonance, 0.0f, 1.0f);
        feedback = kMaxFeedback * res * (0.78f + 0.22f * res);
        drive = maxf(0.1f, driveAmount);
    }

    float process(float input) noexcept
    {
        const float oneMinusG = 1.0f - G;
        const float G2 = G * G;
        const float G3 = G2 * G;
        const float sigma = oneMinusG * (G2 * z[0] + G * z[1] + z[2]);
        float in = (diode(input * drive) - feedback * sigma)
                 / (1.0f + feedback * G3);
        in = diode(in);
        for (int i = 0; i < 3; ++i)
        {
            const float v = G * (in - z[i]);
            float y = diode(z[i] + v);
            z[i] = y + v;
            if (isBad(z[i]))
            {
                z[i] = 0.0f;
                y = 0.0f;
            }
            in = y;
        }

        // Resonance bass compensation and the slight output compression of the
        // original single-transistor VCA/filter output stage.
        const float output = std::tanh((in + input * kBassComp * res) * 1.15f)
                           / 1.15f;

        const float dcOut = output - dcState + dcCoeff * dcPrev;
        dcPrev  = dcOut;
        dcState = output;
        if (isBad(dcPrev)) dcPrev = 0.0f;
        return dcOut;
    }

    void reset() noexcept
    {
        for (auto& st : z) st = 0.0f;
        dcState = 0.0f;
        dcPrev  = 0.0f;
    }

    static constexpr float kMaxFeedback = 8.35f;
    static constexpr float kBassComp    = 0.08f;

private:
    static float diode(float x) noexcept
    {
        // A small polarity-dependent gain mismatch approximates the diode
        // string's asymmetric conduction while retaining unity small-signal gain.
        const float asym = x >= 0.0f ? 1.08f : 0.92f;
        return std::tanh(x * asym) / asym;
    }

    float sr = 44100.0f;
    float G = 0.0f, res = 0.0f, feedback = 0.0f, drive = 1.0f;
    float lastCutoff = 1000.0f;
    float z[3] = {};
    float dcCoeff = 0.999f, dcState = 0.0f, dcPrev = 0.0f;
};

// Dedicated acid oscillator.  It starts with polyBLEP edges but then passes the
// wave through an asymmetric transistor shaper and a fixed high-frequency pole,
// avoiding the mathematically perfect ramps/rectangles of the general oscillator.
class AcidOscillator
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        updateCoeff();
        resetPhase();
    }
    void setSampleRate(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        updateCoeff();
    }
    void setFrequency(float hz) noexcept { dt = clampf(hz / sr, 0.0f, 0.45f); }
    void setWaveform(Waveform w) noexcept
    {
        waveform = (w == Waveform::Square || w == Waveform::Pulse)
            ? Waveform::Square : Waveform::Saw;
    }
    void setPulseWidth(float pw) noexcept { pulseWidth = clampf(pw, 0.42f, 0.58f); }

    float processSample() noexcept
    {
        float raw;
        if (waveform == Waveform::Square)
        {
            raw = phase < pulseWidth ? 1.0f : -1.0f;
            raw += polyBlep(phase, dt);
            raw -= polyBlep(std::fmod(phase + (1.0f - pulseWidth), 1.0f), dt);
            // The square output is slightly softer and narrower than ideal.
            raw = std::tanh((raw - 0.035f) * 1.18f) / 1.18f;
        }
        else
        {
            raw = 2.0f * phase - 1.0f;
            raw -= polyBlep(phase, dt);
            // Gentle curvature/asymmetry characteristic of the charging ramp.
            raw += 0.075f * (raw * raw - 0.3333333f);
            raw = std::tanh(raw * 1.08f) / 1.08f;
        }

        shapeState += (raw - shapeState) * shapeCoeff;
        const float out = shapeState - dcIn + dcCoeff * dcOut;
        dcIn = shapeState;
        dcOut = isBad(out) ? 0.0f : out;

        phase += dt;
        if (phase >= 1.0f) phase -= std::floor(phase);
        return dcOut;
    }

    void resetPhase() noexcept
    {
        phase = 0.0f;
        shapeState = dcIn = dcOut = 0.0f;
    }
    void seedNoise(uint32_t) noexcept {}

private:
    void updateCoeff() noexcept
    {
        // Measured units retain substantial energy above 10 kHz but do not have
        // ideal vertical edges.  Keep this pole independent of oversampling.
        shapeCoeff = 1.0f - std::exp(-kTwoPi * 15500.0f / sr);
        dcCoeff = std::exp(-kTwoPi * 8.0f / sr);
    }

    float sr = 44100.0f;
    float dt = 0.01f, phase = 0.0f, pulseWidth = 0.5f;
    float shapeCoeff = 1.0f, shapeState = 0.0f;
    float dcCoeff = 0.999f, dcIn = 0.0f, dcOut = 0.0f;
    Waveform waveform = Waveform::Saw;
};

// =============================================================================
//  AcidVoice — mono voice
// =============================================================================
//
// One dedicated band-limited oscillator (saw/square) into the
// AcidFilter, gated by a single fast-decay envelope that is REUSED for both the
// amplitude and the filter cutoff modulation (the hallmark of the classic box:
// one envelope makes the whole "wow"). Cutoff modulation follows the same
// exponential rule as the poly voice: cutoff * 2^(env * envMod * kEnvOctaves).
//
// ACCENT circuit ("wow"): an accented note injects a charge into a leaky
// accumulator (accentCharge) that leaks toward zero with a ~180 ms time constant,
// plus a faster VCA strike. Repeated accents recharge rather than sum either bus,
// so the characteristic brightness and level emphasis cannot run away:
//     amp   *= 1 + accentAttack * accentAmount * kAccentAmp
//     envMod*= 1 + accentCharge * kAccentEnv
//     res   += accentCharge * kAccentRes
//
// SLIDE: the played pitch (kept in log2-Hz) glides one-pole toward the target so
// the 10..90 % transition ~= slideTime (configurable 10..200 ms). A slid note
// TIES to the previous one: the envelope and oscillator phase are NOT retriggered
// (no amplitude dip), only the pitch target moves.
//
class AcidVoice
{
public:
    static constexpr float kRelease = 0.010f;  // 10 ms

    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        osc.prepare(sampleRate);
        osc.seedNoise(0xAC1D5EEDu);
        filter.prepare(sampleRate);
        env.prepare(sampleRate);
        env.setCurve(EnvelopeCurve::Exponential);
        updateEnvelope();
        recomputeSlideCoeff();
        accentLeak = std::exp(-1.0f / (kAccentTau * sr));
        accentAttackLeak = std::exp(-1.0f / (kAccentAttackTau * sr));
        reset();
    }

    void setSampleRate(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        osc.setSampleRate(sampleRate);
        filter.setSampleRate(sampleRate);
        env.setSampleRate(sampleRate);
        recomputeSlideCoeff();
        accentLeak = std::exp(-1.0f / (kAccentTau * sr));
        accentAttackLeak = std::exp(-1.0f / (kAccentAttackTau * sr));
    }

    // --- Voice parameters -----------------------------------------------------
    void setWaveform(Waveform w) noexcept   { osc.setWaveform(w); }
    void setPulseWidth(float pw) noexcept   { osc.setPulseWidth(pw); }
    void setCutoff(float hz) noexcept       { cutoffHz = clampf(hz, 20.0f, 20000.0f); }
    void setResonance(float r) noexcept     { resonance = clampf(r, 0.0f, 1.0f); }
    void setDrive(float d) noexcept         { drive = maxf(0.1f, d); }
    void setEnvMod(float m) noexcept        { envMod = clampf(m, 0.0f, 1.0f); }
    void setDecay(float sec) noexcept       { decayTime = clampf(sec, 0.02f, 5.0f); updateEnvelope(); }
    void setSustain(float s) noexcept       { sustainLevel = clampf(s, 0.0f, 1.0f); updateEnvelope(); }
    void setAccentAmount(float a) noexcept  { accentAmount = clampf(a, 0.0f, 1.0f); }
    void setSlideTime(float ms) noexcept    { slideTimeMs = clampf(ms, 10.0f, 200.0f); recomputeSlideCoeff(); }
    void setGain(float g) noexcept          { baseGain = maxf(0.0f, g); }

    // --- Note events ----------------------------------------------------------
    // A slid note glides + ties (no retrigger) ONLY if the voice is already
    // sounding; otherwise it behaves as a fresh note.
    void noteOn(float freqHz, bool accent, bool slide, float velocity = 1.0f) noexcept
    {
        vel = clampf(velocity, 0.0f, 1.0f);
        targetLogFreq = std::log2(maxf(1.0f, freqHz));

        const bool tie = slide && env.isActive();
        if (tie)
        {
            // Glide toward the new pitch; keep envelope + phase running -> no dip.
        }
        else
        {
            curLogFreq = targetLogFreq;   // jump to pitch
            osc.resetPhase();
            env.noteOn();                 // retrigger the (shared) envelope
        }

        if (accent)
        {
            // The accent bus is recharged, not accumulated without bound.
            accentCharge = maxf(accentCharge, accentAmount);
            accentAttack = 1.0f;
        }
    }

    void noteOff() noexcept { env.noteOff(); }

    bool isActive() const noexcept { return env.isActive(); }

    float processSample() noexcept
    {
        const float e = env.processSample();          // shared env, 0..1

        // Slide: one-pole glide of the log-frequency toward the target.
        curLogFreq += (targetLogFreq - curLogFreq) * slideCoeff;
        osc.setFrequency(std::exp2(curLogFreq));
        const float oscOut = osc.processSample();

        // Two-rate accent contour: a fast VCA strike plus the slower filter
        // "wow" discharge.  Repeated accents recharge the circuit naturally.
        accentCharge *= accentLeak;
        accentAttack *= accentAttackLeak;
        if (accentCharge < 1.0e-6f) accentCharge = 0.0f;
        if (accentAttack < 1.0e-6f) accentAttack = 0.0f;

        // Filter env-mod (brighter with accent), exponential cutoff sweep.
        const float envModEff = envMod * (1.0f + accentCharge * kAccentEnv);
        float cut = cutoffHz * std::exp2(e * envModEff * kEnvOctaves);
        // Conservative pre-limit below AcidFilter's sr * 0.45f TPT ceiling.
        cut = clampf(cut, 20.0f, sr * 0.40f);
        const float resEff = clampf(resonance + accentCharge * kAccentRes, 0.0f, 1.0f);
        filter.setParameters(cut, resEff, drive);

        const float filtered = filter.process(oscOut);

        // Nonlinear VCA: accent hits quickly, while filter brightness decays on
        // the slower bus.  The tanh knee is level-dependent rather than a clean
        // multiplication after the filter.
        const float amp = e * baseGain * vel
                        * (1.0f + accentAttack * accentAmount * kAccentAmp);
        const float vcaDrive = 1.0f + 0.65f * accentAttack;
        float out = std::tanh(filtered * vcaDrive) / vcaDrive * amp;
        if (isBad(out)) out = 0.0f;
        return out;
    }

    void reset() noexcept
    {
        env.reset();
        filter.reset();
        osc.resetPhase();
        accentCharge = 0.0f;
        accentAttack = 0.0f;
        curLogFreq = targetLogFreq = std::log2(440.0f);
    }

    // Accent/env tuning constants (documented in the class header).
    static constexpr float kEnvOctaves = 4.0f;   // full env * envMod -> 4-octave sweep
    static constexpr float kAccentAmp  = 1.30f;
    static constexpr float kAccentEnv  = 0.85f;
    static constexpr float kAccentRes  = 0.18f;
    static constexpr float kAccentTau  = 0.180f;
    static constexpr float kAccentAttackTau = 0.045f;

private:
    void updateEnvelope() noexcept
    {
        // Fast attack (~3 ms), decay = param, sustain = param, fast release.
        env.setParameters(kAttack, decayTime, sustainLevel, kRelease);
    }

    void recomputeSlideCoeff() noexcept
    {
        // One-pole coefficient chosen so the 10..90 % transition ~= slideTime.
        // For y += (t-y)*a, the 10..90 % time is ln(9)*tau where tau = -1/ln(1-a).
        const float t1090 = clampf(slideTimeMs, 10.0f, 200.0f) * 0.001f;
        const float tau = t1090 / 2.19722458f;           // ln(9) = 2.19722458
        slideCoeff = 1.0f - std::exp(-1.0f / (tau * sr));
        slideCoeff = clampf(slideCoeff, 1.0e-5f, 1.0f);
    }

    static constexpr float kAttack  = 0.003f;  // 3 ms

    float sr = 44100.0f;

    AcidOscillator osc;
    AcidFilter    filter;
    ADSREnvelope  env;

    // Parameters.
    float cutoffHz     = 500.0f;
    float resonance    = 0.5f;
    float drive        = 1.0f;
    float envMod       = 0.5f;
    float decayTime    = 0.3f;
    float sustainLevel = 0.0f;   // classic pluck: full decay to silence
    float accentAmount = 0.7f;
    float slideTimeMs  = 60.0f;
    float baseGain     = 0.7f;

    // State.
    float vel           = 1.0f;
    float accentCharge  = 0.0f;
    float accentAttack  = 0.0f;
    float accentLeak    = 0.999f;
    float accentAttackLeak = 0.999f;
    float curLogFreq    = 8.78f;  // log2(440)
    float targetLogFreq = 8.78f;
    float slideCoeff    = 0.05f;
};

// =============================================================================
//  AcidSequencer — 16-step pattern sequencer
// =============================================================================
//
// 16 steps, each {on, pitchOffset (-24..+24 st), accent, slide}. Clocked
// per-sample from the host tempo + note division + swing + gate length, using
// the SAME conventions as Arpeggiator.hpp (getBeatsPerStep, odd-step swing,
// gate-length note-off). The player holds ONE root note; the pattern transposes
// from it. Latch keeps the pattern running after the key is released.
//
// advanceSample() returns the note event for this sample (allocation-free); the
// caller applies it to an AcidVoice (or the integration shell). A slid step ties
// to the previous note: the sequencer flags the noteOn as a slide and suppresses
// the previous note's gate-end note-off, so the AcidVoice glides without a gap.
//
class AcidSequencer
{
public:
    struct Step
    {
        bool on     = true;
        int  pitch  = 0;      // semitone offset from the held root, -24..+24
        bool accent = false;
        bool slide  = false;
    };

    struct Event
    {
        bool  noteOn  = false;
        float freq    = 0.0f;
        bool  accent  = false;
        bool  slide   = false;
        bool  noteOff = false;
    };

    void prepare(double sampleRate) noexcept { sr = sampleRate; reset(); }

    // --- Configuration --------------------------------------------------------
    void setEnabled(bool on) noexcept { if (on != enabled) { enabled = on; if (!on) reset(); } }
    bool isEnabled() const noexcept   { return enabled; }

    void setRate(ArpRateDivision r) noexcept { rateDivision = r; }
    void setGate(float g) noexcept           { gateLength = clampf(g, 0.01f, 1.0f); }
    void setSwing(float s) noexcept          { swing = clampf(s, 0.0f, 1.0f); }
    void setLatch(bool on) noexcept          { latch = on; }

    // Per-step setters (Phase 3 wires 16×4 params to these).
    void setStep(int i, bool on, int pitch, bool accent, bool slide) noexcept
    {
        if (i < 0 || i >= 16) return;
        steps[(size_t)i] = { on, clampi(pitch, -24, 24), accent, slide };
    }
    void setStepOn(int i, bool on) noexcept        { if (i >= 0 && i < 16) steps[(size_t)i].on = on; }
    void setStepPitch(int i, int pitch) noexcept   { if (i >= 0 && i < 16) steps[(size_t)i].pitch = clampi(pitch, -24, 24); }
    void setStepAccent(int i, bool a) noexcept     { if (i >= 0 && i < 16) steps[(size_t)i].accent = a; }
    void setStepSlide(int i, bool s) noexcept      { if (i >= 0 && i < 16) steps[(size_t)i].slide = s; }
    const Step& getStep(int i) const noexcept      { return steps[(size_t)clampi(i, 0, 15)]; }

    int  getCurrentStep() const noexcept { return currentStep; }

    // --- Held root note (mono; last note wins) --------------------------------
    void noteOn(int noteNumber) noexcept
    {
        rootNote = clampi(noteNumber, 0, 127);
        held = true;
    }
    // Release `noteNumber`. `nextRoot` is the note that should take the root over
    // (the caller's next-newest key still down) or -1 for "nothing left".
    //
    // Releasing a note that is not the root is a no-op, as it always was. Releasing
    // the ROOT used to stop the pattern outright, which is wrong the moment more
    // than one key is down: hold C, hold E (root moves to E), release E and the
    // sequencer stopped with C still held. The root is a last-note-priority stack
    // position, so a release unwinds to the previous entry and only the last one
    // out stops the pattern. Latch still outranks everything -- it is what KEEPS
    // held true across a key-up, so a latched pattern ignores releases entirely
    // and is cleared by clearLatch() / retainHeld() instead.
    void noteOff(int noteNumber, int nextRoot = -1) noexcept
    {
        if (noteNumber != rootNote || latch) return;
        if (nextRoot >= 0 && nextRoot < 128) rootNote = nextRoot;  // held stays true
        else                                 held = false;
    }
    void clearLatch() noexcept { if (latch) held = false; }
    // Latch released: drop the root unless its key is physically down (engine
    // heldNotes mask) — mirrors Arpeggiator::retainHeld.
    void retainHeld(uint64_t maskLo, uint64_t maskHi) noexcept
    {
        const int n = rootNote;
        const bool down = n >= 0 && n < 128
            && ((((n < 64) ? maskLo : maskHi) >> (n & 63)) & 1ull) != 0;
        if (!down) held = false;
    }

    // Advance one sample; returns the note event (if any) for this sample.
    //
    // hostLocked = the DAW transport is playing AND a valid song position is
    // available. When locked the step clock is derived STATELESSLY from songBeat
    // (song position in beats) so steps land on the absolute host grid and
    // re-sync on loop-wrap; when not locked the free-run counter path (unchanged)
    // is used. Conventions match Arpeggiator.hpp.
    Event advanceSample(double bpm, bool transportPlaying,
                        double songBeat, bool hostLocked) noexcept
    {
        Event ev;

        // Runs while a root note is HELD. `latch` is not an alternative to held:
        // it is what KEEPS held true across a key-up (noteOff/reset both preserve
        // it while latched), so testing it here made the pattern unstoppable —
        // clearLatch() and retainHeld() clear `held`, and the old `|| latch` term
        // then ran the sequencer anyway, from the default C3 root, straight through
        // an All Notes Off (measured -16.6 dB still playing 1.5 s after the panic).
        if (!enabled || !held)
        {
            if (notePlaying) { ev.noteOff = true; notePlaying = false; }
            currentStep = 0;
            sampleCounter = 0;
            return ev;
        }

        // Clean clock switch: kill any sounding note and reset BOTH clocks.
        if (hostLocked != wasLocked)
        {
            wasLocked = hostLocked;
            sampleCounter = 0;
            currentStep = 0;
            lockedInited = false;
            lastFiredGlobalStep = kNoStep;
            prevSongBeat = -1.0e18;
            if (notePlaying) { ev.noteOff = true; notePlaying = false; }
            return ev;
        }

        if (hostLocked) return advanceLocked(songBeat);
        return advanceFree(bpm, transportPlaying);
    }

    void reset() noexcept
    {
        currentStep = 0;
        sampleCounter = 0;
        notePlaying = false;
        wasLocked = false;
        lockedInited = false;
        lastFiredGlobalStep = kNoStep;
        prevSongBeat = -1.0e18;
        if (!latch) held = false;
    }

private:
    // Sentinel for "no step fired yet"; every real global step index is >= 0.
    static constexpr long long kNoStep = -1;

    // ------------------------------------------------------------------ free-run
    Event advanceFree(double bpm, bool transportPlaying) noexcept
    {
        Event ev;

        // C7: free-run tempo no longer requires the transport to be playing so a
        // stopped-transport audition steps at the host BPM. 120 remains only the
        // fallback for an invalid (<=0) BPM.
        (void)transportPlaying;
        const double effBpm = (bpm > 0.0) ? bpm : 120.0;
        const double samplesPerStep = sr * 60.0 / effBpm * getBeatsPerStep(rateDivision);

        ++sampleCounter;

        // Symmetric swing: the even step (leading into the offbeat) lengthens and
        // the odd step shortens by the same amount, so each pair still lasts two
        // grid steps and downbeats stay on the tempo grid.
        double effStep = samplesPerStep;
        if (swing > 0.0f)
            effStep *= (currentStep % 2 == 0) ? (1.0 + (double)swing * 0.5)
                                              : (1.0 - (double)swing * 0.5);
        const double gateSamples = effStep * (double)gateLength;

        if (sampleCounter == 1)
        {
            const Step& st = steps[(size_t)currentStep];
            if (st.on)
            {
                ev.noteOn = true;
                ev.freq   = midiToHz((float)clampi(rootNote + st.pitch, 0, 127));
                ev.accent = st.accent;
                ev.slide  = st.slide && notePlaying; // tie only if a note is sounding
                notePlaying = true;
            }
            else if (notePlaying)
            {
                ev.noteOff = true;
                notePlaying = false;
            }
        }

        // Gate-end note-off, UNLESS the next active step slides (tie -> no gap).
        if ((double)sampleCounter >= gateSamples && notePlaying && !ev.noteOn)
        {
            const Step& next = steps[(size_t)((currentStep + 1) % 16)];
            const bool nextSlide = next.on && next.slide;
            if (!nextSlide)
            {
                ev.noteOff = true;
                notePlaying = false;
            }
        }

        if ((double)sampleCounter >= effStep)
        {
            sampleCounter = 0;
            if (++currentStep >= 16) currentStep = 0;
        }

        return ev;
    }

    // -------------------------------------------------------------- host-locked
    // Stateless grid clock (16 steps). Step k spans [k*bps, (k+1)*bps) in song
    // beats; swing delays the odd-step onset and shortens its span, matching the
    // free-run swing intervals while pinning even onsets to the host grid. Fires
    // each not-yet-fired step once; a backward jump (loop wrap) re-arms the grid.
    Event advanceLocked(double songBeat) noexcept
    {
        Event ev;

        // Loop-wrap: song position regressed -> let the grid re-fire from here.
        if (songBeat < prevSongBeat - 1.0e-9) lastFiredGlobalStep = kNoStep;
        prevSongBeat = songBeat;

        if (songBeat < 0.0)
        {
            if (notePlaying) { ev.noteOff = true; notePlaying = false; }
            return ev;
        }

        const double bps = getBeatsPerStep(rateDivision);
        const long long globalStep = (long long)std::floor(songBeat / bps);

        // Quantized start: mid-step key-press waits for the next boundary.
        if (!lockedInited) { lastFiredGlobalStep = globalStep; lockedInited = true; }

        if (globalStep != lastFiredGlobalStep)
        {
            const double swingOff  = (globalStep & 1) ? (double)swing * 0.5 * bps : 0.0;
            const double onsetBeat = (double)globalStep * bps + swingOff;
            if (songBeat >= onsetBeat)
            {
                // C8: a forward seek can land past this step's own gate-off beat;
                // firing the note-on there produces a blip. Skip the note-on in
                // that case, but mark the step fired either way so the grid moves on.
                const double durB = bps * ((globalStep & 1) ? (1.0 - (double)swing * 0.5)
                                                            : (1.0 + (double)swing * 0.5));
                const bool pastGate = songBeat >= onsetBeat + (double)gateLength * durB;

                const int idx = (int)(globalStep % 16);
                currentStep = idx;
                const Step& st = steps[(size_t)idx];
                if (st.on && !pastGate)
                {
                    ev.noteOn = true;
                    ev.freq   = midiToHz((float)clampi(rootNote + st.pitch, 0, 127));
                    ev.accent = st.accent;
                    ev.slide  = st.slide && notePlaying; // tie only if a note is sounding
                    notePlaying = true;
                }
                else if (notePlaying)
                {
                    ev.noteOff = true;
                    notePlaying = false;
                }
                lastFiredGlobalStep = globalStep;
            }
        }

        // Gate-end note-off (for lastFiredGlobalStep), UNLESS the next active step
        // slides (tie -> no gap), matching the free-run convention.
        if (notePlaying && !ev.noteOn && lastFiredGlobalStep >= 0)
        {
            const long long g = lastFiredGlobalStep;
            const double swOff = (g & 1) ? (double)swing * 0.5 * bps : 0.0;
            const double onB   = (double)g * bps + swOff;
            const double durB  = bps * ((g & 1) ? (1.0 - (double)swing * 0.5)
                                                : (1.0 + (double)swing * 0.5));
            if (songBeat >= onB + (double)gateLength * durB)
            {
                const Step& next = steps[(size_t)((g + 1) % 16)];
                const bool nextSlide = next.on && next.slide;
                if (!nextSlide)
                {
                    ev.noteOff = true;
                    notePlaying = false;
                }
            }
        }

        return ev;
    }

    double sr = 44100.0;
    bool   enabled = false;
    bool   latch   = false;
    bool   held    = false;
    int    rootNote = 48; // C3

    ArpRateDivision rateDivision = ArpRateDivision::Sixteenth;
    float  gateLength = 0.5f;
    float  swing      = 0.0f;

    Step steps[16] {};

    int       currentStep = 0;
    long long sampleCounter = 0;
    bool      notePlaying = false;

    // Host-locked (song-position) clock state.
    bool      wasLocked = false;
    bool      lockedInited = false;
    long long lastFiredGlobalStep = kNoStep;
    double    prevSongBeat = -1.0e18;
};

} // namespace msynth

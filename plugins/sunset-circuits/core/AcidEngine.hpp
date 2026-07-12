// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// AcidEngine.hpp — the "classic acid box" voice + 16-step pattern sequencer.
//
// Framework-free C++17 (zero JUCE/DPF includes). Three classes:
//
//   * AcidFilter     — 3-pole (18 dB/oct) diode-ladder-flavoured resonant lowpass.
//   * AcidVoice      — mono voice: one polyBLEP oscillator, a single fast-decay
//                      envelope shared by amp + filter, the accent "wow" circuit
//                      and exponential note-to-note slide (glide).
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

namespace msynth
{

// =============================================================================
//  AcidFilter — 3-pole diode-ladder-flavoured resonant lowpass (18 dB/oct)
// =============================================================================
//
// Topology mirrors FourPoleOTA (FilterEngine.hpp) but with THREE one-pole stages
// instead of four, and a diode-style (tanh-clipped) resonance feedback path:
//
//   * Each stage is the same naive zero-delay one-pole used by the OTA/ladder
//     models: y = s + g*(in - s), s = tanh(y), with the exact bilinear coefficient
//     g = gw/(1+gw) where gw = tan(pi*fc/sr) (always < 1 -> stable at the ceiling).
//     Three of them cascade to give the canonical 18 dB/oct slope. tanh(y) keeps each
//     stage output bounded to +-1 and gives unity small-signal gain (so at res 0
//     the response is a clean linear 3-pole rolloff — see slope_gate).
//   * Resonance feeds the third stage's output back to the input through a tanh
//     "diode" clipper: fb = tanh(s2 * feedback). Because a pure 3-pole cascade
//     only reaches 135 deg at cutoff, self-oscillation needs a generous feedback
//     gain (the 180 deg point sits above cutoff where each pole gives ~60 deg,
//     |pole| = 0.5, so loop gain 1 needs feedback ~= 8). kMaxFeedback = 8 puts
//     res ~0.95 right at the edge of self-oscillation — the "scream" — while the
//     per-stage tanh caps every state at +-1, so the loop can never blow up
//     (bounded, finite; see scream_gate).
//   * Input drive is a tanh waveshaper (classic acid "overdrive into the filter").
//   * A one-pole DC blocker sits on the output; NaN/Inf guards reset dead state.
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
        const float fc = clampf(cutoffHz, 10.0f, sr * 0.40f); // 0.4x-rate ceiling (see FourPoleOTA)
        // Exact bilinear one-pole coefficient: gw = tan(pi*fc/sr), g = gw/(1+gw).
        // g is always < 1 so the small-signal pole stays stable even at the 0.4x
        // ceiling (a naive g = tan(...) reaches ~3.08 there -> |1-g| > 1).
        const float gw = std::tan(kPi * fc / sr);
        g = gw / (1.0f + gw);
        res = clampf(resonance, 0.0f, 1.0f);
        feedback = res * kMaxFeedback;
        drive = maxf(0.1f, driveAmount);
    }

    float process(float input) noexcept
    {
        // Diode-style resonance feedback from the last stage.
        const float fb = std::tanh(s[2] * feedback);
        // Input drive (tanh overdrive) minus the resonance feedback.
        float in = std::tanh(input * drive) - fb;

        for (int i = 0; i < 3; ++i)
        {
            const float y = s[i] + g * (in - s[i]);
            s[i] = std::tanh(y);      // bounded, unity small-signal gain
            in = s[i];
        }

        for (auto& st : s)
            if (isBad(st)) st = 0.0f;

        // Resonance loses low end; add a touch back for body (bass compensation).
        const float output = s[2] + input * kBassComp * res;

        // One-pole DC blocker.
        const float dcOut = output - dcState + dcCoeff * dcPrev;
        dcPrev  = dcOut;
        dcState = output;
        if (isBad(dcPrev)) dcPrev = 0.0f;
        return dcOut;
    }

    void reset() noexcept
    {
        for (auto& st : s) st = 0.0f;
        dcState = 0.0f;
        dcPrev  = 0.0f;
    }

    // Screaming near-self-oscillation without blowing up: see the header comment.
    static constexpr float kMaxFeedback = 8.0f;
    static constexpr float kBassComp    = 0.10f;

private:
    float sr = 44100.0f;
    float g = 0.0f, res = 0.0f, feedback = 0.0f, drive = 1.0f;
    float lastCutoff = 1000.0f;
    float s[3] = {};
    float dcCoeff = 0.999f, dcState = 0.0f, dcPrev = 0.0f;
};

// =============================================================================
//  AcidVoice — mono voice
// =============================================================================
//
// One polyBLEP oscillator (saw/square canonical, any waveform allowed) into the
// AcidFilter, gated by a single fast-decay envelope that is REUSED for both the
// amplitude and the filter cutoff modulation (the hallmark of the classic box:
// one envelope makes the whole "wow"). Cutoff modulation follows the same
// exponential rule as the poly voice: cutoff * 2^(env * envMod * kEnvOctaves).
//
// ACCENT circuit ("wow"): an accented note injects a charge into a leaky
// accumulator (accentCharge) that leaks toward zero with a ~50 ms time constant.
// Because the leak is slow relative to fast (e.g. 1/16) step rates, a run of
// accented steps STACKS the charge -> brightness and level grow across the run,
// then relax after it -> the characteristic accent "wow". The charge is clamped
// (kAccentMax) so it can never run away. Each sample the charge drives three
// things, exactly like the hardware accent bus:
//     amp   *= 1 + accentCharge * kAccentAmp     (louder)
//     envMod*= 1 + accentCharge * kAccentEnv     (brighter — deeper filter sweep)
//     res   += accentCharge * kAccentRes         (more resonant "kick")
//
// SLIDE: the played pitch (kept in log2-Hz) glides one-pole toward the target so
// the 10..90 % transition ~= slideTime (configurable 10..200 ms). A slid note
// TIES to the previous one: the envelope and oscillator phase are NOT retriggered
// (no amplitude dip), only the pitch target moves.
//
class AcidVoice
{
public:
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
            accentCharge = clampf(accentCharge + accentAmount, 0.0f, kAccentMax);
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

        // Accent charge leaks toward zero (the "wow").
        accentCharge *= accentLeak;
        if (accentCharge < 1.0e-6f) accentCharge = 0.0f;

        // Filter env-mod (brighter with accent), exponential cutoff sweep.
        const float envModEff = envMod * (1.0f + accentCharge * kAccentEnv);
        float cut = cutoffHz * std::exp2(e * envModEff * kEnvOctaves);
        cut = clampf(cut, 20.0f, sr * 0.40f); // 0.4x-rate ceiling (see FourPoleOTA)
        const float resEff = clampf(resonance + accentCharge * kAccentRes, 0.0f, 0.99f);
        filter.setParameters(cut, resEff, drive);

        const float filtered = filter.process(oscOut);

        // Amplitude: env * base gain * velocity * accent boost.
        const float amp = e * baseGain * vel * (1.0f + accentCharge * kAccentAmp);
        float out = filtered * amp;
        if (isBad(out)) out = 0.0f;
        return out;
    }

    void reset() noexcept
    {
        env.reset();
        filter.reset();
        osc.resetPhase();
        accentCharge = 0.0f;
        curLogFreq = targetLogFreq = std::log2(440.0f);
    }

    // Accent/env tuning constants (documented in the class header).
    static constexpr float kEnvOctaves = 4.0f;   // full env * envMod -> 4-octave sweep
    static constexpr float kAccentAmp  = 1.2f;   // amp boost per unit charge
    static constexpr float kAccentEnv  = 1.0f;   // env-depth (brightness) boost
    static constexpr float kAccentRes  = 0.25f;  // resonance kick
    static constexpr float kAccentMax  = 1.5f;   // charge clamp (no runaway)
    static constexpr float kAccentTau  = 0.050f; // 50 ms leak

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
    static constexpr float kRelease = 0.010f;  // 10 ms

    float sr = 44100.0f;

    Oscillator    osc;
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
    float accentLeak    = 0.999f;
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
    void noteOff(int noteNumber) noexcept
    {
        if (noteNumber == rootNote && !latch)
            held = false;
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

        if (!enabled || (!held && !latch))
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

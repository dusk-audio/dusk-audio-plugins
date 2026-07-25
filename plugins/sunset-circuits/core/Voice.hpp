// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Voice.hpp — SynthVoice + VoiceAllocator.
//
// Framework-free port of the JUCE MultiSynthVoice.h with the mandatory fixes
// folded in:
//   #2 pitch bend + master tune always shift the oscillator base frequency;
//   #3 REAL unison — each unison sub-voice has its own detuned oscillator set,
//      spread with constant-power panning; polyphony auto-reduces so
//      poly x unison <= 16 (handled in VoiceAllocator);
//   #4 Cross Mod wired — osc2 (1-sample-delayed) modulates osc1 frequency;
//   #5 LFO1Rate / LFO2Rate / UnisonDetune mod destinations consumed here
//      (EffectsMix is consumed at engine level);
//   #10 member xorshift PRNG (drift / per-note random), NaN guards kept.
//
// The oscillator section is a plain per-mode switch. Modes 0-3 use the analog
// oscillator bank; mode 4 (Prism) renders through a per-unison-sub-voice
// FMVoiceEngine bank (FMEngine.hpp); mode 5 (Acid) is rendered by the engine's
// dedicated mono AcidEngine path in MultiSynthDSP. Poly voices are not rendered
// at all in Acid mode (note-off short-circuits to acidNoteOff, and a mode switch
// only commits with the voice path muted, where it resets the poly voices), so the
// silentMode guard is defensive only.
//
// Prepared at the INTERNAL (oversampled) rate; renderInternalSample() is called
// osFactor times per host sample by the engine, then decimated.

#pragma once

#include "Oscillator.hpp"
#include "FilterEngine.hpp"
#include "Envelope.hpp"
#include "ModMatrix.hpp"
#include "FMEngine.hpp"        // Prism mode (mode 4): 4-op FM osc section
#include "DuskSmoothed.hpp"

namespace msynth
{

enum class SynthMode
{
    Cosmos = 0, // 6-voice DCO poly
    Oracle,     // 5-voice poly, poly-mod
    Mono,       // mono, sub + ring + sync
    Modular,    // duophonic, 3 osc + S&H + spring
    Prism,      // 4-op FM (FMEngine.hpp, one op bank per unison sub-voice)
    Acid        // acid bass + sequencer (AcidEngine.hpp, engine-level mono path)
};

static constexpr int kMaxPolyphony   = 8;
static constexpr int kMaxUnison      = 8;
static constexpr int kMaxOscVoices   = 16; // poly x unison ceiling

// Fade-out length for a voice that falls outside the effective polyphony while
// still sounding (SynthVoice::retire). Long enough that the fade itself is
// inaudible, short enough that an over-budget voice cannot linger: at 15 ms the
// worst case is one extra voice-render's worth of CPU for under a millisecond of
// music, and the freed slot is back in the allocator's hands almost immediately.
static constexpr float kRetireSeconds = 0.015f;

// Per-voice parameters (shared across voices for a given mode). Set once per
// block by the engine from atomics — never looked up in the render path.
struct VoiceParameters
{
    SynthMode mode = SynthMode::Cosmos;

    Waveform osc1Wave = Waveform::Saw;
    float osc1Detune = 0.0f, osc1PulseWidth = 0.5f, osc1Level = 1.0f;

    Waveform osc2Wave = Waveform::Saw;
    float osc2Detune = 0.0f, osc2PulseWidth = 0.5f, osc2Level = 0.8f;
    int   osc2SemiOffset = 0;

    Waveform osc3Wave = Waveform::Saw;
    float osc3Level = 0.5f;

    float subLevel = 0.5f;
    Waveform subWave = Waveform::Square;

    float noiseLevel = 0.0f;
    float shRate = 5.0f;

    float filterCutoff = 8000.0f, filterResonance = 0.3f, filterHPCutoff = 20.0f;
    float filterEnvAmount = 0.5f;

    float ampAttack = 0.01f, ampDecay = 0.2f, ampSustain = 0.8f, ampRelease = 0.3f;
    EnvelopeCurve ampCurve = EnvelopeCurve::Exponential;
    float filtAttack = 0.01f, filtDecay = 0.3f, filtSustain = 0.4f, filtRelease = 0.5f;
    EnvelopeCurve filtCurve = EnvelopeCurve::Exponential;

    float crossMod = 0.0f;   // fix #4 — osc2 -> osc1 frequency
    float ringMod = 0.0f;
    bool  hardSync = false;
    float fmAmount = 0.0f;

    // Prism (mode 4) 4-op FM. algo 0..7, feedback 0..1; op[] carries the 4
    // operators' {ratio, fine cents, level, velSens, keyScale, ADSR}.
    int   prismAlgo = 4;
    float prismFB = 0.0f;
    struct FMOpParams
    {
        float ratio = 1.0f, fine = 0.0f, level = 0.0f, vel = 0.0f, keyScale = 0.0f;
        float a = 0.005f, d = 0.4f, s = 0.7f, r = 0.4f;
    };
    FMOpParams op[FMVoiceEngine::kNumOps];

    float polyModFEnvOscA = 0.0f, polyModFEnvFilt = 0.0f, polyModOscBOscA = 0.0f, polyModOscBPWM = 0.0f;

    float portamentoTime = 0.0f;
    bool  legatoMode = false;
    int   glideMode = 0;

    float analogAmount = 0.0f;
    float velocitySensitivity = 0.7f;
    int   velocityCurve = 0;

    // Unison (fix #3).
    int   unisonVoices = 1;
    float unisonDetune = 10.0f;  // cents (max spread)
    float unisonSpread = 1.0f;

    // Global pitch (fix #2) — set per block from bend/tune.
    float pitchBendSemis = 0.0f;   // applied to the oscillator base frequency
    float pitchBendNorm  = 0.0f;   // normalized ±1 for the mod matrix source (C2)
    float masterTuneSemis = 0.0f;

    // Performance controllers (mod sources).
    float modWheel = 0.0f, aftertouch = 0.0f;
};

//==============================================================================
class SynthVoice
{
public:
    void prepare(double sampleRate, uint32_t seed) noexcept
    {
        sr = sampleRate;
        rng.seed(seed);
        for (int u = 0; u < kMaxUnison; ++u)
        {
            osc[u].osc1.prepare(sampleRate); osc[u].osc1.seedNoise(seed + 11u * (uint32_t)u + 1u);
            osc[u].osc2.prepare(sampleRate); osc[u].osc2.seedNoise(seed + 11u * (uint32_t)u + 2u);
            osc[u].osc3.prepare(sampleRate); osc[u].osc3.seedNoise(seed + 11u * (uint32_t)u + 3u);
            osc[u].sub.prepare(sampleRate);  osc[u].sub.seedNoise(seed + 11u * (uint32_t)u + 4u);
            lastOsc2[u] = 0.0f;
        }
        for (int u = 0; u < kMaxUnison; ++u)
            fm[u].prepare(sampleRate);
        pinkNoise.seed(seed + 77u);
        filterL.prepare(sampleRate);
        filterR.prepare(sampleRate);
        ampEnv.prepare(sampleRate);
        filtEnv.prepare(sampleRate);
        lfo1.prepare(sampleRate); lfo1.seed(seed + 101u);
        lfo2.prepare(sampleRate); lfo2.seed(seed + 202u);
        sampleAndHold.prepare(sampleRate);

        driftCounter = rng.nextInt(200) + 100;
        driftTarget = rng.nextBipolar();
        driftSmooth = 0.0f;
        filterTrackingOffset = (rng.nextFloat() - 0.5f) * 0.04f;
        voicePanOffset = (rng.nextFloat() - 0.5f) * 0.3f;
        portaFreq.prepare(sampleRate, 0.1f);
        portaFreq.snap(440.0f);
        retireStep = 1.0f / (float)(kRetireSeconds * sampleRate);
        retiring = false; retirePhase = 1.0f;
    }

    // Change the internal (oversampled) rate WITHOUT resetting musical state:
    // oscillator phases, envelope stages, portamento value and active status
    // are all preserved so an oversampling-factor switch never changes pitch or
    // cuts a held note (the filters reset — a brief, inaudible transient).
    void setSampleRate(double sampleRate) noexcept
    {
        sr = sampleRate;
        for (int u = 0; u < kMaxUnison; ++u)
        {
            osc[u].osc1.setSampleRate(sampleRate);
            osc[u].osc2.setSampleRate(sampleRate);
            osc[u].osc3.setSampleRate(sampleRate);
            osc[u].sub.setSampleRate(sampleRate);
            fm[u].setSampleRate(sampleRate); // preserves the held FM note
        }
        filterL.setSampleRate(sampleRate);
        filterR.setSampleRate(sampleRate);
        ampEnv.setSampleRate(sampleRate);
        filtEnv.setSampleRate(sampleRate);
        lfo1.setSampleRate(sampleRate);
        lfo2.setSampleRate(sampleRate);
        sampleAndHold.setSampleRate(sampleRate);
        portaFreq.prepare(sampleRate, 0.1f); // updates coeff; keeps current/target value
        retireStep = 1.0f / (float)(kRetireSeconds * sampleRate); // keeps retirePhase
    }

    void reset() noexcept
    {
        active = false;
        ampEnv.reset(); filtEnv.reset();
        filterL.reset(); filterR.reset();
        lfo1.reset(); lfo2.reset();
        sampleAndHold.reset(); pinkNoise.reset();
        for (int u = 0; u < kMaxUnison; ++u)
        {
            osc[u].osc1.resetPhase(); osc[u].osc2.resetPhase();
            osc[u].osc3.resetPhase(); osc[u].sub.resetPhase();
            fm[u].reset();
            lastOsc2[u] = 0.0f;
        }
        lfoRateMod1 = lfoRateMod2 = 0.0f;
        effectsMixMod = 0.0f;
        retiring = false; retirePhase = 1.0f;
    }

    void noteOn(int midiNote, float velocity, const VoiceParameters& params) noexcept
    {
        currentNote = midiNote;
        currentVelocity = velocity;
        active = true;
        retiring = false; retirePhase = 1.0f;   // a re-used voice is no longer retiring

        const float targetFreq = midiToHz((float)midiNote);

        if (params.portamentoTime > 0.0f && lastFreq > 0.0f)
        {
            float tau = params.portamentoTime;
            if (params.glideMode != 0) // constant rate: proportional to interval (octaves)
                tau = params.portamentoTime * maxf(0.001f, std::abs(std::log2(targetFreq / lastFreq)));
            portaFreq.prepare(sr, maxf(0.001f, tau));
            portaFreq.setTarget(targetFreq);
        }
        else
        {
            portaFreq.snap(targetFreq);
        }
        lastFreq = targetFreq;

        float mappedVel = velocity;
        switch (params.velocityCurve)
        {
            case 1: mappedVel = std::sqrt(velocity); break;
            case 2: mappedVel = velocity * velocity; break;
            case 3: mappedVel = 1.0f; break;
            default: break;
        }
        velocityGain = 1.0f - params.velocitySensitivity * (1.0f - mappedVel);

        randomPerNote = rng.nextFloat();

        if (!params.legatoMode || !ampEnv.isActive())
        {
            ampEnv.noteOn();
            filtEnv.noteOn();
            // Per-note LFO retrigger (resets phase + fade-in + S&H target). Legato
            // notes keep the running LFO. Without this the LFO fade-in is inert.
            lfo1.retrigger(); lfo2.retrigger();
            filterR.reset(); // keep the (usually idle) R filter fresh for unison
            for (int u = 0; u < kMaxUnison; ++u)
            {
                osc[u].osc1.resetPhase(); osc[u].osc2.resetPhase();
                osc[u].osc3.resetPhase(); osc[u].sub.resetPhase();
                lastOsc2[u] = 0.0f;
            }
            // Prism: retrigger every operator envelope. Base freq/keyscaling use
            // the played note here; per-sample bend/porta/detune flow through
            // fm[u].setFrequency() during rendering.
            if (params.mode == SynthMode::Prism)
                for (int u = 0; u < kMaxUnison; ++u)
                    fm[u].noteOn(targetFreq, velocity);
        }
    }

    // Push the per-block Prism (FM) parameters to every unison operator bank and
    // cache the carrier-count headroom trim. Called once per block by the engine
    // (only in Prism mode) — never in the per-sample render path.
    void updateFMParams(const VoiceParameters& params) noexcept
    {
        const int algo = clampi(params.prismAlgo, 0, 7);
        for (int u = 0; u < kMaxUnison; ++u)
        {
            fm[u].setAlgorithm(algo);
            fm[u].setFeedback(params.prismFB);
            for (int i = 0; i < FMVoiceEngine::kNumOps; ++i)
            {
                const auto& o = params.op[i];
                fm[u].setOpRatio(i, o.ratio);
                fm[u].setOpFine(i, o.fine);
                fm[u].setOpLevel(i, o.level);
                fm[u].setOpVelSens(i, o.vel);
                fm[u].setOpKeyScale(i, o.keyScale);
                fm[u].setOpADSR(i, o.a, o.d, o.s, o.r);
            }
        }
        // Summed carriers are NOT normalised inside FMVoiceEngine (peak can reach
        // ~2.5 with 4 carriers), so trim by 1/sqrt(numCarriers) per voice.
        const int nc = popcount8(kPrismAlgos[algo].carrierMask);
        fmCarrierTrim = nc > 0 ? 1.0f / std::sqrt((float)nc) : 1.0f;
    }

    // Note-off also releases every Prism operator envelope, so per-op release
    // times shape the FM timbre during the tail (the amp env still gates the
    // voice). Harmless outside Prism: an idle FMVoiceEngine renders nothing.
    void noteOff() noexcept
    {
        ampEnv.noteOff(); filtEnv.noteOff();
        for (int u = 0; u < kMaxUnison; ++u) fm[u].noteOff();
    }
    // Voice steal: release everything that shapes the outgoing note before the new
    // one is triggered on top. The FILTER envelope must release with the amp
    // envelope, not just alongside it: in legato mode noteOn() deliberately skips
    // the retrigger while ampEnv is still active, so a stolen voice runs its amp
    // release to silence while filtEnv sat in Sustain holding the cutoff wide open
    // for the whole fade — and left that stale level as the seed for the next note
    // to land on this voice. Outside legato the following noteOn() retriggers both
    // envelopes from their current values, so this is a no-op there.
    void setSteal() noexcept
    {
        ampEnv.noteOff(); filtEnv.noteOff();
        for (int u = 0; u < kMaxUnison; ++u) fm[u].noteOff();
    }

    // Retire: the voice has fallen OUTSIDE the allocator's effective polyphony
    // (the unison count grew, or a mode with a smaller voice budget took over)
    // while still sounding. It used to be reset() outright, which is a full-scale
    // step to zero in one sample — a click.
    //
    // Instead: release like a note-off, and gate it with an independent ramp that
    // guarantees the voice is finished within kRetireSeconds. The ramp is what
    // makes this safe. A plain release would let a 10 s patch release hold a voice
    // that is over budget for 10 s; the ramp caps the overrun at ~15 ms, so the
    // extra rendering can never accumulate and the allocator's accounting (which
    // only ever hands out slots below effectivePoly()) stays honest.
    void retire() noexcept
    {
        if (!active || retiring) return;
        noteOff();
        retiring = true;
        retirePhase = 1.0f;
        // Freeze the sub-voice count for the fade. The edge that retires a voice is
        // almost always a unison change, and letting the retiring voice follow it
        // would drop fresh sub-voices in at phase zero on the very sample the fade
        // starts — a second discontinuity, in the one voice that exists only to
        // avoid the first. A voice on its way out plays out as it was.
        retireUnison = prevUCount;
    }

    // Oscillator banks this voice would keep rendering if it were retired now:
    // retire() freezes the sub-voice count, so it is the last one rendered.
    int  retireBankCost() const noexcept { return prevUCount; }

    bool isActive() const noexcept { return active; }
    bool isReleasing() const noexcept { return ampEnv.getStage() == ADSREnvelope::Stage::Release; }
    int  getCurrentNote() const noexcept { return currentNote; }
    float getCurrentLevel() const noexcept { return ampEnv.getCurrentValue() * velocityGain; }
    float getEffectsMixMod() const noexcept { return effectsMixMod; }

    void setLFO1Params(LFOShape shape, float rate, float fadeIn) noexcept
    { lfo1.setShape(shape); baseLfo1Rate = rate; lfo1.setFadeIn(fadeIn); }
    void setLFO2Params(LFOShape shape, float rate, float fadeIn) noexcept
    { lfo2.setShape(shape); baseLfo2Rate = rate; lfo2.setFadeIn(fadeIn); }

    // Render one INTERNAL-rate stereo sample for this voice.
    void renderInternalSample(const VoiceParameters& params, const ModMatrix& matrix,
                              int unisonCount, float& outL, float& outR) noexcept
    {
        outL = outR = 0.0f;
        if (!active) return;

        // Acid (mode 5) is rendered by the engine's dedicated mono path, not the
        // poly voice; if it ever reaches here, render silence but keep lifecycle
        // honest so the voice frees on note-off. Prism (mode 4) renders below.
        const bool silentMode = (params.mode == SynthMode::Acid);

        // --- envelopes ---
        ampEnv.setParameters(params.ampAttack, params.ampDecay, params.ampSustain, params.ampRelease);
        ampEnv.setCurve(params.ampCurve);
        filtEnv.setParameters(params.filtAttack, params.filtDecay, params.filtSustain, params.filtRelease);
        filtEnv.setCurve(params.filtCurve);
        const float ampVal = ampEnv.processSample();
        const float filtVal = filtEnv.processSample();
        if (!ampEnv.isActive()) { active = false; return; }

        // Retire ramp (see retire()). Advanced BEFORE the silent-mode early-out so
        // a retiring voice always finishes on schedule, whatever the render branch
        // is doing. Smoothstep rather than a linear ramp: a linear fade still has a
        // slope corner at each end, and on a loud voice that corner is audible as a
        // faint tick; p*p*(3-2p) is C1 at both ends and costs two multiplies.
        float retireGain = 1.0f;
        if (retiring)
        {
            retirePhase -= retireStep;
            if (retirePhase <= 0.0f)
            {
                retirePhase = 0.0f; retiring = false; active = false;
                return;
            }
            retireGain = retirePhase * retirePhase * (3.0f - 2.0f * retirePhase);
        }
        if (silentMode) return;

        // --- modulation state ---
        ModulationState modState;
        buildModState(modState, matrix, params);
        lfoRateMod1 = modState.getDestValue(ModDest::LFO1Rate);
        lfoRateMod2 = modState.getDestValue(ModDest::LFO2Rate);
        effectsMixMod = modState.getDestValue(ModDest::EffectsMix);

        const float pitchMod1 = modState.getDestValue(ModDest::Osc1Pitch);
        const float pitchMod2 = modState.getDestValue(ModDest::Osc2Pitch);
        const float pwm1 = modState.getDestValue(ModDest::Osc1PWM) * 0.4f;
        const float pwm2 = modState.getDestValue(ModDest::Osc2PWM) * 0.4f;
        const float cutoffMod = modState.getDestValue(ModDest::FilterCutoff);
        const float resMod = modState.getDestValue(ModDest::FilterResonance);
        const float panMod = modState.getDestValue(ModDest::Pan) + voicePanOffset * params.analogAmount;
        const float uniDetMod = modState.getDestValue(ModDest::UnisonDetune);

        // --- base frequency: porta + drift + pitch bend + master tune (fix #2) ---
        float baseFreq = portaFreq.next();
        if (--driftCounter <= 0) { driftTarget = rng.nextBipolar(); driftCounter = 200 + rng.nextInt(300); }
        driftSmooth += (driftTarget - driftSmooth) * 0.001f;
        baseFreq *= (1.0f + driftSmooth * params.analogAmount * 0.002f);
        baseFreq *= std::pow(2.0f, (params.pitchBendSemis + params.masterTuneSemis) / 12.0f);

        const float freq1 = baseFreq * std::pow(2.0f, pitchMod1 * 2.0f / 12.0f);
        const float freq2 = baseFreq * std::pow(2.0f, ((float)params.osc2SemiOffset + pitchMod2 * 2.0f) / 12.0f);

        const float noiseSample = pinkNoise.processSample() * params.noiseLevel;

        const int uCount = clampi(retiring ? retireUnison : unisonCount, 1, kMaxUnison);
        // The R filter runs only on the multi-voice path; when unison count grows
        // from 1 -> >1 mid-note its state is stale (last touched at the previous
        // note-on). Reset it before first use to avoid a startup transient (C4b).
        if (uCount > 1 && prevUCount == 1) filterR.reset();
        prevUCount = uCount;
        const float uGain = 1.0f / std::sqrt((float)uCount);
        const float maxDetune = params.unisonDetune * (1.0f + clampf(uniDetMod, -0.9f, 4.0f));

        float preL = 0.0f, preR = 0.0f;

        for (int u = 0; u < uCount; ++u)
        {
            float detCents = 0.0f, uPan = 0.0f;
            if (uCount > 1)
            {
                const float t = (float)u / (float)(uCount - 1); // 0..1
                detCents = (t * 2.0f - 1.0f) * maxDetune;
                uPan = (t * 2.0f - 1.0f) * params.unisonSpread;
            }

            const float mix = renderOscSet(u, params, filtVal, freq1, freq2, baseFreq,
                                           pwm1, pwm2, detCents, noiseSample);

            if (uCount == 1)
            {
                preL = mix; preR = mix;
            }
            else
            {
                const float totalPan = clampf(uPan + panMod, -1.0f, 1.0f);
                const float angle = (totalPan + 1.0f) * 0.25f * kPi;
                preL += mix * std::cos(angle) * uGain;
                preR += mix * std::sin(angle) * uGain;
            }
        }

        // --- filter (env-modulated cutoff) ---
        const float polyModFiltExtra = (params.mode == SynthMode::Oracle)
            ? filtVal * params.polyModFEnvFilt * 2.0f : 0.0f;
        const float envModTotal = clampf((filtVal * params.filterEnvAmount + cutoffMod + polyModFiltExtra) * 2.0f, -2.0f, 2.0f);
        float envCutoff = params.filterCutoff * std::pow(2.0f, envModTotal);
        envCutoff *= (1.0f + filterTrackingOffset * params.analogAmount);
        // 0.4x internal-rate ceiling (fix): the OTA one-pole g = tan(pi*fc/sr)
        // misbehaves near Nyquist at 1x OS; 0.4x keeps every model well-behaved.
        envCutoff = clampf(envCutoff, 20.0f, (float)sr * 0.40f);
        const float envRes = clampf(params.filterResonance + resMod, 0.0f, 1.0f);

        // Prism has no analog filter model of its own; route it through the clean
        // (non-self-oscillating) Cosmos LPF so the section stays in circuit.
        const FilterMode filtMode = (params.mode == SynthMode::Prism)
            ? FilterMode::Cosmos : (FilterMode)clampi((int)params.mode, 0, 3);
        // Prism routes through the Cosmos filter model but has no dedicated HP
        // control, so feed a neutral 20 Hz HP (U3). Modes 1-3 ignore the HP arg.
        const float hpCut = (params.mode == SynthMode::Prism) ? 20.0f : params.filterHPCutoff;
        filterL.setMode(filtMode); filterL.setParameters(envCutoff, envRes, hpCut);

        float sL, sR;
        if (uCount == 1)
        {
            const float filtered = filterL.process(preL);
            // Clamp panMod to the valid pan range before the constant-power angle
            // math; the multi-voice path already clamps its totalPan (C4a).
            const float sPan = clampf(panMod, -1.0f, 1.0f);
            const float angle = (sPan + 1.0f) * 0.25f * kPi; // == JUCE single-voice pan
            sL = filtered * std::cos(angle);
            sR = filtered * std::sin(angle);
        }
        else
        {
            filterR.setMode(filtMode); filterR.setParameters(envCutoff, envRes, hpCut);
            sL = filterL.process(preL);
            sR = filterR.process(preR);
        }

        // --- amplitude ---
        const float ampMod = clampf(1.0f + modState.getDestValue(ModDest::Amplitude), 0.0f, 2.0f);
        const float g = ampVal * velocityGain * ampMod * retireGain;
        outL = sanitize(sL * g);
        outR = sanitize(sR * g);
    }

private:
    struct OscSet { Oscillator osc1, osc2, osc3; SubOscillator sub; };

    static float sanitize(float x) noexcept
    {
        if (isBad(x)) return 0.0f;
        return clampf(x, -4.0f, 4.0f);
    }

    static int popcount8(uint8_t v) noexcept
    {
        int c = 0;
        for (; v; v &= (uint8_t)(v - 1)) ++c;
        return c;
    }

    // One unison sub-voice's oscillator mix (pre-filter, mode-normalized).
    float renderOscSet(int u, const VoiceParameters& params, float filtVal,
                       float freq1, float freq2, float baseFreq,
                       float pwm1, float pwm2, float detCents, float noiseSample) noexcept
    {
        OscSet& o = osc[(size_t)u];

        o.osc1.setFrequency(freq1);
        o.osc1.setWaveform(params.osc1Wave);
        // Oracle poly-mod OscB->PW: folded into the pre-render pulse-width set using
        // the 1-sample-delayed osc2 value. (The old Oracle-branch setPulseWidth ran
        // AFTER this call, so it was overwritten here on the next sample -> dead.)
        float pw1 = params.osc1PulseWidth + pwm1;
        if (params.mode == SynthMode::Oracle && params.polyModOscBPWM > 0.0f)
            pw1 += lastOsc2[(size_t)u] * params.polyModOscBPWM * 0.3f;
        o.osc1.setPulseWidth(pw1);
        o.osc1.setDetune(params.osc1Detune + detCents);

        // Cross mod (fix #4): osc2 (previous sample) -> osc1 frequency. The UI and
        // design expose cross-mod only in Cosmos and Oracle, so gate it to those
        // modes; every preset keeps crossMod=0 elsewhere, so renders are unchanged.
        if (params.crossMod > 0.0f
            && (params.mode == SynthMode::Cosmos || params.mode == SynthMode::Oracle))
            o.osc1.applyFM(lastOsc2[(size_t)u] * params.crossMod * 0.03f);

        float osc1Sample = 0.0f, osc2Sample = 0.0f, osc3Sample = 0.0f, subSample = 0.0f;
        float mix = 0.0f, activeGain = 0.0f;

        switch (params.mode)
        {
            case SynthMode::Cosmos:
            {
                // Single DCO: saw (osc1) + pulse (osc2) at the SAME frequency + sub.
                // (JUCE double-advanced osc2 here, an octave-up bug; fixed: osc2 is
                //  configured as the pulse component at freq1 and processed once.)
                osc1Sample = o.osc1.processSample();
                o.osc2.setFrequency(freq1);
                o.osc2.setWaveform(Waveform::Pulse);
                o.osc2.setPulseWidth(params.osc2PulseWidth + pwm2);
                o.osc2.setDetune(params.osc1Detune + detCents);
                osc2Sample = o.osc2.processSample();

                o.sub.setFrequency(baseFreq);
                o.sub.setWaveform(params.subWave);
                o.sub.setDetune(detCents);
                subSample = o.sub.processSample() * params.subLevel;

                mix = osc1Sample * params.osc1Level + osc2Sample * params.osc2Level + subSample + noiseSample;
                activeGain = params.osc1Level + params.osc2Level + params.subLevel + params.noiseLevel;
                break;
            }

            case SynthMode::Oracle:
            {
                o.osc2.setFrequency(freq2);
                o.osc2.setWaveform(params.osc2Wave);
                o.osc2.setPulseWidth(params.osc2PulseWidth + pwm2);
                o.osc2.setDetune(params.osc2Detune + detCents);
                osc1Sample = o.osc1.processSample();
                osc2Sample = o.osc2.processSample();

                // Poly-mod (affects next sample via applyFM, matching original).
                if (params.polyModFEnvOscA > 0.0f) o.osc1.applyFM(filtVal * params.polyModFEnvOscA * 0.03f);
                if (params.polyModOscBOscA > 0.0f) o.osc1.applyFM(osc2Sample * params.polyModOscBOscA * 0.03f);
                // (OscB->PW poly-mod is applied to osc1's pulse width up in renderOscSet's
                //  pre-render set, using the 1-sample-delayed osc2 value.)

                mix = osc1Sample * params.osc1Level + osc2Sample * params.osc2Level + noiseSample;
                activeGain = params.osc1Level + params.osc2Level + params.noiseLevel;
                break;
            }

            case SynthMode::Mono:
            {
                o.osc2.setFrequency(freq2);
                o.osc2.setWaveform(params.osc2Wave);
                o.osc2.setPulseWidth(params.osc2PulseWidth + pwm2);
                o.osc2.setDetune(params.osc2Detune + detCents);
                osc1Sample = o.osc1.processSample();
                if (params.hardSync && o.osc1.didCross()) o.osc2.hardSync();
                osc2Sample = o.osc2.processSample();

                o.sub.setFrequency(baseFreq);
                o.sub.setWaveform(params.subWave);
                o.sub.setDetune(detCents);
                subSample = o.sub.processSample() * params.subLevel;

                if (params.ringMod > 0.0f)
                {
                    const float ring = ringModulate(osc1Sample, osc2Sample);
                    osc1Sample = osc1Sample * (1.0f - params.ringMod) + ring * params.ringMod;
                }

                mix = osc1Sample * params.osc1Level + osc2Sample * params.osc2Level + subSample + noiseSample;
                activeGain = params.osc1Level + params.osc2Level + params.subLevel + params.noiseLevel;
                break;
            }

            case SynthMode::Modular:
            {
                o.osc2.setFrequency(freq2);
                o.osc2.setWaveform(params.osc2Wave);
                o.osc2.setPulseWidth(params.osc2PulseWidth + pwm2);
                o.osc2.setDetune(params.osc2Detune + detCents);
                osc1Sample = o.osc1.processSample();
                if (params.hardSync && o.osc1.didCross()) o.osc2.hardSync();
                if (params.fmAmount > 0.0f) o.osc2.applyFM(osc1Sample * params.fmAmount * 0.05f);
                osc2Sample = o.osc2.processSample();

                o.osc3.setFrequency(baseFreq);
                o.osc3.setWaveform(params.osc3Wave);
                o.osc3.setDetune(detCents);
                osc3Sample = o.osc3.processSample() * params.osc3Level;

                if (params.ringMod > 0.0f)
                {
                    const float ring = ringModulate(osc1Sample, osc2Sample);
                    osc2Sample = osc2Sample * (1.0f - params.ringMod) + ring * params.ringMod;
                }

                mix = osc1Sample * params.osc1Level + osc2Sample * params.osc2Level + osc3Sample + noiseSample;
                activeGain = params.osc1Level + params.osc2Level + params.osc3Level + params.noiseLevel;
                break;
            }

            case SynthMode::Prism:
            {
                // 4-op FM replaces the analog osc bank. Per-sample frequency
                // (freq1 already carries bend/tune/porta/drift/Osc1Pitch mod)
                // plus this unison sub-voice's detune; the FM output is already
                // envelope-shaped internally, so no activeGain normalisation —
                // just the carrier-count headroom trim.
                const float detFreq = freq1 * std::pow(2.0f, detCents / 1200.0f);
                fm[(size_t)u].setFrequency(detFreq);
                // Wire noise into Prism so the always-visible NOISE knob is honest
                // (U4). noiseLevel defaults to 0, so existing presets are bit-null.
                mix = fm[(size_t)u].processSample() * fmCarrierTrim + noiseSample;
                activeGain = 0.0f;
                break;
            }

            default: break; // Acid handled by the engine's mono path
        }

        lastOsc2[(size_t)u] = osc2Sample;
        if (activeGain > 1.0f) mix /= activeGain;
        return mix;
    }

    void buildModState(ModulationState& state, const ModMatrix& matrix, const VoiceParameters& params) noexcept
    {
        // LFO rate mod (fix #5) uses the previous sample's dest value to avoid a
        // dependency cycle (the LFO output itself feeds the matrix).
        lfo1.setRate(baseLfo1Rate * (1.0f + clampf(lfoRateMod1, -0.9f, 4.0f)));
        lfo2.setRate(baseLfo2Rate * (1.0f + clampf(lfoRateMod2, -0.9f, 4.0f)));

        state.setSourceValue(ModSource::LFO1, lfo1.processSample());
        state.setSourceValue(ModSource::LFO2, lfo2.processSample());
        state.setSourceValue(ModSource::Envelope2, filtEnv.getCurrentValue());
        state.setSourceValue(ModSource::ModWheel, params.modWheel);
        state.setSourceValue(ModSource::Aftertouch, params.aftertouch);
        state.setSourceValue(ModSource::Velocity, currentVelocity);
        state.setSourceValue(ModSource::KeyTracking, (float)(currentNote - 60) / 60.0f);
        state.setSourceValue(ModSource::Random, randomPerNote);
        // PitchBend mod source is a normalized ±1 value (C2). Feeding the raw
        // ±24 semitone value here broke the ±1 source convention and made every
        // PitchBend->pitch route double-scale. The semitone value is still used
        // for the oscillator base frequency above.
        state.setSourceValue(ModSource::PitchBend, params.pitchBendNorm);

        sampleAndHold.setRate(params.shRate);
        state.setSourceValue(ModSource::SampleAndHold, sampleAndHold.process(rng.nextBipolar()));

        matrix.process(state);
    }

    double sr = 44100.0;
    int   currentNote = -1;
    float currentVelocity = 0.0f;
    float velocityGain = 1.0f;
    float randomPerNote = 0.0f;
    bool  active = false;
    float lastFreq = 0.0f;
    float driftTarget = 0.0f, driftSmooth = 0.0f;
    int   driftCounter = 0;
    float filterTrackingOffset = 0.0f, voicePanOffset = 0.0f;
    float baseLfo1Rate = 1.0f, baseLfo2Rate = 0.5f;
    float lfoRateMod1 = 0.0f, lfoRateMod2 = 0.0f;
    float effectsMixMod = 0.0f;
    int   prevUCount = 1; // detects the 1 -> >1 unison transition (C4b)
    bool  retiring = false;        // bounded fade-out in progress (see retire())
    float retirePhase = 1.0f;      // 1 -> 0 over kRetireSeconds
    float retireStep = 1.0f;       // per internal sample, set by prepare/setSampleRate
    int   retireUnison = 1;        // sub-voice count frozen for the duration of the fade

    OscSet osc[kMaxUnison];
    FMVoiceEngine fm[kMaxUnison];      // Prism (mode 4) — one op bank per unison sub-voice
    float  fmCarrierTrim = 1.0f;       // 1/sqrt(numCarriers), set by updateFMParams
    float  lastOsc2[kMaxUnison] = {};
    PinkNoiseGenerator pinkNoise;
    SampleAndHold sampleAndHold;
    SynthFilter filterL, filterR;
    ADSREnvelope ampEnv, filtEnv;
    LFO lfo1, lfo2;
    duskaudio::SmoothedValue portaFreq;
    Xorshift rng;
};

//==============================================================================
// Voice allocator: free-first then steal-quietest; unison sub-voice count with
// automatic polyphony reduction so poly x unison <= 16 (fix #3).
class VoiceAllocator
{
public:
    void prepare(double sampleRate) noexcept
    {
        for (int i = 0; i < kMaxPolyphony; ++i)
            voices[(size_t)i].prepare(sampleRate, 0x1000u + 0x1000u * (uint32_t)i);
        voiceGain.prepare(sampleRate, kParamSmoothTau);
        snapVoiceGain();
    }
    void reset() noexcept
    {
        for (auto& v : voices) v.reset();
        snapVoiceGain();
    }
    // Rate change preserving active notes/pitch (oversampling-factor switch).
    void setSampleRate(double sampleRate) noexcept
    {
        for (auto& v : voices) v.setSampleRate(sampleRate);
        voiceGain.prepare(sampleRate, kParamSmoothTau); // new coeff, keeps the value
    }
    // Land the headroom trim on the current polyphony instead of gliding to it:
    // the first snapshot after prepare/reset (the budget is simply being
    // established), and any snapshot classified as a preset load — a program
    // change must land, never swoop, and this is a smoothed control like any other.
    void snapVoiceGain() noexcept { polyTrim = gainForPoly(effectivePoly()); voiceGain.snap(polyTrim); }

    // modeMaxVoices: nominal polyphony for the current mode. On an actual change,
    // retire voices that fall outside the new limit so they can't become zombies
    // that resume sounding if the limit later grows again.
    void setModeVoices(int modeMaxVoices) noexcept
    {
        const int v = clampi(modeMaxVoices, 1, kMaxPolyphony);
        if (v == modeVoices) return;
        modeVoices = v;
        retireAbove();
    }
    void setUnison(int count) noexcept
    {
        const int c = clampi(count, 1, kMaxUnison);
        if (c == unisonCount) return;
        unisonCount = c;
        retireAbove();
    }

    int effectivePoly() const noexcept
    {
        const int byUnison = kMaxOscVoices / unisonCount;
        return clampi(byUnison < modeVoices ? byUnison : modeVoices, 1, kMaxPolyphony);
    }

    // Clear every voice at or above the current effective polyphony, so a later
    // limit increase cannot revive a voice that was silently dropped. A voice that
    // is still SOUNDING is faded out (SynthVoice::retire) rather than reset — the
    // reset was a full-scale step to zero in one sample. renderInternalSample()
    // keeps rendering retiring voices until their ramp ends; noteOn() never looks
    // at them, because it only ever searches below effectivePoly(), so a retiring
    // voice cannot occupy a slot a new note needs.
    void retireAbove() noexcept
    {
        const int poly = effectivePoly();
        polyTrim = gainForPoly(poly);   // only edge on which effectivePoly() moves

        // The fade is BUDGETED. A retiring voice keeps rendering with its previous
        // sub-voice count frozen, so a shrink transiently runs more oscillator
        // banks than the poly x unison <= kMaxOscVoices ceiling allows in steady
        // state — up to 2x it:
        //     polyNew*unisonNew + (polyOld - polyNew)*unisonOld  <=  16 + 16
        // Measured with cpu_bench "shrink" (Prism 4-op FM, 4x OS, full FX — the
        // worst reachable case, 28 banks) the unbudgeted transient cost a 22.0 ms
        // worst block against a 10.7 ms budget. Half the ceiling may fade;
        // everything past it is reset outright.
        //
        // WHICH voices fade is chosen by level, loudest first, because a hard reset
        // is a step proportional to the voice's current output: the voices that
        // would actually be heard clicking get the fade, and the step that is left
        // is always the smallest one available. (cpu_bench "steady16" is the
        // control for all of this: 16 banks with nothing retiring already costs
        // 96.2%, so the ceiling itself — not this fade — is the real CPU wall.)
        int budget = kMaxOscVoices / 2;
        bool handled[kMaxPolyphony] = {};
        for (int pass = poly; pass < kMaxPolyphony; ++pass)
        {
            int best = -1;
            float bestLevel = -1.0f;
            for (int i = poly; i < kMaxPolyphony; ++i)
            {
                if (handled[(size_t)i] || !voices[(size_t)i].isActive()) continue;
                const float level = voices[(size_t)i].getCurrentLevel();
                if (level > bestLevel) { bestLevel = level; best = i; }
            }
            if (best < 0) break;                    // nothing sounding left to place
            handled[(size_t)best] = true;
            SynthVoice& v = voices[(size_t)best];
            const int cost = v.retireBankCost();
            if (cost <= budget) { budget -= cost; v.retire(); }
            else                { v.reset(); }
        }
        // Slots that were already idle (or were just reset) are cleared outright,
        // so a later limit increase can never revive one.
        for (int i = poly; i < kMaxPolyphony; ++i)
            if (!voices[(size_t)i].isActive()) voices[(size_t)i].reset();
    }

    SynthVoice* noteOn(int note, float velocity, const VoiceParameters& params) noexcept
    {
        const int poly = effectivePoly();
        for (int i = 0; i < poly; ++i)
            if (!voices[(size_t)i].isActive()) { voices[(size_t)i].noteOn(note, velocity, params); return &voices[(size_t)i]; }

        int quietest = 0;
        float quietestLevel = 1.0e9f;
        for (int i = 0; i < poly; ++i)
        {
            float level = voices[(size_t)i].getCurrentLevel();
            if (voices[(size_t)i].isReleasing()) level *= 0.1f;
            if (level < quietestLevel) { quietestLevel = level; quietest = i; }
        }
        voices[(size_t)quietest].setSteal();
        voices[(size_t)quietest].noteOn(note, velocity, params);
        return &voices[(size_t)quietest];
    }

    void noteOff(int note) noexcept
    {
        for (auto& v : voices)
            if (v.isActive() && v.getCurrentNote() == note) v.noteOff();
    }

    void allNotesOff() noexcept { for (auto& v : voices) if (v.isActive()) v.noteOff(); }

    SynthVoice* getVoice(int i) noexcept { return &voices[(size_t)i]; }
    int getPoly() const noexcept { return effectivePoly(); }

    // Render one INTERNAL-rate stereo sample summing all active voices. Also
    // aggregates the EffectsMix mod (fix #5) as the mean over active voices.
    void renderInternalSample(const VoiceParameters& params, const ModMatrix& matrix,
                              float& outL, float& outR, float& effectsMixModOut) noexcept
    {
        outL = outR = 0.0f;
        // The headroom trim is a TARGET, not a value: effectivePoly() changes on a
        // unison or mode-budget edge, and stepping 2/sqrt(poly) there jumps the
        // level of every surviving voice on that one sample (6 -> 5 voices is
        // +0.79 dB; the largest reachable step is 8 -> 4 or below at +3.01 dB,
        // where the trim saturates at unity) — the same click the retire fade
        // exists to remove. Gliding it costs one multiply-add per internal sample
        // and is exactly a no-op while the polyphony holds still. The target is
        // cached on the edge, so no sqrt is taken in the render path.
        voiceGain.setTarget(polyTrim);
        const float g = voiceGain.next();

        // All slots are visited, not just the ones below effectivePoly(): a voice
        // above the limit can still be sounding while its retire ramp runs out.
        float fxSum = 0.0f; int activeN = 0;
        for (int v = 0; v < kMaxPolyphony; ++v)
        {
            auto& voice = voices[(size_t)v];
            if (!voice.isActive()) continue;
            float l, r;
            voice.renderInternalSample(params, matrix, unisonCount, l, r);
            outL += l * g;
            outR += r * g;
            fxSum += voice.getEffectsMixMod();
            ++activeN;
        }
        effectsMixModOut = activeN > 0 ? fxSum / (float)activeN : 0.0f;
    }

private:
    // Constant-power headroom referenced to a 4-voice sum: 2/sqrt(poly), clamped
    // to unity. The old 1/(1+log2(poly)) cost a 6-voice mode 11 dB even when ONE
    // note sounded, which made every poly preset audibly quieter than the
    // mono/acid modes (fleet audit: poly peaks -21..-33 dBFS vs mono -7..-9).
    // With this curve the loudest poly preset peaks ~-7 dBFS under the audit
    // performances (rule: peak <= -1), mono/acid are untouched (poly=1 -> 1.0),
    // and sqrt still tracks uncorrelated voice summing as polyphony grows.
    static float gainForPoly(int poly) noexcept
    {
        return std::min(1.0f, 2.0f / std::sqrt((float)poly));
    }

    SynthVoice voices[kMaxPolyphony];
    duskaudio::SmoothedValue voiceGain;   // glided headroom trim
    // gainForPoly(effectivePoly()), cached. effectivePoly() only moves when
    // setModeVoices/setUnison change it and both funnel through retireAbove(), so
    // the sqrt is taken on that edge rather than once per internal sample.
    float polyTrim = 1.0f;
    int modeVoices = 6;
    int unisonCount = 1;
};

} // namespace msynth

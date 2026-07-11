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
// The oscillator section is a plain per-mode switch so Phase 2 can slot an
// alternative engine (FMEngine / AcidEngine) per voice; modes 4/5 (Prism/Acid)
// are safe stubs that render silence for now. TODO(Phase 2): dispatch modes
// 4/5 to FMEngine.hpp / AcidEngine.hpp.
//
// Prepared at the INTERNAL (oversampled) rate; renderInternalSample() is called
// osFactor times per host sample by the engine, then decimated.

#pragma once

#include "Oscillator.hpp"
#include "FilterEngine.hpp"
#include "Envelope.hpp"
#include "ModMatrix.hpp"
#include "DuskSmoothed.hpp"

namespace msynth
{

enum class SynthMode
{
    Cosmos = 0, // 6-voice DCO poly
    Oracle,     // 5-voice poly, poly-mod
    Mono,       // mono, sub + ring + sync
    Modular,    // duophonic, 3 osc + S&H + spring
    Prism,      // 4-op FM (Phase 2 stub)
    Acid        // acid bass + sequencer (Phase 2 stub)
};

static constexpr int kMaxPolyphony   = 8;
static constexpr int kMaxUnison      = 8;
static constexpr int kMaxOscVoices   = 16; // poly x unison ceiling

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
    float pitchBendSemis = 0.0f;
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
        }
        filterL.setSampleRate(sampleRate);
        filterR.setSampleRate(sampleRate);
        ampEnv.setSampleRate(sampleRate);
        filtEnv.setSampleRate(sampleRate);
        lfo1.setSampleRate(sampleRate);
        lfo2.setSampleRate(sampleRate);
        sampleAndHold.setSampleRate(sampleRate);
        portaFreq.prepare(sampleRate, 0.1f); // updates coeff; keeps current/target value
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
            lastOsc2[u] = 0.0f;
        }
        lfoRateMod1 = lfoRateMod2 = 0.0f;
        effectsMixMod = 0.0f;
    }

    void noteOn(int midiNote, float velocity, const VoiceParameters& params) noexcept
    {
        currentNote = midiNote;
        currentVelocity = velocity;
        active = true;

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
            filterR.reset(); // keep the (usually idle) R filter fresh for unison
            for (int u = 0; u < kMaxUnison; ++u)
            {
                osc[u].osc1.resetPhase(); osc[u].osc2.resetPhase();
                osc[u].osc3.resetPhase(); osc[u].sub.resetPhase();
                lastOsc2[u] = 0.0f;
            }
        }
    }

    void noteOff() noexcept { ampEnv.noteOff(); filtEnv.noteOff(); }
    void setSteal() noexcept { ampEnv.noteOff(); }

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

        // Modes 4/5 are Phase-2 engines; render silence but keep the voice
        // lifecycle honest so it frees on note-off.
        const bool stubMode = (params.mode == SynthMode::Prism || params.mode == SynthMode::Acid);

        // --- envelopes ---
        ampEnv.setParameters(params.ampAttack, params.ampDecay, params.ampSustain, params.ampRelease);
        ampEnv.setCurve(params.ampCurve);
        filtEnv.setParameters(params.filtAttack, params.filtDecay, params.filtSustain, params.filtRelease);
        filtEnv.setCurve(params.filtCurve);
        const float ampVal = ampEnv.processSample();
        const float filtVal = filtEnv.processSample();
        if (!ampEnv.isActive()) { active = false; return; }
        if (stubMode) return;

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

        const int uCount = clampi(unisonCount, 1, kMaxUnison);
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
        envCutoff = clampf(envCutoff, 20.0f, (float)sr * 0.45f);
        const float envRes = clampf(params.filterResonance + resMod, 0.0f, 1.0f);

        const FilterMode fm = (FilterMode)(int)params.mode;
        filterL.setMode(fm); filterL.setParameters(envCutoff, envRes, params.filterHPCutoff);

        float sL, sR;
        if (uCount == 1)
        {
            const float filtered = filterL.process(preL);
            const float angle = (panMod + 1.0f) * 0.25f * kPi; // == JUCE single-voice pan
            sL = filtered * std::cos(angle);
            sR = filtered * std::sin(angle);
        }
        else
        {
            filterR.setMode(fm); filterR.setParameters(envCutoff, envRes, params.filterHPCutoff);
            sL = filterL.process(preL);
            sR = filterR.process(preR);
        }

        // --- amplitude ---
        const float ampMod = clampf(1.0f + modState.getDestValue(ModDest::Amplitude), 0.0f, 2.0f);
        const float g = ampVal * velocityGain * ampMod;
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

    // One unison sub-voice's oscillator mix (pre-filter, mode-normalized).
    float renderOscSet(int u, const VoiceParameters& params, float filtVal,
                       float freq1, float freq2, float baseFreq,
                       float pwm1, float pwm2, float detCents, float noiseSample) noexcept
    {
        OscSet& o = osc[(size_t)u];

        o.osc1.setFrequency(freq1);
        o.osc1.setWaveform(params.osc1Wave);
        o.osc1.setPulseWidth(params.osc1PulseWidth + pwm1);
        o.osc1.setDetune(params.osc1Detune + detCents);

        // Cross mod (fix #4): osc2 (previous sample) -> osc1 frequency.
        if (params.crossMod > 0.0f)
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
                if (params.polyModOscBPWM > 0.0f)  o.osc1.setPulseWidth(params.osc1PulseWidth + osc2Sample * params.polyModOscBPWM * 0.3f);

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

            default: break;
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
        state.setSourceValue(ModSource::PitchBend, params.pitchBendSemis);

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

    OscSet osc[kMaxUnison];
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
    }
    void reset() noexcept { for (auto& v : voices) v.reset(); }
    // Rate change preserving active notes/pitch (oversampling-factor switch).
    void setSampleRate(double sampleRate) noexcept { for (auto& v : voices) v.setSampleRate(sampleRate); }

    // modeMaxVoices: nominal polyphony for the current mode.
    void setModeVoices(int modeMaxVoices) noexcept { modeVoices = clampi(modeMaxVoices, 1, kMaxPolyphony); }
    void setUnison(int count) noexcept { unisonCount = clampi(count, 1, kMaxUnison); }

    int effectivePoly() const noexcept
    {
        const int byUnison = kMaxOscVoices / unisonCount;
        return clampi(byUnison < modeVoices ? byUnison : modeVoices, 1, kMaxPolyphony);
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
        const int poly = effectivePoly();
        const float voiceGain = 1.0f / (1.0f + std::log2((float)poly));

        float fxSum = 0.0f; int activeN = 0;
        for (int v = 0; v < poly; ++v)
        {
            auto& voice = voices[(size_t)v];
            if (!voice.isActive()) continue;
            float l, r;
            voice.renderInternalSample(params, matrix, unisonCount, l, r);
            outL += l * voiceGain;
            outR += r * voiceGain;
            fxSum += voice.getEffectsMixMod();
            ++activeN;
        }
        effectsMixModOut = activeN > 0 ? fxSum / (float)activeN : 0.0f;
    }

private:
    SynthVoice voices[kMaxPolyphony];
    int modeVoices = 6;
    int unisonCount = 1;
};

} // namespace msynth

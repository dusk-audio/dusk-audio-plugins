#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "OscillatorEngine.h"
#include "FilterEngine.h"
#include "ModMatrix.h"
#include "UnisonEngine.h"

namespace MultiSynthDSP
{

// Synth mode enumeration
enum class SynthMode
{
    Cosmos,   // 8-voice poly, 2 DCOs, HPF+LPF, built-in chorus
    Oracle,   // 5-voice poly, 2 oscs, Curtis filter, poly-mod
    Mono,     // Mono, 2 oscs + sub, aggressive filter, ring mod
    Modular   // Semi-modular, 3 oscs, ladder filter, S&H, spring reverb
};

static constexpr int kMaxPolyphony = 8;

// Per-voice parameters (shared across all voices for a given mode)
struct VoiceParameters
{
    SynthMode mode = SynthMode::Cosmos;

    // Oscillator 1
    Waveform osc1Wave = Waveform::Saw;
    float osc1Detune = 0.0f;      // cents
    float osc1PulseWidth = 0.5f;
    float osc1Level = 1.0f;

    // Oscillator 2
    Waveform osc2Wave = Waveform::Saw;
    float osc2Detune = 0.0f;
    float osc2PulseWidth = 0.5f;
    float osc2Level = 0.8f;
    int osc2SemiOffset = 0;        // semitone offset from osc1

    // Oscillator 3 (Modular only)
    Waveform osc3Wave = Waveform::Saw;
    float osc3Level = 0.5f;

    // Sub oscillator (Mono only)
    float subLevel = 0.5f;
    Waveform subWave = Waveform::Square;

    // Noise
    float noiseLevel = 0.0f;

    // Filter
    float filterCutoff = 8000.0f;
    float filterResonance = 0.3f;
    float filterHPCutoff = 20.0f; // Cosmos HP

    // Filter envelope amount
    float filterEnvAmount = 0.5f;

    // Envelope 1 (Amplitude)
    float ampAttack = 0.01f;
    float ampDecay = 0.2f;
    float ampSustain = 0.8f;
    float ampRelease = 0.3f;
    EnvelopeCurve ampCurve = EnvelopeCurve::Exponential;

    // Envelope 2 (Filter)
    float filtAttack = 0.01f;
    float filtDecay = 0.3f;
    float filtSustain = 0.4f;
    float filtRelease = 0.5f;
    EnvelopeCurve filtCurve = EnvelopeCurve::Exponential;

    // Mode-specific
    float crossMod = 0.0f;        // Cosmos: cross-mod, Oracle: poly-mod amount
    float ringMod = 0.0f;         // Mono/Modular: ring mod amount
    bool hardSync = false;         // Modular: hard sync osc2 to osc1
    float fmAmount = 0.0f;        // Modular: FM between oscillators

    // Portamento
    float portamentoTime = 0.0f;  // seconds
    bool legatoMode = false;

    // Analog character
    float analogAmount = 0.0f;    // 0-1, adds per-voice imperfections

    // Velocity sensitivity
    float velocitySensitivity = 0.7f;
};

// A single synth voice that renders one note with all oscillators & filter
class SynthVoice
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        osc1.prepare(sampleRate);
        osc2.prepare(sampleRate);
        osc3.prepare(sampleRate);
        subOsc.prepare(sampleRate);
        noiseOsc.prepare(sampleRate);
        noiseOsc.setWaveform(Waveform::Noise);
        filter.prepare(sampleRate);
        ampEnv.prepare(sampleRate);
        filtEnv.prepare(sampleRate);
        lfo1.prepare(sampleRate);
        lfo2.prepare(sampleRate);

        // Per-voice analog drift
        analogDrift = (rng.nextFloat() - 0.5f) * 2.0f;
        portaFreq.reset(sampleRate, 0.1);
    }

    void noteOn(int midiNote, float velocity, const VoiceParameters& params)
    {
        currentNote = midiNote;
        currentVelocity = velocity;
        active = true;
        stealing = false;

        float targetFreq = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(midiNote));

        if (params.portamentoTime > 0.0f && lastFreq > 0.0f)
        {
            portaFreq.reset(sr, static_cast<double>(params.portamentoTime));
            portaFreq.setTargetValue(targetFreq);
        }
        else
        {
            portaFreq.setCurrentAndTargetValue(targetFreq);
        }
        lastFreq = targetFreq;

        // Velocity scaling
        float velScale = 1.0f - params.velocitySensitivity * (1.0f - velocity);
        velocityGain = velScale;

        // Random per-note value for mod matrix
        randomPerNote = rng.nextFloat();

        // Start envelopes
        if (!params.legatoMode || !ampEnv.isActive())
        {
            ampEnv.noteOn();
            filtEnv.noteOn();
            osc1.resetPhase();
            osc2.resetPhase();
            osc3.resetPhase();
            subOsc.resetPhase();
        }
    }

    void noteOff()
    {
        ampEnv.noteOff();
        filtEnv.noteOff();
    }

    bool isActive() const { return active; }
    bool isReleasing() const { return ampEnv.getStage() == ADSREnvelope::Stage::Release; }
    int getCurrentNote() const { return currentNote; }
    float getCurrentLevel() const { return ampEnv.getCurrentValue() * velocityGain; }

    void setSteal() { stealing = true; ampEnv.noteOff(); }

    // Render one sample for this voice, returns mono
    float renderSample(const VoiceParameters& params, ModulationState& modState)
    {
        if (!active) return 0.0f;

        // Update envelopes
        ampEnv.setParameters(params.ampAttack, params.ampDecay, params.ampSustain, params.ampRelease);
        ampEnv.setCurve(params.ampCurve);
        filtEnv.setParameters(params.filtAttack, params.filtDecay, params.filtSustain, params.filtRelease);
        filtEnv.setCurve(params.filtCurve);

        float ampVal = ampEnv.processSample();
        float filtVal = filtEnv.processSample();

        if (!ampEnv.isActive())
        {
            active = false;
            return 0.0f;
        }

        // Get base frequency with portamento
        float baseFreq = portaFreq.getNextValue();

        // Apply analog drift
        float drift = analogDrift * params.analogAmount * 0.5f;
        baseFreq *= (1.0f + drift * 0.001f);

        // Mod matrix: pitch modulation
        float pitchMod1 = modState.getDestValue(ModDest::Osc1Pitch);
        float pitchMod2 = modState.getDestValue(ModDest::Osc2Pitch);

        // Set oscillator frequencies
        float freq1 = baseFreq * std::pow(2.0f, pitchMod1 * 2.0f / 12.0f); // +/- 2 semitones per unit
        float freq2 = baseFreq * std::pow(2.0f, (static_cast<float>(params.osc2SemiOffset) + pitchMod2 * 2.0f) / 12.0f);

        osc1.setFrequency(freq1);
        osc1.setWaveform(params.osc1Wave);
        osc1.setPulseWidth(params.osc1PulseWidth + modState.getDestValue(ModDest::Osc1PWM) * 0.4f);
        osc1.setDetune(params.osc1Detune);

        osc2.setFrequency(freq2);
        osc2.setWaveform(params.osc2Wave);
        osc2.setPulseWidth(params.osc2PulseWidth + modState.getDestValue(ModDest::Osc2PWM) * 0.4f);
        osc2.setDetune(params.osc2Detune);

        // Generate oscillator samples
        float osc1Sample = osc1.processSample();
        float osc2Sample = osc2.processSample();
        float osc3Sample = 0.0f;
        float subSample = 0.0f;
        float noiseSample = noiseOsc.processSample() * params.noiseLevel;

        // Mode-specific oscillator processing
        switch (params.mode)
        {
            case SynthMode::Cosmos:
            {
                // Juno-60: Single DCO (saw + pulse simultaneously) + sub-oscillator
                // osc1 = saw, osc2 repurposed as pulse component of same DCO (same freq!)
                osc2.setFrequency(freq1); // Same frequency as osc1 (it's the same DCO)
                osc2.setWaveform(Waveform::Pulse);
                osc2.setPulseWidth(params.osc2PulseWidth + modState.getDestValue(ModDest::Osc2PWM) * 0.4f);
                osc2Sample = osc2.processSample();

                // Sub-oscillator (square, one octave down)
                subOsc.setFrequency(baseFreq);
                subOsc.setWaveform(params.subWave);
                subSample = subOsc.processSample() * params.subLevel;

                // No cross-modulation on Juno (only 1 DCO)
                break;
            }

            case SynthMode::Oracle:
            {
                // Prophet-5 poly-mod: filter env and osc2 modulate osc1 freq and filter cutoff
                if (params.crossMod > 0.0f)
                {
                    float polyMod = (filtVal * 0.5f + osc2Sample * 0.5f) * params.crossMod;
                    osc1.applyFM(polyMod * 0.02f);
                }
                break;
            }

            case SynthMode::Mono:
            {
                // SH-2: 2 VCOs + sub-oscillator
                subOsc.setFrequency(baseFreq);
                subOsc.setWaveform(params.subWave);
                subSample = subOsc.processSample() * params.subLevel;

                // Ring modulation (artistic addition, not on real SH-2)
                if (params.ringMod > 0.0f)
                {
                    float ring = ringModulate(osc1Sample, osc2Sample);
                    osc1Sample = osc1Sample * (1.0f - params.ringMod) + ring * params.ringMod;
                }
                break;
            }

            case SynthMode::Modular:
            {
                // ARP 2600: 3 VCOs + ring mod + hard sync + FM
                float freq3 = baseFreq;
                osc3.setFrequency(freq3);
                osc3.setWaveform(params.osc3Wave);
                osc3Sample = osc3.processSample() * params.osc3Level;

                if (params.hardSync && osc1.didCross())
                    osc2.hardSync();

                if (params.fmAmount > 0.0f)
                    osc2.applyFM(osc1Sample * params.fmAmount * 0.05f);

                if (params.ringMod > 0.0f)
                {
                    float ring = ringModulate(osc1Sample, osc2Sample);
                    osc2Sample = osc2Sample * (1.0f - params.ringMod) + ring * params.ringMod;
                }
                break;
            }
        }

        // Mix oscillators with per-mode normalization
        float mix = 0.0f;
        float activeGain = 0.0f;

        switch (params.mode)
        {
            case SynthMode::Cosmos:
                // Juno: saw level + pulse level + sub
                mix = osc1Sample * params.osc1Level     // Saw component
                    + osc2Sample * params.osc2Level      // Pulse component (same DCO)
                    + subSample + noiseSample;
                activeGain = params.osc1Level + params.osc2Level + params.subLevel + params.noiseLevel;
                break;

            case SynthMode::Oracle:
            case SynthMode::Mono:
                mix = osc1Sample * params.osc1Level
                    + osc2Sample * params.osc2Level
                    + subSample + noiseSample;
                activeGain = params.osc1Level + params.osc2Level + params.subLevel + params.noiseLevel;
                break;

            case SynthMode::Modular:
                mix = osc1Sample * params.osc1Level
                    + osc2Sample * params.osc2Level
                    + osc3Sample + noiseSample;
                activeGain = params.osc1Level + params.osc2Level + params.osc3Level + params.noiseLevel;
                break;
        }

        if (activeGain > 1.0f)
            mix /= activeGain;

        // Filter — clamp cutoff to avoid pushing above Nyquist
        float cutoffMod = modState.getDestValue(ModDest::FilterCutoff);
        float resMod = modState.getDestValue(ModDest::FilterResonance);
        float envModTotal = juce::jlimit(-4.0f, 4.0f, (filtVal * params.filterEnvAmount + cutoffMod) * 4.0f);
        float envCutoff = params.filterCutoff * std::pow(2.0f, envModTotal);
        envCutoff = juce::jlimit(20.0f, static_cast<float>(sr) * 0.45f, envCutoff);
        float envRes = juce::jlimit(0.0f, 1.0f, params.filterResonance + resMod);

        filter.setMode(static_cast<FilterMode>(params.mode));
        filter.setParameters(envCutoff, envRes, params.filterHPCutoff);
        float filtered = filter.process(mix);

        // Amplitude with mod matrix
        float ampMod = 1.0f + modState.getDestValue(ModDest::Amplitude);
        ampMod = juce::jlimit(0.0f, 2.0f, ampMod);

        float output = filtered * ampVal * velocityGain * ampMod;

        // NaN/Inf safety + hard clamp to prevent blowups
        if (std::isnan(output) || std::isinf(output))
            output = 0.0f;
        output = juce::jlimit(-4.0f, 4.0f, output);

        return output;
    }

    // Update modulation state for this voice
    void updateModState(ModulationState& state, const ModMatrix& matrix,
                        float modWheel, float aftertouch, float pitchBend)
    {
        // Set per-voice source values
        state.setSourceValue(ModSource::LFO1, lfo1.processSample());
        state.setSourceValue(ModSource::LFO2, lfo2.processSample());
        state.setSourceValue(ModSource::Envelope2, filtEnv.getCurrentValue());
        state.setSourceValue(ModSource::ModWheel, modWheel);
        state.setSourceValue(ModSource::Aftertouch, aftertouch);
        state.setSourceValue(ModSource::Velocity, currentVelocity);
        state.setSourceValue(ModSource::KeyTracking,
            static_cast<float>(currentNote - 60) / 60.0f); // Centered on middle C
        state.setSourceValue(ModSource::Random, randomPerNote);
        state.setSourceValue(ModSource::PitchBend, pitchBend);

        // Process the matrix
        matrix.process(state);
    }

    void setLFO1Params(LFOShape shape, float rate, float fadeIn, bool oneShot, bool retrigger)
    {
        lfo1.setShape(shape);
        lfo1.setRate(rate);
        lfo1.setFadeIn(fadeIn);
        lfo1.setOneShot(oneShot);
        if (retrigger) lfo1.retrigger();
    }

    void setLFO2Params(LFOShape shape, float rate, float fadeIn, bool oneShot, bool retrigger)
    {
        lfo2.setShape(shape);
        lfo2.setRate(rate);
        lfo2.setFadeIn(fadeIn);
        lfo2.setOneShot(oneShot);
        if (retrigger) lfo2.retrigger();
    }

private:
    double sr = 44100.0;
    int currentNote = -1;
    float currentVelocity = 0.0f;
    float velocityGain = 1.0f;
    float randomPerNote = 0.0f;
    bool active = false;
    bool stealing = false;
    float lastFreq = 0.0f;
    float analogDrift = 0.0f;

    Oscillator osc1, osc2, osc3;
    SubOscillator subOsc;
    Oscillator noiseOsc;
    SynthFilter filter;
    ADSREnvelope ampEnv, filtEnv;
    LFO lfo1, lfo2;

    juce::SmoothedValue<float> portaFreq { 440.0f };
    juce::Random rng;
};

// Voice allocator with steal-quietest strategy
class VoiceAllocator
{
public:
    void prepare(double sampleRate)
    {
        for (auto& v : voices)
            v.prepare(sampleRate);
    }

    void setMaxVoices(int max) { maxVoices = juce::jlimit(1, kMaxPolyphony, max); }

    SynthVoice* noteOn(int note, float velocity, const VoiceParameters& params)
    {
        // Find free voice
        for (int i = 0; i < maxVoices; ++i)
        {
            if (!voices[static_cast<size_t>(i)].isActive())
            {
                voices[static_cast<size_t>(i)].noteOn(note, velocity, params);
                return &voices[static_cast<size_t>(i)];
            }
        }

        // Steal quietest voice
        int quietest = 0;
        float quietestLevel = 999.0f;
        for (int i = 0; i < maxVoices; ++i)
        {
            float level = voices[static_cast<size_t>(i)].getCurrentLevel();
            // Prefer stealing releasing voices
            if (voices[static_cast<size_t>(i)].isReleasing())
                level *= 0.1f;
            if (level < quietestLevel)
            {
                quietestLevel = level;
                quietest = i;
            }
        }

        voices[static_cast<size_t>(quietest)].setSteal();
        voices[static_cast<size_t>(quietest)].noteOn(note, velocity, params);
        return &voices[static_cast<size_t>(quietest)];
    }

    void noteOff(int note)
    {
        for (auto& v : voices)
            if (v.isActive() && v.getCurrentNote() == note)
                v.noteOff();
    }

    void allNotesOff()
    {
        for (auto& v : voices)
            if (v.isActive())
                v.noteOff();
    }

    SynthVoice* getVoice(int index) { return &voices[static_cast<size_t>(index)]; }
    int getMaxVoices() const { return maxVoices; }

    // Render all active voices for one sample, returns stereo
    void renderSample(const VoiceParameters& params, const ModMatrix& modMatrix,
                      const UnisonEngine& unison,
                      float modWheel, float aftertouch, float pitchBend,
                      float& outL, float& outR)
    {
        outL = 0.0f;
        outR = 0.0f;

        int unisonCount = unison.getVoiceCount();

        // Gain compensation: prevent volume explosion from stacking voices
        float voiceGain = 1.0f / std::sqrt(static_cast<float>(maxVoices));

        for (int v = 0; v < maxVoices; ++v)
        {
            auto& voice = voices[static_cast<size_t>(v)];
            if (!voice.isActive()) continue;

            // Update modulation
            ModulationState modState;
            voice.updateModState(modState, modMatrix, modWheel, aftertouch, pitchBend);

            float panMod = modState.getDestValue(ModDest::Pan);

            if (unisonCount <= 1)
            {
                float sample = voice.renderSample(params, modState) * voiceGain;
                // Apply pan
                float angle = (panMod * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
                outL += sample * std::cos(angle);
                outR += sample * std::sin(angle);
            }
            else
            {
                float sample = voice.renderSample(params, modState) * voiceGain;
                float uL, uR;
                float samples[kMaxUnisonVoices];
                for (int u = 0; u < unisonCount; ++u)
                    samples[u] = sample * (1.0f + unison.getVoiceState(u).detuneAmount * 0.0001f);
                unison.mixToStereo(samples, unisonCount, uL, uR);
                outL += uL;
                outR += uR;
            }
        }
    }

private:
    std::array<SynthVoice, kMaxPolyphony> voices;
    int maxVoices = 8;
};

} // namespace MultiSynthDSP

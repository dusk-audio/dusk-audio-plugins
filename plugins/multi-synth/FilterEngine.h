#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Per-mode filter models: each is 4-pole (24dB/oct) matching real hardware.
// Cosmos = IR3109 (Juno), Oracle = CEM3320 (Prophet-5),
// Mono = BA662 OTA (SH-2), Modular = Moog-style ladder (ARP 2600).
namespace MultiSynthDSP
{

enum class FilterMode
{
    Cosmos,   // 4-pole non-self-oscillating LPF + HPF (Juno-60 IR3109)
    Oracle,   // 4-pole self-oscillating LPF (CEM3320)
    Mono,     // 4-pole aggressive LPF (BA662 OTA)
    Modular   // 4-pole Moog-style ladder (ARP 4012)
};

// Simple non-resonant HPF (for Cosmos mode)
class SimpleHPF
{
public:
    void prepare(double sampleRate) { sr = static_cast<float>(sampleRate); reset(); }

    void setCutoff(float cutoffHz)
    {
        float fc = juce::jlimit(10.0f, sr * 0.4f, cutoffHz);
        float wc = juce::MathConstants<float>::twoPi * fc / sr;
        coeff = 1.0f / (1.0f + wc);
    }

    float process(float input)
    {
        float hp = coeff * (prevOutput + input - prevInput);
        prevInput = input;
        prevOutput = hp;
        if (std::isnan(prevOutput)) prevOutput = 0.0f;
        return hp;
    }

    void reset() { prevInput = 0.0f; prevOutput = 0.0f; }

private:
    float sr = 44100.0f;
    float coeff = 1.0f;
    float prevInput = 0.0f;
    float prevOutput = 0.0f;
};

// 4-pole OTA-style cascade filter (base for Cosmos, Oracle, Mono)
// Each pole is: y = s + g * (tanh(in) - s), with configurable per-stage saturation.
class FourPoleOTA
{
public:
    void prepare(double sampleRate)
    {
        sr = static_cast<float>(sampleRate);
        reset();
    }

    void setParameters(float cutoffHz, float resonance, float driveAmount = 1.0f)
    {
        float fc = juce::jlimit(10.0f, sr * 0.45f, cutoffHz);
        g = std::tan(juce::MathConstants<float>::pi * fc / sr);
        g = juce::jlimit(0.0f, 12.0f, g); // Safety clamp

        res = juce::jlimit(0.0f, 1.0f, resonance);
        feedback = res * maxFeedback;
        drive = driveAmount;
    }

    float process(float input)
    {
        // Resonance feedback from 4th pole output
        float fb = std::tanh(s[3] * feedback);
        float in = input * drive - fb;

        // Input saturation (strength varies per filter model)
        in = std::tanh(in * saturationStrength) / saturationStrength;

        // 4 cascaded one-pole lowpass sections
        for (int i = 0; i < 4; ++i)
        {
            float y = s[i] + g * (in - s[i]);
            // Per-stage saturation: gentler for Cosmos, harder for Mono
            s[i] = std::tanh(y * stageNonlinearity) / stageNonlinearity;
            in = s[i];
        }

        // NaN safety
        for (auto& state : s)
            if (std::isnan(state) || std::isinf(state))
                state = 0.0f;

        // Resonance bass compensation: mix back some input to offset bass loss
        float output = s[3] + input * bassComp * res;

        return output;
    }

    void reset()
    {
        for (auto& state : s) state = 0.0f;
    }

    // Tuning knobs for different filter characters
    float maxFeedback = 3.8f;      // <4 = no self-osc, 4+ = self-oscillation
    float saturationStrength = 1.0f; // Input saturation intensity
    float stageNonlinearity = 1.0f;  // Per-stage tanh intensity (1=mild, 2=aggressive)
    float bassComp = 0.0f;          // Resonance bass loss compensation

protected:
    float sr = 44100.0f;
    float g = 0.0f;
    float res = 0.0f;
    float feedback = 0.0f;
    float drive = 1.0f;
    float s[4] = {};
};

//==============================================================================
// Cosmos Filter: 4-pole 24dB/oct LPF + non-resonant HPF (Juno-60 IR3109)
// Character: warm, smooth, non-self-oscillating. Gentle saturation.
class CosmosFilter
{
public:
    void prepare(double sampleRate)
    {
        lpf.prepare(sampleRate);
        hpf.prepare(sampleRate);

        // IR3109 character: gentle, never self-oscillates
        lpf.maxFeedback = 3.0f;        // Cannot self-oscillate
        lpf.saturationStrength = 0.8f;  // Gentle input saturation
        lpf.stageNonlinearity = 0.9f;   // Very mild per-stage warmth
        lpf.bassComp = 0.0f;            // Minimal bass loss at this feedback
    }

    void setParameters(float lpCutoff, float lpResonance, float hpCutoff)
    {
        float clampedRes = juce::jlimit(0.0f, 0.75f, lpResonance);
        lpf.setParameters(lpCutoff, clampedRes);
        hpf.setCutoff(hpCutoff);
    }

    float process(float input)
    {
        float hp = hpf.process(input);
        return lpf.process(hp);
    }

    void reset() { lpf.reset(); hpf.reset(); }

private:
    FourPoleOTA lpf;
    SimpleHPF hpf;
};

//==============================================================================
// Oracle Filter: 4-pole 24dB/oct CEM3320 LPF (Prophet-5)
// Character: punchy, vocal at high resonance, self-oscillates, less bass loss than Moog
class OracleFilter
{
public:
    void prepare(double sampleRate)
    {
        lpf.prepare(sampleRate);

        // CEM3320 character: self-oscillating, vocal, punchy
        lpf.maxFeedback = 4.2f;         // Self-oscillates above ~0.95 resonance
        lpf.saturationStrength = 1.2f;   // Moderate input saturation
        lpf.stageNonlinearity = 1.1f;    // Subtle per-stage character
        lpf.bassComp = 0.15f;            // CEM3320 has less bass loss than Moog
    }

    void setParameters(float cutoffHz, float resonance)
    {
        lpf.setParameters(cutoffHz, resonance);
    }

    float process(float input) { return lpf.process(input); }
    void reset() { lpf.reset(); }

private:
    FourPoleOTA lpf;
};

//==============================================================================
// Mono Filter: 4-pole 24dB/oct BA662 OTA LPF (Roland SH-2)
// Character: massive bass, aggressive, squelchy, sweeps below 50Hz
class MonoFilter
{
public:
    void prepare(double sampleRate)
    {
        lpf.prepare(sampleRate);

        // BA662 character: aggressive, fat, squelchy
        lpf.maxFeedback = 4.0f;         // Can self-oscillate
        lpf.saturationStrength = 1.8f;   // Heavy input drive
        lpf.stageNonlinearity = 1.5f;    // Aggressive per-stage saturation
        lpf.bassComp = 0.2f;             // BA662 retains bass well
    }

    void setParameters(float cutoffHz, float resonance)
    {
        // Allow sweeping below 50Hz (SH-2 famous ability)
        float drive = 1.0f + resonance * 1.5f;
        lpf.setParameters(cutoffHz, resonance, drive);
    }

    float process(float input) { return lpf.process(input); }
    void reset() { lpf.reset(); }

private:
    FourPoleOTA lpf;
};

//==============================================================================
// Modular Filter: 4-pole Moog-style ladder (ARP 2600 4012)
// Character: raw, thick, classic. Uses the existing proven implementation.
class LadderFilter
{
public:
    void prepare(double sampleRate)
    {
        sr = static_cast<float>(sampleRate);
        reset();
    }

    void setParameters(float cutoffHz, float resonance)
    {
        float fc = juce::jlimit(10.0f, sr * 0.45f, cutoffHz);
        float res = juce::jlimit(0.0f, 1.0f, resonance);

        float wc = juce::MathConstants<float>::twoPi * fc / sr;
        wc = juce::jlimit(0.0f, 1.5f, wc);
        g = 0.9892f * wc - 0.4342f * wc * wc + 0.1381f * wc * wc * wc - 0.0202f * wc * wc * wc * wc;
        g = juce::jlimit(0.0f, 0.95f, g);
        feedback = res * 3.8f;
    }

    float process(float input)
    {
        float fb = std::tanh(s[3] * feedback);
        float in = std::tanh(input - fb);

        for (int i = 0; i < 4; ++i)
        {
            float y = s[i] + g * (in - s[i]);
            s[i] = std::tanh(y);
            in = s[i];
        }

        for (auto& state : s)
            if (std::isnan(state) || std::isinf(state))
                state = 0.0f;

        return s[3];
    }

    void reset() { for (auto& state : s) state = 0.0f; }

private:
    float sr = 44100.0f;
    float g = 0.0f;
    float feedback = 0.0f;
    float s[4] = {};
};

//==============================================================================
// Unified filter interface
class SynthFilter
{
public:
    void prepare(double sampleRate)
    {
        cosmosFilter.prepare(sampleRate);
        oracleFilter.prepare(sampleRate);
        monoFilter.prepare(sampleRate);
        ladderFilter.prepare(sampleRate);
    }

    void setMode(FilterMode mode) { currentMode = mode; }

    void setParameters(float cutoff, float resonance, float hpCutoff = 20.0f)
    {
        switch (currentMode)
        {
            case FilterMode::Cosmos:  cosmosFilter.setParameters(cutoff, resonance, hpCutoff); break;
            case FilterMode::Oracle:  oracleFilter.setParameters(cutoff, resonance); break;
            case FilterMode::Mono:    monoFilter.setParameters(cutoff, resonance); break;
            case FilterMode::Modular: ladderFilter.setParameters(cutoff, resonance); break;
        }
    }

    float process(float input)
    {
        switch (currentMode)
        {
            case FilterMode::Cosmos:  return cosmosFilter.process(input);
            case FilterMode::Oracle:  return oracleFilter.process(input);
            case FilterMode::Mono:    return monoFilter.process(input);
            case FilterMode::Modular: return ladderFilter.process(input);
        }
        return input;
    }

    void reset()
    {
        cosmosFilter.reset();
        oracleFilter.reset();
        monoFilter.reset();
        ladderFilter.reset();
    }

private:
    FilterMode currentMode = FilterMode::Cosmos;
    CosmosFilter cosmosFilter;
    OracleFilter oracleFilter;
    MonoFilter monoFilter;
    LadderFilter ladderFilter;
};

} // namespace MultiSynthDSP

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FilterEngine.hpp — per-mode 4-pole (24 dB/oct) filter models.
//
// Framework-free port of the JUCE FilterEngine.h. Every tuning constant
// (maxFeedback / saturationStrength / stageNonlinearity / bassComp per model)
// and the zero-delay one-pole cascade are carried over verbatim so the four
// mode voicings are unchanged. Hardware model names are described generically
// (no third-party trademarks). Prepared at the internal (oversampled) rate.
//
// Phase 2 will add a 3-pole diode-ladder AcidFilter tuning here for the Acid
// mode; Phase 1 ships the four LP models used by modes 0-3.

#pragma once

#include "SynthCommon.hpp"

namespace msynth
{

// Filter voicing selector. Values 0-3 correspond to SynthMode 0-3.
enum class FilterMode
{
    Cosmos = 0, // 4-pole non-self-oscillating LPF + HPF (early-80s Japanese poly)
    Oracle,     // 4-pole self-oscillating LPF (late-70s American poly)
    Mono,       // 4-pole aggressive OTA LPF (70s Japanese mono)
    Modular     // 4-pole transistor-ladder LPF (70s semi-modular)
};

// First-order non-resonant high-pass (Cosmos mode).
class SimpleHPF
{
public:
    void prepare(double sampleRate) noexcept { sr = (float)sampleRate; reset(); }

    void setCutoff(float cutoffHz) noexcept
    {
        const float fc = clampf(cutoffHz, 10.0f, sr * 0.4f);
        const float wc = kTwoPi * fc / sr;
        coeff = 1.0f / (1.0f + wc);
    }

    float process(float input) noexcept
    {
        const float hp = coeff * (prevOutput + input - prevInput);
        prevInput = input;
        prevOutput = hp;
        if (isBad(prevOutput)) prevOutput = 0.0f;
        return hp;
    }

    void reset() noexcept { prevInput = 0.0f; prevOutput = 0.0f; }

private:
    float sr = 44100.0f, coeff = 1.0f, prevInput = 0.0f, prevOutput = 0.0f;
};

// 4-pole OTA-style cascade (base for Cosmos, Oracle, Mono).
class FourPoleOTA
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        dcCoeff = 1.0f - (kTwoPi * 5.0f / sr);
        reset();
    }

    void setParameters(float cutoffHz, float resonance, float driveAmount = 1.0f) noexcept
    {
        // 0.4x-rate ceiling: g = tan(pi*fc/sr) grows unbounded toward Nyquist and
        // the zero-delay one-pole misbehaves at very high cutoff (worst at 1x OS).
        float fc = clampf(cutoffHz, 10.0f, sr * 0.40f);
        g = std::tan(kPi * fc / sr);
        g = clampf(g, 0.0f, 12.0f);
        res = clampf(resonance, 0.0f, 1.0f);
        feedback = res * maxFeedback;
        drive = driveAmount;
    }

    float process(float input) noexcept
    {
        float fb = std::tanh(s[3] * feedback);
        float in = input * drive - fb;
        in = std::tanh(in * saturationStrength) / std::tanh(saturationStrength);

        for (int i = 0; i < 4; ++i)
        {
            const float y = s[i] + g * (in - s[i]);
            s[i] = std::tanh(y * stageNonlinearity) / std::tanh(stageNonlinearity);
            in = s[i];
        }

        for (auto& st : s)
            if (isBad(st)) st = 0.0f;

        const float output = s[3] + input * bassComp * res;

        const float dcOut = output - dcState + dcCoeff * dcPrev;
        dcPrev = dcOut;
        dcState = output;
        return dcOut;
    }

    void reset() noexcept
    {
        for (auto& st : s) st = 0.0f;
        dcState = 0.0f; dcPrev = 0.0f;
    }

    float maxFeedback = 3.8f;
    float saturationStrength = 1.0f;
    float stageNonlinearity = 1.0f;
    float bassComp = 0.0f;

protected:
    float sr = 44100.0f, g = 0.0f, res = 0.0f, feedback = 0.0f, drive = 1.0f;
    float s[4] = {};
    float dcCoeff = 0.999f, dcState = 0.0f, dcPrev = 0.0f;
};

// Warm, non-self-oscillating LPF + HPF (early-80s Japanese poly).
class CosmosFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        lpf.prepare(sampleRate);
        hpf.prepare(sampleRate);
        lpf.maxFeedback = 3.0f;
        lpf.saturationStrength = 0.8f;
        lpf.stageNonlinearity = 0.9f;
        lpf.bassComp = 0.0f;
    }
    void setParameters(float lpCutoff, float lpResonance, float hpCutoff) noexcept
    {
        lpf.setParameters(lpCutoff, clampf(lpResonance, 0.0f, 0.75f));
        hpf.setCutoff(hpCutoff);
    }
    float process(float input) noexcept { return lpf.process(hpf.process(input)); }
    void reset() noexcept { lpf.reset(); hpf.reset(); }
private:
    FourPoleOTA lpf;
    SimpleHPF hpf;
};

// Punchy, self-oscillating LPF, less bass loss (late-70s American poly).
class OracleFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        lpf.prepare(sampleRate);
        lpf.maxFeedback = 4.2f;
        lpf.saturationStrength = 1.2f;
        lpf.stageNonlinearity = 1.1f;
        lpf.bassComp = 0.15f;
    }
    void setParameters(float cutoffHz, float resonance) noexcept { lpf.setParameters(cutoffHz, resonance); }
    float process(float input) noexcept { return lpf.process(input); }
    void reset() noexcept { lpf.reset(); }
private:
    FourPoleOTA lpf;
};

// Fat, driven, squelchy OTA LPF (70s Japanese mono).
class MonoFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        lpf.prepare(sampleRate);
        lpf.maxFeedback = 4.0f;
        lpf.saturationStrength = 1.8f;
        lpf.stageNonlinearity = 1.5f;
        lpf.bassComp = 0.2f;
    }
    void setParameters(float cutoffHz, float resonance) noexcept
    {
        const float drive = 1.0f + resonance * 1.5f;
        lpf.setParameters(cutoffHz, resonance, drive);
    }
    float process(float input) noexcept { return lpf.process(input); }
    void reset() noexcept { lpf.reset(); }
private:
    FourPoleOTA lpf;
};

// Transistor-ladder LPF (70s semi-modular).
class LadderFilter
{
public:
    void prepare(double sampleRate) noexcept { sr = (float)sampleRate; reset(); }

    void setParameters(float cutoffHz, float resonance) noexcept
    {
        const float fc = clampf(cutoffHz, 10.0f, sr * 0.40f); // 0.4x-rate ceiling (see FourPoleOTA)
        const float r  = clampf(resonance, 0.0f, 1.0f);
        float wc = kTwoPi * fc / sr;
        wc = clampf(wc, 0.0f, 1.5f);
        g = 0.9892f * wc - 0.4342f * wc * wc + 0.1381f * wc * wc * wc - 0.0202f * wc * wc * wc * wc;
        g = clampf(g, 0.0f, 0.95f);
        feedback = r * 3.8f;
    }

    float process(float input) noexcept
    {
        const float fb = std::tanh(s[3] * feedback);
        float in = std::tanh(input - fb);
        for (int i = 0; i < 4; ++i)
        {
            const float y = s[i] + g * (in - s[i]);
            s[i] = std::tanh(y);
            in = s[i];
        }
        for (auto& st : s)
            if (isBad(st)) st = 0.0f;
        return s[3];
    }

    void reset() noexcept { for (auto& st : s) st = 0.0f; }

private:
    float sr = 44100.0f, g = 0.0f, feedback = 0.0f;
    float s[4] = {};
};

// Unified per-mode filter dispatch.
class SynthFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        cosmos.prepare(sampleRate);
        oracle.prepare(sampleRate);
        mono.prepare(sampleRate);
        ladder.prepare(sampleRate);
    }

    void setMode(FilterMode m) noexcept { mode = m; }

    // Rate change for oversampling-factor switches. Re-prepares the models
    // (recomputes rate-dependent constants; resets filter state — a brief,
    // acceptable transient. Pitch lives in the oscillators, not here).
    void setSampleRate(double sampleRate) noexcept { prepare(sampleRate); }

    void setParameters(float cutoff, float resonance, float hpCutoff = 20.0f) noexcept
    {
        switch (mode)
        {
            case FilterMode::Cosmos:  cosmos.setParameters(cutoff, resonance, hpCutoff); break;
            case FilterMode::Oracle:  oracle.setParameters(cutoff, resonance); break;
            case FilterMode::Mono:    mono.setParameters(cutoff, resonance); break;
            case FilterMode::Modular: ladder.setParameters(cutoff, resonance); break;
        }
    }

    float process(float input) noexcept
    {
        switch (mode)
        {
            case FilterMode::Cosmos:  return cosmos.process(input);
            case FilterMode::Oracle:  return oracle.process(input);
            case FilterMode::Mono:    return mono.process(input);
            case FilterMode::Modular: return ladder.process(input);
        }
        return input;
    }

    void reset() noexcept { cosmos.reset(); oracle.reset(); mono.reset(); ladder.reset(); }

private:
    FilterMode mode = FilterMode::Cosmos;
    CosmosFilter cosmos;
    OracleFilter oracle;
    MonoFilter mono;
    LadderFilter ladder;
};

} // namespace msynth

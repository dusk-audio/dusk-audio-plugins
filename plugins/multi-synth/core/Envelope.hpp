// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Envelope.hpp — phase-based ADSR (4 curve shapes) and multi-shape LFO.
//
// Framework-free port of the envelope/LFO classes from the JUCE ModMatrix.h.
// Curves, stage logic, fade-in, one-shot and retrigger are all carried over
// verbatim; juce::Random is replaced by the SynthCommon xorshift PRNG. Both are
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

    void noteOn() noexcept  { stage = Stage::Attack; attackPhase = 0.0f; }
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

    float sr = 44100.0f;
    Stage stage = Stage::Idle;
    float currentValue = 0.0f;
    float attackTime = 0.01f, decayTime = 0.1f, sustainLevel = 0.7f, releaseTime = 0.3f;
    float attackPhase = 0.0f, decayPhase = 0.0f, releasePhase = 0.0f, releaseStartLevel = 0.0f;
    EnvelopeCurve curve = EnvelopeCurve::Exponential;
};

enum class LFOShape { Sine = 0, Triangle, Square, SampleAndHold, RandomSmooth };

class LFO
{
public:
    void prepare(double sampleRate) noexcept { sr = (float)sampleRate; phase = 0.0f; }
    void setSampleRate(double sampleRate) noexcept { sr = (float)sampleRate; }

    void setRate(float rateHz) noexcept   { rate = rateHz; }
    void setShape(LFOShape s) noexcept    { shape = s; }
    void setFadeIn(float seconds) noexcept { fadeInTime = maxf(0.0f, seconds); }
    void setOneShot(bool enabled) noexcept { oneShot = enabled; }
    void setTempoSync(bool enabled) noexcept { tempoSync = enabled; }
    void seed(uint32_t s) noexcept { rng.seed(s); }

    void retrigger() noexcept
    {
        phase = 0.0f;
        fadeInPhase = 0.0f;
        completed = false;
        smoothTarget = randomValue();
    }

    float processSample() noexcept
    {
        if (oneShot && completed)
            return 0.0f;

        const float dt = rate / sr;
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
                if (phase < dt) // wrapped (matches original heuristic)
                    currentSHValue = randomValue();
                raw = currentSHValue;
                break;
            case LFOShape::RandomSmooth:
                if (phase + dt >= 1.0f)
                    smoothTarget = randomValue();
                currentSmoothValue += (smoothTarget - currentSmoothValue) * dt * 6.0f;
                raw = currentSmoothValue;
                break;
        }

        phase += dt;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            if (oneShot) completed = true;
        }

        float fadeGain = 1.0f;
        if (fadeInTime > 0.0f && fadeInPhase < 1.0f)
        {
            fadeGain = fadeInPhase;
            fadeInPhase += 1.0f / (fadeInTime * sr);
            if (fadeInPhase > 1.0f) fadeInPhase = 1.0f;
        }

        return raw * fadeGain;
    }

    void reset() noexcept { phase = 0.0f; fadeInPhase = 1.0f; completed = false; currentSHValue = 0.0f; currentSmoothValue = 0.0f; }

private:
    float randomValue() noexcept { return rng.nextBipolar(); }

    float sr = 44100.0f;
    float rate = 1.0f;
    float phase = 0.0f;
    float fadeInTime = 0.0f;
    float fadeInPhase = 1.0f; // 1 = fully faded in
    bool  oneShot = false;
    bool  tempoSync = false;
    bool  completed = false;
    LFOShape shape = LFOShape::Sine;
    float currentSHValue = 0.0f;
    float smoothTarget = 0.0f;
    float currentSmoothValue = 0.0f;
    Xorshift rng;
};

} // namespace msynth

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

// 8-slot modulation matrix routing sources to destinations with bipolar amounts.
namespace MultiSynthDSP
{

enum class ModSource
{
    None = 0,
    LFO1,
    LFO2,
    Envelope2, // Filter envelope
    ModWheel,
    Aftertouch,
    Velocity,
    KeyTracking,
    Random,     // Per-note random value
    PitchBend,
    SampleAndHold, // S&H output (Modular mode)
    NumSources
};

enum class ModDest
{
    None = 0,
    Osc1Pitch,
    Osc2Pitch,
    Osc1PWM,
    Osc2PWM,
    FilterCutoff,
    FilterResonance,
    Amplitude,
    Pan,
    LFO1Rate,
    LFO2Rate,
    EffectsMix,
    UnisonDetune,
    NumDestinations
};

static constexpr int kNumModSlots = 8;
static constexpr int kNumModDests = static_cast<int>(ModDest::NumDestinations);

struct ModSlot
{
    ModSource source = ModSource::None;
    ModDest destination = ModDest::None;
    float amount = 0.0f; // -1.0 to +1.0
};

// Per-voice modulation state: holds current source values and computed destination values
struct ModulationState
{
    // Source values (0..1 or -1..1 depending on source)
    std::array<float, static_cast<int>(ModSource::NumSources)> sourceValues {};

    // Accumulated destination modulation amounts
    std::array<float, kNumModDests> destValues {};

    void clearDestinations()
    {
        destValues.fill(0.0f);
    }

    float getDestValue(ModDest dest) const
    {
        int idx = static_cast<int>(dest);
        if (idx >= 0 && idx < kNumModDests)
            return destValues[static_cast<size_t>(idx)];
        return 0.0f;
    }

    void setSourceValue(ModSource src, float value)
    {
        int idx = static_cast<int>(src);
        if (idx >= 0 && idx < static_cast<int>(ModSource::NumSources))
            sourceValues[static_cast<size_t>(idx)] = value;
    }
};

class ModMatrix
{
public:
    ModSlot& getSlot(int index)
    {
        jassert(index >= 0 && index < kNumModSlots);
        return slots[static_cast<size_t>(index)];
    }

    const ModSlot& getSlot(int index) const
    {
        jassert(index >= 0 && index < kNumModSlots);
        return slots[static_cast<size_t>(index)];
    }

    // Process all slots: read source values, accumulate to destinations
    void process(ModulationState& state) const
    {
        state.clearDestinations();

        for (const auto& slot : slots)
        {
            if (slot.source == ModSource::None || slot.destination == ModDest::None)
                continue;

            float srcVal = state.sourceValues[static_cast<size_t>(slot.source)];
            float modVal = srcVal * slot.amount;

            state.destValues[static_cast<size_t>(slot.destination)] += modVal;
        }
    }

private:
    std::array<ModSlot, kNumModSlots> slots;
};

// ADSR Envelope with adjustable curve shapes
enum class EnvelopeCurve
{
    Linear,
    Exponential,   // Simple x^2 (legacy)
    Logarithmic,   // Simple sqrt(x) (legacy)
    AnalogRC       // Proper RC charging curve: 1-exp(-t/tau) (most authentic)
};

class ADSREnvelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void prepare(double sampleRate)
    {
        sr = static_cast<float>(sampleRate);
    }

    void setParameters(float attack, float decay, float sustain, float release)
    {
        attackTime = juce::jmax(0.001f, attack);
        decayTime = juce::jmax(0.001f, decay);
        sustainLevel = juce::jlimit(0.0f, 1.0f, sustain);
        releaseTime = juce::jmax(0.001f, release);
    }

    void setCurve(EnvelopeCurve c) { curve = c; }

    void noteOn()
    {
        stage = Stage::Attack;
        attackPhase = 0.0f;
    }

    void noteOff()
    {
        if (stage != Stage::Idle)
        {
            stage = Stage::Release;
            releaseStartLevel = currentValue;
            releasePhase = 0.0f;
        }
    }

    float processSample()
    {
        switch (stage)
        {
            case Stage::Idle:
                currentValue = 0.0f;
                break;

            case Stage::Attack:
            {
                attackPhase += 1.0f / (attackTime * sr);
                if (attackPhase >= 1.0f)
                {
                    attackPhase = 1.0f;
                    stage = Stage::Decay;
                    decayPhase = 0.0f;
                }
                currentValue = applyCurve(attackPhase);
                break;
            }

            case Stage::Decay:
            {
                decayPhase += 1.0f / (decayTime * sr);
                if (decayPhase >= 1.0f)
                {
                    decayPhase = 1.0f;
                    stage = Stage::Sustain;
                }
                float decayCurve = 1.0f - applyCurve(decayPhase);
                currentValue = sustainLevel + (1.0f - sustainLevel) * decayCurve;
                break;
            }

            case Stage::Sustain:
                currentValue = sustainLevel;
                break;

            case Stage::Release:
            {
                releasePhase += 1.0f / (releaseTime * sr);
                if (releasePhase >= 1.0f)
                {
                    releasePhase = 1.0f;
                    stage = Stage::Idle;
                }
                float relCurve = 1.0f - applyCurve(releasePhase);
                currentValue = releaseStartLevel * relCurve;
                break;
            }
        }

        return currentValue;
    }

    bool isActive() const { return stage != Stage::Idle; }
    Stage getStage() const { return stage; }
    float getCurrentValue() const { return currentValue; }

private:
    float applyCurve(float linearPhase) const
    {
        switch (curve)
        {
            case EnvelopeCurve::Exponential:
                return linearPhase * linearPhase;
            case EnvelopeCurve::Logarithmic:
                return std::sqrt(linearPhase);
            case EnvelopeCurve::AnalogRC:
            {
                // RC charging: 1 - exp(-t/tau) with tau=1/3 → 95% at phase=1.0
                constexpr float tau = 1.0f / 3.0f;
                return 1.0f - std::exp(-linearPhase / tau);
            }
            case EnvelopeCurve::Linear:
            default:
                return linearPhase;
        }
    }

    float sr = 44100.0f;
    Stage stage = Stage::Idle;
    float currentValue = 0.0f;

    float attackTime = 0.01f;
    float decayTime = 0.1f;
    float sustainLevel = 0.7f;
    float releaseTime = 0.3f;

    float attackPhase = 0.0f;
    float decayPhase = 0.0f;
    float releasePhase = 0.0f;
    float releaseStartLevel = 0.0f;

    EnvelopeCurve curve = EnvelopeCurve::Exponential;
};

// LFO with multiple shapes, sync, fade-in, one-shot, and key retrigger
enum class LFOShape
{
    Sine,
    Triangle,
    Square,
    SampleAndHold,
    RandomSmooth
};

class LFO
{
public:
    void prepare(double sampleRate)
    {
        sr = static_cast<float>(sampleRate);
        phase = 0.0f;
    }

    void setRate(float rateHz) { rate = rateHz; }
    void setShape(LFOShape s) { shape = s; }
    void setFadeIn(float seconds) { fadeInTime = juce::jmax(0.0f, seconds); }
    void setOneShot(bool enabled) { oneShot = enabled; }
    void setTempoSync(bool enabled) { tempoSync = enabled; }

    void retrigger()
    {
        phase = 0.0f;
        fadeInPhase = 0.0f;
        completed = false;
        smoothTarget = randomValue();
    }

    float processSample()
    {
        if (oneShot && completed)
            return 0.0f;

        float dt = rate / sr;
        float raw = 0.0f;

        switch (shape)
        {
            case LFOShape::Sine:
                raw = std::sin(phase * juce::MathConstants<float>::twoPi);
                break;

            case LFOShape::Triangle:
                raw = 2.0f * std::abs(2.0f * (phase - std::floor(phase + 0.5f))) - 1.0f;
                break;

            case LFOShape::Square:
                raw = phase < 0.5f ? 1.0f : -1.0f;
                break;

            case LFOShape::SampleAndHold:
            {
                float prevPhase = phase;
                if (prevPhase > phase + dt || phase < dt) // wrapped
                    currentSHValue = randomValue();
                raw = currentSHValue;
                break;
            }

            case LFOShape::RandomSmooth:
            {
                float prevPhase = phase;
                if (phase + dt >= 1.0f || prevPhase > phase + dt)
                    smoothTarget = randomValue();
                // Smooth interpolation toward target
                currentSmoothValue += (smoothTarget - currentSmoothValue) * dt * 6.0f;
                raw = currentSmoothValue;
                break;
            }
        }

        // Advance phase
        phase += dt;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            if (oneShot)
                completed = true;
        }

        // Apply fade-in
        float fadeGain = 1.0f;
        if (fadeInTime > 0.0f && fadeInPhase < 1.0f)
        {
            fadeGain = fadeInPhase;
            fadeInPhase += 1.0f / (fadeInTime * sr);
            if (fadeInPhase > 1.0f)
                fadeInPhase = 1.0f;
        }

        return raw * fadeGain;
    }

private:
    float randomValue()
    {
        return (static_cast<float>(rng.nextFloat()) * 2.0f - 1.0f);
    }

    float sr = 44100.0f;
    float rate = 1.0f;
    float phase = 0.0f;
    float fadeInTime = 0.0f;
    float fadeInPhase = 1.0f; // 1.0 = fully faded in (no fade). retrigger() resets to 0.0
    bool oneShot = false;
    bool tempoSync = false;
    bool completed = false;
    LFOShape shape = LFOShape::Sine;

    // S&H state
    float currentSHValue = 0.0f;
    // Random smooth state
    float smoothTarget = 0.0f;
    float currentSmoothValue = 0.0f;

    juce::Random rng;
};

} // namespace MultiSynthDSP

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <random>

// Band-limited oscillator engine using polyBLEP antialiasing.
// Supports all waveforms needed by the four synth modes.
namespace MultiSynthDSP
{

enum class Waveform
{
    Saw,
    Square,
    Triangle,
    Sine,
    Pulse,   // Square with variable pulse width
    Noise    // White noise
};

// PolyBLEP residual for antialiased waveforms
inline float polyBlep(float t, float dt)
{
    if (t < dt)
    {
        float n = t / dt;
        return n + n - n * n - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        float n = (t - 1.0f) / dt;
        return n * n + n + n + 1.0f;
    }
    return 0.0f;
}

class Oscillator
{
public:
    void prepare(double sampleRate)
    {
        sr = static_cast<float>(sampleRate);
        phase = 0.0f;
        syncPhase = 0.0f;
    }

    void setFrequency(float freqHz)
    {
        freq = freqHz;
        dt = freq / sr;
    }

    void setWaveform(Waveform w) { waveform = w; }

    void setPulseWidth(float pw) { pulseWidth = juce::jlimit(0.05f, 0.95f, pw); }

    void setDetune(float cents) { detuneRatio = std::pow(2.0f, cents / 1200.0f); }

    // Hard sync: reset phase when sync source completes a cycle
    void hardSync() { phase = 0.0f; }

    // Get current phase for sync detection
    float getPhase() const { return phase; }

    float processSample()
    {
        float effectiveDt = dt * detuneRatio;
        float sample = 0.0f;

        switch (waveform)
        {
            case Waveform::Saw:
                sample = 2.0f * phase - 1.0f;
                sample -= polyBlep(phase, effectiveDt);
                break;

            case Waveform::Square:
                sample = phase < 0.5f ? 1.0f : -1.0f;
                sample += polyBlep(phase, effectiveDt);
                sample -= polyBlep(std::fmod(phase + 0.5f, 1.0f), effectiveDt);
                break;

            case Waveform::Pulse:
                sample = phase < pulseWidth ? 1.0f : -1.0f;
                sample += polyBlep(phase, effectiveDt);
                sample -= polyBlep(std::fmod(phase + (1.0f - pulseWidth), 1.0f), effectiveDt);
                break;

            case Waveform::Triangle:
            {
                // Integrated square -> triangle (leaky integrator for band-limiting)
                float sq = phase < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep(phase, effectiveDt);
                sq -= polyBlep(std::fmod(phase + 0.5f, 1.0f), effectiveDt);
                // Leaky integrator
                triState = 0.999f * triState + effectiveDt * sq * 4.0f;
                sample = triState;
                break;
            }

            case Waveform::Sine:
                sample = std::sin(phase * juce::MathConstants<float>::twoPi);
                break;

            case Waveform::Noise:
                sample = noiseDistribution(noiseGen);
                break;
        }

        // Advance phase
        float prevPhase = phase;
        phase += effectiveDt;
        if (phase >= 1.0f)
            phase -= 1.0f;

        // Detect zero crossing for sync output
        lastCrossed = (prevPhase > phase); // wrapped around

        return sample;
    }

    // Did the oscillator just complete a cycle? (for hard sync)
    bool didCross() const { return lastCrossed; }

    void resetPhase() { phase = 0.0f; triState = 0.0f; }

    // FM: add to phase increment
    void applyFM(float fmAmount)
    {
        phase += fmAmount;
        while (phase < 0.0f) phase += 1.0f;
        while (phase >= 1.0f) phase -= 1.0f;
    }

private:
    float sr = 44100.0f;
    float freq = 440.0f;
    float dt = 0.01f;
    float phase = 0.0f;
    float syncPhase = 0.0f;
    float pulseWidth = 0.5f;
    float detuneRatio = 1.0f;
    float triState = 0.0f;
    bool lastCrossed = false;
    Waveform waveform = Waveform::Saw;

    std::mt19937 noiseGen { std::random_device{}() };
    std::uniform_real_distribution<float> noiseDistribution { -1.0f, 1.0f };
};

// Sub-oscillator: one octave down, typically square or sine
class SubOscillator
{
public:
    void prepare(double sampleRate) { osc.prepare(sampleRate); }

    void setFrequency(float freqHz)
    {
        osc.setFrequency(freqHz * 0.5f); // One octave down
    }

    void setWaveform(Waveform w) { osc.setWaveform(w); }

    float processSample() { return osc.processSample(); }

    void resetPhase() { osc.resetPhase(); }

private:
    Oscillator osc;
};

// Ring modulator: multiplies two signals
inline float ringModulate(float a, float b)
{
    return a * b;
}

// Cross-modulation: one osc modulates another's frequency
inline float crossModulate(float modSignal, float amount)
{
    return modSignal * amount;
}

// Sample & Hold: holds a value until triggered
class SampleAndHold
{
public:
    void prepare(double sampleRate)
    {
        sr = static_cast<float>(sampleRate);
        phase = 0.0f;
    }

    void setRate(float rateHz)
    {
        dt = rateHz / sr;
    }

    float process(float noiseInput)
    {
        phase += dt;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            heldValue = noiseInput;
        }
        return heldValue;
    }

    void reset()
    {
        phase = 0.0f;
        heldValue = 0.0f;
    }

private:
    float sr = 44100.0f;
    float dt = 0.01f;
    float phase = 0.0f;
    float heldValue = 0.0f;
};

} // namespace MultiSynthDSP

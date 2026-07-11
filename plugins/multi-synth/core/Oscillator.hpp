// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Oscillator.hpp — band-limited (polyBLEP) oscillators, sub-oscillator, ring/
// cross modulation helpers, sample & hold, and a pink-noise generator.
//
// Framework-free port of the JUCE OscillatorEngine.h. Every waveform formula is
// carried over verbatim; substantive changes are (1) noise uses a per-instance
// xorshift PRNG (SynthCommon) instead of std::mt19937/random_device, and (2) the
// polyBLEP trailing-edge sign was corrected (the JUCE original's -1 form was a
// bug; see polyBlep). Prepared at the INTERNAL (oversampled) rate by the
// voice — dt = freq / internalRate — so pitch is correct at every OS factor.

#pragma once

#include "SynthCommon.hpp"

namespace msynth
{

enum class Waveform
{
    Saw = 0,
    Square,
    Triangle,
    Sine,
    Pulse,   // square with variable pulse width
    Noise    // white noise
};

// PolyBLEP residual for antialiased edges (adapted from the JUCE original;
// the trailing-edge sign was fixed here — the original returned n*n+n+n-1,
// the corrupted form inherited verbatim, instead of the canonical (n+1)^2).
inline float polyBlep(float t, float dt) noexcept
{
    if (t < dt)
    {
        const float n = t / dt;
        return n + n - n * n - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        const float n = (t - 1.0f) / dt;
        return n * n + n + n + 1.0f;
    }
    return 0.0f;
}

class Oscillator
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        phase = 0.0f;
        triState = 0.0f;
    }

    void setFrequency(float freqHz) noexcept { freq = freqHz; dt = freq / sr; }
    // Change rate WITHOUT resetting phase (for oversampling-factor changes).
    void setSampleRate(double sampleRate) noexcept { sr = (float)sampleRate; dt = freq / sr; }
    void setWaveform(Waveform w) noexcept    { waveform = w; }
    void setPulseWidth(float pw) noexcept    { pulseWidth = clampf(pw, 0.05f, 0.95f); }
    void setDetune(float cents) noexcept     { detuneRatio = std::pow(2.0f, cents / 1200.0f); }

    void hardSync() noexcept   { phase = 0.0f; triState = 0.0f; }
    float getPhase() const noexcept { return phase; }
    bool  didCross() const noexcept { return lastCrossed; }
    void  resetPhase() noexcept { phase = 0.0f; triState = 0.0f; }

    void seedNoise(uint32_t s) noexcept { rng.seed(s); }

    // FM: add to phase (through-zero-ish, wraps).
    void applyFM(float fmAmount) noexcept
    {
        phase += fmAmount;
        while (phase < 0.0f)  phase += 1.0f;
        while (phase >= 1.0f) phase -= 1.0f;
    }

    float processSample() noexcept
    {
        const float effectiveDt = dt * detuneRatio;
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
                float sq = phase < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep(phase, effectiveDt);
                sq -= polyBlep(std::fmod(phase + 0.5f, 1.0f), effectiveDt);
                triState = 0.999f * triState + effectiveDt * sq * 4.0f; // leaky integrator
                sample = triState;
                break;
            }

            case Waveform::Sine:
                sample = std::sin(phase * kTwoPi);
                break;

            case Waveform::Noise:
                sample = rng.nextBipolar();
                break;
        }

        const float prevPhase = phase;
        phase += effectiveDt;
        // Full wrap via floor handles pathological dt > 1 (extreme freq/OS);
        // for the normal dt < 1 case this is identical to a single subtraction
        // (floor(phase) is exactly 1 when 1 <= phase < 2). (C3)
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        lastCrossed = (prevPhase > phase); // wrapped at least once this sample

        return sample;
    }

private:
    float sr = 44100.0f;
    float freq = 440.0f;
    float dt = 0.01f;
    float phase = 0.0f;
    float pulseWidth = 0.5f;
    float detuneRatio = 1.0f;
    float triState = 0.0f;
    bool  lastCrossed = false;
    Waveform waveform = Waveform::Saw;
    Xorshift rng;
};

// Sub-oscillator: one octave below the note.
class SubOscillator
{
public:
    void prepare(double sampleRate) noexcept { osc.prepare(sampleRate); }
    void setSampleRate(double sampleRate) noexcept { osc.setSampleRate(sampleRate); }
    void setFrequency(float freqHz) noexcept { osc.setFrequency(freqHz * 0.5f); }
    void setWaveform(Waveform w) noexcept    { osc.setWaveform(w); }
    void setDetune(float cents) noexcept     { osc.setDetune(cents); }
    float processSample() noexcept           { return osc.processSample(); }
    void resetPhase() noexcept               { osc.resetPhase(); }
    void seedNoise(uint32_t s) noexcept      { osc.seedNoise(s); }

private:
    Oscillator osc;
};

inline float ringModulate(float a, float b) noexcept { return a * b; }

// Sample & Hold: latches its input each time the phase accumulator wraps.
class SampleAndHold
{
public:
    void prepare(double sampleRate) noexcept { sr = (float)sampleRate; phase = 0.0f; }
    // Recompute dt from the stored rate so a sample-rate change (oversampling
    // switch) keeps the same S&H rate in Hz.
    void setSampleRate(double sampleRate) noexcept { sr = (float)sampleRate; dt = rate / sr; }
    void setRate(float rateHz) noexcept      { rate = rateHz; dt = rate / sr; }

    float process(float noiseInput) noexcept
    {
        phase += dt;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            heldValue = noiseInput;
        }
        return heldValue;
    }

    void reset() noexcept { phase = 0.0f; heldValue = 0.0f; }

private:
    float sr = 44100.0f;
    float rate = 1.0f;
    float dt = 0.01f;
    float phase = 0.0f;
    float heldValue = 0.0f;
};

// Pink noise — Paul Kellet's refined 7-coefficient method.
class PinkNoiseGenerator
{
public:
    void seed(uint32_t s) noexcept { rng.seed(s); }

    float processSample() noexcept
    {
        const float white = rng.nextBipolar();
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;
        const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;
        return pink * 0.11f;
    }

    void reset() noexcept { b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f; }

private:
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    Xorshift rng;
};

} // namespace msynth

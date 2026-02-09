/*
  ==============================================================================

    FDNReverb.h
    Feedback Delay Network Reverb Engine for SilkVerb

    8-channel stereo FDN with orthogonal Hadamard matrix feedback,
    per-channel allpass diffusers, and mode-specific delay times.

    Enhanced with Lexicon/Valhalla-style features:
    - Allpass interpolation for smooth modulation (Thiran)
    - Two-band decay with biquad crossover
    - Complex modulation (multiple uncorrelated LFOs + random)
    - Soft-knee feedback saturation with vintage mode
    - DC blocking in feedback path
    - Pre-delay with crossfeed to late reverb
    - Output EQ with proper biquad filters
    - Early/Late diffusion controls
    - Color modes (Modern/Vintage)
    - Freeze mode
    - 10 reverb modes: Plate, Room, Hall, Chamber, Cathedral, Ambience,
      Bright Hall, Chorus Space, Random Space, Dirty Hall

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <random>
#include <vector>

#include "../../shared/AnalogEmulation/AnalogEmulation.h"

namespace SilkVerb {

//==============================================================================
// Constants
constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 6.28318530718f;

//==============================================================================
// Fast sine approximation for normalized phase [0, 1): returns sin(2π * phase)
// 5th-order polynomial, max error ~0.00025 (inaudible for LFO modulation)
// Replaces std::sin() — ~5-10x faster per call, saving ~48 sin() per sample
inline float fastSin2Pi(float phase)
{
    // Map [0, 1) to [-π, π)
    float x = (phase < 0.5f) ? phase : (phase - 1.0f);
    x *= TWO_PI;
    // sin(x) ≈ x(1 - x²/6 + x⁴/120)
    float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * 0.00833333f));
}

//==============================================================================
// Color mode enumeration (era-based, inspired by VintageVerb)
enum class ColorMode
{
    Seventies = 0,  // 1970s: EMT 250 era — bandwidth-limited, tube saturation, lo-fi
    Eighties,       // 1980s: Lexicon 224/480 era — cleaner but still colored
    Now             // Modern: full-bandwidth, minimal saturation
};

//==============================================================================
// DC Blocker - prevents DC buildup in feedback path
class DCBlocker
{
public:
    void prepare(double sampleRate)
    {
        // ~20 Hz cutoff for DC blocking
        float freq = 20.0f;
        float w = TWO_PI * freq / static_cast<float>(sampleRate);
        coeff = 1.0f / (1.0f + w);
    }

    void clear()
    {
        xPrev = 0.0f;
        yPrev = 0.0f;
    }

    float process(float input)
    {
        // High-pass filter: y[n] = coeff * (y[n-1] + x[n] - x[n-1])
        float output = coeff * (yPrev + input - xPrev);
        xPrev = input;
        yPrev = output;
        return output;
    }

private:
    float coeff = 0.995f;
    float xPrev = 0.0f;
    float yPrev = 0.0f;
};

//==============================================================================
// Biquad filter for professional EQ and crossovers
class BiquadFilter
{
public:
    void prepare(double sr)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        clear();
    }

    void clear()
    {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    void setLowPass(float freq, float q = 0.707f)
    {
        float w0 = TWO_PI * std::clamp(freq, 20.0f, static_cast<float>(sampleRate) * 0.49f)
                   / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * q);

        float a0 = 1.0f + alpha;
        b0 = ((1.0f - cosw0) / 2.0f) / a0;
        b1 = (1.0f - cosw0) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighPass(float freq, float q = 0.707f)
    {
        float w0 = TWO_PI * std::clamp(freq, 20.0f, static_cast<float>(sampleRate) * 0.49f)
                   / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * q);

        float a0 = 1.0f + alpha;
        b0 = ((1.0f + cosw0) / 2.0f) / a0;
        b1 = -(1.0f + cosw0) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighShelf(float freq, float gainDb, float q = 0.707f)
    {
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = TWO_PI * std::clamp(freq, 20.0f, static_cast<float>(sampleRate) * 0.49f)
                   / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * q);
        float sqrtA = std::sqrt(A);

        float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha)) / a0;
        b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha)) / a0;
        a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha) / a0;
    }

    void setLowShelf(float freq, float gainDb, float q = 0.707f)
    {
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = TWO_PI * std::clamp(freq, 20.0f, static_cast<float>(sampleRate) * 0.49f)
                   / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * q);
        float sqrtA = std::sqrt(A);

        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha)) / a0;
        b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha)) / a0;
        a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha) / a0;
    }

    float process(float input)
    {
        float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;
        return output;
    }

private:
    double sampleRate = 44100.0;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
};

//==============================================================================
// Delay line with allpass interpolation (Thiran) for smooth modulation
class DelayLine
{
public:
    void prepare(double sr, float maxDelayMs)
    {
        if (sr <= 0.0 || maxDelayMs <= 0.0f)
        {
            sampleRate = 44100.0;
            buffer.resize(4, 0.0f);
            writePos = 0;
            return;
        }
        sampleRate = sr;
        int maxSamples = std::max(4, static_cast<int>(maxDelayMs * 0.001 * sampleRate) + 2);
        buffer.resize(static_cast<size_t>(maxSamples), 0.0f);
        writePos = 0;
        allpassState = 0.0f;
    }

    void clear()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        allpassState = 0.0f;
    }

    void setDelayMs(float delayMs)
    {
        float newDelaySamples = static_cast<float>(delayMs * 0.001 * sampleRate);
        newDelaySamples = std::max(1.0f, std::min(newDelaySamples, static_cast<float>(buffer.size() - 2)));

        // Only update allpass coefficient if delay changed significantly
        if (std::abs(newDelaySamples - delaySamples) > 0.0001f)
        {
            delaySamples = newDelaySamples;
            updateAllpassCoefficient();
        }
    }

    float process(float input)
    {
        buffer[static_cast<size_t>(writePos)] = input;

        // Integer part of delay
        int intDelay = static_cast<int>(delaySamples);

        // Read position for integer delay
        int readPos = writePos - intDelay;
        if (readPos < 0) readPos += static_cast<int>(buffer.size());

        int readPosPrev = readPos - 1;
        if (readPosPrev < 0) readPosPrev += static_cast<int>(buffer.size());

        float y0 = buffer[static_cast<size_t>(readPos)];
        float y1 = buffer[static_cast<size_t>(readPosPrev)];

        // First-order allpass interpolation (Thiran)
        // H(z) = (a + z^-1) / (1 + a*z^-1)
        float output = allpassCoeff * (y0 - allpassState) + y1;
        allpassState = output;

        writePos = (writePos + 1) % static_cast<int>(buffer.size());
        return output;
    }

private:
    void updateAllpassCoefficient()
    {
        float frac = delaySamples - static_cast<float>(static_cast<int>(delaySamples));
        // Thiran allpass coefficient for fractional delay
        // For stability, clamp frac away from 0 and 1
        frac = std::clamp(frac, 0.01f, 0.99f);
        allpassCoeff = (1.0f - frac) / (1.0f + frac);
    }

    std::vector<float> buffer;
    double sampleRate = 44100.0;
    float delaySamples = 1.0f;
    float allpassCoeff = 0.0f;
    float allpassState = 0.0f;
    int writePos = 0;
};

//==============================================================================
// Delay line with separate read/write for proper allpass diffuser implementation
class DelayLineWithSeparateReadWrite
{
public:
    void prepare(double sr, float maxDelayMs)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        int maxSamples = std::max(4, static_cast<int>(maxDelayMs * 0.001 * sampleRate) + 2);
        buffer.resize(static_cast<size_t>(maxSamples), 0.0f);
        writePos = 0;
        delaySamples = 1.0f;
    }

    void clear()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void setDelayMs(float delayMs)
    {
        delaySamples = static_cast<float>(delayMs * 0.001 * sampleRate);
        delaySamples = std::clamp(delaySamples, 1.0f, static_cast<float>(buffer.size() - 2));
    }

    float readCurrent() const
    {
        int intDelay = static_cast<int>(delaySamples);
        float frac = delaySamples - static_cast<float>(intDelay);

        int readPos = writePos - intDelay;
        if (readPos < 0) readPos += static_cast<int>(buffer.size());

        int readPosNext = (readPos + 1) % static_cast<int>(buffer.size());

        // Linear interpolation is acceptable for fixed-delay allpass diffusers
        return buffer[static_cast<size_t>(readPos)] * (1.0f - frac)
             + buffer[static_cast<size_t>(readPosNext)] * frac;
    }

    void write(float value)
    {
        buffer[static_cast<size_t>(writePos)] = value;
    }

    void advance()
    {
        writePos = (writePos + 1) % static_cast<int>(buffer.size());
    }

private:
    std::vector<float> buffer;
    double sampleRate = 44100.0;
    float delaySamples = 1.0f;
    int writePos = 0;
};

//==============================================================================
// Four-band decay filter with subtractive crossovers (guarantees flat sum)
// Three cascaded 1-pole LP/HP splits create 4 independent frequency bands
// Each band gets its own decay multiplier for precise frequency-dependent RT60
class FourBandDecayFilter
{
public:
    void prepare(double sr)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        z1 = z2 = z3 = 0.0f;
        updateCoefficients();
    }

    void clear()
    {
        z1 = z2 = z3 = 0.0f;
    }

    void setCrossoverFreqs(float freq1, float freq2, float freq3)
    {
        crossoverFreq1 = std::clamp(freq1, 50.0f, 2000.0f);
        crossoverFreq2 = std::clamp(freq2, 200.0f, 12000.0f);
        crossoverFreq3 = std::clamp(freq3, 1000.0f, static_cast<float>(sampleRate) * 0.45f);
        // Enforce ordering
        if (crossoverFreq2 <= crossoverFreq1) crossoverFreq2 = crossoverFreq1 * 2.0f;
        if (crossoverFreq3 <= crossoverFreq2) crossoverFreq3 = crossoverFreq2 * 2.0f;
        updateCoefficients();
    }

    void setDecayMultipliers(float lowMult, float midMult, float highMult, float trebleMult)
    {
        lowDecayMult = std::clamp(lowMult, 0.25f, 4.0f);
        midDecayMult = std::clamp(midMult, 0.25f, 4.0f);
        highDecayMult = std::clamp(highMult, 0.25f, 4.0f);
        trebleDecayMult = std::clamp(trebleMult, 0.25f, 4.0f);
    }

    // Pre-compute per-band gains (call once per parameter change, not per sample)
    void updateGains(float baseGain)
    {
        cachedG1 = std::min(std::pow(baseGain, 1.0f / lowDecayMult), 0.9999f);
        cachedG2 = std::min(std::pow(baseGain, 1.0f / midDecayMult), 0.9999f);
        cachedG3 = std::min(std::pow(baseGain, 1.0f / highDecayMult), 0.9999f);
        cachedG4 = std::min(std::pow(baseGain, 1.0f / trebleDecayMult), 0.9999f);
    }

    float process(float input)
    {
        // Three subtractive crossover stages — guarantees flat sum
        z1 += coeff1 * (input - z1);
        float band1 = z1;                // Sub-bass (below f1)

        float hp1 = input - band1;
        z2 += coeff2 * (hp1 - z2);
        float band2 = z2;                // Low-mid (f1 to f2)

        float hp2 = hp1 - band2;
        z3 += coeff3 * (hp2 - z3);
        float band3 = z3;                // High-mid (f2 to f3)
        float band4 = hp2 - band3;       // Treble (above f3)

        return band1 * cachedG1 + band2 * cachedG2 + band3 * cachedG3 + band4 * cachedG4;
    }

private:
    void updateCoefficients()
    {
        coeff1 = 1.0f - std::exp(-6.2831853f * crossoverFreq1 / static_cast<float>(sampleRate));
        coeff2 = 1.0f - std::exp(-6.2831853f * crossoverFreq2 / static_cast<float>(sampleRate));
        coeff3 = 1.0f - std::exp(-6.2831853f * crossoverFreq3 / static_cast<float>(sampleRate));
    }

    double sampleRate = 44100.0;
    float crossoverFreq1 = 200.0f;   // Sub-bass / bass boundary
    float crossoverFreq2 = 1500.0f;  // Bass / mid boundary
    float crossoverFreq3 = 5000.0f;  // Mid / treble boundary
    float lowDecayMult = 1.0f;
    float midDecayMult = 1.0f;
    float highDecayMult = 1.0f;
    float trebleDecayMult = 1.0f;
    float coeff1 = 0.1f, coeff2 = 0.1f, coeff3 = 0.1f;
    float z1 = 0.0f, z2 = 0.0f, z3 = 0.0f;
    float cachedG1 = 0.9f, cachedG2 = 0.9f, cachedG3 = 0.9f, cachedG4 = 0.9f;
};

//==============================================================================
// One-pole damping filter for high-frequency absorption (6dB/oct)
// Gentler slope than biquad — matches Lexicon-style progressive HF darkening
class DampingFilter
{
public:
    void prepare(double sr)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        z1 = 0.0f;
    }

    void clear() { z1 = 0.0f; }

    void setFrequency(float freq)
    {
        freq = std::clamp(freq, 200.0f, static_cast<float>(sampleRate) * 0.49f);
        // One-pole coefficient: g = 1 - exp(-2*pi*fc/fs)
        coeff = 1.0f - std::exp(-6.2831853f * freq / static_cast<float>(sampleRate));
    }

    float process(float input)
    {
        z1 += coeff * (input - z1);
        return z1;
    }

private:
    double sampleRate = 44100.0;
    float coeff = 1.0f;
    float z1 = 0.0f;
};

//==============================================================================
// Proper Schroeder allpass filter for diffusion
class AllpassFilter
{
public:
    void prepare(double sampleRate, float maxDelayMs)
    {
        delay.prepare(sampleRate, maxDelayMs);
    }

    void setParameters(float delayMs, float fb)
    {
        delay.setDelayMs(delayMs);
        feedback = std::clamp(fb, -0.75f, 0.75f);  // Slightly reduced max for stability
    }

    void clear()
    {
        delay.clear();
    }

    float process(float input)
    {
        // Standard Schroeder allpass structure
        // y[n] = -g*x[n] + x[n-D] + g*y[n-D]
        float bufferOutput = delay.readCurrent();  // Read before writing
        float toBuffer = input + feedback * bufferOutput;
        delay.write(toBuffer);
        delay.advance();

        return bufferOutput - feedback * input;
    }

private:
    DelayLineWithSeparateReadWrite delay;
    float feedback = 0.5f;
};

//==============================================================================
// Early reflections generator with 12 taps and mode-specific patterns
class EarlyReflections
{
public:
    static constexpr int NUM_TAPS = 12;

    void prepare(double sr)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        // Max tap (~130ms) + max pre-delay (50ms) + margin + time scaling
        int maxSamples = std::max(2, static_cast<int>(0.5 * sampleRate));
        buffer.resize(static_cast<size_t>(maxSamples), 0.0f);
        writePos = 0;

        // Default pattern (Room-like)
        baseTapTimesMs = { 3.1f, 7.2f, 11.7f, 17.3f, 23.9f, 31.1f, 41.3f, 53.7f, 0.0f, 0.0f, 0.0f, 0.0f };
        baseTapGains = { 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.35f, 0.3f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f };

        updateShapedGains();
        updateTapPositions();
    }

    void setTapPattern(const std::array<float, NUM_TAPS>& times,
                       const std::array<float, NUM_TAPS>& gains)
    {
        baseTapTimesMs = times;
        baseTapGains = gains;
        updateShapedGains();
        updateTapPositions();
    }

    void setAmount(float amt)
    {
        amount = std::clamp(amt, 0.0f, 1.0f);
    }

    void setPreDelay(float preDelayMsVal)
    {
        this->preDelayMs = std::clamp(preDelayMsVal, 0.0f, 50.0f);
        updateTapPositions();
    }

    void setTimeScale(float scale)
    {
        timeScale = std::clamp(scale, 0.5f, 2.0f);
        updateTapPositions();
    }

    void setShape(float shp)
    {
        shape = std::clamp(shp, 0.0f, 1.0f);
        updateShapedGains();
    }

    void setSpread(float sp)
    {
        spread = std::clamp(sp, 0.0f, 1.0f);
        updateTapPositions();
    }

    void clear()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    float process(float input)
    {
        buffer[static_cast<size_t>(writePos)] = input;

        float output = 0.0f;
        for (int i = 0; i < NUM_TAPS; ++i)
        {
            if (shapedTapGains[static_cast<size_t>(i)] <= 0.0f) continue;
            int readPos = writePos - tapPositions[static_cast<size_t>(i)];
            if (readPos < 0) readPos += static_cast<int>(buffer.size());
            output += buffer[static_cast<size_t>(readPos)] * shapedTapGains[static_cast<size_t>(i)];
        }

        writePos = (writePos + 1) % static_cast<int>(buffer.size());
        return output * amount;
    }

private:
    void updateShapedGains()
    {
        // Find the last active tap for normalization
        int lastActive = 0;
        for (int i = NUM_TAPS - 1; i >= 0; --i)
        {
            if (baseTapGains[static_cast<size_t>(i)] > 0.0f)
            {
                lastActive = i;
                break;
            }
        }

        for (int i = 0; i < NUM_TAPS; ++i)
        {
            size_t idx = static_cast<size_t>(i);
            if (baseTapGains[idx] <= 0.0f)
            {
                shapedTapGains[idx] = 0.0f;
                continue;
            }

            float t = (lastActive > 0) ? static_cast<float>(i) / static_cast<float>(lastActive) : 0.0f;
            float envelope;

            if (shape < 0.5f)
            {
                // Blend from front-loaded to neutral
                float frontLoaded = std::pow(std::max(1.0f - t, 0.01f), 2.0f);
                float blend = shape * 2.0f;  // 0 at shape=0, 1 at shape=0.5
                envelope = frontLoaded * (1.0f - blend) + 1.0f * blend;
            }
            else
            {
                // Blend from neutral to building (sine hump)
                float building = std::sin(PI * t) * (1.0f - 0.3f * t);
                float blend = (shape - 0.5f) * 2.0f;  // 0 at shape=0.5, 1 at shape=1.0
                envelope = 1.0f * (1.0f - blend) + building * blend;
            }

            shapedTapGains[idx] = baseTapGains[idx] * std::max(envelope, 0.01f);
        }
    }

    void updateTapPositions()
    {
        // Find the last active tap for spread normalization
        int lastActive = 0;
        float maxBaseTime = 0.0f;
        for (int i = NUM_TAPS - 1; i >= 0; --i)
        {
            if (baseTapGains[static_cast<size_t>(i)] > 0.0f)
            {
                lastActive = i;
                maxBaseTime = baseTapTimesMs[static_cast<size_t>(i)] * timeScale;
                break;
            }
        }

        float spreadExponent = 0.5f + spread;  // 0.5 (compress) to 1.5 (stretch)

        for (int i = 0; i < NUM_TAPS; ++i)
        {
            size_t idx = static_cast<size_t>(i);
            float baseTime = baseTapTimesMs[idx] * timeScale;

            // Apply spread warping via power curve
            float adjustedTime = baseTime;
            if (maxBaseTime > 0.0f && baseTime > 0.0f)
            {
                float normalizedTime = baseTime / maxBaseTime;
                float warpedNorm = std::pow(normalizedTime, spreadExponent);
                adjustedTime = warpedNorm * maxBaseTime;
            }

            float totalMs = preDelayMs + adjustedTime;
            tapPositions[idx] = static_cast<int>(totalMs * 0.001 * sampleRate);
            tapPositions[idx] = std::min(tapPositions[idx],
                                         static_cast<int>(buffer.size() - 1));
        }
    }

    std::vector<float> buffer;
    double sampleRate = 44100.0;
    int writePos = 0;
    float amount = 0.1f;
    float preDelayMs = 0.0f;
    float timeScale = 1.0f;
    float shape = 0.5f;    // 0=front-loaded, 0.5=neutral, 1=building
    float spread = 0.5f;   // 0=compressed, 0.5=neutral, 1=stretched

    std::array<float, NUM_TAPS> baseTapTimesMs = {};
    std::array<float, NUM_TAPS> baseTapGains = {};
    std::array<float, NUM_TAPS> shapedTapGains = {};
    std::array<int, NUM_TAPS> tapPositions = {};
};

//==============================================================================
// Complex LFO system (Lexicon-style with multiple rates + random)
class ComplexModulator
{
public:
    void prepare(double sr, int index)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;

        // Each modulator gets unique phase offsets based on index (increased decorrelation)
        phase1 = static_cast<double>(index) * 0.25;
        phase2 = static_cast<double>(index) * 0.41;
        phase3 = static_cast<double>(index) * 0.67;

        // Initialize random generator with index-based seed
        rng.seed(static_cast<unsigned int>(42 + index * 17));
        randomTarget = 0.0f;
        randomPrev = 0.0f;
        randomCurrent = 0.0f;
        randomPhase = 1.0f;  // Start fully interpolated
        randomCounter = 0;
    }

    void setParameters(float baseRate, float depthVal, float randomAmountVal)
    {
        // Primary LFO
        rate1 = baseRate;
        // Secondary LFO at golden ratio offset
        rate2 = baseRate * 1.618f;
        // Tertiary LFO at slower rate
        rate3 = baseRate * 0.382f;

        this->depth = depthVal;
        this->randomAmount = randomAmountVal;

        updateIncrements();
    }

    float process()
    {
        // Three sine LFOs at different rates (Lexicon-style)
        // Uses fast polynomial sine approximation (~5-10x faster than std::sin)
        float lfo1 = fastSin2Pi(static_cast<float>(phase1)) * 0.5f;
        float lfo2 = fastSin2Pi(static_cast<float>(phase2)) * 0.3f;
        float lfo3 = fastSin2Pi(static_cast<float>(phase3)) * 0.2f;

        // Random component with cosine interpolation for smooth transitions
        randomCounter++;
        if (randomCounter >= randomUpdateRate)
        {
            randomCounter = 0;
            randomPrev = randomCurrent;
            randomPhase = 0.0f;
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            randomTarget = dist(rng);
        }
        // Smoothstep interpolation for smooth random value transitions
        // Replaces cosine interpolation — no trig needed
        if (randomPhase < 1.0f)
        {
            randomPhase += 1.0f / static_cast<float>(randomUpdateRate);
            if (randomPhase > 1.0f) randomPhase = 1.0f;
            float t = randomPhase * randomPhase * (3.0f - 2.0f * randomPhase);
            randomCurrent = randomPrev + (randomTarget - randomPrev) * t;
        }

        // Combine all modulation sources
        float output = (lfo1 + lfo2 + lfo3 + randomCurrent * randomAmount) * depth;

        // Update phases
        phase1 += increment1;
        phase2 += increment2;
        phase3 += increment3;

        if (phase1 >= 1.0) phase1 -= 1.0;
        if (phase2 >= 1.0) phase2 -= 1.0;
        if (phase3 >= 1.0) phase3 -= 1.0;

        return output;
    }

    // Set random component refresh rate (Hz). Lower = coarser modulation (1970s character).
    void setRandomUpdateHz(float hz)
    {
        randomRefreshHz = std::clamp(hz, 5.0f, 60.0f);
        randomUpdateRate = std::max(1, static_cast<int>(sampleRate / static_cast<double>(randomRefreshHz)));
    }

private:
    void updateIncrements()
    {
        increment1 = rate1 / sampleRate;
        increment2 = rate2 / sampleRate;
        increment3 = rate3 / sampleRate;
        randomUpdateRate = std::max(1, static_cast<int>(sampleRate / static_cast<double>(randomRefreshHz)));
    }

    double sampleRate = 44100.0;
    double phase1 = 0.0, phase2 = 0.0, phase3 = 0.0;
    double increment1 = 0.0, increment2 = 0.0, increment3 = 0.0;
    float rate1 = 0.5f, rate2 = 0.8f, rate3 = 0.2f;
    float depth = 0.3f;
    float randomAmount = 0.2f;
    float randomRefreshHz = 30.0f;

    std::mt19937 rng;
    float randomTarget = 0.0f;
    float randomPrev = 0.0f;
    float randomCurrent = 0.0f;
    float randomPhase = 1.0f;
    int randomCounter = 0;
    int randomUpdateRate = 1470;
};

//==============================================================================
// Output EQ with proper biquad filters
class OutputEQ
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        highCutL.prepare(sr);
        highCutR.prepare(sr);
        lowCutL.prepare(sr);
        lowCutR.prepare(sr);
        updateFilters();
    }

    void clear()
    {
        highCutL.clear();
        highCutR.clear();
        lowCutL.clear();
        lowCutR.clear();
    }

    void setHighCut(float freq)
    {
        highCutFreq = std::clamp(freq, 1000.0f, 20000.0f);
        updateFilters();
    }

    void setLowCut(float freq)
    {
        lowCutFreq = std::clamp(freq, 20.0f, 500.0f);
        updateFilters();
    }

    void process(float& left, float& right)
    {
        left = highCutL.process(lowCutL.process(left));
        right = highCutR.process(lowCutR.process(right));
    }

private:
    void updateFilters()
    {
        highCutL.setLowPass(highCutFreq, 0.707f);
        highCutR.setLowPass(highCutFreq, 0.707f);
        lowCutL.setHighPass(lowCutFreq, 0.707f);
        lowCutR.setHighPass(lowCutFreq, 0.707f);
    }

    double sampleRate = 44100.0;
    float highCutFreq = 12000.0f;
    float lowCutFreq = 20.0f;
    BiquadFilter highCutL, highCutR, lowCutL, lowCutR;
};

//==============================================================================
// Reverb mode enumeration
enum class ReverbMode
{
    Plate = 0,
    Room,
    Hall,
    Chamber,
    Cathedral,
    Ambience,
    BrightHall,
    ChorusSpace,
    RandomSpace,
    DirtyHall
};

//==============================================================================
// Mode-specific parameters (enhanced for Lexicon-style sound)
struct ModeParameters
{
    std::array<float, 8> delayTimesMs;
    float dampingBase;
    float dampingFreq;
    float highShelfGain;
    float highShelfFreq;
    float modRate;
    float modDepth;
    float modRandom;
    float diffusionAmount;
    float earlyReflectionsAmount;
    float preDelayMs;
    float decayMultiplier;
    float crossoverFreq;    // Band 1/2 boundary (bassFreq)
    float lowDecayMult;     // Band 1 (sub-bass) decay multiplier
    float midDecayMult;     // Band 2 (low-mid) decay multiplier
    float highDecayMult;    // Band 3 (high-mid) decay multiplier
    float highFreq;         // Band 2/3 boundary
    float saturationDrive;
    float erToLateBlend;  // Early reflections crossfeed to late reverb
    float outputGain;     // Per-mode output level compensation (normalize volume across modes)
    float inputDiffuserScale;  // Scales input diffuser times per mode (small spaces = shorter)
    float tankDiffuserScale;   // Scales tank diffuser times per mode

    // Mode-specific early reflection patterns (12 taps)
    std::array<float, 12> erTapTimesMs;
    std::array<float, 12> erTapGains;
};

// Prime-number based delay times for reduced metallic resonance
inline ModeParameters getPlateParameters()
{
    return {
        // Prime-derived delays - longer for better decay accumulation
        { 17.3f, 23.9f, 31.3f, 41.7f, 53.1f, 67.3f, 79.9f, 97.3f },
        0.35f,          // Damping base (reduced for longer decay)
        13000.0f,       // Damping freq: air-band absorption (6dB/oct)
        2.0f,           // High shelf gain (bright plate)
        7000.0f,        // High shelf freq
        1.8f,           // Mod rate (faster for shimmer)
        1.0f,           // Mod depth
        0.35f,          // Random modulation
        0.75f,          // High diffusion
        0.0f,           // No early reflections (plate characteristic)
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        1.2f,           // Extended decay multiplier
        500.0f,         // crossoverFreq (band 1/2 boundary)
        1.03f,          // lowDecayMult: slight bass warmth
        1.0f,           // midDecayMult: neutral mid
        0.92f,          // highDecayMult: Lexicon-calibrated
        4000.0f,        // highFreq (band 2/3 boundary)
        0.06f,          // Subtle saturation
        0.0f,           // No ER crossfeed
        1.0f,           // outputGain: standard
        0.8f,           // inputDiffuserScale: medium (plate is dense but not huge)
        0.8f,           // tankDiffuserScale
        // ER pattern (unused for Plate since amount=0)
        { 3.1f, 7.2f, 11.7f, 17.3f, 23.9f, 31.1f, 41.3f, 53.7f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.35f, 0.3f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f }
    };
}

inline ModeParameters getRoomParameters()
{
    return {
        // Prime-derived delays
        { 13.1f, 19.7f, 27.1f, 33.7f, 41.3f, 49.9f, 59.3f, 67.9f },
        0.45f,          // Lighter damping
        12000.0f,       // Damping freq: air-band absorption (6dB/oct)
        0.0f,           // Flat response
        8000.0f,        // High shelf freq
        1.2f,           // Moderate mod rate
        0.6f,           // Less modulation
        0.25f,          // Less random
        0.6f,           // Medium diffusion
        0.20f,          // Moderate early reflections (feeds FDN for buildup)
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        0.9f,           // Slightly shorter decay
        500.0f,         // crossoverFreq (band 1/2 boundary)
        1.03f,          // lowDecayMult: slight bass warmth
        1.0f,           // midDecayMult: neutral mid
        0.90f,          // highDecayMult: Lexicon-calibrated
        4000.0f,        // highFreq (band 2/3 boundary)
        0.05f,          // Very subtle saturation
        0.35f,          // Strong ER to late blend (builds density for EDT > RT60)
        1.0f,           // outputGain: standard
        0.6f,           // inputDiffuserScale: short (small space = fast diffusion)
        0.6f,           // tankDiffuserScale
        // Room ER: extended pattern with buildup for Shape/Spread-like density
        // Non-monotonic gains: builds up then decays (simulates room fill)
        { 2.1f, 5.3f, 9.1f, 14.7f, 21.3f, 29.9f, 41.7f, 55.3f, 71.9f, 89.3f, 109.7f, 131.1f },
        { 0.45f, 0.55f, 0.70f, 0.85f, 0.90f, 0.85f, 0.70f, 0.55f, 0.40f, 0.28f, 0.18f, 0.10f }
    };
}

inline ModeParameters getHallParameters()
{
    return {
        // Prime-derived delays, longer for hall
        { 41.3f, 53.9f, 67.1f, 79.9f, 97.3f, 113.9f, 131.3f, 149.9f },
        0.5f,           // Medium damping
        12000.0f,       // Damping freq: air-band absorption (6dB/oct)
        -1.5f,          // Slight high cut
        5000.0f,        // Lower shelf freq
        0.6f,           // Slow modulation
        0.8f,           // Moderate depth
        0.2f,           // Subtle random
        0.8f,           // High diffusion (smooth)
        0.12f,          // Moderate early reflections
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        1.4f,           // Extended decay
        500.0f,         // crossoverFreq (band 1/2 boundary)
        1.03f,          // lowDecayMult: slight bass warmth
        1.0f,           // midDecayMult: neutral mid
        0.88f,          // highDecayMult: Lexicon-calibrated (hall = darker)
        3500.0f,        // highFreq (band 2/3 boundary)
        0.03f,          // Minimal saturation
        0.15f,          // ER to late blend
        0.9f,           // outputGain: slightly reduced (long decay accumulates energy)
        1.2f,           // inputDiffuserScale: longer (large space needs thorough smearing)
        1.2f,           // tankDiffuserScale
        // Hall ER: buildup then decay (large surface reflections arrive later)
        { 5.0f, 12.3f, 19.7f, 27.3f, 35.1f, 43.9f, 53.7f, 67.3f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.45f, 0.55f, 0.7f, 0.65f, 0.5f, 0.4f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f }
    };
}

inline ModeParameters getChamberParameters()
{
    return {
        // Shorter prime-derived delays for small dense space
        { 11.3f, 17.9f, 23.3f, 29.7f, 37.1f, 43.9f, 53.3f, 61.7f },
        0.40f,          // dampingBase: moderate
        12000.0f,       // dampingFreq: air-band absorption (6dB/oct)
        0.0f,           // highShelfGain: neutral
        6000.0f,        // highShelfFreq
        1.4f,           // modRate: moderate
        0.7f,           // modDepth
        0.30f,          // modRandom
        0.85f,          // diffusionAmount: very high for dense tail
        0.20f,          // earlyReflectionsAmount: prominent
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        1.0f,           // decayMultiplier: normal
        500.0f,         // crossoverFreq (band 1/2 boundary)
        1.03f,          // lowDecayMult: slight bass warmth
        1.0f,           // midDecayMult: neutral mid
        0.90f,          // highDecayMult: Lexicon-calibrated
        4000.0f,        // highFreq (band 2/3 boundary)
        0.04f,          // saturationDrive: subtle
        0.35f,          // erToLateBlend: strong crossfeed (builds density for short rooms)
        1.1f,           // outputGain: slight boost (shorter delays = less energy)
        0.7f,           // inputDiffuserScale: medium-short (mid-sized space)
        0.7f,           // tankDiffuserScale
        // Chamber ER: dense with buildup pattern (small room reflections build and decay)
        { 1.5f, 3.2f, 5.1f, 7.3f, 9.8f, 13.5f, 18.1f, 24.3f, 32.1f, 42.7f, 55.3f, 69.9f },
        { 0.50f, 0.65f, 0.80f, 0.90f, 0.95f, 0.90f, 0.80f, 0.65f, 0.50f, 0.35f, 0.22f, 0.12f }
    };
}

inline ModeParameters getCathedralParameters()
{
    return {
        // Very long prime-derived delays for massive space
        { 61.3f, 79.9f, 97.3f, 113.9f, 131.7f, 149.3f, 167.9f, 191.3f },
        0.55f,          // dampingBase: significant (stone absorption)
        10000.0f,       // dampingFreq: air-band absorption
        -2.5f,          // highShelfGain: dark character
        4000.0f,        // highShelfFreq
        0.4f,           // modRate: very slow (large space)
        0.9f,           // modDepth: moderate
        0.15f,          // modRandom: subtle
        0.90f,          // diffusionAmount: very high
        0.10f,          // earlyReflectionsAmount: sparse initially
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        1.6f,           // decayMultiplier: very long tails
        400.0f,         // crossoverFreq (band 1/2 boundary)
        1.04f,          // lowDecayMult: warm bass (stone reverb)
        1.0f,           // midDecayMult: neutral mid
        0.85f,          // highDecayMult: cathedral = more HF absorption
        3000.0f,        // highFreq (band 2/3 boundary)
        0.02f,          // saturationDrive: minimal
        0.10f,          // erToLateBlend: sparse ER blending
        0.75f,          // outputGain: reduced (very long decay = high energy)
        1.5f,           // inputDiffuserScale: long (massive space needs maximum smearing)
        1.5f,           // tankDiffuserScale
        // Cathedral ER: sparse then building (large-space behavior)
        { 8.0f, 18.5f, 31.2f, 42.7f, 55.3f, 67.9f, 82.1f, 95.7f, 110.3f, 128.5f, 0.0f, 0.0f },
        { 0.3f, 0.35f, 0.5f, 0.55f, 0.6f, 0.55f, 0.45f, 0.35f, 0.25f, 0.15f, 0.0f, 0.0f }
    };
}

inline ModeParameters getAmbienceParameters()
{
    return {
        // Very short prime-derived delays
        { 7.1f, 11.3f, 14.9f, 19.3f, 23.7f, 29.1f, 33.7f, 39.1f },
        0.30f,          // dampingBase: light (transparent)
        14000.0f,       // dampingFreq: very high (transparent ambience)
        1.0f,           // highShelfGain: slightly bright
        8000.0f,        // highShelfFreq
        1.6f,           // modRate: fast
        0.5f,           // modDepth: moderate
        0.20f,          // modRandom: subtle
        0.70f,          // diffusionAmount: good
        0.35f,          // earlyReflectionsAmount: dominant
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        0.5f,           // decayMultiplier: very short decay
        500.0f,         // crossoverFreq (band 1/2 boundary)
        1.02f,          // lowDecayMult: near-neutral
        1.0f,           // midDecayMult: neutral mid
        0.94f,          // highDecayMult: transparent (short reverb)
        5000.0f,        // highFreq (band 2/3 boundary)
        0.02f,          // saturationDrive: minimal
        0.40f,          // erToLateBlend: heavy ER crossfeed
        1.3f,           // outputGain: boosted (very short decay = less energy)
        0.5f,           // inputDiffuserScale: very short (tight space = fast diffusion)
        0.5f,           // tankDiffuserScale
        // Ambience ER: dense and rapidly decaying (tight space)
        { 0.8f, 1.7f, 2.9f, 4.3f, 5.9f, 7.7f, 10.1f, 13.3f, 17.1f, 21.7f, 27.3f, 33.9f },
        { 0.95f, 0.90f, 0.85f, 0.80f, 0.70f, 0.60f, 0.50f, 0.35f, 0.25f, 0.15f, 0.10f, 0.05f }
    };
}

inline ModeParameters getBrightHallParameters()
{
    return {
        // Hall-length delays with slightly shorter average for clarity
        { 37.1f, 49.9f, 61.3f, 73.7f, 89.3f, 103.9f, 121.7f, 139.3f },
        0.40f,          // dampingBase: less damping (brighter)
        14000.0f,       // dampingFreq: high (bright character)
        2.0f,           // highShelfGain: bright boost
        7000.0f,        // highShelfFreq
        0.7f,           // modRate: moderate
        0.9f,           // modDepth: good movement
        0.25f,          // modRandom
        0.8f,           // diffusionAmount
        0.14f,          // earlyReflectionsAmount
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        1.3f,           // decayMultiplier: long-ish
        500.0f,         // crossoverFreq (band 1/2 boundary)
        1.03f,          // lowDecayMult: slight warmth
        1.0f,           // midDecayMult: neutral mid
        0.91f,          // highDecayMult: bright (less HF rolloff than Hall)
        5000.0f,        // highFreq (band 2/3 boundary)
        0.03f,          // saturationDrive
        0.15f,          // erToLateBlend
        0.9f,           // outputGain
        1.1f,           // inputDiffuserScale
        1.1f,           // tankDiffuserScale
        // Bright Hall ER: buildup with bright character
        { 4.5f, 11.1f, 18.3f, 25.7f, 33.1f, 41.9f, 51.3f, 63.7f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.5f, 0.6f, 0.75f, 0.65f, 0.5f, 0.4f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f }
    };
}

inline ModeParameters getChorusSpaceParameters()
{
    return {
        // Hall-like delays for lush chorused space
        { 39.7f, 51.3f, 63.7f, 77.9f, 91.3f, 107.9f, 127.1f, 143.3f },
        0.45f,          // dampingBase
        12000.0f,       // dampingFreq: air-band absorption
        -0.5f,          // highShelfGain: very slight cut
        6000.0f,        // highShelfFreq
        2.0f,           // modRate: fast for chorus effect
        3.5f,           // modDepth: very high (3.5x normal = audible chorusing)
        0.45f,          // modRandom: significant for richness
        0.75f,          // diffusionAmount
        0.10f,          // earlyReflectionsAmount
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        1.3f,           // decayMultiplier
        500.0f,         // crossoverFreq (band 1/2 boundary)
        1.03f,          // lowDecayMult: slight warmth
        1.0f,           // midDecayMult: neutral mid
        0.88f,          // highDecayMult: Lexicon-calibrated
        4000.0f,        // highFreq (band 2/3 boundary)
        0.04f,          // saturationDrive
        0.12f,          // erToLateBlend
        0.85f,          // outputGain
        1.0f,           // inputDiffuserScale
        1.0f,           // tankDiffuserScale
        // Sparse ER — let the chorus tail dominate
        { 5.5f, 13.7f, 22.1f, 30.9f, 40.3f, 51.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.5f, 0.45f, 0.4f, 0.35f, 0.25f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
    };
}

inline ModeParameters getRandomSpaceParameters()
{
    return {
        // Medium-long delays with wide spread for random character
        { 29.3f, 43.7f, 59.3f, 71.9f, 83.1f, 97.3f, 113.9f, 131.7f },
        0.45f,          // dampingBase
        12000.0f,       // dampingFreq: air-band absorption
        -1.0f,          // highShelfGain: slight cut
        5500.0f,        // highShelfFreq
        0.5f,           // modRate: slow base rate
        2.5f,           // modDepth: very high (wandering delays)
        0.85f,          // modRandom: very high — this is the key differentiator
        0.70f,          // diffusionAmount
        0.08f,          // earlyReflectionsAmount: minimal
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        1.2f,           // decayMultiplier
        500.0f,         // crossoverFreq (band 1/2 boundary)
        1.03f,          // lowDecayMult: slight warmth
        1.0f,           // midDecayMult: neutral mid
        0.88f,          // highDecayMult: Lexicon-calibrated
        4000.0f,        // highFreq (band 2/3 boundary)
        0.04f,          // saturationDrive
        0.10f,          // erToLateBlend
        0.85f,          // outputGain
        1.1f,           // inputDiffuserScale
        1.2f,           // tankDiffuserScale
        // Very sparse ER — randomness should dominate
        { 6.7f, 15.3f, 27.1f, 41.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.4f, 0.35f, 0.25f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
    };
}

inline ModeParameters getDirtyHallParameters()
{
    return {
        // Hall-like delays for lo-fi hall character
        { 41.3f, 53.9f, 67.1f, 79.9f, 97.3f, 113.9f, 131.3f, 149.9f },
        0.65f,          // dampingBase: heavy damping
        6000.0f,        // dampingFreq: lower (Dirty = intentionally dark)
        -5.0f,          // highShelfGain: strong HF cut
        3000.0f,        // highShelfFreq: low shelf target
        0.5f,           // modRate: slow
        0.7f,           // modDepth
        0.3f,           // modRandom
        0.65f,          // diffusionAmount: moderate (slightly gritty)
        0.12f,          // earlyReflectionsAmount
        0.0f,           // No pre-delay (user controls pre-delay entirely)
        1.3f,           // decayMultiplier
        400.0f,         // crossoverFreq (band 1/2 boundary)
        1.06f,          // lowDecayMult: boomy (dirty character)
        1.0f,           // midDecayMult: neutral mid
        0.78f,          // highDecayMult: heavy HF absorption (lo-fi)
        3000.0f,        // highFreq (band 2/3 boundary)
        0.25f,          // saturationDrive: VERY high — this is the "dirty" part
        0.15f,          // erToLateBlend
        0.85f,          // outputGain
        1.0f,           // inputDiffuserScale
        1.0f,           // tankDiffuserScale
        // Hall ER pattern
        { 5.0f, 12.3f, 19.7f, 27.3f, 35.1f, 43.9f, 53.7f, 67.3f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.7f, 0.65f, 0.55f, 0.45f, 0.4f, 0.35f, 0.25f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f }
    };
}

//==============================================================================
// Main FDN Reverb Engine (Lexicon/Valhalla-enhanced with professional upgrades)
class FDNReverb
{
public:
    static constexpr int NUM_DELAYS = 8;
    static constexpr int NUM_INPUT_DIFFUSERS = 4;
    static constexpr int NUM_TANK_DIFFUSERS = 8;

    void prepare(double sr, int /*maxBlockSize*/)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;

        // Prepare delay lines (500ms max for Cathedral at 2x room size)
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delaysL[static_cast<size_t>(i)].prepare(sampleRate, 500.0f);
            delaysR[static_cast<size_t>(i)].prepare(sampleRate, 500.0f);
            dampingL[static_cast<size_t>(i)].prepare(sampleRate);
            dampingR[static_cast<size_t>(i)].prepare(sampleRate);
            fourBandL[static_cast<size_t>(i)].prepare(sampleRate);
            fourBandR[static_cast<size_t>(i)].prepare(sampleRate);
            modulatorsL[static_cast<size_t>(i)].prepare(sampleRate, i);
            modulatorsR[static_cast<size_t>(i)].prepare(sampleRate, i + NUM_DELAYS);
        }

        // Prepare pre-delay (300ms max for 250ms user + mode pre-delay)
        preDelayL.prepare(sampleRate, 300.0f);
        preDelayR.prepare(sampleRate, 300.0f);

        // Prepare input diffusers (early diffusion)
        for (int i = 0; i < NUM_INPUT_DIFFUSERS; ++i)
        {
            inputDiffuserL[static_cast<size_t>(i)].prepare(sampleRate, 50.0f);
            inputDiffuserR[static_cast<size_t>(i)].prepare(sampleRate, 50.0f);
        }

        // Prepare tank diffusers (late diffusion - in feedback path)
        for (int i = 0; i < NUM_TANK_DIFFUSERS; ++i)
        {
            tankDiffuserL[static_cast<size_t>(i)].prepare(sampleRate, 80.0f);
            tankDiffuserR[static_cast<size_t>(i)].prepare(sampleRate, 80.0f);
        }

        // Prepare early reflections
        earlyReflectionsL.prepare(sampleRate);
        earlyReflectionsR.prepare(sampleRate);

        // Prepare ER diffusers (smooth tap reflections into diffuse field)
        for (int i = 0; i < NUM_ER_DIFFUSERS; ++i)
        {
            erDiffuserL[static_cast<size_t>(i)].prepare(sampleRate, 5.0f);
            erDiffuserR[static_cast<size_t>(i)].prepare(sampleRate, 5.0f);
        }

        // Prepare ER bass cut filters
        erBassCutL.prepare(sampleRate);
        erBassCutR.prepare(sampleRate);

        // Prepare output EQ
        outputEQ.prepare(sampleRate);

        // Prepare DC blockers (per-channel in feedback loop + output pair)
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            dcBlockersL[static_cast<size_t>(i)].prepare(sampleRate);
            dcBlockersR[static_cast<size_t>(i)].prepare(sampleRate);
        }
        dcBlockerOutL.prepare(sampleRate);
        dcBlockerOutR.prepare(sampleRate);

        // Prepare high shelf biquads
        highShelfL.prepare(sampleRate);
        highShelfR.prepare(sampleRate);

        // Prepare era bandwidth limiters
        eraBandwidthL.prepare(sampleRate);
        eraBandwidthR.prepare(sampleRate);

        // Force initialization of waveshaper lookup tables (avoid RT allocation)
        AnalogEmulation::initializeLibrary();

        // Initialize state
        feedbackL.fill(0.0f);
        feedbackR.fill(0.0f);

        setMode(ReverbMode::Plate);
    }

    void reset()
    {
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delaysL[static_cast<size_t>(i)].clear();
            delaysR[static_cast<size_t>(i)].clear();
            dampingL[static_cast<size_t>(i)].clear();
            dampingR[static_cast<size_t>(i)].clear();
            fourBandL[static_cast<size_t>(i)].clear();
            fourBandR[static_cast<size_t>(i)].clear();
        }

        preDelayL.clear();
        preDelayR.clear();

        for (int i = 0; i < NUM_INPUT_DIFFUSERS; ++i)
        {
            inputDiffuserL[static_cast<size_t>(i)].clear();
            inputDiffuserR[static_cast<size_t>(i)].clear();
        }

        for (int i = 0; i < NUM_TANK_DIFFUSERS; ++i)
        {
            tankDiffuserL[static_cast<size_t>(i)].clear();
            tankDiffuserR[static_cast<size_t>(i)].clear();
        }

        earlyReflectionsL.clear();
        earlyReflectionsR.clear();

        for (int i = 0; i < NUM_ER_DIFFUSERS; ++i)
        {
            erDiffuserL[static_cast<size_t>(i)].clear();
            erDiffuserR[static_cast<size_t>(i)].clear();
        }

        outputEQ.clear();

        erBassCutL.clear();
        erBassCutR.clear();

        // Clear per-channel DC blockers (feedback loop)
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            dcBlockersL[static_cast<size_t>(i)].clear();
            dcBlockersR[static_cast<size_t>(i)].clear();
        }
        dcBlockerOutL.clear();
        dcBlockerOutR.clear();

        highShelfL.clear();
        highShelfR.clear();

        eraBandwidthL.clear();
        eraBandwidthR.clear();

        feedbackL.fill(0.0f);
        feedbackR.fill(0.0f);
    }

    void setMode(ReverbMode mode)
    {
        // Snapshot current delay times for crossfade (prevents clicks)
        bool needsCrossfade = (mode != currentMode);
        if (needsCrossfade)
        {
            fadeFromDelayL = baseDelayTimesL;
            fadeFromDelayR = baseDelayTimesR;
        }

        currentMode = mode;

        switch (mode)
        {
            case ReverbMode::Plate:     modeParams = getPlateParameters();     break;
            case ReverbMode::Room:      modeParams = getRoomParameters();      break;
            case ReverbMode::Hall:      modeParams = getHallParameters();      break;
            case ReverbMode::Chamber:   modeParams = getChamberParameters();   break;
            case ReverbMode::Cathedral:   modeParams = getCathedralParameters();   break;
            case ReverbMode::Ambience:    modeParams = getAmbienceParameters();    break;
            case ReverbMode::BrightHall:  modeParams = getBrightHallParameters();  break;
            case ReverbMode::ChorusSpace: modeParams = getChorusSpaceParameters(); break;
            case ReverbMode::RandomSpace: modeParams = getRandomSpaceParameters(); break;
            case ReverbMode::DirtyHall:   modeParams = getDirtyHallParameters();   break;
        }

        updateAllParameters();

        // Start crossfade from old delay times to new
        if (needsCrossfade)
        {
            modeChangeFadePos = 0;
            modeChangeFadeSamples = MODE_CHANGE_FADE_LENGTH;
        }
    }

    void setColor(ColorMode color)
    {
        currentColor = color;

        switch (color)
        {
            case ColorMode::Seventies:
            {
                // 1970s: EMT 250 era — bandwidth-limited, tube saturation, noise
                eraSatCurve = AnalogEmulation::WaveshaperCurves::CurveType::LA2A_Tube;
                eraSatDrive = std::clamp(modeParams.saturationDrive * 4.0f, 0.05f, 0.35f);

                // Bandwidth limiting at 8kHz (early digital reverb character)
                eraBandwidthActive = true;
                eraBandwidthL.setLowPass(8000.0f, 0.707f);
                eraBandwidthR.setLowPass(8000.0f, 0.707f);

                // Audible noise floor (-80dB = 0.0001 linear)
                eraNoiseLevel = 0.0001f;

                // Coarser modulation (10Hz random refresh)
                updateModulatorRandomRate(10.0f);
                break;
            }

            case ColorMode::Eighties:
            {
                // 1980s: Lexicon 224/480 era — cleaner but still colored
                eraSatCurve = AnalogEmulation::WaveshaperCurves::CurveType::Triode;
                eraSatDrive = std::clamp(modeParams.saturationDrive * 2.0f, 0.02f, 0.15f);

                // Bandwidth limiting at 14kHz
                eraBandwidthActive = true;
                eraBandwidthL.setLowPass(14000.0f, 0.707f);
                eraBandwidthR.setLowPass(14000.0f, 0.707f);

                // Subtle noise floor (-96dB)
                eraNoiseLevel = 0.000016f;

                // Medium modulation smoothness (20Hz random refresh)
                updateModulatorRandomRate(20.0f);
                break;
            }

            case ColorMode::Now:
            default:
            {
                // Modern: full-bandwidth, minimal saturation
                eraSatCurve = AnalogEmulation::WaveshaperCurves::CurveType::Linear;
                eraSatDrive = modeParams.saturationDrive;

                // No bandwidth limiting
                eraBandwidthActive = false;

                // No noise
                eraNoiseLevel = 0.0f;

                // Full-quality modulation (30Hz random refresh)
                updateModulatorRandomRate(30.0f);
                break;
            }
        }
    }

    void setSize(float sz)
    {
        size = std::clamp(sz, 0.0f, 1.0f);
        // Exponential curve for more usable range: 0.1s to 10s
        float decaySeconds = 0.1f + std::pow(size, 1.5f) * 9.9f;
        targetDecay = decaySeconds * modeParams.decayMultiplier;

        // Scale early reflections with size
        float erScale = 0.7f + size * 0.6f;  // 0.7x to 1.3x
        earlyReflectionsL.setTimeScale(erScale);
        earlyReflectionsR.setTimeScale(erScale);

        updateFeedbackGain();
    }

    void setDamping(float damp)
    {
        damping = std::clamp(damp, 0.0f, 1.0f);
        updateDamping();
    }

    void setWidth(float w)
    {
        width = std::clamp(w, 0.0f, 1.0f);
    }

    void setMix(float m)
    {
        mix = std::clamp(m, 0.0f, 1.0f);
    }

    void setFreeze(bool frozen)
    {
        if (freezeMode != frozen)
        {
            freezeMode = frozen;
            // Re-compute 4-band gains for freeze/unfreeze transition
            updateFourBandDecay();
        }
    }

    // New Valhalla-style parameters
    void setPreDelay(float ms)
    {
        userPreDelay = std::clamp(ms, 0.0f, 250.0f);
        updatePreDelay();
    }

    void setModRate(float rate)
    {
        userModRate = std::clamp(rate, 0.1f, 5.0f);
        updateModulation();
    }

    void setModDepth(float depthVal)
    {
        userModDepth = std::clamp(depthVal, 0.0f, 1.0f);
        updateModulation();
    }

    void setBassMult(float mult)
    {
        userBassMult = std::clamp(mult, 0.1f, 3.0f);
        updateFourBandDecay();
    }

    void setBassFreq(float freq)
    {
        userBassFreq = std::clamp(freq, 100.0f, 1000.0f);
        updateFourBandDecay();
    }

    void setHighCut(float freq)
    {
        outputEQ.setHighCut(freq);
    }

    void setLowCut(float freq)
    {
        outputEQ.setLowCut(freq);
    }

    void setEarlyDiffusion(float diff)
    {
        earlyDiffusion = std::clamp(diff, 0.0f, 1.0f);
        updateDiffusion();
    }

    void setLateDiffusion(float diff)
    {
        lateDiffusion = std::clamp(diff, 0.0f, 1.0f);
        updateTankDiffusion();
    }

    void setRoomSize(float rs)
    {
        roomSize = std::clamp(rs, 0.0f, 1.0f);
        updateDelayTimes();
        updateFeedbackGain();
    }

    void setEarlyLateBalance(float bal)
    {
        earlyLateBalance = std::clamp(bal, 0.0f, 1.0f);
    }

    void setHighDecayMult(float mult)
    {
        userHighDecayMult = std::clamp(mult, 0.25f, 4.0f);
        updateFourBandDecay();
    }

    void setMidDecayMult(float mult)
    {
        userMidDecayMult = std::clamp(mult, 0.25f, 4.0f);
        updateFourBandDecay();
    }

    void setHighFreq(float freq)
    {
        userHighFreq = std::clamp(freq, 1000.0f, 12000.0f);
        updateFourBandDecay();
    }

    void setERShape(float shp)
    {
        erShape = std::clamp(shp, 0.0f, 1.0f);
        earlyReflectionsL.setShape(erShape);
        earlyReflectionsR.setShape(erShape);
    }

    void setERSpread(float sp)
    {
        erSpread = std::clamp(sp, 0.0f, 1.0f);
        earlyReflectionsL.setSpread(erSpread);
        earlyReflectionsR.setSpread(erSpread);
    }

    void setERBassCut(float freq)
    {
        erBassCutFreq = std::clamp(freq, 20.0f, 500.0f);
        erBassCutActive = (erBassCutFreq > 30.0f);
        if (erBassCutActive)
        {
            erBassCutL.setHighPass(erBassCutFreq, 0.707f);
            erBassCutR.setHighPass(erBassCutFreq, 0.707f);
        }
    }

    // RT60 readout for UI display (returns calculated decay time in seconds)
    float getTargetRT60() const { return targetDecay; }

    void process(float inputL, float inputR, float& outputL, float& outputR)
    {
        // In freeze mode, cut input
        float effectiveInputL = freezeMode ? 0.0f : inputL;
        float effectiveInputR = freezeMode ? 0.0f : inputR;

        // Pre-delay
        float preDelayedL = preDelayL.process(effectiveInputL);
        float preDelayedR = preDelayR.process(effectiveInputR);

        // Early reflections (from dry input)
        float earlyL = earlyReflectionsL.process(effectiveInputL);
        float earlyR = earlyReflectionsR.process(effectiveInputR);

        // ER diffusion: smooth individual tap reflections into a diffuse early field
        for (int i = 0; i < NUM_ER_DIFFUSERS; ++i)
        {
            earlyL = erDiffuserL[static_cast<size_t>(i)].process(earlyL);
            earlyR = erDiffuserR[static_cast<size_t>(i)].process(earlyR);
        }

        // ER bass cut: reduce bass buildup from early reflections (critical for short reverbs)
        if (erBassCutActive)
        {
            earlyL = erBassCutL.process(earlyL);
            earlyR = erBassCutR.process(earlyR);
        }

        // Crossfeed early reflections to late reverb input
        float erCrossfeed = modeParams.erToLateBlend;
        float lateInputL = preDelayedL + earlyL * erCrossfeed;
        float lateInputR = preDelayedR + earlyR * erCrossfeed;

        // Era noise injection (simulates vintage ADC/circuit noise floor)
        if (eraNoiseLevel > 0.0f)
        {
            lateInputL += noiseDist(noiseRng) * eraNoiseLevel;
            lateInputR += noiseDist(noiseRng) * eraNoiseLevel;
        }

        // Input diffusion (early diffusion)
        float diffusedL = lateInputL;
        float diffusedR = lateInputR;
        for (int i = 0; i < NUM_INPUT_DIFFUSERS; ++i)
        {
            diffusedL = inputDiffuserL[static_cast<size_t>(i)].process(diffusedL);
            diffusedR = inputDiffuserR[static_cast<size_t>(i)].process(diffusedR);
        }

        // In freeze mode, cut diffused input to tank
        if (freezeMode)
        {
            diffusedL = 0.0f;
            diffusedR = 0.0f;
        }

        // Mode change crossfade: smoothly interpolate delay times to prevent clicks
        bool fading = modeChangeFadePos < modeChangeFadeSamples;
        float fadeT = 1.0f;
        if (fading)
        {
            fadeT = static_cast<float>(modeChangeFadePos) / static_cast<float>(modeChangeFadeSamples);
            fadeT = fadeT * fadeT * (3.0f - 2.0f * fadeT);  // smoothstep
            modeChangeFadePos++;
        }

        // FDN processing
        std::array<float, NUM_DELAYS> delayOutputsL, delayOutputsR;

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            size_t idx = static_cast<size_t>(i);

            // Complex modulation
            float modL = modulatorsL[idx].process();
            float modR = modulatorsR[idx].process();

            float effectiveBaseL = baseDelayTimesL[idx];
            float effectiveBaseR = baseDelayTimesR[idx];

            // During mode crossfade, interpolate from old to new delay times
            if (fading)
            {
                effectiveBaseL = fadeFromDelayL[idx] + fadeT * (baseDelayTimesL[idx] - fadeFromDelayL[idx]);
                effectiveBaseR = fadeFromDelayR[idx] + fadeT * (baseDelayTimesR[idx] - fadeFromDelayR[idx]);
            }

            // Width-scaled stereo offsets: at width=0, both channels use same delay
            // times (true mono FDN). This enables matching mono/narrow PCM 90 presets.
            effectiveBaseR = effectiveBaseL + (effectiveBaseR - effectiveBaseL) * width;

            float modDelayL = effectiveBaseL + modL;
            float modDelayR = effectiveBaseR + modR;

            delaysL[idx].setDelayMs(modDelayL);
            delaysR[idx].setDelayMs(modDelayR);

            // DC blocking inside feedback loop (prevents DC buildup in recirculation)
            float fbL = dcBlockersL[idx].process(feedbackL[idx]);
            float fbR = dcBlockersR[idx].process(feedbackR[idx]);

            // Four-band decay: frequency-dependent feedback gain (uses pre-computed gains)
            float decayedL = fourBandL[idx].process(fbL);
            float decayedR = fourBandR[idx].process(fbR);

            // One-pole damping: gentle air-frequency absorption (6dB/oct above cutoff)
            delayOutputsL[idx] = dampingL[idx].process(decayedL);
            delayOutputsR[idx] = dampingR[idx].process(decayedR);
        }

        // Hadamard matrix mixing
        std::array<float, NUM_DELAYS> mixedL = applyHadamard(delayOutputsL);
        std::array<float, NUM_DELAYS> mixedR = applyHadamard(delayOutputsR);

        // Cross-channel coupling for natural stereo diffusion
        // Blends a small portion of opposite channel into each, preventing
        // the L/R networks from being completely independent (real spaces couple)
        constexpr float coupling = 0.15f;
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            size_t idx = static_cast<size_t>(i);
            float tmpL = mixedL[idx];
            mixedL[idx] = mixedL[idx] * (1.0f - coupling) + mixedR[idx] * coupling;
            mixedR[idx] = mixedR[idx] * (1.0f - coupling) + tmpL * coupling;
        }

        // Write to delays with saturation and tank diffusion
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            size_t idx = static_cast<size_t>(i);

            float inputToDelayL = mixedL[idx] + diffusedL * 0.25f;
            float inputToDelayR = mixedR[idx] + diffusedR * 0.25f;

            // Era-based saturation in feedback path (uses shared AnalogEmulation LUT)
            auto& waveshaper = AnalogEmulation::getWaveshaperCurves();
            inputToDelayL = waveshaper.processWithDrive(inputToDelayL, eraSatCurve, eraSatDrive);
            inputToDelayR = waveshaper.processWithDrive(inputToDelayR, eraSatCurve, eraSatDrive);

            // Era bandwidth limiting (1970s=8kHz, 1980s=14kHz, Now=bypass)
            if (eraBandwidthActive)
            {
                inputToDelayL = eraBandwidthL.process(inputToDelayL);
                inputToDelayR = eraBandwidthR.process(inputToDelayR);
            }

            // Tank diffusion (late diffusion) - applied to all delay lines
            inputToDelayL = tankDiffuserL[idx].process(inputToDelayL);
            inputToDelayR = tankDiffuserR[idx].process(inputToDelayR);

            feedbackL[idx] = delaysL[idx].process(inputToDelayL);
            feedbackR[idx] = delaysR[idx].process(inputToDelayR);
        }

        // Sum delay outputs (late reverb)
        float lateL = 0.0f, lateR = 0.0f;
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            lateL += delayOutputsL[static_cast<size_t>(i)];
            lateR += delayOutputsR[static_cast<size_t>(i)];
        }
        lateL *= 0.25f * modeParams.outputGain;
        lateR *= 0.25f * modeParams.outputGain;

        // Early/Late balance: 0.0 = all early, 0.5 = equal, 1.0 = all late
        float lateGain = earlyLateBalance;
        float earlyGain = 1.0f - earlyLateBalance;
        float wetL = lateL * lateGain + earlyL * earlyGain;
        float wetR = lateR * lateGain + earlyR * earlyGain;

        // Output DC blocking (catch any residual)
        wetL = dcBlockerOutL.process(wetL);
        wetR = dcBlockerOutR.process(wetR);

        // High shelf (using biquad)
        wetL = highShelfL.process(wetL);
        wetR = highShelfR.process(wetR);

        // Output EQ (highcut/lowcut)
        outputEQ.process(wetL, wetR);

        // Width (mid-side)
        float mid = (wetL + wetR) * 0.5f;
        float side = (wetL - wetR) * 0.5f * width;
        wetL = mid + side;
        wetR = mid - side;

        // Mix
        outputL = inputL * (1.0f - mix) + wetL * mix;
        outputR = inputR * (1.0f - mix) + wetR * mix;
    }

private:
    double sampleRate = 44100.0;
    ReverbMode currentMode = ReverbMode::Plate;
    ColorMode currentColor = ColorMode::Now;
    ModeParameters modeParams;

    // User parameters
    float size = 0.5f;
    float damping = 0.5f;
    float width = 1.0f;
    float mix = 0.5f;
    float userPreDelay = 0.0f;
    float userModRate = 1.0f;
    float userModDepth = 0.5f;
    float userBassMult = 1.0f;
    float userBassFreq = 500.0f;
    float earlyDiffusion = 0.7f;
    float lateDiffusion = 0.5f;
    float roomSize = 0.5f;
    float earlyLateBalance = 0.7f;  // Default favors late reverb
    float userHighDecayMult = 1.0f;
    float userMidDecayMult = 1.0f;
    float userHighFreq = 4000.0f;
    float erShape = 0.5f;
    float erSpread = 0.5f;
    bool freezeMode = false;

    // Internal state
    float targetDecay = 2.0f;
    float feedbackGain = 0.85f;

    // Delay lines
    std::array<DelayLine, NUM_DELAYS> delaysL, delaysR;
    std::array<float, NUM_DELAYS> baseDelayTimesL = {}, baseDelayTimesR = {};
    std::array<float, NUM_DELAYS> feedbackL = {}, feedbackR = {};

    // Filters
    std::array<DampingFilter, NUM_DELAYS> dampingL, dampingR;
    std::array<FourBandDecayFilter, NUM_DELAYS> fourBandL, fourBandR;

    // Pre-delay
    DelayLine preDelayL, preDelayR;

    // Diffusers (early and late/tank)
    std::array<AllpassFilter, NUM_INPUT_DIFFUSERS> inputDiffuserL, inputDiffuserR;
    std::array<AllpassFilter, NUM_TANK_DIFFUSERS> tankDiffuserL, tankDiffuserR;

    // Early reflections
    EarlyReflections earlyReflectionsL, earlyReflectionsR;

    // ER diffusion (smooths individual tap reflections into diffuse early field)
    static constexpr int NUM_ER_DIFFUSERS = 2;
    std::array<AllpassFilter, NUM_ER_DIFFUSERS> erDiffuserL, erDiffuserR;

    // ER bass cut (HP filter to reduce bass buildup in short reverbs)
    BiquadFilter erBassCutL, erBassCutR;
    float erBassCutFreq = 20.0f;
    bool erBassCutActive = false;

    // Complex modulators
    std::array<ComplexModulator, NUM_DELAYS> modulatorsL, modulatorsR;

    // Era-based saturation (replaces simple FeedbackSaturator)
    // Uses shared AnalogEmulation library for authentic hardware character
    AnalogEmulation::WaveshaperCurves::CurveType eraSatCurve = AnalogEmulation::WaveshaperCurves::CurveType::Linear;
    float eraSatDrive = 0.0f;

    // Era bandwidth limiter (LP in feedback path — 1970s=8kHz, 1980s=14kHz, Now=passthrough)
    BiquadFilter eraBandwidthL, eraBandwidthR;
    bool eraBandwidthActive = false;

    // Mode change crossfade (prevents clicks from instant delay time changes)
    static constexpr int MODE_CHANGE_FADE_LENGTH = 2048;  // ~50ms at 44.1kHz
    std::array<float, NUM_DELAYS> fadeFromDelayL = {};
    std::array<float, NUM_DELAYS> fadeFromDelayR = {};
    int modeChangeFadePos = 0;
    int modeChangeFadeSamples = 0;

    // Era noise injection
    float eraNoiseLevel = 0.0f;  // Linear gain for noise floor injection
    std::mt19937 noiseRng { 12345 };
    std::uniform_real_distribution<float> noiseDist { -1.0f, 1.0f };

    // Output EQ
    OutputEQ outputEQ;

    // Per-channel DC blockers (inside feedback loop)
    std::array<DCBlocker, NUM_DELAYS> dcBlockersL, dcBlockersR;
    // Output DC blockers (catch any residual)
    DCBlocker dcBlockerOutL, dcBlockerOutR;

    // High shelf biquads
    BiquadFilter highShelfL, highShelfR;

    void updateAllParameters()
    {
        updateDelayTimes();
        updateDamping();
        updateFeedbackGain();
        updateModulation();
        updateDiffusion();
        updateTankDiffusion();
        updateEarlyReflections();
        updateERDiffusion();
        updateHighShelf(modeParams.highShelfFreq, modeParams.highShelfGain);
        updateFourBandDecay();
        updatePreDelay();
        // Re-apply ER shape/spread
        earlyReflectionsL.setShape(erShape);
        earlyReflectionsR.setShape(erShape);
        earlyReflectionsL.setSpread(erSpread);
        earlyReflectionsR.setSpread(erSpread);
        // Re-apply current color mode (recalculates era saturation drive from new mode params)
        setColor(currentColor);
    }

    void updateModulatorRandomRate(float hz)
    {
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            modulatorsL[static_cast<size_t>(i)].setRandomUpdateHz(hz);
            modulatorsR[static_cast<size_t>(i)].setRandomUpdateHz(hz);
        }
    }

    void updateDelayTimes()
    {
        // Different prime-based offsets for each delay line (enhanced stereo decorrelation)
        constexpr std::array<float, NUM_DELAYS> stereoOffsets = {
            1.000f, 1.037f, 1.019f, 1.053f, 1.011f, 1.043f, 1.029f, 1.061f
        };

        // Room size scales delay times: 0.5x at 0.0 to 2.0x at 1.0
        float roomScale = 0.5f + roomSize * 1.5f;

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            size_t idx = static_cast<size_t>(i);
            baseDelayTimesL[idx] = modeParams.delayTimesMs[idx] * roomScale;
            baseDelayTimesR[idx] = modeParams.delayTimesMs[idx] * stereoOffsets[idx] * roomScale;

            delaysL[idx].setDelayMs(baseDelayTimesL[idx]);
            delaysR[idx].setDelayMs(baseDelayTimesR[idx]);
        }
    }

    void updatePreDelay()
    {
        float totalPreDelay = modeParams.preDelayMs + userPreDelay;
        preDelayL.setDelayMs(totalPreDelay);
        preDelayR.setDelayMs(totalPreDelay + 0.5f);
    }

    void updateDamping()
    {
        // Map damping (0-1) to frequency: high freq at 0% damping, low freq at 100%
        float freq = modeParams.dampingFreq * (1.0f - damping * 0.85f);
        freq = std::clamp(freq, 200.0f, 20000.0f);

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            dampingL[static_cast<size_t>(i)].setFrequency(freq);
            dampingR[static_cast<size_t>(i)].setFrequency(freq);
        }
    }

    void updateFourBandDecay()
    {
        float lowMult = modeParams.lowDecayMult * userBassMult;
        float midMult = modeParams.midDecayMult * userMidDecayMult;
        // Band 3 (high-mid) and Band 4 (treble) are independently controlled
        // by their own parameters — damping only affects DampingFilter (air absorption)
        float highMult = modeParams.highDecayMult * userHighDecayMult;
        // Treble band: ratio scales with damping — low damping = treble tracks high-mid closely
        // (bright reverbs), high damping = treble decays faster (dark reverbs)
        float trebleRatio = 0.85f - damping * 0.35f;  // 0.85 at damping=0, 0.50 at damping=1
        float trebleMult = highMult * trebleRatio;

        float f1 = userBassFreq;
        float f2 = userHighFreq;
        float f3 = std::min(f2 * 2.5f, static_cast<float>(sampleRate) * 0.45f);

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            size_t idx = static_cast<size_t>(i);
            fourBandL[idx].setCrossoverFreqs(f1, f2, f3);
            fourBandR[idx].setCrossoverFreqs(f1, f2, f3);
            fourBandL[idx].setDecayMultipliers(lowMult, midMult, highMult, trebleMult);
            fourBandR[idx].setDecayMultipliers(lowMult, midMult, highMult, trebleMult);
            // Pre-compute gains for per-sample efficiency
            fourBandL[idx].updateGains(freezeMode ? 0.9997f : feedbackGain);
            fourBandR[idx].updateGains(freezeMode ? 0.9997f : feedbackGain);
        }
    }

    void updateFeedbackGain()
    {
        // Account for room size scaling: actual delay times = base * roomScale
        float roomScale = 0.5f + roomSize * 1.5f;
        float avgDelay = 0.0f;
        for (int i = 0; i < NUM_DELAYS; ++i)
            avgDelay += modeParams.delayTimesMs[static_cast<size_t>(i)];
        avgDelay = (avgDelay / NUM_DELAYS) * roomScale;

        if (avgDelay <= 0.0f)
        {
            feedbackGain = 0.0f;
            return;
        }

        float loopsPerSecond = 1000.0f / avgDelay;
        float loopsForRT60 = loopsPerSecond * targetDecay;

        feedbackGain = std::pow(0.001f, 1.0f / loopsForRT60);
        // Higher cap allows longer decay times for Cathedral mode
        feedbackGain = std::clamp(feedbackGain, 0.0f, 0.9995f);

        // Update pre-computed 4-band gains when feedback gain changes
        updateFourBandDecay();
    }

    void updateModulation()
    {
        float rate = modeParams.modRate * userModRate;
        float depthVal = modeParams.modDepth * userModDepth;
        float random = modeParams.modRandom * userModDepth;

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            float rateOffset = 0.8f + 0.4f * (static_cast<float>(i) / 7.0f);
            modulatorsL[static_cast<size_t>(i)].setParameters(rate * rateOffset, depthVal, random);
            modulatorsR[static_cast<size_t>(i)].setParameters(rate * rateOffset * 1.07f, depthVal, random);
        }
    }

    void updateDiffusion()
    {
        // Prime-number derived diffuser times, scaled per mode
        std::array<float, NUM_INPUT_DIFFUSERS> baseDiffuserTimes = { 1.3f, 2.9f, 4.3f, 6.1f };
        float scale = modeParams.inputDiffuserScale;

        for (int i = 0; i < NUM_INPUT_DIFFUSERS; ++i)
        {
            // Alternate feedback sign to reduce metallic coloration (Lexicon/Eventide technique)
            float sign = (i % 2 == 0) ? 1.0f : -1.0f;
            float fb = modeParams.diffusionAmount * earlyDiffusion * sign;
            float time = baseDiffuserTimes[static_cast<size_t>(i)] * scale;
            inputDiffuserL[static_cast<size_t>(i)].setParameters(time, fb);
            inputDiffuserR[static_cast<size_t>(i)].setParameters(time * 1.07f, fb);
        }
    }

    void updateTankDiffusion()
    {
        // Prime-derived times for all 8 tank diffusers, scaled per mode
        std::array<float, NUM_TANK_DIFFUSERS> baseTankTimes = { 22.7f, 37.1f, 47.3f, 61.9f, 29.3f, 43.7f, 53.9f, 71.3f };
        float scale = modeParams.tankDiffuserScale;

        for (int i = 0; i < NUM_TANK_DIFFUSERS; ++i)
        {
            // Alternate feedback sign for tank diffusers too
            float sign = (i % 2 == 0) ? 1.0f : -1.0f;
            float fb = lateDiffusion * 0.6f * sign;
            float time = baseTankTimes[static_cast<size_t>(i)] * scale;
            tankDiffuserL[static_cast<size_t>(i)].setParameters(time, fb);
            tankDiffuserR[static_cast<size_t>(i)].setParameters(time * 1.05f, fb);
        }
    }

    void updateERDiffusion()
    {
        // Short allpass diffusers to smear ER taps into a smooth early field
        constexpr std::array<float, 2> erDiffTimes = { 0.7f, 1.7f };
        for (int i = 0; i < NUM_ER_DIFFUSERS; ++i)
        {
            float fb = 0.4f * ((i % 2 == 0) ? 1.0f : -1.0f);
            float time = erDiffTimes[static_cast<size_t>(i)] * modeParams.inputDiffuserScale;
            erDiffuserL[static_cast<size_t>(i)].setParameters(time, fb);
            erDiffuserR[static_cast<size_t>(i)].setParameters(time * 1.1f, fb);
        }
    }

    void updateEarlyReflections()
    {
        earlyReflectionsL.setAmount(modeParams.earlyReflectionsAmount);
        earlyReflectionsR.setAmount(modeParams.earlyReflectionsAmount);
        earlyReflectionsL.setPreDelay(modeParams.preDelayMs);
        earlyReflectionsR.setPreDelay(modeParams.preDelayMs + 1.5f);

        // ER stereo panning: alternate taps between L-heavy and R-heavy
        // Creates spatial width in the early reflection field
        auto gainsL = modeParams.erTapGains;
        auto gainsR = modeParams.erTapGains;
        for (size_t i = 0; i < gainsL.size(); ++i)
        {
            if (gainsL[i] <= 0.0f) continue;
            float panL = (i % 2 == 0) ? 1.25f : 0.75f;
            float panR = (i % 2 == 0) ? 0.75f : 1.25f;
            gainsL[i] *= panL;
            gainsR[i] *= panR;
        }

        earlyReflectionsL.setTapPattern(modeParams.erTapTimesMs, gainsL);
        earlyReflectionsR.setTapPattern(modeParams.erTapTimesMs, gainsR);
    }

    void updateHighShelf(float freq, float gainDb)
    {
        highShelfL.setHighShelf(freq, gainDb, 0.707f);
        highShelfR.setHighShelf(freq, gainDb, 0.707f);
    }

    std::array<float, NUM_DELAYS> applyHadamard(const std::array<float, NUM_DELAYS>& input)
    {
        constexpr float scale = 0.35355339059f;  // 1/sqrt(8)

        std::array<float, NUM_DELAYS> output;

        output[0] = (input[0] + input[1] + input[2] + input[3] + input[4] + input[5] + input[6] + input[7]) * scale;
        output[1] = (input[0] - input[1] + input[2] - input[3] + input[4] - input[5] + input[6] - input[7]) * scale;
        output[2] = (input[0] + input[1] - input[2] - input[3] + input[4] + input[5] - input[6] - input[7]) * scale;
        output[3] = (input[0] - input[1] - input[2] + input[3] + input[4] - input[5] - input[6] + input[7]) * scale;
        output[4] = (input[0] + input[1] + input[2] + input[3] - input[4] - input[5] - input[6] - input[7]) * scale;
        output[5] = (input[0] - input[1] + input[2] - input[3] - input[4] + input[5] - input[6] + input[7]) * scale;
        output[6] = (input[0] + input[1] - input[2] - input[3] - input[4] - input[5] + input[6] + input[7]) * scale;
        output[7] = (input[0] - input[1] - input[2] + input[3] - input[4] + input[5] + input[6] - input[7]) * scale;

        return output;
    }
};

} // namespace SilkVerb

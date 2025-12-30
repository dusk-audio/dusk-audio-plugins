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

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <random>
#include <vector>

namespace SilkVerb {

//==============================================================================
// Constants
constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 6.28318530718f;

//==============================================================================
// Color mode enumeration
enum class ColorMode
{
    Modern = 0,
    Vintage
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
// Two-band decay filter with biquad crossover (Linkwitz-Riley style)
class TwoBandDecayFilter
{
public:
    void prepare(double sr)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        lowpass.prepare(sr);
        highpass.prepare(sr);
        updateCoefficients();
    }

    void clear()
    {
        lowpass.clear();
        highpass.clear();
    }

    void setCrossoverFreq(float freq)
    {
        crossoverFreq = std::clamp(freq, 100.0f, 4000.0f);
        updateCoefficients();
    }

    void setDecayMultipliers(float lowMult, float highMult)
    {
        lowDecayMult = std::clamp(lowMult, 0.25f, 2.0f);
        highDecayMult = std::clamp(highMult, 0.25f, 2.0f);
    }

    float process(float input, float baseGain)
    {
        // Split using biquad filters
        float low = lowpass.process(input);
        float high = highpass.process(input);

        // Apply different decay multipliers to each band
        float lowGain = std::pow(baseGain, 1.0f / lowDecayMult);
        float highGain = std::pow(baseGain, 1.0f / highDecayMult);

        // Safety clamp
        lowGain = std::min(lowGain, 0.9999f);
        highGain = std::min(highGain, 0.9999f);

        return low * lowGain + high * highGain;
    }

private:
    void updateCoefficients()
    {
        lowpass.setLowPass(crossoverFreq, 0.5f);   // Q=0.5 for Butterworth response
        highpass.setHighPass(crossoverFreq, 0.5f);
    }

    double sampleRate = 44100.0;
    float crossoverFreq = 500.0f;
    float lowDecayMult = 1.0f;
    float highDecayMult = 1.0f;
    BiquadFilter lowpass, highpass;
};

//==============================================================================
// One-pole lowpass for damping (high frequency absorption)
class DampingFilter
{
public:
    void setCoefficient(float newCoeff)
    {
        coeff = std::clamp(newCoeff, 0.0f, 0.999f);
    }

    void clear() { state = 0.0f; }

    float process(float input)
    {
        state = input * (1.0f - coeff) + state * coeff;
        return state;
    }

private:
    float coeff = 0.5f;
    float state = 0.0f;
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
// Early reflections generator
class EarlyReflections
{
public:
    static constexpr int NUM_TAPS = 8;

    void prepare(double sr)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        // Max tap (53.7ms) + max pre-delay (50ms) + margin + time scaling
        int maxSamples = std::max(2, static_cast<int>(0.25 * sampleRate));
        buffer.resize(static_cast<size_t>(maxSamples), 0.0f);
        writePos = 0;

        tapTimesMs = { 3.1f, 7.2f, 11.7f, 17.3f, 23.9f, 31.1f, 41.3f, 53.7f };
        tapGains = { 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.35f, 0.3f, 0.25f };

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
            int readPos = writePos - tapPositions[static_cast<size_t>(i)];
            if (readPos < 0) readPos += static_cast<int>(buffer.size());
            output += buffer[static_cast<size_t>(readPos)] * tapGains[static_cast<size_t>(i)];
        }

        writePos = (writePos + 1) % static_cast<int>(buffer.size());
        return output * amount;
    }

private:
    void updateTapPositions()
    {
        for (int i = 0; i < NUM_TAPS; ++i)
        {
            float totalMs = preDelayMs + tapTimesMs[static_cast<size_t>(i)] * timeScale;
            tapPositions[static_cast<size_t>(i)] = static_cast<int>(totalMs * 0.001 * sampleRate);
            tapPositions[static_cast<size_t>(i)] = std::min(tapPositions[static_cast<size_t>(i)],
                                                            static_cast<int>(buffer.size() - 1));
        }
    }

    std::vector<float> buffer;
    double sampleRate = 44100.0;
    int writePos = 0;
    float amount = 0.1f;
    float preDelayMs = 0.0f;
    float timeScale = 1.0f;

    std::array<float, NUM_TAPS> tapTimesMs = {};
    std::array<float, NUM_TAPS> tapGains = {};
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
        randomCurrent = 0.0f;
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
        float lfo1 = std::sin(static_cast<float>(phase1 * TWO_PI)) * 0.5f;
        float lfo2 = std::sin(static_cast<float>(phase2 * TWO_PI)) * 0.3f;
        float lfo3 = std::sin(static_cast<float>(phase3 * TWO_PI)) * 0.2f;

        // Random component (smoothed noise)
        randomCounter++;
        if (randomCounter >= randomUpdateRate)
        {
            randomCounter = 0;
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            randomTarget = dist(rng);
        }
        // Smooth random value
        randomCurrent += (randomTarget - randomCurrent) * 0.001f;

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

private:
    void updateIncrements()
    {
        increment1 = rate1 / sampleRate;
        increment2 = rate2 / sampleRate;
        increment3 = rate3 / sampleRate;
        randomUpdateRate = static_cast<int>(sampleRate / 30.0);  // Update ~30Hz
    }

    double sampleRate = 44100.0;
    double phase1 = 0.0, phase2 = 0.0, phase3 = 0.0;
    double increment1 = 0.0, increment2 = 0.0, increment3 = 0.0;
    float rate1 = 0.5f, rate2 = 0.8f, rate3 = 0.2f;
    float depth = 0.3f;
    float randomAmount = 0.2f;

    std::mt19937 rng;
    float randomTarget = 0.0f;
    float randomCurrent = 0.0f;
    int randomCounter = 0;
    int randomUpdateRate = 1470;
};

//==============================================================================
// Soft saturation with soft-knee for feedback path
class FeedbackSaturator
{
public:
    void setDrive(float drv)
    {
        drive = std::clamp(drv, 0.0f, 1.0f);
        // Pre-calculate knee threshold
        threshold = 0.8f - drive * 0.3f;  // Lower threshold with more drive
    }

    void setVintageMode(bool vintage)
    {
        vintageMode = vintage;
    }

    float process(float input)
    {
        if (drive < 0.001f) return input;

        float x = input;
        float sign = (x >= 0.0f) ? 1.0f : -1.0f;
        float absX = std::abs(x);

        if (vintageMode)
        {
            // Tube-style: asymmetric soft clipping with even harmonics
            float shaped;
            if (absX < threshold)
            {
                shaped = x;  // Linear below threshold
            }
            else
            {
                // Soft knee into tanh
                float excess = absX - threshold;
                float knee = threshold + std::tanh(excess * (1.0f + drive)) * (1.0f - threshold);
                shaped = sign * knee;
            }

            // Add slight even harmonic content (asymmetry)
            return shaped + drive * 0.1f * shaped * shaped * sign;
        }
        else
        {
            // Modern mode: clean soft clip with soft knee
            if (absX < threshold)
                return x;

            float excess = absX - threshold;
            float compressed = threshold + std::tanh(excess * 2.0f) * (1.0f - threshold);
            return sign * compressed;
        }
    }

private:
    float drive = 0.1f;
    float threshold = 0.7f;
    bool vintageMode = false;
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
    Hall
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
    float crossoverFreq;
    float lowDecayMult;
    float highDecayMult;
    float saturationDrive;
    float erToLateBlend;  // Early reflections crossfeed to late reverb
};

// Prime-number based delay times for reduced metallic resonance
inline ModeParameters getPlateParameters()
{
    return {
        // Prime-derived delays - longer for better decay accumulation
        { 17.3f, 23.9f, 31.3f, 41.7f, 53.1f, 67.3f, 79.9f, 97.3f },
        0.35f,          // Damping base (reduced for longer decay)
        3500.0f,        // Damping freq (higher = less HF loss)
        2.0f,           // High shelf gain (bright plate)
        7000.0f,        // High shelf freq
        1.8f,           // Mod rate (faster for shimmer)
        1.0f,           // Mod depth
        0.35f,          // Random modulation
        0.75f,          // High diffusion
        0.0f,           // No early reflections (plate characteristic)
        0.0f,           // No pre-delay
        1.2f,           // Extended decay multiplier
        1000.0f,        // Crossover freq
        1.15f,          // Low decay slightly longer
        0.9f,           // High decay slightly shorter (but less aggressive)
        0.06f,          // Subtle saturation
        0.0f            // No ER crossfeed
    };
}

inline ModeParameters getRoomParameters()
{
    return {
        // Prime-derived delays
        { 13.1f, 19.7f, 27.1f, 33.7f, 41.3f, 49.9f, 59.3f, 67.9f },
        0.45f,          // Lighter damping
        2500.0f,        // Higher damping freq
        0.0f,           // Flat response
        8000.0f,        // High shelf freq
        1.2f,           // Moderate mod rate
        0.6f,           // Less modulation
        0.25f,          // Less random
        0.6f,           // Medium diffusion
        0.15f,          // Subtle early reflections
        12.0f,          // 12ms pre-delay
        0.9f,           // Slightly shorter decay
        600.0f,         // Lower crossover
        1.2f,           // Longer low decay (room boom)
        0.7f,           // Shorter high decay
        0.05f,          // Very subtle saturation
        0.2f            // Some ER to late blend
    };
}

inline ModeParameters getHallParameters()
{
    return {
        // Prime-derived delays, longer for hall
        { 41.3f, 53.9f, 67.1f, 79.9f, 97.3f, 113.9f, 131.3f, 149.9f },
        0.5f,           // Medium damping
        2000.0f,        // Lower damping freq (darker)
        -1.5f,          // Slight high cut
        5000.0f,        // Lower shelf freq
        0.6f,           // Slow modulation
        0.8f,           // Moderate depth
        0.2f,           // Subtle random
        0.8f,           // High diffusion (smooth)
        0.12f,          // Moderate early reflections
        25.0f,          // 25ms pre-delay
        1.4f,           // Extended decay
        500.0f,         // Low crossover
        1.3f,           // Much longer low decay
        0.6f,           // Shorter high decay (air absorption)
        0.03f,          // Minimal saturation
        0.15f           // ER to late blend
    };
}

//==============================================================================
// Main FDN Reverb Engine (Lexicon/Valhalla-enhanced with professional upgrades)
class FDNReverb
{
public:
    static constexpr int NUM_DELAYS = 8;
    static constexpr int NUM_INPUT_DIFFUSERS = 4;
    static constexpr int NUM_TANK_DIFFUSERS = 2;

    void prepare(double sr, int /*maxBlockSize*/)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;

        // Prepare delay lines
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delaysL[static_cast<size_t>(i)].prepare(sampleRate, 200.0f);
            delaysR[static_cast<size_t>(i)].prepare(sampleRate, 200.0f);
            dampingL[static_cast<size_t>(i)].clear();
            dampingR[static_cast<size_t>(i)].clear();
            twoBandL[static_cast<size_t>(i)].prepare(sampleRate);
            twoBandR[static_cast<size_t>(i)].prepare(sampleRate);
            modulatorsL[static_cast<size_t>(i)].prepare(sampleRate, i);
            modulatorsR[static_cast<size_t>(i)].prepare(sampleRate, i + NUM_DELAYS);
        }

        // Prepare pre-delay
        preDelayL.prepare(sampleRate, 150.0f);
        preDelayR.prepare(sampleRate, 150.0f);

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

        // Prepare output EQ
        outputEQ.prepare(sampleRate);

        // Prepare DC blockers
        dcBlockerL.prepare(sampleRate);
        dcBlockerR.prepare(sampleRate);

        // Prepare high shelf biquads
        highShelfL.prepare(sampleRate);
        highShelfR.prepare(sampleRate);

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
            twoBandL[static_cast<size_t>(i)].clear();
            twoBandR[static_cast<size_t>(i)].clear();
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
        outputEQ.clear();
        dcBlockerL.clear();
        dcBlockerR.clear();
        highShelfL.clear();
        highShelfR.clear();

        feedbackL.fill(0.0f);
        feedbackR.fill(0.0f);
    }

    void setMode(ReverbMode mode)
    {
        currentMode = mode;

        switch (mode)
        {
            case ReverbMode::Plate: modeParams = getPlateParameters(); break;
            case ReverbMode::Room:  modeParams = getRoomParameters();  break;
            case ReverbMode::Hall:  modeParams = getHallParameters();  break;
        }

        updateAllParameters();
    }

    void setColor(ColorMode color)
    {
        currentColor = color;
        saturator.setVintageMode(color == ColorMode::Vintage);

        // Adjust parameters based on color mode
        if (color == ColorMode::Vintage)
        {
            // Vintage: more saturation, slightly darker, more modulation
            saturator.setDrive(modeParams.saturationDrive * 2.0f);
        }
        else
        {
            // Modern: cleaner, brighter
            saturator.setDrive(modeParams.saturationDrive);
        }
    }

    void setSize(float sz)
    {
        size = std::clamp(sz, 0.0f, 1.0f);
        // Exponential curve for more usable range: 0.3s to 10s
        float decaySeconds = 0.3f + std::pow(size, 1.5f) * 9.7f;
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
        freezeMode = frozen;
    }

    // New Valhalla-style parameters
    void setPreDelay(float ms)
    {
        userPreDelay = std::clamp(ms, 0.0f, 100.0f);
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
        userBassMult = std::clamp(mult, 0.5f, 2.0f);
        updateTwoBandDecay();
    }

    void setBassFreq(float freq)
    {
        userBassFreq = std::clamp(freq, 100.0f, 1000.0f);
        updateTwoBandDecay();
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

        // Crossfeed early reflections to late reverb input
        float erCrossfeed = modeParams.erToLateBlend;
        float lateInputL = preDelayedL + earlyL * erCrossfeed;
        float lateInputR = preDelayedR + earlyR * erCrossfeed;

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

        // Use freeze feedback gain or normal
        float currentFeedbackGain = freezeMode ? 0.9997f : feedbackGain;

        // FDN processing
        std::array<float, NUM_DELAYS> delayOutputsL, delayOutputsR;

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            size_t idx = static_cast<size_t>(i);

            // Complex modulation
            float modL = modulatorsL[idx].process();
            float modR = modulatorsR[idx].process();

            float modDelayL = baseDelayTimesL[idx] + modL;
            float modDelayR = baseDelayTimesR[idx] + modR;

            delaysL[idx].setDelayMs(modDelayL);
            delaysR[idx].setDelayMs(modDelayR);

            // Two-band decay processing
            float decayedL = twoBandL[idx].process(feedbackL[idx], currentFeedbackGain);
            float decayedR = twoBandR[idx].process(feedbackR[idx], currentFeedbackGain);

            // Additional high-frequency damping
            delayOutputsL[idx] = dampingL[idx].process(decayedL);
            delayOutputsR[idx] = dampingR[idx].process(decayedR);
        }

        // Hadamard matrix mixing
        std::array<float, NUM_DELAYS> mixedL = applyHadamard(delayOutputsL);
        std::array<float, NUM_DELAYS> mixedR = applyHadamard(delayOutputsR);

        // Write to delays with saturation and tank diffusion
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            size_t idx = static_cast<size_t>(i);

            float inputToDelayL = mixedL[idx] + diffusedL * 0.25f;
            float inputToDelayR = mixedR[idx] + diffusedR * 0.25f;

            // Subtle saturation in feedback path
            inputToDelayL = saturator.process(inputToDelayL);
            inputToDelayR = saturator.process(inputToDelayR);

            // Tank diffusion (late diffusion) - applied to some delay lines
            if (idx < NUM_TANK_DIFFUSERS)
            {
                inputToDelayL = tankDiffuserL[idx].process(inputToDelayL);
                inputToDelayR = tankDiffuserR[idx].process(inputToDelayR);
            }

            feedbackL[idx] = delaysL[idx].process(inputToDelayL);
            feedbackR[idx] = delaysR[idx].process(inputToDelayR);
        }

        // Sum delay outputs
        float wetL = 0.0f, wetR = 0.0f;
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            wetL += delayOutputsL[static_cast<size_t>(i)];
            wetR += delayOutputsR[static_cast<size_t>(i)];
        }
        wetL *= 0.25f;
        wetR *= 0.25f;

        // Add early reflections to output
        wetL += earlyL;
        wetR += earlyR;

        // DC blocking
        wetL = dcBlockerL.process(wetL);
        wetR = dcBlockerR.process(wetR);

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
    ColorMode currentColor = ColorMode::Modern;
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
    std::array<TwoBandDecayFilter, NUM_DELAYS> twoBandL, twoBandR;

    // Pre-delay
    DelayLine preDelayL, preDelayR;

    // Diffusers (early and late/tank)
    std::array<AllpassFilter, NUM_INPUT_DIFFUSERS> inputDiffuserL, inputDiffuserR;
    std::array<AllpassFilter, NUM_TANK_DIFFUSERS> tankDiffuserL, tankDiffuserR;

    // Early reflections
    EarlyReflections earlyReflectionsL, earlyReflectionsR;

    // Complex modulators
    std::array<ComplexModulator, NUM_DELAYS> modulatorsL, modulatorsR;

    // Saturation
    FeedbackSaturator saturator;

    // Output EQ
    OutputEQ outputEQ;

    // DC blockers
    DCBlocker dcBlockerL, dcBlockerR;

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
        updateHighShelf(modeParams.highShelfFreq, modeParams.highShelfGain);
        updateTwoBandDecay();
        updatePreDelay();
        saturator.setDrive(modeParams.saturationDrive);
    }

    void updateDelayTimes()
    {
        // Different prime-based offsets for each delay line (enhanced stereo decorrelation)
        constexpr std::array<float, NUM_DELAYS> stereoOffsets = {
            1.000f, 1.037f, 1.019f, 1.053f, 1.011f, 1.043f, 1.029f, 1.061f
        };

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            size_t idx = static_cast<size_t>(i);
            baseDelayTimesL[idx] = modeParams.delayTimesMs[idx];
            baseDelayTimesR[idx] = modeParams.delayTimesMs[idx] * stereoOffsets[idx];

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
        float totalDamping = modeParams.dampingBase + damping * 0.35f;
        totalDamping = std::clamp(totalDamping, 0.0f, 0.95f);

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            dampingL[static_cast<size_t>(i)].setCoefficient(totalDamping);
            dampingR[static_cast<size_t>(i)].setCoefficient(totalDamping);
        }
    }

    void updateTwoBandDecay()
    {
        float lowMult = modeParams.lowDecayMult * userBassMult;
        float highMult = modeParams.highDecayMult;
        float crossover = userBassFreq;

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            twoBandL[static_cast<size_t>(i)].setCrossoverFreq(crossover);
            twoBandR[static_cast<size_t>(i)].setCrossoverFreq(crossover);
            twoBandL[static_cast<size_t>(i)].setDecayMultipliers(lowMult, highMult);
            twoBandR[static_cast<size_t>(i)].setDecayMultipliers(lowMult, highMult);
        }
    }

    void updateFeedbackGain()
    {
        float avgDelay = 0.0f;
        for (int i = 0; i < NUM_DELAYS; ++i)
            avgDelay += modeParams.delayTimesMs[static_cast<size_t>(i)];
        avgDelay /= NUM_DELAYS;

        if (avgDelay <= 0.0f)
        {
            feedbackGain = 0.0f;
            return;
        }

        float loopsPerSecond = 1000.0f / avgDelay;
        float loopsForRT60 = loopsPerSecond * targetDecay;

        feedbackGain = std::pow(0.001f, 1.0f / loopsForRT60);
        // Limit max feedback to prevent runaway - higher cap for longer decays
        feedbackGain = std::clamp(feedbackGain, 0.0f, 0.995f);
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
        // Prime-number derived diffuser times for reduced metallic resonance
        std::array<float, NUM_INPUT_DIFFUSERS> diffuserTimes = { 1.3f, 2.9f, 4.3f, 6.1f };

        for (int i = 0; i < NUM_INPUT_DIFFUSERS; ++i)
        {
            float fb = modeParams.diffusionAmount * earlyDiffusion;
            inputDiffuserL[static_cast<size_t>(i)].setParameters(
                diffuserTimes[static_cast<size_t>(i)], fb);
            inputDiffuserR[static_cast<size_t>(i)].setParameters(
                diffuserTimes[static_cast<size_t>(i)] * 1.07f, fb);
        }
    }

    void updateTankDiffusion()
    {
        // Longer delays for tank diffusers
        std::array<float, NUM_TANK_DIFFUSERS> tankTimes = { 22.7f, 37.1f };

        for (int i = 0; i < NUM_TANK_DIFFUSERS; ++i)
        {
            float fb = lateDiffusion * 0.6f;
            tankDiffuserL[static_cast<size_t>(i)].setParameters(
                tankTimes[static_cast<size_t>(i)], fb);
            tankDiffuserR[static_cast<size_t>(i)].setParameters(
                tankTimes[static_cast<size_t>(i)] * 1.05f, fb);
        }
    }

    void updateEarlyReflections()
    {
        earlyReflectionsL.setAmount(modeParams.earlyReflectionsAmount);
        earlyReflectionsR.setAmount(modeParams.earlyReflectionsAmount);
        earlyReflectionsL.setPreDelay(modeParams.preDelayMs);
        earlyReflectionsR.setPreDelay(modeParams.preDelayMs + 1.5f);
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

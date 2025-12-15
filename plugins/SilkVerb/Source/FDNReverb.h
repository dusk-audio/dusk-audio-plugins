/*
  ==============================================================================

    FDNReverb.h
    Feedback Delay Network Reverb Engine for SilkVerb

    8-channel stereo FDN with orthogonal Hadamard matrix feedback,
    per-channel allpass diffusers, and mode-specific delay times.

    Enhanced with Lexicon/Valhalla-style features:
    - Two-band decay (separate low/high frequency decay)
    - Complex modulation (multiple uncorrelated LFOs + random)
    - Subtle feedback saturation
    - Pre-delay with crossfeed to late reverb
    - Output EQ (highcut/lowcut)
    - Early/Late diffusion controls
    - Color modes (Modern/Vintage)

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <random>

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
// Simple delay line with linear interpolation
class DelayLine
{
public:
    void prepare(double sr, float maxDelayMs)
    {
        if (sr <= 0.0 || maxDelayMs <= 0.0f)
        {
            sampleRate = 44100.0;
            buffer.resize(2, 0.0f);
            writePos = 0;
            return;
        }
        sampleRate = sr;
        int maxSamples = std::max(2, static_cast<int>(maxDelayMs * 0.001 * sampleRate) + 1);
        buffer.resize(static_cast<size_t>(maxSamples), 0.0f);
        writePos = 0;
    }

    void clear()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void setDelayMs(float delayMs)
    {
        delaySamples = static_cast<float>(delayMs * 0.001 * sampleRate);
        delaySamples = std::max(1.0f, std::min(delaySamples, static_cast<float>(buffer.size() - 1)));
    }

    float process(float input)
    {
        buffer[static_cast<size_t>(writePos)] = input;

        // Read with linear interpolation
        float readPos = static_cast<float>(writePos) - delaySamples;
        if (readPos < 0.0f)
            readPos += static_cast<float>(buffer.size());

        int idx0 = static_cast<int>(readPos);
        size_t idx1 = static_cast<size_t>((idx0 + 1) % static_cast<int>(buffer.size()));
        float frac = readPos - static_cast<float>(idx0);

        float output = buffer[static_cast<size_t>(idx0)] * (1.0f - frac) + buffer[idx1] * frac;

        writePos = (writePos + 1) % static_cast<int>(buffer.size());
        return output;
    }

private:
    std::vector<float> buffer;
    double sampleRate = 44100.0;
    float delaySamples = 1.0f;
    int writePos = 0;
};

//==============================================================================
// Two-band decay filter (Lexicon-style low/high frequency decay control)
class TwoBandDecayFilter
{
public:
    void prepare(double sr)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        updateCoefficients();
    }

    void clear()
    {
        lowState = 0.0f;
        highState = 0.0f;
    }

    void setCrossoverFreq(float freq)
    {
        crossoverFreq = std::clamp(freq, 100.0f, 8000.0f);
        updateCoefficients();
    }

    void setDecayMultipliers(float lowMult, float highMult)
    {
        // Multipliers: 0.5 = half decay time, 2.0 = double decay time
        lowDecayMult = std::clamp(lowMult, 0.25f, 2.0f);
        highDecayMult = std::clamp(highMult, 0.25f, 2.0f);
    }

    float process(float input, float baseGain)
    {
        // Split into low and high bands
        lowState += crossoverCoeff * (input - lowState);
        float low = lowState;
        float high = input - low;

        // Apply different decay multipliers to each band
        // Clamp the resulting gains to prevent runaway feedback
        float lowGain = std::pow(baseGain, 1.0f / lowDecayMult);
        float highGain = std::pow(baseGain, 1.0f / highDecayMult);

        // Ensure gains never exceed 0.999 to prevent instability
        lowGain = std::min(lowGain, 0.999f);
        highGain = std::min(highGain, 0.999f);

        return low * lowGain + high * highGain;
    }

private:
    void updateCoefficients()
    {
        float w = TWO_PI * crossoverFreq / static_cast<float>(sampleRate);
        crossoverCoeff = w / (w + 1.0f);
    }

    double sampleRate = 44100.0;
    float crossoverFreq = 1000.0f;
    float crossoverCoeff = 0.1f;
    float lowDecayMult = 1.0f;
    float highDecayMult = 1.0f;
    float lowState = 0.0f;
    float highState = 0.0f;
};

//==============================================================================
// One-pole lowpass for damping (high frequency absorption)
class DampingFilter
{
public:
    void setCoefficient(float coeff)
    {
        this->coeff = std::clamp(coeff, 0.0f, 0.999f);
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
// Allpass filter for diffusion
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
        feedback = std::clamp(fb, -0.9f, 0.9f);
    }

    void clear()
    {
        delay.clear();
        lastOutput = 0.0f;
    }

    float process(float input)
    {
        float delayed = delay.process(input + lastOutput * feedback);
        lastOutput = delayed;
        return delayed - input * feedback;
    }

private:
    DelayLine delay;
    float feedback = 0.5f;
    float lastOutput = 0.0f;
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
        // Max tap (53.7ms) + max pre-delay (50ms) + margin
        int maxSamples = std::max(2, static_cast<int>(0.12 * sampleRate));
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

    void setPreDelay(float preDelayMs)
    {
        this->preDelayMs = std::clamp(preDelayMs, 0.0f, 50.0f);
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
            float totalMs = preDelayMs + tapTimesMs[static_cast<size_t>(i)];
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

        // Each modulator gets unique phase offsets based on index
        phase1 = static_cast<double>(index) * 0.13;
        phase2 = static_cast<double>(index) * 0.29;
        phase3 = static_cast<double>(index) * 0.47;

        // Initialize random generator with index-based seed
        rng.seed(static_cast<unsigned int>(42 + index * 17));
        randomTarget = 0.0f;
        randomCurrent = 0.0f;
        randomCounter = 0;
    }

    void setParameters(float baseRate, float depth, float randomAmount)
    {
        // Primary LFO
        rate1 = baseRate;
        // Secondary LFO at golden ratio offset
        rate2 = baseRate * 1.618f;
        // Tertiary LFO at slower rate
        rate3 = baseRate * 0.382f;

        this->depth = depth;
        this->randomAmount = randomAmount;

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
// Soft saturation for feedback path (subtle analog warmth)
class FeedbackSaturator
{
public:
    void setDrive(float drv)
    {
        drive = std::clamp(drv, 0.0f, 1.0f);
    }

    void setVintageMode(bool vintage)
    {
        vintageMode = vintage;
    }

    float process(float input)
    {
        if (drive < 0.001f) return input;

        // Soft saturation curve (asymmetric for analog character)
        float x = input * (1.0f + drive * 2.0f);

        if (vintageMode)
        {
            // Vintage mode: more harmonics, tube-like asymmetric clipping
            if (x > 0.0f)
                return std::tanh(x * 1.5f) / 1.5f;
            else
                return std::tanh(x * 0.7f) / 0.7f;
        }
        else
        {
            // Modern mode: cleaner, more symmetric
            return std::tanh(x);
        }
    }

private:
    float drive = 0.1f;
    bool vintageMode = false;
};

//==============================================================================
// Output EQ filters (highcut/lowcut)
class OutputEQ
{
public:
    void prepare(double sr)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        updateCoefficients();
    }

    void clear()
    {
        highcutStateL = 0.0f;
        highcutStateR = 0.0f;
        lowcutStateL = 0.0f;
        lowcutStateR = 0.0f;
    }

    void setHighCut(float freq)
    {
        highcutFreq = std::clamp(freq, 1000.0f, 20000.0f);
        updateCoefficients();
    }

    void setLowCut(float freq)
    {
        lowcutFreq = std::clamp(freq, 20.0f, 500.0f);
        updateCoefficients();
    }

    void process(float& left, float& right)
    {
        // Highcut (lowpass)
        highcutStateL += highcutCoeff * (left - highcutStateL);
        highcutStateR += highcutCoeff * (right - highcutStateR);
        left = highcutStateL;
        right = highcutStateR;

        // Lowcut (highpass)
        lowcutStateL += lowcutCoeff * (left - lowcutStateL);
        lowcutStateR += lowcutCoeff * (right - lowcutStateR);
        left = left - lowcutStateL;
        right = right - lowcutStateR;
    }

private:
    void updateCoefficients()
    {
        float wHigh = TWO_PI * highcutFreq / static_cast<float>(sampleRate);
        highcutCoeff = wHigh / (wHigh + 1.0f);

        float wLow = TWO_PI * lowcutFreq / static_cast<float>(sampleRate);
        lowcutCoeff = wLow / (wLow + 1.0f);
    }

    double sampleRate = 44100.0;
    float highcutFreq = 12000.0f;
    float lowcutFreq = 20.0f;
    float highcutCoeff = 0.9f;
    float lowcutCoeff = 0.01f;

    float highcutStateL = 0.0f;
    float highcutStateR = 0.0f;
    float lowcutStateL = 0.0f;
    float lowcutStateR = 0.0f;
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
        // Prime-derived delays to reduce metallic resonance
        { 7.3f, 11.7f, 17.3f, 23.9f, 31.3f, 37.9f, 43.7f, 53.1f },
        0.65f,          // Damping base
        1200.0f,        // Damping freq
        2.5f,           // High shelf gain (bright plate)
        6000.0f,        // High shelf freq
        1.8f,           // Mod rate (faster for shimmer)
        1.2f,           // Mod depth (more for plate character)
        0.4f,           // Random modulation
        0.75f,          // High diffusion
        0.0f,           // No early reflections (plate characteristic)
        0.0f,           // No pre-delay
        1.0f,           // Normal decay
        800.0f,         // Crossover freq
        1.1f,           // Low decay slightly longer
        0.85f,          // High decay slightly shorter
        0.08f,          // Subtle saturation
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
// Main FDN Reverb Engine (Lexicon/Valhalla-enhanced)
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

        // Prepare high shelf
        updateHighShelf(7000.0f, 0.0f);

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

        feedbackL.fill(0.0f);
        feedbackR.fill(0.0f);

        highShelfStateL = 0.0f;
        highShelfStateR = 0.0f;
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
        float decaySeconds = 0.5f + size * 4.5f;
        targetDecay = decaySeconds * modeParams.decayMultiplier;
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

    void setModDepth(float depth)
    {
        userModDepth = std::clamp(depth, 0.0f, 1.0f);
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
        // Pre-delay
        float preDelayedL = preDelayL.process(inputL);
        float preDelayedR = preDelayR.process(inputR);

        // Early reflections (from dry input)
        float earlyL = earlyReflectionsL.process(inputL);
        float earlyR = earlyReflectionsR.process(inputR);

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
            float decayedL = twoBandL[idx].process(feedbackL[idx], feedbackGain);
            float decayedR = twoBandR[idx].process(feedbackR[idx], feedbackGain);

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

        // High shelf
        wetL = processHighShelf(wetL, highShelfStateL);
        wetR = processHighShelf(wetR, highShelfStateR);

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

    // High shelf state
    float highShelfCoeff = 0.0f;
    float highShelfGain = 1.0f;
    float highShelfStateL = 0.0f;
    float highShelfStateR = 0.0f;

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
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            size_t idx = static_cast<size_t>(i);
            baseDelayTimesL[idx] = modeParams.delayTimesMs[idx];
            baseDelayTimesR[idx] = modeParams.delayTimesMs[idx] * 1.017f;

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
        // Limit max feedback to prevent runaway when combined with two-band decay
        feedbackGain = std::clamp(feedbackGain, 0.0f, 0.97f);
    }

    void updateModulation()
    {
        float rate = modeParams.modRate * userModRate;
        float depth = modeParams.modDepth * userModDepth;
        float random = modeParams.modRandom * userModDepth;

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            float rateOffset = 0.8f + 0.4f * (static_cast<float>(i) / 7.0f);
            modulatorsL[static_cast<size_t>(i)].setParameters(rate * rateOffset, depth, random);
            modulatorsR[static_cast<size_t>(i)].setParameters(rate * rateOffset * 1.07f, depth, random);
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
        float w = TWO_PI * freq / static_cast<float>(sampleRate);
        highShelfCoeff = w / (w + 1.0f);
        highShelfGain = std::pow(10.0f, gainDb / 20.0f);
    }

    float processHighShelf(float input, float& state)
    {
        float high = input - state;
        state += highShelfCoeff * high;
        return state + high * highShelfGain;
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

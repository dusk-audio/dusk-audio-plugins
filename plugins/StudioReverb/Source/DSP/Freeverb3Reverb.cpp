/*
  ==============================================================================

    Freeverb3Reverb.cpp
    Implementation matching Dragonfly's Freeverb3 algorithms

  ==============================================================================
*/

#include "Freeverb3Reverb.h"

// Define M_PI if not already defined
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//==============================================================================
// EarlyReflections implementation (based on fv3::earlyref)
//==============================================================================

void Freeverb3Reverb::EarlyReflections::AllpassFilter::setSize(int samples)
{
    buffer.resize(samples);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::EarlyReflections::AllpassFilter::setFeedback(float g)
{
    feedback = juce::jlimit(-0.99f, 0.99f, g);
}

float Freeverb3Reverb::EarlyReflections::AllpassFilter::process(float input)
{
    if (buffer.empty()) return input;

    float delayed = buffer[writePos];
    float output = -input + delayed;
    buffer[writePos] = input + delayed * feedback;
    writePos = (writePos + 1) % static_cast<int>(buffer.size());

    return output;
}

void Freeverb3Reverb::EarlyReflections::AllpassFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::EarlyReflections::prepare(double sr)
{
    sampleRate = sr;
    int maxDelaySamples = static_cast<int>(maxDelayMs * sampleRate * 0.001);

    delayLineL.resize(maxDelaySamples);
    delayLineR.resize(maxDelaySamples);
    std::fill(delayLineL.begin(), delayLineL.end(), 0.0f);
    std::fill(delayLineR.begin(), delayLineR.end(), 0.0f);

    // Initialize diffusion allpasses (matching Dragonfly)
    float diffusionTimes[] = {4.771f, 3.595f, 2.734f, 1.987f};
    for (int i = 0; i < 4; ++i)
    {
        int size = static_cast<int>(diffusionTimes[i] * sampleRate * 0.001);
        diffusionL[i].setSize(size);
        diffusionR[i].setSize(size * 1.01f); // Slight stereo detuning
        diffusionL[i].setFeedback(0.75f);
        diffusionR[i].setFeedback(0.75f);
    }

    loadPresetReflection(1); // Load default preset
}

void Freeverb3Reverb::EarlyReflections::reset()
{
    std::fill(delayLineL.begin(), delayLineL.end(), 0.0f);
    std::fill(delayLineR.begin(), delayLineR.end(), 0.0f);
    writePos = 0;

    for (auto& ap : diffusionL) ap.clear();
    for (auto& ap : diffusionR) ap.clear();
}

void Freeverb3Reverb::EarlyReflections::setRoomSize(float meters)
{
    roomSizeFactor = meters / 30.0f; // Normalize to 30m reference
}

void Freeverb3Reverb::EarlyReflections::setDiffusion(float diff)
{
    diffusionAmount = juce::jlimit(0.0f, 1.0f, diff);
    float feedback = 0.25f + diffusionAmount * 0.5f;

    for (auto& ap : diffusionL) ap.setFeedback(feedback);
    for (auto& ap : diffusionR) ap.setFeedback(feedback);
}

void Freeverb3Reverb::EarlyReflections::setWidth(float w)
{
    stereoWidth = juce::jlimit(0.0f, 2.0f, w);
}

void Freeverb3Reverb::EarlyReflections::setLRDelay(float delay)
{
    lrDelay = juce::jlimit(0.0f, 1.0f, delay);
}

void Freeverb3Reverb::EarlyReflections::setLRCrossApFreq(float freq, float stages)
{
    // This would set crossover allpass frequency in full Freeverb3
    // Simplified for this implementation
}

void Freeverb3Reverb::EarlyReflections::setDiffusionApFreq(float freq, float stages)
{
    // This would set diffusion allpass frequency in full Freeverb3
    // Simplified for this implementation
}

void Freeverb3Reverb::EarlyReflections::loadPresetReflection(int preset)
{
    // Preset 1: Concert Hall (matching Dragonfly's FV3_EARLYREF_PRESET_1)
    initializePreset1Taps();
}

void Freeverb3Reverb::EarlyReflections::initializePreset1Taps()
{
    // Early reflection pattern based on concert hall measurements
    // Times in ms, gains normalized
    float tapData[][3] = {
        // {delay_ms, gain_L, gain_R}
        {5.0f, 0.841f, 0.504f},
        {7.0f, 0.504f, 0.841f},
        {11.0f, 0.491f, 0.379f},
        {13.0f, 0.379f, 0.491f},
        {17.0f, 0.380f, 0.346f},
        {19.0f, 0.346f, 0.380f},
        {23.0f, 0.289f, 0.272f},
        {27.0f, 0.272f, 0.289f},
        {29.0f, 0.192f, 0.208f},
        {31.0f, 0.208f, 0.192f},
        {37.0f, 0.193f, 0.217f},
        {39.0f, 0.217f, 0.193f},
        {41.0f, 0.181f, 0.180f},
        {43.0f, 0.180f, 0.181f},
        {47.0f, 0.176f, 0.142f},
        {49.0f, 0.142f, 0.176f},
        {53.0f, 0.151f, 0.167f},
        {57.0f, 0.167f, 0.151f},
        {59.0f, 0.134f, 0.134f},
        {61.0f, 0.134f, 0.134f},
        {67.0f, 0.127f, 0.120f},
        {71.0f, 0.120f, 0.127f},
        {73.0f, 0.117f, 0.117f},
        {79.0f, 0.118f, 0.118f}
    };

    for (int i = 0; i < numTaps; ++i)
    {
        taps[i].delayMs = tapData[i][0];
        taps[i].gainL = tapData[i][1];
        taps[i].gainR = tapData[i][2];
    }
}

void Freeverb3Reverb::EarlyReflections::process(const float* inputL, const float* inputR,
                                                 float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Write to delay lines
        delayLineL[writePos] = inputL[i];
        delayLineR[writePos] = inputR[i];

        // Sum early reflections
        float sumL = 0.0f;
        float sumR = 0.0f;

        for (const auto& tap : taps)
        {
            int delaySamples = static_cast<int>(tap.delayMs * roomSizeFactor * sampleRate * 0.001);
            int readPos = writePos - delaySamples;
            while (readPos < 0) readPos += static_cast<int>(delayLineL.size());

            float tapL = delayLineL[readPos];
            float tapR = delayLineR[readPos];

            // Apply tap gains with stereo width
            float midGain = (tap.gainL + tap.gainR) * 0.5f;
            float sideGain = (tap.gainL - tap.gainR) * 0.5f * stereoWidth;

            sumL += tapL * (midGain + sideGain);
            sumR += tapR * (midGain - sideGain);
        }

        // Apply diffusion if enabled
        if (diffusionAmount > 0.0f)
        {
            for (auto& ap : diffusionL)
            {
                sumL = ap.process(sumL);
            }
            for (auto& ap : diffusionR)
            {
                sumR = ap.process(sumR);
            }
        }

        outputL[i] = sumL * 0.5f;
        outputR[i] = sumR * 0.5f;

        writePos = (writePos + 1) % static_cast<int>(delayLineL.size());
    }
}

//==============================================================================
// Zrev2 (Hall) implementation (based on fv3::zrev2)
//==============================================================================

void Freeverb3Reverb::Zrev2::ModulatedDelayLine::setMaxSize(int samples)
{
    maxSize = samples;
    buffer.resize(samples + 1);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::Zrev2::ModulatedDelayLine::setDelay(float samples)
{
    delayTime = juce::jlimit(0.0f, static_cast<float>(maxSize - 1), samples);
}

float Freeverb3Reverb::Zrev2::ModulatedDelayLine::read(float modulation) const
{
    if (buffer.empty()) return 0.0f;

    float totalDelay = delayTime + modulation;
    totalDelay = juce::jlimit(0.0f, static_cast<float>(maxSize - 1), totalDelay);

    int delaySamples = static_cast<int>(totalDelay);
    float frac = totalDelay - delaySamples;

    int readPos1 = writePos - delaySamples;
    while (readPos1 < 0) readPos1 += static_cast<int>(buffer.size());

    int readPos2 = readPos1 - 1;
    while (readPos2 < 0) readPos2 += static_cast<int>(buffer.size());

    // Linear interpolation
    return buffer[readPos1] * (1.0f - frac) + buffer[readPos2] * frac;
}

void Freeverb3Reverb::Zrev2::ModulatedDelayLine::write(float sample)
{
    if (buffer.empty()) return;

    buffer[writePos] = sample;
    writePos = (writePos + 1) % static_cast<int>(buffer.size());
}

void Freeverb3Reverb::Zrev2::ModulatedDelayLine::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::Zrev2::AllpassFilter::setSize(int samples)
{
    buffer.resize(samples);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::Zrev2::AllpassFilter::setFeedback(float g)
{
    feedback = juce::jlimit(-0.99f, 0.99f, g);
}

float Freeverb3Reverb::Zrev2::AllpassFilter::process(float input)
{
    if (buffer.empty()) return input;

    float delayed = buffer[writePos];
    float output = -input + delayed;
    buffer[writePos] = input + delayed * feedback;
    writePos = (writePos + 1) % static_cast<int>(buffer.size());

    return output;
}

void Freeverb3Reverb::Zrev2::AllpassFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::Zrev2::prepare(double sr)
{
    sampleRate = sr;

    // Initialize FDN delay lines with prime number delays
    float baseTimes[] = {
        31.0f, 37.0f, 41.0f, 43.0f, 47.0f, 53.0f, 59.0f, 61.0f,
        67.0f, 71.0f, 73.0f, 79.0f, 83.0f, 89.0f, 97.0f, 101.0f
    };

    for (int i = 0; i < numDelays; ++i)
    {
        delayTimes[i] = baseTimes[i];
        int maxSamples = static_cast<int>(baseTimes[i] * 2.0f * sampleRate * 0.001f);
        delayLines[i].setMaxSize(maxSamples);
        delayLines[i].setDelay(baseTimes[i] * sampleRate * 0.001f);
    }

    // Initialize input diffusion (matching Dragonfly Hall)
    float inputDiffTimes[] = {8.9f, 7.2f, 4.8f, 3.7f};
    for (int i = 0; i < 4; ++i)
    {
        int size = static_cast<int>(inputDiffTimes[i] * sampleRate * 0.001f);
        inputDiffusionL[i].setSize(size);
        inputDiffusionR[i].setSize(size * 1.01f);
        inputDiffusionL[i].setFeedback(0.75f);
        inputDiffusionR[i].setFeedback(0.75f);
    }

    // Initialize output diffusion
    float outputDiffTimes[] = {11.8f, 5.9f};
    for (int i = 0; i < 2; ++i)
    {
        int size = static_cast<int>(outputDiffTimes[i] * sampleRate * 0.001f);
        outputDiffusionL[i].setSize(size);
        outputDiffusionR[i].setSize(size * 1.01f);
        outputDiffusionL[i].setFeedback(0.7f);
        outputDiffusionR[i].setFeedback(0.7f);
    }

    generateHadamardMatrix();
    updateDelayTimes();
    reset();
}

void Freeverb3Reverb::Zrev2::reset()
{
    for (auto& delay : delayLines) delay.clear();
    for (auto& ap : inputDiffusionL) ap.clear();
    for (auto& ap : inputDiffusionR) ap.clear();
    for (auto& ap : outputDiffusionL) ap.clear();
    for (auto& ap : outputDiffusionR) ap.clear();

    std::fill(dampingStates.begin(), dampingStates.end(), 0.0f);
    lfoPhase = 0.0f;
}

void Freeverb3Reverb::Zrev2::generateHadamardMatrix()
{
    // Generate Hadamard matrix for FDN feedback
    float scale = 1.0f / std::sqrt(static_cast<float>(numDelays));

    for (int i = 0; i < numDelays; ++i)
    {
        for (int j = 0; j < numDelays; ++j)
        {
            // Simple Hadamard-like pattern
            int sign = ((i & j) == 0) ? 1 : -1;
            feedbackMatrix[i][j] = scale * sign;
        }
    }
}

void Freeverb3Reverb::Zrev2::updateDelayTimes()
{
    for (int i = 0; i < numDelays; ++i)
    {
        float scaledTime = delayTimes[i] * roomSizeFactor;
        delayLines[i].setDelay(scaledTime * sampleRate * 0.001f);
    }
}

void Freeverb3Reverb::Zrev2::setrt60(float seconds)
{
    rt60 = juce::jlimit(0.1f, 30.0f, seconds);

    // Calculate feedback gains for target RT60
    for (int i = 0; i < numDelays; ++i)
    {
        float delayMs = delayTimes[i] * roomSizeFactor;
        float samples = delayMs * sampleRate * 0.001f;
        feedbackGains[i] = std::pow(0.001f, samples / (rt60 * sampleRate));
        feedbackGains[i] = juce::jlimit(0.0f, 0.99f, feedbackGains[i]);
    }
}

void Freeverb3Reverb::Zrev2::setidiffusion1(float diff)
{
    float feedback = 0.25f + diff * 0.5f;
    for (auto& ap : inputDiffusionL) ap.setFeedback(feedback);
    for (auto& ap : inputDiffusionR) ap.setFeedback(feedback);
}

void Freeverb3Reverb::Zrev2::setodiffusion1(float diff)
{
    float feedback = 0.25f + diff * 0.45f;
    for (auto& ap : outputDiffusionL) ap.setFeedback(feedback);
    for (auto& ap : outputDiffusionR) ap.setFeedback(feedback);
}

void Freeverb3Reverb::Zrev2::setwidth(float w)
{
    stereoWidth = juce::jlimit(0.0f, 2.0f, w);
}

void Freeverb3Reverb::Zrev2::setRSFactor(float factor)
{
    roomSizeFactor = juce::jlimit(0.5f, 2.0f, factor);
    updateDelayTimes();
}

void Freeverb3Reverb::Zrev2::setDamping(float damp)
{
    dampingCoeff = juce::jlimit(0.0f, 1.0f, damp);
}

void Freeverb3Reverb::Zrev2::setModulation(float depth, float speed)
{
    modDepth = depth * 0.5f; // Scale modulation depth
    modRate = speed;
}

void Freeverb3Reverb::Zrev2::process(const float* inputL, const float* inputR,
                                      float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Mix to mono and apply input diffusion
        float input = (inputL[i] + inputR[i]) * 0.015f;

        for (auto& ap : inputDiffusionL)
        {
            input = ap.process(input);
        }

        // FDN processing
        std::array<float, numDelays> delayOutputs;

        // Read from all delay lines
        for (int d = 0; d < numDelays; ++d)
        {
            // Apply modulation
            float mod = 0.0f;
            if (modDepth > 0.0f)
            {
                mod = std::sin(lfoPhase + d * 0.43f) * modDepth;
            }
            delayOutputs[d] = delayLines[d].read(mod);
        }

        // Apply feedback matrix and write back
        for (int d = 0; d < numDelays; ++d)
        {
            float sum = input;

            // Hadamard matrix multiplication
            for (int j = 0; j < numDelays; ++j)
            {
                sum += delayOutputs[j] * feedbackMatrix[d][j] * feedbackGains[d];
            }

            // Apply damping
            dampingStates[d] = sum * (1.0f - dampingCoeff) + dampingStates[d] * dampingCoeff;

            delayLines[d].write(dampingStates[d]);
        }

        // Sum outputs with decorrelation
        float sumL = 0.0f;
        float sumR = 0.0f;

        for (int d = 0; d < numDelays; ++d)
        {
            float gain = 1.0f / numDelays;
            if (d % 2 == 0)
            {
                sumL += delayOutputs[d] * gain * (1.0f + stereoWidth * 0.5f);
                sumR += delayOutputs[d] * gain * (1.0f - stereoWidth * 0.5f);
            }
            else
            {
                sumL += delayOutputs[d] * gain * (1.0f - stereoWidth * 0.5f);
                sumR += delayOutputs[d] * gain * (1.0f + stereoWidth * 0.5f);
            }
        }

        // Apply output diffusion
        for (auto& ap : outputDiffusionL)
        {
            sumL = ap.process(sumL);
        }
        for (auto& ap : outputDiffusionR)
        {
            sumR = ap.process(sumR);
        }

        outputL[i] = sumL;
        outputR[i] = sumR;

        // Update LFO
        lfoPhase += 2.0f * M_PI * modRate / sampleRate;
        while (lfoPhase >= 2.0f * M_PI) lfoPhase -= 2.0f * M_PI;
    }
}

//==============================================================================
// Progenitor2 (Room) implementation (based on fv3::progenitor2)
//==============================================================================

void Freeverb3Reverb::Progenitor2::CombFilter::setSize(int samples)
{
    buffer.resize(samples);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::Progenitor2::CombFilter::setFeedback(float g)
{
    feedback = juce::jlimit(0.0f, 0.99f, g);
}

void Freeverb3Reverb::Progenitor2::CombFilter::setDamping(float d)
{
    damping = juce::jlimit(0.0f, 1.0f, d);
}

float Freeverb3Reverb::Progenitor2::CombFilter::process(float input)
{
    if (buffer.empty()) return 0.0f;

    float output = buffer[writePos];

    // Apply damping (simple lowpass)
    filterStore = output * (1.0f - damping) + filterStore * damping;

    buffer[writePos] = input + filterStore * feedback;
    writePos = (writePos + 1) % static_cast<int>(buffer.size());

    return output;
}

void Freeverb3Reverb::Progenitor2::CombFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    filterStore = 0.0f;
    writePos = 0;
}

void Freeverb3Reverb::Progenitor2::AllpassFilter::setSize(int samples)
{
    buffer.resize(samples);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::Progenitor2::AllpassFilter::setFeedback(float g)
{
    feedback = juce::jlimit(-0.99f, 0.99f, g);
}

float Freeverb3Reverb::Progenitor2::AllpassFilter::process(float input)
{
    if (buffer.empty()) return input;

    float delayed = buffer[writePos];
    float output = -input + delayed;
    buffer[writePos] = input + delayed * feedback;
    writePos = (writePos + 1) % static_cast<int>(buffer.size());

    return output;
}

void Freeverb3Reverb::Progenitor2::AllpassFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::Progenitor2::prepare(double sr)
{
    sampleRate = sr;

    // Initialize input diffusion
    float inputDiffTimes[] = {4.31f, 3.73f};
    for (int i = 0; i < 2; ++i)
    {
        int size = static_cast<int>(inputDiffTimes[i] * sampleRate * 0.001f);
        inputDiffusion[i].setSize(size);
        inputDiffusion[i].setFeedback(0.75f);
    }

    // Initialize comb filters
    for (int i = 0; i < numCombs; ++i)
    {
        int sizeL = static_cast<int>(combTuningsMs[i] * sampleRate * 0.001f);
        int sizeR = static_cast<int>(combTuningsMs[i] * sampleRate * 0.001f * 1.0001f);

        combsL[i].setSize(sizeL);
        combsR[i].setSize(sizeR);
    }

    // Initialize allpass filters
    for (int i = 0; i < numAllpasses; ++i)
    {
        int sizeL = static_cast<int>(allpassTuningsMs[i] * sampleRate * 0.001f);
        int sizeR = static_cast<int>(allpassTuningsMs[i] * sampleRate * 0.001f * 1.0001f);

        allpassesL[i].setSize(sizeL);
        allpassesR[i].setSize(sizeR);
        allpassesL[i].setFeedback(0.5f);
        allpassesR[i].setFeedback(0.5f);
    }

    updateParameters();
    reset();
}

void Freeverb3Reverb::Progenitor2::reset()
{
    for (auto& ap : inputDiffusion) ap.clear();
    for (auto& comb : combsL) comb.clear();
    for (auto& comb : combsR) comb.clear();
    for (auto& ap : allpassesL) ap.clear();
    for (auto& ap : allpassesR) ap.clear();
}

void Freeverb3Reverb::Progenitor2::setrt60(float seconds)
{
    rt60 = juce::jlimit(0.1f, 30.0f, seconds);
    updateParameters();
}

void Freeverb3Reverb::Progenitor2::setidiffusion1(float diff)
{
    float feedback = 0.25f + diff * 0.5f;
    for (auto& ap : inputDiffusion)
    {
        ap.setFeedback(feedback);
    }
}

void Freeverb3Reverb::Progenitor2::setodiffusion1(float diff)
{
    float feedback = 0.4f + diff * 0.2f;
    for (auto& ap : allpassesL) ap.setFeedback(feedback);
    for (auto& ap : allpassesR) ap.setFeedback(feedback);
}

void Freeverb3Reverb::Progenitor2::setwidth(float w)
{
    stereoWidth = juce::jlimit(0.0f, 2.0f, w);
}

void Freeverb3Reverb::Progenitor2::setRSFactor(float factor)
{
    roomSizeFactor = juce::jlimit(0.5f, 2.0f, factor);

    // Update comb filter sizes
    for (int i = 0; i < numCombs; ++i)
    {
        int sizeL = static_cast<int>(combTuningsMs[i] * roomSizeFactor * sampleRate * 0.001f);
        int sizeR = static_cast<int>(combTuningsMs[i] * roomSizeFactor * sampleRate * 0.001f * 1.0001f);

        combsL[i].setSize(sizeL);
        combsR[i].setSize(sizeR);
    }

    updateParameters();
}

void Freeverb3Reverb::Progenitor2::setDamping(float damp)
{
    damping = juce::jlimit(0.0f, 1.0f, damp);

    for (auto& comb : combsL) comb.setDamping(damping);
    for (auto& comb : combsR) comb.setDamping(damping);
}

void Freeverb3Reverb::Progenitor2::updateParameters()
{
    // Calculate feedback for target RT60
    for (int i = 0; i < numCombs; ++i)
    {
        float delayMs = combTuningsMs[i] * roomSizeFactor;
        float samples = delayMs * sampleRate * 0.001f;
        float feedback = std::pow(0.001f, samples / (rt60 * sampleRate));
        feedback = juce::jlimit(0.0f, 0.99f, feedback);

        combsL[i].setFeedback(feedback);
        combsR[i].setFeedback(feedback);
        combsL[i].setDamping(damping);
        combsR[i].setDamping(damping);
    }
}

void Freeverb3Reverb::Progenitor2::process(const float* inputL, const float* inputR,
                                            float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Mix to mono and apply input diffusion
        float input = (inputL[i] + inputR[i]) * 0.015f;

        for (auto& ap : inputDiffusion)
        {
            input = ap.process(input);
        }

        // Process through parallel comb filters
        float combSumL = 0.0f;
        float combSumR = 0.0f;

        for (int c = 0; c < numCombs; ++c)
        {
            combSumL += combsL[c].process(input);
            combSumR += combsR[c].process(input);
        }

        // Scale comb output
        combSumL *= 0.25f;
        combSumR *= 0.25f;

        // Process through series allpass filters
        float outL = combSumL;
        float outR = combSumR;

        for (int a = 0; a < numAllpasses; ++a)
        {
            outL = allpassesL[a].process(outL);
            outR = allpassesR[a].process(outR);
        }

        // Apply stereo width
        float mid = (outL + outR) * 0.5f;
        float side = (outL - outR) * 0.5f * stereoWidth;

        outputL[i] = mid + side;
        outputR[i] = mid - side;
    }
}

//==============================================================================
// PlateReverb implementation (Dattorro-style plate)
//==============================================================================

void Freeverb3Reverb::PlateReverb::DelayLine::setSize(int samples)
{
    buffer.resize(samples);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float Freeverb3Reverb::PlateReverb::DelayLine::read(int delaySamples) const
{
    if (buffer.empty()) return 0.0f;

    int readPos = writePos - delaySamples;
    while (readPos < 0) readPos += static_cast<int>(buffer.size());

    return buffer[readPos];
}

void Freeverb3Reverb::PlateReverb::DelayLine::write(float sample)
{
    if (buffer.empty()) return;

    buffer[writePos] = sample;
    writePos = (writePos + 1) % static_cast<int>(buffer.size());
}

void Freeverb3Reverb::PlateReverb::DelayLine::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::PlateReverb::AllpassFilter::setSize(int samples)
{
    buffer.resize(samples);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::PlateReverb::AllpassFilter::setFeedback(float g)
{
    feedback = juce::jlimit(-0.99f, 0.99f, g);
}

float Freeverb3Reverb::PlateReverb::AllpassFilter::process(float input)
{
    if (buffer.empty()) return input;

    float delayed = buffer[writePos];
    float output = -input + delayed;
    buffer[writePos] = input + delayed * feedback;
    writePos = (writePos + 1) % static_cast<int>(buffer.size());

    return output;
}

void Freeverb3Reverb::PlateReverb::AllpassFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void Freeverb3Reverb::PlateReverb::prepare(double sr)
{
    sampleRate = sr;
    initializePlate();
}

void Freeverb3Reverb::PlateReverb::initializePlate()
{
    // Initialize input diffusion network (Dattorro plate structure)
    float inputDiffTimes[] = {4.771f, 3.595f, 2.556f, 1.73f};
    for (int i = 0; i < 4; ++i)
    {
        int size = static_cast<int>(inputDiffTimes[i] * sampleRate * 0.001f);
        inputDiffusionL[i].setSize(size);
        inputDiffusionR[i].setSize(size * 1.0001f);
        inputDiffusionL[i].setFeedback(0.75f);
        inputDiffusionR[i].setFeedback(0.75f);
    }

    // Initialize tank structure (lattice)
    tankL.allpass1.setSize(static_cast<int>(22.58f * sampleRate * 0.001f));
    tankL.delay1.setSize(static_cast<int>(30.51f * sampleRate * 0.001f));
    tankL.allpass2.setSize(static_cast<int>(8.97f * sampleRate * 0.001f));
    tankL.delay2.setSize(static_cast<int>(60.48f * sampleRate * 0.001f));

    tankR.allpass1.setSize(static_cast<int>(35.78f * sampleRate * 0.001f));
    tankR.delay1.setSize(static_cast<int>(39.54f * sampleRate * 0.001f));
    tankR.allpass2.setSize(static_cast<int>(11.96f * sampleRate * 0.001f));
    tankR.delay2.setSize(static_cast<int>(69.72f * sampleRate * 0.001f));

    tankL.allpass1.setFeedback(-0.7f);
    tankL.allpass2.setFeedback(0.5f);
    tankR.allpass1.setFeedback(-0.7f);
    tankR.allpass2.setFeedback(0.5f);

    // Initialize output tap positions (multiple taps for dense plate sound)
    outputTapsL = {266, 2974, 1913, 1996, 1990, 187, 1066};
    outputTapsR = {353, 3627, 1228, 2673, 2111, 335, 121};

    reset();
}

void Freeverb3Reverb::PlateReverb::reset()
{
    for (auto& ap : inputDiffusionL) ap.clear();
    for (auto& ap : inputDiffusionR) ap.clear();

    tankL.allpass1.clear();
    tankL.delay1.clear();
    tankL.allpass2.clear();
    tankL.delay2.clear();
    tankL.lpState = 0.0f;

    tankR.allpass1.clear();
    tankR.delay1.clear();
    tankR.allpass2.clear();
    tankR.delay2.clear();
    tankR.lpState = 0.0f;
}

void Freeverb3Reverb::PlateReverb::setDecay(float seconds)
{
    decay = std::pow(0.001f, 1.0f / (seconds * sampleRate / 1000.0f));
    decay = juce::jlimit(0.0f, 0.999f, decay);
}

void Freeverb3Reverb::PlateReverb::setDamping(float damp)
{
    damping = juce::jlimit(0.0f, 1.0f, damp);
}

void Freeverb3Reverb::PlateReverb::setBandwidth(float bw)
{
    bandwidth = juce::jlimit(0.0f, 1.0f, bw);
}

void Freeverb3Reverb::PlateReverb::setDiffusion(float diff)
{
    diffusion = juce::jlimit(0.0f, 1.0f, diff);

    float feedback = 0.5f + diffusion * 0.25f;
    for (auto& ap : inputDiffusionL) ap.setFeedback(feedback);
    for (auto& ap : inputDiffusionR) ap.setFeedback(feedback);

    tankL.allpass1.setFeedback(-diffusion * 0.7f);
    tankL.allpass2.setFeedback(diffusion * 0.5f);
    tankR.allpass1.setFeedback(-diffusion * 0.7f);
    tankR.allpass2.setFeedback(diffusion * 0.5f);
}

void Freeverb3Reverb::PlateReverb::process(const float* inputL, const float* inputR,
                                            float* outputL, float* outputR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float inL = inputL[i] * 0.015f * bandwidth;
        float inR = inputR[i] * 0.015f * bandwidth;

        // Apply input diffusion
        for (int d = 0; d < 4; ++d)
        {
            inL = inputDiffusionL[d].process(inL);
            inR = inputDiffusionR[d].process(inR);
        }

        // Left tank processing
        float tankInL = inL + tankR.delay2.read(static_cast<int>(69.72f * sampleRate * 0.001f)) * decay;
        float ap1L = tankL.allpass1.process(tankInL);
        float del1L = tankL.delay1.read(static_cast<int>(30.51f * sampleRate * 0.001f));

        // Apply damping
        tankL.lpState = del1L * (1.0f - damping) + tankL.lpState * damping;
        tankL.delay1.write(ap1L + tankL.lpState * decay);

        float ap2L = tankL.allpass2.process(tankL.lpState);
        tankL.delay2.write(ap2L);

        // Right tank processing
        float tankInR = inR + tankL.delay2.read(static_cast<int>(60.48f * sampleRate * 0.001f)) * decay;
        float ap1R = tankR.allpass1.process(tankInR);
        float del1R = tankR.delay1.read(static_cast<int>(39.54f * sampleRate * 0.001f));

        // Apply damping
        tankR.lpState = del1R * (1.0f - damping) + tankR.lpState * damping;
        tankR.delay1.write(ap1R + tankR.lpState * decay);

        float ap2R = tankR.allpass2.process(tankR.lpState);
        tankR.delay2.write(ap2R);

        // Sum multiple output taps for dense plate sound
        float outL = 0.0f;
        float outR = 0.0f;

        for (int tap = 0; tap < 7; ++tap)
        {
            outL += tankL.delay2.read(outputTapsL[tap]) * 0.14f;
            outR += tankR.delay2.read(outputTapsR[tap]) * 0.14f;
        }

        outputL[i] = outL;
        outputR[i] = outR;
    }
}

//==============================================================================
// Main Freeverb3Reverb implementation
//==============================================================================

Freeverb3Reverb::Freeverb3Reverb()
{
}

void Freeverb3Reverb::prepare(double sr, int samplesPerBlock)
{
    sampleRate = sr;

    // Prepare all reverb algorithms
    earlyReflections.prepare(sampleRate);
    hallReverb.prepare(sampleRate);
    roomReverb.prepare(sampleRate);
    plateReverb.prepare(sampleRate);

    // Set up pre-delay buffer
    int maxPreDelay = static_cast<int>(sampleRate * 0.1f); // 100ms max
    preDelayBufferL.resize(maxPreDelay);
    preDelayBufferR.resize(maxPreDelay);
    std::fill(preDelayBufferL.begin(), preDelayBufferL.end(), 0.0f);
    std::fill(preDelayBufferR.begin(), preDelayBufferR.end(), 0.0f);

    // Set up filters
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;

    inputHighpass.prepare(spec);
    inputLowpass.prepare(spec);
    outputHighpass.prepare(spec);
    outputLowpass.prepare(spec);
    lowCrossover.prepare(spec);
    highCrossover.prepare(spec);

    inputHighpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    inputLowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    outputHighpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    outputLowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    // Set default filter frequencies
    setLowCut(20.0f);
    setHighCut(20000.0f);
    setLowCrossover(200.0f);
    setHighCrossover(2000.0f);

    // Allocate temporary buffers
    tempBufferL.resize(samplesPerBlock);
    tempBufferR.resize(samplesPerBlock);
    earlyBufferL.resize(samplesPerBlock);
    earlyBufferR.resize(samplesPerBlock);
    lateBufferL.resize(samplesPerBlock);
    lateBufferR.resize(samplesPerBlock);

    // Set default parameters matching Dragonfly
    setSize(30.0f);
    setWidth(1.0f);
    setDiffuse(0.8f);
    setDecay(2.0f);

    reset();
}

void Freeverb3Reverb::reset()
{
    earlyReflections.reset();
    hallReverb.reset();
    roomReverb.reset();
    plateReverb.reset();

    std::fill(preDelayBufferL.begin(), preDelayBufferL.end(), 0.0f);
    std::fill(preDelayBufferR.begin(), preDelayBufferR.end(), 0.0f);
    preDelayWritePos = 0;

    inputHighpass.reset();
    inputLowpass.reset();
    outputHighpass.reset();
    outputLowpass.reset();
    lowCrossover.reset();
    highCrossover.reset();
}

void Freeverb3Reverb::setSize(float meters)
{
    roomSize = juce::jlimit(10.0f, 60.0f, meters);
    float factor = roomSize / 30.0f; // Normalize to 30m reference

    earlyReflections.setRoomSize(roomSize);
    hallReverb.setRSFactor(factor);
    roomReverb.setRSFactor(factor);
}

void Freeverb3Reverb::setWidth(float percent)
{
    width = juce::jlimit(0.5f, 1.5f, percent);

    earlyReflections.setWidth(width);
    hallReverb.setwidth(width);
    roomReverb.setwidth(width);
}

void Freeverb3Reverb::setPreDelay(float ms)
{
    preDelayMs = juce::jlimit(0.0f, 100.0f, ms);
    preDelaySamples = static_cast<int>(preDelayMs * sampleRate * 0.001f);
}

void Freeverb3Reverb::setDiffuse(float percent)
{
    diffusion = juce::jlimit(0.0f, 1.0f, percent);

    earlyReflections.setDiffusion(diffusion);
    hallReverb.setidiffusion1(diffusion);
    hallReverb.setodiffusion1(diffusion * 0.7f);
    roomReverb.setidiffusion1(diffusion);
    roomReverb.setodiffusion1(diffusion * 0.7f);
    plateReverb.setDiffusion(diffusion);
}

void Freeverb3Reverb::setDecay(float seconds)
{
    decay = juce::jlimit(0.1f, 10.0f, seconds);

    hallReverb.setrt60(decay);
    roomReverb.setrt60(decay);
    plateReverb.setDecay(decay);
}

void Freeverb3Reverb::setLowCut(float freq)
{
    inputHighpass.setCutoffFrequency(freq);
    outputHighpass.setCutoffFrequency(freq * 0.8f);
}

void Freeverb3Reverb::setHighCut(float freq)
{
    inputLowpass.setCutoffFrequency(freq);
    outputLowpass.setCutoffFrequency(freq * 1.2f);
}

void Freeverb3Reverb::setLowCrossover(float freq)
{
    lowCrossover.setCutoffFrequency(freq);
}

void Freeverb3Reverb::setHighCrossover(float freq)
{
    highCrossover.setCutoffFrequency(freq);
}

void Freeverb3Reverb::setLowMult(float mult)
{
    // This would affect low frequency reverb time in full implementation
}

void Freeverb3Reverb::setHighMult(float mult)
{
    // This would affect high frequency reverb time in full implementation
    float damping = 1.0f - mult;
    hallReverb.setDamping(damping);
    roomReverb.setDamping(damping);
    plateReverb.setDamping(damping);
}

void Freeverb3Reverb::setModAmount(float amount)
{
    modAmount = juce::jlimit(0.0f, 1.0f, amount);
    hallReverb.setModulation(modAmount, modSpeed);
}

void Freeverb3Reverb::setModSpeed(float speed)
{
    modSpeed = juce::jlimit(0.1f, 5.0f, speed);
    hallReverb.setModulation(modAmount, modSpeed);
}

void Freeverb3Reverb::processBlock(juce::AudioBuffer<float>& buffer)
{
    switch (currentType)
    {
        case ReverbType::Room:
            processRoom(buffer);
            break;
        case ReverbType::Hall:
            processHall(buffer);
            break;
        case ReverbType::Plate:
            processPlate(buffer);
            break;
        case ReverbType::EarlyReflections:
            processEarlyOnly(buffer);
            break;
    }
}

void Freeverb3Reverb::processRoom(juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inputL;
    float* outputL = buffer.getWritePointer(0);
    float* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        if (preDelaySamples > 0)
        {
            tempBufferL[i] = preDelayBufferL[preDelayWritePos];
            tempBufferR[i] = preDelayBufferR[preDelayWritePos];

            preDelayBufferL[preDelayWritePos] = inputL[i];
            preDelayBufferR[preDelayWritePos] = inputR[i];

            preDelayWritePos = (preDelayWritePos + 1) % static_cast<int>(preDelayBufferL.size());
        }
        else
        {
            tempBufferL[i] = inputL[i];
            tempBufferR[i] = inputR[i];
        }
    }

    // Process early reflections
    earlyReflections.process(tempBufferL.data(), tempBufferR.data(),
                            earlyBufferL.data(), earlyBufferR.data(), numSamples);

    // Mix early reflections with input for late reverb (matching Dragonfly)
    for (int i = 0; i < numSamples; ++i)
    {
        tempBufferL[i] += earlyBufferL[i] * earlyLateSend;
        tempBufferR[i] += earlyBufferR[i] * earlyLateSend;
    }

    // Process late reverb
    roomReverb.process(tempBufferL.data(), tempBufferR.data(),
                      lateBufferL.data(), lateBufferR.data(), numSamples);

    // Mix dry, early, and late (matching Dragonfly mixing)
    for (int i = 0; i < numSamples; ++i)
    {
        outputL[i] = inputL[i] * dryLevel +
                     earlyBufferL[i] * earlyLevel +
                     lateBufferL[i] * lateLevel;
        outputR[i] = inputR[i] * dryLevel +
                     earlyBufferR[i] * earlyLevel +
                     lateBufferR[i] * lateLevel;
    }
}

void Freeverb3Reverb::processHall(juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inputL;
    float* outputL = buffer.getWritePointer(0);
    float* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        if (preDelaySamples > 0)
        {
            tempBufferL[i] = preDelayBufferL[preDelayWritePos];
            tempBufferR[i] = preDelayBufferR[preDelayWritePos];

            preDelayBufferL[preDelayWritePos] = inputL[i];
            preDelayBufferR[preDelayWritePos] = inputR[i];

            preDelayWritePos = (preDelayWritePos + 1) % static_cast<int>(preDelayBufferL.size());
        }
        else
        {
            tempBufferL[i] = inputL[i];
            tempBufferR[i] = inputR[i];
        }
    }

    // Process early reflections
    earlyReflections.process(tempBufferL.data(), tempBufferR.data(),
                            earlyBufferL.data(), earlyBufferR.data(), numSamples);

    // Mix early reflections with input for late reverb
    for (int i = 0; i < numSamples; ++i)
    {
        tempBufferL[i] += earlyBufferL[i] * earlyLateSend;
        tempBufferR[i] += earlyBufferR[i] * earlyLateSend;
    }

    // Process late reverb
    hallReverb.process(tempBufferL.data(), tempBufferR.data(),
                      lateBufferL.data(), lateBufferR.data(), numSamples);

    // Mix dry, early, and late
    for (int i = 0; i < numSamples; ++i)
    {
        outputL[i] = inputL[i] * dryLevel +
                     earlyBufferL[i] * earlyLevel +
                     lateBufferL[i] * lateLevel;
        outputR[i] = inputR[i] * dryLevel +
                     earlyBufferR[i] * earlyLevel +
                     lateBufferR[i] * lateLevel;
    }
}

void Freeverb3Reverb::processPlate(juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inputL;
    float* outputL = buffer.getWritePointer(0);
    float* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;

    // Apply pre-delay
    for (int i = 0; i < numSamples; ++i)
    {
        if (preDelaySamples > 0)
        {
            tempBufferL[i] = preDelayBufferL[preDelayWritePos];
            tempBufferR[i] = preDelayBufferR[preDelayWritePos];

            preDelayBufferL[preDelayWritePos] = inputL[i];
            preDelayBufferR[preDelayWritePos] = inputR[i];

            preDelayWritePos = (preDelayWritePos + 1) % static_cast<int>(preDelayBufferL.size());
        }
        else
        {
            tempBufferL[i] = inputL[i];
            tempBufferR[i] = inputR[i];
        }
    }

    // Process plate reverb (no separate early reflections)
    plateReverb.process(tempBufferL.data(), tempBufferR.data(),
                       lateBufferL.data(), lateBufferR.data(), numSamples);

    // Mix dry and wet
    for (int i = 0; i < numSamples; ++i)
    {
        outputL[i] = inputL[i] * dryLevel + lateBufferL[i] * lateLevel;
        outputR[i] = inputR[i] * dryLevel + lateBufferR[i] * lateLevel;
    }
}

void Freeverb3Reverb::processEarlyOnly(juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inputL;
    float* outputL = buffer.getWritePointer(0);
    float* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;

    // Process early reflections only
    earlyReflections.process(inputL, inputR,
                            earlyBufferL.data(), earlyBufferR.data(), numSamples);

    // Mix dry and early
    for (int i = 0; i < numSamples; ++i)
    {
        outputL[i] = inputL[i] * dryLevel + earlyBufferL[i] * earlyLevel;
        outputR[i] = inputR[i] * dryLevel + earlyBufferR[i] * earlyLevel;
    }
}
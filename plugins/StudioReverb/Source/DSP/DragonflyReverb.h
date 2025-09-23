/*
  ==============================================================================

    DragonflyReverb.h
    Proper implementation using actual Freeverb3 library from Dragonfly

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../freeverb/earlyref.hpp"
#include "../freeverb/zrev2.hpp"
#include "../freeverb/progenitor2.hpp"
#include "../freeverb/strev.hpp"
#include <memory>
#include <array>

class DragonflyReverb
{
public:
    enum class Algorithm {
        Room = 0,           // Progenitor2 algorithm (smaller, warmer spaces)
        Hall,               // Zrev2 algorithm (large concert halls)
        Plate,              // Strev algorithm (metallic plate reverb)
        EarlyReflections    // Early reflections only
    };

    DragonflyReverb();
    ~DragonflyReverb() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer);
    void reset();

    // Algorithm selection
    void setAlgorithm(Algorithm algo) { currentAlgorithm = algo; }
    Algorithm getAlgorithm() const { return currentAlgorithm; }

    // Main mix controls (matching Dragonfly exactly)
    void setDryLevel(float level) { dryLevel = level; }
    void setEarlyLevel(float level) { earlyLevel = level; }
    void setLateLevel(float level) { lateLevel = level; }
    void setEarlySend(float send) { earlySend = send; }  // How much early feeds into late

    // Core reverb parameters (matching Dragonfly's parameter scaling)
    void setSize(float meters);        // Room size in meters (10-60)
    void setWidth(float percent);      // Stereo width (0-100%)
    void setPreDelay(float ms);        // Pre-delay in milliseconds (0-100ms)
    void setDiffuse(float percent);    // Diffusion amount (0-100%)
    void setDecay(float seconds);      // RT60 decay time (0.1-10s)

    // Filter/Tone parameters
    void setLowCut(float freq);        // High-pass frequency (0-200Hz)
    void setHighCut(float freq);       // Low-pass frequency (1000-20000Hz)
    void setLowCrossover(float freq);  // Low frequency crossover
    void setHighCrossover(float freq); // High frequency crossover
    void setLowMult(float mult);       // Low frequency decay multiplier
    void setHighMult(float mult);      // High frequency decay multiplier

    // Modulation parameters (for Hall reverb)
    void setSpin(float amount);        // Modulation speed
    void setWander(float amount);      // Modulation depth

private:
    double sampleRate = 44100.0;
    int blockSize = 512;
    Algorithm currentAlgorithm = Algorithm::Hall;

    // Mix levels (0-1 range internally)
    float dryLevel = 0.7f;
    float earlyLevel = 0.3f;
    float lateLevel = 0.5f;
    float earlySend = 0.2f;

    // Parameters
    float size = 30.0f;
    float lastSetSize = -1.0f;  // Track when size actually changes
    float width = 100.0f;
    float preDelay = 0.0f;
    float diffusion = 50.0f;
    float decay = 2.0f;
    float lowCut = 0.0f;
    float highCut = 20000.0f;
    float lowXover = 200.0f;
    float highXover = 2000.0f;
    float lowMult = 1.0f;
    float highMult = 0.8f;
    float spin = 0.5f;
    float wander = 0.1f;

    // Freeverb3 processors (the actual Dragonfly algorithms)
    fv3::earlyref_f early;
    fv3::zrev2_f hall;
    fv3::progenitor2_f room;
    fv3::strev_f plate;

    // Processing buffers (matching Dragonfly's buffer management)
    static constexpr size_t BUFFER_SIZE = 256;
    float earlyOutBuffer[2][BUFFER_SIZE];
    float lateInBuffer[2][BUFFER_SIZE];
    float lateOutBuffer[2][BUFFER_SIZE];

    // Update reverb parameters based on current settings
    void updateEarlyReflections();
    void updateRoomReverb();
    void updateHallReverb();
    void updatePlateReverb();

    // Process different algorithms
    void processRoom(juce::AudioBuffer<float>& buffer);
    void processHall(juce::AudioBuffer<float>& buffer);
    void processPlate(juce::AudioBuffer<float>& buffer);
    void processEarlyOnly(juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DragonflyReverb)
};
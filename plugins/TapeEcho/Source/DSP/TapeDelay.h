#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

// Vintage tape echo processor with multi-head delay emulation
class TapeDelay
{
public:
    TapeDelay();
    ~TapeDelay() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void setDelayTime(int head, float delayMs);
    void setFeedback(float feedback);
    void setWowFlutter(float amount, float rate, int lfoShape);
    void setTapeAge(float age);
    void setHeadEnabled(int head, bool enabled);

    // New interface: pass in the feedback signal that has been externally filtered
    float processSample(float input, float externalFeedback, int channel);

    enum LFOShape
    {
        Sine = 0,
        Triangle,
        Square,
        SawUp,
        SawDown,
        Random
    };

private:
    static constexpr int NUM_HEADS = 3;
    static constexpr float MAX_DELAY_MS = 1000.0f;

    struct DelayHead
    {
        float delayMs = 200.0f;
        float delaySamples = 0.0f;
        bool enabled = false;
        float smoothedDelay = 0.0f;
    };

    std::array<DelayHead, NUM_HEADS> heads;
    std::vector<float> delayBufferL;
    std::vector<float> delayBufferR;

    int writePosition = 0;
    int bufferSize = 0;
    float sampleRate = 44100.0f;

    float feedback = 0.0f;
    float smoothedFeedback = 0.0f;
    float lastOutputL = 0.0f;
    float lastOutputR = 0.0f;

    // Wow and flutter
    float wowFlutterAmount = 0.0f;
    float wowFlutterRate = 1.0f;
    float lfoPhase = 0.0f;
    int lfoShape = Sine;
    float randomValue = 0.0f;
    float targetRandomValue = 0.0f;

    // Tape aging simulation
    float tapeAge = 0.0f;
    float noiseLevel = 0.0f;
    float highFreqDamping = 0.0f;
    juce::Random random;

    // Filters for tape coloration
    juce::IIRFilter lowpassL, lowpassR;
    juce::IIRFilter highpassL, highpassR;

    float getLFOValue();
    float getInterpolatedSample(const std::vector<float>& buffer, float delaySamples);
    void updateFilters();
};
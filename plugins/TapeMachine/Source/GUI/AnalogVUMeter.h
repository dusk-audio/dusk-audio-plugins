#pragma once

#include <JuceHeader.h>

//==============================================================================
// Professional Analog VU Meter Component with dual-needle stereo display
class AnalogVUMeter : public juce::Component, private juce::Timer
{
public:
    AnalogVUMeter();
    ~AnalogVUMeter() override;

    void setLevels(float leftLevel, float rightLevel);
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    // Stereo levels
    float currentLevelL = 0.0f;
    float currentLevelR = 0.0f;
    float targetLevelL = 0.0f;
    float targetLevelR = 0.0f;
    float needlePositionL = 0.13f;  // Rest at -20dB
    float needlePositionR = 0.13f;  // Rest at -20dB

    // Peak hold
    float peakLevelL = 0.0f;
    float peakLevelR = 0.0f;
    float peakHoldTimeL = 0.0f;
    float peakHoldTimeR = 0.0f;

    // VU Ballistics
    const float attackTime = 0.3f;  // 300ms VU standard
    const float releaseTime = 0.3f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVUMeter)
};
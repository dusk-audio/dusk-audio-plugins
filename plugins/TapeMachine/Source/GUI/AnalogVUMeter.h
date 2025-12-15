#pragma once

#include <JuceHeader.h>

//==============================================================================
// Professional Analog VU Meter Component
// Supports both mono (single meter) and stereo (dual meter) display modes
// Inspired by Studer A800 and Ampex ATR-102 VU meters
class AnalogVUMeter : public juce::Component, private juce::Timer
{
public:
    AnalogVUMeter();
    ~AnalogVUMeter() override;

    // Set levels for L/R (for stereo, call with left and right; for mono, both values are used)
    void setLevels(float leftLevel, float rightLevel);

    // Set stereo mode - when true, shows two VU meters; when false, shows single VU meter
    void setStereoMode(bool isStereo);
    bool isStereoMode() const { return stereoMode; }

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    // Helper to paint a single VU meter in the given bounds
    void paintSingleMeter(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                          float needlePos, float peakLevel, const juce::String& label);

    // Stereo/mono mode
    bool stereoMode = true;

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
#pragma once

#include <JuceHeader.h>

class VUMeter : public juce::Component, public juce::Timer
{
public:
    VUMeter();
    ~VUMeter() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setLevels(float leftLevel, float rightLevel);
    void timerCallback() override;

private:
    // Stereo levels
    float levelL = 0.0f;
    float levelR = 0.0f;
    float targetLevelL = 0.0f;
    float targetLevelR = 0.0f;
    float needleAngleL = -45.0f;
    float needleAngleR = -45.0f;
    float targetAngleL = -45.0f;
    float targetAngleR = -45.0f;

    // VU meter appearance
    bool isVintage = true;
    juce::Colour meterBackground{40, 35, 30};
    juce::Colour needleColourL{220, 80, 40};  // Red/orange for left
    juce::Colour needleColourR{200, 60, 30};  // Slightly darker red for right

    void drawVintageVUMeter(juce::Graphics& g);
    void drawModernVUMeter(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeter)
};
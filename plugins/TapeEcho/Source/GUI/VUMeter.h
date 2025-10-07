#pragma once

#include <JuceHeader.h>

class VUMeter : public juce::Component, public juce::Timer
{
public:
    VUMeter();
    ~VUMeter() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setLevel(float newLevel);
    void setVintageMode(bool vintageMode);
    void timerCallback() override;

private:
    float level = 0.0f;
    float targetLevel = 0.0f;
    float needleAngle = -45.0f;
    float targetAngle = -45.0f;

    // VU meter appearance
    bool isVintage = true;
    juce::Colour meterBackground{40, 35, 30};
    juce::Colour needleColour{200, 50, 30};

    void drawVintageVUMeter(juce::Graphics& g);
    void drawModernVUMeter(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeter)
};
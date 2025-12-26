/*
  ==============================================================================

    RE-201 Space Echo - VU Meter
    Authentic vintage VU meter with needle ballistics
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../ui/Colours.h"

class VUMeter : public juce::Component, public juce::Timer
{
public:
    VUMeter();
    ~VUMeter() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setLevel(float newLevel);
    void timerCallback() override;

    // Optional title for the meter
    void setTitle(const juce::String& title) { meterTitle = title; }

private:
    float level = 0.0f;
    float targetLevel = 0.0f;
    float needleAngle = -45.0f;  // Degrees, -45 = left, +45 = right
    float targetAngle = -45.0f;

    // Ballistics (300ms attack/release for vintage VU feel)
    static constexpr float attackTime = 0.3f;   // 300ms
    static constexpr float releaseTime = 0.3f;  // 300ms
    float smoothingCoeff = 0.0f;

    juce::String meterTitle;

    // Horizontal bar-graph meter (UAD style)
    void drawHorizontalBarMeter(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawBarMeterScale(juce::Graphics& g, juce::Rectangle<float> ledBounds);

    // Traditional VU meter (legacy)
    void drawVUMeterFace(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawNeedle(juce::Graphics& g, juce::Rectangle<float> bounds, float angle);
    void drawScaleArc(juce::Graphics& g, juce::Point<float> centre, float radius);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeter)
};

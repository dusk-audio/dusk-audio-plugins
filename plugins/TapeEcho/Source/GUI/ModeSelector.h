/*
  ==============================================================================

    RE-201 Space Echo - Mode Selector
    UAD Galaxy-style rotary mode selector with cream ring
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../ui/Colours.h"

class ModeSelector : public juce::Component
{
public:
    ModeSelector();
    ~ModeSelector() override = default;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

    void setMode(int newMode);
    int getMode() const { return currentMode; }

    std::function<void(int)> onModeChanged;

private:
    int currentMode = 0;
    static constexpr int numModes = 12;

    juce::Point<float> lastMousePosition;
    float knobAngle = -135.0f;  // Degrees, -135 to +135 range

    // Mode names for display
    static constexpr const char* modeNames[] = {
        "HEAD 1", "HEAD 2", "HEAD 3",
        "H1+H2", "H1+H3", "H2+H3", "ALL",
        "H1+H2+R", "H1+H3+R", "H2+H3+R", "ALL+REV",
        "REVERB"
    };

    void updateAngleFromMode();
    void updateModeFromAngle();

    // UAD Galaxy-style drawing methods
    void drawCreamRing(juce::Graphics& g, juce::Point<float> centre, float outerRadius, float innerRadius);
    void drawRecessedCenter(juce::Graphics& g, juce::Point<float> centre, float radius);
    void drawChickenHeadKnob(juce::Graphics& g, juce::Point<float> centre, float radius, float angle);
    void drawPositionNumbers(juce::Graphics& g, juce::Point<float> centre, float ringRadius);
    void drawCurvedLabels(juce::Graphics& g, juce::Point<float> centre, float radius);
    void drawModeDisplay(juce::Graphics& g, juce::Rectangle<float> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModeSelector)
};

/*
  ==============================================================================

    RE-201 Space Echo - Toggle Switch
    Chrome bat-handle toggle switch component
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Colours.h"

class ToggleSwitch : public juce::Component
{
public:
    ToggleSwitch(const juce::String& labelText = "");
    ~ToggleSwitch() override = default;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

    void setToggleState(bool shouldBeOn);
    bool getToggleState() const { return isOn; }

    void setLabelText(const juce::String& text) { label = text; repaint(); }

    std::function<void(bool)> onStateChange;

private:
    bool isOn = false;
    juce::String label;

    void drawSwitch(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawBatHandle(juce::Graphics& g, juce::Rectangle<float> bounds, bool up);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleSwitch)
};

/*
  ==============================================================================

    RE-201 Space Echo - Custom Look and Feel
    Authentic Roland RE-201 styling with chrome knobs
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Colours.h"

class RE201LookAndFeel : public juce::LookAndFeel_V4
{
public:
    RE201LookAndFeel();
    ~RE201LookAndFeel() override = default;

    // Rotary slider with chrome knob
    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    // Label styling
    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    // Toggle button styling (for switches)
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    // ComboBox styling
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;

    // Font
    juce::Font getLabelFont(juce::Label& label) override;

private:
    // Helper to draw ribbed edge on chrome knobs
    void drawRibbedEdge(juce::Graphics& g, float centreX, float centreY,
                        float outerRadius, float innerRadius, int numRibs);

    // Helper to draw pointer/indicator on knob
    void drawKnobPointer(juce::Graphics& g, float centreX, float centreY,
                         float radius, float angle);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RE201LookAndFeel)
};

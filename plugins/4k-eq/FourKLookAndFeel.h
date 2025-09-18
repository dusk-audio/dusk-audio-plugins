#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Custom Look and Feel for 4K EQ professional styling
*/
class FourKLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FourKLookAndFeel();
    ~FourKLookAndFeel() override = default;

    //==============================================================================
    // Slider customization
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float minSliderPos, float maxSliderPos,
                         const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    // Button customization
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    // ComboBox customization
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                     int buttonX, int buttonY, int buttonW, int buttonH,
                     juce::ComboBox& box) override;

    juce::Font getComboBoxFont(juce::ComboBox&) override;

    // Label customization
    juce::Font getLabelFont(juce::Label&) override;

    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    // Additional helper methods for professional styling
    void drawScaleMarkings(juce::Graphics& g, float cx, float cy, float radius,
                          float startAngle, float endAngle);
    void drawValueReadout(juce::Graphics& g, juce::Slider& slider,
                         int x, int y, int width, int height);

private:
    // Professional colors
    juce::Colour knobColour;
    juce::Colour backgroundColour;
    juce::Colour outlineColour;
    juce::Colour textColour;
    juce::Colour highlightColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKLookAndFeel)
};
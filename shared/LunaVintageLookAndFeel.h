#pragma once

#include <JuceHeader.h>

// Luna Co. Audio - Shared Vintage Look and Feel
// Base class for vintage-style plugin interfaces
class LunaVintageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Color constants
    static constexpr uint32_t BACKGROUND_COLOR = 0xff1a1a1a;
    static constexpr uint32_t PANEL_COLOR = 0xff2a2a2a;
    static constexpr uint32_t TEXT_COLOR = 0xffcccccc;

    LunaVintageLookAndFeel()
    {
        // Dark vintage color scheme
        setColour(juce::Slider::thumbColourId, juce::Colour(0xffcccccc));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff444444));
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff666666));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff333333));
        setColour(juce::Label::textColourId, juce::Colour(0xffcccccc));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        // Vintage-style rotary knob
        auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
        auto centreX = x + width * 0.5f;
        auto centreY = y + height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Body
        g.setColour(juce::Colour(0xff2a2a2a));
        g.fillEllipse(rx, ry, rw, rw);

        // Outline
        g.setColour(juce::Colour(0xff555555));
        g.drawEllipse(rx, ry, rw, rw, 2.0f);

        // Pointer
        juce::Path p;
        auto pointerLength = radius * 0.5f;
        auto pointerThickness = 3.0f;
        p.addRectangle(-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
        p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        g.setColour(juce::Colour(0xffdddddd));
        g.fillPath(p);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().reduced(2);
        auto isOn = button.getToggleState();

        // Background
        g.setColour(isOn ? juce::Colour(0xff555555) : juce::Colour(0xff2a2a2a));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

        // Border
        g.setColour(juce::Colour(0xff666666));
        g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.5f);

        // Text
        g.setColour(juce::Colour(0xffcccccc));
        g.setFont(14.0f);
        g.drawText(button.getButtonText(), bounds, juce::Justification::centred);
    }

    // Helper method to draw plugin headers
    static void drawPluginHeader(juce::Graphics& g, juce::Rectangle<int> bounds,
                                  const juce::String& pluginName, const juce::String& subtitle)
    {
        auto headerArea = bounds.removeFromTop(60);

        // Draw plugin name in its own area
        auto nameArea = headerArea.removeFromTop(30).reduced(10, 5);
        g.setColour(juce::Colour(TEXT_COLOR));
        g.setFont(juce::Font(juce::FontOptions(24.0f)).withStyle(juce::Font::bold));
        g.drawText(pluginName, nameArea, juce::Justification::centredLeft);

        // Draw subtitle in the remaining area
        auto subtitleArea = headerArea.reduced(10, 0);
        g.setColour(juce::Colour(0xff888888));
        g.setFont(juce::Font(juce::FontOptions(12.0f)).withStyle(juce::Font::italic));
        g.drawText(subtitle, subtitleArea, juce::Justification::centredLeft);
    }
};

// Alias for compatibility
using LunaLookAndFeel = LunaVintageLookAndFeel;

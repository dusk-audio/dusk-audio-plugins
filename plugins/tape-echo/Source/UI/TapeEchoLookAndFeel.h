/*
  ==============================================================================

    TapeEchoLookAndFeel.h
    Tape Echo - Modern UI Styling

    Color Palette:
    - Primary: Deep forest green (#1a332a)
    - Secondary: Muted sage (#4a6b5d)
    - Accent: Soft mint (#7fbc9d)
    - Text: Off-white (#e8efe8)
    - Highlights: Warm amber (#d4a055) for LEDs

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class TapeEchoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Color palette
    static inline const juce::Colour primaryColor    { 0xff1a332a };  // Deep forest green
    static inline const juce::Colour secondaryColor  { 0xff4a6b5d };  // Muted sage
    static inline const juce::Colour accentColor     { 0xff7fbc9d };  // Soft mint
    static inline const juce::Colour textColor       { 0xffe8efe8 };  // Off-white
    static inline const juce::Colour highlightColor  { 0xffd4a055 };  // Warm amber
    static inline const juce::Colour darkBgColor     { 0xff0f1f1a };  // Darker background
    static inline const juce::Colour knobColor       { 0xff2a4a3f };  // Knob background

    TapeEchoLookAndFeel()
    {
        setColour(juce::Slider::thumbColourId, accentColor);
        setColour(juce::Slider::rotarySliderFillColourId, accentColor);
        setColour(juce::Slider::rotarySliderOutlineColourId, secondaryColor);
        setColour(juce::Label::textColourId, textColor);
        setColour(juce::ComboBox::backgroundColourId, secondaryColor);
        setColour(juce::ComboBox::textColourId, textColor);
        setColour(juce::PopupMenu::backgroundColourId, primaryColor);
        setColour(juce::PopupMenu::textColourId, textColor);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accentColor);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const float radius = static_cast<float>(juce::jmin(width, height)) * 0.4f;
        const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float rx = centreX - radius;
        const float ry = centreY - radius;
        const float rw = radius * 2.0f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Outer shadow
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillEllipse(rx + 2, ry + 2, rw, rw);

        // Knob background with gradient
        juce::ColourGradient knobGradient(
            knobColor.brighter(0.2f), centreX, centreY - radius,
            knobColor.darker(0.3f), centreX, centreY + radius, false);
        g.setGradientFill(knobGradient);
        g.fillEllipse(rx, ry, rw, rw);

        // Subtle bevel
        g.setColour(secondaryColor.withAlpha(0.3f));
        g.drawEllipse(rx, ry, rw, rw, 1.5f);

        // Arc track (background)
        juce::Path arcTrack;
        arcTrack.addCentredArc(centreX, centreY, radius * 0.75f, radius * 0.75f,
                               0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(primaryColor.darker(0.3f));
        g.strokePath(arcTrack, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        // Arc track (value)
        if (sliderPos > 0.0f)
        {
            juce::Path arcValue;
            arcValue.addCentredArc(centreX, centreY, radius * 0.75f, radius * 0.75f,
                                   0.0f, rotaryStartAngle, angle, true);
            g.setColour(accentColor);
            g.strokePath(arcValue, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }

        // Pointer line
        juce::Path pointer;
        const float pointerLength = radius * 0.55f;
        const float pointerThickness = 2.5f;
        pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength, 1.0f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        g.setColour(textColor);
        g.fillPath(pointer);

        // Center cap
        g.setColour(secondaryColor);
        g.fillEllipse(centreX - radius * 0.2f, centreY - radius * 0.2f, radius * 0.4f, radius * 0.4f);

        // Show value when mouse is over or dragging
        if (slider.isMouseOverOrDragging())
        {
            g.setColour(textColor);
            g.setFont(juce::FontOptions(11.0f));

            juce::String valueText;
            if (slider.getTextValueSuffix().isNotEmpty())
                valueText = juce::String(slider.getValue(), 1) + slider.getTextValueSuffix();
            else
                valueText = juce::String(slider.getValue(), 1);

            // Draw value in center of knob
            g.drawText(valueText, static_cast<int>(centreX - 20), static_cast<int>(centreY - 6),
                       40, 12, juce::Justification::centred);
        }
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.setColour(textColor);
        g.setFont(getFont().withHeight(12.0f));
        g.drawText(label.getText(), label.getLocalBounds(), juce::Justification::centred, true);
    }

    juce::Font getFont() const
    {
        return juce::Font(juce::FontOptions(12.0f));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour&, bool isMouseOverButton, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

        juce::Colour baseColour = button.getToggleState() ? accentColor : secondaryColor;

        if (isButtonDown)
            baseColour = baseColour.darker(0.2f);
        else if (isMouseOverButton)
            baseColour = baseColour.brighter(0.1f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(baseColour.brighter(0.2f));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool, bool) override
    {
        g.setColour(button.getToggleState() ? primaryColor : textColor);
        g.setFont(getFont().withHeight(11.0f));
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoLookAndFeel)
};

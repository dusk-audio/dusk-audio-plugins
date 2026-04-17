// SPDX-License-Identifier: GPL-3.0-or-later

#include "DuskAmpLookAndFeel.h"

DuskAmpLookAndFeel::DuskAmpLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (kBackground));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf01a1a1a));
    setColour (juce::TooltipWindow::textColourId, juce::Colour (kText));
    setColour (juce::TooltipWindow::outlineColourId, juce::Colour (kBorder));

    // ComboBox: subtle border, no accent outline
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (kPanel));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (kBorder));
    setColour (juce::ComboBox::textColourId, juce::Colour (kText));
    setColour (juce::ComboBox::arrowColourId, juce::Colour (kGroupText));

    // TextButton: quiet styling for Save/Delete/Load
    setColour (juce::TextButton::buttonColourId, juce::Colour (kPanel));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (kPanel));
    setColour (juce::TextButton::textColourOffId, juce::Colour (kGroupText));
    setColour (juce::TextButton::textColourOnId, juce::Colour (kValueText));
}

void DuskAmpLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                            int width, int height,
                                            float sliderPos,
                                            float rotaryStartAngle,
                                            float rotaryEndAngle,
                                            juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    float diameter = std::min (bounds.getWidth(), bounds.getHeight());
    auto centre = bounds.getCentre();
    float radius = diameter * 0.5f;

    bool isHovered  = slider.isMouseOverOrDragging();
    bool isDragging = slider.isMouseButtonDown();

    // Active glow ring when dragging
    if (isDragging)
    {
        float glowRadius = radius;
        g.setColour (juce::Colour (kAccent).withAlpha (0.12f));
        g.fillEllipse (centre.x - glowRadius, centre.y - glowRadius,
                       glowRadius * 2.0f, glowRadius * 2.0f);
    }

    // Outer dark ring
    float outerRadius = radius - 2.0f;
    g.setColour (juce::Colour (0xff0d0d0d));
    g.fillEllipse (centre.x - outerRadius, centre.y - outerRadius,
                   outerRadius * 2.0f, outerRadius * 2.0f);

    // Knob body (brightens on hover)
    float knobRadius = outerRadius - 3.0f;
    g.setColour (isHovered ? juce::Colour (kKnobFill).brighter (0.15f)
                           : juce::Colour (kKnobFill));
    g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius,
                   knobRadius * 2.0f, knobRadius * 2.0f);

    // Arc track (background)
    float arcRadius = outerRadius - 1.5f;
    float lineW = 3.0f;
    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (kBorder));
    g.strokePath (trackArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    // Filled arc with gradient (darker at start -> brighter at current position)
    float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    if (angle > rotaryStartAngle + 0.01f)
    {
        juce::Path filledArc;
        filledArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                                 0.0f, rotaryStartAngle, angle, true);

        auto accentCol = juce::Colour (kAccent);
        juce::ColourGradient arcGradient (
            accentCol.darker (0.3f),
            centre.x + arcRadius * std::sin (rotaryStartAngle),
            centre.y - arcRadius * std::cos (rotaryStartAngle),
            isDragging ? accentCol.brighter (0.2f) : accentCol,
            centre.x + arcRadius * std::sin (angle),
            centre.y - arcRadius * std::cos (angle),
            false);
        g.setGradientFill (arcGradient);
        g.strokePath (filledArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
    }

    // Dot indicator at current position (brighter when dragging)
    float dotRadius = 3.0f;
    float dotDist = knobRadius - 6.0f;
    float dotX = centre.x + dotDist * std::sin (angle);
    float dotY = centre.y - dotDist * std::cos (angle);
    g.setColour (isDragging ? juce::Colours::white : juce::Colour (kText));
    g.fillEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);

    // Draw value text inside large knobs
    if (diameter >= 70.0f)
    {
        auto text = slider.getTextFromValue (slider.getValue());
        g.setColour (juce::Colour (kValueText));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text, bounds.toNearestInt(), juce::Justification::centred);
    }
}

void DuskAmpLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (label.getFont());
    g.drawFittedText (label.getText(), label.getLocalBounds(),
                      label.getJustificationType(), 1);
}

void DuskAmpLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                            bool /*shouldDrawButtonAsHighlighted*/,
                                            bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (4.0f);
    bool on = button.getToggleState();
    auto accent = juce::Colour (kAccent);
    float cornerSize = bounds.getHeight() * 0.5f;

    if (on)
    {
        // Active glow (2px larger pill behind)
        g.setColour (accent.withAlpha (0.4f));
        g.fillRoundedRectangle (bounds.expanded (2.0f), cornerSize + 2.0f);

        // Filled pill
        g.setColour (accent);
        g.fillRoundedRectangle (bounds, cornerSize);

        // Text
        g.setColour (juce::Colours::white);
    }
    else
    {
        // Inactive background
        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (bounds, cornerSize);

        // Border
        g.setColour (juce::Colour (kBorder));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerSize, 1.0f);

        // Text
        g.setColour (juce::Colour (kGroupText));
    }

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred);
}

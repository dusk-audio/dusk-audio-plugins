/*
  ==============================================================================

    PultecLookAndFeel.h

    Vintage Pultec-style Look and Feel for Multi-Q's Tube mode.

    Emulates the classic appearance of the Pultec EQP-1A:
    - Cream/ivory colored chassis
    - Large chicken-head style knobs with gold caps
    - Warm brown tones and vintage aesthetics
    - Rotary switch styling for frequency selectors
    - Tube-era VU meter aesthetics

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class PultecLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PultecLookAndFeel()
    {
        // Vintage Pultec color palette
        chassisColor = juce::Colour(0xfff0e8d8);     // Cream/ivory chassis
        knobBodyColor = juce::Colour(0xff2a2520);    // Dark brown knob body
        knobCapColor = juce::Colour(0xffc4a050);     // Gold/brass cap
        pointerColor = juce::Colour(0xfff8f0e0);     // Cream white pointer
        textColor = juce::Colour(0xff3a3030);        // Dark brown text
        accentColor = juce::Colour(0xff8a6a40);      // Warm brown accent
        panelColor = juce::Colour(0xff201810);       // Dark panel background

        // Set component colors
        setColour(juce::Slider::thumbColourId, knobCapColor);
        setColour(juce::Slider::rotarySliderFillColourId, accentColor);
        setColour(juce::Slider::rotarySliderOutlineColourId, knobBodyColor);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3030));
        setColour(juce::TextButton::buttonOnColourId, accentColor);
        setColour(juce::TextButton::textColourOffId, chassisColor);
        setColour(juce::TextButton::textColourOnId, chassisColor);
        setColour(juce::ComboBox::backgroundColourId, panelColor);
        setColour(juce::ComboBox::textColourId, chassisColor);
        setColour(juce::ComboBox::outlineColourId, accentColor);
        setColour(juce::Label::textColourId, textColor);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
        auto centreX = x + width * 0.5f;
        auto centreY = y + height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        bool isMouseOver = slider.isMouseOverOrDragging();
        bool isDragging = slider.isMouseButtonDown();

        // Check if this is a stepped/frequency selector knob
        bool isSteppedKnob = slider.getInterval() > 0.9;  // Stepped knobs have larger intervals

        if (isSteppedKnob)
        {
            // Rotary switch style (for frequency selectors)
            drawRotarySwitchKnob(g, centreX, centreY, radius, angle, slider, isMouseOver, isDragging);
        }
        else
        {
            // Standard Pultec chicken-head style knob
            drawChickenHeadKnob(g, centreX, centreY, radius, angle, slider, isMouseOver, isDragging);
        }
    }

    void drawChickenHeadKnob(juce::Graphics& g, float centreX, float centreY, float radius,
                             float angle, juce::Slider& slider, bool isMouseOver, bool isDragging)
    {
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;

        // Outer shadow for depth
        {
            juce::ColourGradient shadowGradient(
                juce::Colour(0x50000000), centreX, centreY,
                juce::Colour(0x00000000), centreX, centreY + radius + 8, true);
            g.setGradientFill(shadowGradient);
            g.fillEllipse(rx - 4, ry, rw + 8, rw + 12);
        }

        // Brass/gold outer ring (bezel)
        {
            juce::ColourGradient bezelGradient(
                juce::Colour(0xffd4b870), centreX - radius * 0.5f, centreY - radius * 0.5f,
                juce::Colour(0xff8a6a40), centreX + radius * 0.5f, centreY + radius * 0.5f, true);
            g.setGradientFill(bezelGradient);
            g.fillEllipse(rx - 2, ry - 2, rw + 4, rw + 4);
        }

        // Dark brown knob body with 3D effect
        {
            juce::ColourGradient knobGradient(
                juce::Colour(0xff4a4038), centreX - radius * 0.6f, centreY - radius * 0.6f,
                juce::Colour(0xff1a1410), centreX + radius * 0.4f, centreY + radius * 0.6f, true);
            g.setGradientFill(knobGradient);
            g.fillEllipse(rx, ry, rw, rw);
        }

        // Inner ring highlight
        g.setColour(juce::Colour(0x20ffffff));
        g.drawEllipse(rx + 2, ry + 2, rw - 4, rw - 4, 1.0f);

        // Radial grooves for grip texture
        g.setColour(juce::Colour(0x20000000));
        for (int i = 0; i < 20; ++i)
        {
            float grooveAngle = (i / 20.0f) * juce::MathConstants<float>::twoPi;
            auto x1 = centreX + radius * 0.65f * std::cos(grooveAngle);
            auto y1 = centreY + radius * 0.65f * std::sin(grooveAngle);
            auto x2 = centreX + radius * 0.92f * std::cos(grooveAngle);
            auto y2 = centreY + radius * 0.92f * std::sin(grooveAngle);
            g.drawLine(x1, y1, x2, y2, 0.6f);
        }

        // Gold center cap
        auto capRadius = radius * 0.45f;
        {
            // Cap shadow
            g.setColour(juce::Colour(0x50000000));
            g.fillEllipse(centreX - capRadius + 1, centreY - capRadius + 2, capRadius * 2, capRadius * 2);

            // Get cap color based on parameter
            juce::Colour capColor = getKnobCapColor(slider);

            // Hover brightening
            if (isMouseOver && !isDragging)
                capColor = capColor.brighter(0.15f);

            // Main cap with gradient
            juce::ColourGradient capGradient(
                capColor.brighter(0.5f), centreX - capRadius * 0.4f, centreY - capRadius * 0.5f,
                capColor.darker(0.3f), centreX + capRadius * 0.3f, centreY + capRadius * 0.5f, true);
            g.setGradientFill(capGradient);
            g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

            // Cap highlight arc
            g.setColour(capColor.brighter(0.7f).withAlpha(0.35f));
            juce::Path highlightArc;
            highlightArc.addArc(centreX - capRadius + 2, centreY - capRadius + 2,
                               (capRadius - 2) * 2, (capRadius - 2) * 2,
                               juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 1.8f, true);
            g.strokePath(highlightArc, juce::PathStrokeType(1.5f));
        }

        // Drag indicator ring
        if (isDragging)
        {
            g.setColour(knobCapColor.withAlpha(0.4f));
            g.drawEllipse(rx - 3, ry - 3, rw + 6, rw + 6, 2.0f);
        }

        // Cream/white pointer line (chicken-head style)
        {
            juce::Path pointer;
            auto pointerLength = capRadius * 0.9f;
            auto pointerWidth = 3.0f;

            pointer.addRectangle(-pointerWidth * 0.5f, -pointerLength, pointerWidth, pointerLength * 0.85f);
            pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

            // Pointer shadow
            g.setColour(juce::Colour(0x50000000));
            g.fillPath(pointer, juce::AffineTransform::translation(0.5f, 1.0f));

            // Main pointer
            g.setColour(pointerColor);
            g.fillPath(pointer);
        }

        // Center dot
        g.setColour(juce::Colour(0xff100c08));
        g.fillEllipse(centreX - 2.5f, centreY - 2.5f, 5, 5);
        g.setColour(juce::Colour(0x30ffffff));
        g.fillEllipse(centreX - 1.5f, centreY - 2.0f, 2, 2);
    }

    void drawRotarySwitchKnob(juce::Graphics& g, float centreX, float centreY, float radius,
                               float angle, juce::Slider& slider, bool isMouseOver, bool isDragging)
    {
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;

        // Outer shadow
        {
            juce::ColourGradient shadowGradient(
                juce::Colour(0x50000000), centreX, centreY,
                juce::Colour(0x00000000), centreX, centreY + radius + 6, true);
            g.setGradientFill(shadowGradient);
            g.fillEllipse(rx - 3, ry, rw + 6, rw + 10);
        }

        // Chrome outer ring
        {
            juce::ColourGradient chromeGradient(
                juce::Colour(0xffc0b8a8), centreX - radius * 0.5f, centreY - radius * 0.5f,
                juce::Colour(0xff706858), centreX + radius * 0.5f, centreY + radius * 0.5f, true);
            g.setGradientFill(chromeGradient);
            g.fillEllipse(rx - 2, ry - 2, rw + 4, rw + 4);
        }

        // Black switch body
        {
            juce::ColourGradient switchGradient(
                juce::Colour(0xff303030), centreX - radius * 0.5f, centreY - radius * 0.5f,
                juce::Colour(0xff101010), centreX + radius * 0.4f, centreY + radius * 0.5f, true);
            g.setGradientFill(switchGradient);
            g.fillEllipse(rx, ry, rw, rw);
        }

        // Notch markings around switch
        int numSteps = static_cast<int>(slider.getMaximum() - slider.getMinimum() + 1);
        float startAngle = juce::MathConstants<float>::pi * 0.75f;
        float endAngle = juce::MathConstants<float>::pi * 2.25f;

        g.setColour(juce::Colour(0xffa09080));
        for (int i = 0; i < numSteps; ++i)
        {
            float notchAngle = startAngle + (static_cast<float>(i) / (numSteps - 1)) * (endAngle - startAngle);
            auto x1 = centreX + radius * 1.08f * std::cos(notchAngle);
            auto y1 = centreY + radius * 1.08f * std::sin(notchAngle);
            auto x2 = centreX + radius * 1.18f * std::cos(notchAngle);
            auto y2 = centreY + radius * 1.18f * std::sin(notchAngle);
            g.drawLine(x1, y1, x2, y2, 2.0f);
        }

        // Pointer arrow
        {
            juce::Path pointer;
            float pointerLength = radius * 0.7f;
            float pointerBase = 8.0f;

            pointer.addTriangle(-pointerBase * 0.5f, 0,
                               pointerBase * 0.5f, 0,
                               0, -pointerLength);
            pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

            // Shadow
            g.setColour(juce::Colour(0x40000000));
            g.fillPath(pointer, juce::AffineTransform::translation(0.5f, 1.0f));

            // Main pointer
            g.setColour(juce::Colour(0xfff0e8d8));
            g.fillPath(pointer);
        }

        // Center screw
        auto screwRadius = 4.0f;
        g.setColour(juce::Colour(0xff808080));
        g.fillEllipse(centreX - screwRadius, centreY - screwRadius, screwRadius * 2, screwRadius * 2);
        g.setColour(juce::Colour(0xff404040));
        g.drawLine(centreX - screwRadius * 0.6f, centreY,
                   centreX + screwRadius * 0.6f, centreY, 1.5f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        auto isOn = button.getToggleState();

        // Vintage push button style

        // Button shadow
        g.setColour(juce::Colour(0xff151510));
        g.fillRoundedRectangle(bounds.translated(1, 2), 5.0f);

        // Button body
        juce::Colour baseColor = isOn ? juce::Colour(0xff6a5030) : juce::Colour(0xff3a3530);

        juce::ColourGradient buttonGradient(
            baseColor.brighter(0.2f), bounds.getX(), bounds.getY(),
            baseColor.darker(0.2f), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(buttonGradient);
        g.fillRoundedRectangle(bounds, 4.0f);

        // Highlight
        if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colour(0x15ffffff));
            g.fillRoundedRectangle(bounds.reduced(1), 3.0f);
        }

        // Pressed
        if (shouldDrawButtonAsDown)
        {
            g.setColour(juce::Colour(0x20000000));
            g.fillRoundedRectangle(bounds.reduced(1), 3.0f);
        }

        // Border
        g.setColour(isOn ? accentColor : juce::Colour(0xff504840));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        // LED indicator when on
        if (isOn)
        {
            juce::Colour ledColor = juce::Colour(0xffffb040);  // Warm amber
            auto ledRect = juce::Rectangle<float>(bounds.getCentreX() - 4, bounds.getY() + 3, 8, 3);
            g.setColour(ledColor);
            g.fillRoundedRectangle(ledRect, 1.0f);
            g.setColour(ledColor.withAlpha(0.4f));
            g.fillRoundedRectangle(ledRect.expanded(2, 1), 2.0f);
        }

        // Text
        g.setColour(isOn ? chassisColor : juce::Colour(0xffa0a0a0));
        g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
        g.drawFittedText(button.getButtonText(), bounds.toNearestInt(),
                        juce::Justification::centred, 1);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));

        // Vintage dropdown style

        // Shadow
        g.setColour(juce::Colour(0xff151510));
        g.fillRoundedRectangle(bounds.translated(1, 2), 5.0f);

        // Background
        juce::ColourGradient bgGradient(
            juce::Colour(0xff3a3530), 0, 0,
            juce::Colour(0xff282420), 0, static_cast<float>(height), false);
        g.setGradientFill(bgGradient);
        g.fillRoundedRectangle(bounds, 4.0f);

        // Border
        g.setColour(accentColor.withAlpha(0.6f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        // Arrow
        float arrowCenterX = buttonX + buttonW * 0.5f;
        float arrowCenterY = buttonY + buttonH * 0.5f;
        float arrowSize = 5.0f;

        juce::Path arrow;
        arrow.addTriangle(arrowCenterX - arrowSize, arrowCenterY - arrowSize * 0.3f,
                         arrowCenterX + arrowSize, arrowCenterY - arrowSize * 0.3f,
                         arrowCenterX, arrowCenterY + arrowSize * 0.6f);

        g.setColour(chassisColor.withAlpha(0.8f));
        g.fillPath(arrow);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(14.0f).withStyle("Bold"));
    }

    juce::Font getLabelFont(juce::Label&) override
    {
        return juce::Font(juce::FontOptions(11.0f).withStyle("Bold"));
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        auto bounds = label.getLocalBounds().toFloat();

        // Vintage-style label with subtle embossing
        g.setColour(textColor.darker(0.3f));
        g.setFont(getLabelFont(label));
        g.drawFittedText(label.getText(),
                        bounds.translated(0.5f, 0.5f).toNearestInt(),
                        label.getJustificationType(), 1);

        g.setColour(textColor);
        g.drawFittedText(label.getText(), bounds.toNearestInt(),
                        label.getJustificationType(), 1);
    }

    // Helper to draw vintage scale markings
    void drawPultecScaleMarkings(juce::Graphics& g, float cx, float cy, float radius,
                                  float startAngle, float endAngle, int numSteps)
    {
        for (int i = 0; i <= numSteps; ++i)
        {
            auto tickAngle = startAngle + (i / static_cast<float>(numSteps)) * (endAngle - startAngle);
            auto tickStartRadius = radius + 3;
            auto tickEndRadius = radius + 7;

            auto startX = cx + tickStartRadius * std::cos(tickAngle);
            auto startY = cy + tickStartRadius * std::sin(tickAngle);
            auto endX = cx + tickEndRadius * std::cos(tickAngle);
            auto endY = cy + tickEndRadius * std::sin(tickAngle);

            // Darker shadow
            g.setColour(juce::Colour(0xff201810));
            g.drawLine(startX + 0.5f, startY + 0.5f, endX + 0.5f, endY + 0.5f, 1.5f);

            // Light tick
            g.setColour(juce::Colour(0xff504840));
            g.drawLine(startX, startY, endX, endY, 1.0f);
        }
    }

private:
    juce::Colour chassisColor;
    juce::Colour knobBodyColor;
    juce::Colour knobCapColor;
    juce::Colour pointerColor;
    juce::Colour textColor;
    juce::Colour accentColor;
    juce::Colour panelColor;

    juce::Colour getKnobCapColor(juce::Slider& slider)
    {
        auto name = slider.getName().toLowerCase();

        // LF section - warm brown/copper
        if (name.contains("lf_boost") || name.contains("lf_atten"))
            return juce::Colour(0xffc4784c);  // Copper

        // HF boost section - gold
        if (name.contains("hf_boost") || name.contains("hf_bandwidth"))
            return juce::Colour(0xffc4a050);  // Gold

        // HF atten section - bronze
        if (name.contains("hf_atten"))
            return juce::Colour(0xffa08040);  // Bronze

        // Input/Output - silver/chrome
        if (name.contains("input") || name.contains("output"))
            return juce::Colour(0xffa0a0a0);  // Silver

        // Tube drive - warm orange
        if (name.contains("tube") || name.contains("drive"))
            return juce::Colour(0xffb47040);  // Warm orange

        // Default gold
        return knobCapColor;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PultecLookAndFeel)
};

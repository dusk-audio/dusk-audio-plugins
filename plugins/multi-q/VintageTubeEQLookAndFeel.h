/*
  ==============================================================================

    VintageTubeEQLookAndFeel.h

    Logic Pro Vintage Tube EQ style Look and Feel for Multi-Q's Tube mode.

    Features:
    - Dark charcoal/black knobs with subtle gradient
    - White/silver radial line pointer indicator
    - Light gray/white scale numbers
    - Clean, minimal design without heavy bezels
    - Blue background aesthetic

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class VintageTubeEQLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VintageTubeEQLookAndFeel()
    {
        // Logic Pro Vintage Tube EQ inspired color palette
        faceplateColor = juce::Colour(0xff31444b);       // Dark blue-gray background
        chassisBorderColor = juce::Colour(0xff1a2a30);   // Darker border
        panelColor = juce::Colour(0xff3a5058);           // Slightly lighter panel
        knobBodyColor = juce::Colour(0xff2a2a2a);        // Dark charcoal knob body
        knobRingColor = juce::Colour(0xff1a1a1a);        // Very dark ring
        pointerColor = juce::Colour(0xffffffff);         // White pointer line
        textColor = juce::Colour(0xffc0c0c0);            // Light gray text for scale
        accentColor = juce::Colour(0xff60a0c0);          // Blue accent
        ledOnColor = juce::Colour(0xffff6030);           // Warm amber/orange jewel lamp
        ledOffColor = juce::Colour(0xff4a3828);          // Dark amber when off
        screwColor = juce::Colour(0xff6a5a48);           // Brass/bronze screw heads

        // Set component colors for dark blue-gray theme
        setColour(juce::Slider::thumbColourId, knobBodyColor);
        setColour(juce::Slider::rotarySliderFillColourId, accentColor);
        setColour(juce::Slider::rotarySliderOutlineColourId, knobRingColor);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a5058));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4a6068));
        setColour(juce::TextButton::textColourOffId, textColor);
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1a2a30));  // Darker background
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffffffff));        // White text for readability
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff506068));
        setColour(juce::Label::textColourId, textColor);
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff31444b));
        setColour(juce::PopupMenu::textColourId, textColor);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff4a6068));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xffffffff));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        // Reduce the knob body radius to leave room for scale numbers
        // The actual knob body will be smaller, but numbers will be clearly visible
        auto radius = juce::jmin(width / 2, height / 2) * 0.58f;  // Smaller knob body (was -4.0f)
        auto centreX = x + width * 0.5f;
        auto centreY = y + height * 0.5f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        bool isMouseOver = slider.isMouseOverOrDragging();

        // All knobs use the same vintage chicken-head style
        drawVintageChickenHeadKnob(g, centreX, centreY, radius, angle, slider, isMouseOver,
                                    rotaryStartAngle, rotaryEndAngle, width);
    }

    void drawVintageChickenHeadKnob(juce::Graphics& g, float centreX, float centreY, float radius,
                                     float angle, juce::Slider& slider, bool isMouseOver,
                                     float startAngle, float endAngle, int boundsWidth = 85)
    {
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;

        // Determine if stepped (frequency selector) or continuous (gain)
        bool isSteppedKnob = slider.getInterval() >= 1.0;
        int numSteps = isSteppedKnob ? static_cast<int>(slider.getMaximum() - slider.getMinimum() + 1) : 11;

        // ===== SCALE MARKINGS - LIGHT GRAY, NO HEAVY BEZEL =====
        float fontSize = static_cast<float>(boundsWidth) * 0.15f;  // Slightly smaller for cleaner look
        fontSize = juce::jmax(fontSize, 12.0f);  // Minimum 12px
        g.setFont(juce::Font(juce::FontOptions(fontSize)));

        // Calculate radius for number placement
        float numberRadius = static_cast<float>(boundsWidth) * 0.44f;  // Numbers at edge

        for (int i = 0; i < numSteps; ++i)
        {
            float tickAngle;
            if (isSteppedKnob)
                tickAngle = (numSteps > 1)
                    ? startAngle + (static_cast<float>(i) / (numSteps - 1)) * (endAngle - startAngle)
                    : startAngle;
            else
                tickAngle = startAngle + (static_cast<float>(i) / 10.0f) * (endAngle - startAngle);
            // Short tick marks - subtle
            float tickInnerRadius = radius * 1.12f;
            float tickOuterRadius = radius * 1.22f;
            float innerX = centreX + tickInnerRadius * std::sin(tickAngle);
            float innerY = centreY - tickInnerRadius * std::cos(tickAngle);
            float outerX = centreX + tickOuterRadius * std::sin(tickAngle);
            float outerY = centreY - tickOuterRadius * std::cos(tickAngle);

            g.setColour(juce::Colour(0xff909090));  // Medium gray ticks
            g.drawLine(innerX, innerY, outerX, outerY, 1.5f);

            // Numbers for continuous knobs (0-10 scale)
            if (!isSteppedKnob)
            {
                float tickX = centreX + numberRadius * std::sin(tickAngle);
                float tickY = centreY - numberRadius * std::cos(tickAngle);

                g.setColour(juce::Colour(0xffc0c0c0));  // Light gray numbers
                int textBoxSize = static_cast<int>(fontSize * 1.5f);
                g.drawText(juce::String(i),
                           static_cast<int>(tickX - textBoxSize / 2),
                           static_cast<int>(tickY - textBoxSize / 2),
                           textBoxSize, textBoxSize, juce::Justification::centred);
            }
        }

        // ===== SUBTLE KNOB SHADOW =====
        g.setColour(juce::Colour(0x30000000));
        g.fillEllipse(rx + 1, ry + 2, rw, rw);

        // ===== MAIN KNOB BODY (dark charcoal with subtle gradient) =====
        // Outer dark ring (very subtle)
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillEllipse(rx - 1, ry - 1, rw + 2, rw + 2);

        // Main knob body with 3D gradient - dark charcoal
        juce::ColourGradient knobGradient(
            juce::Colour(0xff404040), centreX - radius * 0.4f, centreY - radius * 0.5f,  // Lighter charcoal highlight
            juce::Colour(0xff1a1a1a), centreX + radius * 0.4f, centreY + radius * 0.6f,  // Darker shadow
            true);
        g.setGradientFill(knobGradient);
        g.fillEllipse(rx, ry, rw, rw);

        // Inner subtle highlight (top-left) for 3D effect
        juce::ColourGradient highlightGradient(
            juce::Colour(0x18ffffff), centreX - radius * 0.3f, centreY - radius * 0.3f,
            juce::Colour(0x00ffffff), centreX, centreY, true);
        g.setGradientFill(highlightGradient);
        g.fillEllipse(rx + 2, ry + 2, rw - 4, rw - 4);

        // Subtle edge highlight
        g.setColour(juce::Colour(0x15ffffff));
        g.drawEllipse(rx + 0.5f, ry + 0.5f, rw - 1, rw - 1, 0.5f);

        // ===== HOVER EFFECT =====
        if (isMouseOver)
        {
            g.setColour(juce::Colour(0x15ffffff));
            g.fillEllipse(rx + 2, ry + 2, rw - 4, rw - 4);
        }

        // ===== WHITE RADIAL LINE POINTER =====
        float pointerLength = radius * 0.85f;
        float pointerWidth = 3.0f;

        // Calculate pointer line endpoints
        float innerRadius = radius * 0.15f;  // Start near center
        float outerRadius = pointerLength;

        float innerX = centreX + innerRadius * std::sin(angle);
        float innerY = centreY - innerRadius * std::cos(angle);
        float outerX = centreX + outerRadius * std::sin(angle);
        float outerY = centreY - outerRadius * std::cos(angle);

        // Pointer shadow
        g.setColour(juce::Colour(0x40000000));
        g.drawLine(innerX + 1, innerY + 1, outerX + 1, outerY + 1, pointerWidth);

        // Main white pointer line
        g.setColour(juce::Colour(0xffffffff));  // Pure white
        g.drawLine(innerX, innerY, outerX, outerY, pointerWidth);

        // Small bright dot at end for emphasis
        g.fillEllipse(outerX - 2.5f, outerY - 2.5f, 5.0f, 5.0f);

        // ===== CENTER DOT (small dark circle) =====
        float capRadius = 3.0f;
        g.setColour(juce::Colour(0xff0a0a0a));
        g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

        juce::ignoreUnused(slider);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        auto isOn = button.getToggleState();

        // ===== VINTAGE JEWEL LAMP INDICATOR STYLE =====
        auto buttonArea = bounds.reduced(2);

        // Recessed bezel (darker inset)
        g.setColour(juce::Colour(0xff1a1410));
        g.fillRoundedRectangle(buttonArea.expanded(1), 5.0f);

        // Metal bezel ring
        juce::ColourGradient bezelGradient(
            juce::Colour(0xff5a5040), buttonArea.getX(), buttonArea.getY(),
            juce::Colour(0xff3a3028), buttonArea.getX(), buttonArea.getBottom(), false);
        g.setGradientFill(bezelGradient);
        g.fillRoundedRectangle(buttonArea, 5.0f);

        // Inner jewel area
        auto jewelArea = buttonArea.reduced(3);

        if (isOn)
        {
            // Glowing amber jewel lamp
            // Outer glow
            g.setColour(ledOnColor.withAlpha(0.4f));
            g.fillRoundedRectangle(jewelArea.expanded(4), 6.0f);

            // Mid glow
            g.setColour(ledOnColor.withAlpha(0.6f));
            g.fillRoundedRectangle(jewelArea.expanded(2), 5.0f);

            // Jewel body (warm amber gradient)
            juce::ColourGradient jewelGradient(
                juce::Colour(0xffff8040), jewelArea.getX(), jewelArea.getY(),
                juce::Colour(0xffcc5020), jewelArea.getX(), jewelArea.getBottom(), false);
            g.setGradientFill(jewelGradient);
            g.fillRoundedRectangle(jewelArea, 4.0f);

            // Hot spot highlight
            g.setColour(juce::Colour(0x80ffffff));
            g.fillEllipse(jewelArea.getX() + jewelArea.getWidth() * 0.3f,
                         jewelArea.getY() + 2,
                         jewelArea.getWidth() * 0.3f,
                         jewelArea.getHeight() * 0.25f);
        }
        else
        {
            // Dark jewel (off state)
            juce::ColourGradient offGradient(
                juce::Colour(0xff4a3828), jewelArea.getX(), jewelArea.getY(),
                juce::Colour(0xff2a2018), jewelArea.getX(), jewelArea.getBottom(), false);
            g.setGradientFill(offGradient);
            g.fillRoundedRectangle(jewelArea, 4.0f);

            // Subtle glass reflection
            g.setColour(juce::Colour(0x15ffffff));
            g.fillRoundedRectangle(jewelArea.reduced(2).withHeight(jewelArea.getHeight() * 0.3f), 2.0f);
        }

        // Highlight on hover
        if (shouldDrawButtonAsHighlighted && !shouldDrawButtonAsDown)
        {
            g.setColour(juce::Colour(0x10ffffff));
            g.fillRoundedRectangle(buttonArea, 5.0f);
        }

        // Text label below jewel
        g.setColour(isOn ? juce::Colour(0xff1a1410) : textColor);
        g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
        auto textArea = buttonArea.withTrimmedTop(buttonArea.getHeight() - 14);
        g.drawFittedText(button.getButtonText(), textArea.toNearestInt(),
                        juce::Justification::centred, 1);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& /*box*/) override
    {
        auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));

        // ===== VINTAGE BEVELED SELECTOR STYLE =====
        // Outer bevel shadow (makes it look recessed)
        g.setColour(juce::Colour(0x50000000));
        g.fillRoundedRectangle(bounds.expanded(2), 5.0f);

        // Outer bevel highlight (top-left)
        juce::ColourGradient outerBevel(
            juce::Colour(0x30ffffff), bounds.getX(), bounds.getY(),
            juce::Colour(0x10000000), bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(outerBevel);
        g.fillRoundedRectangle(bounds.expanded(1), 4.0f);

        // Main body with 3D gradient (darker variant for readability)
        juce::ColourGradient bgGradient(
            juce::Colour(0xff3a4a50), 0, 0,  // Slightly lighter top
            juce::Colour(0xff2a3a40), 0, static_cast<float>(height), false);  // Darker bottom
        g.setGradientFill(bgGradient);
        g.fillRoundedRectangle(bounds, 4.0f);

        // Inner highlight (top edge) for beveled look
        g.setColour(juce::Colour(0x25ffffff));
        g.drawLine(bounds.getX() + 4, bounds.getY() + 1.5f,
                   bounds.getRight() - 4, bounds.getY() + 1.5f, 1.0f);

        // Inner shadow (bottom edge) for 3D depth
        g.setColour(juce::Colour(0x30000000));
        g.drawLine(bounds.getX() + 4, bounds.getBottom() - 1.5f,
                   bounds.getRight() - 4, bounds.getBottom() - 1.5f, 1.0f);

        // Border with metallic look
        g.setColour(juce::Colour(0xff506068));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        // Pressed state
        if (isButtonDown)
        {
            g.setColour(juce::Colour(0x15000000));
            g.fillRoundedRectangle(bounds, 4.0f);
        }

        // Arrow indicator (vintage style down chevron)
        float arrowCenterX = buttonX + buttonW * 0.5f;
        float arrowCenterY = buttonY + buttonH * 0.5f;
        float arrowSize = 5.0f;

        juce::Path arrow;
        arrow.addTriangle(arrowCenterX - arrowSize, arrowCenterY - arrowSize * 0.4f,
                         arrowCenterX + arrowSize, arrowCenterY - arrowSize * 0.4f,
                         arrowCenterX, arrowCenterY + arrowSize * 0.6f);

        // Arrow shadow
        g.setColour(juce::Colour(0x40000000));
        g.fillPath(arrow, juce::AffineTransform::translation(0.5f, 0.5f));

        // Arrow body
        g.setColour(juce::Colour(0xffe0e0e0));
        g.fillPath(arrow);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        auto bounds = label.getLocalBounds().toFloat();

        // Vintage silk-screened text style
        g.setFont(getLabelFont(label));
        g.setColour(label.findColour(juce::Label::textColourId));
        g.drawFittedText(label.getText(), bounds.toNearestInt(),
                        label.getJustificationType(), 1);
    }

    juce::Font getLabelFont(juce::Label&) override
    {
        return juce::Font(juce::FontOptions(15.0f).withStyle("Bold"));  // Larger, more readable
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(14.0f).withStyle("Bold"));  // Larger combo text
    }

    // Public color getters for external use
    juce::Colour getFaceplateColor() const { return faceplateColor; }
    juce::Colour getChassisColor() const { return chassisBorderColor; }
    juce::Colour getPanelColor() const { return panelColor; }
    juce::Colour getTextColor() const { return textColor; }
    juce::Colour getAccentColor() const { return accentColor; }

private:
    juce::Colour faceplateColor;
    juce::Colour chassisBorderColor;
    juce::Colour panelColor;
    juce::Colour knobBodyColor;
    juce::Colour knobRingColor;
    juce::Colour pointerColor;
    juce::Colour textColor;
    juce::Colour accentColor;
    juce::Colour ledOnColor;
    juce::Colour ledOffColor;
    juce::Colour screwColor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VintageTubeEQLookAndFeel)
};

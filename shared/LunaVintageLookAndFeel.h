#pragma once
#include "LunaLookAndFeel.h"

// Vintage-themed variant for tape/analog plugins
class LunaVintageLookAndFeel : public LunaLookAndFeel
{
public:
    LunaVintageLookAndFeel()
    {
        // Override with warmer, vintage-inspired colors while maintaining brand consistency
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffE8A628)); // Warm gold
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffE8A628));
        setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffE8A628));
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

        // Deep shadow for 3D effect
        g.setColour(juce::Colour(0x90000000));
        g.fillEllipse(rx + 4, ry + 4, rw, rw);

        // Metallic outer ring with warm bronze gradient
        juce::ColourGradient outerRing(
            juce::Colour(0xff9a8468), centreX - radius, centreY - radius,
            juce::Colour(0xff4a3828), centreX + radius, centreY + radius, true);
        g.setGradientFill(outerRing);
        g.fillEllipse(rx - 4, ry - 4, rw + 8, rw + 8);

        // Inner ring highlight
        g.setColour(juce::Colour(0xffC4A878));
        g.drawEllipse(rx - 3, ry - 3, rw + 6, rw + 6, 1.5f);

        // Main knob body with vintage bakelite texture
        juce::ColourGradient bodyGradient(
            juce::Colour(0xff5a4030), centreX - radius * 0.7f, centreY - radius * 0.7f,
            juce::Colour(0xff2a1810), centreX + radius * 0.7f, centreY + radius * 0.7f, true);
        g.setGradientFill(bodyGradient);
        g.fillEllipse(rx, ry, rw, rw);

        // Inner detail ring
        g.setColour(juce::Colour(0xff3a2818));
        g.drawEllipse(rx + radius * 0.25f, ry + radius * 0.25f,
                      rw - radius * 0.5f, rw - radius * 0.5f, 2.0f);

        // Center cap with metallic finish
        auto capRadius = radius * 0.3f;
        juce::ColourGradient capGradient(
            juce::Colour(0xffA08860), centreX - capRadius, centreY - capRadius,
            juce::Colour(0xff504030), centreX + capRadius, centreY + capRadius, false);
        g.setGradientFill(capGradient);
        g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

        // Pointer line - classic cream color
        juce::Path pointer;
        auto pointerLength = radius * 0.75f;
        auto pointerWidth = 3.0f;
        pointer.addRectangle(-pointerWidth * 0.5f, -pointerLength, pointerWidth, pointerLength * 0.5f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        // Pointer shadow
        g.setColour(juce::Colour(0x80000000));
        auto shadowPointer = pointer;
        shadowPointer.applyTransform(juce::AffineTransform::translation(1, 1));
        g.fillPath(shadowPointer);

        // Main pointer with warm cream color
        g.setColour(juce::Colour(0xffF5E8D0));
        g.fillPath(pointer);

        // Position dot on pointer
        auto dotAngle = angle;
        auto dotDistance = radius * 0.65f;
        auto dotX = centreX + dotDistance * std::sin(dotAngle);
        auto dotY = centreY - dotDistance * std::cos(dotAngle);
        g.setColour(juce::Colour(0xffF5E8D0));
        g.fillEllipse(dotX - 3, dotY - 3, 6, 6);

        // Scale markings
        g.setColour(juce::Colour(0xffC4A878));
        for (int i = 0; i <= 10; ++i)
        {
            auto tickAngle = rotaryStartAngle + (i / 10.0f) * (rotaryEndAngle - rotaryStartAngle);
            auto tickLength = (i == 0 || i == 5 || i == 10) ? radius * 0.15f : radius * 0.1f;

            juce::Path tick;
            tick.addRectangle(-1.0f, -radius - 10, 2.0f, tickLength);
            tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(centreX, centreY));

            g.setColour(juce::Colour(0xffC4A878).withAlpha((i == 0 || i == 5 || i == 10) ? 1.0f : 0.6f));
            g.fillPath(tick);
        }

        // Center screw detail
        g.setColour(juce::Colour(0xff1a0a05));
        g.fillEllipse(centreX - 4, centreY - 4, 8, 8);
        g.setColour(juce::Colour(0xff7a6050));
        g.drawEllipse(centreX - 4, centreY - 4, 8, 8, 1.0f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);

        // Vintage switch style
        if (button.getToggleState())
        {
            // On state - warm gold glow
            g.setColour(juce::Colour(0xffE8A628).withAlpha(0.3f));
            g.fillRoundedRectangle(bounds.expanded(2), 6.0f);

            g.setColour(juce::Colour(0xffE8A628));
            g.fillRoundedRectangle(bounds, 5.0f);

            g.setColour(juce::Colour(BACKGROUND_COLOR));
            g.setFont(12.0f);
            g.drawText("ON", bounds, juce::Justification::centred);
        }
        else
        {
            // Off state - recessed vintage look
            g.setColour(juce::Colour(0xff1a1510));
            g.fillRoundedRectangle(bounds, 5.0f);

            g.setColour(juce::Colour(PANEL_COLOR));
            g.fillRoundedRectangle(bounds.reduced(1), 4.0f);

            g.setColour(juce::Colour(0xff8a7050));
            g.setFont(12.0f);
            g.drawText("OFF", bounds, juce::Justification::centred);
        }

        if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colour(0xffE8A628).withAlpha(0.4f));
            g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
        }
    }

    // Helper to draw VU meters in vintage style
    static void drawVUMeter(juce::Graphics& g, juce::Rectangle<float> bounds,
                           float leftLevel, float rightLevel, bool showLabels = true)
    {
        // VU meter background
        g.setColour(juce::Colour(0xff1a1510));
        g.fillRoundedRectangle(bounds, 4.0f);

        // Meter gradient background
        juce::ColourGradient meterGradient(
            juce::Colour(0xff2a2018), bounds.getX(), bounds.getY(),
            juce::Colour(0xff3a3028), bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(meterGradient);
        g.fillRoundedRectangle(bounds.reduced(2), 3.0f);

        // Draw scale
        auto meterArea = bounds.reduced(10);
        auto centerY = meterArea.getCentreY();
        auto needleLength = meterArea.getWidth() * 0.4f;

        // Scale arc
        g.setColour(juce::Colour(0xffC4A878));
        for (int i = -20; i <= 3; ++i)
        {
            float angle = juce::jmap((float)i, -20.0f, 3.0f, -45.0f, 45.0f) * juce::MathConstants<float>::pi / 180.0f;
            float x1 = meterArea.getCentreX() + (needleLength + 5) * std::cos(angle);
            float y1 = centerY - (needleLength + 5) * std::sin(angle);
            float x2 = meterArea.getCentreX() + (needleLength + 10) * std::cos(angle);
            float y2 = centerY - (needleLength + 10) * std::sin(angle);

            if (i % 5 == 0 || i == 3)
            {
                g.setColour(juce::Colour(0xffE8D4B0));
                g.drawLine(x1, y1, x2, y2, 2.0f);
            }
            else
            {
                g.setColour(juce::Colour(0xffA08860));
                g.drawLine(x1, y1, x2, y2, 1.0f);
            }
        }

        // Red zone
        for (int i = 0; i <= 3; ++i)
        {
            float angle = juce::jmap((float)i, 0.0f, 3.0f, 0.0f, 45.0f) * juce::MathConstants<float>::pi / 180.0f;
            float x1 = meterArea.getCentreX() + (needleLength + 5) * std::cos(angle);
            float y1 = centerY - (needleLength + 5) * std::sin(angle);
            float x2 = meterArea.getCentreX() + (needleLength + 10) * std::cos(angle);
            float y2 = centerY - (needleLength + 10) * std::sin(angle);
            g.setColour(juce::Colour(0xffcc4040));
            g.drawLine(x1, y1, x2, y2, 2.0f);
        }

        // Draw needles
        auto drawNeedle = [&](float level, juce::Colour colour, float offsetX)
        {
            float dbValue = 20.0f * std::log10(std::max(0.001f, level));
            float angle = juce::jmap(dbValue, -20.0f, 3.0f, -45.0f, 45.0f) * juce::MathConstants<float>::pi / 180.0f;

            juce::Path needle;
            needle.addRectangle(-1, -needleLength, 2, needleLength);
            needle.applyTransform(juce::AffineTransform::rotation(angle)
                                .translated(meterArea.getCentreX() + offsetX, centerY));

            g.setColour(colour.withAlpha(0.3f));
            g.strokePath(needle, juce::PathStrokeType(3.0f));
            g.setColour(colour);
            g.fillPath(needle);
        };

        drawNeedle(leftLevel, juce::Colour(0xffE8D4B0), -5);
        drawNeedle(rightLevel, juce::Colour(0xffD8C4A0), 5);

        // Center pivot
        g.setColour(juce::Colour(0xff7a6050));
        g.fillEllipse(meterArea.getCentreX() - 4, centerY - 4, 8, 8);

        if (showLabels)
        {
            g.setColour(juce::Colour(0xffC4A878));
            g.setFont(10.0f);
            g.drawText("VU", bounds.removeFromTop(15), juce::Justification::centred);
            g.drawText("L  R", bounds.removeFromBottom(15), juce::Justification::centred);
        }
    }
};
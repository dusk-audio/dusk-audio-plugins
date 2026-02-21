#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Dynamic EQ Knob Look and Feel

    Compact knob styling for dynamic EQ controls:
    - Dark gray knob body
    - Tan/brown center with value display
    - Orange arc indicator showing current position
    - Range labels above and below (drawn separately)
*/
class F6KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    F6KnobLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFFff8844));  // Orange arc
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
        auto diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        auto radius = diameter / 2.0f;
        auto centre = bounds.getCentre();

        // Interaction state
        bool isEnabled = slider.isEnabled();
        float alpha = isEnabled ? 1.0f : 0.4f;

        // Arc parameters
        float arcThickness = 4.0f;
        float arcRadius = radius - arcThickness - 2.0f;
        float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // ===== LAYER 1: Outer ring (dark gray) =====
        g.setColour(juce::Colour(0xFF2a2a2d).withAlpha(alpha));
        g.fillEllipse(centre.x - radius, centre.y - radius, diameter, diameter);

        // Subtle 3D edge
        g.setColour(juce::Colour(0xFF1a1a1c).withAlpha(alpha));
        g.drawEllipse(centre.x - radius, centre.y - radius, diameter, diameter, 1.5f);

        // ===== LAYER 2: Track arc (inactive) =====
        {
            juce::Path trackArc;
            trackArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                   0.0f, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(juce::Colour(0xFF3a3a3e).withAlpha(alpha * 0.6f));
            g.strokePath(trackArc, juce::PathStrokeType(arcThickness,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ===== LAYER 3: Value arc (orange) =====
        if (isEnabled && sliderPos > 0.001f)
        {
            juce::Path valueArc;

            // Check for bipolar (like gain sliders)
            bool isBipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;

            if (isBipolar)
            {
                float centerAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);
                if (toAngle > centerAngle)
                    valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                           0.0f, centerAngle, toAngle, true);
                else
                    valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                           0.0f, toAngle, centerAngle, true);
            }
            else
            {
                valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                       0.0f, rotaryStartAngle, toAngle, true);
            }

            // Orange arc color
            juce::Colour arcColor = slider.findColour(juce::Slider::rotarySliderFillColourId);
            if (arcColor == juce::Colour())
                arcColor = juce::Colour(0xFFff8844);  // Default orange

            g.setColour(arcColor);
            g.strokePath(valueArc, juce::PathStrokeType(arcThickness,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ===== LAYER 4: Inner circle (tan/brown center for value display) =====
        float innerRadius = juce::jmax(1.0f, radius - arcThickness - 8.0f);
        // Tan/brown gradient for center
        juce::ColourGradient centerGradient(
            juce::Colour(0xFF4a4540).withAlpha(alpha), centre.x, centre.y - innerRadius,  // Lighter top
            juce::Colour(0xFF3a352f).withAlpha(alpha), centre.x, centre.y + innerRadius,  // Darker bottom
            false);
        g.setGradientFill(centerGradient);
        g.fillEllipse(centre.x - innerRadius, centre.y - innerRadius,
                      innerRadius * 2.0f, innerRadius * 2.0f);

        // Subtle inner shadow
        g.setColour(juce::Colour(0x30000000));
        g.drawEllipse(centre.x - innerRadius + 1, centre.y - innerRadius + 1,
                      (innerRadius - 1) * 2.0f, (innerRadius - 1) * 2.0f, 1.0f);

        // ===== LAYER 5: Tick marks around the arc =====
        // Draw tick marks at regular intervals
        int numTicks = 11;
        float tickLength = 4.0f;
        float tickRadius = radius - 2.0f;

        for (int i = 0; i < numTicks; ++i)
        {
            float tickAngle = rotaryStartAngle + (static_cast<float>(i) / static_cast<float>(numTicks - 1))
                              * (rotaryEndAngle - rotaryStartAngle);
            float tickStartX = centre.x + (tickRadius - tickLength) * std::sin(tickAngle);
            float tickStartY = centre.y - (tickRadius - tickLength) * std::cos(tickAngle);
            float tickEndX = centre.x + tickRadius * std::sin(tickAngle);
            float tickEndY = centre.y - tickRadius * std::cos(tickAngle);

            g.setColour(juce::Colour(0xFF5a5a5e).withAlpha(alpha * 0.5f));
            g.drawLine(tickStartX, tickStartY, tickEndX, tickEndY, 1.0f);
        }

        // ===== Disabled overlay =====
        if (!isEnabled)
        {
            g.setColour(juce::Colour(0x60000000));
            g.fillEllipse(centre.x - radius, centre.y - radius, diameter, diameter);
        }
    }
};

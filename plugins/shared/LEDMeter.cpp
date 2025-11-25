#include "LEDMeter.h"

LEDMeter::LEDMeter(Orientation orient) : orientation(orient)
{
}

void LEDMeter::setLevel(float newLevel)
{
    // Clamp to reasonable dB range
    newLevel = juce::jlimit(-60.0f, 6.0f, newLevel);

    // Always update and repaint if the level has changed at all
    if (std::abs(newLevel - currentLevel) > 0.01f)
    {
        currentLevel = newLevel;
        repaint();
    }
}

juce::Colour LEDMeter::getLEDColor(int ledIndex, int totalLEDs)
{
    float position = ledIndex / (float)totalLEDs;

    if (position < 0.5f)
        return juce::Colour(0xFF00FF00);  // Green
    else if (position < 0.75f)
        return juce::Colour(0xFFFFFF00);  // Yellow
    else if (position < 0.9f)
        return juce::Colour(0xFFFF6600);  // Orange
    else
        return juce::Colour(0xFFFF0000);  // Red
}

void LEDMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(bounds, 3.0f);

    // Calculate lit LEDs based on level
    // Map -60dB to +6dB range to 0.0 to 1.0
    // -18dB should map to (42/66) = 0.636
    float normalizedLevel = juce::jlimit(0.0f, 1.0f, (currentLevel + 60.0f) / 66.0f); // Extended range to +6dB
    int litLEDs = juce::roundToInt(normalizedLevel * numLEDs);

    if (orientation == Vertical)
    {
        float ledHeight = (bounds.getHeight() - (numLEDs + 1) * 2) / numLEDs;
        float ledWidth = bounds.getWidth() - 6;

        for (int i = 0; i < numLEDs; ++i)
        {
            float y = bounds.getBottom() - 3 - (i + 1) * (ledHeight + 2);

            // LED background
            g.setColour(juce::Colour(0xFF0A0A0A));
            g.fillRoundedRectangle(3, y, ledWidth, ledHeight, 1.0f);

            // LED lit state
            if (i < litLEDs)
            {
                auto ledColor = getLEDColor(i, numLEDs);

                // Glow effect
                g.setColour(ledColor.withAlpha(0.3f));
                g.fillRoundedRectangle(2, y - 1, ledWidth + 2, ledHeight + 2, 1.0f);

                // Main LED
                g.setColour(ledColor);
                g.fillRoundedRectangle(3, y, ledWidth, ledHeight, 1.0f);

                // Highlight
                g.setColour(ledColor.brighter(0.5f).withAlpha(0.5f));
                g.fillRoundedRectangle(4, y + 1, ledWidth - 2, ledHeight / 3, 1.0f);
            }
        }
    }
    else // Horizontal
    {
        float ledWidth = (bounds.getWidth() - (numLEDs + 1) * 2) / numLEDs;
        float ledHeight = bounds.getHeight() - 6;

        for (int i = 0; i < numLEDs; ++i)
        {
            float x = 3 + i * (ledWidth + 2);

            // LED background
            g.setColour(juce::Colour(0xFF0A0A0A));
            g.fillRoundedRectangle(x, 3, ledWidth, ledHeight, 1.0f);

            // LED lit state
            if (i < litLEDs)
            {
                auto ledColor = getLEDColor(i, numLEDs);

                // Glow effect
                g.setColour(ledColor.withAlpha(0.3f));
                g.fillRoundedRectangle(x - 1, 2, ledWidth + 2, ledHeight + 2, 1.0f);

                // Main LED
                g.setColour(ledColor);
                g.fillRoundedRectangle(x, 3, ledWidth, ledHeight, 1.0f);

                // Highlight
                g.setColour(ledColor.brighter(0.5f).withAlpha(0.5f));
                g.fillRoundedRectangle(x + 1, 4, ledWidth / 3, ledHeight - 2, 1.0f);
            }
        }
    }

    // Frame
    g.setColour(juce::Colour(0xFF4A4A4A));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

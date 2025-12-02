#include "LEDMeter.h"
#include <cmath>

LEDMeter::LEDMeter(Orientation orient) : orientation(orient)
{
    updateBallisticsCoefficients();
}

void LEDMeter::setSampleRate(double /*sampleRate*/)
{
    // Sample rate not needed for UI-based ballistics
    // Keeping for API compatibility
}

void LEDMeter::setRefreshRate(float rateHz)
{
    refreshRateHz = rateHz;
    updateBallisticsCoefficients();
}

void LEDMeter::updateBallisticsCoefficients()
{
    // VU meter standard: 300ms integration time (time to reach ~99% of target)
    // For a one-pole filter, time constant tau = -1 / (refreshRate * ln(1 - coeff))
    // To reach 99% in 300ms: coeff = 1 - exp(-1 / (refreshRate * 0.3 / 4.6))
    // Simplified: coeff ≈ 1 - exp(-interval / tau) where tau is time constant

    // Attack: ~300ms to rise (standard VU behavior)
    // Time constant for 99% in 300ms is about 65ms (300ms / 4.6)
    float attackTimeMs = 65.0f;  // Time constant in ms
    float intervalMs = 1000.0f / refreshRateHz;
    attackCoeff = 1.0f - std::exp(-intervalMs / attackTimeMs);

    // Release: ~300ms to fall (matching VU standard)
    // Same time constant for symmetric VU behavior
    float releaseTimeMs = 65.0f;
    releaseCoeff = 1.0f - std::exp(-intervalMs / releaseTimeMs);

    // Calculate peak hold in UI frames
    peakHoldSamples = static_cast<int>(peakHoldTimeSeconds * refreshRateHz);
}

void LEDMeter::setLevel(float newLevel)
{
    // Clamp to reasonable dB range
    currentLevel = juce::jlimit(-60.0f, 6.0f, newLevel);

    // Apply VU ballistics (one-pole smoothing filter)
    if (currentLevel > displayLevel)
    {
        // Attack: meter rising
        displayLevel += attackCoeff * (currentLevel - displayLevel);
    }
    else
    {
        // Release: meter falling
        displayLevel += releaseCoeff * (currentLevel - displayLevel);
    }

    // Clamp display level
    displayLevel = juce::jlimit(-60.0f, 6.0f, displayLevel);

    // Peak hold logic
    if (peakHoldEnabled)
    {
        if (currentLevel > peakLevel)
        {
            // New peak detected - update and reset hold counter
            peakLevel = currentLevel;
            peakHoldCounter = peakHoldSamples;
        }
        else if (peakHoldCounter > 0)
        {
            // Still holding peak
            peakHoldCounter--;
        }
        else
        {
            // Hold time expired - let peak fall slowly
            peakLevel -= 0.5f;  // Fall at ~15dB/sec at 30Hz refresh
            if (peakLevel < displayLevel)
                peakLevel = displayLevel;
        }
    }

    // Always repaint for smooth animation
    repaint();
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

    // Calculate lit LEDs based on display level (with VU ballistics applied)
    // Map -60dB to +6dB range to 0.0 to 1.0
    // -18dB should map to (42/66) = 0.636
    float normalizedLevel = juce::jlimit(0.0f, 1.0f, (displayLevel + 60.0f) / 66.0f); // Extended range to +6dB
    int litLEDs = juce::roundToInt(normalizedLevel * numLEDs);

    // Calculate peak hold LED position
    float normalizedPeak = juce::jlimit(0.0f, 1.0f, (peakLevel + 60.0f) / 66.0f);
    int peakLED = juce::roundToInt(normalizedPeak * numLEDs) - 1;  // -1 because we want the LED index

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
            // Peak hold indicator - single LED that stays lit
            else if (peakHoldEnabled && i == peakLED && peakLED >= litLEDs)
            {
                auto ledColor = getLEDColor(i, numLEDs);

                // Dimmer glow for peak hold
                g.setColour(ledColor.withAlpha(0.2f));
                g.fillRoundedRectangle(2, y - 1, ledWidth + 2, ledHeight + 2, 1.0f);

                // Peak hold LED (slightly dimmer than lit LEDs)
                g.setColour(ledColor.withAlpha(0.8f));
                g.fillRoundedRectangle(3, y, ledWidth, ledHeight, 1.0f);
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
            // Peak hold indicator - single LED that stays lit
            else if (peakHoldEnabled && i == peakLED && peakLED >= litLEDs)
            {
                auto ledColor = getLEDColor(i, numLEDs);

                // Dimmer glow for peak hold
                g.setColour(ledColor.withAlpha(0.2f));
                g.fillRoundedRectangle(x - 1, 2, ledWidth + 2, ledHeight + 2, 1.0f);

                // Peak hold LED (slightly dimmer than lit LEDs)
                g.setColour(ledColor.withAlpha(0.8f));
                g.fillRoundedRectangle(x, 3, ledWidth, ledHeight, 1.0f);
            }
        }
    }

    // Frame
    g.setColour(juce::Colour(0xFF4A4A4A));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

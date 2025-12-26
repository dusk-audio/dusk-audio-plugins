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

float LEDMeter::applyBallistics(float current, float display)
{
    if (current > display)
    {
        // Attack: meter rising
        display += attackCoeff * (current - display);
    }
    else
    {
        // Release: meter falling
        display += releaseCoeff * (current - display);
    }
    return juce::jlimit(-60.0f, 6.0f, display);
}

void LEDMeter::updatePeakHold(float current, float display, float& peak, int& counter)
{
    if (!peakHoldEnabled)
        return;

    if (current > peak)
    {
        // New peak detected - update and reset hold counter
        peak = current;
        counter = peakHoldSamples;
    }
    else if (counter > 0)
    {
        // Still holding peak
        counter--;
    }
    else
    {
        // Hold time expired - let peak fall slowly
        peak -= 0.5f;  // Fall at ~15dB/sec at 30Hz refresh
        if (peak < display)
            peak = display;
    }
}

void LEDMeter::setLevel(float newLevel)
{
    // Clamp to reasonable dB range
    currentLevel = juce::jlimit(-60.0f, 6.0f, newLevel);

    // Apply VU ballistics
    displayLevel = applyBallistics(currentLevel, displayLevel);

    // Peak hold logic
    updatePeakHold(currentLevel, displayLevel, peakLevel, peakHoldCounter);

    // For stereo mode, use the same level for both channels when setLevel is called
    if (stereoMode)
    {
        currentLevelL = currentLevelR = currentLevel;
        displayLevelL = displayLevelR = displayLevel;
        peakLevelL = peakLevelR = peakLevel;
        peakHoldCounterL = peakHoldCounterR = peakHoldCounter;
    }

    // Always repaint for smooth animation
    repaint();
}

void LEDMeter::setStereoLevels(float leftLevel, float rightLevel)
{
    // Clamp to reasonable dB range
    currentLevelL = juce::jlimit(-60.0f, 6.0f, leftLevel);
    currentLevelR = juce::jlimit(-60.0f, 6.0f, rightLevel);

    // Apply VU ballistics for each channel
    displayLevelL = applyBallistics(currentLevelL, displayLevelL);
    displayLevelR = applyBallistics(currentLevelR, displayLevelR);

    // Peak hold logic for each channel
    updatePeakHold(currentLevelL, displayLevelL, peakLevelL, peakHoldCounterL);
    updatePeakHold(currentLevelR, displayLevelR, peakLevelR, peakHoldCounterR);

    // Also update mono level as max of L/R for backwards compatibility
    currentLevel = std::max(currentLevelL, currentLevelR);
    displayLevel = std::max(displayLevelL, displayLevelR);
    peakLevel = std::max(peakLevelL, peakLevelR);

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

void LEDMeter::paintVerticalColumn(juce::Graphics& g, juce::Rectangle<float> bounds,
                                   float level, float peak)
{
    // Calculate lit LEDs based on display level
    float normalizedLevel = juce::jlimit(0.0f, 1.0f, (level + 60.0f) / 66.0f);
    int litLEDs = juce::roundToInt(normalizedLevel * numLEDs);

    // Calculate peak hold LED position
    float normalizedPeak = juce::jlimit(0.0f, 1.0f, (peak + 60.0f) / 66.0f);
    int peakLED = juce::roundToInt(normalizedPeak * numLEDs) - 1;

    float ledHeight = (bounds.getHeight() - (numLEDs + 1) * 2) / numLEDs;
    float ledWidth = bounds.getWidth() - 4;  // 2px padding on each side

    for (int i = 0; i < numLEDs; ++i)
    {
        float y = bounds.getBottom() - 2 - (i + 1) * (ledHeight + 2);
        float x = bounds.getX() + 2;

        // LED background
        g.setColour(juce::Colour(0xFF0A0A0A));
        g.fillRoundedRectangle(x, y, ledWidth, ledHeight, 1.0f);

        // LED lit state
        if (i < litLEDs)
        {
            auto ledColor = getLEDColor(i, numLEDs);

            // Glow effect
            g.setColour(ledColor.withAlpha(0.3f));
            g.fillRoundedRectangle(x - 1, y - 1, ledWidth + 2, ledHeight + 2, 1.0f);

            // Main LED
            g.setColour(ledColor);
            g.fillRoundedRectangle(x, y, ledWidth, ledHeight, 1.0f);

            // Highlight
            g.setColour(ledColor.brighter(0.5f).withAlpha(0.5f));
            g.fillRoundedRectangle(x + 1, y + 1, ledWidth - 2, ledHeight / 3, 1.0f);
        }
        // Peak hold indicator - single LED that stays lit
        else if (peakHoldEnabled && i == peakLED && peakLED >= litLEDs)
        {
            auto ledColor = getLEDColor(i, numLEDs);

            // Dimmer glow for peak hold
            g.setColour(ledColor.withAlpha(0.2f));
            g.fillRoundedRectangle(x - 1, y - 1, ledWidth + 2, ledHeight + 2, 1.0f);

            // Peak hold LED (slightly dimmer than lit LEDs)
            g.setColour(ledColor.withAlpha(0.8f));
            g.fillRoundedRectangle(x, y, ledWidth, ledHeight, 1.0f);
        }
    }
}

void LEDMeter::paintHorizontalRow(juce::Graphics& g, juce::Rectangle<float> bounds,
                                  float level, float peak)
{
    // Calculate lit LEDs based on display level
    float normalizedLevel = juce::jlimit(0.0f, 1.0f, (level + 60.0f) / 66.0f);
    int litLEDs = juce::roundToInt(normalizedLevel * numLEDs);

    // Calculate peak hold LED position
    float normalizedPeak = juce::jlimit(0.0f, 1.0f, (peak + 60.0f) / 66.0f);
    int peakLED = juce::roundToInt(normalizedPeak * numLEDs) - 1;

    float ledWidth = (bounds.getWidth() - (numLEDs + 1) * 2) / numLEDs;
    float ledHeight = bounds.getHeight() - 4;  // 2px padding on each side

    for (int i = 0; i < numLEDs; ++i)
    {
        float x = bounds.getX() + 2 + i * (ledWidth + 2);
        float y = bounds.getY() + 2;

        // LED background
        g.setColour(juce::Colour(0xFF0A0A0A));
        g.fillRoundedRectangle(x, y, ledWidth, ledHeight, 1.0f);

        // LED lit state
        if (i < litLEDs)
        {
            auto ledColor = getLEDColor(i, numLEDs);

            // Glow effect
            g.setColour(ledColor.withAlpha(0.3f));
            g.fillRoundedRectangle(x - 1, y - 1, ledWidth + 2, ledHeight + 2, 1.0f);

            // Main LED
            g.setColour(ledColor);
            g.fillRoundedRectangle(x, y, ledWidth, ledHeight, 1.0f);

            // Highlight
            g.setColour(ledColor.brighter(0.5f).withAlpha(0.5f));
            g.fillRoundedRectangle(x + 1, y + 1, ledWidth / 3, ledHeight - 2, 1.0f);
        }
        // Peak hold indicator - single LED that stays lit
        else if (peakHoldEnabled && i == peakLED && peakLED >= litLEDs)
        {
            auto ledColor = getLEDColor(i, numLEDs);

            // Dimmer glow for peak hold
            g.setColour(ledColor.withAlpha(0.2f));
            g.fillRoundedRectangle(x - 1, y - 1, ledWidth + 2, ledHeight + 2, 1.0f);

            // Peak hold LED (slightly dimmer than lit LEDs)
            g.setColour(ledColor.withAlpha(0.8f));
            g.fillRoundedRectangle(x, y, ledWidth, ledHeight, 1.0f);
        }
    }
}

void LEDMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(bounds, 3.0f);

    if (orientation == Vertical)
    {
        if (stereoMode)
        {
            // Stereo mode: split into L and R columns with a small gap
            float gap = 2.0f;
            float labelHeight = 12.0f;
            float columnWidth = (bounds.getWidth() - gap) / 2.0f;

            // Left channel - trim bottom for label space
            auto leftBounds = bounds.withWidth(columnWidth).withTrimmedBottom(labelHeight);
            paintVerticalColumn(g, leftBounds, displayLevelL, peakLevelL);

            // Right channel - trim bottom for label space
            auto rightBounds = bounds.withLeft(bounds.getX() + columnWidth + gap)
                                     .withWidth(columnWidth)
                                     .withTrimmedBottom(labelHeight);
            paintVerticalColumn(g, rightBounds, displayLevelR, peakLevelR);

            // Draw L/R labels at the bottom (in the reserved space)
            g.setColour(juce::Colours::grey.withAlpha(0.6f));
            g.setFont(8.0f);
            g.drawText("L", bounds.withWidth(columnWidth).removeFromBottom(labelHeight),
                       juce::Justification::centred);
            g.drawText("R", bounds.withLeft(bounds.getX() + columnWidth + gap)
                                  .withWidth(columnWidth).removeFromBottom(labelHeight),
                       juce::Justification::centred);
        }
        else
        {
            // Mono mode: single column using the full width
            paintVerticalColumn(g, bounds, displayLevel, peakLevel);
        }
    }
    else // Horizontal
    {
        if (stereoMode)
        {
            // Stereo mode: split into L and R rows with a small gap
            float gap = 2.0f;
            float labelWidth = 12.0f;
            float rowHeight = (bounds.getHeight() - gap) / 2.0f;

            // Left channel (top row) - trim left for label space
            auto leftBounds = bounds.withHeight(rowHeight).withTrimmedLeft(labelWidth);
            paintHorizontalRow(g, leftBounds, displayLevelL, peakLevelL);

            // Right channel (bottom row) - trim left for label space
            auto rightBounds = bounds.withTop(bounds.getY() + rowHeight + gap)
                                     .withHeight(rowHeight)
                                     .withTrimmedLeft(labelWidth);
            paintHorizontalRow(g, rightBounds, displayLevelR, peakLevelR);

            // Draw L/R labels on the left (in the reserved space)
            g.setColour(juce::Colours::grey.withAlpha(0.6f));
            g.setFont(8.0f);
            g.drawText("L", bounds.withHeight(rowHeight).removeFromLeft(labelWidth),
                       juce::Justification::centred);
            g.drawText("R", bounds.withTop(bounds.getY() + rowHeight + gap)
                                  .withHeight(rowHeight).removeFromLeft(labelWidth),
                       juce::Justification::centred);
        }
        else
        {
            // Mono mode: single row using the full height
            paintHorizontalRow(g, bounds, displayLevel, peakLevel);
        }
    }

    // Frame
    g.setColour(juce::Colour(0xFF4A4A4A));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

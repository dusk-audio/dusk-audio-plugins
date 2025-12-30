#include "LEDMeter.h"
#include <cmath>

LEDMeter::LEDMeter(Orientation orient) : orientation(orient)
{
    setOpaque(false);
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

    // Auto-detect stereo mode based on level difference (unless forced)
    if (!stereoModeForced)
    {
        // Enable stereo display if L/R differ by more than 1dB
        float levelDiff = std::abs(displayLevelL - displayLevelR);
        stereoMode = (levelDiff > 1.0f);
    }

    // Always repaint for smooth animation
    repaint();
}

LEDMeter::LEDColors LEDMeter::getColorsForSegment(int segmentIndex) const
{
    LEDColors colors;

    // Bottom 60% green (7 LEDs), next 25% yellow (3 LEDs), top 15% red (2 LEDs)
    // With 12 LEDs: 0-6 green, 7-9 yellow, 10-11 red
    if (segmentIndex >= 10)  // Top 2 (red zone)
    {
        colors.litColor = juce::Colour(0xFFf87171);    // Bright red
        colors.unlitColor = juce::Colour(0xFF2a0d0d); // Dark red - visible but dim
        colors.glowColor = juce::Colour(0xFFef4444);
    }
    else if (segmentIndex >= 7)  // Next 3 (yellow zone)
    {
        colors.litColor = juce::Colour(0xFFfde047);    // Bright yellow
        colors.unlitColor = juce::Colour(0xFF2a2208); // Dark amber - visible but dim
        colors.glowColor = juce::Colour(0xFFeab308);
    }
    else  // Bottom 7 (green zone)
    {
        colors.litColor = juce::Colour(0xFF4ade80);    // Bright green
        colors.unlitColor = juce::Colour(0xFF0d2a12); // Dark green - visible but dim
        colors.glowColor = juce::Colour(0xFF22c55e);
    }

    return colors;
}

void LEDMeter::drawLEDSegment(juce::Graphics& g, juce::Rectangle<float> bounds,
                               bool isLit, bool isPeak, const LEDColors& colors) const
{
    const float cornerRadius = 2.0f;

    // Always draw the LED segment - lit or unlit
    if (isLit)
    {
        // === LIT LED ===
        // Outer glow (bloom effect) - multiple layers for intensity
        auto glowBounds = bounds.expanded(4.0f);

        // First glow layer (wider, more diffuse)
        g.setColour(colors.glowColor.withAlpha(0.25f));
        g.fillRoundedRectangle(glowBounds.expanded(2.0f), cornerRadius + 2.0f);

        // Second glow layer (tighter)
        g.setColour(colors.glowColor.withAlpha(0.4f));
        g.fillRoundedRectangle(glowBounds, cornerRadius + 1.0f);

        // Main body gradient (bright top to slightly darker bottom)
        juce::ColourGradient bodyGradient(colors.litColor.brighter(0.2f), bounds.getX(), bounds.getY(),
                                           colors.litColor.darker(0.3f), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(bodyGradient);
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Top highlight (plastic lens reflection)
        auto highlightBounds = bounds.reduced(1.0f).removeFromTop(bounds.getHeight() * 0.4f);
        juce::ColourGradient highlight(juce::Colours::white.withAlpha(0.35f),
                                        highlightBounds.getX(), highlightBounds.getY(),
                                        juce::Colours::white.withAlpha(0.0f),
                                        highlightBounds.getX(), highlightBounds.getBottom(), false);
        g.setGradientFill(highlight);
        g.fillRoundedRectangle(highlightBounds, cornerRadius - 0.5f);
    }
    else if (isPeak)
    {
        // === PEAK HOLD LED (slightly dimmer than lit) ===
        // Subtle glow
        g.setColour(colors.glowColor.withAlpha(0.2f));
        g.fillRoundedRectangle(bounds.expanded(2.0f), cornerRadius + 1.0f);

        // Body at ~70% brightness
        auto peakColor = colors.litColor.interpolatedWith(colors.unlitColor, 0.3f);
        juce::ColourGradient bodyGradient(peakColor.brighter(0.1f), bounds.getX(), bounds.getY(),
                                           peakColor.darker(0.2f), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(bodyGradient);
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Subtle highlight
        auto highlightBounds = bounds.reduced(1.0f).removeFromTop(bounds.getHeight() * 0.4f);
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.fillRoundedRectangle(highlightBounds, cornerRadius - 0.5f);
    }
    else
    {
        // === UNLIT LED (dim but visible) ===
        // No glow, just the dim segment

        // Main body gradient (dark, but showing the LED exists)
        juce::ColourGradient bodyGradient(colors.unlitColor.brighter(0.15f), bounds.getX(), bounds.getY(),
                                           colors.unlitColor.darker(0.1f), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(bodyGradient);
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Inner shadow for recessed look
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 0.5f);

        // Very subtle top highlight (plastic still reflects a tiny bit)
        auto highlightBounds = bounds.reduced(1.0f).removeFromTop(bounds.getHeight() * 0.3f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRoundedRectangle(highlightBounds, cornerRadius - 0.5f);
    }
}

void LEDMeter::paintVerticalColumn(juce::Graphics& g, juce::Rectangle<float> bounds,
                                    float level, float peak)
{
    // Calculate lit LEDs based on display level (-60dB to +6dB range)
    float normalizedLevel = juce::jlimit(0.0f, 1.0f, (level + 60.0f) / 66.0f);
    int litLEDs = static_cast<int>(normalizedLevel * numLEDs);

    // Calculate peak hold LED position
    float normalizedPeak = juce::jlimit(0.0f, 1.0f, (peak + 60.0f) / 66.0f);
    int peakLED = static_cast<int>(normalizedPeak * numLEDs) - 1;

    // Calculate LED dimensions with gaps
    const float gap = 2.0f;
    const float padding = 3.0f;
    auto meterArea = bounds.reduced(padding);
    float ledHeight = (meterArea.getHeight() - (numLEDs - 1) * gap) / numLEDs;
    float ledWidth = meterArea.getWidth();

    // Draw each LED segment from bottom to top
    for (int i = 0; i < numLEDs; ++i)
    {
        float y = meterArea.getBottom() - (i + 1) * (ledHeight + gap) + gap;
        auto ledBounds = juce::Rectangle<float>(meterArea.getX(), y, ledWidth, ledHeight);

        bool isLit = i < litLEDs;
        bool isPeak = (i == peakLED) && (peakHoldCounter > 0 || peakHoldCounterL > 0 || peakHoldCounterR > 0) && !isLit;

        auto colors = getColorsForSegment(i);
        drawLEDSegment(g, ledBounds, isLit, isPeak, colors);
    }
}

void LEDMeter::paintHorizontalRow(juce::Graphics& g, juce::Rectangle<float> bounds,
                                   float level, float peak)
{
    // Calculate lit LEDs based on display level (-60dB to +6dB range)
    float normalizedLevel = juce::jlimit(0.0f, 1.0f, (level + 60.0f) / 66.0f);
    int litLEDs = static_cast<int>(normalizedLevel * numLEDs);

    // Calculate peak hold LED position
    float normalizedPeak = juce::jlimit(0.0f, 1.0f, (peak + 60.0f) / 66.0f);
    int peakLED = static_cast<int>(normalizedPeak * numLEDs) - 1;

    // Calculate LED dimensions with gaps
    const float gap = 2.0f;
    const float padding = 3.0f;
    auto meterArea = bounds.reduced(padding);
    float ledWidth = (meterArea.getWidth() - (numLEDs - 1) * gap) / numLEDs;
    float ledHeight = meterArea.getHeight();

    // Draw each LED segment from left to right
    for (int i = 0; i < numLEDs; ++i)
    {
        float x = meterArea.getX() + i * (ledWidth + gap);
        auto ledBounds = juce::Rectangle<float>(x, meterArea.getY(), ledWidth, ledHeight);

        bool isLit = i < litLEDs;
        bool isPeak = (i == peakLED) && (peakHoldCounter > 0 || peakHoldCounterL > 0 || peakHoldCounterR > 0) && !isLit;

        auto colors = getColorsForSegment(i);
        drawLEDSegment(g, ledBounds, isLit, isPeak, colors);
    }
}

void LEDMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark bezel/track behind the meter
    g.setColour(juce::Colour(0xFF0a0a0a));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Inner shadow for recessed look
    juce::ColourGradient innerShadow(juce::Colours::black.withAlpha(0.6f), bounds.getX(), bounds.getY(),
                                      juce::Colours::black.withAlpha(0.0f), bounds.getX(), bounds.getY() + 10, false);
    g.setGradientFill(innerShadow);
    g.fillRoundedRectangle(bounds.withHeight(15), 4.0f);

    // Bezel border
    g.setColour(juce::Colour(0xFF2a2a2a));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

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
}

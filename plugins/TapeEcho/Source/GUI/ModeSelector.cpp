/*
  ==============================================================================

    RE-201 Space Echo - Mode Selector Implementation
    UAD Galaxy-style "HEAD SELECT" rotary with chrome knob
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#include "ModeSelector.h"

// Define static member
constexpr const char* ModeSelector::modeNames[];

ModeSelector::ModeSelector()
{
    updateAngleFromMode();
}

void ModeSelector::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto centre = bounds.getCentre();

    // Size calculations
    float componentSize = juce::jmin(bounds.getWidth(), bounds.getHeight());
    float outerRadius = componentSize * 0.42f;
    float innerRadius = outerRadius * 0.65f;
    float knobRadius = innerRadius * 0.7f;

    // 1. Draw the green ring with position markers and text
    drawCreamRing(g, centre, outerRadius, innerRadius);

    // 2. Draw position numbers around the ring
    drawPositionNumbers(g, centre, outerRadius - (outerRadius - innerRadius) * 0.5f);

    // 3. Draw curved labels outside
    drawCurvedLabels(g, centre, outerRadius + 12.0f);

    // 4. Draw the recessed dark center
    drawRecessedCenter(g, centre, innerRadius);

    // 5. Draw the chrome knob with pointer
    float angleRad = juce::degreesToRadians(knobAngle);
    drawChickenHeadKnob(g, centre, knobRadius, angleRad);

    // 6. Draw mode display at bottom
    auto displayBounds = bounds.removeFromBottom(18.0f);
    displayBounds = displayBounds.withSizeKeepingCentre(displayBounds.getWidth() * 0.7f, 16.0f);
    drawModeDisplay(g, displayBounds);
}

void ModeSelector::drawCreamRing(juce::Graphics& g, juce::Point<float> centre,
                                   float outerRadius, float innerRadius)
{
    // This is now a green ring (matches UAD Galaxy green panel)
    // Outer shadow for depth
    {
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillEllipse(centre.x - outerRadius + 2.0f, centre.y - outerRadius + 3.0f,
                      outerRadius * 2.0f, outerRadius * 2.0f);
    }

    // Main ring - darker green to stand out on panel
    {
        juce::ColourGradient ringGradient(
            RE201Colours::panelGreenLight, centre.x - outerRadius * 0.3f, centre.y - outerRadius * 0.3f,
            RE201Colours::panelGreenDark.darker(0.2f), centre.x + outerRadius * 0.5f, centre.y + outerRadius * 0.6f,
            true);
        g.setGradientFill(ringGradient);
        g.fillEllipse(centre.x - outerRadius, centre.y - outerRadius,
                      outerRadius * 2.0f, outerRadius * 2.0f);
    }

    // Cut out the inner circle (will be filled by recessed center)
    // We don't actually cut - recessed center will draw on top

    // Outer rim highlight
    {
        g.setColour(RE201Colours::panelGreenLight.withAlpha(0.3f));
        juce::Path highlight;
        highlight.addArc(centre.x - outerRadius + 1.0f, centre.y - outerRadius + 1.0f,
                         (outerRadius - 1.0f) * 2.0f, (outerRadius - 1.0f) * 2.0f,
                         juce::MathConstants<float>::pi * 1.2f,
                         juce::MathConstants<float>::pi * 1.8f, true);
        g.strokePath(highlight, juce::PathStrokeType(1.5f));
    }

    // Outer rim edge
    g.setColour(RE201Colours::panelGreenShadow);
    g.drawEllipse(centre.x - outerRadius, centre.y - outerRadius,
                  outerRadius * 2.0f, outerRadius * 2.0f, 1.0f);
}

void ModeSelector::drawRecessedCenter(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    // Dark recessed area where knob sits
    {
        juce::ColourGradient recess(
            juce::Colour(0xFF1A1A1A), centre.x, centre.y - radius * 0.5f,
            juce::Colour(0xFF0A0A0A), centre.x, centre.y + radius * 0.5f,
            false);
        g.setGradientFill(recess);
        g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    }

    // Inner shadow ring
    {
        juce::ColourGradient innerShadow(
            juce::Colours::black.withAlpha(0.6f), centre.x, centre.y - radius,
            juce::Colours::transparentBlack, centre.x, centre.y - radius * 0.4f,
            false);
        g.setGradientFill(innerShadow);

        juce::Path shadowPath;
        shadowPath.addEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
        g.fillPath(shadowPath);
    }

    // Edge definition
    g.setColour(juce::Colour(0xFF252525));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
}

void ModeSelector::drawChickenHeadKnob(juce::Graphics& g, juce::Point<float> centre,
                                        float radius, float angle)
{
    // This is now a chrome knob like UAD Galaxy (not cream chicken head)

    // Shadow
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(centre.x - radius + 2.0f, centre.y - radius + 3.0f,
                  radius * 2.0f, radius * 2.0f);

    // Main chrome body
    {
        juce::ColourGradient chromeGrad(
            juce::Colour(0xFFE8E8E8), centre.x - radius * 0.4f, centre.y - radius * 0.4f,
            juce::Colour(0xFF606060), centre.x + radius * 0.5f, centre.y + radius * 0.7f,
            true);
        chromeGrad.addColour(0.15, juce::Colour(0xFFF0F0F0));
        chromeGrad.addColour(0.4, juce::Colour(0xFFD0D0D0));
        chromeGrad.addColour(0.6, juce::Colour(0xFFB0B0B0));
        chromeGrad.addColour(0.85, juce::Colour(0xFF808080));
        g.setGradientFill(chromeGrad);
        g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    }

    // Edge ring
    g.setColour(juce::Colour(0xFF505050));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // Top highlight arc
    {
        juce::Path highlightArc;
        highlightArc.addArc(centre.x - radius * 0.85f, centre.y - radius * 0.85f,
                            radius * 1.7f, radius * 1.7f,
                            juce::MathConstants<float>::pi * 1.15f,
                            juce::MathConstants<float>::pi * 1.85f, true);
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.strokePath(highlightArc, juce::PathStrokeType(2.0f));
    }

    // Specular highlight
    {
        const float hlW = radius * 0.4f;
        const float hlH = radius * 0.2f;
        const float hlX = centre.x - radius * 0.25f;
        const float hlY = centre.y - radius * 0.4f;

        juce::ColourGradient hlGrad(
            juce::Colours::white.withAlpha(0.5f), hlX, hlY,
            juce::Colours::transparentWhite, hlX + hlW, hlY + hlH, true);
        g.setGradientFill(hlGrad);
        g.fillEllipse(hlX - hlW * 0.3f, hlY - hlH * 0.2f, hlW, hlH);
    }

    // Black pointer line
    {
        const float lineStartRadius = radius * 0.15f;
        const float lineEndRadius = radius * 0.85f;

        // Adjust angle so 0 is at top (subtract PI/2)
        float adjustedAngle = angle + juce::MathConstants<float>::halfPi;

        float cosA = std::cos(adjustedAngle);
        float sinA = std::sin(adjustedAngle);

        float x1 = centre.x + cosA * lineStartRadius;
        float y1 = centre.y + sinA * lineStartRadius;
        float x2 = centre.x + cosA * lineEndRadius;
        float y2 = centre.y + sinA * lineEndRadius;

        // Black pointer line
        g.setColour(juce::Colour(0xFF1A1A1A));
        g.drawLine(x1, y1, x2, y2, 3.0f);

        // Subtle highlight
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawLine(x1 - 0.5f, y1 - 0.5f, x2 - 0.5f, y2 - 0.5f, 1.0f);
    }
}

void ModeSelector::drawPositionNumbers(juce::Graphics& g, juce::Point<float> centre, float ringRadius)
{
    g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));

    for (int i = 0; i < numModes; ++i)
    {
        // Angle for this position (spread across 270 degrees, starting at -135)
        float angleDeg = -135.0f + (270.0f * i / (numModes - 1));
        float angleRad = juce::degreesToRadians(angleDeg);

        // Position on the ring
        float labelX = centre.x + ringRadius * std::cos(angleRad);
        float labelY = centre.y + ringRadius * std::sin(angleRad);

        // Number text (1-11, then "R" for reverb only)
        juce::String label;
        if (i == numModes - 1)
            label = "R";
        else
            label = juce::String(i + 1);

        // Color based on selection
        if (currentMode == i)
            g.setColour(RE201Colours::textWhite);
        else
            g.setColour(RE201Colours::textWhite.withAlpha(0.6f));

        // Draw text
        juce::Rectangle<float> textBounds(labelX - 8.0f, labelY - 6.0f, 16.0f, 12.0f);
        g.drawText(label, textBounds, juce::Justification::centred);
    }
}

void ModeSelector::drawCurvedLabels(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    g.setFont(juce::FontOptions(8.0f).withStyle("Bold"));
    g.setColour(RE201Colours::textWhite);

    // Simple approach: just draw "ECHO" and "REVERB" as flat labels positioned at the sides
    // This avoids the backwards text issue from character-by-character rotation

    // "ECHO" label - positioned at upper-left of the mode selector
    {
        auto echoBounds = juce::Rectangle<float>(
            centre.x - radius - 35.0f,  // Left of ring
            centre.y - radius * 0.6f,   // Upper portion
            30.0f, 40.0f);

        // Draw vertically by rotating the entire text
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(
            -juce::MathConstants<float>::halfPi,  // -90 degrees (text reads top to bottom)
            echoBounds.getCentreX(), echoBounds.getCentreY()));
        g.drawText("ECHO", echoBounds, juce::Justification::centred);
        g.restoreState();
    }

    // "REVERB" label - positioned at upper-right of the mode selector
    {
        auto reverbBounds = juce::Rectangle<float>(
            centre.x + radius + 5.0f,   // Right of ring
            centre.y - radius * 0.6f,   // Upper portion
            30.0f, 50.0f);

        // Draw vertically by rotating the entire text
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(
            juce::MathConstants<float>::halfPi,  // +90 degrees (text reads top to bottom)
            reverbBounds.getCentreX(), reverbBounds.getCentreY()));
        g.drawText("REVERB", reverbBounds, juce::Justification::centred);
        g.restoreState();
    }
}

void ModeSelector::drawModeDisplay(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Black LCD-style display
    g.setColour(juce::Colour(0xFF0A0A0A));
    g.fillRoundedRectangle(bounds, 3.0f);

    // Inner bevel
    {
        juce::ColourGradient innerBevel(
            juce::Colours::black.withAlpha(0.4f), bounds.getX(), bounds.getY(),
            juce::Colours::transparentBlack, bounds.getX(), bounds.getY() + 3.0f, false);
        g.setGradientFill(innerBevel);
        g.fillRoundedRectangle(bounds, 3.0f);
    }

    // Outer rim
    g.setColour(juce::Colour(0xFF202020));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // Green LED text
    g.setColour(RE201Colours::ledGreenOn);
    g.setFont(juce::FontOptions("Courier New", 9.0f, juce::Font::bold));
    g.drawText(modeNames[currentMode], bounds, juce::Justification::centred);
}

void ModeSelector::mouseDown(const juce::MouseEvent& event)
{
    lastMousePosition = event.position;
}

void ModeSelector::mouseDrag(const juce::MouseEvent& event)
{
    auto bounds = getLocalBounds().toFloat();
    auto centre = bounds.getCentre();

    auto currentAngle = std::atan2(event.position.y - centre.y,
                                    event.position.x - centre.x);
    auto lastAngle = std::atan2(lastMousePosition.y - centre.y,
                                 lastMousePosition.x - centre.x);

    auto angleDelta = currentAngle - lastAngle;

    // Handle wrap-around
    if (angleDelta > juce::MathConstants<float>::pi)
        angleDelta -= 2.0f * juce::MathConstants<float>::pi;
    else if (angleDelta < -juce::MathConstants<float>::pi)
        angleDelta += 2.0f * juce::MathConstants<float>::pi;

    knobAngle += juce::radiansToDegrees(angleDelta);
    knobAngle = juce::jlimit(-135.0f, 135.0f, knobAngle);

    updateModeFromAngle();
    lastMousePosition = event.position;
}

void ModeSelector::setMode(int newMode)
{
    currentMode = juce::jlimit(0, numModes - 1, newMode);
    updateAngleFromMode();
    repaint();
}

void ModeSelector::updateAngleFromMode()
{
    knobAngle = -135.0f + (270.0f * currentMode / (numModes - 1));
}

void ModeSelector::updateModeFromAngle()
{
    int oldMode = currentMode;
    float normalizedAngle = (knobAngle + 135.0f) / 270.0f;
    currentMode = juce::roundToInt(normalizedAngle * (numModes - 1));
    currentMode = juce::jlimit(0, numModes - 1, currentMode);

    if (currentMode != oldMode)
    {
        // Snap knob to exact position
        updateAngleFromMode();

        if (onModeChanged)
            onModeChanged(currentMode);
        repaint();
    }
}

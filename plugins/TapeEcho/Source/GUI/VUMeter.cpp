/*
  ==============================================================================

    RE-201 Space Echo - VU Meter Implementation
    UAD Galaxy-style horizontal LED bar-graph meter
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#include "VUMeter.h"

VUMeter::VUMeter()
{
    // 60 Hz for smooth meter movement
    startTimerHz(60);

    // Calculate smoothing coefficient for ballistics
    smoothingCoeff = 1.0f - std::exp(-1.0f / (60.0f * attackTime));
}

VUMeter::~VUMeter()
{
    stopTimer();
}

void VUMeter::setLevel(float newLevel)
{
    targetLevel = juce::jlimit(0.0f, 1.0f, newLevel);
    targetAngle = -45.0f + targetLevel * 90.0f;
}

void VUMeter::timerCallback()
{
    float delta = targetLevel - level;

    if (delta > 0)
    {
        // Attack - fast response
        level += delta * smoothingCoeff * 2.0f;
    }
    else
    {
        // Release - slower decay
        level += delta * smoothingCoeff * 0.5f;
    }

    // Update needle angle for legacy support
    needleAngle += (targetAngle - needleAngle) * smoothingCoeff;

    if (std::abs(level - targetLevel) > 0.001f)
    {
        repaint();
    }
}

void VUMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Choose meter style based on aspect ratio
    if (bounds.getWidth() > bounds.getHeight() * 1.5f)
    {
        // Horizontal bar-graph style (new UAD style)
        drawHorizontalBarMeter(g, bounds);
    }
    else
    {
        // Traditional VU style (legacy)
        drawVUMeterFace(g, bounds);
    }
}

void VUMeter::resized()
{
}

void VUMeter::drawHorizontalBarMeter(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const float padding = 2.0f;
    auto meterBounds = bounds.reduced(padding);

    // Outer bezel (dark frame)
    g.setColour(RE201Colours::vuBezel);
    g.fillRoundedRectangle(meterBounds, 3.0f);

    // Inner meter area
    auto innerBounds = meterBounds.reduced(2.0f);
    g.setColour(RE201Colours::vuBackground);
    g.fillRoundedRectangle(innerBounds, 2.0f);

    // Inner shadow for depth
    {
        juce::ColourGradient shadow(
            juce::Colours::black.withAlpha(0.4f), innerBounds.getX(), innerBounds.getY(),
            juce::Colours::transparentBlack, innerBounds.getX(), innerBounds.getY() + 5.0f, false);
        g.setGradientFill(shadow);
        g.fillRoundedRectangle(innerBounds, 2.0f);
    }

    // LED segments area
    auto ledBounds = innerBounds.reduced(3.0f, 4.0f);

    // Number of LED segments
    const int numSegments = 12;
    const float segmentWidth = (ledBounds.getWidth() - (numSegments - 1) * 2.0f) / numSegments;
    const float segmentHeight = ledBounds.getHeight();

    // Segment thresholds and colors
    // Segments: 0-7 green, 8-9 orange, 10-11 red
    for (int i = 0; i < numSegments; ++i)
    {
        float segmentX = ledBounds.getX() + i * (segmentWidth + 2.0f);
        auto segmentRect = juce::Rectangle<float>(segmentX, ledBounds.getY(),
                                                    segmentWidth, segmentHeight);

        // Determine segment threshold (0-1 range)
        float segmentThreshold = (i + 0.5f) / numSegments;

        // Determine color based on position
        juce::Colour segmentOnColor;
        juce::Colour segmentOffColor;

        if (i < 8)
        {
            segmentOnColor = RE201Colours::ledGreenOn;
            segmentOffColor = RE201Colours::ledGreenOff;
        }
        else if (i < 10)
        {
            segmentOnColor = RE201Colours::ledOrangeOn;
            segmentOffColor = juce::Colour(0xFF403010);
        }
        else
        {
            segmentOnColor = RE201Colours::ledRedOn;
            segmentOffColor = RE201Colours::ledOff;
        }

        // Is this segment lit?
        bool isLit = level >= segmentThreshold;

        if (isLit)
        {
            // Lit segment with glow effect
            g.setColour(segmentOnColor);
            g.fillRoundedRectangle(segmentRect, 1.0f);

            // Glow effect
            g.setColour(segmentOnColor.withAlpha(0.3f));
            g.fillRoundedRectangle(segmentRect.expanded(1.0f), 2.0f);

            // Bright center highlight
            auto highlightRect = segmentRect.reduced(1.0f, 2.0f);
            g.setColour(segmentOnColor.brighter(0.3f));
            g.fillRoundedRectangle(highlightRect, 1.0f);
        }
        else
        {
            // Unlit segment (dark but visible)
            g.setColour(segmentOffColor);
            g.fillRoundedRectangle(segmentRect, 1.0f);

            // Subtle inner shadow
            g.setColour(juce::Colours::black.withAlpha(0.3f));
            g.drawRoundedRectangle(segmentRect.reduced(0.5f), 1.0f, 0.5f);
        }
    }

    // Scale markings above/below
    if (bounds.getHeight() > 30.0f)
    {
        drawBarMeterScale(g, ledBounds);
    }

    // Glass reflection overlay
    {
        juce::ColourGradient glassReflection(
            juce::Colours::white.withAlpha(0.08f), innerBounds.getX(), innerBounds.getY(),
            juce::Colours::transparentWhite, innerBounds.getX(), innerBounds.getCentreY(), false);
        g.setGradientFill(glassReflection);
        g.fillRoundedRectangle(innerBounds.removeFromTop(innerBounds.getHeight() * 0.4f), 2.0f);
    }
}

void VUMeter::drawBarMeterScale(juce::Graphics& g, juce::Rectangle<float> ledBounds)
{
    g.setFont(juce::FontOptions(7.0f));
    g.setColour(RE201Colours::textLight);

    // Scale labels: -15, -10, -7, -5, -3, 0, +3
    struct ScaleLabel { float position; const char* text; };
    const ScaleLabel labels[] = {
        { 0.0f, "-15" },
        { 0.25f, "-10" },
        { 0.42f, "-7" },
        { 0.5f, "-5" },
        { 0.58f, "-3" },
        { 0.71f, "0" },
        { 1.0f, "+3" }
    };

    float scaleY = ledBounds.getY() - 10.0f;

    for (const auto& label : labels)
    {
        float x = ledBounds.getX() + label.position * ledBounds.getWidth();

        // Tick mark
        g.drawLine(x, scaleY + 6.0f, x, scaleY + 9.0f, 0.5f);

        // Label
        g.drawText(label.text,
                   juce::Rectangle<float>(x - 12.0f, scaleY - 2.0f, 24.0f, 10.0f),
                   juce::Justification::centred);
    }
}

void VUMeter::drawVUMeterFace(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Legacy analog VU meter style
    const float padding = 4.0f;
    auto meterBounds = bounds.reduced(padding);

    // Outer frame (chrome bezel)
    {
        juce::ColourGradient bezelGradient(RE201Colours::chromeLight, meterBounds.getX(), meterBounds.getY(),
                                            RE201Colours::chromeDark, meterBounds.getRight(), meterBounds.getBottom(), false);
        bezelGradient.addColour(0.5, RE201Colours::chromeMid);
        g.setGradientFill(bezelGradient);
        g.fillRoundedRectangle(meterBounds, 4.0f);
    }

    // Inner meter face (cream background)
    auto faceBounds = meterBounds.reduced(3.0f);
    g.setColour(RE201Colours::vuFace);
    g.fillRoundedRectangle(faceBounds, 2.0f);

    // Inner shadow for depth
    {
        juce::ColourGradient shadowGradient(RE201Colours::vuShadow.withAlpha(0.3f),
                                             faceBounds.getX(), faceBounds.getY(),
                                             juce::Colours::transparentBlack,
                                             faceBounds.getX(), faceBounds.getY() + 10.0f, false);
        g.setGradientFill(shadowGradient);
        g.fillRoundedRectangle(faceBounds.withHeight(10.0f), 2.0f);
    }

    // Calculate scale center and radius
    float centreX = faceBounds.getCentreX();
    float centreY = faceBounds.getBottom() - faceBounds.getHeight() * 0.15f;
    float radius = juce::jmin(faceBounds.getWidth(), faceBounds.getHeight()) * 0.55f;

    auto centre = juce::Point<float>(centreX, centreY);

    // Draw scale arc and markings
    drawScaleArc(g, centre, radius);

    // Draw meter title at top
    if (meterTitle.isNotEmpty())
    {
        g.setColour(RE201Colours::vuText);
        g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
        auto titleBounds = faceBounds.removeFromTop(14.0f);
        g.drawText(meterTitle, titleBounds, juce::Justification::centred);
    }

    // Draw "VU" label at bottom
    g.setColour(RE201Colours::vuText);
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    auto vuBounds = faceBounds.removeFromBottom(14.0f);
    g.drawText("VU", vuBounds, juce::Justification::centred);

    // Draw needle
    drawNeedle(g, faceBounds, needleAngle);

    // Draw needle pivot (hub)
    const float hubRadius = 5.0f;
    g.setColour(RE201Colours::vuNeedle);
    g.fillEllipse(centreX - hubRadius, centreY - hubRadius, hubRadius * 2.0f, hubRadius * 2.0f);

    // Highlight on hub
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.fillEllipse(centreX - hubRadius * 0.5f, centreY - hubRadius * 0.5f,
                  hubRadius * 0.8f, hubRadius * 0.8f);
}

void VUMeter::drawScaleArc(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    const float startAngle = juce::degreesToRadians(-135.0f);
    const float endAngle = juce::degreesToRadians(-45.0f);

    // Draw the arc line
    juce::Path arcPath;
    arcPath.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);

    // Green zone (-20 to 0)
    float zeroAngle = startAngle + (endAngle - startAngle) * 0.71f;
    {
        juce::Path greenArc;
        greenArc.addCentredArc(centre.x, centre.y, radius - 2.0f, radius - 2.0f,
                                0.0f, startAngle, zeroAngle, true);
        g.setColour(RE201Colours::vuGreen);
        g.strokePath(greenArc, juce::PathStrokeType(4.0f));
    }

    // Red zone (0 to +3)
    {
        juce::Path redArc;
        redArc.addCentredArc(centre.x, centre.y, radius - 2.0f, radius - 2.0f,
                              0.0f, zeroAngle, endAngle, true);
        g.setColour(RE201Colours::vuRed);
        g.strokePath(redArc, juce::PathStrokeType(4.0f));
    }

    // Draw scale markings and labels
    struct ScaleMark { float value; float position; const char* label; bool major; };
    const ScaleMark marks[] = {
        { -20.0f, 0.00f, "-20", true },
        { -10.0f, 0.29f, "-10", true },
        { -7.0f,  0.40f, "-7",  false },
        { -5.0f,  0.50f, "-5",  false },
        { -3.0f,  0.57f, "-3",  false },
        { -2.0f,  0.62f, "-2",  false },
        { -1.0f,  0.67f, "-1",  false },
        { 0.0f,   0.71f, "0",   true },
        { 1.0f,   0.80f, "+1",  false },
        { 2.0f,   0.90f, "+2",  false },
        { 3.0f,   1.00f, "+3",  true },
    };

    g.setFont(juce::FontOptions(8.0f));

    for (const auto& mark : marks)
    {
        float angle = startAngle + (endAngle - startAngle) * mark.position;
        float tickLength = mark.major ? 8.0f : 5.0f;

        float innerRadius = radius - 8.0f - tickLength;
        float outerRadius = radius - 8.0f;

        float x1 = centre.x + innerRadius * std::cos(angle);
        float y1 = centre.y + innerRadius * std::sin(angle);
        float x2 = centre.x + outerRadius * std::cos(angle);
        float y2 = centre.y + outerRadius * std::sin(angle);

        g.setColour(RE201Colours::vuText);
        g.drawLine(x1, y1, x2, y2, mark.major ? 1.5f : 1.0f);

        if (mark.major || mark.value >= 0)
        {
            float labelRadius = radius - 22.0f;
            float labelX = centre.x + labelRadius * std::cos(angle);
            float labelY = centre.y + labelRadius * std::sin(angle);

            g.setColour(mark.value >= 0 ? RE201Colours::vuRed : RE201Colours::vuText);
            g.drawText(mark.label,
                       juce::Rectangle<float>(labelX - 12.0f, labelY - 6.0f, 24.0f, 12.0f),
                       juce::Justification::centred);
        }
    }
}

void VUMeter::drawNeedle(juce::Graphics& g, juce::Rectangle<float> bounds, float angle)
{
    float centreX = bounds.getCentreX();
    float centreY = bounds.getBottom() - bounds.getHeight() * 0.15f;
    float needleLength = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

    float angleRad = juce::degreesToRadians(-90.0f + angle);

    float tipX = centreX + needleLength * std::cos(angleRad);
    float tipY = centreY + needleLength * std::sin(angleRad);

    // Draw needle shadow
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.drawLine(centreX + 1.5f, centreY + 1.5f, tipX + 1.5f, tipY + 1.5f, 2.5f);

    // Draw needle (tapered)
    juce::Path needlePath;
    const float baseWidth = 3.0f;
    const float tipWidth = 0.5f;

    float perpX = -std::sin(angleRad);
    float perpY = std::cos(angleRad);

    needlePath.startNewSubPath(centreX + perpX * baseWidth, centreY + perpY * baseWidth);
    needlePath.lineTo(tipX + perpX * tipWidth, tipY + perpY * tipWidth);
    needlePath.lineTo(tipX - perpX * tipWidth, tipY - perpY * tipWidth);
    needlePath.lineTo(centreX - perpX * baseWidth, centreY - perpY * baseWidth);
    needlePath.closeSubPath();

    g.setColour(RE201Colours::vuNeedle);
    g.fillPath(needlePath);

    // White line down center for highlight
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawLine(centreX, centreY, tipX, tipY, 0.5f);
}

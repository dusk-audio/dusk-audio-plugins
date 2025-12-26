/*
  ==============================================================================

    RE-201 Space Echo - Custom Look and Feel Implementation
    UAD Galaxy-style professional chrome knobs
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#include "RE201LookAndFeel.h"

RE201LookAndFeel::RE201LookAndFeel()
{
    // Set default colors
    setColour(juce::Slider::rotarySliderFillColourId, RE201Colours::chromeMid);
    setColour(juce::Slider::rotarySliderOutlineColourId, RE201Colours::chromeDark);
    setColour(juce::Slider::thumbColourId, RE201Colours::chromeLight);

    setColour(juce::Label::textColourId, RE201Colours::textWhite);

    setColour(juce::ComboBox::backgroundColourId, RE201Colours::frameBlack);
    setColour(juce::ComboBox::textColourId, RE201Colours::textWhite);
    setColour(juce::ComboBox::outlineColourId, RE201Colours::frameHighlight);
    setColour(juce::ComboBox::arrowColourId, RE201Colours::textWhite);

    setColour(juce::PopupMenu::backgroundColourId, RE201Colours::frameBlack);
    setColour(juce::PopupMenu::textColourId, RE201Colours::textWhite);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, RE201Colours::panelGreenDark);
    setColour(juce::PopupMenu::highlightedTextColourId, RE201Colours::textWhite);
}

void RE201LookAndFeel::drawRotarySlider(juce::Graphics& g,
                                         int x, int y, int width, int height,
                                         float sliderPosProportional,
                                         float rotaryStartAngle,
                                         float rotaryEndAngle,
                                         juce::Slider& slider)
{
    juce::ignoreUnused(slider);

    const float radius = juce::jmin(width / 2.0f, height / 2.0f) - 8.0f;  // Leave room for tick marks
    const float centerX = x + width * 0.5f;
    const float centerY = y + height * 0.5f;
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // 0. TICK MARKS (draw before knob, behind it)
    {
        const int numTicks = 11;  // Number of tick marks
        const float tickInnerRadius = radius + 4.0f;
        const float tickOuterRadius = radius + 8.0f;

        g.setColour(juce::Colour(0xFFDDDDDD));  // Light tick marks

        for (int i = 0; i < numTicks; ++i)
        {
            float tickAngle = rotaryStartAngle + (float(i) / float(numTicks - 1)) * (rotaryEndAngle - rotaryStartAngle);
            float cosT = std::cos(tickAngle);
            float sinT = std::sin(tickAngle);

            float x1 = centerX + cosT * tickInnerRadius;
            float y1 = centerY + sinT * tickInnerRadius;
            float x2 = centerX + cosT * tickOuterRadius;
            float y2 = centerY + sinT * tickOuterRadius;

            // Thicker ticks for 0 and 10
            float thickness = (i == 0 || i == numTicks - 1 || i == numTicks / 2) ? 2.0f : 1.5f;
            g.drawLine(x1, y1, x2, y2, thickness);
        }
    }

    // 1. DROP SHADOW (offset down-right)
    {
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(centerX - radius + 3, centerY - radius + 4, radius * 2, radius * 2);
    }

    // 2. OUTER RING (darker edge for depth)
    {
        g.setColour(juce::Colour(0xFF707070));
        g.fillEllipse(centerX - radius, centerY - radius, radius * 2, radius * 2);
    }

    // 3. MAIN CHROME BODY - slightly smaller to show the ring
    {
        const float bodyRadius = radius * 0.92f;

        // Main chrome body with bright gradient from top-left to bottom-right
        juce::ColourGradient chromeGrad(
            juce::Colour(0xFFEEEEEE), centerX - bodyRadius * 0.5f, centerY - bodyRadius * 0.5f,  // Bright silver top-left
            juce::Colour(0xFF666666), centerX + bodyRadius * 0.5f, centerY + bodyRadius * 0.6f,   // Dark gray bottom-right
            true);
        chromeGrad.addColour(0.2, juce::Colour(0xFFE0E0E0));   // Bright
        chromeGrad.addColour(0.4, juce::Colour(0xFFCCCCCC));   // Mid-light
        chromeGrad.addColour(0.6, juce::Colour(0xFFAAAAAA));   // Medium
        chromeGrad.addColour(0.8, juce::Colour(0xFF888888));   // Mid-dark
        g.setGradientFill(chromeGrad);
        g.fillEllipse(centerX - bodyRadius, centerY - bodyRadius, bodyRadius * 2, bodyRadius * 2);
    }

    // 4. EDGE RING (subtle dark outline)
    {
        g.setColour(juce::Colour(0xFF505050));
        g.drawEllipse(centerX - radius, centerY - radius, radius * 2, radius * 2, 1.5f);
    }

    // 5. TOP HIGHLIGHT ARC (gives 3D roundness)
    {
        juce::Path highlightArc;
        highlightArc.addArc(centerX - radius * 0.82f, centerY - radius * 0.82f,
                            radius * 1.64f, radius * 1.64f,
                            juce::MathConstants<float>::pi * 1.15f,
                            juce::MathConstants<float>::pi * 1.85f, true);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.strokePath(highlightArc, juce::PathStrokeType(2.5f));
    }

    // 6. SPECULAR HIGHLIGHT BLOB (bright spot top-left)
    {
        const float hlW = radius * 0.5f;
        const float hlH = radius * 0.3f;
        const float hlX = centerX - radius * 0.3f;
        const float hlY = centerY - radius * 0.45f;

        juce::ColourGradient hlGrad(
            juce::Colours::white.withAlpha(0.7f), hlX, hlY,
            juce::Colours::transparentWhite, hlX + hlW * 0.5f, hlY + hlH, true);
        g.setGradientFill(hlGrad);
        g.fillEllipse(hlX - hlW * 0.4f, hlY - hlH * 0.3f, hlW, hlH);
    }

    // 7. BLACK POINTER LINE (UAD style - simple line, not triangle)
    {
        const float lineStartRadius = radius * 0.2f;
        const float lineEndRadius = radius * 0.78f;

        // Calculate line start and end points
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        float x1 = centerX + cosA * lineStartRadius;
        float y1 = centerY + sinA * lineStartRadius;
        float x2 = centerX + cosA * lineEndRadius;
        float y2 = centerY + sinA * lineEndRadius;

        // Draw black pointer line
        g.setColour(juce::Colour(0xFF1A1A1A));
        g.drawLine(x1, y1, x2, y2, 3.5f);

        // Thin highlight on one side for 3D effect
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.drawLine(x1 - 0.5f, y1 - 0.5f, x2 - 0.5f, y2 - 0.5f, 1.0f);
    }
}

void RE201LookAndFeel::drawRibbedEdge(juce::Graphics& g, float centreX, float centreY,
                                       float outerRadius, float innerRadius, int numRibs)
{
    const float ribWidth = juce::MathConstants<float>::twoPi / static_cast<float>(numRibs);

    for (int i = 0; i < numRibs; ++i)
    {
        float startAngle = static_cast<float>(i) * ribWidth;
        float endAngle = startAngle + ribWidth * 0.5f;

        juce::Path ribPath;
        ribPath.addCentredArc(centreX, centreY, outerRadius, outerRadius,
                               0.0f, startAngle, endAngle, true);
        ribPath.lineTo(centreX + innerRadius * std::cos(endAngle),
                       centreY + innerRadius * std::sin(endAngle));
        ribPath.addCentredArc(centreX, centreY, innerRadius, innerRadius,
                               0.0f, endAngle, startAngle, false);
        ribPath.closeSubPath();

        if (i % 2 == 0)
            g.setColour(RE201Colours::chromeLight);
        else
            g.setColour(RE201Colours::chromeDark);

        g.fillPath(ribPath);
    }
}

void RE201LookAndFeel::drawKnobPointer(juce::Graphics& g, float centreX, float centreY,
                                        float radius, float angle)
{
    const float pointerLength = radius * 0.85f;
    const float pointerWidth = 3.0f;

    juce::Path pointer;
    pointer.addRoundedRectangle(-pointerWidth * 0.5f, -pointerLength,
                                 pointerWidth, pointerLength * 0.5f, pointerWidth * 0.25f);

    g.setColour(RE201Colours::vuNeedle);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
}

void RE201LookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    auto bounds = label.getLocalBounds().toFloat();

    // Draw text shadow for depth
    g.setColour(RE201Colours::textShadow);
    g.setFont(getLabelFont(label));
    g.drawText(label.getText(), bounds.translated(1.0f, 1.0f),
               label.getJustificationType(), true);

    // Draw main text
    g.setColour(label.findColour(juce::Label::textColourId));
    g.drawText(label.getText(), bounds, label.getJustificationType(), true);
}

void RE201LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    auto bounds = button.getLocalBounds().toFloat();
    const float toggleHeight = juce::jmin(bounds.getHeight() * 0.7f, 40.0f);
    const float toggleWidth = toggleHeight * 0.4f;

    auto toggleBounds = bounds.withSizeKeepingCentre(toggleWidth, toggleHeight);

    // Draw mounting plate (dark with slot)
    auto plateBounds = toggleBounds.expanded(4.0f, 2.0f);
    g.setColour(RE201Colours::togglePlate);
    g.fillRoundedRectangle(plateBounds, 3.0f);

    // Inner slot shadow
    g.setColour(RE201Colours::toggleSlot);
    g.fillRoundedRectangle(toggleBounds.expanded(1.0f), 2.0f);

    // Draw bat handle
    const bool isOn = button.getToggleState();
    const float batWidth = toggleWidth * 0.7f;
    const float batHeight = toggleHeight * 0.5f;

    juce::Rectangle<float> batBounds;
    if (isOn)
        batBounds = juce::Rectangle<float>(toggleBounds.getCentreX() - batWidth * 0.5f,
                                            toggleBounds.getY() - batHeight * 0.2f,
                                            batWidth, batHeight);
    else
        batBounds = juce::Rectangle<float>(toggleBounds.getCentreX() - batWidth * 0.5f,
                                            toggleBounds.getBottom() - batHeight * 0.8f,
                                            batWidth, batHeight);

    // Bat shadow
    g.setColour(RE201Colours::shadow);
    g.fillRoundedRectangle(batBounds.translated(1.5f, 1.5f), 3.0f);

    // Bat handle chrome gradient
    juce::ColourGradient batGradient(
        RE201Colours::chromeLight, batBounds.getX(), batBounds.getY(),
        RE201Colours::chromeDark, batBounds.getRight(), batBounds.getBottom(), false);
    batGradient.addColour(0.3, RE201Colours::chromeWhite);
    batGradient.addColour(0.5, RE201Colours::chromeMid);
    g.setGradientFill(batGradient);
    g.fillRoundedRectangle(batBounds, 3.0f);

    // Bat highlight
    g.setColour(RE201Colours::chromeWhite.withAlpha(0.4f));
    g.drawLine(batBounds.getX() + 2, batBounds.getY() + 2,
               batBounds.getRight() - 2, batBounds.getY() + 2, 1.0f);

    // Bat edge
    g.setColour(RE201Colours::chromeEdge);
    g.drawRoundedRectangle(batBounds, 3.0f, 0.5f);
}

void RE201LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                     int buttonX, int buttonY, int buttonW, int buttonH,
                                     juce::ComboBox& box)
{
    juce::ignoreUnused(buttonX, buttonY, buttonW, buttonH, isButtonDown);

    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

    // Background
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Border
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Arrow
    auto arrowZone = bounds.removeFromRight(bounds.getHeight()).reduced(8.0f);
    juce::Path arrow;
    arrow.addTriangle(arrowZone.getX(), arrowZone.getCentreY() - 3.0f,
                      arrowZone.getRight(), arrowZone.getCentreY() - 3.0f,
                      arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

void RE201LookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                          bool isSeparator, bool isActive, bool isHighlighted,
                                          bool isTicked, bool hasSubMenu,
                                          const juce::String& text, const juce::String& shortcutKeyText,
                                          const juce::Drawable* icon, const juce::Colour* textColour)
{
    juce::ignoreUnused(icon, shortcutKeyText, hasSubMenu, isTicked, textColour);

    if (isSeparator)
    {
        g.setColour(RE201Colours::frameHighlight);
        g.fillRect(area.reduced(5, 0).withHeight(1));
        return;
    }

    auto bounds = area.reduced(2);

    if (isHighlighted && isActive)
    {
        g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRect(bounds);
    }

    g.setColour(isActive ? findColour(juce::PopupMenu::textColourId)
                         : findColour(juce::PopupMenu::textColourId).withAlpha(0.5f));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(text, bounds.reduced(8, 0), juce::Justification::centredLeft);
}

juce::Font RE201LookAndFeel::getLabelFont(juce::Label& label)
{
    juce::ignoreUnused(label);
    return juce::Font(juce::FontOptions(10.0f).withStyle("Bold"));
}

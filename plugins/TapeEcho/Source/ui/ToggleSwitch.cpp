/*
  ==============================================================================

    RE-201 Space Echo - Toggle Switch Implementation
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#include "ToggleSwitch.h"

ToggleSwitch::ToggleSwitch(const juce::String& labelText)
    : label(labelText)
{
}

void ToggleSwitch::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Reserve space for label at bottom if present
    juce::Rectangle<float> labelBounds;
    if (label.isNotEmpty())
    {
        labelBounds = bounds.removeFromBottom(16.0f);
    }

    // Draw the switch in remaining space
    drawSwitch(g, bounds);

    // Draw label
    if (label.isNotEmpty())
    {
        // Text shadow
        g.setColour(RE201Colours::labelShadow);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(label, labelBounds.translated(1.0f, 1.0f), juce::Justification::centredTop);

        // Main text
        g.setColour(RE201Colours::labelText);
        g.drawText(label, labelBounds, juce::Justification::centredTop);
    }
}

void ToggleSwitch::drawSwitch(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Calculate switch dimensions
    float switchWidth = juce::jmin(bounds.getWidth() * 0.5f, 24.0f);
    float switchHeight = juce::jmin(bounds.getHeight() * 0.6f, 40.0f);

    auto switchBounds = bounds.withSizeKeepingCentre(switchWidth, switchHeight);

    // Draw chrome housing/bezel
    {
        juce::ColourGradient bezelGradient(RE201Colours::chromeLight, switchBounds.getX(), switchBounds.getY(),
                                            RE201Colours::chromeDark, switchBounds.getRight(), switchBounds.getBottom(), false);
        bezelGradient.addColour(0.3, RE201Colours::chromeMid);
        bezelGradient.addColour(0.7, RE201Colours::chromeRim);
        g.setGradientFill(bezelGradient);
        g.fillRoundedRectangle(switchBounds, 4.0f);
    }

    // Inner slot (recessed area)
    auto slotBounds = switchBounds.reduced(3.0f);
    g.setColour(RE201Colours::selectorBg);
    g.fillRoundedRectangle(slotBounds, 2.0f);

    // Inner shadow
    {
        juce::ColourGradient shadowGradient(juce::Colours::black.withAlpha(0.4f),
                                             slotBounds.getX(), slotBounds.getY(),
                                             juce::Colours::transparentBlack,
                                             slotBounds.getX(), slotBounds.getY() + 5.0f, false);
        g.setGradientFill(shadowGradient);
        g.fillRoundedRectangle(slotBounds.withHeight(5.0f), 2.0f);
    }

    // Draw the bat handle
    drawBatHandle(g, slotBounds, isOn);
}

void ToggleSwitch::drawBatHandle(juce::Graphics& g, juce::Rectangle<float> bounds, bool up)
{
    float handleWidth = bounds.getWidth() * 0.7f;
    float handleHeight = bounds.getHeight() * 0.45f;

    juce::Rectangle<float> handleBounds;

    if (up)
    {
        // Handle pointing up (ON state)
        handleBounds = juce::Rectangle<float>(
            bounds.getCentreX() - handleWidth * 0.5f,
            bounds.getY() - handleHeight * 0.4f,
            handleWidth,
            handleHeight
        );
    }
    else
    {
        // Handle pointing down (OFF state)
        handleBounds = juce::Rectangle<float>(
            bounds.getCentreX() - handleWidth * 0.5f,
            bounds.getBottom() - handleHeight * 0.6f,
            handleWidth,
            handleHeight
        );
    }

    // Shadow
    g.setColour(RE201Colours::shadow);
    g.fillRoundedRectangle(handleBounds.translated(1.5f, 1.5f), 3.0f);

    // Chrome bat handle with gradient
    {
        juce::ColourGradient handleGradient(RE201Colours::chromeLight,
                                             handleBounds.getX(), handleBounds.getY(),
                                             RE201Colours::chromeDark,
                                             handleBounds.getRight(), handleBounds.getBottom(), false);
        handleGradient.addColour(0.2, RE201Colours::chromeLight.brighter(0.1f));
        handleGradient.addColour(0.5, RE201Colours::chromeMid);
        handleGradient.addColour(0.8, RE201Colours::chromeDark);
        g.setGradientFill(handleGradient);
        g.fillRoundedRectangle(handleBounds, 3.0f);
    }

    // Highlight line on top edge
    g.setColour(RE201Colours::chromeLight.withAlpha(0.6f));
    g.drawLine(handleBounds.getX() + 2.0f, handleBounds.getY() + 1.0f,
               handleBounds.getRight() - 2.0f, handleBounds.getY() + 1.0f, 1.0f);

    // Rim
    g.setColour(RE201Colours::chromeDark.darker(0.2f));
    g.drawRoundedRectangle(handleBounds, 3.0f, 0.5f);
}

void ToggleSwitch::mouseDown(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    isOn = !isOn;

    if (onStateChange)
        onStateChange(isOn);

    repaint();
}

void ToggleSwitch::setToggleState(bool shouldBeOn)
{
    if (isOn != shouldBeOn)
    {
        isOn = shouldBeOn;
        repaint();
    }
}

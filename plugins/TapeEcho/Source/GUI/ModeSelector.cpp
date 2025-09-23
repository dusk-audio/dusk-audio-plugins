#include "ModeSelector.h"

ModeSelector::ModeSelector()
{
    updateAngleFromMode();
}

void ModeSelector::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto centre = bounds.getCentre();
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.35f;

    // Background plate - dark green
    g.setColour(juce::Colour(40, 45, 30));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(juce::Colour(25, 30, 18));
    g.drawRoundedRectangle(bounds, 5.0f, 2.0f);

    // Mode label at top
    g.setColour(juce::Colour(200, 190, 170));
    g.setFont(juce::Font("Arial", 11.0f, juce::Font::bold));
    g.drawText("MODE", bounds.removeFromTop(20), juce::Justification::centred);

    // Chrome knob body like hardware
    juce::ColourGradient gradient(juce::Colour(180, 175, 170), centre,
                                   juce::Colour(120, 115, 110), bounds.getTopLeft(), true);
    g.setGradientFill(gradient);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

    // Knob edge
    g.setColour(juce::Colour(90, 85, 80));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 2.0f);

    // Position markers - draw around the knob
    g.setFont(9.0f);
    for (int i = 0; i < numModes; ++i)
    {
        float angle = juce::degreesToRadians(-135.0f + (270.0f * i / (numModes - 1)));
        float markerRadius = radius + 12.0f;
        float labelRadius = radius + 25.0f;

        float markerX = centre.x + markerRadius * std::cos(angle);
        float markerY = centre.y + markerRadius * std::sin(angle);
        float labelX = centre.x + labelRadius * std::cos(angle);
        float labelY = centre.y + labelRadius * std::sin(angle);

        // Marker dot - vintage LED style when selected
        if (currentMode == i)
        {
            // Red LED glow effect
            g.setColour(juce::Colour(255, 100, 80).withAlpha(0.3f));
            g.fillEllipse(markerX - 6, markerY - 6, 12, 12);
            g.setColour(juce::Colour(255, 60, 40));
            g.fillEllipse(markerX - 3, markerY - 3, 6, 6);
            g.setColour(juce::Colour(255, 200, 180));
            g.fillEllipse(markerX - 1, markerY - 1, 2, 2);
        }
        else
        {
            g.setColour(juce::Colour(100, 95, 85));
            g.fillEllipse(markerX - 2, markerY - 2, 4, 4);
        }

        // Mode number - cream colored
        juce::String label = juce::String(i + 1);
        g.setColour(currentMode == i ? juce::Colour(255, 245, 225) : juce::Colour(150, 140, 120));
        g.setFont(9.0f);
        g.drawText(label, juce::Rectangle<float>(labelX - 8, labelY - 6, 16, 12),
                   juce::Justification::centred);
    }

    // Pointer line - thick white indicator
    float pointerAngle = juce::degreesToRadians(knobAngle);
    float pointerLength = radius * 0.8f;
    float pointerX = centre.x + pointerLength * std::cos(pointerAngle);
    float pointerY = centre.y + pointerLength * std::sin(pointerAngle);

    // Draw pointer with shadow
    juce::Path pointerPath;
    pointerPath.startNewSubPath(centre.x, centre.y);
    pointerPath.lineTo(pointerX, pointerY);

    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.strokePath(pointerPath, juce::PathStrokeType(5.0f),
                 juce::AffineTransform::translation(1.0f, 1.0f));

    g.setColour(juce::Colours::white);
    g.strokePath(pointerPath, juce::PathStrokeType(4.0f));

    // Center cap
    g.setColour(juce::Colours::darkgrey);
    g.fillEllipse(centre.x - 8, centre.y - 8, 16, 16);
    g.setColour(juce::Colours::black);
    g.drawEllipse(centre.x - 8, centre.y - 8, 16, 16, 1.0f);

    // Mode description in LED display style
    juce::String modeText;
    switch (currentMode)
    {
        case 0: modeText = "HEAD 1"; break;
        case 1: modeText = "HEAD 2"; break;
        case 2: modeText = "HEAD 3"; break;
        case 3: modeText = "H1+H2"; break;
        case 4: modeText = "H1+H3"; break;
        case 5: modeText = "H2+H3"; break;
        case 6: modeText = "ALL"; break;
        case 7: modeText = "H1+H2+R"; break;
        case 8: modeText = "H1+H3+R"; break;
        case 9: modeText = "H2+H3+R"; break;
        case 10: modeText = "ALL+REV"; break;
        case 11: modeText = "REVERB"; break;
    }

    auto textArea = bounds.removeFromBottom(22).reduced(5, 0);
    // Black display background
    g.setColour(juce::Colour(15, 18, 12));
    g.fillRoundedRectangle(textArea, 3.0f);
    g.setColour(juce::Colour(35, 40, 25));
    g.drawRoundedRectangle(textArea, 3.0f, 1.0f);
    // Green LED text
    g.setColour(juce::Colour(100, 255, 100));
    g.setFont(juce::Font("Courier New", 10.0f, juce::Font::bold));
    g.drawText(modeText, textArea, juce::Justification::centred);
}

void ModeSelector::mouseDown(const juce::MouseEvent& event)
{
    lastMousePosition = event.position;
}

void ModeSelector::mouseDrag(const juce::MouseEvent& event)
{
    auto centre = getLocalBounds().getCentre().toFloat();
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
        if (onModeChanged)
            onModeChanged(currentMode);
        repaint();
    }
}
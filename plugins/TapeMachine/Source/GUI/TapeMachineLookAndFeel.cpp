#include "TapeMachineLookAndFeel.h"

using namespace TapeMachineColors;

//==============================================================================
// Constructor
//==============================================================================
TapeMachineLookAndFeel::TapeMachineLookAndFeel()
{
    // Set up color scheme
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(textPrimary));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(panelDark));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(metalDark));

    setColour(juce::Label::textColourId, juce::Colour(textPrimary));

    setColour(juce::ComboBox::backgroundColourId, juce::Colour(panelDark));
    setColour(juce::ComboBox::textColourId, juce::Colour(textPrimary));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(metalDark));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(metalLight));

    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(panelDark));
    setColour(juce::PopupMenu::textColourId, juce::Colour(textPrimary));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(metalDark));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(metalHighlight));

    setColour(juce::TextButton::buttonColourId, juce::Colour(panelDark));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(metalDark));
    setColour(juce::TextButton::textColourOffId, juce::Colour(textSecondary));
    setColour(juce::TextButton::textColourOnId, juce::Colour(textPrimary));
}

//==============================================================================
// Premium Chicken-Head Rotary Knob
//==============================================================================
void TapeMachineLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                               juce::Slider& slider)
{
    auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
    auto centreX = x + width * 0.5f;
    auto centreY = y + height * 0.5f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Knob skirt/base (larger, behind the knob)
    float skirtRadius = radius * 1.1f;
    {
        // Shadow
        g.setColour(juce::Colour(0x60000000));
        g.fillEllipse(centreX - skirtRadius + 3, centreY - skirtRadius + 3,
                      skirtRadius * 2, skirtRadius * 2);

        // Skirt body with gradient
        juce::ColourGradient skirtGrad(
            juce::Colour(knobSkirt).brighter(0.2f), centreX - skirtRadius * 0.5f, centreY - skirtRadius * 0.5f,
            juce::Colour(knobSkirt).darker(0.2f), centreX + skirtRadius * 0.5f, centreY + skirtRadius * 0.5f,
            true);
        g.setGradientFill(skirtGrad);
        g.fillEllipse(centreX - skirtRadius, centreY - skirtRadius, skirtRadius * 2, skirtRadius * 2);

        // Skirt ring
        g.setColour(juce::Colour(0xff1a1510));
        g.drawEllipse(centreX - skirtRadius, centreY - skirtRadius, skirtRadius * 2, skirtRadius * 2, 1.5f);
    }

    // Main knob body - Bakelite-style with realistic 3D shading
    {
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;

        // Outer shadow ring for depth
        g.setColour(juce::Colour(0x40000000));
        g.fillEllipse(rx + 2, ry + 2, rw, rw);

        // Main body gradient (top-left lit)
        juce::ColourGradient bodyGrad(
            juce::Colour(knobBodyLight), centreX - radius * 0.6f, centreY - radius * 0.6f,
            juce::Colour(knobBody).darker(0.3f), centreX + radius * 0.6f, centreY + radius * 0.6f,
            true);
        g.setGradientFill(bodyGrad);
        g.fillEllipse(rx, ry, rw, rw);

        // Outer edge ring (beveled look)
        g.setColour(juce::Colour(knobRing));
        g.drawEllipse(rx, ry, rw, rw, 2.5f);

        // Inner ring for definition
        g.setColour(juce::Colour(0xff1a1510));
        g.drawEllipse(rx + 3, ry + 3, rw - 6, rw - 6, 1.2f);

        // Subtle highlight arc (top portion)
        juce::Path highlightArc;
        highlightArc.addArc(rx + 2, ry + 2, rw - 4, rw - 4,
                            -juce::MathConstants<float>::pi * 0.8f,
                            -juce::MathConstants<float>::pi * 0.2f,
                            true);
        g.setColour(juce::Colour(0x20ffffff));
        g.strokePath(highlightArc, juce::PathStrokeType(2.0f));
    }

    // Center cap
    {
        float capRadius = radius * 0.28f;
        juce::ColourGradient capGrad(
            juce::Colour(metalMid), centreX - capRadius * 0.5f, centreY - capRadius * 0.5f,
            juce::Colour(knobBody), centreX + capRadius * 0.5f, centreY + capRadius * 0.5f,
            false);
        g.setGradientFill(capGrad);
        g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

        // Cap ring
        g.setColour(juce::Colour(0xff1a1510));
        g.drawEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2, 1.0f);

        // Tiny highlight
        g.setColour(juce::Colour(0x30ffffff));
        g.fillEllipse(centreX - capRadius * 0.3f, centreY - capRadius * 0.5f,
                      capRadius * 0.6f, capRadius * 0.4f);
    }

    // Pointer/indicator line - chicken-head style
    {
        juce::Path pointer;
        float pointerLength = radius * 0.72f;
        float pointerWidth = 5.0f;

        // Glow behind pointer
        juce::Path glowPath;
        glowPath.addRoundedRectangle(-pointerWidth * 0.5f - 2.0f, -radius + 5,
                                      pointerWidth + 4.0f, pointerLength + 2.0f, 3.0f);
        glowPath.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        g.setColour(juce::Colour(0x25f8e4c0));
        g.fillPath(glowPath);

        // Main pointer
        pointer.addRoundedRectangle(-pointerWidth * 0.5f, -radius + 6,
                                    pointerWidth, pointerLength, 2.5f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        // Pointer with gradient for 3D effect
        g.setColour(juce::Colour(knobPointer));
        g.fillPath(pointer);

        // Pointer outline
        juce::Path pointerOutline;
        pointerOutline.addRoundedRectangle(-pointerWidth * 0.5f - 0.5f, -radius + 6,
                                           pointerWidth + 1.0f, pointerLength, 2.5f);
        pointerOutline.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        g.setColour(juce::Colour(0xff1a1510));
        g.strokePath(pointerOutline, juce::PathStrokeType(0.8f));
    }
}

//==============================================================================
// Illuminated Vintage Toggle Button
//==============================================================================
void TapeMachineLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2);
    auto isOn = button.getToggleState();
    auto buttonText = button.getButtonText();

    bool isNoiseSwitch = (buttonText == "ON" || buttonText == "OFF");
    bool isLinkButton = (buttonText == "LINK");

    if (isNoiseSwitch)
    {
        // Vintage rotary switch style
        float switchSize = juce::jmin(bounds.getWidth(), bounds.getHeight() - 16.0f);
        auto switchBounds = juce::Rectangle<float>(
            bounds.getCentreX() - switchSize / 2,
            bounds.getY(),
            switchSize, switchSize);

        // Shadow
        g.setColour(juce::Colour(0x50000000));
        g.fillEllipse(switchBounds.translated(2, 2));

        // Switch body - metallic gradient
        juce::ColourGradient bodyGrad(
            juce::Colour(metalMid), switchBounds.getX(), switchBounds.getY(),
            juce::Colour(knobBody), switchBounds.getRight(), switchBounds.getBottom(),
            true);
        g.setGradientFill(bodyGrad);
        g.fillEllipse(switchBounds);

        // Outer ring - brighter when on
        g.setColour(isOn ? juce::Colour(metalLight) : juce::Colour(metalDark));
        g.drawEllipse(switchBounds.reduced(1), 2.0f);

        // Position indicator
        float indicatorAngle = isOn ? -0.78f : -2.36f;
        float indicatorLength = switchSize * 0.30f;
        float cx = switchBounds.getCentreX();
        float cy = switchBounds.getCentreY();

        juce::Path indicator;
        indicator.addRoundedRectangle(-2.5f, -indicatorLength, 5.0f, indicatorLength, 2.0f);
        indicator.applyTransform(juce::AffineTransform::rotation(indicatorAngle).translated(cx, cy));

        g.setColour(isOn ? juce::Colour(knobPointer) : juce::Colour(textSecondary));
        g.fillPath(indicator);

        // OFF/ON labels
        float labelY = switchBounds.getBottom() + 4.0f;
        g.setFont(juce::Font(10.0f, juce::Font::bold));

        g.setColour(isOn ? juce::Colour(textSecondary) : juce::Colour(textPrimary));
        g.drawText("OFF", juce::Rectangle<float>(cx - switchSize * 0.65f - 12, labelY, 24, 14),
                   juce::Justification::centred);

        g.setColour(isOn ? juce::Colour(textPrimary) : juce::Colour(textSecondary));
        g.drawText("ON", juce::Rectangle<float>(cx + switchSize * 0.65f - 12, labelY, 24, 14),
                   juce::Justification::centred);
    }
    else if (isLinkButton)
    {
        // Link button with LED and chain icon
        if (isOn)
        {
            // Glow when active
            g.setColour(juce::Colour(ledAmberGlow));
            g.fillRoundedRectangle(bounds.expanded(3), 8.0f);
        }

        // Button body
        juce::ColourGradient buttonGrad(
            isOn ? juce::Colour(metalDark) : juce::Colour(panelDark),
            bounds.getCentreX(), bounds.getY(),
            isOn ? juce::Colour(panelDark) : juce::Colour(panelDark).darker(0.2f),
            bounds.getCentreX(), bounds.getBottom(),
            false);
        g.setGradientFill(buttonGrad);
        g.fillRoundedRectangle(bounds, 6.0f);

        // Border
        g.setColour(isOn ? juce::Colour(metalMid) : juce::Colour(metalDark));
        g.drawRoundedRectangle(bounds, 6.0f, 1.5f);

        // LED indicator
        auto ledSize = bounds.getHeight() * 0.38f;
        auto ledBounds = juce::Rectangle<float>(bounds.getX() + 10,
                                                 bounds.getCentreY() - ledSize / 2,
                                                 ledSize, ledSize);
        drawLED(g, ledBounds, isOn, ledAmberOff, ledAmberOn, ledAmberGlow);

        // Chain icon
        float cx = bounds.getCentreX() + 10;
        float cy = bounds.getCentreY();

        g.setColour(isOn ? juce::Colour(ledAmberOn) : juce::Colour(textSecondary));
        float linkW = 16.0f, linkH = 10.0f, overlap = 6.0f;
        g.drawRoundedRectangle(cx - linkW + overlap/2, cy - linkH/2, linkW, linkH, 4.0f, 2.0f);
        g.drawRoundedRectangle(cx - overlap/2, cy - linkH/2, linkW, linkH, 4.0f, 2.0f);
    }
    else
    {
        // Standard toggle button with LED
        if (isOn)
        {
            g.setColour(juce::Colour(ledAmberGlow));
            g.fillRoundedRectangle(bounds.expanded(2), 6.0f);
        }

        juce::ColourGradient buttonGrad(
            isOn ? juce::Colour(metalDark) : juce::Colour(panelDark),
            bounds.getCentreX(), bounds.getY(),
            isOn ? juce::Colour(panelDark) : juce::Colour(panelDark).darker(0.2f),
            bounds.getCentreX(), bounds.getBottom(),
            false);
        g.setGradientFill(buttonGrad);
        g.fillRoundedRectangle(bounds, 5.0f);

        g.setColour(isOn ? juce::Colour(metalMid) : juce::Colour(metalDark));
        g.drawRoundedRectangle(bounds, 5.0f, 1.5f);

        // LED
        auto ledSize = bounds.getHeight() * 0.35f;
        auto ledBounds = juce::Rectangle<float>(bounds.getX() + 8,
                                                 bounds.getCentreY() - ledSize / 2,
                                                 ledSize, ledSize);
        drawLED(g, ledBounds, isOn, ledAmberOff, ledAmberOn, ledAmberGlow);

        // Text
        auto textBounds = bounds.withTrimmedLeft(ledSize + 16);
        g.setColour(isOn ? juce::Colour(textPrimary) : juce::Colour(textSecondary));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(buttonText, textBounds, juce::Justification::centred);
    }
}

//==============================================================================
// Vintage Combo Box
//==============================================================================
void TapeMachineLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                           int buttonX, int buttonY, int buttonW, int buttonH,
                                           juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height);
    auto cornerSize = 4.0f;

    // Background with subtle gradient
    juce::ColourGradient bgGrad(
        juce::Colour(panelLight), 0, 0,
        juce::Colour(panelDark), 0, (float)height,
        false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds, cornerSize);

    // Beveled border
    g.setColour(juce::Colour(metalDark));
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerSize, 1.5f);

    // Inner highlight (top edge)
    g.setColour(juce::Colour(0x18ffffff));
    g.drawHorizontalLine(2, 4, width - 4);

    // Arrow area background
    auto arrowBounds = juce::Rectangle<float>((float)buttonX, (float)buttonY,
                                               (float)buttonW, (float)buttonH);
    g.setColour(juce::Colour(metalDark).withAlpha(0.5f));
    g.fillRoundedRectangle(arrowBounds.reduced(2), 2.0f);

    // Draw arrow
    juce::Path arrow;
    auto arrowSize = 8.0f;
    auto arrowX = arrowBounds.getCentreX();
    auto arrowY = arrowBounds.getCentreY();

    arrow.startNewSubPath(arrowX - arrowSize * 0.5f, arrowY - arrowSize * 0.25f);
    arrow.lineTo(arrowX, arrowY + arrowSize * 0.25f);
    arrow.lineTo(arrowX + arrowSize * 0.5f, arrowY - arrowSize * 0.25f);

    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(arrow, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void TapeMachineLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                bool isSeparator, bool isActive, bool isHighlighted,
                                                bool isTicked, bool hasSubMenu,
                                                const juce::String& text, const juce::String& shortcutKeyText,
                                                const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        auto r = area.reduced(5, 0).toFloat();
        g.setColour(juce::Colour(metalDark));
        g.fillRect(r.removeFromTop(r.getHeight() / 2).withHeight(1.0f));
        return;
    }

    auto r = area.reduced(1);

    if (isHighlighted && isActive)
    {
        g.setColour(juce::Colour(metalDark));
        g.fillRect(r);
    }

    auto textColor = isActive ? (isHighlighted ? juce::Colour(metalHighlight) : juce::Colour(textPrimary))
                              : juce::Colour(textSecondary);

    g.setColour(textColor);
    g.setFont(getPopupMenuFont());

    auto textArea = r.reduced(8, 0);
    g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

    if (isTicked)
    {
        auto tickArea = r.removeFromRight(r.getHeight());
        g.setColour(juce::Colour(ledAmberOn));
        g.fillEllipse(tickArea.reduced(8).toFloat());
    }
}

juce::Font TapeMachineLookAndFeel::getComboBoxFont(juce::ComboBox& /*box*/)
{
    return juce::Font(14.0f);
}

juce::Font TapeMachineLookAndFeel::getPopupMenuFont()
{
    return juce::Font(14.0f);
}

//==============================================================================
// Engraved Label
//==============================================================================
void TapeMachineLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    auto bounds = label.getLocalBounds().toFloat();
    auto textColour = label.findColour(juce::Label::textColourId);
    auto font = label.getFont();

    g.setFont(font);

    // Engraved shadow (darker, offset down-right)
    g.setColour(juce::Colour(0x80000000));
    g.drawText(label.getText(), bounds.translated(1.0f, 1.0f),
               label.getJustificationType(), true);

    // Subtle highlight (up-left, for embossed effect)
    g.setColour(juce::Colour(0x10ffffff));
    g.drawText(label.getText(), bounds.translated(-0.5f, -0.5f),
               label.getJustificationType(), true);

    // Main text
    g.setColour(textColour);
    g.drawText(label.getText(), bounds, label.getJustificationType(), true);
}

//==============================================================================
// Text Editor Styling
//==============================================================================
void TapeMachineLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                                       juce::TextEditor& textEditor)
{
    g.setColour(juce::Colour(panelDark));
    g.fillRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, 3.0f);
}

void TapeMachineLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height,
                                                    juce::TextEditor& textEditor)
{
    g.setColour(juce::Colour(metalDark));
    g.drawRoundedRectangle(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f, 3.0f, 1.0f);
}

//==============================================================================
// Static Helper Functions
//==============================================================================

void TapeMachineLookAndFeel::drawLED(juce::Graphics& g, juce::Rectangle<float> bounds,
                                      bool isOn, uint32_t offColor, uint32_t onColor, uint32_t glowColor)
{
    // Glow when on
    if (isOn)
    {
        g.setColour(juce::Colour(glowColor));
        g.fillEllipse(bounds.expanded(3));
    }

    // LED body
    juce::ColourGradient ledGrad(
        isOn ? juce::Colour(onColor).brighter(0.3f) : juce::Colour(offColor).brighter(0.1f),
        bounds.getX(), bounds.getY(),
        isOn ? juce::Colour(onColor) : juce::Colour(offColor),
        bounds.getRight(), bounds.getBottom(),
        false);
    g.setGradientFill(ledGrad);
    g.fillEllipse(bounds);

    // Bezel ring
    g.setColour(juce::Colour(0xff1a1510));
    g.drawEllipse(bounds, 1.0f);

    // Highlight spot when on
    if (isOn)
    {
        auto spotBounds = bounds.reduced(bounds.getWidth() * 0.3f);
        spotBounds = spotBounds.withPosition(bounds.getX() + 2, bounds.getY() + 2);
        g.setColour(juce::Colour(0x60ffffff));
        g.fillEllipse(spotBounds);
    }
}

void TapeMachineLookAndFeel::drawBrushedMetal(juce::Graphics& g, juce::Rectangle<float> bounds,
                                               bool isVertical)
{
    // Base metal color
    g.setColour(juce::Colour(chrome));
    g.fillRect(bounds);

    // Subtle brush strokes
    juce::Random rng(42);  // Deterministic for consistent look
    g.setColour(juce::Colour(0x08000000));

    if (isVertical)
    {
        for (int i = 0; i < (int)bounds.getWidth(); i += 2)
        {
            if (rng.nextFloat() < 0.7f)
            {
                float x = bounds.getX() + i + rng.nextFloat() * 1.5f;
                g.drawVerticalLine((int)x, bounds.getY(), bounds.getBottom());
            }
        }
    }
    else
    {
        for (int i = 0; i < (int)bounds.getHeight(); i += 2)
        {
            if (rng.nextFloat() < 0.7f)
            {
                float y = bounds.getY() + i + rng.nextFloat() * 1.5f;
                g.drawHorizontalLine((int)y, bounds.getX(), bounds.getRight());
            }
        }
    }
}

void TapeMachineLookAndFeel::drawBeveledPanel(juce::Graphics& g, juce::Rectangle<float> bounds,
                                               float cornerSize, float bevelWidth)
{
    // Main panel
    g.setColour(juce::Colour(panelDark));
    g.fillRoundedRectangle(bounds, cornerSize);

    // Top/left highlight (light source from top-left)
    g.setColour(juce::Colour(0x15ffffff));
    g.drawRoundedRectangle(bounds.reduced(1), cornerSize, bevelWidth);

    // Bottom/right shadow
    auto shadowBounds = bounds.reduced(bevelWidth);
    g.setColour(juce::Colour(0x20000000));
    g.drawRoundedRectangle(shadowBounds, cornerSize - 1, bevelWidth);

    // Outer border
    g.setColour(juce::Colour(metalDark));
    g.drawRoundedRectangle(bounds, cornerSize, 1.5f);
}

void TapeMachineLookAndFeel::drawScrew(juce::Graphics& g, float cx, float cy, float radius)
{
    // Screw head body
    juce::ColourGradient screwGrad(
        juce::Colour(chrome), cx - radius * 0.5f, cy - radius * 0.5f,
        juce::Colour(metalDark), cx + radius * 0.5f, cy + radius * 0.5f,
        false);
    g.setGradientFill(screwGrad);
    g.fillEllipse(cx - radius, cy - radius, radius * 2, radius * 2);

    // Screw slot
    g.setColour(juce::Colour(0xff0a0a08));
    g.drawLine(cx - radius * 0.6f, cy, cx + radius * 0.6f, cy, 2.0f);

    // Border
    g.setColour(juce::Colour(0xff1a1510));
    g.drawEllipse(cx - radius, cy - radius, radius * 2, radius * 2, 1.0f);
}

void TapeMachineLookAndFeel::drawNameplate(juce::Graphics& g, juce::Rectangle<float> bounds,
                                            const juce::String& text, float fontSize)
{
    // Plate background
    juce::ColourGradient plateGrad(
        juce::Colour(metalLight), bounds.getX(), bounds.getY(),
        juce::Colour(metalMid), bounds.getX(), bounds.getBottom(),
        false);
    g.setGradientFill(plateGrad);
    g.fillRoundedRectangle(bounds, 3.0f);

    // Border
    g.setColour(juce::Colour(metalDark));
    g.drawRoundedRectangle(bounds, 3.0f, 1.5f);

    // Embossed text
    g.setFont(juce::Font(fontSize, juce::Font::bold));

    // Shadow
    g.setColour(juce::Colour(0x80000000));
    g.drawText(text, bounds.translated(1, 1), juce::Justification::centred);

    // Highlight
    g.setColour(juce::Colour(0x40ffffff));
    g.drawText(text, bounds.translated(-0.5f, -0.5f), juce::Justification::centred);

    // Main text
    g.setColour(juce::Colour(panelDark));
    g.drawText(text, bounds, juce::Justification::centred);
}

void TapeMachineLookAndFeel::drawRackEars(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    float earWidth = 20.0f;

    // Left ear
    auto leftEar = juce::Rectangle<float>(0, 0, earWidth, (float)bounds.getHeight());
    drawBrushedMetal(g, leftEar, true);

    // Right ear
    auto rightEar = juce::Rectangle<float>((float)bounds.getWidth() - earWidth, 0,
                                            earWidth, (float)bounds.getHeight());
    drawBrushedMetal(g, rightEar, true);

    // Screw holes on ears
    float screwRadius = 4.0f;
    float screwMargin = 15.0f;

    // Left ear screws
    drawScrew(g, earWidth / 2, screwMargin, screwRadius);
    drawScrew(g, earWidth / 2, bounds.getHeight() - screwMargin, screwRadius);

    // Right ear screws
    drawScrew(g, bounds.getWidth() - earWidth / 2, screwMargin, screwRadius);
    drawScrew(g, bounds.getWidth() - earWidth / 2, bounds.getHeight() - screwMargin, screwRadius);
}

void TapeMachineLookAndFeel::createKnobCache(int size)
{
    // Reserved for future film-strip knob caching
    cachedKnobSize = size;
}

//==============================================================================
// Premium Reel Renderer
//==============================================================================

void PremiumReelRenderer::drawReel(juce::Graphics& g, juce::Rectangle<float> bounds,
                                    float rotation, float tapeAmount, bool isSupplyReel)
{
    auto centre = bounds.getCentre();
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.45f;

    // Calculate tape radius
    float minTapeRatio = 0.28f;
    float maxTapeRatio = 0.88f;
    float tapeRadius = radius * (minTapeRatio + tapeAmount * (maxTapeRatio - minTapeRatio));

    // Draw components in order (back to front)
    drawFlangeWithReflections(g, centre, radius, rotation);

    if (tapeAmount > 0.05f)
    {
        drawTapePack(g, centre, radius * 0.24f, tapeRadius);
    }

    drawSpokes(g, centre, radius * 0.24f, tapeRadius, rotation);
    drawHub(g, centre, radius * 0.24f);
}

void PremiumReelRenderer::drawFlangeWithReflections(juce::Graphics& g, juce::Point<float> centre,
                                                     float radius, float rotation)
{
    // Outer shadow
    g.setColour(juce::Colour(0x80000000));
    g.fillEllipse(centre.x - radius + 4, centre.y - radius + 4, radius * 2, radius * 2);

    // Main flange with metallic gradient
    juce::ColourGradient flangeGrad(
        juce::Colour(TapeMachineColors::chrome), centre.x - radius * 0.7f, centre.y - radius * 0.7f,
        juce::Colour(TapeMachineColors::metalDark), centre.x + radius * 0.7f, centre.y + radius * 0.7f,
        true);
    g.setGradientFill(flangeGrad);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

    // Reflective highlight arc (rotating with reel for dynamic look)
    juce::Path highlightArc;
    float arcStart = rotation - 0.5f;
    float arcEnd = rotation + 0.5f;
    highlightArc.addArc(centre.x - radius * 0.92f, centre.y - radius * 0.92f,
                        radius * 1.84f, radius * 1.84f,
                        arcStart, arcEnd, true);
    g.setColour(juce::Colour(0x30ffffff));
    g.strokePath(highlightArc, juce::PathStrokeType(4.0f));

    // Inner flange edge ring
    g.setColour(juce::Colour(TapeMachineColors::metalDark));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 2.5f);

    // Inner edge highlight
    float innerEdge = radius * 0.95f;
    g.setColour(juce::Colour(0x18ffffff));
    g.drawEllipse(centre.x - innerEdge, centre.y - innerEdge, innerEdge * 2, innerEdge * 2, 1.0f);
}

void PremiumReelRenderer::drawTapePack(juce::Graphics& g, juce::Point<float> centre,
                                        float innerRadius, float outerRadius)
{
    // Tape shadow for depth
    g.setColour(juce::Colour(0xff080606));
    g.fillEllipse(centre.x - outerRadius - 1, centre.y - outerRadius + 2,
                  outerRadius * 2 + 2, outerRadius * 2);

    // Main tape pack with subtle radial gradient
    juce::ColourGradient tapeGrad(
        juce::Colour(0xff2a2420), centre.x, centre.y,
        juce::Colour(0xff181410), centre.x, centre.y - outerRadius,
        true);
    g.setGradientFill(tapeGrad);
    g.fillEllipse(centre.x - outerRadius, centre.y - outerRadius,
                  outerRadius * 2, outerRadius * 2);

    // Tape edge highlight (shiny oxide surface)
    g.setColour(juce::Colour(0x25ffffff));
    g.drawEllipse(centre.x - outerRadius + 2, centre.y - outerRadius + 2,
                  outerRadius * 2 - 4, outerRadius * 2 - 4, 1.5f);

    // Inner tape edge (near hub)
    g.setColour(juce::Colour(0xff0a0808));
    g.drawEllipse(centre.x - innerRadius * 1.1f, centre.y - innerRadius * 1.1f,
                  innerRadius * 2.2f, innerRadius * 2.2f, 1.0f);
}

void PremiumReelRenderer::drawSpokes(juce::Graphics& g, juce::Point<float> centre,
                                      float innerRadius, float outerRadius, float rotation)
{
    // 3 spokes at 120-degree intervals
    for (int i = 0; i < 3; ++i)
    {
        float spokeAngle = rotation + (i * 2.0f * juce::MathConstants<float>::pi / 3.0f);

        // Spoke dimensions
        float spokeLength = outerRadius * 0.92f;
        float spokeWidth = 10.0f;

        // Create spoke path
        juce::Path spoke;
        spoke.addRoundedRectangle(-spokeLength, -spokeWidth / 2, spokeLength * 2, spokeWidth, 3.0f);
        spoke.applyTransform(juce::AffineTransform::rotation(spokeAngle).translated(centre.x, centre.y));

        // Clip to donut shape (between hub and flange, excluding tape/hub area)
        g.saveState();

        juce::Path clipPath;
        // Outer boundary: slightly larger than the flange
        clipPath.addEllipse(centre.x - outerRadius * 1.05f, centre.y - outerRadius * 1.05f,
                           outerRadius * 2.1f, outerRadius * 2.1f);

        // Inner boundary: exclude the tape pack or hub area using even-odd rule
        clipPath.setUsingNonZeroWinding(false);
        // Use innerRadius to create the donut hole (hub/tape area to exclude)
        float excludeRadius = juce::jmax(innerRadius * 1.1f, outerRadius * 0.95f);
        clipPath.addEllipse(centre.x - excludeRadius, centre.y - excludeRadius,
                           excludeRadius * 2, excludeRadius * 2);

        g.reduceClipRegion(clipPath);

        // Spoke with metallic gradient
        juce::ColourGradient spokeGrad(
            juce::Colour(TapeMachineColors::metalMid), centre.x, centre.y - spokeWidth,
            juce::Colour(TapeMachineColors::metalDark), centre.x, centre.y + spokeWidth,
            false);
        g.setGradientFill(spokeGrad);
        g.fillPath(spoke);

        // Spoke outline
        g.setColour(juce::Colour(0xff2a2520));
        g.strokePath(spoke, juce::PathStrokeType(1.0f));

        g.restoreState();
    }
}

void PremiumReelRenderer::drawHub(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    // Hub shadow
    g.setColour(juce::Colour(0x40000000));
    g.fillEllipse(centre.x - radius + 2, centre.y - radius + 2, radius * 2, radius * 2);

    // Hub body with metallic finish
    juce::ColourGradient hubGrad(
        juce::Colour(TapeMachineColors::chrome), centre.x - radius * 0.5f, centre.y - radius * 0.5f,
        juce::Colour(TapeMachineColors::metalMid), centre.x + radius * 0.5f, centre.y + radius * 0.5f,
        false);
    g.setGradientFill(hubGrad);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

    // Hub ring detail
    g.setColour(juce::Colour(TapeMachineColors::metalDark));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 2.0f);

    // Center spindle hole
    float holeRadius = radius * 0.4f;
    g.setColour(juce::Colour(0xff080808));
    g.fillEllipse(centre.x - holeRadius, centre.y - holeRadius, holeRadius * 2, holeRadius * 2);

    // Spindle highlight
    g.setColour(juce::Colour(0x25ffffff));
    g.fillEllipse(centre.x - holeRadius + 2, centre.y - holeRadius + 2,
                  holeRadius * 0.8f, holeRadius * 0.6f);

    // Hub highlight
    g.setColour(juce::Colour(0x20ffffff));
    g.fillEllipse(centre.x - radius * 0.5f, centre.y - radius * 0.6f,
                  radius * 0.6f, radius * 0.4f);
}

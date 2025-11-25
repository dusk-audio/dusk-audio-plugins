#include "FourKLookAndFeel.h"
#include <cmath>

FourKLookAndFeel::FourKLookAndFeel()
{
    // Professional console colors
    knobColour = juce::Colour(0xff5a5a5a);          // Medium grey knob body
    backgroundColour = juce::Colour(0xff2a2a2a);    // Dark console background
    outlineColour = juce::Colour(0xff808080);       // Light grey for outlines
    textColour = juce::Colour(0xffe0e0e0);          // Off-white text
    highlightColour = juce::Colour(0xff007bff);     // Professional blue

    // Set component colors
    setColour(juce::Slider::thumbColourId, knobColour);
    setColour(juce::Slider::rotarySliderFillColourId, highlightColour);
    setColour(juce::Slider::rotarySliderOutlineColourId, outlineColour);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff404040));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffff3030));
    setColour(juce::TextButton::textColourOffId, textColour);
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));
    setColour(juce::ComboBox::backgroundColourId, backgroundColour);
    setColour(juce::ComboBox::textColourId, textColour);
    setColour(juce::ComboBox::outlineColourId, outlineColour);
}

void FourKLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider)
{
    auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
    auto centreX = x + width * 0.5f;
    auto centreY = y + height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Check if mouse is over or dragging
    bool isMouseOver = slider.isMouseOverOrDragging();
    bool isDragging = slider.isMouseButtonDown();

    // Outer shadow/glow
    {
        juce::ColourGradient shadowGradient(
            juce::Colour(0x40000000), centreX, centreY,
            juce::Colour(0x00000000), centreX, centreY + radius + 6, true);
        g.setGradientFill(shadowGradient);
        g.fillEllipse(rx - 4, ry - 2, rw + 8, rw + 10);
    }

    // Black outer ring
    g.setColour(juce::Colour(0xff0a0a0a));
    g.fillEllipse(rx - 2, ry - 2, rw + 4, rw + 4);

    // Dark metallic knob body with improved 3D gradient (light from top-left)
    {
        juce::ColourGradient knobGradient(
            juce::Colour(0xff606060), centreX - radius * 0.7f, centreY - radius * 0.7f,
            juce::Colour(0xff181818), centreX + radius * 0.5f, centreY + radius * 0.7f, true);
        g.setGradientFill(knobGradient);
        g.fillEllipse(rx, ry, rw, rw);
    }

    // Inner highlight ring (simulates 3D bevel)
    {
        g.setColour(juce::Colour(0x20ffffff));
        g.drawEllipse(rx + 1, ry + 1, rw - 2, rw - 2, 1.0f);
    }

    // Draw radial ridges/grooves for texture
    g.setColour(juce::Colour(0x18000000));
    for (int i = 0; i < 24; ++i)
    {
        float ridgeAngle = (i / 24.0f) * juce::MathConstants<float>::twoPi;
        auto x1 = centreX + radius * 0.6f * std::cos(ridgeAngle);
        auto y1 = centreY + radius * 0.6f * std::sin(ridgeAngle);
        auto x2 = centreX + radius * 0.95f * std::cos(ridgeAngle);
        auto y2 = centreY + radius * 0.95f * std::sin(ridgeAngle);
        g.drawLine(x1, y1, x2, y2, 0.5f);
    }

    // Determine color for center cap based on parameter name
    juce::Colour capColour;
    auto paramName = slider.getName().toLowerCase();
    if (paramName.contains("hf") || paramName.contains("high"))
        capColour = juce::Colour(0xff4a7c9e); // Blue
    else if (paramName.contains("hmf") || paramName.contains("hi-mid"))
        capColour = juce::Colour(0xff5c9a5c); // Green
    else if (paramName.contains("lmf") || paramName.contains("lo-mid"))
        capColour = juce::Colour(0xffc47a44); // Orange
    else if (paramName.contains("lf") || paramName.contains("low"))
        capColour = juce::Colour(0xffc44444); // Red
    else
        capColour = juce::Colour(0xff5a5a5a); // Grey default

    // Hover state: brighten the cap
    if (isMouseOver && !isDragging)
        capColour = capColour.brighter(0.15f);

    // Colored center cap with improved 3D gradient
    auto capRadius = radius * 0.52f;
    {
        // Cap shadow
        g.setColour(juce::Colour(0x40000000));
        g.fillEllipse(centreX - capRadius + 1, centreY - capRadius + 2, capRadius * 2, capRadius * 2);

        // Main cap with gradient
        juce::ColourGradient capGradient(
            capColour.brighter(0.4f), centreX - capRadius * 0.4f, centreY - capRadius * 0.5f,
            capColour.darker(0.35f), centreX + capRadius * 0.3f, centreY + capRadius * 0.5f, true);
        g.setGradientFill(capGradient);
        g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

        // Cap highlight arc
        g.setColour(capColour.brighter(0.6f).withAlpha(0.3f));
        juce::Path highlightArc;
        highlightArc.addArc(centreX - capRadius + 2, centreY - capRadius + 2,
                           (capRadius - 2) * 2, (capRadius - 2) * 2,
                           juce::MathConstants<float>::pi * 1.2f,
                           juce::MathConstants<float>::pi * 1.8f, true);
        g.strokePath(highlightArc, juce::PathStrokeType(1.5f));
    }

    // Dragging state: colored ring around the knob
    if (isDragging)
    {
        g.setColour(capColour.withAlpha(0.4f));
        g.drawEllipse(rx - 3, ry - 3, rw + 6, rw + 6, 2.0f);
    }

    // White pointer line on the colored cap (brighter and slightly thicker)
    {
        juce::Path pointer;
        auto pointerLength = capRadius * 0.85f;
        auto pointerThickness = 2.5f;

        pointer.addRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength * 0.9f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        // Pointer shadow
        g.setColour(juce::Colour(0x40000000));
        g.fillPath(pointer, juce::AffineTransform::translation(0.5f, 1.0f));

        // Main pointer
        g.setColour(juce::Colour(0xffffffff));
        g.fillPath(pointer);
    }

    // Small center dot with subtle highlight
    g.setColour(juce::Colour(0xff151515));
    g.fillEllipse(centreX - 2.5f, centreY - 2.5f, 5, 5);
    g.setColour(juce::Colour(0x30ffffff));
    g.fillEllipse(centreX - 1.5f, centreY - 2.0f, 2, 2);
}

void FourKLookAndFeel::drawScaleMarkings(juce::Graphics& g, float cx, float cy, float radius,
                                       float startAngle, float endAngle)
{
    // Draw 11 main tick marks (0-10) with better visibility
    for (int i = 0; i <= 10; ++i)
    {
        auto tickAngle = startAngle + (i / 10.0f) * (endAngle - startAngle);
        auto tickStartRadius = radius + 3;
        auto tickEndRadius = radius + 8;

        auto startX = cx + tickStartRadius * std::cos(tickAngle);
        auto startY = cy + tickStartRadius * std::sin(tickAngle);
        auto endX = cx + tickEndRadius * std::cos(tickAngle);
        auto endY = cy + tickEndRadius * std::sin(tickAngle);

        // Draw black shadow for contrast
        g.setColour(juce::Colour(0xff000000));
        g.drawLine(startX + 0.5f, startY + 0.5f, endX + 0.5f, endY + 0.5f, 1.5f);

        // Draw bright white tick mark on top (thicker for visibility)
        g.setColour(juce::Colour(0xffffffff));
        g.drawLine(startX, startY, endX, endY, 1.5f);
    }

    // Draw scale numbers at key positions with enhanced visibility
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));

    auto drawValueWithShadow = [&g](const juce::String& text, float x, float y, int w, int h)
    {
        // Draw dark shadow/outline for contrast
        g.setColour(juce::Colour(0xff000000));
        g.drawText(text, x - 10 + 1, y - 5 + 1, w, h, juce::Justification::centred);

        // Draw bright white text on top
        g.setColour(juce::Colour(0xffffffff));
        g.drawText(text, x - 10, y - 5, w, h, juce::Justification::centred);
    };

    // Min value (7 o'clock)
    auto minX = cx + (radius + 18) * std::cos(startAngle);
    auto minY = cy + (radius + 18) * std::sin(startAngle);
    drawValueWithShadow("0", minX, minY, 20, 12);

    // Center value (12 o'clock)
    auto centerAngle = (startAngle + endAngle) * 0.5f;
    auto centerX = cx + (radius + 18) * std::cos(centerAngle);
    auto centerY = cy + (radius + 18) * std::sin(centerAngle);
    drawValueWithShadow("5", centerX, centerY, 20, 12);

    // Max value (5 o'clock)
    auto maxX = cx + (radius + 18) * std::cos(endAngle);
    auto maxY = cy + (radius + 18) * std::sin(endAngle);
    drawValueWithShadow("10", maxX, maxY, 20, 12);
}

void FourKLookAndFeel::drawValueReadout(juce::Graphics& g, juce::Slider& slider,
                                      int x, int y, int width, int height)
{
    // Draw wider digital readout box to prevent ellipsis
    auto boxWidth = width * 0.8f;  // Increased from 0.5f to 0.8f
    auto boxX = x + (width - boxWidth) * 0.5f;

    g.setColour(juce::Colour(0xff101010));
    g.fillRoundedRectangle(boxX, y, boxWidth, height, 2.0f);

    g.setColour(juce::Colour(0xff303030));
    g.drawRoundedRectangle(boxX, y, boxWidth, height, 2.0f, 0.5f);

    // Format and display value in white with slightly smaller text to fit better
    g.setColour(juce::Colour(0xffffffff));  // White text
    g.setFont(juce::Font(juce::FontOptions(16.0f)));  // Increased for better visibility

    juce::String text;
    auto value = slider.getValue();
    auto suffix = slider.getTextValueSuffix();

    if (suffix.contains("Hz"))
    {
        if (value >= 1000.0f)
            text = juce::String(value / 1000.0f, 1) + "kHz";
        else
            text = juce::String((int)value) + "Hz";
    }
    else if (suffix.contains("dB"))
    {
        text = (value >= 0 ? "+" : "") + juce::String(value, 1) + "dB";
    }
    else if (suffix.contains("%"))
    {
        text = juce::String((int)value) + "%";
    }
    else
    {
        text = juce::String(value, 2);
    }

    g.drawText(text, boxX, y, boxWidth, height,
               juce::Justification::centred);
}

void FourKLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float minSliderPos, float maxSliderPos,
                                      const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearVertical)
    {
        // SSL-style vertical fader
        auto trackWidth = 8.0f;
        auto trackX = x + width * 0.5f - trackWidth * 0.5f;

        // Track background
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillRoundedRectangle(trackX, y, trackWidth, height, 2.0f);

        // Track groove
        g.setColour(juce::Colour(0xff0a0a0a));
        g.fillRoundedRectangle(trackX + 2, y, trackWidth - 4, height, 1.0f);

        // Fill
        auto fillHeight = y + height - sliderPos;
        g.setColour(highlightColour.withAlpha(0.7f));
        g.fillRoundedRectangle(trackX + 2, sliderPos, trackWidth - 4, fillHeight, 1.0f);

        // Fader cap (SSL-style)
        auto thumbWidth = 24.0f;
        auto thumbHeight = 12.0f;
        auto thumbX = x + width * 0.5f - thumbWidth * 0.5f;
        auto thumbY = sliderPos - thumbHeight * 0.5f;

        // Thumb shadow
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(thumbX + 1, thumbY + 1, thumbWidth, thumbHeight, 2.0f);

        // Thumb body
        juce::ColourGradient thumbGradient(
            juce::Colour(0xff808080), thumbX, thumbY,
            juce::Colour(0xff404040), thumbX, thumbY + thumbHeight, false);
        g.setGradientFill(thumbGradient);
        g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 2.0f);

        // Thumb line indicator
        g.setColour(juce::Colour(0xffffffff));
        g.fillRect(thumbX + thumbWidth * 0.5f - 1, thumbY + 2, 2.0f, thumbHeight - 4);
    }
    else
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                        minSliderPos, maxSliderPos, style, slider);
    }
}

void FourKLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                      bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    auto isOn = button.getToggleState();
    auto buttonText = button.getButtonText();
    bool isBypass = buttonText.containsIgnoreCase("BYPASS");
    bool isAutoGain = buttonText.containsIgnoreCase("AUTO");

    // SSL-style illuminated push button with improved styling

    // Outer bezel/shadow
    g.setColour(juce::Colour(0xff151515));
    g.fillRoundedRectangle(bounds.expanded(1), 4.0f);

    // Button bezel
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Button face with gradient
    juce::Colour baseColor;
    if (isBypass)
    {
        // BYPASS uses orange/amber when active for high visibility
        baseColor = isOn ? juce::Colour(0xff8a5020) : juce::Colour(0xff454545);
    }
    else if (isAutoGain)
    {
        // AUTO GAIN uses green when active
        baseColor = isOn ? juce::Colour(0xff2a6a2a) : juce::Colour(0xff404040);
    }
    else
    {
        // Default red for other toggle buttons
        baseColor = isOn ? juce::Colour(0xff6a2020) : juce::Colour(0xff404040);
    }

    juce::ColourGradient buttonGradient(
        baseColor.brighter(0.2f), bounds.getX(), bounds.getY(),
        baseColor.darker(0.3f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(buttonGradient);
    g.fillRoundedRectangle(bounds.reduced(2), 3.0f);

    // Highlight effect when hovered
    if (shouldDrawButtonAsHighlighted && !shouldDrawButtonAsDown)
    {
        g.setColour(juce::Colour(0x15ffffff));
        g.fillRoundedRectangle(bounds.reduced(2), 3.0f);
    }

    // Pressed effect
    if (shouldDrawButtonAsDown)
    {
        g.setColour(juce::Colour(0x20000000));
        g.fillRoundedRectangle(bounds.reduced(2), 3.0f);
    }

    // Inner highlight (top edge)
    g.setColour(juce::Colour(0x15ffffff));
    g.drawLine(bounds.getX() + 4, bounds.getY() + 3, bounds.getRight() - 4, bounds.getY() + 3, 1.0f);

    // LED indicator strip at top of button when on
    if (isOn)
    {
        juce::Colour ledColor;
        if (isBypass)
            ledColor = juce::Colour(0xffff8000);  // Orange
        else if (isAutoGain)
            ledColor = juce::Colour(0xff00cc00);  // Green
        else
            ledColor = juce::Colour(0xffff3030);  // Red

        // LED strip
        auto ledRect = juce::Rectangle<float>(bounds.getX() + 4, bounds.getY() + 2,
                                               bounds.getWidth() - 8, 3.0f);
        g.setColour(ledColor);
        g.fillRoundedRectangle(ledRect, 1.0f);

        // Glow effect
        g.setColour(ledColor.withAlpha(0.3f));
        g.fillRoundedRectangle(ledRect.expanded(2, 1), 2.0f);
    }

    // Border
    g.setColour(isOn ? baseColor.brighter(0.3f) : juce::Colour(0xff505050));
    g.drawRoundedRectangle(bounds.reduced(1), 3.0f, 1.0f);

    // Button text
    g.setColour(isOn ? juce::Colours::white : juce::Colour(0xffc0c0c0));
    g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));

    // Offset text down slightly to account for LED strip
    auto textBounds = bounds;
    if (isOn) textBounds.translate(0, 1);

    g.drawFittedText(buttonText, textBounds.toNearestInt(),
                    juce::Justification::centred, 1);
}

void FourKLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                  int buttonX, int buttonY, int buttonW, int buttonH,
                                  juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));

    // SSL-style selector with improved styling

    // Outer shadow
    g.setColour(juce::Colour(0xff151515));
    g.fillRoundedRectangle(bounds.expanded(1), 5.0f);

    // Main background with gradient
    juce::ColourGradient bgGradient(
        juce::Colour(0xff3a3a3a), 0, 0,
        juce::Colour(0xff2a2a2a), 0, static_cast<float>(height), false);
    g.setGradientFill(bgGradient);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Inner highlight at top
    g.setColour(juce::Colour(0x10ffffff));
    g.drawLine(4, 2, static_cast<float>(width) - 4, 2, 1.0f);

    // Border
    g.setColour(juce::Colour(0xff4a4a4a));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Pressed state
    if (isButtonDown)
    {
        g.setColour(juce::Colour(0x15000000));
        g.fillRoundedRectangle(bounds.reduced(1), 3.0f);
    }

    // Highlight if focused
    if (box.hasKeyboardFocus(false))
    {
        g.setColour(highlightColour.withAlpha(0.25f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.5f);
    }

    // Arrow button separator line
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawLine(static_cast<float>(buttonX), 4.0f, static_cast<float>(buttonX), static_cast<float>(height) - 4.0f, 1.0f);

    // Custom arrow with shadow
    float arrowCenterX = buttonX + buttonW * 0.5f;
    float arrowCenterY = buttonY + buttonH * 0.5f;
    float arrowSize = 5.0f;

    juce::Path arrow;
    arrow.addTriangle(arrowCenterX - arrowSize, arrowCenterY - arrowSize * 0.3f,
                     arrowCenterX + arrowSize, arrowCenterY - arrowSize * 0.3f,
                     arrowCenterX, arrowCenterY + arrowSize * 0.6f);

    // Arrow shadow
    g.setColour(juce::Colour(0x40000000));
    g.fillPath(arrow, juce::AffineTransform::translation(0.5f, 1.0f));

    // Arrow
    g.setColour(juce::Colour(0xffc0c0c0));
    g.fillPath(arrow);
}

juce::Font FourKLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions(16.0f).withStyle("Bold"));  // Larger for better readability
}

juce::Font FourKLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions(10.0f).withStyle("Bold"));
}

void FourKLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    auto bounds = label.getLocalBounds().toFloat();

    // SSL-style label text
    g.setColour(textColour);
    g.setFont(getLabelFont(label));
    g.drawFittedText(label.getText(), bounds.toNearestInt(),
                    label.getJustificationType(), 1);
}
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

    // SSL-style knob matching reference image

    // Black outer ring/shadow
    g.setColour(juce::Colour(0xff000000));
    g.fillEllipse(rx - 2, ry - 2, rw + 4, rw + 4);

    // Dark metallic knob body with ridges effect
    juce::ColourGradient knobGradient(
        juce::Colour(0xff4a4a4a), centreX - radius * 0.5f, centreY - radius * 0.5f,
        juce::Colour(0xff1a1a1a), centreX + radius * 0.5f, centreY + radius * 0.5f, false);
    g.setGradientFill(knobGradient);
    g.fillEllipse(rx, ry, rw, rw);

    // Draw radial ridges/grooves for texture
    g.setColour(juce::Colour(0x20000000));
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
        capColour = juce::Colour(0xff5a9a5a); // Green
    else if (paramName.contains("lmf") || paramName.contains("lo-mid"))
        capColour = juce::Colour(0xff9a6a3a); // Brown/Orange
    else if (paramName.contains("lf") || paramName.contains("low"))
        capColour = juce::Colour(0xff8a4a4a); // Red
    else
        capColour = juce::Colour(0xff5a5a5a); // Grey default

    // Colored center cap with gradient
    auto capRadius = radius * 0.5f;
    juce::ColourGradient capGradient(
        capColour.brighter(0.3f), centreX - capRadius * 0.3f, centreY - capRadius * 0.3f,
        capColour.darker(0.3f), centreX + capRadius * 0.3f, centreY + capRadius * 0.3f, false);
    g.setGradientFill(capGradient);
    g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

    // White pointer line on the colored cap
    juce::Path pointer;
    auto pointerLength = capRadius * 0.8f;
    auto pointerThickness = 2.0f;

    pointer.addRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength * 0.9f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(juce::Colour(0xffffffff));
    g.fillPath(pointer);

    // Small center dot
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillEllipse(centreX - 2, centreY - 2, 4, 4);
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

    // SSL-style illuminated push button

    // Button bezel
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRoundedRectangle(bounds, 3.0f);

    // Button face
    juce::ColourGradient buttonGradient(
        isOn ? juce::Colour(0xff602020) : juce::Colour(0xff404040),
        bounds.getX(), bounds.getY(),
        isOn ? juce::Colour(0xff301010) : juce::Colour(0xff202020),
        bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(buttonGradient);
    g.fillRoundedRectangle(bounds.reduced(2), 2.0f);

    // LED indicator (SSL-style)
    if (isOn)
    {
        // Red illumination when on
        g.setColour(juce::Colour(0xffff0000));
        auto ledBounds = bounds.reduced(bounds.getWidth() * 0.3f, bounds.getHeight() * 0.3f);
        g.fillEllipse(ledBounds);

        // Glow effect
        g.setColour(juce::Colour(0x40ff0000));
        g.fillEllipse(ledBounds.expanded(3));
    }

    // Button text
    g.setColour(isOn ? juce::Colours::white : textColour);
    g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
    g.drawFittedText(button.getButtonText(), bounds.toNearestInt(),
                    juce::Justification::centred, 1);
}

void FourKLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                  int buttonX, int buttonY, int buttonW, int buttonH,
                                  juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0, width, height);

    // SSL-style selector switch
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRoundedRectangle(bounds, 3.0f);

    // Inner face
    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillRoundedRectangle(bounds.reduced(2), 2.0f);

    // Highlight if focused
    if (box.hasKeyboardFocus(false))
    {
        g.setColour(highlightColour.withAlpha(0.3f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.5f);
    }

    // Down arrow
    juce::Path arrow;
    arrow.addTriangle(buttonX + buttonW * 0.3f, buttonY + buttonH * 0.45f,
                     buttonX + buttonW * 0.7f, buttonY + buttonH * 0.45f,
                     buttonX + buttonW * 0.5f, buttonY + buttonH * 0.6f);
    g.setColour(textColour);
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
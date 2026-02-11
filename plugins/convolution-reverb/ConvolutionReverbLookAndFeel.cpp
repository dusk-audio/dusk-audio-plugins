/*
  ==============================================================================

    Convolution Reverb - Custom Look and Feel
    Dusk Audio visual styling
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#include "ConvolutionReverbLookAndFeel.h"
#include <cmath>

ConvolutionReverbLookAndFeel::ConvolutionReverbLookAndFeel()
{
    // Set default colors
    setColour(juce::Slider::backgroundColourId, knobColour);
    setColour(juce::Slider::thumbColourId, accentColour);
    setColour(juce::Slider::trackColourId, accentColour);
    setColour(juce::Slider::rotarySliderFillColourId, accentColour);
    setColour(juce::Slider::rotarySliderOutlineColourId, knobColour);
    setColour(juce::Slider::textBoxTextColourId, textColour);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour(juce::Label::textColourId, textColour);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::buttonColourId, panelColour);
    setColour(juce::TextButton::textColourOffId, textColour);
    setColour(juce::TextButton::textColourOnId, accentColour);

    setColour(juce::ToggleButton::textColourId, textColour);
    setColour(juce::ToggleButton::tickColourId, accentColour);

    setColour(juce::ComboBox::backgroundColourId, panelColour);
    setColour(juce::ComboBox::textColourId, textColour);
    setColour(juce::ComboBox::arrowColourId, textColour);
    setColour(juce::ComboBox::outlineColourId, gridColour);

    setColour(juce::PopupMenu::backgroundColourId, panelColour);
    setColour(juce::PopupMenu::textColourId, textColour);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentColour.withAlpha(0.3f));
    setColour(juce::PopupMenu::highlightedTextColourId, textColour);

    setColour(juce::TreeView::backgroundColourId, backgroundColour);
    setColour(juce::TreeView::linesColourId, gridColour);
    setColour(juce::TreeView::selectedItemBackgroundColourId, accentColour.withAlpha(0.3f));

    setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, accentColour.withAlpha(0.3f));
    setColour(juce::DirectoryContentsDisplayComponent::textColourId, textColour);

    setColour(juce::ScrollBar::thumbColourId, gridColour);
    setColour(juce::ScrollBar::trackColourId, backgroundColour);
}

void ConvolutionReverbLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                                     float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                                     juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(8);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto lineW = juce::jmin(2.5f, radius * 0.15f);  // Reduced arc stroke width (was 6.0f)
    auto arcRadius = radius - lineW * 0.5f - 2.0f;  // Slightly inset arc

    auto centreX = bounds.getCentreX();
    auto centreY = bounds.getCentreY();

    // Shadow
    g.setColour(shadowColour.withAlpha(0.5f));
    g.fillEllipse(centreX - radius + 2, centreY - radius + 2, radius * 2.0f, radius * 2.0f);

    // Background circle with gradient
    juce::ColourGradient grad(
        knobColour.brighter(0.15f), centreX, bounds.getY(),
        knobColour.darker(0.2f), centreX, bounds.getBottom(),
        false
    );
    g.setGradientFill(grad);
    g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

    // Outer ring
    g.setColour(juce::Colour(0xff4a4a4a));
    g.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 2.0f);

    // Inner highlight ring
    g.setColour(juce::Colour(0xff5a5a5a));
    g.drawEllipse(centreX - radius + 3, centreY - radius + 3, (radius - 3) * 2.0f, (radius - 3) * 2.0f, 1.0f);

    // Draw radial ridges for texture
    g.setColour(juce::Colour(0xff505050));
    int numRidges = 24;
    for (int i = 0; i < numRidges; ++i)
    {
        float angle = static_cast<float>(i) / numRidges * juce::MathConstants<float>::twoPi;
        float innerR = radius * 0.55f;
        float outerR = radius * 0.85f;

        float x1 = centreX + innerR * std::cos(angle);
        float y1 = centreY + innerR * std::sin(angle);
        float x2 = centreX + outerR * std::cos(angle);
        float y2 = centreY + outerR * std::sin(angle);

        g.drawLine(x1, y1, x2, y2, 0.5f);
    }

    // Track arc (background) - thinner stroke
    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff353535));
    g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

    // Value arc - thinner stroke
    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f,
                               rotaryStartAngle, toAngle, true);
        g.setColour(accentColour);
        g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        // ========== HOT POINT INDICATOR ==========
        // Bright dot at the current value position on the arc
        float hotPointX = centreX + arcRadius * std::sin(toAngle);
        float hotPointY = centreY - arcRadius * std::cos(toAngle);
        float hotPointRadius = lineW * 1.8f;  // Slightly larger than arc width

        // Glow behind hot point
        g.setColour(accentColour.withAlpha(0.4f));
        g.fillEllipse(hotPointX - hotPointRadius * 1.5f, hotPointY - hotPointRadius * 1.5f,
                      hotPointRadius * 3.0f, hotPointRadius * 3.0f);

        // Main hot point
        g.setColour(accentColour.brighter(0.3f));
        g.fillEllipse(hotPointX - hotPointRadius, hotPointY - hotPointRadius,
                      hotPointRadius * 2.0f, hotPointRadius * 2.0f);

        // Bright center highlight
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.fillEllipse(hotPointX - hotPointRadius * 0.4f, hotPointY - hotPointRadius * 0.4f,
                      hotPointRadius * 0.8f, hotPointRadius * 0.8f);
    }

    // Center cap
    float capRadius = radius * 0.35f;
    juce::ColourGradient capGrad(
        juce::Colour(0xff5a5a5a), centreX, centreY - capRadius,
        juce::Colour(0xff3a3a3a), centreX, centreY + capRadius,
        false
    );
    g.setGradientFill(capGrad);
    g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2.0f, capRadius * 2.0f);

    // Pointer line
    juce::Path pointer;
    auto pointerLength = radius * 0.65f;
    auto pointerThickness = 2.5f;

    pointer.addRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength * 0.6f);
    pointer.applyTransform(juce::AffineTransform::rotation(toAngle).translated(centreX, centreY));

    g.setColour(textColour);
    g.fillPath(pointer);

    // Center dot
    g.setColour(accentColour);
    g.fillEllipse(centreX - 3, centreY - 3, 6, 6);
}

void ConvolutionReverbLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                                     float sliderPos, float minSliderPos, float maxSliderPos,
                                                     juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::ignoreUnused(minSliderPos, maxSliderPos);

    auto trackWidth = 6.0f;

    if (style == juce::Slider::LinearVertical)
    {
        auto trackX = static_cast<float>(x) + static_cast<float>(width) * 0.5f - trackWidth * 0.5f;

        // Track background
        g.setColour(juce::Colour(0xff303030));
        g.fillRoundedRectangle(trackX, static_cast<float>(y), trackWidth, static_cast<float>(height), 3.0f);

        // Value fill
        g.setColour(accentColour);
        float fillHeight = static_cast<float>(height) - sliderPos + static_cast<float>(y);
        g.fillRoundedRectangle(trackX, sliderPos, trackWidth, fillHeight, 3.0f);

        // Thumb
        auto thumbWidth = 24.0f;
        auto thumbHeight = 12.0f;
        auto thumbX = static_cast<float>(x) + static_cast<float>(width) * 0.5f - thumbWidth * 0.5f;
        auto thumbY = sliderPos - thumbHeight * 0.5f;

        // Thumb shadow
        g.setColour(shadowColour.withAlpha(0.5f));
        g.fillRoundedRectangle(thumbX + 1, thumbY + 1, thumbWidth, thumbHeight, 4.0f);

        // Thumb body
        juce::ColourGradient thumbGrad(
            juce::Colour(0xff606060), thumbX, thumbY,
            juce::Colour(0xff404040), thumbX, thumbY + thumbHeight,
            false
        );
        g.setGradientFill(thumbGrad);
        g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f);

        // Thumb highlight
        g.setColour(juce::Colour(0xff707070));
        g.drawRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f, 1.0f);

        // Center line
        g.setColour(textColour);
        g.drawLine(thumbX + 4, thumbY + thumbHeight * 0.5f,
                   thumbX + thumbWidth - 4, thumbY + thumbHeight * 0.5f, 1.5f);
    }
    else
    {
        // Horizontal slider
        auto trackY = static_cast<float>(y) + static_cast<float>(height) * 0.5f - trackWidth * 0.5f;

        // Track background
        g.setColour(juce::Colour(0xff303030));
        g.fillRoundedRectangle(static_cast<float>(x), trackY, static_cast<float>(width), trackWidth, 3.0f);

        // Value fill
        g.setColour(accentColour);
        g.fillRoundedRectangle(static_cast<float>(x), trackY, sliderPos - static_cast<float>(x), trackWidth, 3.0f);

        // Thumb
        auto thumbWidth = 12.0f;
        auto thumbHeight = 24.0f;
        auto thumbX = sliderPos - thumbWidth * 0.5f;
        auto thumbY = static_cast<float>(y) + static_cast<float>(height) * 0.5f - thumbHeight * 0.5f;

        // Thumb shadow
        g.setColour(shadowColour.withAlpha(0.5f));
        g.fillRoundedRectangle(thumbX + 1, thumbY + 1, thumbWidth, thumbHeight, 4.0f);

        // Thumb body
        juce::ColourGradient thumbGrad(
            juce::Colour(0xff606060), thumbX, thumbY,
            juce::Colour(0xff404040), thumbX + thumbWidth, thumbY,
            false
        );
        g.setGradientFill(thumbGrad);
        g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f);

        // Thumb highlight
        g.setColour(juce::Colour(0xff707070));
        g.drawRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 4.0f, 1.0f);

        // Center line
        g.setColour(textColour);
        g.drawLine(thumbX + thumbWidth * 0.5f, thumbY + 4,
                   thumbX + thumbWidth * 0.5f, thumbY + thumbHeight - 4, 1.5f);
    }
}

void ConvolutionReverbLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                                     bool shouldDrawButtonAsHighlighted,
                                                     bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsDown);

    // Unified button dimensions: consistent 26px height feel, 4px corner radius
    auto bounds = button.getLocalBounds().toFloat().reduced(1);
    bool isOn = button.getToggleState();
    float cornerRadius = 4.0f;  // Consistent corner radius across all buttons

    // OFF state: subtle border, very dark transparent fill
    // ON state: accent blue fill with white/bright text
    if (isOn)
    {
        // ON STATE - filled with accent blue, subtle gradient for depth
        juce::ColourGradient bgGrad(
            accentColour.withAlpha(0.85f), bounds.getX(), bounds.getY(),
            accentColour.withAlpha(0.65f), bounds.getX(), bounds.getBottom(),
            false
        );
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Subtle inner highlight at top
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        auto highlightLine = bounds.reduced(cornerRadius, 0).removeFromTop(1.0f);
        highlightLine.translate(0, 1);
        g.fillRoundedRectangle(highlightLine, 0.5f);

        // Border in matching accent colour
        g.setColour(accentColour.brighter(0.2f));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }
    else
    {
        // OFF STATE - transparent/very dark fill with subtle border
        g.setColour(juce::Colour(0x18FFFFFF));  // Very subtle white overlay
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Subtle dim border
        g.setColour(juce::Colour(0xff404040));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }

    // Hover highlight - slightly brighter on hover
    if (shouldDrawButtonAsHighlighted)
    {
        g.setColour(juce::Colours::white.withAlpha(isOn ? 0.1f : 0.06f));
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Brighter border on hover
        g.setColour(isOn ? accentColour.brighter(0.4f) : juce::Colour(0xff505050));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }

    // Text - bright/white when ON, dim when OFF
    g.setColour(isOn ? juce::Colours::white : dimTextColour);
    g.setFont(juce::Font(9.5f, juce::Font::bold));
    g.drawText(button.getButtonText(), button.getLocalBounds().reduced(4), juce::Justification::centred);
}

void ConvolutionReverbLookAndFeel::drawTreeviewPlusMinusBox(juce::Graphics& g, const juce::Rectangle<float>& area,
                                                            juce::Colour backgroundColour, bool isOpen,
                                                            bool isMouseOver)
{
    juce::ignoreUnused(backgroundColour);

    auto boxSize = juce::jmin(area.getWidth(), area.getHeight()) * 0.7f;
    auto boxBounds = area.withSizeKeepingCentre(boxSize, boxSize);

    g.setColour(isMouseOver ? accentColour : dimTextColour);
    g.drawRoundedRectangle(boxBounds, 2.0f, 1.0f);

    // Draw plus/minus
    auto lineLength = boxSize * 0.6f;
    auto centreX = boxBounds.getCentreX();
    auto centreY = boxBounds.getCentreY();

    g.drawLine(centreX - lineLength * 0.5f, centreY,
               centreX + lineLength * 0.5f, centreY, 1.5f);

    if (!isOpen)
    {
        g.drawLine(centreX, centreY - lineLength * 0.5f,
                   centreX, centreY + lineLength * 0.5f, 1.5f);
    }
}

void ConvolutionReverbLookAndFeel::drawFileBrowserRow(juce::Graphics& g, int width, int height,
                                                       const juce::File& file, const juce::String& filename,
                                                       juce::Image* icon, const juce::String& fileSizeDescription,
                                                       const juce::String& fileTimeDescription,
                                                       bool isDirectory, bool isItemSelected,
                                                       int itemIndex, juce::DirectoryContentsDisplayComponent& component)
{
    juce::ignoreUnused(file, icon, fileSizeDescription, fileTimeDescription, itemIndex, component);

    auto bounds = juce::Rectangle<int>(0, 0, width, height);

    // Selection background with accent bar on left
    if (isItemSelected)
    {
        // Accent bar on left edge
        g.setColour(accentColour);
        g.fillRect(0, 0, 3, height);

        // Selection highlight background
        g.setColour(accentColour.withAlpha(0.2f));
        g.fillRect(bounds.withTrimmedLeft(3));
    }

    // Icon area
    auto iconBounds = bounds.removeFromLeft(height).reduced(4);

    if (isDirectory)
    {
        // Folder icon with improved styling
        g.setColour(isItemSelected ? accentColour.brighter(0.2f) : accentColour);
        auto folderBounds = iconBounds.toFloat().reduced(2);

        juce::Path folderPath;
        // Main folder body
        folderPath.addRoundedRectangle(folderBounds.getX(), folderBounds.getY() + folderBounds.getHeight() * 0.25f,
                                        folderBounds.getWidth(), folderBounds.getHeight() * 0.75f, 2.0f);
        // Tab
        folderPath.addRoundedRectangle(folderBounds.getX(), folderBounds.getY(),
                                        folderBounds.getWidth() * 0.45f, folderBounds.getHeight() * 0.3f, 1.0f);
        g.fillPath(folderPath);

        // Subtle highlight on folder
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        auto highlightRect = folderBounds.reduced(2, 0);
        highlightRect.removeFromBottom(highlightRect.getHeight() * 0.6f);
        highlightRect.translate(0, folderBounds.getHeight() * 0.25f);
        g.fillRect(highlightRect);
    }
    else
    {
        // Audio file icon
        g.setColour(isItemSelected ? waveformColour.brighter(0.2f) : waveformColour);
        auto iconCenter = iconBounds.getCentre().toFloat();
        auto iconRadius = juce::jmin(iconBounds.getWidth(), iconBounds.getHeight()) * 0.35f;

        juce::Path wavePath;
        for (int i = 0; i < 5; ++i)
        {
            float x = iconCenter.x - iconRadius + i * (iconRadius * 0.4f);
            float h = iconRadius * (0.3f + 0.7f * std::sin(i * 1.2f));
            wavePath.addRectangle(x, iconCenter.y - h, iconRadius * 0.25f, h * 2.0f);
        }
        g.fillPath(wavePath);
    }

    // Filename with brighter text when selected
    g.setColour(isItemSelected ? textColour : (isDirectory ? accentColour.withAlpha(0.9f) : dimTextColour.brighter(0.3f)));
    g.setFont(juce::Font(11.5f, isDirectory ? juce::Font::bold : juce::Font::plain));
    g.drawText(filename, bounds.reduced(4, 0), juce::Justification::centredLeft);
}

void ConvolutionReverbLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    if (!label.isBeingEdited())
    {
        auto textArea = label.getBorderSize().subtractedFrom(label.getLocalBounds());

        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(label.getFont());
        g.drawText(label.getText(), textArea, label.getJustificationType(), true);
    }
}

void ConvolutionReverbLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                                 int buttonX, int buttonY, int buttonW, int buttonH,
                                                 juce::ComboBox& box)
{
    juce::ignoreUnused(buttonX, buttonY, buttonW, buttonH);

    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

    // Background
    g.setColour(isButtonDown ? panelColour.brighter(0.1f) : panelColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Border
    g.setColour(gridColour);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Arrow
    auto arrowZone = bounds.removeFromRight(height).reduced(8);
    juce::Path arrow;
    arrow.addTriangle(arrowZone.getX(), arrowZone.getCentreY() - 3,
                      arrowZone.getRight(), arrowZone.getCentreY() - 3,
                      arrowZone.getCentreX(), arrowZone.getCentreY() + 3);

    g.setColour(box.isEnabled() ? textColour : dimTextColour);
    g.fillPath(arrow);
}

void ConvolutionReverbLookAndFeel::drawMetallicKnob(juce::Graphics& g, float x, float y, float diameter,
                                                     float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                                     const juce::Colour& baseColour)
{
    juce::ignoreUnused(sliderPos, rotaryStartAngle, rotaryEndAngle, baseColour);

    auto radius = diameter * 0.5f;
    auto centreX = x + radius;
    auto centreY = y + radius;

    // Main body gradient
    juce::ColourGradient grad(
        juce::Colour(0xff5a5a5a), centreX, y,
        juce::Colour(0xff3a3a3a), centreX, y + diameter,
        false
    );
    g.setGradientFill(grad);
    g.fillEllipse(x, y, diameter, diameter);

    // Rim
    g.setColour(juce::Colour(0xff2a2a2a));
    g.drawEllipse(x, y, diameter, diameter, 2.0f);
}

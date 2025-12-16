#pragma once

#include <JuceHeader.h>
#include "EQBand.h"

//==============================================================================
/**
    Custom Look and Feel for Multi-Q Plugin

    Dark theme with color-coded band controls matching Logic Pro Channel EQ style
*/
class MultiQLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MultiQLookAndFeel()
    {
        // Dark color scheme
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xFF1a1a1a));
        setColour(juce::Label::textColourId, juce::Colour(0xFFCCCCCC));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xFFCCCCCC));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF2a2a2a));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xFF444444));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF2a2a2a));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xFFCCCCCC));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF444444));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xFF2a2a2a));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xFFCCCCCC));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xFF444444));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2a2a2a));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFCCCCCC));
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto lineW = juce::jmin(4.0f, radius * 0.15f);
        auto arcRadius = radius - lineW * 0.5f;

        auto centre = bounds.getCentre();

        // Get custom color from slider if set
        // Use a default blue, but allow it to be overridden via the colour scheme
        juce::Colour trackColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
        if (trackColour == juce::Colour())
            trackColour = juce::Colour(0xFF4488ff);
        // Background track
        juce::Path backgroundArc;
        backgroundArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                    0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xFF333333));
        g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));

        // Value arc
        if (slider.isEnabled())
        {
            juce::Path valueArc;

            // For centered sliders (like gain), draw from center
            if (slider.getMinimum() < 0 && slider.getMaximum() > 0)
            {
                float centerAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);
                if (toAngle > centerAngle)
                    valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                           0.0f, centerAngle, toAngle, true);
                else
                    valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                           0.0f, toAngle, centerAngle, true);
            }
            else
            {
                valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                       0.0f, rotaryStartAngle, toAngle, true);
            }

            g.setColour(trackColour);
            g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }

        // Knob body
        float knobRadius = radius * 0.65f;
        g.setColour(juce::Colour(0xFF3a3a3a));
        g.fillEllipse(centre.x - knobRadius, centre.y - knobRadius,
                      knobRadius * 2.0f, knobRadius * 2.0f);

        // Knob highlight
        g.setColour(juce::Colour(0xFF4a4a4a));
        g.drawEllipse(centre.x - knobRadius, centre.y - knobRadius,
                      knobRadius * 2.0f, knobRadius * 2.0f, 1.0f);

        // Pointer
        juce::Path pointer;
        auto pointerLength = knobRadius * 0.8f;
        auto pointerThickness = lineW * 0.8f;
        pointer.addRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength);
        pointer.applyTransform(juce::AffineTransform::rotation(toAngle).translated(centre));

        g.setColour(trackColour);
        g.fillPath(pointer);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
        auto isOn = button.getToggleState();

        // Get custom color from button if set
        juce::Colour onColour = button.findColour(juce::ToggleButton::tickColourId);
        if (onColour == juce::Colour())
            onColour = juce::Colour(0xFF4488ff);

        // Background - use darker shade when on for better text contrast
        juce::Colour bgColour = isOn ? onColour.darker(0.3f) : juce::Colour(0xFF2a2a2a);
        g.setColour(bgColour);
        g.fillRoundedRectangle(bounds, 4.0f);

        // Border - use accent color when on
        if (isOn)
            g.setColour(onColour.brighter(0.2f));
        else
            g.setColour(shouldDrawButtonAsHighlighted ? juce::Colour(0xFF666666) : juce::Colour(0xFF444444));
        g.drawRoundedRectangle(bounds, 4.0f, 1.5f);

        // Text - high contrast for both states
        g.setColour(isOn ? juce::Colours::white : juce::Colour(0xFFAAAAAA));
        g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        g.drawText(button.getButtonText(), bounds, juce::Justification::centred);

        juce::ignoreUnused(shouldDrawButtonAsDown);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                     int buttonX, int buttonY, int buttonW, int buttonH,
                     juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        // Arrow
        auto arrowZone = juce::Rectangle<int>(buttonX, buttonY, buttonW, buttonH).toFloat().reduced(8.0f);
        juce::Path arrow;
        arrow.addTriangle(arrowZone.getX(), arrowZone.getCentreY() - 2.0f,
                         arrowZone.getCentreX(), arrowZone.getCentreY() + 4.0f,
                         arrowZone.getRight(), arrowZone.getCentreY() - 2.0f);
        g.setColour(box.findColour(juce::ComboBox::textColourId));
        g.fillPath(arrow);

        juce::ignoreUnused(isButtonDown);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));

        if (!label.isBeingEdited())
        {
            auto alpha = label.isEnabled() ? 1.0f : 0.5f;
            auto font = getLabelFont(label);

            g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
            g.setFont(font);

            auto textArea = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());

            g.drawFittedText(label.getText(), textArea, label.getJustificationType(),
                            juce::jmax(1, static_cast<int>(textArea.getHeight() / font.getHeight())),
                            label.getMinimumHorizontalScale());

            g.setColour(label.findColour(juce::Label::outlineColourId).withMultipliedAlpha(alpha));
        }
        else if (label.isEnabled())
        {
            g.setColour(label.findColour(juce::Label::outlineColourId));
        }

        g.drawRect(label.getLocalBounds());
    }

    juce::Font getLabelFont(juce::Label& label) override
    {
        return juce::Font(juce::FontOptions(label.getFont().getHeight()));
    }
};

//==============================================================================
/**
    Band Enable Button with color indicator and filter type icon
*/
class BandEnableButton : public juce::ToggleButton
{
public:
    BandEnableButton(int bandIndex)
        : bandNum(bandIndex)
    {
        if (bandIndex >= 0 && bandIndex < 8)
        {
            const auto& config = DefaultBandConfigs[static_cast<size_t>(bandIndex)];
            bandColor = config.color;
            filterType = config.type;
        }
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/) override
    {
        auto buttonBounds = getLocalBounds().toFloat().reduced(2.0f);
        bool toggleState = getToggleState();

        // Button background
        g.setColour(toggleState ? bandColor : juce::Colour(0xFF2a2a2a));
        g.fillRoundedRectangle(buttonBounds, 4.0f);

        // Button border
        g.setColour(shouldDrawButtonAsHighlighted ? bandColor.brighter() : bandColor.darker());
        g.drawRoundedRectangle(buttonBounds, 4.0f, toggleState ? 2.0f : 1.0f);

        // Draw filter type icon inside button (high contrast)
        drawFilterTypeIcon(g, buttonBounds.reduced(5.0f), toggleState);
    }

private:
    void drawFilterTypeIcon(juce::Graphics& g, juce::Rectangle<float> bounds, bool isEnabled)
    {
        // High contrast: white when enabled (on colored background), gray when disabled
        g.setColour(isEnabled ? juce::Colours::white : juce::Colour(0xFF666666));

        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float w = bounds.getWidth() * 0.4f;  // Slightly smaller for better fit
        float h = bounds.getHeight() * 0.4f;
        float strokeWidth = isEnabled ? 2.0f : 1.5f;  // Thicker lines for visibility

        juce::Path path;

        switch (filterType)
        {
            case BandType::HighPass:
                // Simple angled line sloping down (left to right, high on right)
                path.startNewSubPath(cx - w, cy + h * 0.5f);
                path.lineTo(cx + w, cy - h * 0.5f);
                g.strokePath(path, juce::PathStrokeType(strokeWidth));
                break;

            case BandType::LowShelf:
                // Low shelf shape (step down on left)
                path.startNewSubPath(cx - w, cy - h * 0.4f);
                path.lineTo(cx - w * 0.3f, cy - h * 0.4f);
                path.lineTo(cx + w * 0.3f, cy + h * 0.4f);
                path.lineTo(cx + w, cy + h * 0.4f);
                g.strokePath(path, juce::PathStrokeType(strokeWidth));
                break;

            case BandType::Parametric:
                // Diamond shape (peak/bell)
                path.startNewSubPath(cx, cy - h * 0.7f);
                path.lineTo(cx + w * 0.7f, cy);
                path.lineTo(cx, cy + h * 0.7f);
                path.lineTo(cx - w * 0.7f, cy);
                path.closeSubPath();
                g.strokePath(path, juce::PathStrokeType(strokeWidth));
                break;

            case BandType::HighShelf:
                // High shelf shape (step up on right)
                path.startNewSubPath(cx - w, cy + h * 0.4f);
                path.lineTo(cx - w * 0.3f, cy + h * 0.4f);
                path.lineTo(cx + w * 0.3f, cy - h * 0.4f);
                path.lineTo(cx + w, cy - h * 0.4f);
                g.strokePath(path, juce::PathStrokeType(strokeWidth));
                break;

            case BandType::LowPass:
                // Simple angled line sloping down (left to right, low on right)
                path.startNewSubPath(cx - w, cy - h * 0.5f);
                path.lineTo(cx + w, cy + h * 0.5f);
                g.strokePath(path, juce::PathStrokeType(strokeWidth));
                break;

            default:
                // Fallback: draw a simple circle for unknown types
                g.drawEllipse(bounds.reduced(2.0f), strokeWidth);
                break;
        }    }

    int bandNum;
    juce::Colour bandColor = juce::Colours::grey;
    BandType filterType = BandType::Parametric;
};

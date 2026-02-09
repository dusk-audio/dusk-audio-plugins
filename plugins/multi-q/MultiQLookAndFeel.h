#pragma once

#include <JuceHeader.h>
#include "EQBand.h"

//==============================================================================
/**
    Custom Look and Feel for Multi-Q Plugin

    Dark theme with color-coded band controls matching Logic Pro Channel EQ style
    Modernized control panel with refined knobs, value readouts, and toggles
*/
class MultiQLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Selected band color (can be updated dynamically)
    juce::Colour selectedBandColor = juce::Colour(0xFF4488ff);

    MultiQLookAndFeel()
    {
        // Dark color scheme - refined
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xFF181818));
        setColour(juce::Label::textColourId, juce::Colour(0xFFb0b0b0));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xFFd0d0d0));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF222224));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xFF3a3a3e));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF222224));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xFFd0d0d0));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF3a3a3e));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xFF252528));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xFFd0d0d0));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xFF3a3a40));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF252528));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFa0a0a0));
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void setSelectedBandColor(juce::Colour color)
    {
        selectedBandColor = color;
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        // ===== MODERN ARC-STYLE KNOB (FabFilter/Kilohearts style) =====
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        auto diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        auto radius = diameter / 2.0f;
        auto centre = bounds.getCentre();

        // Interaction state detection
        bool isHovered = slider.isMouseOverOrDragging();
        bool isDragging = slider.isMouseButtonDown();
        bool isEnabled = slider.isEnabled();

        // Get arc color from slider (use selected band color as default)
        juce::Colour arcColor = slider.findColour(juce::Slider::rotarySliderFillColourId);
        if (arcColor == juce::Colour())
            arcColor = selectedBandColor;

        // Brighten on hover/drag
        juce::Colour arcColorFinal = isDragging ? arcColor.brighter(0.15f) :
                                     (isHovered ? arcColor.brighter(0.08f) : arcColor);

        // Arc dimensions
        float arcThickness = juce::jmax(3.5f, radius * 0.12f);
        float trackThickness = juce::jmax(1.5f, arcThickness * 0.4f);
        float arcRadius = radius - arcThickness * 0.5f - 2.0f;

        // Calculate current angle
        float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // ===== LAYER 1: Background Circle with Inner Shadow =====
        // Outer shadow
        g.setColour(juce::Colour(0x30000000));
        g.fillEllipse(centre.x - radius + 1, centre.y - radius + 1,
                      diameter - 2, diameter - 2);

        // Main background circle with gradient (inner shadow effect)
        {
            float bgRadius = radius - 1.0f;
            juce::ColourGradient bgGradient(
                juce::Colour(0xFF1a1a1c), centre.x, centre.y - bgRadius,  // Darker top
                juce::Colour(0xFF262628), centre.x, centre.y + bgRadius,  // Lighter bottom
                false);
            g.setGradientFill(bgGradient);
            g.fillEllipse(centre.x - bgRadius, centre.y - bgRadius,
                          bgRadius * 2.0f, bgRadius * 2.0f);
        }

        // Subtle inner shadow ring
        g.setColour(juce::Colour(0x20000000));
        g.drawEllipse(centre.x - radius + 2, centre.y - radius + 2,
                      (radius - 2) * 2.0f, (radius - 2) * 2.0f, 1.5f);

        // ===== LAYER 2: Track Ring (inactive arc) =====
        {
            juce::Path trackArc;
            trackArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                   0.0f, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(juce::Colour(0x663a3a3e));  // ~40% opacity
            g.strokePath(trackArc, juce::PathStrokeType(trackThickness,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ===== LAYER 3: Value Arc with Glow =====
        if (isEnabled)
        {
            juce::Path valueArc;

            // Check if this is a bipolar slider (like gain with range -dB to +dB)
            bool isBipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;

            if (isBipolar)
            {
                // Bipolar: draw from center outward in both directions
                float centerAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);
                if (toAngle > centerAngle)
                    valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                           0.0f, centerAngle, toAngle, true);
                else
                    valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                           0.0f, toAngle, centerAngle, true);

                // Draw center tick mark for bipolar
                float tickRadius = arcRadius - arcThickness;
                float tickX = centre.x + tickRadius * std::sin(centerAngle);
                float tickY = centre.y - tickRadius * std::cos(centerAngle);
                g.setColour(juce::Colour(0x60ffffff));
                g.fillEllipse(tickX - 1.5f, tickY - 1.5f, 3.0f, 3.0f);
            }
            else
            {
                // Unipolar: draw from start to current
                valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                                       0.0f, rotaryStartAngle, toAngle, true);
            }

            // Glow layers (more prominent when hovered/dragging)
            float glowAlpha = isDragging ? 0.5f : (isHovered ? 0.35f : 0.2f);

            // Outer glow
            g.setColour(arcColorFinal.withAlpha(glowAlpha * 0.5f));
            g.strokePath(valueArc, juce::PathStrokeType(arcThickness + 8.0f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Middle glow
            g.setColour(arcColorFinal.withAlpha(glowAlpha));
            g.strokePath(valueArc, juce::PathStrokeType(arcThickness + 4.0f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Main value arc
            g.setColour(arcColorFinal);
            g.strokePath(valueArc, juce::PathStrokeType(arcThickness,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ===== LAYER 4: Center Cap (metallic look) =====
        float capRadius = radius * 0.42f;
        {
            // Cap shadow
            g.setColour(juce::Colour(0x40000000));
            g.fillEllipse(centre.x - capRadius + 1, centre.y - capRadius + 1,
                          capRadius * 2.0f, capRadius * 2.0f);

            // Cap body with gradient
            juce::ColourGradient capGradient(
                juce::Colour(0xFF3a3a3e), centre.x, centre.y - capRadius,
                juce::Colour(0xFF242428), centre.x, centre.y + capRadius,
                false);
            g.setGradientFill(capGradient);
            g.fillEllipse(centre.x - capRadius, centre.y - capRadius,
                          capRadius * 2.0f, capRadius * 2.0f);

            // Cap highlight (top reflection)
            juce::Path capHighlight;
            capHighlight.addCentredArc(centre.x, centre.y, capRadius * 0.8f, capRadius * 0.8f,
                                       0.0f, -juce::MathConstants<float>::pi * 0.5f,
                                       juce::MathConstants<float>::pi * 0.5f, true);
            g.setColour(juce::Colour(0x15ffffff));
            g.strokePath(capHighlight, juce::PathStrokeType(1.5f));

            // Cap edge
            g.setColour(juce::Colour(0xFF1e1e20));
            g.drawEllipse(centre.x - capRadius, centre.y - capRadius,
                          capRadius * 2.0f, capRadius * 2.0f, 0.75f);
        }

        // ===== LAYER 5: Position Indicator (Dot at arc end) =====
        if (isEnabled)
        {
            float dotRadius = juce::jmax(3.0f, arcThickness * 0.6f);
            float dotDist = arcRadius;  // Place dot on the arc
            float dotX = centre.x + dotDist * std::sin(toAngle);
            float dotY = centre.y - dotDist * std::cos(toAngle);

            // Dot glow (more prominent when active)
            float dotGlowAlpha = isDragging ? 0.6f : (isHovered ? 0.4f : 0.25f);
            g.setColour(juce::Colours::white.withAlpha(dotGlowAlpha));
            g.fillEllipse(dotX - dotRadius * 1.8f, dotY - dotRadius * 1.8f,
                          dotRadius * 3.6f, dotRadius * 3.6f);

            // Main dot
            g.setColour(juce::Colours::white);
            g.fillEllipse(dotX - dotRadius, dotY - dotRadius,
                          dotRadius * 2.0f, dotRadius * 2.0f);
        }

        // ===== Disabled state overlay =====
        if (!isEnabled)
        {
            g.setColour(juce::Colour(0x80000000));
            g.fillEllipse(centre.x - radius, centre.y - radius,
                          radius * 2.0f, radius * 2.0f);
        }
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
        auto isOn = button.getToggleState();

        // Get custom color from button if set
        juce::Colour onColour = button.findColour(juce::ToggleButton::tickColourId);
        if (onColour == juce::Colour())
            onColour = selectedBandColor;

        // Background with subtle gradient
        if (isOn)
        {
            // Active state - gradient fill with soft glow
            juce::ColourGradient bgGradient(
                onColour.darker(0.15f), bounds.getX(), bounds.getY(),
                onColour.darker(0.35f), bounds.getX(), bounds.getBottom(),
                false);
            g.setGradientFill(bgGradient);
            g.fillRoundedRectangle(bounds, 5.0f);

            // Soft outer glow
            g.setColour(onColour.withAlpha(0.2f));
            g.drawRoundedRectangle(bounds.expanded(1.0f), 6.0f, 2.0f);

            // Inner highlight
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.drawHorizontalLine(static_cast<int>(bounds.getY() + 2),
                                 bounds.getX() + 4, bounds.getRight() - 4);
        }
        else if (shouldDrawButtonAsHighlighted)
        {
            // Hover state
            g.setColour(juce::Colour(0xFF2e2e32));
            g.fillRoundedRectangle(bounds, 5.0f);
            g.setColour(juce::Colour(0xFF4a4a50));
            g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
        }
        else
        {
            // Default state
            g.setColour(juce::Colour(0xFF252528));
            g.fillRoundedRectangle(bounds, 5.0f);
            g.setColour(juce::Colour(0xFF3a3a3e));
            g.drawRoundedRectangle(bounds, 5.0f, 0.75f);
        }

        // Border for active state
        if (isOn)
        {
            g.setColour(onColour.brighter(0.1f));
            g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
        }

        // Text
        float fontSize = bounds.getHeight() > 28 ? 11.0f : 10.0f;
        g.setFont(juce::Font(juce::FontOptions(fontSize).withStyle(isOn ? "Bold" : "Regular")));
        g.setColour(isOn ? juce::Colours::white : juce::Colour(0xFF9a9a9a));
        g.drawText(button.getButtonText(), bounds, juce::Justification::centred);

        juce::ignoreUnused(shouldDrawButtonAsDown);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                     int buttonX, int buttonY, int buttonW, int buttonH,
                     juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
        bool isHovered = box.isMouseOver();

        // Background with subtle gradient
        juce::ColourGradient bgGradient(
            juce::Colour(0xFF2a2a2e), 0, 0,
            juce::Colour(0xFF242428), 0, static_cast<float>(height),
            false);
        g.setGradientFill(bgGradient);
        g.fillRoundedRectangle(bounds, 5.0f);

        // Border - brighter on hover
        g.setColour(isHovered ? juce::Colour(0xFF4a4a50) : juce::Colour(0xFF3a3a3e));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, isButtonDown ? 1.5f : 0.75f);

        // Arrow indicator (chevron style)
        auto arrowZone = juce::Rectangle<int>(buttonX, buttonY, buttonW, buttonH).toFloat().reduced(10.0f, 8.0f);
        float arrowSize = juce::jmin(arrowZone.getWidth(), arrowZone.getHeight()) * 0.5f;
        float cx = arrowZone.getCentreX();
        float cy = arrowZone.getCentreY();

        juce::Path arrow;
        arrow.startNewSubPath(cx - arrowSize, cy - arrowSize * 0.3f);
        arrow.lineTo(cx, cy + arrowSize * 0.4f);
        arrow.lineTo(cx + arrowSize, cy - arrowSize * 0.3f);

        g.setColour(isHovered ? juce::Colour(0xFFa0a0a0) : juce::Colour(0xFF707070));
        g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                     juce::PathStrokeType::rounded));
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        auto bounds = label.getLocalBounds().toFloat();

        // Check if this is a slider value label (has numeric content)
        bool isValueLabel = label.getText().containsAnyOf("0123456789.-+");

        if (isValueLabel)
        {
            // Value readout with subtle rounded pill background
            g.setColour(juce::Colour(0xFF1e1e20));
            g.fillRoundedRectangle(bounds.reduced(1.0f), 3.0f);

            // Subtle inset border
            g.setColour(juce::Colour(0xFF2a2a2e));
            g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 0.5f);
        }
        else
        {
            g.fillAll(label.findColour(juce::Label::backgroundColourId));
        }

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
        }
    }

    juce::Font getLabelFont(juce::Label& label) override
    {
        // Use a clean sans-serif font with slight letter spacing
        return juce::Font(juce::FontOptions(label.getFont().getHeight()));
    }

    // Linear horizontal slider (for decay slider, etc.)
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float minSliderPos, float maxSliderPos,
                         juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearVertical)
        {
            auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
            bool isHorizontal = (style == juce::Slider::LinearHorizontal);

            // Track background
            float trackHeight = isHorizontal ? 4.0f : bounds.getWidth();
            float trackWidth = isHorizontal ? bounds.getWidth() : 4.0f;
            auto trackBounds = bounds.withSizeKeepingCentre(trackWidth, trackHeight);

            g.setColour(juce::Colour(0xFF1e1e20));
            g.fillRoundedRectangle(trackBounds, 2.0f);

            // Value fill
            juce::Colour trackColour = slider.findColour(juce::Slider::trackColourId);
            if (trackColour == juce::Colour())
                trackColour = selectedBandColor;

            auto valueBounds = trackBounds;
            if (isHorizontal)
                valueBounds.setWidth(sliderPos - static_cast<float>(x));
            else
                valueBounds.setTop(sliderPos);

            g.setColour(trackColour.withAlpha(0.7f));
            g.fillRoundedRectangle(valueBounds, 2.0f);

            // Thumb
            float thumbSize = isHorizontal ? 12.0f : 12.0f;
            float thumbX = isHorizontal ? sliderPos - thumbSize * 0.5f : bounds.getCentreX() - thumbSize * 0.5f;
            float thumbY = isHorizontal ? bounds.getCentreY() - thumbSize * 0.5f : sliderPos - thumbSize * 0.5f;

            // Thumb shadow
            g.setColour(juce::Colour(0x30000000));
            g.fillEllipse(thumbX + 1, thumbY + 1, thumbSize, thumbSize);

            // Thumb body
            juce::ColourGradient thumbGradient(
                juce::Colour(0xFF4a4a50), thumbX, thumbY,
                juce::Colour(0xFF3a3a40), thumbX, thumbY + thumbSize,
                false);
            g.setGradientFill(thumbGradient);
            g.fillEllipse(thumbX, thumbY, thumbSize, thumbSize);

            // Thumb highlight
            g.setColour(juce::Colour(0x20ffffff));
            g.drawEllipse(thumbX + 1, thumbY + 1, thumbSize - 2, thumbSize - 2, 0.5f);

            // Thumb border
            g.setColour(juce::Colour(0xFF2a2a2e));
            g.drawEllipse(thumbX, thumbY, thumbSize, thumbSize, 0.75f);
        }
        else
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                             minSliderPos, maxSliderPos, style, slider);
        }

        juce::ignoreUnused(minSliderPos, maxSliderPos);
    }
};

//==============================================================================
/**
    Band Enable Button with color indicator and filter type icon
    Redesigned with cleaner, minimal line art style (Logic Pro aesthetic)
*/
class BandEnableButton : public juce::ToggleButton
{
public:
    BandEnableButton(int bandIndex)
    {
        if (bandIndex >= 0 && bandIndex < 8)
        {
            bandColor = DefaultBandConfigs[bandIndex].color;
            filterType = DefaultBandConfigs[bandIndex].type;
        }
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto buttonBounds = getLocalBounds().toFloat().reduced(2.0f);
        bool toggleState = getToggleState();

        // Smooth hover/selection background with rounded rectangle
        if (toggleState)
        {
            // Active state - filled with band color, subtle gradient
            juce::ColourGradient bgGradient(
                bandColor.brighter(0.15f), buttonBounds.getX(), buttonBounds.getY(),
                bandColor.darker(0.1f), buttonBounds.getX(), buttonBounds.getBottom(),
                false);
            g.setGradientFill(bgGradient);
            g.fillRoundedRectangle(buttonBounds, 6.0f);

            // Subtle inner highlight at top
            g.setColour(juce::Colours::white.withAlpha(0.15f));
            g.drawHorizontalLine(static_cast<int>(buttonBounds.getY() + 2),
                                 buttonBounds.getX() + 4, buttonBounds.getRight() - 4);
        }
        else if (shouldDrawButtonAsHighlighted)
        {
            // Hover state - subtle background highlight
            g.setColour(bandColor.withAlpha(0.25f));
            g.fillRoundedRectangle(buttonBounds, 6.0f);

            // Outline on hover
            g.setColour(bandColor.withAlpha(0.5f));
            g.drawRoundedRectangle(buttonBounds, 6.0f, 1.0f);
        }
        else
        {
            // Inactive state - very subtle background
            g.setColour(juce::Colour(0xFF222226));
            g.fillRoundedRectangle(buttonBounds, 6.0f);

            // Very subtle outline
            g.setColour(juce::Colour(0xFF333338));
            g.drawRoundedRectangle(buttonBounds, 6.0f, 0.5f);
        }

        // Button border - prominent when enabled
        if (toggleState)
        {
            // Outer glow for active state
            g.setColour(bandColor.withAlpha(0.3f));
            g.drawRoundedRectangle(buttonBounds.expanded(1.0f), 7.0f, 2.0f);

            // White border for selected
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawRoundedRectangle(buttonBounds, 6.0f, 1.5f);
        }

        // Draw filter type icon
        auto iconBounds = buttonBounds.reduced(buttonBounds.getWidth() * 0.2f);
        drawFilterTypeIcon(g, iconBounds, toggleState, shouldDrawButtonAsHighlighted);

        juce::ignoreUnused(shouldDrawButtonAsDown);
    }

private:
    void drawFilterTypeIcon(juce::Graphics& g, juce::Rectangle<float> bounds, bool isEnabled, bool isHovered)
    {
        // Icon color based on state
        juce::Colour iconColor;
        if (isEnabled)
            iconColor = juce::Colours::white;
        else if (isHovered)
            iconColor = bandColor.withAlpha(0.9f);
        else
            iconColor = bandColor.withAlpha(0.5f);

        g.setColour(iconColor);

        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float w = bounds.getWidth() * 0.38f;
        float h = bounds.getHeight() * 0.35f;
        float strokeWidth = isEnabled ? 1.8f : 1.5f;

        juce::Path path;

        switch (filterType)
        {
            case BandType::HighPass:
                // Clean HPF slope: flat line to slope up
                path.startNewSubPath(cx - w, cy + h * 0.6f);
                path.quadraticTo(cx - w * 0.2f, cy + h * 0.6f, cx, cy);
                path.lineTo(cx + w, cy - h * 0.4f);
                g.strokePath(path, juce::PathStrokeType(strokeWidth,
                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;

            case BandType::LowShelf:
                // Smooth low shelf curve
                path.startNewSubPath(cx - w, cy - h * 0.5f);
                path.lineTo(cx - w * 0.4f, cy - h * 0.5f);
                path.quadraticTo(cx, cy, cx + w * 0.4f, cy + h * 0.5f);
                path.lineTo(cx + w, cy + h * 0.5f);
                g.strokePath(path, juce::PathStrokeType(strokeWidth,
                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;

            case BandType::Parametric:
                // Smooth bell/peak curve
                path.startNewSubPath(cx - w, cy);
                path.quadraticTo(cx - w * 0.5f, cy, cx, cy - h * 0.8f);
                path.quadraticTo(cx + w * 0.5f, cy, cx + w, cy);
                g.strokePath(path, juce::PathStrokeType(strokeWidth,
                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;

            case BandType::HighShelf:
                // Smooth high shelf curve
                path.startNewSubPath(cx - w, cy + h * 0.5f);
                path.lineTo(cx - w * 0.4f, cy + h * 0.5f);
                path.quadraticTo(cx, cy, cx + w * 0.4f, cy - h * 0.5f);
                path.lineTo(cx + w, cy - h * 0.5f);
                g.strokePath(path, juce::PathStrokeType(strokeWidth,
                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;

            case BandType::LowPass:
                // Clean LPF slope: slope down to flat line
                path.startNewSubPath(cx - w, cy - h * 0.4f);
                path.lineTo(cx, cy);
                path.quadraticTo(cx + w * 0.2f, cy + h * 0.6f, cx + w, cy + h * 0.6f);
                g.strokePath(path, juce::PathStrokeType(strokeWidth,
                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;

            default:
                // Fallback: simple bell
                path.startNewSubPath(cx - w, cy);
                path.quadraticTo(cx, cy - h, cx + w, cy);
                g.strokePath(path, juce::PathStrokeType(strokeWidth,
                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;
        }
    }

    juce::Colour bandColor = juce::Colours::grey;
    BandType filterType = BandType::Parametric;
};

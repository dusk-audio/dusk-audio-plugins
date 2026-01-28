/*
  ==============================================================================

    LunaLookAndFeel.h
    Shared look and feel for Luna Co. Audio plugins

  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
 * Standard LED meter styling constants for Luna Co. Audio plugins
 * Use these to ensure consistent meter appearance across all plugins
 */
struct LEDMeterStyle
{
    // Standard meter dimensions
    static constexpr int standardWidth = 32;           // Standard meter width in pixels (wider for visibility)
    static constexpr int meterAreaWidth = 60;          // Total area including labels
    static constexpr int labelHeight = 16;             // Height for "INPUT"/"OUTPUT" labels
    static constexpr int valueHeight = 20;             // Height for dB value display below meter
    static constexpr int labelSpacing = 4;             // Space between label and meter

    // Label styling
    static constexpr float labelFontSize = 10.0f;      // Font size for "INPUT"/"OUTPUT"
    static constexpr float valueFontSize = 10.0f;      // Font size for dB values

    // Colors
    static inline juce::Colour getLabelColor() { return juce::Colour(0xffe0e0e0); }
    static inline juce::Colour getValueColor() { return juce::Colour(0xffcccccc); }

    /**
     * Draw meter labels and values in a standard way
     * @param g Graphics context
     * @param meterBounds The bounds of the actual meter component
     * @param label The label text ("INPUT" or "OUTPUT")
     * @param levelDb The current level in dB to display
     * @param scaleFactor Optional scale factor for responsive layouts
     */
    static void drawMeterLabels(juce::Graphics& g,
                                 juce::Rectangle<int> meterBounds,
                                 const juce::String& label,
                                 float levelDb,
                                 float scaleFactor = 1.0f)
    {
        // Draw label above meter
        g.setFont(juce::Font(juce::FontOptions(labelFontSize * scaleFactor).withStyle("Bold")));
        g.setColour(getLabelColor());

        int labelWidth = static_cast<int>(50 * scaleFactor);
        int labelX = meterBounds.getCentreX() - labelWidth / 2;
        g.drawText(label, labelX, meterBounds.getY() - static_cast<int>((labelHeight + labelSpacing) * scaleFactor),
                   labelWidth, static_cast<int>(labelHeight * scaleFactor), juce::Justification::centred);

        // Draw value below meter
        g.setFont(juce::Font(juce::FontOptions(valueFontSize * scaleFactor).withStyle("Bold")));
        g.setColour(getValueColor());

        juce::String valueText = juce::String(levelDb, 1) + " dB";
        g.drawText(valueText, labelX, meterBounds.getBottom() + static_cast<int>(labelSpacing * scaleFactor),
                   labelWidth, static_cast<int>(valueHeight * scaleFactor), juce::Justification::centred);
    }
};

//==============================================================================
/**
 * Standard slider/knob configuration for Luna Co. Audio plugins
 * Use these to ensure consistent knob behavior across all plugins
 */
struct LunaSliderStyle
{
    // Velocity mode parameters for professional knob feel
    static constexpr double sensitivity = 0.5;    // Lower = slower, more controlled movement
    static constexpr int threshold = 2;           // Ignore tiny mouse movements (reduces jitter)
    static constexpr double fineControlOffset = 0.1;  // 10x finer when Ctrl/Cmd held
    static constexpr bool allowModifierToggle = true; // Allow Ctrl/Cmd for fine mode

    /**
     * Configure a rotary slider with professional Luna knob behavior
     * Call this after setting slider style to RotaryVerticalDrag
     *
     * Features:
     * - 50% slower base movement for precise control
     * - Jitter filtering (ignores tiny mouse movements)
     * - 10x fine control with Ctrl/Cmd modifier
     *
     * @param slider The slider to configure
     */
    static void configureKnob(juce::Slider& slider)
    {
        slider.setVelocityBasedMode(true);
        slider.setVelocityModeParameters(sensitivity, threshold, fineControlOffset, allowModifierToggle);
    }

    /**
     * Configure a rotary slider with custom sensitivity
     * @param slider The slider to configure
     * @param customSensitivity Sensitivity multiplier (0.3 = slower, 1.0 = default JUCE)
     */
    static void configureKnob(juce::Slider& slider, double customSensitivity)
    {
        slider.setVelocityBasedMode(true);
        slider.setVelocityModeParameters(customSensitivity, threshold, fineControlOffset, allowModifierToggle);
    }

    /**
     * Full setup for a rotary knob with Luna defaults
     * Sets style, enables scroll wheel, and configures velocity mode
     * @param slider The slider to setup
     */
    static void setupRotaryKnob(juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setScrollWheelEnabled(true);
        configureKnob(slider);
    }
};

class LunaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LunaLookAndFeel()
    {
        // Dark theme colors
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1a1a1a));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff4a9eff));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff0f0f0f));
        setColour(juce::Label::textColourId, juce::Colours::white);
    }

    ~LunaLookAndFeel() override = default;
};

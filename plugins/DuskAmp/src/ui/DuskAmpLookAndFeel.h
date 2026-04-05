#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class DuskAmpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DuskAmpLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&,
                        int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    // Premium dark theme — deeper background, cooler panels
    static constexpr juce::uint32 kBackground   = 0xff121212;  // Very deep neutral grey
    static constexpr juce::uint32 kPanel        = 0xff1e1e1e;  // Slightly lighter panel
    static constexpr juce::uint32 kPanelHi      = 0xff282828;  // Elevated panel / hover
    static constexpr juce::uint32 kAccent       = 0xffda8a2e;  // Warm amber/orange
    static constexpr juce::uint32 kKnobFill     = 0xff2e2e2e;  // Knob body (cooler)
    static constexpr juce::uint32 kKnobEdge     = 0xff0a0a0a;  // Knob outer ring
    static constexpr juce::uint32 kBorder       = 0xff3a3a3a;  // Subtle border (dimmer)
    static constexpr juce::uint32 kBorderHi     = 0xff4a4a4a;  // Brighter border / hover

    // 4-tier text hierarchy (brightest → dimmest)
    static constexpr juce::uint32 kValueText    = 0xfff0f0f0;  // Value readouts (brightest)
    static constexpr juce::uint32 kLabelText    = 0xffb0b0b0;  // Knob name labels (bold)
    static constexpr juce::uint32 kGroupText    = 0xff808080;  // Group titles, inactive buttons
    static constexpr juce::uint32 kDimText      = 0xff505050;  // Disabled controls

    // Legacy aliases
    static constexpr juce::uint32 kText         = 0xffe0e0e0;
    static constexpr juce::uint32 kSubtleText   = 0xff808080;

    // Amp-specific
    static constexpr juce::uint32 kLED          = 0xff44ff44;
    static constexpr juce::uint32 kLEDOff       = 0xff224422;
};

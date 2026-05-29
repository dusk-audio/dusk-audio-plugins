#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Modern minimalist look — single-accent, flat, no skeumorphic gradients.
// Drawing principle: reserve ALL orange for engaged / actively-changing
// state. Static labels are neutral grey. Knob arc only fills past the
// 12-o'clock position to make value-at-a-glance obvious.
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

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    // ===== Modern minimalist palette =====
    // Background = warm near-black; panels = one notch up; accent = single
    // amber used ONLY for engaged state. Text uses 3 tiers, all neutral.
    static constexpr juce::uint32 kBackground   = 0xff0e0e10;  // near-black, faintly warm
    static constexpr juce::uint32 kPanel        = 0xff17171a;  // panel — one notch up
    static constexpr juce::uint32 kAccent       = 0xffda8a2e;  // amber — ENGAGED state only
    static constexpr juce::uint32 kKnobFill     = 0xff1c1c20;  // slightly darker than panel
    static constexpr juce::uint32 kBorder       = 0xff2a2a2e;  // thin border

    // 3 tiers — no more 4-tier ramp; flat hierarchy.
    static constexpr juce::uint32 kValueText    = 0xffe8e8ea;  // primary readouts
    static constexpr juce::uint32 kLabelText    = 0xff9a9a9e;  // knob names + section titles
    static constexpr juce::uint32 kGroupText    = 0xff707074;  // inactive UI

    // Compatibility aliases (still referenced by some call sites).
    static constexpr juce::uint32 kDimText      = 0xff4a4a4e;
    static constexpr juce::uint32 kText         = 0xffe8e8ea;
    static constexpr juce::uint32 kSubtleText   = 0xff707074;

    // LED indicator (gate/clip)
    static constexpr juce::uint32 kLED          = 0xffda8a2e;  // accent for ON
    static constexpr juce::uint32 kLEDOff       = 0xff2a2a2e;  // off = border colour
};

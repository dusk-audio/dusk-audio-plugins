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

    // Dark amp-style theme with warm amber accent
    static constexpr juce::uint32 kBackground   = 0xff1a1a1a;  // Very dark grey
    static constexpr juce::uint32 kPanel        = 0xff242424;  // Dark panel
    static constexpr juce::uint32 kAccent       = 0xffda8a2e;  // Warm amber/orange
    static constexpr juce::uint32 kKnobFill     = 0xff3a3a3a;  // Dark knob
    static constexpr juce::uint32 kBorder       = 0xff4a4a4a;  // Subtle border

    // 4-tier text hierarchy (brightest -> dimmest)
    static constexpr juce::uint32 kValueText    = 0xfff0f0f0;  // value readouts (brightest)
    static constexpr juce::uint32 kLabelText    = 0xffb0b0b0;  // knob name labels (bold)
    static constexpr juce::uint32 kGroupText    = 0xff888888;  // group titles, inactive buttons
    static constexpr juce::uint32 kDimText      = 0xff555555;  // disabled controls

    // Legacy aliases (title, tooltips)
    static constexpr juce::uint32 kText         = 0xffe0e0e0;
    static constexpr juce::uint32 kSubtleText   = 0xff888888;

    // Amp-specific
    static constexpr juce::uint32 kLED          = 0xff44ff44;  // Green LED
    static constexpr juce::uint32 kLEDOff       = 0xff224422;  // LED off state

    // Per-model accent colors (DSP mode)
    static constexpr juce::uint32 kAccentRound  = 0xffda8a2e;  // Warm amber (Fender)
    static constexpr juce::uint32 kAccentChime  = 0xff4a9ed6;  // Cool blue (Vox AC30)
    static constexpr juce::uint32 kAccentPunch  = 0xffc84040;  // Hot red (Marshall)
    static constexpr juce::uint32 kAccentNAM    = 0xff8866cc;  // Purple (NAM mode)

    // Per-model background tint
    static constexpr juce::uint32 kBgRound      = 0xff1a1810;  // Warm dark tint
    static constexpr juce::uint32 kBgChime      = 0xff141a1e;  // Cool dark tint
    static constexpr juce::uint32 kBgPunch      = 0xff1e1414;  // Red dark tint
    static constexpr juce::uint32 kBgNAM        = 0xff181418;  // Purple dark tint

    void setAmpModelTheme (int model, bool isNAMMode)
    {
        if (isNAMMode)
        {
            currentAccent_ = kAccentNAM;
            currentBackground_ = kBgNAM;
        }
        else
        {
            switch (model)
            {
                case 0: currentAccent_ = kAccentRound; currentBackground_ = kBgRound; break;
                case 1: currentAccent_ = kAccentChime; currentBackground_ = kBgChime; break;
                case 2: currentAccent_ = kAccentPunch; currentBackground_ = kBgPunch; break;
                default: currentAccent_ = kAccent; currentBackground_ = kBackground; break;
            }
        }
    }

    juce::uint32 getCurrentAccent() const { return currentAccent_; }
    juce::uint32 getCurrentBackground() const { return currentBackground_; }

private:
    juce::uint32 currentAccent_ = kAccent;
    juce::uint32 currentBackground_ = kBackground;
};

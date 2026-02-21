#pragma once

#include <JuceHeader.h>

//==============================================================================
// TapeMachine Premium Look and Feel
// Vintage professional recording studio aesthetic inspired by 1970s tape machines
// (classic professional tape machines from the 1970s)
//==============================================================================

namespace TapeMachineColors
{
    // Primary surfaces
    constexpr uint32_t background       = 0xff1a1512;     // Deep warm brown
    constexpr uint32_t panelDark        = 0xff2d2520;     // Panel base
    constexpr uint32_t panelLight       = 0xff3d3530;     // Raised panel
    constexpr uint32_t panelHighlight   = 0xff4d4540;     // Panel highlights

    // Metallic accents
    constexpr uint32_t metalDark        = 0xff5a5048;     // Dark metal
    constexpr uint32_t metalMid         = 0xff8b7355;     // Mid metal
    constexpr uint32_t metalLight       = 0xffc4a77d;     // Light metal / brass
    constexpr uint32_t metalHighlight   = 0xffd4c090;     // Metal highlight
    constexpr uint32_t chrome           = 0xffb8b0a0;     // Chrome accents

    // VU meter colors
    constexpr uint32_t vuFace           = 0xfff5f0e6;     // Warm cream face
    constexpr uint32_t vuFaceAged       = 0xfff0e8d8;     // Slightly aged cream
    constexpr uint32_t vuNeedle         = 0xffcc3333;     // Classic red needle
    constexpr uint32_t vuRedZone        = 0xffd42c2c;     // Red zone markings
    constexpr uint32_t vuBlackText      = 0xff2a2a2a;     // Scale text

    // LED colors
    constexpr uint32_t ledGreenOff      = 0xff1a3018;     // Unlit green LED
    constexpr uint32_t ledGreenOn       = 0xff7cba5f;     // Lit green LED
    constexpr uint32_t ledGreenGlow     = 0x4080d060;     // Green glow
    constexpr uint32_t ledRedOff        = 0xff301818;     // Unlit red LED
    constexpr uint32_t ledRedOn         = 0xffe85d4c;     // Lit red LED
    constexpr uint32_t ledRedGlow       = 0x40e05050;     // Red glow
    constexpr uint32_t ledAmberOff      = 0xff302818;     // Unlit amber LED
    constexpr uint32_t ledAmberOn       = 0xfff8d080;     // Lit amber LED
    constexpr uint32_t ledAmberGlow     = 0x40f0c050;     // Amber glow

    // Text colors
    constexpr uint32_t textPrimary      = 0xffd4c8b8;     // Warm off-white
    constexpr uint32_t textSecondary    = 0xffa09888;     // Dimmed text
    constexpr uint32_t textEngraved     = 0xff1a1510;     // Engraved text shadow

    // Knob colors
    constexpr uint32_t knobBody         = 0xff3a3028;     // Bakelite brown
    constexpr uint32_t knobBodyLight    = 0xff5a4838;     // Lighter variant
    constexpr uint32_t knobRing         = 0xff6a5848;     // Outer ring
    constexpr uint32_t knobPointer      = 0xfff5f0e6;     // Pointer/indicator
    constexpr uint32_t knobSkirt        = 0xff2a2018;     // Skirt base
}

//==============================================================================
// Premium Vintage Look and Feel for TapeMachine
//==============================================================================
class TapeMachineLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TapeMachineLookAndFeel();
    ~TapeMachineLookAndFeel() override = default;

    //==========================================================================
    // Rotary Slider (Premium Chicken-Head Knobs)
    //==========================================================================
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;

    //==========================================================================
    // Toggle Button (Illuminated Vintage Switches)
    //==========================================================================
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    //==========================================================================
    // Combo Box (Vintage Selector Styling)
    //==========================================================================
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;

    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    juce::Font getPopupMenuFont() override;

    //==========================================================================
    // Label (Engraved Text Effect)
    //==========================================================================
    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    //==========================================================================
    // Text Editor (Value Display)
    //==========================================================================
    void fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                   juce::TextEditor& textEditor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height,
                                juce::TextEditor& textEditor) override;

    //==========================================================================
    // Helper Drawing Functions
    //==========================================================================

    // Draw a realistic LED indicator
    static void drawLED(juce::Graphics& g, juce::Rectangle<float> bounds,
                        bool isOn, uint32_t offColor, uint32_t onColor, uint32_t glowColor);

    // Draw brushed metal texture
    static void drawBrushedMetal(juce::Graphics& g, juce::Rectangle<float> bounds,
                                  bool isVertical = false);

    // Draw beveled panel section
    static void drawBeveledPanel(juce::Graphics& g, juce::Rectangle<float> bounds,
                                  float cornerSize = 4.0f, float bevelWidth = 2.0f);

    // Draw screw/rivet detail
    static void drawScrew(juce::Graphics& g, float cx, float cy, float radius);

    // Draw nameplate with embossed text
    static void drawNameplate(juce::Graphics& g, juce::Rectangle<float> bounds,
                               const juce::String& text, float fontSize = 18.0f);

    // Draw vintage rack ears
    static void drawRackEars(juce::Graphics& g, juce::Rectangle<int> bounds);

private:
    // Cache for gradient images
    juce::Image knobCache;
    int cachedKnobSize = 0;

    void createKnobCache(int size);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeMachineLookAndFeel)
};

//==============================================================================
// Helper class for rendering tape reels
//==============================================================================
class PremiumReelRenderer
{
public:
    static void drawReel(juce::Graphics& g, juce::Rectangle<float> bounds,
                         float rotation, float tapeAmount, bool isSupplyReel);

private:
    static void drawFlangeWithReflections(juce::Graphics& g, juce::Point<float> centre,
                                           float radius, float rotation);
    static void drawTapePack(juce::Graphics& g, juce::Point<float> centre,
                              float innerRadius, float outerRadius);
    static void drawSpokes(juce::Graphics& g, juce::Point<float> centre,
                           float innerRadius, float outerRadius, float rotation);
    static void drawHub(juce::Graphics& g, juce::Point<float> centre, float radius);
};

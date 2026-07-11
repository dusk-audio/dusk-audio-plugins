#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MultiSynthVoice.h" // for SynthMode enum

//==============================================================================
// Base class for Multi-Synth mode-specific looks
class MultiSynthLookAndFeelBase : public juce::LookAndFeel_V4
{
public:
    struct ColorScheme
    {
        juce::Colour panelBackground;
        juce::Colour sectionBackground;
        juce::Colour sectionBorder;
        juce::Colour knobBody;
        juce::Colour knobIndicator;
        juce::Colour accent;
        juce::Colour text;
        juce::Colour textDim;
        juce::Colour sliderTrack;
        juce::Colour shadow;
    };

    ColorScheme colors;

    juce::Label* createSliderTextBox(juce::Slider& slider) override;
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor&) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    // Linear slider (vertical fader) — base implementation, overridden per mode
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override;

    // Section painting helper (called from editor paint())
    virtual void paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                              const juce::String& title, float scaleFactor) const;

    // Background painting (called from editor paint())
    virtual void paintBackground(juce::Graphics& g, int width, int height) const;

    // Special elements (called from editor paint())
    virtual void paintSpecialElements(juce::Graphics& g, juce::Rectangle<int> bounds) const {}

protected:
    void drawIlluminatedToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                     bool highlighted, bool down,
                                     const juce::Colour& onTop, const juce::Colour& onBottom,
                                     const juce::Colour& onText,
                                     const juce::Colour& offTop, const juce::Colour& offBottom,
                                     const juce::Colour& offText);

    void drawChromeKnob(juce::Graphics& g, float x, float y, float w, float h,
                        float sliderPos, float startAngle, float endAngle);
    void drawBakeliteKnob(juce::Graphics& g, float x, float y, float w, float h,
                          float sliderPos, float startAngle, float endAngle);
    void drawChunkyKnob(juce::Graphics& g, float x, float y, float w, float h,
                        float sliderPos, float startAngle, float endAngle);
    void drawDaviesKnob(juce::Graphics& g, float x, float y, float w, float h,
                        float sliderPos, float startAngle, float endAngle);
};

//==============================================================================
class CosmosLookAndFeel : public MultiSynthLookAndFeelBase
{
public:
    CosmosLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void paintBackground(juce::Graphics& g, int width, int height) const override;
    void paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                      const juce::String& title, float scaleFactor) const override;
};

//==============================================================================
class OracleLookAndFeel : public MultiSynthLookAndFeelBase
{
public:
    OracleLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void paintBackground(juce::Graphics& g, int width, int height) const override;
    void paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                      const juce::String& title, float scaleFactor) const override;
};

//==============================================================================
class MonoLookAndFeel : public MultiSynthLookAndFeelBase
{
public:
    MonoLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void paintBackground(juce::Graphics& g, int width, int height) const override;
    void paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                      const juce::String& title, float scaleFactor) const override;
};

//==============================================================================
class ModularLookAndFeel : public MultiSynthLookAndFeelBase
{
public:
    ModularLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void paintBackground(juce::Graphics& g, int width, int height) const override;
    void paintSection(juce::Graphics& g, juce::Rectangle<int> bounds,
                      const juce::String& title, float scaleFactor) const override;
    void paintSpecialElements(juce::Graphics& g, juce::Rectangle<int> bounds) const override;
};

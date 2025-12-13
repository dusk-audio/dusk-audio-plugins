/*
  ==============================================================================

    Convolution Reverb - Custom Look and Feel
    Luna Co. Audio visual styling
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class ConvolutionReverbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ConvolutionReverbLookAndFeel();
    ~ConvolutionReverbLookAndFeel() override = default;

    // Slider drawing
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override;

    // Toggle button drawing
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    // Tree view drawing (for IR browser)
    void drawTreeviewPlusMinusBox(juce::Graphics& g, const juce::Rectangle<float>& area,
                                   juce::Colour backgroundColour, bool isOpen,
                                   bool isMouseOver) override;

    // File browser styling
    void drawFileBrowserRow(juce::Graphics& g, int width, int height,
                            const juce::File& file, const juce::String& filename,
                            juce::Image* icon, const juce::String& fileSizeDescription,
                            const juce::String& fileTimeDescription,
                            bool isDirectory, bool isItemSelected,
                            int itemIndex, juce::DirectoryContentsDisplayComponent& component) override;

    // Label styling
    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    // ComboBox styling
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    // Color getters
    juce::Colour getBackgroundColour() const { return backgroundColour; }
    juce::Colour getPanelColour() const { return panelColour; }
    juce::Colour getAccentColour() const { return accentColour; }
    juce::Colour getTextColour() const { return textColour; }
    juce::Colour getWaveformColour() const { return waveformColour; }
    juce::Colour getEnvelopeColour() const { return envelopeColour; }

private:
    // Luna unified color scheme
    juce::Colour backgroundColour{0xff1a1a1a};
    juce::Colour panelColour{0xff2a2a2a};
    juce::Colour knobColour{0xff3a3a3a};
    juce::Colour accentColour{0xff4a9eff};      // Blue accent
    juce::Colour textColour{0xffe0e0e0};
    juce::Colour dimTextColour{0xff909090};
    juce::Colour waveformColour{0xff5588cc};    // IR waveform color
    juce::Colour envelopeColour{0xffcc8855};    // Envelope overlay color
    juce::Colour gridColour{0xff3a3a3a};
    juce::Colour highlightColour{0xff4a9eff};
    juce::Colour shadowColour{0xff0a0a0a};

    // Helper methods
    void drawMetallicKnob(juce::Graphics& g, float x, float y, float diameter,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          const juce::Colour& baseColour);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConvolutionReverbLookAndFeel)
};

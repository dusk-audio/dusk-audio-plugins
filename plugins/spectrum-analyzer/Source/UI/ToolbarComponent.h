#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    Toolbar Component for Spectrum Analyzer settings
*/
class ToolbarComponent : public juce::Component
{
public:
    ToolbarComponent()
    {
        // FFT Resolution
        fftResolutionLabel.setText("FFT:", juce::dontSendNotification);
        fftResolutionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        addAndMakeVisible(fftResolutionLabel);

        fftResolutionCombo.addItem("2048", 1);
        fftResolutionCombo.addItem("4096", 2);
        fftResolutionCombo.addItem("8192", 3);
        fftResolutionCombo.setSelectedId(2);
        addAndMakeVisible(fftResolutionCombo);

        // Smoothing
        smoothingLabel.setText("Smooth:", juce::dontSendNotification);
        smoothingLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        addAndMakeVisible(smoothingLabel);

        smoothingSlider.setRange(0.0, 1.0, 0.01);
        smoothingSlider.setValue(0.5);
        smoothingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
        smoothingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        smoothingSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffaaaaaa));
        smoothingSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(smoothingSlider);

        // Slope (dB/octave)
        slopeLabel.setText("Slope:", juce::dontSendNotification);
        slopeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        addAndMakeVisible(slopeLabel);

        slopeSlider.setRange(-4.5, 4.5, 0.5);
        slopeSlider.setValue(0.0);  // Default flat response
        slopeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 18);
        slopeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        slopeSlider.setTextValueSuffix(" dB");
        slopeSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffaaaaaa));
        slopeSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(slopeSlider);

        // Decay (dB/s)
        decayLabel.setText("Decay:", juce::dontSendNotification);
        decayLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        addAndMakeVisible(decayLabel);

        decaySlider.setRange(3.0, 60.0, 1.0);
        decaySlider.setValue(20.0);
        decaySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
        decaySlider.setSliderStyle(juce::Slider::LinearHorizontal);
        decaySlider.setTextValueSuffix(" dB/s");
        decaySlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffaaaaaa));
        decaySlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(decaySlider);

        // Peak Hold
        peakHoldButton.setButtonText("Peak Hold");
        peakHoldButton.setToggleState(true, juce::dontSendNotification);
        peakHoldButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff888888));
        peakHoldButton.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff00aaff));
        addAndMakeVisible(peakHoldButton);

        // Range (min dB)
        rangeLabel.setText("Range:", juce::dontSendNotification);
        rangeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        addAndMakeVisible(rangeLabel);

        rangeSlider.setRange(-100.0, -30.0, 10.0);
        rangeSlider.setValue(-60.0);
        rangeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 18);
        rangeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        rangeSlider.setTextValueSuffix(" dB");
        rangeSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffaaaaaa));
        rangeSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(rangeSlider);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10, 2);
        int spacing = 12;

        // FFT Resolution
        fftResolutionLabel.setBounds(bounds.removeFromLeft(28));
        fftResolutionCombo.setBounds(bounds.removeFromLeft(65).reduced(0, 4));
        bounds.removeFromLeft(spacing);

        // Smoothing
        smoothingLabel.setBounds(bounds.removeFromLeft(52));
        smoothingSlider.setBounds(bounds.removeFromLeft(85));
        bounds.removeFromLeft(spacing);

        // Slope
        slopeLabel.setBounds(bounds.removeFromLeft(40));
        slopeSlider.setBounds(bounds.removeFromLeft(90));
        bounds.removeFromLeft(spacing);

        // Decay
        decayLabel.setBounds(bounds.removeFromLeft(45));
        decaySlider.setBounds(bounds.removeFromLeft(95));
        bounds.removeFromLeft(spacing);

        // Range (min dB)
        rangeLabel.setBounds(bounds.removeFromLeft(45));
        rangeSlider.setBounds(bounds.removeFromLeft(95));
        bounds.removeFromLeft(spacing);

        // Peak Hold - checkbox on right side
        peakHoldButton.setBounds(bounds.removeFromLeft(90));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff252525));

        // Top border
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    }

    //==========================================================================
    // Access controls for attachments
    juce::ComboBox& getFFTResolutionCombo() { return fftResolutionCombo; }
    juce::Slider& getSmoothingSlider() { return smoothingSlider; }
    juce::Slider& getSlopeSlider() { return slopeSlider; }
    juce::Slider& getDecaySlider() { return decaySlider; }
    juce::ToggleButton& getPeakHoldButton() { return peakHoldButton; }
    juce::Slider& getRangeSlider() { return rangeSlider; }

private:
    juce::Label fftResolutionLabel;
    juce::ComboBox fftResolutionCombo;

    juce::Label smoothingLabel;
    juce::Slider smoothingSlider;

    juce::Label slopeLabel;
    juce::Slider slopeSlider;

    juce::Label decayLabel;
    juce::Slider decaySlider;

    juce::ToggleButton peakHoldButton;

    juce::Label rangeLabel;
    juce::Slider rangeSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToolbarComponent)
};

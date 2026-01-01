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
        slopeSlider.setValue(0.0);
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
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10, 2);
        int spacing = 15;

        // FFT Resolution - wider to show "8192" fully
        fftResolutionLabel.setBounds(bounds.removeFromLeft(30));
        fftResolutionCombo.setBounds(bounds.removeFromLeft(70).reduced(0, 4));
        bounds.removeFromLeft(spacing);

        // Smoothing - slider + text box (40px for value)
        smoothingLabel.setBounds(bounds.removeFromLeft(55));
        smoothingSlider.setBounds(bounds.removeFromLeft(90));  // 50 slider + 40 text
        bounds.removeFromLeft(spacing);

        // Slope - slider + text box (45px for value with "dB")
        slopeLabel.setBounds(bounds.removeFromLeft(42));
        slopeSlider.setBounds(bounds.removeFromLeft(100));  // 55 slider + 45 text
        bounds.removeFromLeft(spacing);

        // Decay - slider + text box (50px for value with "dB/s")
        decayLabel.setBounds(bounds.removeFromLeft(48));
        decaySlider.setBounds(bounds.removeFromLeft(105));  // 55 slider + 50 text
        bounds.removeFromLeft(spacing);

        // Peak Hold - checkbox on right side
        peakHoldButton.setBounds(bounds.removeFromLeft(100));
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToolbarComponent)
};

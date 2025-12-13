/*
  ==============================================================================

    Convolution Reverb - Plugin Editor
    Main UI for the convolution reverb
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ConvolutionReverbLookAndFeel.h"
#include "IRBrowser.h"
#include "IRWaveformDisplay.h"
#include "LEDMeter.h"

class ConvolutionReverbEditor : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private IRBrowser::Listener
{
public:
    ConvolutionReverbEditor(ConvolutionReverbProcessor& processor);
    ~ConvolutionReverbEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    // IRBrowser::Listener
    void irFileSelected(const juce::File& file) override;

    // Setup methods
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText,
                     const juce::String& suffix = "");
    void setupToggleButton(juce::ToggleButton& button, const juce::String& text);
    void updateWaveformDisplay();
    void updateEnvelopeDisplay();
    void updateIRNameLabel();

    ConvolutionReverbProcessor& audioProcessor;
    ConvolutionReverbLookAndFeel lookAndFeel;

    // IR Browser
    std::unique_ptr<IRBrowser> irBrowser;

    // Waveform display
    std::unique_ptr<IRWaveformDisplay> waveformDisplay;

    // IR name label
    std::unique_ptr<juce::Label> irNameLabel;

    // Envelope controls
    std::unique_ptr<juce::Slider> attackSlider;
    std::unique_ptr<juce::Slider> decaySlider;
    std::unique_ptr<juce::Slider> lengthSlider;
    std::unique_ptr<juce::ToggleButton> reverseButton;
    std::unique_ptr<juce::Label> attackLabel;
    std::unique_ptr<juce::Label> decayLabel;
    std::unique_ptr<juce::Label> lengthLabel;

    // Main controls
    std::unique_ptr<juce::Slider> preDelaySlider;
    std::unique_ptr<juce::Slider> widthSlider;
    std::unique_ptr<juce::Slider> mixSlider;
    std::unique_ptr<juce::Label> preDelayLabel;
    std::unique_ptr<juce::Label> widthLabel;
    std::unique_ptr<juce::Label> mixLabel;

    // Filter controls
    std::unique_ptr<juce::Slider> hpfSlider;
    std::unique_ptr<juce::Slider> lpfSlider;
    std::unique_ptr<juce::Label> hpfLabel;
    std::unique_ptr<juce::Label> lpfLabel;

    // EQ controls
    std::unique_ptr<juce::Slider> eqLowFreqSlider;
    std::unique_ptr<juce::Slider> eqLowGainSlider;
    std::unique_ptr<juce::Slider> eqLowMidFreqSlider;
    std::unique_ptr<juce::Slider> eqLowMidGainSlider;
    std::unique_ptr<juce::Slider> eqHighMidFreqSlider;
    std::unique_ptr<juce::Slider> eqHighMidGainSlider;
    std::unique_ptr<juce::Slider> eqHighFreqSlider;
    std::unique_ptr<juce::Slider> eqHighGainSlider;
    std::unique_ptr<juce::Label> eqLowLabel;
    std::unique_ptr<juce::Label> eqLowMidLabel;
    std::unique_ptr<juce::Label> eqHighMidLabel;
    std::unique_ptr<juce::Label> eqHighLabel;

    // Latency toggle
    std::unique_ptr<juce::ToggleButton> zeroLatencyButton;

    // Meters
    std::unique_ptr<LEDMeter> inputMeter;
    std::unique_ptr<LEDMeter> outputMeter;
    std::unique_ptr<juce::Label> inputMeterLabel;
    std::unique_ptr<juce::Label> outputMeterLabel;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowMidFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowMidGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighMidFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighMidGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> zeroLatencyAttachment;

    // Smoothed meter values
    float smoothedInputLevel = -60.0f;
    float smoothedOutputLevel = -60.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConvolutionReverbEditor)
};

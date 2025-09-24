#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/VUMeter.h"
#include "../../../shared/LunaVintageLookAndFeel.h"

class CustomLookAndFeel : public LunaVintageLookAndFeel
{
public:
    CustomLookAndFeel();
    ~CustomLookAndFeel() override;

    // Inherits drawRotarySlider and drawToggleButton from LunaVintageLookAndFeel
    // Can override if TapeMachine-specific customization is needed
};

class ReelAnimation : public juce::Component, public juce::Timer
{
public:
    ReelAnimation();
    ~ReelAnimation() override;

    void paint(juce::Graphics& g) override;
    void timerCallback() override;
    void setSpeed(float speed);

private:
    float rotation = 0.0f;
    float rotationSpeed = 1.0f;
};

// VUMeter class is now imported from GUI/VUMeter.h

class TapeMachineAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    TapeMachineAudioProcessorEditor (TapeMachineAudioProcessor&);
    ~TapeMachineAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    TapeMachineAudioProcessor& audioProcessor;
    CustomLookAndFeel customLookAndFeel;

    juce::ComboBox tapeMachineSelector;
    juce::ComboBox tapeSpeedSelector;
    juce::ComboBox tapeTypeSelector;

    juce::Slider inputGainSlider;
    juce::Slider saturationSlider;
    juce::Slider biasSlider;
    juce::Slider highpassFreqSlider;
    juce::Slider lowpassFreqSlider;
    juce::Slider noiseAmountSlider;
    juce::Slider wowFlutterSlider;
    juce::Slider outputGainSlider;

    juce::ToggleButton noiseEnabledButton;

    juce::Label tapeMachineLabel;
    juce::Label tapeSpeedLabel;
    juce::Label tapeTypeLabel;
    juce::Label inputGainLabel;
    juce::Label saturationLabel;
    juce::Label biasLabel;
    juce::Label highpassFreqLabel;
    juce::Label lowpassFreqLabel;
    juce::Label noiseAmountLabel;
    juce::Label wowFlutterLabel;
    juce::Label outputGainLabel;

    ReelAnimation leftReel;
    ReelAnimation rightReel;

    VUMeter mainVUMeter;  // Single stereo VU meter at top

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeMachineAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeTypeAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> saturationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> biasAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highpassFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowpassFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wowFlutterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> noiseEnabledAttachment;

    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupComboBox(juce::ComboBox& combo, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeMachineAudioProcessorEditor)
};
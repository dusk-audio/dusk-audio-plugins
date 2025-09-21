#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();
    ~CustomLookAndFeel() override;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    juce::Colour backgroundColour;
    juce::Colour knobColour;
    juce::Colour pointerColour;
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

class VUMeter : public juce::Component, public juce::Timer
{
public:
    VUMeter();
    ~VUMeter() override;

    void paint(juce::Graphics& g) override;
    void timerCallback() override;
    void setLevels(float leftLevel, float rightLevel);
    void setPeakLevels(float leftPeak, float rightPeak);

private:
    float currentLevelL = 0.0f;
    float currentLevelR = 0.0f;
    float targetLevelL = 0.0f;
    float targetLevelR = 0.0f;
    float peakLevelL = 0.0f;
    float peakLevelR = 0.0f;
    float peakHoldTimeL = 0.0f;
    float peakHoldTimeR = 0.0f;

    float smoothedLevelL = 0.0f;
    float smoothedLevelR = 0.0f;
    const float smoothingFactor = 0.85f;

    void drawSingleVUMeter(juce::Graphics& g, juce::Rectangle<float> bounds);
};

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
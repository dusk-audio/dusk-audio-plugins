#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/AnalogVUMeter.h"
#include "../../../shared/LunaVintageLookAndFeel.h"

class CustomLookAndFeel : public LunaVintageLookAndFeel
{
public:
    CustomLookAndFeel();
    ~CustomLookAndFeel() override;

    // TapeMachine-specific knob and button styling
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

class ReelAnimation : public juce::Component, public juce::Timer
{
public:
    ReelAnimation();
    ~ReelAnimation() override;

    void paint(juce::Graphics& g) override;
    void timerCallback() override;
    void setSpeed(float speed);

    // Tape amount: 0.0 = empty reel, 1.0 = full reel
    void setTapeAmount(float amount);
    float getTapeAmount() const { return tapeAmount; }

    // Set whether this is the supply (left) or take-up (right) reel
    void setIsSupplyReel(bool isSupply) { isSupplyReel = isSupply; }

private:
    float rotation = 0.0f;
    float rotationSpeed = 1.0f;
    float tapeAmount = 0.5f;      // Current tape on this reel (0-1)
    bool isSupplyReel = true;     // Supply reel starts full, take-up starts empty
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
    juce::Slider biasSlider;
    juce::Slider highpassFreqSlider;
    juce::Slider lowpassFreqSlider;
    juce::Slider noiseAmountSlider;
    juce::Slider wowFlutterSlider;
    juce::Slider outputGainSlider;

    juce::ToggleButton noiseEnabledButton;
    juce::ToggleButton autoCompButton;

    juce::Label tapeMachineLabel;
    juce::Label tapeSpeedLabel;
    juce::Label tapeTypeLabel;
    juce::Label inputGainLabel;
    juce::Label biasLabel;
    juce::Label highpassFreqLabel;
    juce::Label lowpassFreqLabel;
    juce::Label noiseAmountLabel;
    juce::Label wowFlutterLabel;
    juce::Label outputGainLabel;

    ReelAnimation leftReel;
    ReelAnimation rightReel;

    AnalogVUMeter mainVUMeter;  // Professional analog VU meter with dual needles

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeMachineAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeTypeAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> biasAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highpassFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowpassFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wowFlutterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> noiseEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoCompAttachment;

    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupComboBox(juce::ComboBox& combo, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeMachineAudioProcessorEditor)
};
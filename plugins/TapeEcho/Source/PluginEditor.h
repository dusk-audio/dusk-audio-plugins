#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "../../../shared/LunaVintageLookAndFeel.h"
#include "GUI/VUMeter.h"
#include "GUI/ModeSelector.h"

class VintageKnobLookAndFeel : public LunaVintageLookAndFeel
{
public:
    VintageKnobLookAndFeel();
    ~VintageKnobLookAndFeel() override = default;

    // Inherits drawRotarySlider from LunaVintageLookAndFeel
    // Can override if TapeEcho-specific customization is needed
};

class TapeEchoEditor : public juce::AudioProcessorEditor,
                       public juce::Timer,
                       public juce::ComboBox::Listener
{
public:
    TapeEchoEditor(TapeEchoProcessor&);
    ~TapeEchoEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void comboBoxChanged(juce::ComboBox* comboBox) override;

private:
    TapeEchoProcessor& audioProcessor;

    VintageKnobLookAndFeel knobLookAndFeel;

    // Custom components
    VUMeter vuMeter;
    ModeSelector modeSelector;

    // Main controls
    juce::Slider repeatRateKnob;
    juce::Slider intensityKnob;
    juce::Slider echoVolumeKnob;
    juce::Slider reverbVolumeKnob;
    juce::Slider bassKnob;
    juce::Slider trebleKnob;
    juce::Slider inputVolumeKnob;

    // Extended controls
    juce::Slider wowFlutterSlider;
    juce::Slider tapeAgeSlider;
    juce::Slider motorTorqueSlider;
    juce::ToggleButton stereoModeButton;

    // Labels
    juce::Label repeatRateLabel;
    juce::Label intensityLabel;
    juce::Label echoVolumeLabel;
    juce::Label reverbVolumeLabel;
    juce::Label bassLabel;
    juce::Label trebleLabel;
    juce::Label inputVolumeLabel;
    juce::Label wowFlutterLabel;
    juce::Label tapeAgeLabel;
    juce::Label motorTorqueLabel;

    // Preset selector
    juce::ComboBox presetSelector;
    juce::Label presetLabel;

    // Appearance toggle
    juce::ToggleButton vintageToggle;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> repeatRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> intensityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> echoVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trebleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wowFlutterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tapeAgeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> motorTorqueAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoModeAttachment;

    bool isVintageMode = true;

    void setupControls();
    void setupLabels();
    void updateAppearance();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoEditor)
};
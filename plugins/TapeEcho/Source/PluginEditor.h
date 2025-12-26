/*
  ==============================================================================

    RE-201 Space Echo - Plugin Editor
    Authentic Roland RE-201 styling
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/RE201LookAndFeel.h"
#include "ui/ToggleSwitch.h"
#include "GUI/VUMeter.h"
#include "GUI/ModeSelector.h"

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

    RE201LookAndFeel lookAndFeel;

    // Custom components
    VUMeter vuMeter;
    ModeSelector modeSelector;

    // Main chrome knobs (like RE-201 hardware)
    juce::Slider repeatRateKnob;      // REPEAT RATE
    juce::Slider intensityKnob;       // INTENSITY
    juce::Slider echoVolumeKnob;      // ECHO VOLUME
    juce::Slider reverbVolumeKnob;    // REVERB VOLUME
    juce::Slider bassKnob;            // BASS
    juce::Slider trebleKnob;          // TREBLE
    juce::Slider inputVolumeKnob;     // INPUT VOLUME

    // Extended controls (smaller knobs on lower section)
    juce::Slider wowFlutterKnob;
    juce::Slider tapeAgeKnob;
    juce::Slider motorTorqueKnob;

    // Toggle switches
    ToggleSwitch stereoSwitch;

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

    void setupControls();
    void setupLabels();

    // Layout helpers
    void layoutKnobWithLabel(juce::Slider& knob, juce::Label& label,
                              juce::Rectangle<int> area, int labelHeight, int knobSize);

    // Drawing helpers - 3-layer hardware emulation
    void drawBrushedAluminum(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawBlackFrame(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawGreenPanel(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawCornerScrews(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawLogoAndTitle(juce::Graphics& g, juce::Rectangle<float> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoEditor)
};

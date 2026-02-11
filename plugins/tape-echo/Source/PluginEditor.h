/*
  ==============================================================================

    PluginEditor.h
    Tape Echo - RE-201 Style Plugin UI

    Modern, minimal UI design with deep forest green color scheme.
    Features animated tape visualization and mode selector grid.

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/TapeEchoLookAndFeel.h"
#include "UI/TapeVisualization.h"
#include "../../shared/LEDMeter.h"
#include "../../shared/DuskLookAndFeel.h"
#include "../../shared/ScalableEditorHelper.h"

class TapeEchoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TapeEchoEditor(TapeEchoProcessor&);
    ~TapeEchoEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    TapeEchoProcessor& processor;
    TapeEchoLookAndFeel lookAndFeel;

    // Resizable UI helper (shared across all Dusk Audio plugins)
    ScalableEditorHelper resizeHelper;

    // Tape visualization
    TapeVisualization tapeViz;

    // Main knobs (DuskSlider for Cmd/Ctrl+drag fine control)
    DuskSlider inputKnob;
    DuskSlider repeatRateKnob;
    DuskSlider intensityKnob;
    DuskSlider echoVolumeKnob;
    DuskSlider reverbVolumeKnob;

    // Secondary knobs
    DuskSlider bassKnob;
    DuskSlider trebleKnob;
    DuskSlider wowFlutterKnob;
    DuskSlider dryWetKnob;

    // Tempo sync controls
    juce::ToggleButton tempoSyncButton { "SYNC" };
    DuskSlider noteDivisionKnob;
    juce::Label noteDivisionLabel;

    // Labels
    juce::Label inputLabel;
    juce::Label repeatRateLabel;
    juce::Label intensityLabel;
    juce::Label echoVolumeLabel;
    juce::Label reverbVolumeLabel;
    juce::Label bassLabel;
    juce::Label trebleLabel;
    juce::Label wowFlutterLabel;
    juce::Label dryWetLabel;

    // Mode selector buttons (12 modes)
    std::array<std::unique_ptr<juce::TextButton>, 12> modeButtons;

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> repeatRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> intensityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> echoVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trebleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wowFlutterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tempoSyncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteDivisionAttachment;

    // Level meters (using shared LEDMeter with stereo mode)
    std::unique_ptr<LEDMeter> inputMeter;
    std::unique_ptr<LEDMeter> outputMeter;

    void timerCallback() override;
    void setupKnob(juce::Slider& knob, juce::Label& label, const juce::String& text);
    void updateModeButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoEditor)
};

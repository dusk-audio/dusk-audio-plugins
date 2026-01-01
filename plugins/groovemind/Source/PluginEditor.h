/*
  ==============================================================================

    GrooveMind - ML-Powered Intelligent Drummer
    PluginEditor.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "XYPad.h"

//==============================================================================
class GrooveMindEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    GrooveMindEditor(GrooveMindProcessor&);
    ~GrooveMindEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    GrooveMindProcessor& processor;

    // Style and drummer selection
    juce::ComboBox styleSelector;
    juce::ComboBox drummerSelector;
    juce::ComboBox kitSelector;
    juce::ComboBox sectionSelector;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> drummerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> kitAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sectionAttachment;

    // XY Pad for complexity/loudness
    XYPad xyPad;

    // Sliders
    juce::Slider energySlider;
    juce::Slider grooveSlider;
    juce::Slider swingSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> energyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grooveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAttachment;

    // Fill controls
    juce::ComboBox fillModeSelector;
    juce::Slider fillIntensitySlider;
    juce::TextButton fillTriggerButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fillModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fillIntensityAttachment;

    // Instrument toggles
    juce::ToggleButton kickToggle;
    juce::ToggleButton snareToggle;
    juce::ToggleButton hihatToggle;
    juce::ToggleButton tomsToggle;
    juce::ToggleButton cymbalsToggle;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kickAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> snareAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hihatAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tomsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cymbalsAttachment;

    // Follow mode
    juce::ToggleButton followToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> followAttachment;

    // Labels
    juce::Label styleLabel;
    juce::Label drummerLabel;
    juce::Label kitLabel;
    juce::Label sectionLabel;
    juce::Label energyLabel;
    juce::Label grooveLabel;
    juce::Label swingLabel;

    // Transport display
    juce::Label transportLabel;
    juce::Label bpmLabel;

    // Pattern library status
    juce::Label patternCountLabel;
    juce::Label currentPatternLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMindEditor)
};

/*
  ==============================================================================

    Plate Reverb - Plugin Editor
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    Custom look and feel for Plate Reverb
*/
class PlateReverbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PlateReverbLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;

private:
    juce::Colour backgroundColour;
    juce::Colour knobColour;
    juce::Colour trackColour;
    juce::Colour textColour;
};

//==============================================================================
/**
    Main plugin editor class
*/
class PlateReverbAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    PlateReverbAudioProcessorEditor(PlateReverbAudioProcessor&);
    ~PlateReverbAudioProcessorEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    // Reference to processor
    PlateReverbAudioProcessor& audioProcessor;

    // Look and feel
    PlateReverbLookAndFeel lookAndFeel;

    // UI Components
    juce::Slider sizeSlider;
    juce::Slider decaySlider;
    juce::Slider dampingSlider;
    juce::Slider predelaySlider;
    juce::Slider widthSlider;
    juce::Slider mixSlider;

    juce::Label sizeLabel;
    juce::Label decayLabel;
    juce::Label dampingLabel;
    juce::Label predelayLabel;
    juce::Label widthLabel;
    juce::Label mixLabel;

    // Value labels
    juce::Label sizeValueLabel;
    juce::Label decayValueLabel;
    juce::Label dampingValueLabel;
    juce::Label predelayValueLabel;
    juce::Label widthValueLabel;
    juce::Label mixValueLabel;

    // Attachments for parameter binding
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> predelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    // Helper methods
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText);
    void updateValueLabels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlateReverbAudioProcessorEditor)
};

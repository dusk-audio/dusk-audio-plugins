/*
  ==============================================================================

    Studio Verb - Plugin Editor
    Copyright (c) 2024 Luna CO. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    Custom look and feel for Studio Verb
*/
class StudioVerbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioVerbLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

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
class StudioVerbAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::ComboBox::Listener,
                                        private juce::Timer
{
public:
    StudioVerbAudioProcessorEditor(StudioVerbAudioProcessor&);
    ~StudioVerbAudioProcessorEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void timerCallback() override;

private:
    // Reference to processor
    StudioVerbAudioProcessor& audioProcessor;

    // Look and feel
    StudioVerbLookAndFeel lookAndFeel;

    // UI Components
    juce::ComboBox algorithmSelector;
    juce::ComboBox presetSelector;

    juce::Slider sizeSlider;
    juce::Slider dampSlider;
    juce::Slider predelaySlider;
    juce::Slider mixSlider;

    juce::Label algorithmLabel;
    juce::Label presetLabel;
    juce::Label sizeLabel;
    juce::Label dampLabel;
    juce::Label predelayLabel;
    juce::Label mixLabel;

    // Value labels (showing current values)
    juce::Label sizeValueLabel;
    juce::Label dampValueLabel;
    juce::Label predelayValueLabel;
    juce::Label mixValueLabel;

    // Attachments for parameter binding
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> algorithmAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> predelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    // UI State
    int lastAlgorithm = -1;

    // Helper methods
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText);
    void updatePresetList();
    void updateValueLabels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioVerbAudioProcessorEditor)
};
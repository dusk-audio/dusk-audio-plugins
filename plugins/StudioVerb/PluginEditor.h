/*
  ==============================================================================

    Studio Verb - Plugin Editor
    Copyright (c) 2024 Luna Co. Audio

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

    // UI Components - Main
    juce::ComboBox algorithmSelector;
    juce::ComboBox presetSelector;

    juce::Slider sizeSlider;
    juce::Slider dampSlider;
    juce::Slider predelaySlider;
    juce::Slider mixSlider;
    juce::Slider widthSlider;

    juce::Label algorithmLabel;
    juce::Label presetLabel;
    juce::Label sizeLabel;
    juce::Label dampLabel;
    juce::Label predelayLabel;
    juce::Label mixLabel;
    juce::Label widthLabel;

    // Advanced parameters
    juce::Slider lowRT60Slider;
    juce::Slider midRT60Slider;
    juce::Slider highRT60Slider;
    juce::ToggleButton infiniteButton;
    juce::ComboBox oversamplingSelector;

    juce::Label lowRT60Label;
    juce::Label midRT60Label;
    juce::Label highRT60Label;
    juce::Label infiniteLabel;
    juce::Label oversamplingLabel;
    juce::Label advancedSectionLabel;

    // Room shape selector
    juce::ComboBox roomShapeSelector;
    juce::Label roomShapeLabel;

    // Vintage and tempo sync
    juce::Slider vintageSlider;
    juce::ComboBox predelayBeatsSelector;
    juce::ComboBox colorModeSelector;
    juce::Label vintageLabel;
    juce::Label predelayBeatsLabel;
    juce::Label colorModeLabel;

    // Modulation controls
    juce::Slider modRateSlider;
    juce::Slider modDepthSlider;
    juce::Label modRateLabel;
    juce::Label modDepthLabel;

    // Noise control
    juce::Slider noiseAmountSlider;
    juce::Label noiseAmountLabel;

    // Bass controls
    juce::Slider bassMultSlider;
    juce::Slider bassXoverSlider;
    juce::Label bassMultLabel;
    juce::Label bassXoverLabel;

    // Quality selector
    juce::ComboBox qualitySelector;
    juce::Label qualityLabel;

    // Value labels (showing current values)
    juce::Label sizeValueLabel;
    juce::Label dampValueLabel;
    juce::Label predelayValueLabel;
    juce::Label mixValueLabel;
    juce::Label widthValueLabel;
    juce::Label lowRT60ValueLabel;
    juce::Label midRT60ValueLabel;
    juce::Label highRT60ValueLabel;
    juce::Label vintageValueLabel;
    juce::Label modRateValueLabel;
    juce::Label modDepthValueLabel;
    juce::Label noiseAmountValueLabel;
    juce::Label bassMultValueLabel;
    juce::Label bassXoverValueLabel;

    // Attachments for parameter binding
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> algorithmAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> predelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;

    // Advanced parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowRT60Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> midRT60Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highRT60Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> infiniteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> roomShapeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> vintageAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> predelayBeatsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> colorModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassMultAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassXoverAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment;

    // UI State
    int lastAlgorithm = -1;
    float uiScale = 1.0f;  // UI scale factor for high-DPI displays

    // Helper methods
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText);
    void updatePresetList();
    void updateValueLabels();
    int scaled(int value) const { return static_cast<int>(value * uiScale); }
    float scaledf(float value) const { return value * uiScale; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioVerbAudioProcessorEditor)
};
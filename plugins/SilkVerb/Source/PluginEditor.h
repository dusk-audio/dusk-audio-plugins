/*
  ==============================================================================

    PluginEditor.h
    SilkVerb - Algorithmic Reverb with Plate, Room, Hall modes

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "../../shared/LunaLookAndFeel.h"

//==============================================================================
// Custom look and feel for SilkVerb matching Luna plugin style
class SilkVerbLookAndFeel : public LunaLookAndFeel
{
public:
    SilkVerbLookAndFeel();
    ~SilkVerbLookAndFeel() override = default;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void setFreezeButton(juce::ToggleButton* button) { freezeButtonPtr = button; }

private:
    juce::ToggleButton* freezeButtonPtr = nullptr;
};

//==============================================================================
class SilkVerbEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit SilkVerbEditor(SilkVerbProcessor&);
    ~SilkVerbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    SilkVerbProcessor& audioProcessor;
    SilkVerbLookAndFeel lookAndFeel;

    // Mode toggle buttons (Plate/Room/Hall)
    juce::ToggleButton plateButton;
    juce::ToggleButton roomButton;
    juce::ToggleButton hallButton;

    // Color toggle buttons (Modern/Vintage)
    juce::ToggleButton modernButton;
    juce::ToggleButton vintageButton;

    // Freeze toggle button
    juce::ToggleButton freezeButton;

    // Main parameter sliders (Row 1)
    juce::Slider sizeSlider;
    juce::Slider dampingSlider;
    juce::Slider preDelaySlider;
    juce::Slider mixSlider;

    // Modulation sliders (Row 2)
    juce::Slider modRateSlider;
    juce::Slider modDepthSlider;
    juce::Slider widthSlider;

    // Diffusion sliders (Row 3)
    juce::Slider earlyDiffSlider;
    juce::Slider lateDiffSlider;

    // Bass control sliders (Row 3)
    juce::Slider bassMultSlider;
    juce::Slider bassFreqSlider;

    // EQ sliders (Row 4)
    juce::Slider lowCutSlider;
    juce::Slider highCutSlider;

    // Labels
    juce::Label sizeLabel;
    juce::Label dampingLabel;
    juce::Label preDelayLabel;
    juce::Label mixLabel;
    juce::Label modRateLabel;
    juce::Label modDepthLabel;
    juce::Label widthLabel;
    juce::Label earlyDiffLabel;
    juce::Label lateDiffLabel;
    juce::Label bassMultLabel;
    juce::Label bassFreqLabel;
    juce::Label lowCutLabel;
    juce::Label highCutLabel;

    // Section labels
    juce::Label mainSectionLabel;
    juce::Label modSectionLabel;
    juce::Label diffusionSectionLabel;
    juce::Label eqSectionLabel;

    // Attachments - Main
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    // Attachments - Modulation
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;

    // Attachments - Diffusion/Bass
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> earlyDiffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lateDiffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassMultAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassFreqAttachment;

    // Attachments - EQ
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;

    // Attachment - Freeze
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    // Mode button group handling
    void updateModeButtons();
    void modeButtonClicked(int mode);

    // Color button group handling
    void updateColorButtons();
    void colorButtonClicked(int color);

    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupSmallSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilkVerbEditor)
};

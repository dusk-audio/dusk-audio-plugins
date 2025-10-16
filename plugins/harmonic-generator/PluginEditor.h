#pragma once

#include "PluginProcessor.h"
#include "../shared/LunaLookAndFeel.h"
#include <juce_gui_extra/juce_gui_extra.h>

class HarmonicGeneratorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer,
                                             private juce::ComboBox::Listener
{
public:
    HarmonicGeneratorAudioProcessorEditor(HarmonicGeneratorAudioProcessor&);
    ~HarmonicGeneratorAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    // Level meter component
    class LevelMeter : public juce::Component
    {
    public:
        LevelMeter();
        void paint(juce::Graphics& g) override;
        void setStereoLevels(float left, float right);

    private:
        float levelL = 0.0f;
        float levelR = 0.0f;
        float smoothedLevelL = 0.0f;
        float smoothedLevelR = 0.0f;
    };

    void comboBoxChanged(juce::ComboBox* comboBox) override;
    void updateControlsVisibility();

    HarmonicGeneratorAudioProcessor& audioProcessor;
    LunaLookAndFeel customLookAndFeel;

    // Hardware Mode Selector (NEW!)
    juce::ComboBox hardwareModeSelector;
    juce::Label hardwareModeLabel;

    // Main controls (always visible)
    juce::Slider driveSlider;
    juce::Slider outputGainSlider;
    juce::Slider mixSlider;
    juce::Slider toneSlider;

    juce::Label driveLabel;
    juce::Label outputGainLabel;
    juce::Label mixLabel;
    juce::Label toneLabel;

    // Custom Mode Controls (only visible when mode == Custom)
    juce::Slider secondHarmonicSlider;
    juce::Slider thirdHarmonicSlider;
    juce::Slider fourthHarmonicSlider;
    juce::Slider fifthHarmonicSlider;
    juce::Slider evenHarmonicsSlider;
    juce::Slider oddHarmonicsSlider;
    juce::Slider warmthSlider;
    juce::Slider brightnessSlider;

    juce::Label secondHarmonicLabel;
    juce::Label thirdHarmonicLabel;
    juce::Label fourthHarmonicLabel;
    juce::Label fifthHarmonicLabel;
    juce::Label evenHarmonicsLabel;
    juce::Label oddHarmonicsLabel;
    juce::Label warmthLabel;
    juce::Label brightnessLabel;

    // Visual displays
    LevelMeter inputMeter;
    LevelMeter outputMeter;

    // Parameter attachments (using APVTS)
    std::unique_ptr<juce::ComboBoxParameterAttachment> hardwareModeAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> driveAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> outputGainAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> mixAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> toneAttachment;

    // Custom mode attachments
    std::unique_ptr<juce::SliderParameterAttachment> secondHarmonicAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> thirdHarmonicAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> fourthHarmonicAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> fifthHarmonicAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> evenHarmonicsAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> oddHarmonicsAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> warmthAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> brightnessAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicGeneratorAudioProcessorEditor)
};

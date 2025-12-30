#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class NeuralAmpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeuralAmpLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

private:
    juce::Colour ampGold{0xFFD4A84B};
    juce::Colour ampBrown{0xFF2A2018};
    juce::Colour ampBlack{0xFF1A1A1A};
    juce::Colour ledGreen{0xFF00FF00};
    juce::Colour ledRed{0xFFFF3300};
};

class NeuralAmpAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       public juce::Timer
{
public:
    NeuralAmpAudioProcessorEditor(NeuralAmpAudioProcessor&);
    ~NeuralAmpAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    NeuralAmpAudioProcessor& audioProcessor;
    NeuralAmpLookAndFeel lookAndFeel;

    // Model section
    juce::Label modelLabel;
    juce::Label modelNameLabel;
    juce::TextButton loadModelButton;

    // IR section
    juce::Label irLabel;
    juce::Label irNameLabel;
    juce::TextButton loadIRButton;

    // Input controls
    juce::Slider inputGainSlider;
    juce::Label inputGainLabel;
    juce::Slider gateSlider;
    juce::Label gateLabel;
    juce::ToggleButton gateButton;

    // Tone stack
    juce::Slider bassSlider;
    juce::Label bassLabel;
    juce::Slider midSlider;
    juce::Label midLabel;
    juce::Slider trebleSlider;
    juce::Label trebleLabel;

    // Output controls
    juce::Slider lowCutSlider;
    juce::Label lowCutLabel;
    juce::Slider highCutSlider;
    juce::Label highCutLabel;
    juce::Slider outputSlider;
    juce::Label outputLabel;
    juce::ToggleButton cabButton;

    // Bypass
    juce::ToggleButton bypassButton;

    // Meters
    float inputMeterLevel = 0.0f;
    float outputMeterLevel = 0.0f;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> midAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trebleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    void loadModel();
    void loadIR();
    void drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float level);

    // File choosers must be member variables to stay alive during async callbacks
    std::unique_ptr<juce::FileChooser> modelChooser;
    std::unique_ptr<juce::FileChooser> irChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeuralAmpAudioProcessorEditor)
};

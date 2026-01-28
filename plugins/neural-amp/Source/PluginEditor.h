#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "../../shared/LEDMeter.h"
#include "../../shared/SupportersOverlay.h"
#include "../../shared/LunaLookAndFeel.h"

class NeuralAmpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeuralAmpLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    // Colors
    juce::Colour ampGold{0xFFD4A84B};
    juce::Colour ampCream{0xFFE8DCC8};
    juce::Colour ampBrown{0xFF2A2018};
    juce::Colour ampDarkBrown{0xFF1A1410};
    juce::Colour ampBlack{0xFF0A0A0A};
    juce::Colour chickenHeadCream{0xFFE8E0D0};
    juce::Colour ledGreen{0xFF00DD00};
    juce::Colour ledRed{0xFFFF3300};
    juce::Colour ledOff{0xFF333333};
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
    void mouseDown(const juce::MouseEvent& e) override;

private:
    NeuralAmpAudioProcessor& audioProcessor;
    NeuralAmpLookAndFeel lookAndFeel;

    // Model section
    juce::Label modelNameLabel;
    juce::TextButton loadModelButton;

    // IR section
    juce::Label irNameLabel;
    juce::TextButton loadIRButton;

    // Input controls (LunaSlider for Cmd/Ctrl+drag fine control)
    LunaSlider inputGainSlider;
    juce::Label inputGainLabel;
    LunaSlider gateSlider;
    juce::Label gateLabel;
    juce::ToggleButton gateButton;

    // Tone stack
    LunaSlider bassSlider;
    juce::Label bassLabel;
    LunaSlider midSlider;
    juce::Label midLabel;
    LunaSlider trebleSlider;
    juce::Label trebleLabel;
    LunaSlider presenceSlider;
    juce::Label presenceLabel;

    // Filter controls
    LunaSlider lowCutSlider;
    juce::Label lowCutLabel;
    LunaSlider highCutSlider;
    juce::Label highCutLabel;

    // Output controls
    LunaSlider outputSlider;
    juce::Label outputLabel;
    juce::ToggleButton cabButton;

    // Bypass
    juce::ToggleButton bypassButton;

    // Meters - using shared LEDMeter component
    LEDMeter inputMeter{LEDMeter::Vertical};
    LEDMeter outputMeter{LEDMeter::Vertical};
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

    // File choosers must be member variables to stay alive during async callbacks
    std::unique_ptr<juce::FileChooser> modelChooser;
    std::unique_ptr<juce::FileChooser> irChooser;

    // Patreon supporters overlay
    std::unique_ptr<SupportersOverlay> supportersOverlay;
    juce::Rectangle<int> titleClickArea;
    void showSupportersPanel();
    void hideSupportersPanel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeuralAmpAudioProcessorEditor)
};

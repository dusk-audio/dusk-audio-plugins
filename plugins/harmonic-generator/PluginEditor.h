#pragma once

#include "PluginProcessor.h"
#include "../../shared/LunaLookAndFeel.h"
#include <juce_gui_extra/juce_gui_extra.h>

class HarmonicGeneratorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer,
                                             private juce::Slider::Listener,
                                             private juce::Button::Listener,
                                             private juce::ComboBox::Listener
{
public:
    HarmonicGeneratorAudioProcessorEditor(HarmonicGeneratorAudioProcessor&);
    ~HarmonicGeneratorAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    // Custom look and feel for analog-style interface
    class AnalogLookAndFeel : public LunaLookAndFeel
    {
    public:
        AnalogLookAndFeel();
        ~AnalogLookAndFeel() override;

        // Most drawing is inherited from LunaLookAndFeel
        // Only override if specific customization is needed

        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPos, float minSliderPos, float maxSliderPos,
                            const juce::Slider::SliderStyle style, juce::Slider& slider) override;
    };

    // Spectrum analyzer display
    class SpectrumDisplay : public juce::Component
    {
    public:
        SpectrumDisplay();
        void paint(juce::Graphics& g) override;
        void updateSpectrum(const std::array<float, 5>& harmonics);

    private:
        std::array<float, 5> harmonicLevels = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        std::array<float, 5> smoothedLevels = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    };

    // Level meter component
    class LevelMeter : public juce::Component
    {
    public:
        LevelMeter();
        void paint(juce::Graphics& g) override;
        void setLevel(float newLevel);
        void setStereoLevels(float left, float right);

    private:
        float levelL = 0.0f;
        float levelR = 0.0f;
        float smoothedLevelL = 0.0f;
        float smoothedLevelR = 0.0f;
        bool stereo = false;
    };

    void sliderValueChanged(juce::Slider* slider) override;
    void buttonClicked(juce::Button* button) override;
    void comboBoxChanged(juce::ComboBox* comboBox) override;
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text,
                    juce::Slider::SliderStyle style = juce::Slider::RotaryVerticalDrag);

    HarmonicGeneratorAudioProcessor& audioProcessor;
    AnalogLookAndFeel customLookAndFeel;

    // Main harmonic controls
    juce::Slider secondHarmonicSlider;
    juce::Slider thirdHarmonicSlider;
    juce::Slider fourthHarmonicSlider;
    juce::Slider fifthHarmonicSlider;

    juce::Label secondHarmonicLabel;
    juce::Label thirdHarmonicLabel;
    juce::Label fourthHarmonicLabel;
    juce::Label fifthHarmonicLabel;

    // Global harmonic controls
    juce::Slider evenHarmonicsSlider;
    juce::Slider oddHarmonicsSlider;
    juce::Label evenHarmonicsLabel;
    juce::Label oddHarmonicsLabel;

    // Character controls
    juce::Slider warmthSlider;
    juce::Slider brightnessSlider;
    juce::Label warmthLabel;
    juce::Label brightnessLabel;

    // Input/Output controls
    juce::Slider driveSlider;
    juce::Slider outputGainSlider;
    juce::Slider mixSlider;

    juce::Label driveLabel;
    juce::Label outputGainLabel;
    juce::Label mixLabel;

    // Oversampling switch
    juce::ToggleButton oversamplingButton;

    // Preset selector
    juce::ComboBox presetSelector;
    juce::Label presetLabel;

    // Visual displays
    SpectrumDisplay spectrumDisplay;
    LevelMeter inputMeter;
    LevelMeter outputMeter;

    // Parameter attachments
    std::unique_ptr<juce::SliderParameterAttachment> secondHarmonicAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> thirdHarmonicAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> fourthHarmonicAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> fifthHarmonicAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> evenHarmonicsAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> oddHarmonicsAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> warmthAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> brightnessAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> driveAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> outputGainAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> mixAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> oversamplingAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicGeneratorAudioProcessorEditor)
};
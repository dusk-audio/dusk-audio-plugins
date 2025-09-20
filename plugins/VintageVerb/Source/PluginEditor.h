/*
  ==============================================================================

    VintageVerb - Plugin Editor Header

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class VintageVerbAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer,
                                        private juce::ComboBox::Listener
{
public:
    VintageVerbAudioProcessorEditor (VintageVerbAudioProcessor&);
    ~VintageVerbAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void comboBoxChanged (juce::ComboBox* comboBoxThatHasChanged) override;

private:
    // Custom look and feel
    class VintageVerbLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        VintageVerbLookAndFeel();

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                              juce::Slider& slider) override;

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float minSliderPos, float maxSliderPos,
                              const juce::Slider::SliderStyle style, juce::Slider& slider) override;
    };

    // Level meter component
    class LevelMeter : public juce::Component
    {
    public:
        LevelMeter();
        void paint (juce::Graphics&) override;
        void setLevel (float newLevel);

    private:
        float level = 0.0f;
        float smoothedLevel = 0.0f;
    };

    // Reverb visualizer
    class ReverbVisualizer : public juce::Component
    {
    public:
        ReverbVisualizer();
        void paint (juce::Graphics&) override;
        void updateDisplay (float size, float damping, float diffusion);

    private:
        float currentSize = 0.5f;
        float currentDamping = 0.5f;
        float currentDiffusion = 0.5f;
        juce::Path responseCurve;
    };

    VintageVerbAudioProcessor& audioProcessor;
    VintageVerbLookAndFeel customLookAndFeel;

    // Main controls
    juce::Slider mixSlider;
    juce::Slider sizeSlider;
    juce::Slider attackSlider;
    juce::Slider dampingSlider;
    juce::Slider predelaySlider;
    juce::Slider widthSlider;
    juce::Slider modulationSlider;

    // EQ controls
    juce::Slider bassFreqSlider;
    juce::Slider bassMulSlider;
    juce::Slider highFreqSlider;
    juce::Slider highMulSlider;

    // Advanced controls
    juce::Slider densitySlider;
    juce::Slider diffusionSlider;
    juce::Slider shapeSlider;
    juce::Slider spreadSlider;

    // Mode selectors
    juce::ComboBox reverbModeSelector;
    juce::ComboBox colorModeSelector;
    juce::ComboBox routingModeSelector;
    juce::Slider engineMixSlider;

    // Filter controls
    juce::Slider hpfFreqSlider;
    juce::Slider lpfFreqSlider;
    juce::Slider tiltGainSlider;

    // Gain controls
    juce::Slider inputGainSlider;
    juce::Slider outputGainSlider;

    // Preset management
    juce::ComboBox presetSelector;
    juce::TextButton savePresetButton;
    juce::TextButton loadPresetButton;

    // Meters and visualizers
    LevelMeter inputMeterL, inputMeterR;
    LevelMeter outputMeterL, outputMeterR;
    ReverbVisualizer reverbDisplay;

    // Labels
    juce::Label mixLabel, sizeLabel, attackLabel, dampingLabel;
    juce::Label predelayLabel, widthLabel, modulationLabel;
    juce::Label bassFreqLabel, bassMulLabel, highFreqLabel, highMulLabel;
    juce::Label densityLabel, diffusionLabel, shapeLabel, spreadLabel;
    juce::Label reverbModeLabel, colorModeLabel, routingModeLabel, engineMixLabel;
    juce::Label hpfFreqLabel, lpfFreqLabel, tiltGainLabel;
    juce::Label inputGainLabel, outputGainLabel;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> predelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modulationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bassMulAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highMulAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> densityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> shapeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spreadAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> reverbModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> colorModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> routingModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> engineMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hpfFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lpfFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tiltGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;

    // Helper methods
    void setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& text,
                     juce::Slider::SliderStyle style = juce::Slider::RotaryVerticalDrag);
    void loadPreset (int presetIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VintageVerbAudioProcessorEditor)
};
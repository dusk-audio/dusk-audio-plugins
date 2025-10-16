#pragma once

#include <JuceHeader.h>
#include "FourKEQ.h"
#include "FourKLookAndFeel.h"

//==============================================================================
/**
    4K EQ Plugin Editor

    Professional console-style EQ interface
*/
class FourKEQEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit FourKEQEditor(FourKEQ&);
    ~FourKEQEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    //==============================================================================
    // Reference to processor
    FourKEQ& audioProcessor;

    // Look and feel
    FourKLookAndFeel lookAndFeel;

    // HPF Section
    juce::Slider hpfFreqSlider;

    // LPF Section
    juce::Slider lpfFreqSlider;

    // LF Band
    juce::Slider lfGainSlider;
    juce::Slider lfFreqSlider;
    juce::ToggleButton lfBellButton;

    // LM Band
    juce::Slider lmGainSlider;
    juce::Slider lmFreqSlider;
    juce::Slider lmQSlider;

    // HM Band
    juce::Slider hmGainSlider;
    juce::Slider hmFreqSlider;
    juce::Slider hmQSlider;

    // HF Band
    juce::Slider hfGainSlider;
    juce::Slider hfFreqSlider;
    juce::ToggleButton hfBellButton;

    // Global Controls
    juce::ComboBox eqTypeSelector;
    juce::ComboBox presetSelector;
    juce::ToggleButton bypassButton;
    juce::ToggleButton autoGainButton;  // Auto-gain compensation toggle
    juce::Slider outputGainSlider;
    juce::Slider saturationSlider;
    juce::ComboBox oversamplingSelector;

    // Parameter references for UI updates
    std::atomic<float>* eqTypeParam;
    std::atomic<float>* bypassParam;

    // Cached values for timer optimization
    float lastEqType = -1.0f;
    float lastBypass = -1.0f;
    double lastSampleRate = 0.0;

    // Section labels (LF, LMF, HMF, HF, FILTERS)
    juce::Label filtersLabel, lfLabel, lmfLabel, hmfLabel, hfLabel;

    // Parameter labels for each knob (positioned near knobs like SSL)
    juce::Label hpfLabel, lpfLabel;  // Filter labels
    juce::Label lfGainLabel, lfFreqLabel;  // LF band
    juce::Label lmGainLabel, lmFreqLabel, lmQLabel;  // LMF band
    juce::Label hmGainLabel, hmFreqLabel, hmQLabel;  // HMF band
    juce::Label hfGainLabel, hfFreqLabel;  // HF band
    juce::Label outputLabel, satLabel;  // Master section

    // Attachment classes for parameter binding
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // Attachments
    std::unique_ptr<SliderAttachment> hpfFreqAttachment;
    std::unique_ptr<SliderAttachment> lpfFreqAttachment;

    std::unique_ptr<SliderAttachment> lfGainAttachment;
    std::unique_ptr<SliderAttachment> lfFreqAttachment;
    std::unique_ptr<ButtonAttachment> lfBellAttachment;

    std::unique_ptr<SliderAttachment> lmGainAttachment;
    std::unique_ptr<SliderAttachment> lmFreqAttachment;
    std::unique_ptr<SliderAttachment> lmQAttachment;

    std::unique_ptr<SliderAttachment> hmGainAttachment;
    std::unique_ptr<SliderAttachment> hmFreqAttachment;
    std::unique_ptr<SliderAttachment> hmQAttachment;

    std::unique_ptr<SliderAttachment> hfGainAttachment;
    std::unique_ptr<SliderAttachment> hfFreqAttachment;
    std::unique_ptr<ButtonAttachment> hfBellAttachment;

    std::unique_ptr<ComboBoxAttachment> eqTypeAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ButtonAttachment> autoGainAttachment;  // Auto-gain toggle
    std::unique_ptr<SliderAttachment> outputGainAttachment;
    std::unique_ptr<SliderAttachment> saturationAttachment;
    std::unique_ptr<ComboBoxAttachment> oversamplingAttachment;

    // Helper methods
    void setupKnob(juce::Slider& slider, const juce::String& paramID,
                   const juce::String& label, bool centerDetented = false);
    void setupButton(juce::ToggleButton& button, const juce::String& text);
    void drawKnobMarkings(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKEQEditor)
};
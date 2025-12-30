#pragma once

#include <JuceHeader.h>
#include "FourKEQ.h"
#include "FourKLookAndFeel.h"
#include "EQCurveDisplay.h"
#include "../shared/SupportersOverlay.h"
#include "../shared/LEDMeter.h"
#include "../shared/LunaLookAndFeel.h"

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
    void mouseDown(const juce::MouseEvent& e) override;

private:
    //==============================================================================
    // Reference to processor
    FourKEQ& audioProcessor;

    // Look and feel
    FourKLookAndFeel lookAndFeel;

    // HPF Section
    juce::Slider hpfFreqSlider;
    juce::ToggleButton hpfEnableButton;

    // LPF Section
    juce::Slider lpfFreqSlider;
    juce::ToggleButton lpfEnableButton;

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
    juce::Slider inputGainSlider;   // Input gain control
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

    // Displayed meter levels (throttled for readability)
    float displayedInputLevel = -60.0f;
    float displayedOutputLevel = -60.0f;
    int levelDisplayCounter = 0;

    // A/B Comparison
    juce::TextButton abButton;
    bool isStateA = true;  // true = A, false = B
    juce::ValueTree stateA, stateB;  // Stored parameter states
    void toggleAB();
    void copyCurrentToState(juce::ValueTree& state);
    void applyState(const juce::ValueTree& state);

    // Level meters (professional LED-style like Universal Compressor)
    std::unique_ptr<LEDMeter> inputMeterL;
    std::unique_ptr<LEDMeter> outputMeterL;

    // EQ Curve Display
    std::unique_ptr<EQCurveDisplay> eqCurveDisplay;
    juce::TextButton curveCollapseButton;  // Button to collapse/expand EQ curve
    bool isCurveCollapsed = false;  // Track collapse state

    // Value readout labels (show current parameter value below each knob)
    juce::Label hpfValueLabel, lpfValueLabel, inputValueLabel;
    juce::Label lfGainValueLabel, lfFreqValueLabel;
    juce::Label lmGainValueLabel, lmFreqValueLabel, lmQValueLabel;
    juce::Label hmGainValueLabel, hmFreqValueLabel, hmQValueLabel;
    juce::Label hfGainValueLabel, hfFreqValueLabel;
    juce::Label outputValueLabel, satValueLabel;

    // Section labels (LF, LMF, HMF, HF, FILTERS)
    juce::Label filtersLabel, lfLabel, lmfLabel, hmfLabel, hfLabel;

    // Parameter labels for each knob (positioned near knobs like SSL)
    juce::Label hpfLabel, lpfLabel;  // Filter labels
    juce::Label lfGainLabel, lfFreqLabel;  // LF band
    juce::Label lmGainLabel, lmFreqLabel, lmQLabel;  // LMF band
    juce::Label hmGainLabel, hmFreqLabel, hmQLabel;  // HMF band
    juce::Label hfGainLabel, hfFreqLabel;  // HF band
    juce::Label inputLabel, outputLabel, satLabel;  // Input/Output/Drive labels

    // Attachment classes for parameter binding
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // Attachments
    std::unique_ptr<SliderAttachment> hpfFreqAttachment;
    std::unique_ptr<ButtonAttachment> hpfEnableAttachment;
    std::unique_ptr<SliderAttachment> lpfFreqAttachment;
    std::unique_ptr<ButtonAttachment> lpfEnableAttachment;

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
    std::unique_ptr<SliderAttachment> inputGainAttachment;   // Input gain
    std::unique_ptr<SliderAttachment> outputGainAttachment;
    std::unique_ptr<SliderAttachment> saturationAttachment;
    std::unique_ptr<ComboBoxAttachment> oversamplingAttachment;

    // Helper methods
    void setupKnob(juce::Slider& slider, const juce::String& paramID,
                   const juce::String& label, bool centerDetented = false);
    void setupButton(juce::ToggleButton& button, const juce::String& text);
    void setupValueLabel(juce::Label& label);
    void updateValueLabels();
    void drawKnobMarkings(juce::Graphics& g);
    juce::String formatValue(float value, const juce::String& suffix);

    // Patreon supporters overlay (uses shared component)
    std::unique_ptr<SupportersOverlay> supportersOverlay;
    juce::Rectangle<int> titleClickArea;  // Clickable area for "4K EQ" title

    void showSupportersPanel();
    void hideSupportersPanel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKEQEditor)
};
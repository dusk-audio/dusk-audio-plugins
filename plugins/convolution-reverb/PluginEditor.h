/*
  ==============================================================================

    Convolution Reverb - Plugin Editor
    Main UI for the convolution reverb
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ConvolutionReverbLookAndFeel.h"
#include "IRBrowser.h"
#include "IRWaveformDisplay.h"
#include "LEDMeter.h"
#include "../shared/LunaLookAndFeel.h"
#include "../shared/ScalableEditorHelper.h"

class ConvolutionReverbEditor : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private IRBrowser::Listener
{
public:
    ConvolutionReverbEditor(ConvolutionReverbProcessor& processor);
    ~ConvolutionReverbEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    // IRBrowser::Listener
    void irFileSelected(const juce::File& file) override;

    // Setup methods
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText,
                     const juce::String& suffix = "");
    void setupToggleButton(juce::ToggleButton& button, const juce::String& text);
    void updateWaveformDisplay();
    void updateEnvelopeDisplay();
    void updateIRNameLabel();

    ConvolutionReverbProcessor& audioProcessor;
    ConvolutionReverbLookAndFeel lookAndFeel;

    // Resizable UI helper (shared across all Luna plugins)
    ScalableEditorHelper resizeHelper;

    // IR Browser
    std::unique_ptr<IRBrowser> irBrowser;

    // Waveform display
    std::unique_ptr<IRWaveformDisplay> waveformDisplay;

    // IR name label
    std::unique_ptr<juce::Label> irNameLabel;

    // Envelope controls
    std::unique_ptr<juce::Slider> attackSlider;
    std::unique_ptr<juce::Slider> decaySlider;
    std::unique_ptr<juce::Slider> lengthSlider;
    std::unique_ptr<juce::ToggleButton> reverseButton;
    std::unique_ptr<juce::Label> attackLabel;
    std::unique_ptr<juce::Label> decayLabel;
    std::unique_ptr<juce::Label> lengthLabel;

    // Main controls
    std::unique_ptr<juce::Slider> preDelaySlider;
    std::unique_ptr<juce::Slider> widthSlider;
    std::unique_ptr<juce::Slider> mixSlider;
    std::unique_ptr<juce::Label> preDelayLabel;
    std::unique_ptr<juce::Label> widthLabel;
    std::unique_ptr<juce::Label> mixLabel;

    // Filter controls
    std::unique_ptr<juce::Slider> hpfSlider;
    std::unique_ptr<juce::Slider> lpfSlider;
    std::unique_ptr<juce::Label> hpfLabel;
    std::unique_ptr<juce::Label> lpfLabel;

    // EQ controls
    std::unique_ptr<juce::Slider> eqLowFreqSlider;
    std::unique_ptr<juce::Slider> eqLowGainSlider;
    std::unique_ptr<juce::Slider> eqLowMidFreqSlider;
    std::unique_ptr<juce::Slider> eqLowMidGainSlider;
    std::unique_ptr<juce::Slider> eqHighMidFreqSlider;
    std::unique_ptr<juce::Slider> eqHighMidGainSlider;
    std::unique_ptr<juce::Slider> eqHighFreqSlider;
    std::unique_ptr<juce::Slider> eqHighGainSlider;
    std::unique_ptr<juce::Label> eqLowLabel;
    std::unique_ptr<juce::Label> eqLowMidLabel;
    std::unique_ptr<juce::Label> eqHighMidLabel;
    std::unique_ptr<juce::Label> eqHighLabel;

    // Value display labels (show current parameter values below knobs)
    std::unique_ptr<juce::Label> preDelayValueLabel;
    std::unique_ptr<juce::Label> widthValueLabel;
    std::unique_ptr<juce::Label> mixValueLabel;
    std::unique_ptr<juce::Label> attackValueLabel;
    std::unique_ptr<juce::Label> decayValueLabel;
    std::unique_ptr<juce::Label> lengthValueLabel;
    std::unique_ptr<juce::Label> hpfValueLabel;
    std::unique_ptr<juce::Label> lpfValueLabel;
    std::unique_ptr<juce::Label> eqLowValueLabel;
    std::unique_ptr<juce::Label> eqLowMidValueLabel;
    std::unique_ptr<juce::Label> eqHighMidValueLabel;
    std::unique_ptr<juce::Label> eqHighValueLabel;

    // Helper methods for value formatting
    void updateValueLabels();
    void updateQualityInfo();
    juce::String formatFrequency(float hz);
    juce::String formatGain(float db);
    juce::String formatTime(float ms);
    juce::String formatPercent(float value);
    void createValueLabel(std::unique_ptr<juce::Label>& label);

    // EQ frequency response visualization
    void drawEQCurve(juce::Graphics& g, juce::Rectangle<float> bounds);

    // Latency toggle
    std::unique_ptr<juce::ToggleButton> zeroLatencyButton;

    // New controls - IR Offset
    std::unique_ptr<juce::Slider> irOffsetSlider;
    std::unique_ptr<juce::Label> irOffsetLabel;
    std::unique_ptr<juce::Label> irOffsetValueLabel;

    // Quality dropdown
    std::unique_ptr<juce::ComboBox> qualityComboBox;
    std::unique_ptr<juce::Label> qualityLabel;
    std::unique_ptr<juce::Label> qualityInfoLabel;  // Shows effective sample rate

    // Stereo mode dropdown
    std::unique_ptr<juce::ComboBox> stereoModeComboBox;
    std::unique_ptr<juce::Label> stereoModeLabel;

    // Preset controls
    std::unique_ptr<juce::ComboBox> presetComboBox;
    std::unique_ptr<juce::TextButton> savePresetButton;
    std::unique_ptr<juce::TextButton> deletePresetButton;

    // A/B comparison
    std::unique_ptr<juce::ToggleButton> abToggleButton;
    std::unique_ptr<juce::TextButton> abCopyButton;

    // Mix knob labels
    std::unique_ptr<juce::Label> mixDryLabel;
    std::unique_ptr<juce::Label> mixWetLabel;

    // Volume Compensation toggle
    std::unique_ptr<juce::ToggleButton> volumeCompButton;

    // Filter Envelope controls
    std::unique_ptr<juce::ToggleButton> filterEnvButton;
    std::unique_ptr<juce::Slider> filterEnvInitSlider;
    std::unique_ptr<juce::Slider> filterEnvEndSlider;
    std::unique_ptr<juce::Slider> filterEnvAttackSlider;
    std::unique_ptr<juce::Label> filterEnvInitLabel;
    std::unique_ptr<juce::Label> filterEnvEndLabel;
    std::unique_ptr<juce::Label> filterEnvAttackLabel;
    std::unique_ptr<juce::Label> filterEnvInitValueLabel;
    std::unique_ptr<juce::Label> filterEnvEndValueLabel;
    std::unique_ptr<juce::Label> filterEnvAttackValueLabel;

    // Meters
    std::unique_ptr<LEDMeter> inputMeter;
    std::unique_ptr<LEDMeter> outputMeter;
    std::unique_ptr<juce::Label> inputMeterLabel;
    std::unique_ptr<juce::Label> outputMeterLabel;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowMidFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowMidGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighMidFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighMidGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> zeroLatencyAttachment;

    // New parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> irOffsetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> volumeCompAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> filterEnvAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterEnvInitAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterEnvEndAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterEnvAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stereoModeAttachment;

    // A/B comparison state
    struct ParameterState
    {
        std::map<juce::String, float> values;
    };
    ParameterState stateA;
    ParameterState stateB;
    bool isStateB = false;
    void saveCurrentStateToSlot(ParameterState& slot);
    void loadStateFromSlot(const ParameterState& slot);
    void copyCurrentToOther();

    // Smoothed meter values (stereo L/R)
    float smoothedInputLevelL = -60.0f;
    float smoothedInputLevelR = -60.0f;
    float smoothedOutputLevelL = -60.0f;
    float smoothedOutputLevelR = -60.0f;

    // Cached panel bounds for paint() - calculated in resized()
    juce::Rectangle<float> envelopePanelBounds;
    juce::Rectangle<float> filterEnvPanelBounds;
    juce::Rectangle<float> rightControlsPanelBounds;
    juce::Rectangle<float> eqPanelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConvolutionReverbEditor)
};

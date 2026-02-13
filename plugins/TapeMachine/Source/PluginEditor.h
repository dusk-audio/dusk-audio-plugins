#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/AnalogVUMeter.h"
#include "GUI/TapeMachineLookAndFeel.h"
#include "GUI/TapeReelComponent.h"
#include "../../shared/SupportersOverlay.h"
#include "../../shared/DuskLookAndFeel.h"
#include "../../shared/ScalableEditorHelper.h"

//==============================================================================
// Main Plugin Editor
//==============================================================================
class TapeMachineAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    TapeMachineAudioProcessorEditor(TapeMachineAudioProcessor&);
    ~TapeMachineAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    TapeMachineAudioProcessor& audioProcessor;
    TapeMachineLookAndFeel tapeMachineLookAndFeel;

    // Preset selector
    juce::ComboBox presetSelector;
    juce::Label presetLabel;

    // Combo boxes
    juce::ComboBox tapeMachineSelector;
    juce::ComboBox tapeSpeedSelector;
    juce::ComboBox tapeTypeSelector;
    juce::ComboBox oversamplingSelector;
    juce::ComboBox signalPathSelector;
    juce::ComboBox eqStandardSelector;

    // Sliders (DuskSlider for Cmd/Ctrl+drag fine control)
    DuskSlider inputGainSlider;
    DuskSlider biasSlider;
    DuskSlider highpassFreqSlider;
    DuskSlider lowpassFreqSlider;
    DuskSlider wowSlider;
    DuskSlider flutterSlider;
    DuskSlider outputGainSlider;
    DuskSlider noiseAmountSlider;

    // Toggle buttons
    juce::ToggleButton autoCompButton;
    juce::ToggleButton autoCalButton;

    // Labels
    juce::Label noiseAmountLabel;
    juce::Label autoCompLabel;
    juce::Label autoCalLabel;
    juce::Label tapeMachineLabel;
    juce::Label tapeSpeedLabel;
    juce::Label tapeTypeLabel;
    juce::Label oversamplingLabel;
    juce::Label signalPathLabel;
    juce::Label eqStandardLabel;
    juce::Label inputGainLabel;
    juce::Label biasLabel;
    juce::Label highpassFreqLabel;
    juce::Label lowpassFreqLabel;
    juce::Label wowLabel;
    juce::Label flutterLabel;
    juce::Label outputGainLabel;

    // Visual components
    TapeReelComponent leftReel;
    TapeReelComponent rightReel;
    AnalogVUMeter mainVUMeter;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeMachineAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> signalPathAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> eqStandardAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> biasAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highpassFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowpassFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wowAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flutterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseAmountAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoCompAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoCalAttachment;

    // Setup helpers
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);
    void setupComboBox(juce::ComboBox& combo, juce::Label& label, const juce::String& text);

    // UI drawing helpers
    void drawPanelBackground(juce::Graphics& g);
    void drawVintageTexture(juce::Graphics& g, juce::Rectangle<int> area);

    // Animation state
    float wowPhase = 0.0f;

    // Auto-comp linking
    float lastInputGainValue = 0.0f;
    float lastOutputGainValue = 0.0f;
    bool isUpdatingGainSliders = false;

    // Supporters overlay
    std::unique_ptr<SupportersOverlay> supportersOverlay;
    juce::Rectangle<int> titleClickArea;

    void showSupportersPanel();
    void hideSupportersPanel();

    // Scalable UI support (shared component)
    ScalableEditorHelper resizeHelper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeMachineAudioProcessorEditor)
};

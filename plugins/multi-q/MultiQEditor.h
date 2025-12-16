#pragma once

#include <JuceHeader.h>
#include "MultiQ.h"
#include "MultiQLookAndFeel.h"
#include "EQGraphicDisplay.h"
#include "../../shared/SupportersOverlay.h"
#include "../shared/LEDMeter.h"

//==============================================================================
/**
    Multi-Q Plugin Editor

    UI Layout:
    - Header with plugin name, mode selector, and global controls
    - Band enable buttons (color-coded)
    - Large graphic display with EQ curves and analyzer
    - Selected band parameter controls
    - Footer with analyzer and Q-couple options
*/
class MultiQEditor : public juce::AudioProcessorEditor,
                     private juce::Timer,
                     private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit MultiQEditor(MultiQ&);
    ~MultiQEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    MultiQ& processor;
    MultiQLookAndFeel lookAndFeel;

    // Graphic display
    std::unique_ptr<EQGraphicDisplay> graphicDisplay;

    // Band enable buttons
    std::array<std::unique_ptr<BandEnableButton>, 8> bandEnableButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 8> bandEnableAttachments;

    // Selected band controls
    juce::Label selectedBandLabel;
    std::unique_ptr<juce::Slider> freqSlider;
    std::unique_ptr<juce::Slider> gainSlider;
    std::unique_ptr<juce::Slider> qSlider;
    std::unique_ptr<juce::ComboBox> slopeSelector;

    juce::Label freqLabel;
    juce::Label gainLabel;
    juce::Label qLabel;
    juce::Label slopeLabel;

    // Dynamic attachments for selected band
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> qAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> slopeAttachment;

    // Global controls
    std::unique_ptr<juce::Slider> masterGainSlider;
    std::unique_ptr<juce::ToggleButton> bypassButton;
    std::unique_ptr<juce::ToggleButton> hqButton;
    std::unique_ptr<juce::ComboBox> processingModeSelector;
    std::unique_ptr<juce::ComboBox> qCoupleModeSelector;

    juce::Label masterGainLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> processingModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qCoupleModeAttachment;

    // Analyzer controls
    std::unique_ptr<juce::ToggleButton> analyzerButton;
    std::unique_ptr<juce::ToggleButton> analyzerPrePostButton;
    std::unique_ptr<juce::ComboBox> analyzerModeSelector;
    std::unique_ptr<juce::ComboBox> analyzerResolutionSelector;
    std::unique_ptr<juce::Slider> analyzerDecaySlider;
    std::unique_ptr<juce::ComboBox> displayScaleSelector;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> analyzerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> analyzerPrePostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analyzerModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analyzerResolutionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> analyzerDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> displayScaleAttachment;

    // Meters
    std::unique_ptr<LEDMeter> inputMeter;
    std::unique_ptr<LEDMeter> outputMeter;

    // Supporters overlay
    std::unique_ptr<SupportersOverlay> supportersOverlay;
    juce::Rectangle<int> titleClickArea;

    // Currently selected band (-1 = none)
    int selectedBand = -1;

    // Helpers
    void setupSlider(juce::Slider& slider, const juce::String& suffix = "");
    void setupLabel(juce::Label& label, const juce::String& text);
    void updateSelectedBandControls();
    void onBandSelected(int bandIndex);
    void showSupportersPanel();
    void hideSupportersPanel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiQEditor)
};

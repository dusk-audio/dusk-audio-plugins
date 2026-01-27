#pragma once

#include <JuceHeader.h>
#include "MultiQ.h"
#include "MultiQLookAndFeel.h"
#include "EQGraphicDisplay.h"
#include "BritishEQCurveDisplay.h"
#include "PultecCurveDisplay.h"
#include "PultecLookAndFeel.h"
#include "VintageTubeEQLookAndFeel.h"
#include "BandStripComponent.h"
#include "../shared/SupportersOverlay.h"
#include "../shared/LEDMeter.h"
#include "../shared/LunaLookAndFeel.h"
#include "FourKLookAndFeel.h"

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
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    MultiQ& processor;
    MultiQLookAndFeel lookAndFeel;
    FourKLookAndFeel fourKLookAndFeel;  // For British mode sliders
    PultecLookAndFeel pultecLookAndFeel;  // For Pultec/Tube mode knobs (legacy)
    VintageTubeEQLookAndFeel vintageTubeLookAndFeel;  // For Vintage Tube EQ style

    // Graphic display
    std::unique_ptr<EQGraphicDisplay> graphicDisplay;

    // Band strip component (Eventide SplitEQ-style for Digital mode)
    std::unique_ptr<BandStripComponent> bandStrip;

    // British mode curve display (4K-EQ style)
    std::unique_ptr<BritishEQCurveDisplay> britishCurveDisplay;

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

    // EQ Type selector
    std::unique_ptr<juce::ComboBox> eqTypeSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> eqTypeAttachment;

    // ============== BRITISH MODE CONTROLS ==============
    // HPF/LPF
    std::unique_ptr<juce::Slider> britishHpfFreqSlider;
    std::unique_ptr<juce::ToggleButton> britishHpfEnableButton;
    std::unique_ptr<juce::Slider> britishLpfFreqSlider;
    std::unique_ptr<juce::ToggleButton> britishLpfEnableButton;

    // LF Band
    std::unique_ptr<juce::Slider> britishLfGainSlider;
    std::unique_ptr<juce::Slider> britishLfFreqSlider;
    std::unique_ptr<juce::ToggleButton> britishLfBellButton;

    // LM Band
    std::unique_ptr<juce::Slider> britishLmGainSlider;
    std::unique_ptr<juce::Slider> britishLmFreqSlider;
    std::unique_ptr<juce::Slider> britishLmQSlider;

    // HM Band
    std::unique_ptr<juce::Slider> britishHmGainSlider;
    std::unique_ptr<juce::Slider> britishHmFreqSlider;
    std::unique_ptr<juce::Slider> britishHmQSlider;

    // HF Band
    std::unique_ptr<juce::Slider> britishHfGainSlider;
    std::unique_ptr<juce::Slider> britishHfFreqSlider;
    std::unique_ptr<juce::ToggleButton> britishHfBellButton;

    // Global British controls
    std::unique_ptr<juce::ComboBox> britishModeSelector;  // Brown/Black
    std::unique_ptr<juce::Slider> britishSaturationSlider;
    std::unique_ptr<juce::Slider> britishInputGainSlider;
    std::unique_ptr<juce::Slider> britishOutputGainSlider;

    // British mode section labels
    juce::Label britishLfLabel, britishLmfLabel, britishHmfLabel, britishHfLabel;
    juce::Label britishFiltersLabel, britishMasterLabel;

    // British mode knob labels (below each knob like 4K-EQ)
    juce::Label britishHpfKnobLabel, britishLpfKnobLabel, britishInputKnobLabel;
    juce::Label britishLfGainKnobLabel, britishLfFreqKnobLabel;
    juce::Label britishLmGainKnobLabel, britishLmFreqKnobLabel, britishLmQKnobLabel;
    juce::Label britishHmGainKnobLabel, britishHmFreqKnobLabel, britishHmQKnobLabel;
    juce::Label britishHfGainKnobLabel, britishHfFreqKnobLabel;
    juce::Label britishSatKnobLabel, britishOutputKnobLabel;

    // British mode attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishHpfFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> britishHpfEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishLpfFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> britishLpfEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishLfGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishLfFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> britishLfBellAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishLmGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishLmFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishLmQAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishHmGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishHmFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishHmQAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishHfGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishHfFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> britishHfBellAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> britishModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishSaturationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishInputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishOutputGainAttachment;

    // Track current EQ mode for UI switching
    bool isBritishMode = false;
    bool isPultecMode = false;

    // British mode header controls (matching 4K-EQ)
    juce::TextButton britishCurveCollapseButton;  // "Hide Graph" / "Show Graph"
    std::unique_ptr<juce::ToggleButton> britishBypassButton;
    std::unique_ptr<juce::ToggleButton> britishAutoGainButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> britishBypassAttachment;
    bool britishCurveCollapsed = false;  // Track collapse state for British mode

    // British mode header controls (A/B, Presets - like 4K-EQ)
    juce::TextButton britishAbButton;
    juce::ComboBox britishPresetSelector;

    // Global oversampling selector (shown in header for all modes)
    juce::ComboBox oversamplingSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment;
    bool britishIsStateA = true;  // true = A, false = B
    juce::ValueTree britishStateA, britishStateB;  // Stored parameter states for British mode
    void toggleBritishAB();

    // ============== PULTEC/TUBE MODE CONTROLS ==============
    // Pultec mode curve display
    std::unique_ptr<PultecCurveDisplay> pultecCurveDisplay;

    // LF Section
    std::unique_ptr<juce::Slider> pultecLfBoostSlider;
    std::unique_ptr<juce::ComboBox> pultecLfFreqSelector;
    std::unique_ptr<juce::Slider> pultecLfAttenSlider;

    // HF Boost Section
    std::unique_ptr<juce::Slider> pultecHfBoostSlider;
    std::unique_ptr<juce::ComboBox> pultecHfBoostFreqSelector;
    std::unique_ptr<juce::Slider> pultecHfBandwidthSlider;

    // HF Atten Section
    std::unique_ptr<juce::Slider> pultecHfAttenSlider;
    std::unique_ptr<juce::ComboBox> pultecHfAttenFreqSelector;

    // Global Pultec controls
    std::unique_ptr<juce::Slider> pultecInputGainSlider;
    std::unique_ptr<juce::Slider> pultecOutputGainSlider;
    std::unique_ptr<juce::Slider> pultecTubeDriveSlider;

    // Mid Dip/Peak Section controls
    std::unique_ptr<juce::ToggleButton> pultecMidEnabledButton;
    std::unique_ptr<juce::ComboBox> pultecMidLowFreqSelector;   // Dropdown for LOW FREQ (200-1000 Hz)
    std::unique_ptr<juce::Slider> pultecMidLowPeakSlider;
    std::unique_ptr<juce::ComboBox> pultecMidDipFreqSelector;   // Dropdown for DIP FREQ (200-2000 Hz)
    std::unique_ptr<juce::Slider> pultecMidDipSlider;
    std::unique_ptr<juce::ComboBox> pultecMidHighFreqSelector;  // Dropdown for HIGH FREQ (1500-5000 Hz)
    std::unique_ptr<juce::Slider> pultecMidHighPeakSlider;

    // Pultec section labels
    juce::Label pultecLfLabel, pultecHfBoostLabel, pultecHfAttenLabel, pultecMasterLabel;
    juce::Label pultecLfBoostKnobLabel, pultecLfFreqKnobLabel, pultecLfAttenKnobLabel;
    juce::Label pultecHfBoostKnobLabel, pultecHfBoostFreqKnobLabel, pultecHfBwKnobLabel;
    juce::Label pultecHfAttenKnobLabel, pultecHfAttenFreqKnobLabel;
    juce::Label pultecInputKnobLabel, pultecOutputKnobLabel, pultecTubeKnobLabel;

    // Mid section labels
    juce::Label pultecMidLowFreqLabel, pultecMidLowPeakLabel;
    juce::Label pultecMidDipFreqLabel, pultecMidDipLabel;
    juce::Label pultecMidHighFreqLabel, pultecMidHighPeakLabel;

    // Pultec mode attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecLfBoostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pultecLfFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecLfAttenAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecHfBoostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pultecHfBoostFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecHfBandwidthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecHfAttenAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pultecHfAttenFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecInputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecOutputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecTubeDriveAttachment;

    // Mid section attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pultecMidEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pultecMidLowFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecMidLowPeakAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pultecMidDipFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecMidDipAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pultecMidHighFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pultecMidHighPeakAttachment;

    bool pultecCurveCollapsed = false;  // Track collapse state for Pultec mode

    // Tube mode header controls (A/B, HQ)
    juce::TextButton tubeAbButton;
    std::unique_ptr<juce::ToggleButton> tubeHqButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tubeHqAttachment;

    // A/B comparison state
    bool isStateA = true;  // true = A, false = B
    juce::ValueTree stateA, stateB;  // Stored parameter states
    void toggleAB();
    void copyCurrentToState(juce::ValueTree& state);
    void applyState(const juce::ValueTree& state);

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

    // British mode helpers
    void setupBritishControls();
    void updateEQModeVisibility();
    void layoutBritishControls();
    void drawBritishKnobMarkings(juce::Graphics& g);  // Draw tick marks and value labels around knobs
    void applyBritishPreset(int presetId);  // Apply British mode factory preset

    // Pultec mode helpers
    void setupPultecControls();
    void layoutPultecControls();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiQEditor)
};

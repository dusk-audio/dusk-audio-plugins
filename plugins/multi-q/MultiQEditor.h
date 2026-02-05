#pragma once

#include <JuceHeader.h>
#include "MultiQ.h"
#include "MultiQLookAndFeel.h"
#include "EQGraphicDisplay.h"
#include "BritishEQCurveDisplay.h"
#include "TubeEQCurveDisplay.h"
#include "TubeEQLookAndFeel.h"
#include "VintageTubeEQLookAndFeel.h"
#include "BandDetailPanel.h"
#include "../shared/SupportersOverlay.h"
#include "../shared/LEDMeter.h"
#include "../shared/DuskLookAndFeel.h"
#include "../shared/ScalableEditorHelper.h"
#include "../shared/UserPresetManager.h"
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
    bool keyPressed(const juce::KeyPress& key) override;

private:
    MultiQ& processor;
    MultiQLookAndFeel lookAndFeel;
    FourKLookAndFeel fourKLookAndFeel;  // For British mode sliders
    TubeEQLookAndFeel tubeEQLookAndFeel;  // For Tube EQ mode knobs (legacy)
    VintageTubeEQLookAndFeel vintageTubeLookAndFeel;  // For Vintage Tube EQ style

    // Resizable UI helper (shared across all Dusk Audio plugins)
    ScalableEditorHelper resizeHelper;

    // Graphic display
    std::unique_ptr<EQGraphicDisplay> graphicDisplay;

    // Band detail panel (band selector + single-row controls)
    std::unique_ptr<BandDetailPanel> bandDetailPanel;

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
    std::unique_ptr<juce::ToggleButton> linearPhaseButton;
    std::unique_ptr<juce::ToggleButton> autoGainButton;
    std::unique_ptr<juce::ComboBox> linearPhaseQualitySelector;
    std::unique_ptr<juce::ComboBox> processingModeSelector;
    std::unique_ptr<juce::ComboBox> qCoupleModeSelector;

    juce::Label masterGainLabel;
    juce::Label linearPhaseLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linearPhaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> linearPhaseQualityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> processingModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qCoupleModeAttachment;

    // EQ Type selector
    std::unique_ptr<juce::ComboBox> eqTypeSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> eqTypeAttachment;

    // Factory preset selector (Digital mode)
    std::unique_ptr<juce::ComboBox> presetSelector;
    void updatePresetSelector();
    void onPresetSelected();

    // User preset system
    std::unique_ptr<UserPresetManager> userPresetManager;
    juce::TextButton savePresetButton;
    void saveUserPreset();
    void refreshUserPresets();
    void loadUserPreset(const juce::String& name);
    void deleteUserPreset(const juce::String& name);

    // Undo/Redo buttons
    juce::TextButton undoButton;
    juce::TextButton redoButton;
    void updateUndoRedoButtons();

    // A/B comparison for Digital mode
    juce::TextButton digitalAbButton;
    juce::ValueTree digitalStateA, digitalStateB;
    bool digitalIsStateA = true;
    void toggleDigitalAB();

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
    std::unique_ptr<juce::TextButton> britishModeButton;  // Brown/Black toggle
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
    // britishModeButton uses manual onClick handler instead of attachment
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishSaturationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishInputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> britishOutputGainAttachment;

    // Track current EQ mode for UI switching
    bool isBritishMode = false;
    bool isTubeEQMode = false;
    // Note: Digital mode (default) includes per-band dynamics capability

    // ============== PER-BAND DYNAMICS CONTROLS ==============
    // Dynamics controls (shown in Digital mode for selected band)
    std::unique_ptr<juce::ToggleButton> dynEnableButton;    // Enable dynamics for selected band
    std::unique_ptr<juce::Slider> dynThresholdSlider;       // Threshold in dB
    std::unique_ptr<juce::Slider> dynAttackSlider;          // Attack time in ms
    std::unique_ptr<juce::Slider> dynReleaseSlider;         // Release time in ms
    std::unique_ptr<juce::Slider> dynRangeSlider;           // Max gain change in dB
    std::unique_ptr<juce::Slider> dynRatioSlider;           // Compression ratio

    // Dynamic mode labels
    juce::Label dynSectionLabel;    // "DYNAMICS" section header
    juce::Label dynThresholdLabel;
    juce::Label dynAttackLabel;
    juce::Label dynReleaseLabel;
    juce::Label dynRangeLabel;
    juce::Label dynRatioLabel;

    // Dynamic mode attachments (created/destroyed based on selected band)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dynEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dynThresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dynAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dynReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dynRangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dynRatioAttachment;

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

    // ============== TUBE EQ MODE CONTROLS ==============
    // Tube EQ mode curve display
    std::unique_ptr<TubeEQCurveDisplay> tubeEQCurveDisplay;

    // LF Section
    std::unique_ptr<juce::Slider> tubeEQLfBoostSlider;
    std::unique_ptr<juce::ComboBox> tubeEQLfFreqSelector;
    std::unique_ptr<juce::Slider> tubeEQLfAttenSlider;

    // HF Boost Section
    std::unique_ptr<juce::Slider> tubeEQHfBoostSlider;
    std::unique_ptr<juce::ComboBox> tubeEQHfBoostFreqSelector;
    std::unique_ptr<juce::Slider> tubeEQHfBandwidthSlider;

    // HF Atten Section
    std::unique_ptr<juce::Slider> tubeEQHfAttenSlider;
    std::unique_ptr<juce::ComboBox> tubeEQHfAttenFreqSelector;

    // Global Tube EQ controls
    std::unique_ptr<juce::Slider> tubeEQInputGainSlider;
    std::unique_ptr<juce::Slider> tubeEQOutputGainSlider;
    std::unique_ptr<juce::Slider> tubeEQTubeDriveSlider;

    // Mid Dip/Peak Section controls
    std::unique_ptr<juce::ToggleButton> tubeEQMidEnabledButton;
    std::unique_ptr<juce::ComboBox> tubeEQMidLowFreqSelector;   // Dropdown for LOW FREQ (200-1000 Hz)
    std::unique_ptr<juce::Slider> tubeEQMidLowPeakSlider;
    std::unique_ptr<juce::ComboBox> tubeEQMidDipFreqSelector;   // Dropdown for DIP FREQ (200-2000 Hz)
    std::unique_ptr<juce::Slider> tubeEQMidDipSlider;
    std::unique_ptr<juce::ComboBox> tubeEQMidHighFreqSelector;  // Dropdown for HIGH FREQ (1500-5000 Hz)
    std::unique_ptr<juce::Slider> tubeEQMidHighPeakSlider;

    // Tube EQ section labels
    juce::Label tubeEQLfLabel, tubeEQHfBoostLabel, tubeEQHfAttenLabel, tubeEQMasterLabel;
    juce::Label tubeEQLfBoostKnobLabel, tubeEQLfFreqKnobLabel, tubeEQLfAttenKnobLabel;
    juce::Label tubeEQHfBoostKnobLabel, tubeEQHfBoostFreqKnobLabel, tubeEQHfBwKnobLabel;
    juce::Label tubeEQHfAttenKnobLabel, tubeEQHfAttenFreqKnobLabel;
    juce::Label tubeEQInputKnobLabel, tubeEQOutputKnobLabel, tubeEQTubeKnobLabel;

    // Mid section labels
    juce::Label tubeEQMidLowFreqLabel, tubeEQMidLowPeakLabel;
    juce::Label tubeEQMidDipFreqLabel, tubeEQMidDipLabel;
    juce::Label tubeEQMidHighFreqLabel, tubeEQMidHighPeakLabel;

    // Tube EQ mode attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQLfBoostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tubeEQLfFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQLfAttenAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQHfBoostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tubeEQHfBoostFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQHfBandwidthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQHfAttenAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tubeEQHfAttenFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQInputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQOutputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQTubeDriveAttachment;

    // Mid section attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tubeEQMidEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tubeEQMidLowFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQMidLowPeakAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tubeEQMidDipFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQMidDipAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tubeEQMidHighFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tubeEQMidHighPeakAttachment;

    bool tubeEQCurveCollapsed = false;  // Track collapse state for Tube EQ mode

    // Tube mode header controls (A/B, Preset, HQ, Hide Graph)
    juce::TextButton tubeAbButton;
    juce::ComboBox tubePresetSelector;  // Preset selector for Tube mode
    juce::TextButton tubeCurveCollapseButton;  // "Hide Graph" / "Show Graph"
    std::unique_ptr<juce::ToggleButton> tubeHqButton;
    juce::TextButton tubeEQCurveCollapseButton;  // "Hide Graph" / "Show Graph" for Tube mode
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
    std::unique_ptr<juce::ComboBox> analyzerSmoothingSelector;  // Spectrum smoothing mode
    std::unique_ptr<juce::Slider> analyzerDecaySlider;
    std::unique_ptr<juce::ComboBox> displayScaleSelector;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> analyzerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> analyzerPrePostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analyzerModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analyzerResolutionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analyzerSmoothingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> analyzerDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> displayScaleAttachment;

    // Meters
    std::unique_ptr<LEDMeter> inputMeter;
    std::unique_ptr<LEDMeter> outputMeter;

    // Clip indicators (click-to-reset)
    juce::Rectangle<int> inputClipBounds;
    juce::Rectangle<int> outputClipBounds;
    bool lastInputClipState = false;
    bool lastOutputClipState = false;
    void drawClipIndicator(juce::Graphics& g, juce::Rectangle<int> bounds, bool clipped);

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
    void applyTubePreset(int presetId);     // Apply Tube mode factory preset

    // Tube EQ mode helpers
    void setupTubeEQControls();
    void layoutTubeEQControls();

    // Unified toolbar layout (called from resized() for all modes)
    void layoutUnifiedToolbar();

    // Dynamic mode helpers
    void setupDynamicControls();
    void layoutDynamicControls();
    void updateDynamicAttachments();  // Rebind attachments when selected band changes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiQEditor)
};

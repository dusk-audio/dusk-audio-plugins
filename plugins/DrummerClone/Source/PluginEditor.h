#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "FollowModePanel.h"
#include "MidiExporter.h"
#include "StepSequencer.h"
#include "ProfileEditorPanel.h"

/**
 * Custom XY Pad component for Swing/Drive control
 */
class XYPad : public juce::Component
{
public:
    XYPad();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    // Set position (0.0 - 1.0 for both axes)
    void setPosition(float x, float y);
    std::pair<float, float> getPosition() const { return {posX, posY}; }

    // Callback when position changes
    std::function<void(float, float)> onPositionChanged;

private:
    float posX = 0.5f;
    float posY = 0.5f;

    void updatePositionFromMouse(const juce::MouseEvent& e);
};

/**
 * DrummerCloneAudioProcessorEditor - Main plugin UI
 *
 * Layout mirrors Logic Pro Drummer Editor:
 * - Left sidebar: Library (styles, drummers)
 * - Center: XY Pad for Swing/Drive
 * - Top: Global controls (complexity, loudness)
 * - Bottom: Follow Mode panel and Details
 */
class DrummerCloneAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         public juce::Timer
{
public:
    DrummerCloneAudioProcessorEditor(DrummerCloneAudioProcessor&);
    ~DrummerCloneAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    DrummerCloneAudioProcessor& audioProcessor;

    // Custom look and feel
    juce::LookAndFeel_V4 darkLookAndFeel;

    // ========== LEFT SIDEBAR (Library Panel) ==========
    juce::Label libraryLabel;
    juce::ComboBox styleComboBox;
    juce::Label styleLabel;
    juce::ComboBox drummerComboBox;
    juce::Label drummerLabel;

    // ========== CENTER (XY Pad) ==========
    XYPad xyPad;
    juce::Label xyLabel;

    // ========== TOP BAR (Global Controls) ==========
    juce::Slider swingSlider;
    juce::Label swingLabel;
    juce::Slider complexitySlider;
    juce::Label complexityLabel;
    juce::Slider loudnessSlider;
    juce::Label loudnessLabel;
    juce::TextButton generateButton;
    juce::TextButton exportButton;  // Export to MIDI file
    juce::ComboBox exportBarsComboBox;  // Number of bars to export
    juce::Label exportBarsLabel;

    // ========== BOTTOM (Follow Mode + Details) ==========
    FollowModePanel followModePanel;
    juce::TextButton detailsToggleButton;
    bool detailsPanelVisible = false;

    // Details panel components
    juce::ComboBox kickPatternComboBox;
    juce::Label kickPatternLabel;
    juce::ComboBox snarePatternComboBox;
    juce::Label snarePatternLabel;
    juce::Slider hiHatOpenSlider;
    juce::Label hiHatOpenLabel;
    juce::ToggleButton percussionToggle;

    // ========== SECTION ARRANGEMENT PANEL ==========
    juce::Label sectionLabel;
    juce::ComboBox sectionComboBox;

    // ========== FILLS PANEL ==========
    juce::Label fillsLabel;
    juce::Slider fillFrequencySlider;
    juce::Label fillFrequencyLabel;
    juce::Slider fillIntensitySlider;
    juce::Label fillIntensityLabel;
    juce::ComboBox fillLengthComboBox;
    juce::Label fillLengthLabel;
    juce::TextButton fillTriggerButton;

    // ========== STEP SEQUENCER ==========
    StepSequencer stepSequencer;
    juce::TextButton stepSeqToggleButton;
    bool stepSeqVisible = false;

    // ========== HUMANIZATION PANEL ==========
    juce::Label humanLabel;
    juce::Slider humanTimingSlider;
    juce::Label humanTimingLabel;
    juce::Slider humanVelocitySlider;
    juce::Label humanVelocityLabel;
    juce::Slider humanPushSlider;
    juce::Label humanPushLabel;
    juce::Slider humanGrooveSlider;
    juce::Label humanGrooveLabel;
    juce::TextButton humanToggleButton;
    bool humanPanelVisible = false;

    // ========== MIDI CC CONTROL PANEL ==========
    juce::Label midiCCLabel;
    juce::ToggleButton midiCCEnableToggle;
    juce::Slider sectionCCSlider;
    juce::Label sectionCCLabel;
    juce::Slider fillCCSlider;
    juce::Label fillCCLabel;
    juce::TextButton midiCCToggleButton;
    bool midiCCPanelVisible = false;
    juce::Label midiCCSourceIndicator;  // Shows when section is being controlled via MIDI

    // ========== PROFILE EDITOR PANEL ==========
    ProfileEditorPanel profileEditorPanel;
    juce::TextButton profileEditorToggleButton;
    bool profileEditorVisible = false;

    // ========== KIT ENABLE PANEL ==========
    juce::Label kitLabel;
    juce::ToggleButton kitKickToggle;
    juce::ToggleButton kitSnareToggle;
    juce::ToggleButton kitHiHatToggle;
    juce::ToggleButton kitTomsToggle;
    juce::ToggleButton kitCymbalsToggle;
    juce::ToggleButton kitPercussionToggle;
    juce::TextButton kitToggleButton;
    bool kitPanelVisible = false;

    // ========== STATUS BAR ==========
    juce::Label statusLabel;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> complexityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> loudnessAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> drummerAttachment;

    // Fill parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fillFrequencyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fillIntensityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fillLengthAttachment;

    // Section attachment
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sectionAttachment;

    // Humanization attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humanTimingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humanVelocityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humanPushAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humanGrooveAttachment;

    // MIDI CC attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiCCEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sectionCCAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fillCCAttachment;

    // Kit enable attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kitKickAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kitSnareAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kitHiHatAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kitTomsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kitCymbalsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kitPercussionAttachment;

    // Setup helpers
    void setupLibraryPanel();
    void setupXYPad();
    void setupGlobalControls();
    void setupFollowModePanel();
    void setupDetailsPanel();
    void setupSectionPanel();
    void setupFillsPanel();
    void setupStepSequencer();
    void setupHumanizationPanel();
    void setupMidiCCPanel();
    void setupProfileEditorPanel();
    void setupKitPanel();
    void setupStatusBar();

    void updateStatusBar();
    void exportToMidiFile();
    void updateDrummerListForStyle(int styleIndex);

    // Mapping from filtered drummer combo index to global drummer index
    std::vector<int> filteredDrummerIndices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrummerCloneAudioProcessorEditor)
};
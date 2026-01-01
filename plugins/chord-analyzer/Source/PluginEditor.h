#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "ChordAnalyzerLookAndFeel.h"
#include "TheoryTooltips.h"
#include "../../shared/SupportersOverlay.h"

//==============================================================================
class ChordAnalyzerEditor : public juce::AudioProcessorEditor,
                             public juce::Timer
{
public:
    explicit ChordAnalyzerEditor(ChordAnalyzerProcessor&);
    ~ChordAnalyzerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    ChordAnalyzerProcessor& audioProcessor;
    ChordAnalyzerLookAndFeel lookAndFeel;

    //==========================================================================
    // Supporters overlay
    std::unique_ptr<SupportersOverlay> supportersOverlay;
    juce::Rectangle<int> titleClickArea;

    //==========================================================================
    // Main chord display labels
    juce::Label chordNameLabel;
    juce::Label romanNumeralLabel;
    juce::Label functionLabel;
    juce::Label notesLabel;

    //==========================================================================
    // Key selection
    juce::ComboBox keyRootCombo;
    juce::Label keyRootLabel;
    juce::ComboBox keyModeCombo;
    juce::Label keyModeLabel;

    //==========================================================================
    // Suggestion panel
    juce::Label suggestionsHeaderLabel;
    static constexpr int numSuggestionButtons = 6;
    std::array<juce::TextButton, numSuggestionButtons> suggestionButtons;
    juce::ComboBox suggestionLevelCombo;
    juce::Label suggestionLevelLabel;

    //==========================================================================
    // Recording panel
    juce::TextButton recordButton;
    juce::TextButton clearButton;
    juce::TextButton exportButton;
    juce::Label recordingStatusLabel;
    juce::Label eventCountLabel;

    //==========================================================================
    // Options
    juce::ToggleButton showInversionsToggle;

    //==========================================================================
    // Tooltip display
    juce::Label tooltipLabel;
    juce::String currentTooltipText;

    //==========================================================================
    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyRootAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> suggestionLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> showInversionsAttachment;

    //==========================================================================
    // Animation state
    float chordFadeAlpha = 1.0f;
    float chordSlideOffset = 0.0f;
    ChordInfo lastDisplayedChord;
    bool animatingChordChange = false;
    int animationCounter = 0;

    //==========================================================================
    // Current state cache
    ChordInfo cachedChord;
    std::vector<ChordSuggestion> cachedSuggestions;

    //==========================================================================
    // Setup helpers
    void setupChordDisplay();
    void setupKeySelection();
    void setupSuggestionPanel();
    void setupRecordingPanel();
    void setupOptions();
    void setupTooltip();

    //==========================================================================
    // Update helpers
    void updateChordDisplay();
    void updateSuggestionButtons();
    void updateRecordingStatus();
    void animateChordChange();

    //==========================================================================
    // Tooltip helpers
    void showTooltip(const juce::String& text);
    void clearTooltip();

    //==========================================================================
    // Recording actions
    void toggleRecording();
    void clearRecording();
    void exportRecording();

    //==========================================================================
    // Supporters overlay helpers
    void showSupportersPanel();
    void hideSupportersPanel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAnalyzerEditor)
};

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "ChordAnalyzerLookAndFeel.h"
#include "ChordHistoryStrip.h"
#include "ChordMidiExport.h"
#include "TheoryTooltips.h"
#include "../../shared/SupportersOverlay.h"
#include "../../shared/ScalableEditorHelper.h"

//==============================================================================
class ChordAnalyzerEditor : public juce::AudioProcessorEditor,
                             public juce::DragAndDropContainer,
                             public juce::Timer
{
public:
    explicit ChordAnalyzerEditor(ChordAnalyzerProcessor&);
    ~ChordAnalyzerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    ChordAnalyzerProcessor& audioProcessor;
    ChordAnalyzerLookAndFeel lookAndFeel;
    ScalableEditorHelper resizeHelper;

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
    juce::Rectangle<int> keyHoverArea;

    //==========================================================================
    // Suggestion panel
    juce::Label suggestionsHeaderLabel;
    static constexpr int numSuggestionButtons = 6;
    std::array<juce::TextButton, numSuggestionButtons> suggestionButtons;
    juce::ComboBox suggestionLevelCombo;
    juce::Label suggestionLevelLabel;

    //==========================================================================
    // Recent-chord history strip — always-on rolling window of last N
    // detected chords, independent of the recording session.
    ChordHistoryStrip historyStrip;
    juce::TextButton clearHistoryButton;

    //==========================================================================
    // Drag-and-drop affordances. Hit-tested rectangles so mouseDrag knows
    // which payload to export when a drag is initiated. Updated each
    // resized() call.
    juce::Rectangle<int> chordDisplayDragArea;
    bool dragInitiatedFromEditor_ = false;

    // Pill-shaped drag handle for the recorded session. Custom Component
    // (not a TextButton) so mouseDrag isn't swallowed by click handling
    // — the entire affordance IS the drag.
    class SessionDragHandle : public juce::Component,
                              public juce::SettableTooltipClient
    {
    public:
        std::function<void()> onDragStart;
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragInitiated_ = false; }
    private:
        bool dragInitiated_ = false;
    };
    SessionDragHandle dragSessionHandle;

    //==========================================================================
    // Recording panel — Record / Clear only. JSON export was dropped 2026-05-04
    // (issue #71); MIDI drag-and-drop replaces it as the way to get the
    // recorded progression out into a DAW.
    juce::TextButton recordButton;
    juce::TextButton clearButton;
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
    void setupHistoryStrip();
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

    //==========================================================================
    // Drag-and-drop helpers
    void startDragForChord(const ChordInfo& chord);
    void startDragForSession();

    //==========================================================================
    // Supporters overlay helpers
    void showSupportersPanel();
    void hideSupportersPanel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAnalyzerEditor)
};

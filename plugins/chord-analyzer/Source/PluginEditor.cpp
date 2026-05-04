#include "PluginEditor.h"

//==============================================================================
ChordAnalyzerEditor::ChordAnalyzerEditor(ChordAnalyzerProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);
    // Default 800x580 (was 520) to fit the recent-chord history strip
    // below the main display. Min 460 (was 400) preserves a usable
    // chord-display area at the smallest size.
    resizeHelper.initialize(this, &audioProcessor, 800, 580, 600, 460, 1200, 840, false);
    setSize(resizeHelper.getStoredWidth(), resizeHelper.getStoredHeight());

    setupChordDisplay();
    setupHistoryStrip();
    setupKeySelection();
    setupSuggestionPanel();
    setupRecordingPanel();
    setupOptions();
    setupTooltip();

    // Start UI update timer at 30Hz
    startTimerHz(30);
}

ChordAnalyzerEditor::~ChordAnalyzerEditor()
{
    resizeHelper.saveSize();
    setLookAndFeel(nullptr);
}

//==============================================================================
void ChordAnalyzerEditor::setupChordDisplay()
{
    // Chord name - large display
    chordNameLabel.setJustificationType(juce::Justification::centred);
    chordNameLabel.setFont(juce::Font(juce::FontOptions(52.0f)).boldened());
    chordNameLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::textBright);
    chordNameLabel.setText("-", juce::dontSendNotification);
    addAndMakeVisible(chordNameLabel);

    // Roman numeral - medium display
    romanNumeralLabel.setJustificationType(juce::Justification::centred);
    romanNumeralLabel.setFont(juce::Font(juce::FontOptions(32.0f)).boldened());
    romanNumeralLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::accentBlue);
    romanNumeralLabel.setText("-", juce::dontSendNotification);
    addAndMakeVisible(romanNumeralLabel);

    // Harmonic function - smaller display
    functionLabel.setJustificationType(juce::Justification::centred);
    functionLabel.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    functionLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::textDim);
    functionLabel.setText("", juce::dontSendNotification);
    addAndMakeVisible(functionLabel);

    // Notes display - slightly larger font for readability
    notesLabel.setJustificationType(juce::Justification::centred);
    notesLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    notesLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::textMuted);
    notesLabel.setText("", juce::dontSendNotification);
    addAndMakeVisible(notesLabel);
}

void ChordAnalyzerEditor::setupHistoryStrip()
{
    addAndMakeVisible(historyStrip);
    historyStrip.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    historyStrip.onChipClicked = [this](int idx)
    {
        auto history = audioProcessor.getChordHistory();
        if (idx >= 0 && idx < static_cast<int>(history.size()))
        {
            const auto& chord = history[(size_t) idx];
            juce::String msg = chord.name;
            if (chord.romanNumeral.isNotEmpty())
                msg += "  (" + chord.romanNumeral + ")";
            if (! chord.midiNotes.empty())
            {
                msg += "  Notes: ";
                for (size_t i = 0; i < chord.midiNotes.size(); ++i)
                {
                    if (i > 0) msg += ", ";
                    msg += ChordAnalyzer::noteToName(chord.midiNotes[i]);
                }
            }
            showTooltip(msg);
        }
    };
    historyStrip.onChipDragStart = [this](const ChordInfo& chord)
    {
        startDragForChord(chord);
    };

    clearHistoryButton.setButtonText("Clear");
    clearHistoryButton.setTooltip("Clear the recent-chord history strip");
    clearHistoryButton.setColour(juce::TextButton::buttonColourId,
                                  ChordAnalyzerLookAndFeel::Colors::bgSection);
    clearHistoryButton.onClick = [this]()
    {
        audioProcessor.clearChordHistory();
        historyStrip.setHistory({});
    };
    addAndMakeVisible(clearHistoryButton);
}

void ChordAnalyzerEditor::setupKeySelection()
{
    // Key root combo
    keyRootLabel.setText("Key:", juce::dontSendNotification);
    keyRootLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    keyRootLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::textLight);
    addAndMakeVisible(keyRootLabel);

    keyRootCombo.addItemList({"C", "C#/Db", "D", "D#/Eb", "E", "F",
                              "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"}, 1);
    keyRootCombo.setTooltip("Select the key root note for Roman numeral analysis");
    addAndMakeVisible(keyRootCombo);

    keyRootAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), ChordAnalyzerProcessor::PARAM_KEY_ROOT, keyRootCombo);

    // Key mode combo
    keyModeLabel.setText("Mode:", juce::dontSendNotification);
    keyModeLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    keyModeLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::textLight);
    addAndMakeVisible(keyModeLabel);

    keyModeCombo.addItemList({"Major", "Minor"}, 1);
    keyModeCombo.setTooltip("Select major or minor mode for the key");
    addAndMakeVisible(keyModeCombo);

    keyModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), ChordAnalyzerProcessor::PARAM_KEY_MODE, keyModeCombo);
}

void ChordAnalyzerEditor::setupSuggestionPanel()
{
    // Note: Header "SUGGESTIONS" is drawn by drawSectionPanel() in paint()
    // No need for a separate label

    // Suggestion buttons - start hidden until we have suggestions
    for (int i = 0; i < numSuggestionButtons; ++i)
    {
        suggestionButtons[i].setButtonText("");
        suggestionButtons[i].setEnabled(false);
        suggestionButtons[i].setVisible(false);  // Start hidden
        suggestionButtons[i].onClick = [this, i]()
        {
            if (i < static_cast<int>(cachedSuggestions.size()))
            {
                showTooltip(cachedSuggestions[i].reason);
            }
        };
        addAndMakeVisible(suggestionButtons[i]);
    }

    // Suggestion level combo
    suggestionLevelLabel.setText("Level:", juce::dontSendNotification);
    suggestionLevelLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    suggestionLevelLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::textDim);
    addAndMakeVisible(suggestionLevelLabel);

    suggestionLevelCombo.addItemList({"Basic", "Basic + Inter", "All"}, 1);
    suggestionLevelCombo.setTooltip("Filter suggestion complexity level");
    addAndMakeVisible(suggestionLevelCombo);

    suggestionLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), ChordAnalyzerProcessor::PARAM_SUGGESTION_LEVEL, suggestionLevelCombo);
}

void ChordAnalyzerEditor::setupRecordingPanel()
{
    // Record button with circle indicator
    recordButton.setButtonText("REC");
    recordButton.setColour(juce::TextButton::buttonColourId, ChordAnalyzerLookAndFeel::Colors::bgSection);
    recordButton.setTooltip("Start/stop recording chord progression");
    recordButton.onClick = [this]() { toggleRecording(); };
    addAndMakeVisible(recordButton);

    // Clear button
    clearButton.setButtonText("CLEAR");
    clearButton.setColour(juce::TextButton::buttonColourId, ChordAnalyzerLookAndFeel::Colors::bgSection);
    clearButton.setTooltip("Clear recorded progression");
    clearButton.onClick = [this]() { clearRecording(); };
    addAndMakeVisible(clearButton);

    // Drag-to-DAW handle — replaces the JSON export. The whole component
    // is a drag source; clicking does nothing.
    dragSessionHandle.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    dragSessionHandle.setTooltip("Drag from this pill into a DAW track to drop the recorded progression as a MIDI clip");
    dragSessionHandle.onDragStart = [this]() { startDragForSession(); };
    addAndMakeVisible(dragSessionHandle);

    // Recording status - larger and bolder
    recordingStatusLabel.setText("", juce::dontSendNotification);
    recordingStatusLabel.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    recordingStatusLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::accentRed);
    addAndMakeVisible(recordingStatusLabel);

    // Event count
    eventCountLabel.setText("Events: 0", juce::dontSendNotification);
    eventCountLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    eventCountLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::textDim);
    addAndMakeVisible(eventCountLabel);
}

void ChordAnalyzerEditor::setupOptions()
{
    showInversionsToggle.setButtonText("Show inversions");
    showInversionsToggle.setTooltip("Display slash notation for chord inversions");
    addAndMakeVisible(showInversionsToggle);

    showInversionsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ChordAnalyzerProcessor::PARAM_SHOW_INVERSIONS, showInversionsToggle);
}

void ChordAnalyzerEditor::setupTooltip()
{
    tooltipLabel.setJustificationType(juce::Justification::centredLeft);
    tooltipLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    tooltipLabel.setColour(juce::Label::textColourId, ChordAnalyzerLookAndFeel::Colors::textLight);
    tooltipLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    tooltipLabel.setText("Play some notes to see chord analysis...", juce::dontSendNotification);
    addAndMakeVisible(tooltipLabel);
}

//==============================================================================
void ChordAnalyzerEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(ChordAnalyzerLookAndFeel::Colors::bgMain);

    auto bounds = getLocalBounds();

    // Header
    auto headerArea = bounds.removeFromTop(45);
#if CHORD_ANALYZER_FX_MODE
    ChordAnalyzerLookAndFeel::drawPluginHeader(g, headerArea,
                                                "CHORD ANALYZER FX", "Dusk Audio");
#else
    ChordAnalyzerLookAndFeel::drawPluginHeader(g, headerArea,
                                                "CHORD ANALYZER", "Dusk Audio");
#endif
    titleClickArea = headerArea.reduced(10, 5).withWidth(180);

    // Main chord display section - taller for better spacing
    auto chordDisplayArea = bounds.removeFromTop(175);
    ChordAnalyzerLookAndFeel::drawSectionPanel(g, chordDisplayArea.reduced(10, 5));

    // Recent chord history strip
    auto historyArea = bounds.removeFromTop(60);
    ChordAnalyzerLookAndFeel::drawSectionPanel(g, historyArea.reduced(10, 5), "RECENT");

    // Key selection section
    auto keySelectionArea = bounds.removeFromTop(50);
    ChordAnalyzerLookAndFeel::drawSectionPanel(g, keySelectionArea.reduced(10, 5), "KEY");

    // Suggestions section
    auto suggestionsArea = bounds.removeFromTop(95);
    ChordAnalyzerLookAndFeel::drawSectionPanel(g, suggestionsArea.reduced(10, 5), "SUGGESTIONS");

    // Recording section
    auto recordingArea = bounds.removeFromTop(55);
    ChordAnalyzerLookAndFeel::drawSectionPanel(g, recordingArea.reduced(10, 5), "RECORDING");

    // Tooltip section at bottom (combine options into it)
    auto tooltipArea = bounds.reduced(10, 5);
    ChordAnalyzerLookAndFeel::drawSectionPanel(g, tooltipArea);
}

void ChordAnalyzerEditor::resized()
{
    auto bounds = getLocalBounds();

    // Skip header
    bounds.removeFromTop(45);

    // Chord display area - more space for each element
    auto chordSection = bounds.removeFromTop(175);
    chordDisplayDragArea = chordSection;   // hit-test target for drag-to-DAW
    auto chordArea = chordSection.reduced(20, 12);
    chordNameLabel.setBounds(chordArea.removeFromTop(65));
    romanNumeralLabel.setBounds(chordArea.removeFromTop(45));
    functionLabel.setBounds(chordArea.removeFromTop(28));
    notesLabel.setBounds(chordArea);  // Use remaining space

    // Recent chord history strip
    auto historyArea = bounds.removeFromTop(60).reduced(20, 8);
    historyArea.removeFromTop(14);  // Section title space
    auto historyClearArea = historyArea.removeFromRight(60);
    historyArea.removeFromRight(8);  // Gap between strip and Clear button
    clearHistoryButton.setBounds(historyClearArea.reduced(0, 4));
    historyStrip.setBounds(historyArea);

    // Key selection area
    auto keySection = bounds.removeFromTop(50);
    auto keyArea = keySection.reduced(20, 8);
    keyHoverArea = keySection;  // hit-test target for the key tooltip
    keyArea.removeFromTop(14);  // Account for section title
    keyRootLabel.setBounds(keyArea.removeFromLeft(35));
    keyRootCombo.setBounds(keyArea.removeFromLeft(90).reduced(0, 2));
    keyArea.removeFromLeft(30);
    keyModeLabel.setBounds(keyArea.removeFromLeft(45));
    keyModeCombo.setBounds(keyArea.removeFromLeft(85).reduced(0, 2));

    // Suggestions area
    auto suggestionsArea = bounds.removeFromTop(95).reduced(20, 8);
    auto suggestionsHeader = suggestionsArea.removeFromTop(18);
    // Header text drawn by drawSectionPanel, just reserve space
    suggestionsHeader.removeFromLeft(100);

    // Suggestion level on right side of header
    auto levelArea = suggestionsHeader.removeFromRight(180);
    suggestionLevelLabel.setBounds(levelArea.removeFromLeft(45));
    suggestionLevelCombo.setBounds(levelArea.reduced(0, 0));

    // Suggestion buttons in a grid (2 rows of 3)
    auto buttonsArea = suggestionsArea.reduced(0, 3);
    int buttonWidth = (buttonsArea.getWidth() - 20) / 3;
    int buttonHeight = 30;

    for (int i = 0; i < 3; ++i)
    {
        suggestionButtons[i].setBounds(
            buttonsArea.getX() + i * (buttonWidth + 10),
            buttonsArea.getY(),
            buttonWidth, buttonHeight);
    }
    for (int i = 0; i < 3; ++i)
    {
        suggestionButtons[i + 3].setBounds(
            buttonsArea.getX() + i * (buttonWidth + 10),
            buttonsArea.getY() + buttonHeight + 6,
            buttonWidth, buttonHeight);
    }

    // Recording area
    auto recordingArea = bounds.removeFromTop(55).reduced(20, 8);
    recordingArea.removeFromTop(14);  // Account for section title

    int recordBtnWidth = 65;
    recordButton.setBounds(recordingArea.removeFromLeft(recordBtnWidth).reduced(0, 2));
    recordingArea.removeFromLeft(10);
    clearButton.setBounds(recordingArea.removeFromLeft(recordBtnWidth).reduced(0, 2));
    recordingArea.removeFromLeft(10);
    dragSessionHandle.setBounds(recordingArea.removeFromLeft(110).reduced(0, 2));
    recordingArea.removeFromLeft(10);
    recordingStatusLabel.setBounds(recordingArea.removeFromLeft(110));
    eventCountLabel.setBounds(recordingArea);

    // Bottom area with options and tooltip
    auto bottomArea = bounds.reduced(20, 8);
    auto optionsRow = bottomArea.removeFromTop(24);
    showInversionsToggle.setBounds(optionsRow.removeFromLeft(150));

    // Tooltip uses remaining space
    tooltipLabel.setBounds(bottomArea.reduced(0, 4));

    // Supporters overlay
    if (supportersOverlay)
        supportersOverlay->setBounds(getLocalBounds());

    resizeHelper.updateResizer();
}

//==============================================================================
void ChordAnalyzerEditor::timerCallback()
{
    // Check for chord changes
    if (audioProcessor.hasChordChanged() || cachedChord.name.isEmpty())
    {
        updateChordDisplay();
        updateSuggestionButtons();

        // Refresh the history strip on the same trigger. setHistory()
        // short-circuits on identical content, so calling unconditionally
        // is cheap and also catches the rotated-window case where size
        // stays at maxHistorySize but entries shift.
        historyStrip.setHistory(audioProcessor.getChordHistory());
    }

    // Update recording status (includes blinking animation)
    updateRecordingStatus();

    // Handle animation
    if (animatingChordChange)
    {
        animateChordChange();
    }
}

//==============================================================================
void ChordAnalyzerEditor::updateChordDisplay()
{
    cachedChord = audioProcessor.getCurrentChord();

    // Update chord name
    juce::String displayName = cachedChord.name;

    // Add inversion notation if enabled and not root position
    bool showInv = *audioProcessor.getAPVTS().getRawParameterValue(
        ChordAnalyzerProcessor::PARAM_SHOW_INVERSIONS) > 0.5f;

    if (showInv && !cachedChord.extensions.isEmpty() && cachedChord.inversion > 0)
    {
        displayName += cachedChord.extensions;
    }

    chordNameLabel.setText(displayName, juce::dontSendNotification);

    // Update Roman numeral
    romanNumeralLabel.setText(cachedChord.romanNumeral, juce::dontSendNotification);

    // Update function with color
    juce::String funcText = cachedChord.isValid
        ? "(" + ChordAnalyzer::functionToString(cachedChord.function) + ")"
        : "";
    functionLabel.setText(funcText, juce::dontSendNotification);
    functionLabel.setColour(juce::Label::textColourId,
        ChordAnalyzerLookAndFeel::getFunctionColor(static_cast<int>(cachedChord.function)));

    // Update notes display
    if (!cachedChord.midiNotes.empty())
    {
        juce::String notesStr = "Notes: ";
        for (size_t i = 0; i < cachedChord.midiNotes.size(); ++i)
        {
            if (i > 0) notesStr += ", ";
            notesStr += ChordAnalyzer::noteToName(cachedChord.midiNotes[i]);
        }
        notesLabel.setText(notesStr, juce::dontSendNotification);
    }
    else
    {
        notesLabel.setText("", juce::dontSendNotification);
    }

    // Update tooltip with chord explanation
    if (cachedChord.isValid)
    {
        showTooltip(TheoryTooltips::getChordExplanation(cachedChord.quality));
    }

    // Trigger animation
    if (lastDisplayedChord != cachedChord)
    {
        animatingChordChange = true;
        animationCounter = 0;
        chordFadeAlpha = 0.0f;
        lastDisplayedChord = cachedChord;
    }
}

void ChordAnalyzerEditor::updateSuggestionButtons()
{
    cachedSuggestions = audioProcessor.getCurrentSuggestions();

    for (int i = 0; i < numSuggestionButtons; ++i)
    {
        if (i < static_cast<int>(cachedSuggestions.size()))
        {
            const auto& suggestion = cachedSuggestions[i];
            // Show both roman numeral and actual chord name (e.g., "IV (F)" or "ii (Dm)")
            suggestionButtons[i].setButtonText(suggestion.romanNumeral + "\n" + suggestion.chordName);
            suggestionButtons[i].setEnabled(true);
            suggestionButtons[i].setVisible(true);
            suggestionButtons[i].setTooltip(suggestion.chordName + ": " + suggestion.reason);

            // Color by category - more vibrant colors
            juce::Colour buttonColor = ChordAnalyzerLookAndFeel::getSuggestionColor(
                static_cast<int>(suggestion.category));
            suggestionButtons[i].setColour(juce::TextButton::buttonColourId,
                                            buttonColor.withAlpha(0.25f));
            suggestionButtons[i].setColour(juce::TextButton::textColourOffId,
                                            buttonColor.brighter(0.3f));
        }
        else
        {
            // Hide unused buttons instead of showing "-"
            suggestionButtons[i].setVisible(false);
            suggestionButtons[i].setEnabled(false);
        }
    }
}

void ChordAnalyzerEditor::updateRecordingStatus()
{
    bool isRec = audioProcessor.isRecording();

    if (isRec)
    {
        // Blinking effect for recording indicator
        bool showBlink = (animationCounter / 15) % 2 == 0;  // Blink every ~0.5 sec

        recordButton.setColour(juce::TextButton::buttonColourId,
                                showBlink ? ChordAnalyzerLookAndFeel::Colors::accentRed
                                          : ChordAnalyzerLookAndFeel::Colors::accentRed.darker(0.3f));
        recordButton.setColour(juce::TextButton::textColourOffId,
                                ChordAnalyzerLookAndFeel::Colors::textBright);

        juce::String statusText = showBlink ? "* RECORDING *" : "  RECORDING  ";
        recordingStatusLabel.setText(statusText, juce::dontSendNotification);
    }
    else
    {
        recordButton.setColour(juce::TextButton::buttonColourId,
                                ChordAnalyzerLookAndFeel::Colors::bgSection);
        recordButton.setColour(juce::TextButton::textColourOffId,
                                ChordAnalyzerLookAndFeel::Colors::textLight);
        recordingStatusLabel.setText("", juce::dontSendNotification);
    }

    eventCountLabel.setText("Events: " + juce::String(audioProcessor.getRecordedEventCount()),
                             juce::dontSendNotification);

    animationCounter++;
}

void ChordAnalyzerEditor::animateChordChange()
{
    // Fade in over 8 frames (about 266ms at 30Hz) - slightly faster
    chordFadeAlpha = juce::jmin(1.0f, chordFadeAlpha + 0.125f);

    // Apply alpha to labels
    auto alphaColor = ChordAnalyzerLookAndFeel::Colors::textBright.withAlpha(chordFadeAlpha);
    chordNameLabel.setColour(juce::Label::textColourId, alphaColor);

    auto romanColor = ChordAnalyzerLookAndFeel::Colors::accentBlue.withAlpha(chordFadeAlpha);
    romanNumeralLabel.setColour(juce::Label::textColourId, romanColor);

    if (chordFadeAlpha >= 1.0f)
    {
        animatingChordChange = false;
        chordNameLabel.setColour(juce::Label::textColourId,
                                  ChordAnalyzerLookAndFeel::Colors::textBright);
        romanNumeralLabel.setColour(juce::Label::textColourId,
                                     ChordAnalyzerLookAndFeel::Colors::accentBlue);
    }
}

//==============================================================================
void ChordAnalyzerEditor::showTooltip(const juce::String& text)
{
    currentTooltipText = text;
    tooltipLabel.setText(text, juce::dontSendNotification);
}

void ChordAnalyzerEditor::clearTooltip()
{
    currentTooltipText = "";
    tooltipLabel.setText("Play some notes to see chord analysis...", juce::dontSendNotification);
}

//==============================================================================
void ChordAnalyzerEditor::toggleRecording()
{
    if (audioProcessor.isRecording())
    {
        audioProcessor.stopRecording();
    }
    else
    {
        audioProcessor.startRecording();
    }
}

void ChordAnalyzerEditor::clearRecording()
{
    audioProcessor.clearRecording();
    showTooltip("Recording cleared.");
}

//==============================================================================
void ChordAnalyzerEditor::SessionDragHandle::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const float corner = bounds.getHeight() * 0.35f;

    const auto base = ChordAnalyzerLookAndFeel::Colors::accentBlue.withAlpha(0.25f);
    g.setColour(isMouseButtonDown() ? base.brighter(0.15f)
                                     : (isMouseOver() ? base.brighter(0.08f) : base));
    g.fillRoundedRectangle(bounds, corner);

    g.setColour(ChordAnalyzerLookAndFeel::Colors::accentBlue.withAlpha(0.6f));
    g.drawRoundedRectangle(bounds, corner, 1.0f);

    g.setColour(ChordAnalyzerLookAndFeel::Colors::textBright);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
    g.drawText(juce::CharPointer_UTF8("DRAG \xe2\x86\x92 DAW"),
               getLocalBounds(), juce::Justification::centred, true);
}

void ChordAnalyzerEditor::SessionDragHandle::mouseDrag(const juce::MouseEvent& e)
{
    if (dragInitiated_)
        return;
    if (e.getDistanceFromDragStart() < 4)
        return;
    dragInitiated_ = true;
    if (onDragStart)
        onDragStart();
}

//==============================================================================
void ChordAnalyzerEditor::startDragForChord(const ChordInfo& chord)
{
    if (! chord.isValid || chord.midiNotes.empty())
    {
        showTooltip("No chord to drag — play some notes first.");
        return;
    }

    auto file = ChordMidiExport::writeChordToTempFile(chord);
    if (! file.existsAsFile())
    {
        showTooltip("Failed to write temporary MIDI file.");
        return;
    }

    juce::StringArray paths { file.getFullPathName() };
    performExternalDragDropOfFiles(paths, /*canMoveFiles*/ false);
}

void ChordAnalyzerEditor::startDragForSession()
{
    auto events = audioProcessor.getRecordedEvents();
    if (events.empty())
    {
        showTooltip("No chords recorded — start recording and play a progression first.");
        return;
    }

    // Get tempo from the recorded session if available, else default to
    // 120 BPM (matches the chord-clip drag behaviour for consistency).
    double bpm = 120.0;
    if (auto* head = audioProcessor.getPlayHead())
    {
        if (auto pos = head->getPosition())
            if (auto tempo = pos->getBpm())
                bpm = *tempo;
    }

    auto file = ChordMidiExport::writeSessionToTempFile(events, bpm);
    if (! file.existsAsFile())
    {
        showTooltip("Failed to write temporary MIDI file.");
        return;
    }

    juce::StringArray paths { file.getFullPathName() };
    performExternalDragDropOfFiles(paths, /*canMoveFiles*/ false);
}

//==============================================================================
void ChordAnalyzerEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
    {
        showSupportersPanel();
    }
    dragInitiatedFromEditor_ = false;
}

void ChordAnalyzerEditor::mouseUp(const juce::MouseEvent&)
{
    dragInitiatedFromEditor_ = false;
}

void ChordAnalyzerEditor::mouseMove(const juce::MouseEvent& e)
{
    // Show key tooltip when hovering over key selection
    if (keyHoverArea.contains(e.getPosition()))
    {
        showTooltip(TheoryTooltips::getKeyTip(audioProcessor.isMinorKey()));
    }
}

void ChordAnalyzerEditor::mouseDrag(const juce::MouseEvent& e)
{
    // Drags originating inside the history strip / dragSessionButton are
    // handled by those components themselves (see onChipDragStart and
    // dragSessionButton's mouse-handling below). The editor only kicks
    // in for the main chord display area, where there's no dedicated
    // sub-component to catch the drag.
    if (isDragAndDropActive() || dragInitiatedFromEditor_)
        return;
    if (e.getDistanceFromDragStart() < 4)
        return;

    if (chordDisplayDragArea.contains(e.getMouseDownPosition()))
    {
        dragInitiatedFromEditor_ = true;
        startDragForChord(cachedChord);
    }
}

//==============================================================================
void ChordAnalyzerEditor::showSupportersPanel()
{
    if (!supportersOverlay)
    {
#if CHORD_ANALYZER_MIDI_MODE
        supportersOverlay = std::make_unique<SupportersOverlay>("Chord Analyzer MIDI", JucePlugin_VersionString);
#else
        supportersOverlay = std::make_unique<SupportersOverlay>("Chord Analyzer", JucePlugin_VersionString);
#endif
        supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
        addAndMakeVisible(supportersOverlay.get());
    }

    supportersOverlay->setBounds(getLocalBounds());
    supportersOverlay->toFront(true);
    supportersOverlay->setVisible(true);
}

void ChordAnalyzerEditor::hideSupportersPanel()
{
    if (supportersOverlay)
        supportersOverlay->setVisible(false);
}

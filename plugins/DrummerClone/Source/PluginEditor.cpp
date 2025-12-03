#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DrummerEngine.h"
#include "GrooveTemplateGenerator.h"

//==============================================================================
// XYPad Implementation
//==============================================================================
XYPad::XYPad()
{
    setOpaque(false);
}

void XYPad::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    // Background
    g.setColour(juce::Colour(40, 40, 45));
    g.fillRoundedRectangle(bounds, 8.0f);

    // Border
    g.setColour(juce::Colour(60, 60, 65));
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

    // Grid lines
    g.setColour(juce::Colour(55, 55, 60));
    float centerX = bounds.getCentreX();
    float centerY = bounds.getCentreY();

    // Vertical center line
    g.drawLine(centerX, bounds.getY() + 10, centerX, bounds.getBottom() - 10, 0.5f);
    // Horizontal center line
    g.drawLine(bounds.getX() + 10, centerY, bounds.getRight() - 10, centerY, 0.5f);

    // Labels - X axis: Swing (left=none, right=full), Y axis: Loudness (top=loud, bottom=soft)
    g.setColour(juce::Colour(120, 120, 130));
    g.setFont(10.0f);
    g.drawText("No Swing", bounds.getX() + 5, bounds.getBottom() - 15, 55, 12, juce::Justification::left);
    g.drawText("Full Swing", bounds.getRight() - 60, bounds.getBottom() - 15, 55, 12, juce::Justification::right);
    g.drawText("Loud", bounds.getX() + 5, bounds.getY() + 3, 30, 12, juce::Justification::left);
    g.drawText("Soft", bounds.getX() + 5, bounds.getBottom() - 30, 30, 12, juce::Justification::left);

    // Position indicator
    float indicatorX = bounds.getX() + (posX * bounds.getWidth());
    float indicatorY = bounds.getY() + ((1.0f - posY) * bounds.getHeight());

    // Glow effect
    g.setColour(juce::Colour(100, 180, 255).withAlpha(0.3f));
    g.fillEllipse(indicatorX - 20, indicatorY - 20, 40, 40);

    // Main indicator
    g.setColour(juce::Colour(100, 180, 255));
    g.fillEllipse(indicatorX - 8, indicatorY - 8, 16, 16);

    // Inner highlight
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.fillEllipse(indicatorX - 4, indicatorY - 6, 6, 6);
}

void XYPad::resized()
{
}

void XYPad::mouseDown(const juce::MouseEvent& e)
{
    updatePositionFromMouse(e);
}

void XYPad::mouseDrag(const juce::MouseEvent& e)
{
    updatePositionFromMouse(e);
}

void XYPad::updatePositionFromMouse(const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    posX = juce::jlimit(0.0f, 1.0f, (e.position.x - bounds.getX()) / bounds.getWidth());
    posY = juce::jlimit(0.0f, 1.0f, 1.0f - ((e.position.y - bounds.getY()) / bounds.getHeight()));

    repaint();

    if (onPositionChanged)
        onPositionChanged(posX, posY);
}

void XYPad::setPosition(float x, float y)
{
    posX = juce::jlimit(0.0f, 1.0f, x);
    posY = juce::jlimit(0.0f, 1.0f, y);
    repaint();
}

//==============================================================================
// DrummerCloneAudioProcessorEditor Implementation
//==============================================================================
DrummerCloneAudioProcessorEditor::DrummerCloneAudioProcessorEditor(DrummerCloneAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      followModePanel(p),
      profileEditorPanel(p)
{
    // Set up dark theme
    darkLookAndFeel.setColourScheme(juce::LookAndFeel_V4::getDarkColourScheme());
    setLookAndFeel(&darkLookAndFeel);

    // Set window size
    setSize(850, 700);
    setResizable(true, true);
    setResizeLimits(700, 550, 1200, 900);

    // Setup all panels
    setupLibraryPanel();
    setupXYPad();
    setupGlobalControls();
    setupFollowModePanel();
    setupDetailsPanel();
    setupSectionPanel();
    setupFillsPanel();
    setupStepSequencer();
    setupHumanizationPanel();
    setupMidiCCPanel();
    setupProfileEditorPanel();
    setupKitPanel();
    setupStatusBar();

    // Start timer for UI updates
    startTimer(100);
}

DrummerCloneAudioProcessorEditor::~DrummerCloneAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DrummerCloneAudioProcessorEditor::setupLibraryPanel()
{
    // Library label
    libraryLabel.setText("LIBRARY", juce::dontSendNotification);
    libraryLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    libraryLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(libraryLabel);

    // Style selection (genre filter)
    styleLabel.setText("Genre", juce::dontSendNotification);
    styleLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(styleLabel);

    styleComboBox.addItem("Rock", 1);
    styleComboBox.addItem("HipHop", 2);
    styleComboBox.addItem("Alternative", 3);
    styleComboBox.addItem("R&B", 4);
    styleComboBox.addItem("Electronic", 5);
    styleComboBox.addItem("Trap", 6);
    styleComboBox.addItem("Songwriter", 7);
    addAndMakeVisible(styleComboBox);

    // When genre changes, update the drummer list
    styleComboBox.onChange = [this]()
    {
        int styleIndex = styleComboBox.getSelectedId() - 1;  // 0-based index
        updateDrummerListForStyle(styleIndex);
    };

    styleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "style", styleComboBox);

    // Drummer selection
    drummerLabel.setText("Drummer", juce::dontSendNotification);
    drummerLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(drummerLabel);

    addAndMakeVisible(drummerComboBox);

    // When drummer changes, update the processor with the correct drummer index
    drummerComboBox.onChange = [this]()
    {
        int comboIndex = drummerComboBox.getSelectedId() - 1;  // 0-based
        if (comboIndex >= 0 && comboIndex < static_cast<int>(filteredDrummerIndices.size()))
        {
            int globalDrummerIndex = filteredDrummerIndices[static_cast<size_t>(comboIndex)];
            // Set the drummer parameter (normalized 0-1)
            auto* drummerParam = audioProcessor.getValueTreeState().getParameter("drummer");
            if (drummerParam)
            {
                // Normalize: 29 drummers total, so drummer N = N/28
                float normalized = static_cast<float>(globalDrummerIndex) / 28.0f;
                drummerParam->setValueNotifyingHost(normalized);
            }
        }
    };

    // Initialize with Rock genre drummers
    updateDrummerListForStyle(0);
}

void DrummerCloneAudioProcessorEditor::updateDrummerListForStyle(int styleIndex)
{
    // Style names matching DrummerDNA order
    static const juce::StringArray styleNames = {"Rock", "HipHop", "Alternative", "R&B", "Electronic", "Trap", "Songwriter"};

    // All drummers with their global indices and styles
    // This must match the order in DrummerDNA::createDefaultProfiles()
    static const std::vector<std::pair<juce::String, juce::String>> allDrummers = {
        // Rock (0-2)
        {"Kyle", "Rock"},
        {"Anders", "Rock"},
        {"Max", "Rock"},
        // Alternative (3-4)
        {"Logan", "Alternative"},
        {"Aidan", "Alternative"},
        // HipHop (5-6)
        {"Austin", "HipHop"},
        {"Tyrell", "HipHop"},
        // R&B (7-8)
        {"Brooklyn", "R&B"},
        {"Darnell", "R&B"},
        // Electronic (9-10)
        {"Niklas", "Electronic"},
        {"Lexi", "Electronic"},
        // Songwriter (11-14)
        {"Jesse", "Songwriter"},
        {"Maya", "Songwriter"},
        {"Emily", "Songwriter"},
        {"Sam", "Songwriter"},
        // Trap (15-18)
        {"Xavier", "Trap"},
        {"Jayden", "Trap"},
        {"Zion", "Trap"},
        {"Luna", "Trap"},
        // Additional Rock (19-20)
        {"Ricky", "Rock"},
        {"Jake", "Rock"},
        // Additional Alternative (21-22)
        {"River", "Alternative"},
        {"Quinn", "Alternative"},
        // Additional HipHop (23-24)
        {"Marcus", "HipHop"},
        {"Kira", "HipHop"},
        // Additional R&B (25-26)
        {"Aaliyah", "R&B"},
        {"Andre", "R&B"},
        // Additional Electronic (27-28)
        {"Sasha", "Electronic"},
        {"Felix", "Electronic"}
    };

    juce::String targetStyle = styleNames[juce::jlimit(0, styleNames.size() - 1, styleIndex)];

    // Clear and rebuild the drummer list
    drummerComboBox.clear(juce::dontSendNotification);
    filteredDrummerIndices.clear();

    int comboId = 1;
    for (size_t i = 0; i < allDrummers.size(); ++i)
    {
        if (allDrummers[i].second == targetStyle)
        {
            drummerComboBox.addItem(allDrummers[i].first, comboId);
            filteredDrummerIndices.push_back(static_cast<int>(i));
            comboId++;
        }
    }

    // Select the first drummer in this genre
    if (drummerComboBox.getNumItems() > 0)
    {
        drummerComboBox.setSelectedId(1, juce::sendNotificationSync);
    }
}

void DrummerCloneAudioProcessorEditor::setupXYPad()
{
    addAndMakeVisible(xyPad);

    xyPad.onPositionChanged = [this](float x, float y)
    {
        // X axis: Complexity (simple → complex) - matches Logic Pro Drummer
        auto* complexityParam = audioProcessor.getValueTreeState().getParameter("complexity");
        if (complexityParam)
            complexityParam->setValueNotifyingHost(x);

        // Y axis: Loudness (soft → loud) - matches Logic Pro Drummer
        auto* loudnessParam = audioProcessor.getValueTreeState().getParameter("loudness");
        if (loudnessParam)
            loudnessParam->setValueNotifyingHost(y);
    };

    xyLabel.setText("Simple ← → Complex  /  Soft ↑ Loud", juce::dontSendNotification);
    xyLabel.setFont(juce::Font(12.0f));
    xyLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    xyLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(xyLabel);
}

void DrummerCloneAudioProcessorEditor::setupGlobalControls()
{
    // Swing slider (moved from XY pad - now XY pad controls Complexity/Loudness like Logic)
    swingSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    swingSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    swingSlider.setRange(0.0, 100.0, 1.0);
    swingSlider.setValue(0.0);
    addAndMakeVisible(swingSlider);

    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "swing", swingSlider);

    swingLabel.setText("Swing", juce::dontSendNotification);
    swingLabel.setJustificationType(juce::Justification::centred);
    swingLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(swingLabel);

    // Complexity slider (also controlled via XY pad X-axis for Logic-style control)
    complexitySlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    complexitySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    complexitySlider.setRange(1.0, 10.0, 0.1);
    complexitySlider.setValue(5.0);
    addAndMakeVisible(complexitySlider);

    complexityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "complexity", complexitySlider);

    complexityLabel.setText("Complexity", juce::dontSendNotification);
    complexityLabel.setJustificationType(juce::Justification::centred);
    complexityLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(complexityLabel);

    // Loudness slider (also controlled via XY pad Y-axis for Logic-style control)
    loudnessSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    loudnessSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    loudnessSlider.setRange(0.0, 100.0, 1.0);
    loudnessSlider.setValue(75.0);
    addAndMakeVisible(loudnessSlider);

    loudnessAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "loudness", loudnessSlider);

    loudnessLabel.setText("Loudness", juce::dontSendNotification);
    loudnessLabel.setJustificationType(juce::Justification::centred);
    loudnessLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(loudnessLabel);

    // Generate button
    generateButton.setButtonText("Generate");
    generateButton.onClick = [this]()
    {
        // Trigger regeneration
        statusLabel.setText("Generating pattern...", juce::dontSendNotification);
    };
    addAndMakeVisible(generateButton);

    // Export bars selection
    exportBarsLabel.setText("Bars:", juce::dontSendNotification);
    exportBarsLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(exportBarsLabel);

    exportBarsComboBox.addItem("4 bars", 1);
    exportBarsComboBox.addItem("8 bars", 2);
    exportBarsComboBox.addItem("16 bars", 3);
    exportBarsComboBox.addItem("32 bars", 4);
    exportBarsComboBox.setSelectedId(2);  // Default to 8 bars
    addAndMakeVisible(exportBarsComboBox);

    // Export button
    exportButton.setButtonText("Export MIDI");
    exportButton.onClick = [this]() { exportToMidiFile(); };
    addAndMakeVisible(exportButton);
}

void DrummerCloneAudioProcessorEditor::setupFollowModePanel()
{
    addAndMakeVisible(followModePanel);
}

void DrummerCloneAudioProcessorEditor::setupDetailsPanel()
{
    // Details toggle button
    detailsToggleButton.setButtonText("Details");
    detailsToggleButton.onClick = [this]()
    {
        detailsPanelVisible = !detailsPanelVisible;
        resized();
    };
    addAndMakeVisible(detailsToggleButton);

    // Kick pattern
    kickPatternLabel.setText("Kick", juce::dontSendNotification);
    kickPatternLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(kickPatternLabel);

    kickPatternComboBox.addItem("Basic 4/4", 1);
    kickPatternComboBox.addItem("Syncopated", 2);
    kickPatternComboBox.addItem("Offbeat", 3);
    kickPatternComboBox.addItem("Double Kick", 4);
    kickPatternComboBox.setSelectedId(1);
    addAndMakeVisible(kickPatternComboBox);

    // Snare pattern
    snarePatternLabel.setText("Snare", juce::dontSendNotification);
    snarePatternLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(snarePatternLabel);

    snarePatternComboBox.addItem("Backbeat", 1);
    snarePatternComboBox.addItem("Syncopated", 2);
    snarePatternComboBox.addItem("Ghost Notes", 3);
    snarePatternComboBox.setSelectedId(1);
    addAndMakeVisible(snarePatternComboBox);

    // Hi-hat open amount
    hiHatOpenLabel.setText("Hi-Hat Open", juce::dontSendNotification);
    hiHatOpenLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(hiHatOpenLabel);

    hiHatOpenSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    hiHatOpenSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    hiHatOpenSlider.setRange(0.0, 100.0, 1.0);
    hiHatOpenSlider.setValue(20.0);
    addAndMakeVisible(hiHatOpenSlider);

    // Percussion toggle
    percussionToggle.setButtonText("Percussion");
    percussionToggle.setToggleState(false, juce::dontSendNotification);
    addAndMakeVisible(percussionToggle);

    // Initially hide details
    kickPatternLabel.setVisible(false);
    kickPatternComboBox.setVisible(false);
    snarePatternLabel.setVisible(false);
    snarePatternComboBox.setVisible(false);
    hiHatOpenLabel.setVisible(false);
    hiHatOpenSlider.setVisible(false);
    percussionToggle.setVisible(false);
}

void DrummerCloneAudioProcessorEditor::setupSectionPanel()
{
    // Section label
    sectionLabel.setText("SECTION", juce::dontSendNotification);
    sectionLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    sectionLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(sectionLabel);

    // Section combo box with color-coded items
    sectionComboBox.addItem("Intro", 1);
    sectionComboBox.addItem("Verse", 2);
    sectionComboBox.addItem("Pre-Chorus", 3);
    sectionComboBox.addItem("Chorus", 4);
    sectionComboBox.addItem("Bridge", 5);
    sectionComboBox.addItem("Breakdown", 6);
    sectionComboBox.addItem("Outro", 7);
    sectionComboBox.setSelectedId(2);  // Default to Verse
    addAndMakeVisible(sectionComboBox);

    sectionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "section", sectionComboBox);
}

void DrummerCloneAudioProcessorEditor::setupFillsPanel()
{
    // Section label
    fillsLabel.setText("FILLS", juce::dontSendNotification);
    fillsLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    fillsLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(fillsLabel);

    // Fill frequency slider
    fillFrequencySlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    fillFrequencySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    fillFrequencySlider.setRange(0.0, 100.0, 1.0);
    fillFrequencySlider.setValue(30.0);
    fillFrequencySlider.setTextValueSuffix("%");
    addAndMakeVisible(fillFrequencySlider);

    fillFrequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "fillFrequency", fillFrequencySlider);

    fillFrequencyLabel.setText("Frequency", juce::dontSendNotification);
    fillFrequencyLabel.setJustificationType(juce::Justification::centred);
    fillFrequencyLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fillFrequencyLabel.setFont(juce::Font(10.0f));
    addAndMakeVisible(fillFrequencyLabel);

    // Fill intensity slider
    fillIntensitySlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    fillIntensitySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    fillIntensitySlider.setRange(0.0, 100.0, 1.0);
    fillIntensitySlider.setValue(50.0);
    fillIntensitySlider.setTextValueSuffix("%");
    addAndMakeVisible(fillIntensitySlider);

    fillIntensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "fillIntensity", fillIntensitySlider);

    fillIntensityLabel.setText("Intensity", juce::dontSendNotification);
    fillIntensityLabel.setJustificationType(juce::Justification::centred);
    fillIntensityLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fillIntensityLabel.setFont(juce::Font(10.0f));
    addAndMakeVisible(fillIntensityLabel);

    // Fill length combo box
    fillLengthComboBox.addItem("1 Beat", 1);
    fillLengthComboBox.addItem("2 Beats", 2);
    fillLengthComboBox.addItem("4 Beats", 3);
    fillLengthComboBox.setSelectedId(1);
    addAndMakeVisible(fillLengthComboBox);

    fillLengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "fillLength", fillLengthComboBox);

    fillLengthLabel.setText("Length", juce::dontSendNotification);
    fillLengthLabel.setJustificationType(juce::Justification::centred);
    fillLengthLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fillLengthLabel.setFont(juce::Font(10.0f));
    addAndMakeVisible(fillLengthLabel);

    // Manual fill trigger button
    fillTriggerButton.setButtonText("FILL!");
    fillTriggerButton.setColour(juce::TextButton::buttonColourId, juce::Colour(180, 80, 80));
    fillTriggerButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(220, 100, 100));
    fillTriggerButton.onClick = [this]()
    {
        // Set the trigger parameter to trigger a fill
        auto* triggerParam = audioProcessor.getValueTreeState().getParameter("fillTrigger");
        if (triggerParam)
        {
            triggerParam->setValueNotifyingHost(1.0f);
            // Reset after a short delay (the processor should handle this)
            juce::Timer::callAfterDelay(100, [triggerParam]() {
                triggerParam->setValueNotifyingHost(0.0f);
            });
        }
        statusLabel.setText("Fill triggered!", juce::dontSendNotification);
    };
    addAndMakeVisible(fillTriggerButton);
}

void DrummerCloneAudioProcessorEditor::setupStepSequencer()
{
    // Toggle button - now also enables/disables step sequencer mode
    stepSeqToggleButton.setButtonText("Step Sequencer");
    stepSeqToggleButton.setClickingTogglesState(true);
    stepSeqToggleButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(80, 150, 80));
    stepSeqToggleButton.onClick = [this]()
    {
        stepSeqVisible = stepSeqToggleButton.getToggleState();
        stepSequencer.setVisible(stepSeqVisible);

        // Enable/disable step sequencer mode in processor
        audioProcessor.setStepSequencerEnabled(stepSeqVisible);

        if (stepSeqVisible)
            statusLabel.setText("Step Sequencer: ON", juce::dontSendNotification);
        else
            statusLabel.setText("Step Sequencer: OFF", juce::dontSendNotification);

        resized();
    };
    addAndMakeVisible(stepSeqToggleButton);

    // Step sequencer component (hidden by default)
    stepSequencer.setVisible(false);
    addAndMakeVisible(stepSequencer);

    // Wire up the pattern changed callback to update the processor
    stepSequencer.onPatternChanged = [this]()
    {
        // Convert UI pattern to processor format
        const auto& uiPattern = stepSequencer.getPattern();

        StepSequencerPattern procPattern;
        procPattern.enabled = audioProcessor.isStepSequencerEnabled();

        for (int lane = 0; lane < StepSequencerPattern::NumLanes; ++lane)
        {
            for (int step = 0; step < StepSequencerPattern::NumSteps; ++step)
            {
                const auto& uiStep = uiPattern[static_cast<size_t>(lane)][static_cast<size_t>(step)];
                procPattern.pattern[static_cast<size_t>(lane)][static_cast<size_t>(step)].active = uiStep.active;
                procPattern.pattern[static_cast<size_t>(lane)][static_cast<size_t>(step)].velocity = uiStep.velocity;
            }
        }

        audioProcessor.setStepSequencerPattern(procPattern);
        statusLabel.setText("Pattern modified", juce::dontSendNotification);
    };
}

void DrummerCloneAudioProcessorEditor::setupHumanizationPanel()
{
    // Section label
    humanLabel.setText("HUMANIZE", juce::dontSendNotification);
    humanLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    humanLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    humanLabel.setVisible(false);
    addAndMakeVisible(humanLabel);

    // Timing variation slider
    humanTimingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    humanTimingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 16);
    humanTimingSlider.setRange(0.0, 100.0, 1.0);
    humanTimingSlider.setValue(20.0);
    humanTimingSlider.setTextValueSuffix("%");
    humanTimingSlider.setVisible(false);
    addAndMakeVisible(humanTimingSlider);

    humanTimingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "humanTiming", humanTimingSlider);

    humanTimingLabel.setText("Timing", juce::dontSendNotification);
    humanTimingLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    humanTimingLabel.setFont(juce::Font(10.0f));
    humanTimingLabel.setVisible(false);
    addAndMakeVisible(humanTimingLabel);

    // Velocity variation slider
    humanVelocitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    humanVelocitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 16);
    humanVelocitySlider.setRange(0.0, 100.0, 1.0);
    humanVelocitySlider.setValue(15.0);
    humanVelocitySlider.setTextValueSuffix("%");
    humanVelocitySlider.setVisible(false);
    addAndMakeVisible(humanVelocitySlider);

    humanVelocityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "humanVelocity", humanVelocitySlider);

    humanVelocityLabel.setText("Velocity", juce::dontSendNotification);
    humanVelocityLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    humanVelocityLabel.setFont(juce::Font(10.0f));
    humanVelocityLabel.setVisible(false);
    addAndMakeVisible(humanVelocityLabel);

    // Push/Drag feel slider (bipolar)
    humanPushSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    humanPushSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 16);
    humanPushSlider.setRange(-50.0, 50.0, 1.0);
    humanPushSlider.setValue(0.0);
    humanPushSlider.setVisible(false);
    addAndMakeVisible(humanPushSlider);

    humanPushAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "humanPush", humanPushSlider);

    humanPushLabel.setText("Push/Drag", juce::dontSendNotification);
    humanPushLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    humanPushLabel.setFont(juce::Font(10.0f));
    humanPushLabel.setVisible(false);
    addAndMakeVisible(humanPushLabel);

    // Groove depth slider
    humanGrooveSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    humanGrooveSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 16);
    humanGrooveSlider.setRange(0.0, 100.0, 1.0);
    humanGrooveSlider.setValue(50.0);
    humanGrooveSlider.setTextValueSuffix("%");
    humanGrooveSlider.setVisible(false);
    addAndMakeVisible(humanGrooveSlider);

    humanGrooveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "humanGroove", humanGrooveSlider);

    humanGrooveLabel.setText("Groove", juce::dontSendNotification);
    humanGrooveLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    humanGrooveLabel.setFont(juce::Font(10.0f));
    humanGrooveLabel.setVisible(false);
    addAndMakeVisible(humanGrooveLabel);

    // Toggle button for humanization panel
    humanToggleButton.setButtonText("Humanize");
    humanToggleButton.onClick = [this]()
    {
        humanPanelVisible = !humanPanelVisible;
        humanLabel.setVisible(humanPanelVisible);
        humanTimingSlider.setVisible(humanPanelVisible);
        humanTimingLabel.setVisible(humanPanelVisible);
        humanVelocitySlider.setVisible(humanPanelVisible);
        humanVelocityLabel.setVisible(humanPanelVisible);
        humanPushSlider.setVisible(humanPanelVisible);
        humanPushLabel.setVisible(humanPanelVisible);
        humanGrooveSlider.setVisible(humanPanelVisible);
        humanGrooveLabel.setVisible(humanPanelVisible);
        resized();
    };
    addAndMakeVisible(humanToggleButton);
}

void DrummerCloneAudioProcessorEditor::setupMidiCCPanel()
{
    // Section label (hidden by default)
    midiCCLabel.setText("MIDI CC CONTROL", juce::dontSendNotification);
    midiCCLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    midiCCLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    midiCCLabel.setVisible(false);
    addAndMakeVisible(midiCCLabel);

    // Enable toggle
    midiCCEnableToggle.setButtonText("Enable MIDI CC");
    midiCCEnableToggle.setVisible(false);
    addAndMakeVisible(midiCCEnableToggle);

    midiCCEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "midiCCEnabled", midiCCEnableToggle);

    // Section CC# slider
    sectionCCSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sectionCCSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 16);
    sectionCCSlider.setRange(1.0, 127.0, 1.0);
    sectionCCSlider.setValue(102.0);
    sectionCCSlider.setVisible(false);
    addAndMakeVisible(sectionCCSlider);

    sectionCCAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "sectionCC", sectionCCSlider);

    sectionCCLabel.setText("Section CC#", juce::dontSendNotification);
    sectionCCLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    sectionCCLabel.setFont(juce::Font(10.0f));
    sectionCCLabel.setVisible(false);
    addAndMakeVisible(sectionCCLabel);

    // Fill trigger CC# slider
    fillCCSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fillCCSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 16);
    fillCCSlider.setRange(1.0, 127.0, 1.0);
    fillCCSlider.setValue(103.0);
    fillCCSlider.setVisible(false);
    addAndMakeVisible(fillCCSlider);

    fillCCAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "fillTriggerCC", fillCCSlider);

    fillCCLabel.setText("Fill CC#", juce::dontSendNotification);
    fillCCLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fillCCLabel.setFont(juce::Font(10.0f));
    fillCCLabel.setVisible(false);
    addAndMakeVisible(fillCCLabel);

    // Source indicator (shows "MIDI" when section is controlled via MIDI)
    midiCCSourceIndicator.setText("", juce::dontSendNotification);
    midiCCSourceIndicator.setFont(juce::Font(9.0f, juce::Font::bold));
    midiCCSourceIndicator.setColour(juce::Label::textColourId, juce::Colour(100, 200, 100));
    midiCCSourceIndicator.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(midiCCSourceIndicator);

    // Toggle button for MIDI CC panel
    midiCCToggleButton.setButtonText("MIDI Control");
    midiCCToggleButton.onClick = [this]()
    {
        midiCCPanelVisible = !midiCCPanelVisible;
        midiCCLabel.setVisible(midiCCPanelVisible);
        midiCCEnableToggle.setVisible(midiCCPanelVisible);
        sectionCCSlider.setVisible(midiCCPanelVisible);
        sectionCCLabel.setVisible(midiCCPanelVisible);
        fillCCSlider.setVisible(midiCCPanelVisible);
        fillCCLabel.setVisible(midiCCPanelVisible);
        resized();
    };
    addAndMakeVisible(midiCCToggleButton);
}

void DrummerCloneAudioProcessorEditor::setupProfileEditorPanel()
{
    // Profile editor panel (hidden by default)
    profileEditorPanel.setVisible(false);
    addAndMakeVisible(profileEditorPanel);

    // Profile editor toggle button
    profileEditorToggleButton.setButtonText("Profile Editor");
    profileEditorToggleButton.onClick = [this]()
    {
        profileEditorVisible = !profileEditorVisible;
        profileEditorPanel.setVisible(profileEditorVisible);

        if (profileEditorVisible)
            statusLabel.setText("Profile Editor: ON", juce::dontSendNotification);
        else
            statusLabel.setText("Profile Editor: OFF", juce::dontSendNotification);

        resized();
    };
    addAndMakeVisible(profileEditorToggleButton);

    // Set up callback when profile changes
    profileEditorPanel.onProfileChanged = [this](const DrummerProfile& profile)
    {
        // TODO: Apply custom profile to the drummer engine
        // For now just show the profile name in the status bar
        statusLabel.setText("Editing: " + profile.name, juce::dontSendNotification);
    };
}

void DrummerCloneAudioProcessorEditor::setupKitPanel()
{
    // Kit enable label (hidden by default)
    kitLabel.setText("KIT PIECES", juce::dontSendNotification);
    kitLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    kitLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    kitLabel.setVisible(false);
    addAndMakeVisible(kitLabel);

    // Kit piece toggles
    kitKickToggle.setButtonText("Kick");
    kitKickToggle.setToggleState(true, juce::dontSendNotification);
    kitKickToggle.setVisible(false);
    addAndMakeVisible(kitKickToggle);
    kitKickAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "kitKick", kitKickToggle);

    kitSnareToggle.setButtonText("Snare");
    kitSnareToggle.setToggleState(true, juce::dontSendNotification);
    kitSnareToggle.setVisible(false);
    addAndMakeVisible(kitSnareToggle);
    kitSnareAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "kitSnare", kitSnareToggle);

    kitHiHatToggle.setButtonText("Hi-Hat");
    kitHiHatToggle.setToggleState(true, juce::dontSendNotification);
    kitHiHatToggle.setVisible(false);
    addAndMakeVisible(kitHiHatToggle);
    kitHiHatAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "kitHiHat", kitHiHatToggle);

    kitTomsToggle.setButtonText("Toms");
    kitTomsToggle.setToggleState(true, juce::dontSendNotification);
    kitTomsToggle.setVisible(false);
    addAndMakeVisible(kitTomsToggle);
    kitTomsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "kitToms", kitTomsToggle);

    kitCymbalsToggle.setButtonText("Cymbals");
    kitCymbalsToggle.setToggleState(true, juce::dontSendNotification);
    kitCymbalsToggle.setVisible(false);
    addAndMakeVisible(kitCymbalsToggle);
    kitCymbalsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "kitCymbals", kitCymbalsToggle);

    kitPercussionToggle.setButtonText("Percussion");
    kitPercussionToggle.setToggleState(true, juce::dontSendNotification);
    kitPercussionToggle.setVisible(false);
    addAndMakeVisible(kitPercussionToggle);
    kitPercussionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "kitPercussion", kitPercussionToggle);

    // Toggle button for kit panel
    kitToggleButton.setButtonText("Kit Pieces");
    kitToggleButton.onClick = [this]()
    {
        kitPanelVisible = !kitPanelVisible;
        kitLabel.setVisible(kitPanelVisible);
        kitKickToggle.setVisible(kitPanelVisible);
        kitSnareToggle.setVisible(kitPanelVisible);
        kitHiHatToggle.setVisible(kitPanelVisible);
        kitTomsToggle.setVisible(kitPanelVisible);
        kitCymbalsToggle.setVisible(kitPanelVisible);
        kitPercussionToggle.setVisible(kitPanelVisible);
        resized();
    };
    addAndMakeVisible(kitToggleButton);
}

void DrummerCloneAudioProcessorEditor::setupStatusBar()
{
    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(11.0f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(statusLabel);
}

void DrummerCloneAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Dark background
    g.fillAll(juce::Colour(30, 30, 35));

    // Left panel background
    g.setColour(juce::Colour(25, 25, 30));
    g.fillRect(0, 0, 180, getHeight());

    // Separator line
    g.setColour(juce::Colour(50, 50, 55));
    g.drawLine(180, 0, 180, static_cast<float>(getHeight()), 1.0f);

    // Top bar background
    g.setColour(juce::Colour(35, 35, 40));
    g.fillRect(180, 0, getWidth() - 180, 80);

    // Bottom panel separator
    int bottomPanelY = getHeight() - 150;
    g.setColour(juce::Colour(50, 50, 55));
    g.drawLine(180, static_cast<float>(bottomPanelY), static_cast<float>(getWidth()), static_cast<float>(bottomPanelY), 1.0f);

    // Fills panel separator (vertical line between details and fills)
    int fillsPanelX = getWidth() - 250 - 180;  // Follow mode width + fills width
    g.drawLine(static_cast<float>(fillsPanelX), static_cast<float>(bottomPanelY + 10),
               static_cast<float>(fillsPanelX), static_cast<float>(getHeight() - 30), 1.0f);

    // Separator between fills and follow mode
    int followPanelX = getWidth() - 250;
    g.drawLine(static_cast<float>(followPanelX), static_cast<float>(bottomPanelY + 10),
               static_cast<float>(followPanelX), static_cast<float>(getHeight() - 30), 1.0f);
}

void DrummerCloneAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    int leftPanelWidth = 180;
    int topBarHeight = 80;
    int bottomPanelHeight = 240;  // Increased for Follow Mode Learn/Lock buttons
    int statusBarHeight = 25;

    // ========== LEFT PANEL ==========
    auto leftPanel = bounds.removeFromLeft(leftPanelWidth).reduced(10);

    libraryLabel.setBounds(leftPanel.removeFromTop(25));
    leftPanel.removeFromTop(10);

    styleLabel.setBounds(leftPanel.removeFromTop(18));
    styleComboBox.setBounds(leftPanel.removeFromTop(28).reduced(0, 2));
    leftPanel.removeFromTop(15);

    drummerLabel.setBounds(leftPanel.removeFromTop(18));
    drummerComboBox.setBounds(leftPanel.removeFromTop(28).reduced(0, 2));
    leftPanel.removeFromTop(20);

    // Section selector
    auto sectionLabelRow = leftPanel.removeFromTop(18);
    sectionLabel.setBounds(sectionLabelRow.removeFromLeft(60));
    midiCCSourceIndicator.setBounds(sectionLabelRow);  // Shows "MIDI" indicator when CC controlling
    leftPanel.removeFromTop(5);
    sectionComboBox.setBounds(leftPanel.removeFromTop(28).reduced(0, 2));
    leftPanel.removeFromTop(15);

    // Humanize toggle button
    humanToggleButton.setBounds(leftPanel.removeFromTop(25).reduced(0, 2));

    // Humanization panel (collapsible)
    if (humanPanelVisible)
    {
        leftPanel.removeFromTop(10);
        humanLabel.setBounds(leftPanel.removeFromTop(18));
        leftPanel.removeFromTop(5);

        auto timingRow = leftPanel.removeFromTop(22);
        humanTimingLabel.setBounds(timingRow.removeFromLeft(50));
        humanTimingSlider.setBounds(timingRow);
        leftPanel.removeFromTop(3);

        auto velocityRow = leftPanel.removeFromTop(22);
        humanVelocityLabel.setBounds(velocityRow.removeFromLeft(50));
        humanVelocitySlider.setBounds(velocityRow);
        leftPanel.removeFromTop(3);

        auto pushRow = leftPanel.removeFromTop(22);
        humanPushLabel.setBounds(pushRow.removeFromLeft(50));
        humanPushSlider.setBounds(pushRow);
        leftPanel.removeFromTop(3);

        auto grooveRow = leftPanel.removeFromTop(22);
        humanGrooveLabel.setBounds(grooveRow.removeFromLeft(50));
        humanGrooveSlider.setBounds(grooveRow);
    }

    // MIDI CC toggle button
    leftPanel.removeFromTop(10);
    midiCCToggleButton.setBounds(leftPanel.removeFromTop(25).reduced(0, 2));

    // MIDI CC panel (collapsible)
    if (midiCCPanelVisible)
    {
        leftPanel.removeFromTop(10);
        midiCCLabel.setBounds(leftPanel.removeFromTop(18));
        leftPanel.removeFromTop(5);

        midiCCEnableToggle.setBounds(leftPanel.removeFromTop(22));
        leftPanel.removeFromTop(5);

        auto sectionCCRow = leftPanel.removeFromTop(22);
        sectionCCLabel.setBounds(sectionCCRow.removeFromLeft(65));
        sectionCCSlider.setBounds(sectionCCRow);
        leftPanel.removeFromTop(3);

        auto fillCCRow = leftPanel.removeFromTop(22);
        fillCCLabel.setBounds(fillCCRow.removeFromLeft(65));
        fillCCSlider.setBounds(fillCCRow);
    }

    // Kit pieces toggle button
    leftPanel.removeFromTop(10);
    kitToggleButton.setBounds(leftPanel.removeFromTop(25).reduced(0, 2));

    // Kit pieces panel (collapsible)
    if (kitPanelVisible)
    {
        leftPanel.removeFromTop(10);
        kitLabel.setBounds(leftPanel.removeFromTop(18));
        leftPanel.removeFromTop(5);

        kitKickToggle.setBounds(leftPanel.removeFromTop(20));
        kitSnareToggle.setBounds(leftPanel.removeFromTop(20));
        kitHiHatToggle.setBounds(leftPanel.removeFromTop(20));
        kitTomsToggle.setBounds(leftPanel.removeFromTop(20));
        kitCymbalsToggle.setBounds(leftPanel.removeFromTop(20));
        kitPercussionToggle.setBounds(leftPanel.removeFromTop(20));
    }

    // ========== TOP BAR ==========
    auto topBar = bounds.removeFromTop(topBarHeight);
    topBar = topBar.reduced(20, 10);

    auto swingArea = topBar.removeFromLeft(70);
    swingSlider.setBounds(swingArea.removeFromTop(50));
    swingLabel.setBounds(swingArea);

    topBar.removeFromLeft(15);

    auto complexityArea = topBar.removeFromLeft(70);
    complexitySlider.setBounds(complexityArea.removeFromTop(50));
    complexityLabel.setBounds(complexityArea);

    topBar.removeFromLeft(15);

    auto loudnessArea = topBar.removeFromLeft(70);
    loudnessSlider.setBounds(loudnessArea.removeFromTop(50));
    loudnessLabel.setBounds(loudnessArea);

    topBar.removeFromLeft(15);
    generateButton.setBounds(topBar.removeFromLeft(100).reduced(0, 15));

    // Export controls on the right side of top bar
    auto exportArea = topBar.removeFromRight(250);
    exportButton.setBounds(exportArea.removeFromRight(100).reduced(0, 15));
    exportArea.removeFromRight(5);
    exportBarsComboBox.setBounds(exportArea.removeFromRight(80).reduced(0, 20));
    exportArea.removeFromRight(5);
    exportBarsLabel.setBounds(exportArea.removeFromRight(35).reduced(0, 25));

    // ========== STATUS BAR ==========
    statusLabel.setBounds(bounds.removeFromBottom(statusBarHeight).reduced(10, 5));

    // ========== BOTTOM PANEL ==========
    auto bottomPanel = bounds.removeFromBottom(bottomPanelHeight);

    // Follow mode panel on the right side of bottom
    followModePanel.setBounds(bottomPanel.removeFromRight(250).reduced(10));

    // ========== FILLS PANEL (between details and follow mode) ==========
    auto fillsArea = bottomPanel.removeFromRight(180).reduced(10);
    fillsLabel.setBounds(fillsArea.removeFromTop(18));
    fillsArea.removeFromTop(5);

    // Two knobs side by side
    auto knobRow = fillsArea.removeFromTop(65);
    auto freqArea = knobRow.removeFromLeft(80);
    fillFrequencySlider.setBounds(freqArea.removeFromTop(50));
    fillFrequencyLabel.setBounds(freqArea);

    knobRow.removeFromLeft(10);
    auto intensityArea = knobRow;
    fillIntensitySlider.setBounds(intensityArea.removeFromTop(50));
    fillIntensityLabel.setBounds(intensityArea);

    fillsArea.removeFromTop(5);

    // Length combo and trigger button
    auto lengthRow = fillsArea.removeFromTop(22);
    fillLengthLabel.setBounds(lengthRow.removeFromLeft(45));
    fillLengthComboBox.setBounds(lengthRow.reduced(0, 1));

    fillsArea.removeFromTop(5);
    fillTriggerButton.setBounds(fillsArea.removeFromTop(28));

    // Details toggle and panel
    auto detailsArea = bottomPanel.reduced(10);
    detailsToggleButton.setBounds(detailsArea.removeFromTop(25).removeFromLeft(80));

    if (detailsPanelVisible)
    {
        detailsArea.removeFromTop(10);

        kickPatternLabel.setVisible(true);
        kickPatternComboBox.setVisible(true);
        snarePatternLabel.setVisible(true);
        snarePatternComboBox.setVisible(true);
        hiHatOpenLabel.setVisible(true);
        hiHatOpenSlider.setVisible(true);
        percussionToggle.setVisible(true);

        auto row1 = detailsArea.removeFromTop(25);
        kickPatternLabel.setBounds(row1.removeFromLeft(40));
        kickPatternComboBox.setBounds(row1.removeFromLeft(120));
        row1.removeFromLeft(20);
        snarePatternLabel.setBounds(row1.removeFromLeft(45));
        snarePatternComboBox.setBounds(row1.removeFromLeft(120));

        detailsArea.removeFromTop(8);
        auto row2 = detailsArea.removeFromTop(25);
        hiHatOpenLabel.setBounds(row2.removeFromLeft(70));
        hiHatOpenSlider.setBounds(row2.removeFromLeft(150));
        row2.removeFromLeft(20);
        percussionToggle.setBounds(row2.removeFromLeft(100));
    }
    else
    {
        kickPatternLabel.setVisible(false);
        kickPatternComboBox.setVisible(false);
        snarePatternLabel.setVisible(false);
        snarePatternComboBox.setVisible(false);
        hiHatOpenLabel.setVisible(false);
        hiHatOpenSlider.setVisible(false);
        percussionToggle.setVisible(false);
    }

    // ========== CENTER (XY PAD + STEP SEQUENCER + PROFILE EDITOR) ==========
    auto centerArea = bounds.reduced(20);

    // Toggle buttons at top
    auto topRow = centerArea.removeFromTop(25);
    stepSeqToggleButton.setBounds(topRow.removeFromRight(120));
    topRow.removeFromRight(5);
    profileEditorToggleButton.setBounds(topRow.removeFromRight(100));

    xyLabel.setBounds(topRow.removeFromLeft(120));
    centerArea.removeFromTop(5);

    // Profile editor takes right side if visible
    if (profileEditorVisible)
    {
        auto profileEditorArea = centerArea.removeFromRight(320);
        centerArea.removeFromRight(10);  // Gap
        profileEditorPanel.setBounds(profileEditorArea);
    }

    // Step sequencer takes up space at bottom if visible
    if (stepSeqVisible)
    {
        // Calculate step sequencer height: header + 8 lanes * lane height
        int stepSeqHeight = 20 + (8 * 20) + 10;  // header + lanes + padding
        auto stepSeqArea = centerArea.removeFromBottom(stepSeqHeight);
        stepSequencer.setBounds(stepSeqArea);
        centerArea.removeFromBottom(10);  // Gap between XY pad and sequencer
    }

    xyPad.setBounds(centerArea);
}

void DrummerCloneAudioProcessorEditor::timerCallback()
{
    updateStatusBar();
    followModePanel.updateDisplay();

    // Update MIDI source indicator
    if (audioProcessor.isSectionControlledByMidi())
    {
        midiCCSourceIndicator.setText("MIDI", juce::dontSendNotification);
    }
    else
    {
        midiCCSourceIndicator.setText("", juce::dontSendNotification);
    }
}

void DrummerCloneAudioProcessorEditor::updateStatusBar()
{
    juce::String status = "Ready";

    if (audioProcessor.isFollowModeActive())
    {
        float lock = audioProcessor.getGrooveLockPercentage();
        status = juce::String::formatted("Follow Mode: %.0f%% locked", lock);
    }

    statusLabel.setText(status, juce::dontSendNotification);
}

void DrummerCloneAudioProcessorEditor::exportToMidiFile()
{
    // Determine number of bars to export
    int numBars = 8;  // Default
    switch (exportBarsComboBox.getSelectedId())
    {
        case 1: numBars = 4; break;
        case 2: numBars = 8; break;
        case 3: numBars = 16; break;
        case 4: numBars = 32; break;
    }

    // Create file chooser
    auto fileChooser = std::make_shared<juce::FileChooser>(
        "Export MIDI File",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("DrummerClone_Pattern.mid"),
        "*.mid");

    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, fileChooser, numBars](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();

            if (file == juce::File())
                return;

            // Make sure it has .mid extension
            if (!file.hasFileExtension(".mid"))
                file = file.withFileExtension(".mid");

            statusLabel.setText("Exporting MIDI...", juce::dontSendNotification);

            // Get current parameters
            auto& params = audioProcessor.getValueTreeState();
            float complexity = params.getRawParameterValue("complexity")->load();
            float loudness = params.getRawParameterValue("loudness")->load();
            float swing = params.getRawParameterValue("swing")->load();
            int styleIndex = static_cast<int>(params.getParameter("style")->getValue() * 7);

            // Generate the pattern using the DrummerEngine
            // We need to create a temporary engine for export since the processor's engine
            // is tied to real-time playback
            juce::MidiMessageSequence exportSequence;

            // Add tempo meta event (assume 120 BPM, user can change in DAW)
            auto tempoEvent = juce::MidiMessage::tempoMetaEvent(500000);  // 120 BPM
            tempoEvent.setTimeStamp(0);
            exportSequence.addEvent(tempoEvent);

            // Add time signature (4/4)
            auto timeSigEvent = juce::MidiMessage::timeSignatureMetaEvent(4, 2);
            timeSigEvent.setTimeStamp(0);
            exportSequence.addEvent(timeSigEvent);

            // Add track name
            auto trackName = juce::MidiMessage::textMetaEvent(3, "DrummerClone Drums");
            trackName.setTimeStamp(0);
            exportSequence.addEvent(trackName);

            // Create a temporary DrummerEngine for generation
            // Use the processor's engine directly by generating patterns
            constexpr int PPQ = 960;
            GrooveTemplate emptyGroove;

            // Use a local DrummerEngine for export
            DrummerEngine exportEngine(audioProcessor.getValueTreeState());
            exportEngine.prepare(44100.0, 512);
            // Convert normalized parameter (0-1) to drummer index (0-28)
            int drummerIndex = static_cast<int>(std::round(params.getParameter("drummer")->getValue() * 28.0f));
            exportEngine.setDrummer(drummerIndex);

            for (int bar = 0; bar < numBars; ++bar)
            {
                juce::MidiBuffer barBuffer = exportEngine.generateRegion(
                    1,          // 1 bar at a time
                    120.0,      // BPM (standard, user adjusts in DAW)
                    styleIndex,
                    emptyGroove,
                    complexity,
                    loudness,
                    swing
                );

                int tickOffset = bar * PPQ * 4;  // 4 beats per bar

                for (const auto metadata : barBuffer)
                {
                    auto msg = metadata.getMessage();
                    msg.setTimeStamp(msg.getTimeStamp() + tickOffset);
                    exportSequence.addEvent(msg);
                }
            }

            // Add end of track
            auto endTrack = juce::MidiMessage::endOfTrack();
            endTrack.setTimeStamp(static_cast<double>(numBars * PPQ * 4));
            exportSequence.addEvent(endTrack);

            exportSequence.sort();
            exportSequence.updateMatchedPairs();

            // Export using MidiExporter
            bool success = MidiExporter::exportSequenceToFile(exportSequence, file, 120.0, PPQ);

            if (success)
            {
                statusLabel.setText("Exported: " + file.getFileName(), juce::dontSendNotification);
            }
            else
            {
                statusLabel.setText("Export failed!", juce::dontSendNotification);
            }
        });
}
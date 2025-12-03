#include "FollowModePanel.h"

FollowModePanel::FollowModePanel(DrummerCloneAudioProcessor& processor)
    : audioProcessor(processor)
{
    // Enable toggle
    enableToggle.setButtonText("Follow Input");
    enableToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(enableToggle);

    enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "followEnabled", enableToggle);

    // Source selection
    sourceLabel.setText("Source:", juce::dontSendNotification);
    sourceLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(sourceLabel);

    sourceComboBox.addItem("MIDI", 1);
    sourceComboBox.addItem("Audio", 2);
    sourceComboBox.setSelectedId(1);
    addAndMakeVisible(sourceComboBox);

    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "followSource", sourceComboBox);

    // Sensitivity slider
    sensitivityLabel.setText("Sensitivity:", juce::dontSendNotification);
    sensitivityLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(sensitivityLabel);

    sensitivitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sensitivitySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sensitivitySlider.setRange(0.1, 0.8, 0.01);
    sensitivitySlider.setValue(0.5);
    addAndMakeVisible(sensitivitySlider);

    sensitivityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "followSensitivity", sensitivitySlider);

    // Instruction label - explains how to use Follow Mode
    instructionLabel.setText("Route audio via sidechain, press Learn, play 4 bars",
                            juce::dontSendNotification);
    instructionLabel.setColour(juce::Label::textColourId, juce::Colour(120, 120, 130));
    instructionLabel.setFont(juce::Font(10.0f));
    instructionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(instructionLabel);

    // Groove learning buttons
    learnButton.setButtonText("Learn");
    learnButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 120, 60));
    learnButton.addListener(this);
    addAndMakeVisible(learnButton);

    lockButton.setButtonText("Lock");
    lockButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 80, 120));
    lockButton.addListener(this);
    lockButton.setEnabled(false);
    addAndMakeVisible(lockButton);

    resetButton.setButtonText("Reset");
    resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(100, 60, 60));
    resetButton.addListener(this);
    addAndMakeVisible(resetButton);

    // Status label
    statusLabel.setText("Idle", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    statusLabel.setFont(juce::Font(11.0f));
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    // Lock label
    lockLabel.setText("Bars: 0 / 4", juce::dontSendNotification);
    lockLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(lockLabel);

    // Phase 3: Genre detection label
    genreLabel.setText("Genre:", juce::dontSendNotification);
    genreLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    genreLabel.setFont(juce::Font(10.0f));
    addAndMakeVisible(genreLabel);

    detectedGenreLabel.setText("--", juce::dontSendNotification);
    detectedGenreLabel.setColour(juce::Label::textColourId, juce::Colour(150, 200, 255));
    detectedGenreLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(detectedGenreLabel);

    // Phase 3: Tempo drift label
    tempoDriftLabel.setText("Timing: --", juce::dontSendNotification);
    tempoDriftLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    tempoDriftLabel.setFont(juce::Font(10.0f));
    addAndMakeVisible(tempoDriftLabel);

    // Phase 3: Confidence label
    confidenceLabel.setText("Confidence: 0%", juce::dontSendNotification);
    confidenceLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    confidenceLabel.setFont(juce::Font(10.0f));
    addAndMakeVisible(confidenceLabel);
}

FollowModePanel::~FollowModePanel()
{
    learnButton.removeListener(this);
    lockButton.removeListener(this);
    resetButton.removeListener(this);
}

void FollowModePanel::buttonClicked(juce::Button* button)
{
    if (button == &learnButton)
    {
        audioProcessor.startGrooveLearning();
    }
    else if (button == &lockButton)
    {
        audioProcessor.lockGroove();
    }
    else if (button == &resetButton)
    {
        audioProcessor.resetGrooveLearning();
    }

    updateButtonStates();
}

void FollowModePanel::updateButtonStates()
{
    auto state = audioProcessor.getGrooveLearnerState();

    switch (state)
    {
        case GrooveLearner::State::Idle:
            learnButton.setEnabled(true);
            lockButton.setEnabled(false);
            learnButton.setButtonText("Learn");
            statusLabel.setText("Idle - Press Learn to start", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
            break;

        case GrooveLearner::State::Learning:
            learnButton.setEnabled(false);
            lockButton.setEnabled(audioProcessor.isGrooveReady());
            learnButton.setButtonText("Learning...");
            statusLabel.setText("Learning groove...", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(200, 180, 100));
            break;

        case GrooveLearner::State::Locked:
            learnButton.setEnabled(false);
            lockButton.setEnabled(false);
            learnButton.setButtonText("Locked");
            statusLabel.setText("Groove locked!", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(100, 200, 100));
            break;
    }
}

void FollowModePanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Panel background
    g.setColour(juce::Colour(35, 35, 40));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Border
    g.setColour(juce::Colour(60, 60, 70));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("FOLLOW MODE", bounds.removeFromTop(25).reduced(10, 5), juce::Justification::left);

    // Activity LED
    auto ledBounds = juce::Rectangle<float>(bounds.getRight() - 30, 8, 12, 12);

    if (audioProcessor.isFollowModeActive())
    {
        // Pulsing green when active and receiving input
        float alpha = activityState ? 1.0f : 0.5f;
        g.setColour(juce::Colour(80, 200, 80).withAlpha(alpha));
    }
    else
    {
        // Dim grey when inactive
        g.setColour(juce::Colour(80, 80, 80));
    }

    g.fillEllipse(ledBounds);

    // LED highlight
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.fillEllipse(ledBounds.reduced(3).translated(-1, -1));

    // Groove lock progress bar
    auto lockBarBounds = juce::Rectangle<float>(10, bounds.getBottom() - 25, bounds.getWidth() - 20, 8);

    // Background
    g.setColour(juce::Colour(50, 50, 55));
    g.fillRoundedRectangle(lockBarBounds, 4.0f);

    // Progress
    float lockWidth = lockBarBounds.getWidth() * (currentLockPercentage / 100.0f);
    if (lockWidth > 0)
    {
        auto progressBounds = lockBarBounds.withWidth(lockWidth);

        // Color based on lock percentage
        juce::Colour lockColor;
        if (currentLockPercentage < 30.0f)
            lockColor = juce::Colour(200, 100, 100);  // Red
        else if (currentLockPercentage < 60.0f)
            lockColor = juce::Colour(200, 180, 100);  // Yellow
        else
            lockColor = juce::Colour(100, 200, 100);  // Green

        g.setColour(lockColor);
        g.fillRoundedRectangle(progressBounds, 4.0f);
    }
}

void FollowModePanel::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Title area
    bounds.removeFromTop(25);

    // Enable toggle
    enableToggle.setBounds(bounds.removeFromTop(25));

    bounds.removeFromTop(5);

    // Source row
    auto sourceRow = bounds.removeFromTop(25);
    sourceLabel.setBounds(sourceRow.removeFromLeft(50));
    sourceComboBox.setBounds(sourceRow.reduced(2));

    bounds.removeFromTop(3);

    // Instruction label
    instructionLabel.setBounds(bounds.removeFromTop(15));

    bounds.removeFromTop(3);

    // Sensitivity row
    auto sensRow = bounds.removeFromTop(25);
    sensitivityLabel.setBounds(sensRow.removeFromLeft(70));
    sensitivitySlider.setBounds(sensRow.reduced(2));

    bounds.removeFromTop(8);

    // Learning buttons row
    auto buttonRow = bounds.removeFromTop(28);
    int buttonWidth = (buttonRow.getWidth() - 10) / 3;
    learnButton.setBounds(buttonRow.removeFromLeft(buttonWidth).reduced(2));
    buttonRow.removeFromLeft(5);
    lockButton.setBounds(buttonRow.removeFromLeft(buttonWidth).reduced(2));
    buttonRow.removeFromLeft(5);
    resetButton.setBounds(buttonRow.reduced(2));

    bounds.removeFromTop(5);

    // Status label
    statusLabel.setBounds(bounds.removeFromTop(18));

    bounds.removeFromTop(5);

    // Lock label (above the progress bar)
    lockLabel.setBounds(bounds.removeFromTop(16));

    bounds.removeFromTop(2);

    // Phase 3: Genre and tempo drift info row
    auto infoRow = bounds.removeFromTop(14);
    genreLabel.setBounds(infoRow.removeFromLeft(40));
    detectedGenreLabel.setBounds(infoRow.removeFromLeft(70));
    infoRow.removeFromLeft(5);
    tempoDriftLabel.setBounds(infoRow);

    bounds.removeFromTop(2);

    // Confidence row
    confidenceLabel.setBounds(bounds.removeFromTop(14));
}

void FollowModePanel::updateDisplay()
{
    // Update learning progress
    currentLockPercentage = audioProcessor.getGrooveLockPercentage();
    int barsLearned = audioProcessor.getBarsLearned();

    lockLabel.setText(juce::String::formatted("Bars: %d / 4", barsLearned),
                     juce::dontSendNotification);

    // Update button states
    updateButtonStates();

    // Phase 3: Update genre detection display
    auto state = audioProcessor.getGrooveLearnerState();
    if (state == GrooveLearner::State::Learning || state == GrooveLearner::State::Locked)
    {
        juce::String genreStr = audioProcessor.getDetectedGenreString();
        if (genreStr != "Unknown")
        {
            detectedGenreLabel.setText(genreStr, juce::dontSendNotification);
            detectedGenreLabel.setColour(juce::Label::textColourId, juce::Colour(150, 200, 255));
        }
        else
        {
            detectedGenreLabel.setText("Analyzing...", juce::dontSendNotification);
            detectedGenreLabel.setColour(juce::Label::textColourId, juce::Colour(150, 150, 150));
        }

        // Phase 3: Update tempo drift display
        TempoDriftInfo drift = audioProcessor.getTempoDrift();
        juce::String timingStr;
        juce::Colour timingColor = juce::Colours::lightgrey;

        if (drift.isRushing)
        {
            timingStr = juce::String::formatted("Rushing +%.1f%%", std::abs(drift.driftPercentage));
            timingColor = juce::Colour(255, 150, 100);  // Orange
        }
        else if (drift.isDragging)
        {
            timingStr = juce::String::formatted("Dragging %.1f%%", drift.driftPercentage);
            timingColor = juce::Colour(100, 150, 255);  // Blue
        }
        else
        {
            timingStr = "Steady";
            timingColor = juce::Colour(100, 200, 100);  // Green
        }

        tempoDriftLabel.setText(timingStr, juce::dontSendNotification);
        tempoDriftLabel.setColour(juce::Label::textColourId, timingColor);

        // Update confidence
        float confidence = audioProcessor.getGrooveConfidence();
        confidenceLabel.setText(juce::String::formatted("Confidence: %.0f%%", confidence * 100.0f),
                               juce::dontSendNotification);

        // Color code confidence
        if (confidence < 0.3f)
            confidenceLabel.setColour(juce::Label::textColourId, juce::Colour(200, 100, 100));
        else if (confidence < 0.6f)
            confidenceLabel.setColour(juce::Label::textColourId, juce::Colour(200, 180, 100));
        else
            confidenceLabel.setColour(juce::Label::textColourId, juce::Colour(100, 200, 100));
    }
    else
    {
        // Idle state - reset displays
        detectedGenreLabel.setText("--", juce::dontSendNotification);
        detectedGenreLabel.setColour(juce::Label::textColourId, juce::Colour(150, 150, 150));
        tempoDriftLabel.setText("--", juce::dontSendNotification);
        tempoDriftLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        confidenceLabel.setText("Confidence: 0%", juce::dontSendNotification);
        confidenceLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    }

    // Update activity LED
    if (audioProcessor.isFollowModeActive() && state == GrooveLearner::State::Learning)
    {
        activityCounter++;
        activityState = (activityCounter % 5) < 3;  // Blink pattern
    }
    else if (state == GrooveLearner::State::Locked)
    {
        activityState = true;  // Solid when locked
    }
    else
    {
        activityState = false;
    }

    repaint();
}
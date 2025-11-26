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
    instructionLabel.setText("Use sidechain input to route your bass/audio track here",
                            juce::dontSendNotification);
    instructionLabel.setColour(juce::Label::textColourId, juce::Colour(120, 120, 130));
    instructionLabel.setFont(juce::Font(10.0f));
    instructionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(instructionLabel);

    // Lock label
    lockLabel.setText("Groove Lock: 0%", juce::dontSendNotification);
    lockLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(lockLabel);
}

FollowModePanel::~FollowModePanel()
{
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

    bounds.removeFromTop(5);

    // Lock label (above the progress bar)
    lockLabel.setBounds(bounds.removeFromTop(20));
}

void FollowModePanel::updateDisplay()
{
    // Update lock percentage
    currentLockPercentage = audioProcessor.getGrooveLockPercentage();
    lockLabel.setText(juce::String::formatted("Groove Lock: %.0f%%", currentLockPercentage),
                     juce::dontSendNotification);

    // Update activity LED
    if (audioProcessor.isFollowModeActive() && currentLockPercentage > 10.0f)
    {
        activityCounter++;
        activityState = (activityCounter % 5) < 3;  // Blink pattern
    }
    else
    {
        activityState = false;
    }

    repaint();
}
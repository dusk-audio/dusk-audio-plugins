/*
  ==============================================================================

    GrooveMind - ML-Powered Intelligent Drummer
    PluginEditor.cpp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
GrooveMindEditor::GrooveMindEditor(GrooveMindProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      xyPad(p.getAPVTS(), "complexity", "loudness")
{
    setSize(700, 500);

    // Style selector
    styleLabel.setText("Style", juce::dontSendNotification);
    styleLabel.setJustificationType(juce::Justification::right);
    addAndMakeVisible(styleLabel);

    styleSelector.addItemList({"Rock", "Pop", "Funk", "Soul", "Jazz", "Blues",
                               "HipHop", "R&B", "Electronic", "Latin", "Country", "Punk"}, 1);
    addAndMakeVisible(styleSelector);
    styleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "style", styleSelector);

    // Drummer selector
    drummerLabel.setText("Drummer", juce::dontSendNotification);
    drummerLabel.setJustificationType(juce::Justification::right);
    addAndMakeVisible(drummerLabel);

    drummerSelector.addItemList({"Alex - Versatile", "Jordan - Groovy", "Sam - Steady",
                                 "Riley - Energetic", "Casey - Technical", "Morgan - Jazz"}, 1);
    addAndMakeVisible(drummerSelector);
    drummerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "drummer", drummerSelector);

    // Kit selector
    kitLabel.setText("Kit", juce::dontSendNotification);
    kitLabel.setJustificationType(juce::Justification::right);
    addAndMakeVisible(kitLabel);

    kitSelector.addItemList({"Acoustic", "Brush", "Electronic", "Hybrid"}, 1);
    addAndMakeVisible(kitSelector);
    kitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "kit", kitSelector);

    // Section selector
    sectionLabel.setText("Section", juce::dontSendNotification);
    sectionLabel.setJustificationType(juce::Justification::right);
    addAndMakeVisible(sectionLabel);

    sectionSelector.addItemList({"Intro", "Verse", "Pre-Chorus", "Chorus",
                                 "Bridge", "Breakdown", "Outro"}, 1);
    addAndMakeVisible(sectionSelector);
    sectionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "section", sectionSelector);

    // XY Pad
    addAndMakeVisible(xyPad);

    // Energy slider
    energyLabel.setText("Energy", juce::dontSendNotification);
    energyLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(energyLabel);

    energySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    energySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(energySlider);
    energyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "energy", energySlider);

    // Groove slider
    grooveLabel.setText("Groove", juce::dontSendNotification);
    grooveLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(grooveLabel);

    grooveSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    grooveSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(grooveSlider);
    grooveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "groove", grooveSlider);

    // Swing slider
    swingLabel.setText("Swing", juce::dontSendNotification);
    swingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(swingLabel);

    swingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(swingSlider);
    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "swing", swingSlider);

    // Fill controls
    fillModeSelector.addItemList({"Auto", "Manual", "Off"}, 1);
    addAndMakeVisible(fillModeSelector);
    fillModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), "fill_mode", fillModeSelector);

    fillIntensitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fillIntensitySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(fillIntensitySlider);
    fillIntensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "fill_intensity", fillIntensitySlider);

    fillTriggerButton.setButtonText("Fill!");
    fillTriggerButton.onClick = [this]() {
        processor.getDrummerEngine().triggerFill(4);
    };
    addAndMakeVisible(fillTriggerButton);

    // Instrument toggles
    kickToggle.setButtonText("Kick");
    snareToggle.setButtonText("Snare");
    hihatToggle.setButtonText("Hats");
    tomsToggle.setButtonText("Toms");
    cymbalsToggle.setButtonText("Cymbals");

    addAndMakeVisible(kickToggle);
    addAndMakeVisible(snareToggle);
    addAndMakeVisible(hihatToggle);
    addAndMakeVisible(tomsToggle);
    addAndMakeVisible(cymbalsToggle);

    kickAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), "kick_enabled", kickToggle);
    snareAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), "snare_enabled", snareToggle);
    hihatAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), "hihat_enabled", hihatToggle);
    tomsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), "toms_enabled", tomsToggle);
    cymbalsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), "cymbals_enabled", cymbalsToggle);

    // Follow mode toggle
    followToggle.setButtonText("Follow");
    addAndMakeVisible(followToggle);
    followAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), "follow_enabled", followToggle);

    // Transport display
    transportLabel.setText("Stopped", juce::dontSendNotification);
    transportLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(transportLabel);

    bpmLabel.setText("120 BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(bpmLabel);

    // Pattern library status
    int patternCount = processor.getPatternLibrary().getPatternCount();
    patternCountLabel.setText(juce::String(patternCount) + " patterns loaded", juce::dontSendNotification);
    patternCountLabel.setJustificationType(juce::Justification::right);
    patternCountLabel.setColour(juce::Label::textColourId,
                                 patternCount > 0 ? juce::Colours::limegreen : juce::Colours::red);
    addAndMakeVisible(patternCountLabel);

    currentPatternLabel.setText("", juce::dontSendNotification);
    currentPatternLabel.setJustificationType(juce::Justification::right);
    currentPatternLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888899));
    addAndMakeVisible(currentPatternLabel);

    // Start timer for UI updates
    startTimerHz(30);
}

GrooveMindEditor::~GrooveMindEditor()
{
    stopTimer();
}

//==============================================================================
void GrooveMindEditor::paint(juce::Graphics& g)
{
    // Dark background
    g.fillAll(juce::Colour(0xff1e1e24));

    // Header
    g.setColour(juce::Colour(0xff2a2a32));
    g.fillRect(0, 0, getWidth(), 50);

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("GrooveMind", 15, 10, 200, 30, juce::Justification::left);

    // Subtitle
    g.setColour(juce::Colour(0xff888899));
    g.setFont(juce::Font(11.0f));
    g.drawText("ML-Powered Intelligent Drummer", 15, 32, 200, 15, juce::Justification::left);

    // Section dividers
    g.setColour(juce::Colour(0xff3a3a44));

    // XY Pad label
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("Complexity / Loudness", 20, 160, 250, 20, juce::Justification::centred);

    // Fill section label
    g.drawText("Fills", 295, 320, 100, 20, juce::Justification::left);

    // Kit section label
    g.drawText("Kit Pieces", 20, 400, 100, 20, juce::Justification::left);
}

void GrooveMindEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header area (skip it)
    bounds.removeFromTop(60);

    // Top controls row
    auto topRow = bounds.removeFromTop(35);
    topRow.removeFromLeft(10);

    styleLabel.setBounds(topRow.removeFromLeft(50));
    styleSelector.setBounds(topRow.removeFromLeft(120));
    topRow.removeFromLeft(15);

    drummerLabel.setBounds(topRow.removeFromLeft(60));
    drummerSelector.setBounds(topRow.removeFromLeft(150));

    // Second row
    bounds.removeFromTop(5);
    auto secondRow = bounds.removeFromTop(35);
    secondRow.removeFromLeft(10);

    kitLabel.setBounds(secondRow.removeFromLeft(50));
    kitSelector.setBounds(secondRow.removeFromLeft(120));
    secondRow.removeFromLeft(15);

    sectionLabel.setBounds(secondRow.removeFromLeft(60));
    sectionSelector.setBounds(secondRow.removeFromLeft(150));

    // Transport info in top right
    bpmLabel.setBounds(getWidth() - 100, 60, 90, 25);
    transportLabel.setBounds(getWidth() - 100, 85, 90, 25);

    // Pattern library status in header
    patternCountLabel.setBounds(getWidth() - 200, 15, 185, 18);
    currentPatternLabel.setBounds(getWidth() - 200, 32, 185, 14);

    // Main content area
    bounds.removeFromTop(15);

    // Left side: XY Pad
    auto leftSide = bounds.removeFromLeft(280);
    leftSide.removeFromLeft(20);
    leftSide.removeFromTop(25);
    xyPad.setBounds(leftSide.removeFromTop(200));

    // Right side: Sliders and controls
    auto rightSide = bounds;
    rightSide.removeFromLeft(20);
    rightSide.removeFromRight(20);

    // Energy slider
    auto energyRow = rightSide.removeFromTop(40);
    energyLabel.setBounds(energyRow.removeFromLeft(60));
    energySlider.setBounds(energyRow.reduced(5, 10));

    // Groove slider
    auto grooveRow = rightSide.removeFromTop(40);
    grooveLabel.setBounds(grooveRow.removeFromLeft(60));
    grooveSlider.setBounds(grooveRow.reduced(5, 10));

    // Swing slider
    auto swingRow = rightSide.removeFromTop(40);
    swingLabel.setBounds(swingRow.removeFromLeft(60));
    swingSlider.setBounds(swingRow.reduced(5, 10));

    rightSide.removeFromTop(20);

    // Fill controls
    auto fillRow = rightSide.removeFromTop(35);
    fillModeSelector.setBounds(fillRow.removeFromLeft(80));
    fillRow.removeFromLeft(10);
    fillIntensitySlider.setBounds(fillRow.removeFromLeft(150));
    fillRow.removeFromLeft(10);
    fillTriggerButton.setBounds(fillRow.removeFromLeft(60));

    rightSide.removeFromTop(10);

    // Follow toggle
    followToggle.setBounds(rightSide.removeFromTop(30).removeFromLeft(100));

    // Bottom: Instrument toggles
    auto bottomArea = getLocalBounds().removeFromBottom(60);
    bottomArea.removeFromLeft(20);
    bottomArea.removeFromTop(20);

    int toggleWidth = 80;
    kickToggle.setBounds(bottomArea.removeFromLeft(toggleWidth));
    snareToggle.setBounds(bottomArea.removeFromLeft(toggleWidth));
    hihatToggle.setBounds(bottomArea.removeFromLeft(toggleWidth));
    tomsToggle.setBounds(bottomArea.removeFromLeft(toggleWidth));
    cymbalsToggle.setBounds(bottomArea.removeFromLeft(toggleWidth));
}

void GrooveMindEditor::timerCallback()
{
    // Update transport display
    if (processor.isPlaying())
    {
        transportLabel.setText("Playing", juce::dontSendNotification);
        transportLabel.setColour(juce::Label::textColourId, juce::Colours::limegreen);
    }
    else
    {
        transportLabel.setText("Stopped", juce::dontSendNotification);
        transportLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    }

    // Update BPM display
    bpmLabel.setText(juce::String(processor.getCurrentBPM(), 1) + " BPM", juce::dontSendNotification);

    // Update current pattern display
    auto* pattern = processor.getDrummerEngine().getCurrentPattern();
    if (pattern != nullptr)
    {
        currentPatternLabel.setText(pattern->metadata.name, juce::dontSendNotification);
    }
    else
    {
        currentPatternLabel.setText("No pattern", juce::dontSendNotification);
    }
}

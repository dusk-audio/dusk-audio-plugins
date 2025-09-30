/*
  ==============================================================================

    Studio Verb - Plugin Editor
    Copyright (c) 2024 Luna CO. Audio

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Custom Look and Feel Implementation
//==============================================================================
StudioVerbLookAndFeel::StudioVerbLookAndFeel()
{
    backgroundColour = juce::Colour(0xff1a1a1f);
    knobColour = juce::Colour(0xff2a2a3f);
    trackColour = juce::Colour(0xff4a7c9f);
    textColour = juce::Colour(0xffe0e0e0);

    setColour(juce::Slider::backgroundColourId, knobColour);
    setColour(juce::Slider::thumbColourId, trackColour);
    setColour(juce::Slider::trackColourId, trackColour);
    setColour(juce::Slider::rotarySliderFillColourId, trackColour);
    setColour(juce::Slider::rotarySliderOutlineColourId, knobColour);
    setColour(juce::Slider::textBoxTextColourId, textColour);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour(juce::ComboBox::backgroundColourId, knobColour);
    setColour(juce::ComboBox::textColourId, textColour);
    setColour(juce::ComboBox::outlineColourId, trackColour.withAlpha(0.5f));
    setColour(juce::ComboBox::arrowColourId, textColour);

    setColour(juce::Label::textColourId, textColour);
}

void StudioVerbLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                              juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto lineW = juce::jmin(8.0f, radius * 0.5f);
    auto arcRadius = radius - lineW * 0.5f;

    // Background circle
    g.setColour(knobColour);
    g.fillEllipse(bounds.getCentreX() - radius, bounds.getCentreY() - radius, radius * 2.0f, radius * 2.0f);

    // Value arc
    juce::Path backgroundArc;
    backgroundArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(),
                                arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);

    g.setColour(knobColour.brighter(0.2f));
    g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(),
                               arcRadius, arcRadius, 0.0f,
                               rotaryStartAngle, toAngle, true);

        g.setColour(trackColour);
        g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Pointer
    juce::Point<float> thumbPoint(bounds.getCentreX() + (arcRadius - 10) * std::cos(toAngle - juce::MathConstants<float>::halfPi),
                                  bounds.getCentreY() + (arcRadius - 10) * std::sin(toAngle - juce::MathConstants<float>::halfPi));

    g.setColour(textColour);
    g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(thumbPoint));
}

void StudioVerbLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                         int buttonX, int buttonY, int buttonW, int buttonH,
                                         juce::ComboBox& box)
{
    auto cornerSize = box.findParentComponentOfClass<juce::ChoicePropertyComponent>() != nullptr ? 0.0f : 3.0f;
    juce::Rectangle<int> boxBounds(0, 0, width, height);

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);

    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f, 0.5f), cornerSize, 1.0f);

    // Draw arrow
    juce::Path path;
    path.startNewSubPath(buttonX + buttonW * 0.3f, buttonY + buttonH * 0.4f);
    path.lineTo(buttonX + buttonW * 0.5f, buttonY + buttonH * 0.6f);
    path.lineTo(buttonX + buttonW * 0.7f, buttonY + buttonH * 0.4f);

    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(isButtonDown ? 0.6f : 0.9f));
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

//==============================================================================
// Main Editor Implementation
//==============================================================================
StudioVerbAudioProcessorEditor::StudioVerbAudioProcessorEditor(StudioVerbAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);
    setSize(700, 400);
    setResizable(true, true);
    setResizeLimits(600, 350, 1000, 600);

    // Algorithm selector
    algorithmLabel.setText("Algorithm", juce::dontSendNotification);
    algorithmLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(algorithmLabel);

    algorithmSelector.addItemList({"Room", "Hall", "Plate", "Early Reflections"}, 1);
    algorithmSelector.addListener(this);
    addAndMakeVisible(algorithmSelector);

    algorithmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "algorithm", algorithmSelector);

    // Preset selector
    presetLabel.setText("Preset", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(presetLabel);

    presetSelector.addListener(this);
    addAndMakeVisible(presetSelector);

    // Size slider
    setupSlider(sizeSlider, sizeLabel, "Size");
    sizeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    sizeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "size", sizeSlider);

    // Damp slider
    setupSlider(dampSlider, dampLabel, "Damping");
    dampSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    dampSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    dampAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "damp", dampSlider);

    // Predelay slider
    setupSlider(predelaySlider, predelayLabel, "Predelay");
    predelaySlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    predelaySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    predelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "predelay", predelaySlider);

    // Mix slider
    setupSlider(mixSlider, mixLabel, "Mix");
    mixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "mix", mixSlider);

    // Value labels
    addAndMakeVisible(sizeValueLabel);
    addAndMakeVisible(dampValueLabel);
    addAndMakeVisible(predelayValueLabel);
    addAndMakeVisible(mixValueLabel);

    sizeValueLabel.setJustificationType(juce::Justification::centred);
    dampValueLabel.setJustificationType(juce::Justification::centred);
    predelayValueLabel.setJustificationType(juce::Justification::centred);
    mixValueLabel.setJustificationType(juce::Justification::centred);

    // Initialize preset list
    updatePresetList();

    // Start timer for updating value labels
    startTimer(50);
}

StudioVerbAudioProcessorEditor::~StudioVerbAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

//==============================================================================
void StudioVerbAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText)
{
    addAndMakeVisible(slider);
    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

//==============================================================================
void StudioVerbAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId));

    drawHeader(g);

    // Draw section backgrounds
    auto bounds = getLocalBounds();
    auto controlArea = bounds.removeFromBottom(bounds.getHeight() - 100);

    // Parameters section
    auto paramSection = controlArea.removeFromLeft(controlArea.getWidth() * 0.7f);
    drawSectionBackground(g, paramSection, "Parameters");
}

//==============================================================================
void StudioVerbAudioProcessorEditor::drawHeader(juce::Graphics& g)
{
    auto bounds = getLocalBounds().removeFromTop(60);

    // Background gradient
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a2a3f), 0, 0,
                                           juce::Colour(0xff1a1a1f), 0, static_cast<float>(bounds.getHeight()),
                                           false));
    g.fillRect(bounds);

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(28.0f, juce::Font::bold));
    g.drawText("STUDIO VERB", bounds.removeFromLeft(300), juce::Justification::centred);

    // Subtitle
    g.setFont(juce::Font(12.0f));
    g.setColour(juce::Colours::grey);
    g.drawText("Luna CO. Audio", bounds, juce::Justification::centred);
}

//==============================================================================
void StudioVerbAudioProcessorEditor::drawSectionBackground(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    g.setColour(juce::Colour(0xff25252a).withAlpha(0.5f));
    g.fillRoundedRectangle(bounds.reduced(5).toFloat(), 5.0f);

    g.setColour(juce::Colours::grey);
    g.setFont(11.0f);
    g.drawText(title, bounds.removeFromTop(20), juce::Justification::centredLeft);
}

//==============================================================================
void StudioVerbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(60); // Header space

    // Top controls row
    auto topRow = bounds.removeFromTop(60);
    auto selectorWidth = 150;

    algorithmLabel.setBounds(topRow.removeFromLeft(60).reduced(5));
    algorithmSelector.setBounds(topRow.removeFromLeft(selectorWidth).reduced(5));

    presetLabel.setBounds(topRow.removeFromLeft(60).reduced(5));
    presetSelector.setBounds(topRow.removeFromLeft(selectorWidth).reduced(5));

    // Knobs row
    bounds.removeFromTop(20); // Spacing
    auto knobRow = bounds.removeFromTop(180);
    auto knobSize = 120;
    auto knobSpacing = (knobRow.getWidth() - (knobSize * 4)) / 5;

    auto knobBounds = knobRow.removeFromLeft(knobSpacing);

    // Size knob
    knobBounds = knobRow.removeFromLeft(knobSize);
    sizeSlider.setBounds(knobBounds.removeFromTop(knobSize));
    sizeLabel.setBounds(knobBounds.removeFromTop(20));
    sizeValueLabel.setBounds(knobBounds);

    knobRow.removeFromLeft(knobSpacing);

    // Damp knob
    knobBounds = knobRow.removeFromLeft(knobSize);
    dampSlider.setBounds(knobBounds.removeFromTop(knobSize));
    dampLabel.setBounds(knobBounds.removeFromTop(20));
    dampValueLabel.setBounds(knobBounds);

    knobRow.removeFromLeft(knobSpacing);

    // Predelay knob
    knobBounds = knobRow.removeFromLeft(knobSize);
    predelaySlider.setBounds(knobBounds.removeFromTop(knobSize));
    predelayLabel.setBounds(knobBounds.removeFromTop(20));
    predelayValueLabel.setBounds(knobBounds);

    knobRow.removeFromLeft(knobSpacing);

    // Mix knob
    knobBounds = knobRow.removeFromLeft(knobSize);
    mixSlider.setBounds(knobBounds.removeFromTop(knobSize));
    mixLabel.setBounds(knobBounds.removeFromTop(20));
    mixValueLabel.setBounds(knobBounds);
}

//==============================================================================
void StudioVerbAudioProcessorEditor::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &algorithmSelector)
    {
        updatePresetList();
        // Auto-load first preset when algorithm changes
        presetSelector.setSelectedId(1);
    }
    else if (comboBoxThatHasChanged == &presetSelector)
    {
        int selectedIndex = presetSelector.getSelectedId() - 1;
        if (selectedIndex >= 0)
        {
            // Find the actual preset index in the full list
            auto currentAlgo = static_cast<StudioVerbAudioProcessor::Algorithm>(algorithmSelector.getSelectedId() - 1);
            auto presetNames = audioProcessor.getPresetNamesForAlgorithm(currentAlgo);
            auto selectedName = presetNames[selectedIndex];

            // Find this preset in the factory presets
            const auto& presets = audioProcessor.getFactoryPresets();
            for (size_t i = 0; i < presets.size(); ++i)
            {
                if (presets[i].name == selectedName && presets[i].algorithm == currentAlgo)
                {
                    audioProcessor.loadPreset(static_cast<int>(i));
                    break;
                }
            }
        }
    }
}

//==============================================================================
void StudioVerbAudioProcessorEditor::updatePresetList()
{
    presetSelector.clear();
    auto currentAlgo = static_cast<StudioVerbAudioProcessor::Algorithm>(algorithmSelector.getSelectedId() - 1);
    auto presetNames = audioProcessor.getPresetNamesForAlgorithm(currentAlgo);

    presetSelector.addItemList(presetNames, 1);
    presetSelector.setSelectedId(0);
}

//==============================================================================
void StudioVerbAudioProcessorEditor::timerCallback()
{
    updateValueLabels();

    // Update preset list if algorithm changed
    int currentAlgorithm = algorithmSelector.getSelectedId() - 1;
    if (currentAlgorithm != lastAlgorithm)
    {
        updatePresetList();
        lastAlgorithm = currentAlgorithm;
    }
}

//==============================================================================
void StudioVerbAudioProcessorEditor::updateValueLabels()
{
    sizeValueLabel.setText(juce::String(sizeSlider.getValue(), 2), juce::dontSendNotification);
    dampValueLabel.setText(juce::String(dampSlider.getValue(), 2), juce::dontSendNotification);
    predelayValueLabel.setText(juce::String(predelaySlider.getValue(), 1) + " ms", juce::dontSendNotification);
    mixValueLabel.setText(juce::String(static_cast<int>(mixSlider.getValue() * 100)) + "%", juce::dontSendNotification);
}
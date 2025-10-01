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
    // Luna unified color scheme
    backgroundColour = juce::Colour(0xff1a1a1a);
    knobColour = juce::Colour(0xff2a2a2a);
    trackColour = juce::Colour(0xff4a9eff);  // Blue accent
    textColour = juce::Colour(0xffe0e0e0);

    setColour(juce::Slider::backgroundColourId, knobColour);
    setColour(juce::Slider::thumbColourId, trackColour);
    setColour(juce::Slider::trackColourId, trackColour);
    setColour(juce::Slider::rotarySliderFillColourId, trackColour);
    setColour(juce::Slider::rotarySliderOutlineColourId, knobColour);
    setColour(juce::Slider::textBoxTextColourId, textColour);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    setColour(juce::ComboBox::textColourId, textColour);
    setColour(juce::ComboBox::outlineColourId, trackColour.withAlpha(0.5f));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff808080));

    setColour(juce::Label::textColourId, textColour);
}

void StudioVerbLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                              juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto lineW = juce::jmin(6.0f, radius * 0.4f);
    auto arcRadius = radius - lineW * 0.5f;

    // Background circle with subtle gradient
    juce::ColourGradient grad(
        knobColour.brighter(0.1f), bounds.getCentreX(), bounds.getY(),
        knobColour.darker(0.2f), bounds.getCentreX(), bounds.getBottom(),
        false
    );
    g.setGradientFill(grad);
    g.fillEllipse(bounds.getCentreX() - radius, bounds.getCentreY() - radius, radius * 2.0f, radius * 2.0f);

    // Outer ring
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawEllipse(bounds.getCentreX() - radius, bounds.getCentreY() - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // Track arc
    juce::Path backgroundArc;
    backgroundArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(),
                                arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);

    g.setColour(juce::Colour(0xff404040));
    g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc
    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(),
                               arcRadius, arcRadius, 0.0f,
                               rotaryStartAngle, toAngle, true);

        g.setColour(trackColour);
        g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Pointer line
    juce::Path pointer;
    pointer.addLineSegment(juce::Line<float>(
        bounds.getCentreX() + (arcRadius - 12) * std::cos(toAngle - juce::MathConstants<float>::halfPi),
        bounds.getCentreY() + (arcRadius - 12) * std::sin(toAngle - juce::MathConstants<float>::halfPi),
        bounds.getCentreX() + (arcRadius * 0.3f) * std::cos(toAngle - juce::MathConstants<float>::halfPi),
        bounds.getCentreY() + (arcRadius * 0.3f) * std::sin(toAngle - juce::MathConstants<float>::halfPi)
    ), 2.5f);

    g.setColour(textColour);
    g.fillPath(pointer);

    // Center dot
    g.fillEllipse(bounds.getCentreX() - 3, bounds.getCentreY() - 3, 6, 6);
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
    setSize(750, 450);
    setResizable(true, true);
    setResizeLimits(650, 400, 1000, 650);

    // Algorithm selector
    algorithmLabel.setText("ALGORITHM", juce::dontSendNotification);
    algorithmLabel.setJustificationType(juce::Justification::centredLeft);
    algorithmLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    algorithmLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
    addAndMakeVisible(algorithmLabel);

    algorithmSelector.addItemList({"Room", "Hall", "Plate", "Early Reflections"}, 1);
    algorithmSelector.addListener(this);
    addAndMakeVisible(algorithmSelector);

    algorithmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "algorithm", algorithmSelector);

    // Preset selector
    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centredLeft);
    presetLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    presetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
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

    // Value labels with Luna styling
    addAndMakeVisible(sizeValueLabel);
    addAndMakeVisible(dampValueLabel);
    addAndMakeVisible(predelayValueLabel);
    addAndMakeVisible(mixValueLabel);

    sizeValueLabel.setJustificationType(juce::Justification::centred);
    dampValueLabel.setJustificationType(juce::Justification::centred);
    predelayValueLabel.setJustificationType(juce::Justification::centred);
    mixValueLabel.setJustificationType(juce::Justification::centred);

    sizeValueLabel.setFont(juce::Font(12.0f));
    dampValueLabel.setFont(juce::Font(12.0f));
    predelayValueLabel.setFont(juce::Font(12.0f));
    mixValueLabel.setFont(juce::Font(12.0f));

    sizeValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    dampValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    predelayValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    mixValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));

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
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(11.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
    addAndMakeVisible(label);
}

//==============================================================================
void StudioVerbAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Luna unified background
    g.fillAll(juce::Colour(0xff1a1a1a));

    auto bounds = getLocalBounds();

    // Draw header with Luna styling
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(0, 0, bounds.getWidth(), 55);

    // Plugin name
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xffe0e0e0));
    g.drawText("STUDIO VERB", 60, 10, 300, 30, juce::Justification::left);

    // Subtitle
    g.setFont(juce::Font(11.0f));
    g.setColour(juce::Colour(0xff909090));
    g.drawText("Digital Reverb Processor", 60, 32, 300, 20, juce::Justification::left);

    // Section dividers
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawLine(0, 55, getWidth(), 55, 2.0f);
}


//==============================================================================
void StudioVerbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(65); // Header space

    // Top controls row - Algorithm and Preset selectors
    auto topRow = bounds.removeFromTop(70);
    topRow.reduce(20, 10);

    auto leftSection = topRow.removeFromLeft(topRow.getWidth() / 2);

    // Algorithm selector
    algorithmLabel.setBounds(leftSection.removeFromTop(20));
    algorithmSelector.setBounds(leftSection.removeFromTop(35).reduced(0, 5));

    // Preset selector
    presetLabel.setBounds(topRow.removeFromTop(20));
    presetSelector.setBounds(topRow.removeFromTop(35).reduced(0, 5));

    // Main controls section
    bounds.removeFromTop(15); // Spacing
    auto controlsArea = bounds.reduced(30, 10);

    auto knobSize = 95;
    auto labelHeight = 18;
    auto valueHeight = 20;
    auto totalKnobHeight = knobSize + labelHeight + valueHeight;
    auto knobSpacing = (controlsArea.getWidth() - (knobSize * 4)) / 5;

    int xPos = knobSpacing;

    // Size knob
    auto sizeArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    sizeLabel.setBounds(sizeArea.removeFromTop(labelHeight));
    sizeSlider.setBounds(sizeArea.removeFromTop(knobSize));
    sizeValueLabel.setBounds(sizeArea.removeFromTop(valueHeight));
    xPos += knobSize + knobSpacing;

    // Damp knob
    auto dampArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    dampLabel.setBounds(dampArea.removeFromTop(labelHeight));
    dampSlider.setBounds(dampArea.removeFromTop(knobSize));
    dampValueLabel.setBounds(dampArea.removeFromTop(valueHeight));
    xPos += knobSize + knobSpacing;

    // Predelay knob
    auto predelayArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    predelayLabel.setBounds(predelayArea.removeFromTop(labelHeight));
    predelaySlider.setBounds(predelayArea.removeFromTop(knobSize));
    predelayValueLabel.setBounds(predelayArea.removeFromTop(valueHeight));
    xPos += knobSize + knobSpacing;

    // Mix knob
    auto mixArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    mixLabel.setBounds(mixArea.removeFromTop(labelHeight));
    mixSlider.setBounds(mixArea.removeFromTop(knobSize));
    mixValueLabel.setBounds(mixArea.removeFromTop(valueHeight));
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
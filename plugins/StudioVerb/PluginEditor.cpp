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

    algorithmSelector.addItemList({"Room", "Hall", "Plate", "Early Reflections", "Gated", "Reverse"}, 1);
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

    // Width slider
    setupSlider(widthSlider, widthLabel, "Width");
    widthSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    widthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "width", widthSlider);

    // Advanced section label
    advancedSectionLabel.setText("ADVANCED", juce::dontSendNotification);
    advancedSectionLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    advancedSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4a9eff));
    addAndMakeVisible(advancedSectionLabel);

    // RT60 sliders (horizontal style for compact layout)
    setupSlider(lowRT60Slider, lowRT60Label, "Low RT60");
    lowRT60Slider.setSliderStyle(juce::Slider::LinearHorizontal);
    lowRT60Slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    lowRT60Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "lowRT60", lowRT60Slider);

    setupSlider(midRT60Slider, midRT60Label, "Mid RT60");
    midRT60Slider.setSliderStyle(juce::Slider::LinearHorizontal);
    midRT60Slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    midRT60Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "midRT60", midRT60Slider);

    setupSlider(highRT60Slider, highRT60Label, "High RT60");
    highRT60Slider.setSliderStyle(juce::Slider::LinearHorizontal);
    highRT60Slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    highRT60Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "highRT60", highRT60Slider);

    // Infinite mode button
    infiniteLabel.setText("Infinite", juce::dontSendNotification);
    infiniteLabel.setFont(juce::Font(11.0f));
    infiniteLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
    addAndMakeVisible(infiniteLabel);

    infiniteButton.setButtonText("");
    addAndMakeVisible(infiniteButton);
    infiniteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "infinite", infiniteButton);

    // Oversampling selector
    oversamplingLabel.setText("Oversampling", juce::dontSendNotification);
    oversamplingLabel.setFont(juce::Font(11.0f));
    oversamplingLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
    addAndMakeVisible(oversamplingLabel);

    oversamplingSelector.addItemList({"Off", "2x", "4x"}, 1);
    addAndMakeVisible(oversamplingSelector);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "oversampling", oversamplingSelector);

    // Room shape selector
    roomShapeLabel.setText("Room Shape", juce::dontSendNotification);
    roomShapeLabel.setFont(juce::Font(11.0f));
    roomShapeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
    addAndMakeVisible(roomShapeLabel);

    roomShapeSelector.addItemList({"Studio Room", "Small Room", "Large Hall", "Cathedral", "Chamber", "Warehouse", "Booth", "Tunnel"}, 1);
    addAndMakeVisible(roomShapeSelector);
    roomShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "roomShape", roomShapeSelector);

    // Vintage slider
    vintageLabel.setText("Vintage", juce::dontSendNotification);
    vintageLabel.setFont(juce::Font(11.0f));
    vintageLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
    addAndMakeVisible(vintageLabel);

    vintageSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    vintageSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(vintageSlider);
    vintageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "vintage", vintageSlider);

    // Predelay beats selector
    predelayBeatsLabel.setText("Sync", juce::dontSendNotification);
    predelayBeatsLabel.setFont(juce::Font(11.0f));
    predelayBeatsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
    addAndMakeVisible(predelayBeatsLabel);

    predelayBeatsSelector.addItemList({"Off", "1/16", "1/8", "1/4", "1/2"}, 1);
    addAndMakeVisible(predelayBeatsSelector);
    predelayBeatsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "predelayBeats", predelayBeatsSelector);

    // Value labels with Luna styling
    addAndMakeVisible(sizeValueLabel);
    addAndMakeVisible(dampValueLabel);
    addAndMakeVisible(predelayValueLabel);
    addAndMakeVisible(mixValueLabel);
    addAndMakeVisible(widthValueLabel);
    addAndMakeVisible(lowRT60ValueLabel);
    addAndMakeVisible(midRT60ValueLabel);
    addAndMakeVisible(highRT60ValueLabel);
    addAndMakeVisible(vintageValueLabel);

    sizeValueLabel.setJustificationType(juce::Justification::centred);
    dampValueLabel.setJustificationType(juce::Justification::centred);
    predelayValueLabel.setJustificationType(juce::Justification::centred);
    mixValueLabel.setJustificationType(juce::Justification::centred);
    widthValueLabel.setJustificationType(juce::Justification::centred);
    lowRT60ValueLabel.setJustificationType(juce::Justification::centred);
    midRT60ValueLabel.setJustificationType(juce::Justification::centred);
    highRT60ValueLabel.setJustificationType(juce::Justification::centred);
    vintageValueLabel.setJustificationType(juce::Justification::centred);

    sizeValueLabel.setFont(juce::Font(12.0f));
    dampValueLabel.setFont(juce::Font(12.0f));
    predelayValueLabel.setFont(juce::Font(12.0f));
    mixValueLabel.setFont(juce::Font(12.0f));
    widthValueLabel.setFont(juce::Font(12.0f));
    lowRT60ValueLabel.setFont(juce::Font(12.0f));
    midRT60ValueLabel.setFont(juce::Font(12.0f));
    highRT60ValueLabel.setFont(juce::Font(12.0f));
    vintageValueLabel.setFont(juce::Font(12.0f));

    sizeValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    dampValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    predelayValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    mixValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    widthValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    lowRT60ValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    midRT60ValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    highRT60ValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    vintageValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));

    // Initialize preset list
    updatePresetList();

    // Set initial room shape visibility based on algorithm
    int currentAlgorithm = algorithmSelector.getSelectedId() - 1;
    bool showRoomShape = (currentAlgorithm == 0 || currentAlgorithm == 3);  // Room or Early Reflections
    roomShapeLabel.setVisible(showRoomShape);
    roomShapeSelector.setVisible(showRoomShape);

    // Detect display scale for high-DPI support
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        uiScale = display->scale;
        // Clamp to reasonable range (1.0 to 2.0)
        uiScale = juce::jlimit(1.0f, 2.0f, uiScale);
    }

    // Set window size with DPI awareness
    int baseWidth = 750;
    int baseHeight = 550;
    setSize(scaled(baseWidth), scaled(baseHeight));

    // Make window resizable with proportional limits
    setResizable(true, true);
    setResizeLimits(scaled(650), scaled(500), scaled(1200), scaled(900));

    // Start timer for updating value labels (lower frequency for efficiency)
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

    // Proportional header space (12% of height)
    int headerHeight = static_cast<int>(getHeight() * 0.12f);
    bounds.removeFromTop(headerHeight);

    // Top controls row - Algorithm and Preset selectors (proportional)
    int topRowHeight = static_cast<int>(getHeight() * 0.13f);
    auto topRow = bounds.removeFromTop(topRowHeight);
    int topRowPadding = static_cast<int>(getWidth() * 0.027f);  // ~2.7% of width
    topRow.reduce(topRowPadding, topRowHeight / 7);

    auto leftSection = topRow.removeFromLeft(topRow.getWidth() / 2);

    // Algorithm selector
    algorithmLabel.setBounds(leftSection.removeFromTop(topRowHeight / 3));
    algorithmSelector.setBounds(leftSection.removeFromTop(topRowHeight / 2).reduced(0, topRowHeight / 14));

    // Preset selector
    presetLabel.setBounds(topRow.removeFromTop(topRowHeight / 3));
    presetSelector.setBounds(topRow.removeFromTop(topRowHeight / 2).reduced(0, topRowHeight / 14));

    // Main controls section (proportional)
    bounds.removeFromTop(static_cast<int>(getHeight() * 0.027f)); // Spacing
    int controlsHeight = static_cast<int>(getHeight() * 0.27f);
    auto controlsArea = bounds.removeFromTop(controlsHeight).reduced(static_cast<int>(getWidth() * 0.04f), 10);

    // Calculate knob sizes proportionally
    int knobSize = static_cast<int>(getWidth() * 0.127f);  // ~12.7% of width
    int labelHeight = static_cast<int>(controlsHeight * 0.12f);
    int valueHeight = static_cast<int>(controlsHeight * 0.13f);
    int totalKnobHeight = knobSize + labelHeight + valueHeight;
    int knobSpacing = (controlsArea.getWidth() - (knobSize * 5)) / 6;

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
    xPos += knobSize + knobSpacing;

    // Width knob
    auto widthArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    widthLabel.setBounds(widthArea.removeFromTop(labelHeight));
    widthSlider.setBounds(widthArea.removeFromTop(knobSize));
    widthValueLabel.setBounds(widthArea.removeFromTop(valueHeight));

    // Advanced section (proportional)
    bounds.removeFromTop(static_cast<int>(getHeight() * 0.018f)); // Spacing
    auto advancedArea = bounds.reduced(static_cast<int>(getWidth() * 0.04f), static_cast<int>(getHeight() * 0.018f));

    // Advanced section header
    int advancedHeaderHeight = static_cast<int>(getHeight() * 0.045f);
    auto advancedHeader = advancedArea.removeFromTop(advancedHeaderHeight);
    advancedSectionLabel.setBounds(advancedHeader);

    // Two-column layout for advanced controls
    int columnPadding = static_cast<int>(getWidth() * 0.013f);
    auto leftColumn = advancedArea.removeFromLeft(advancedArea.getWidth() / 2).reduced(columnPadding, 0);
    auto rightColumn = advancedArea.reduced(columnPadding, 0);

    // Left column: RT60 controls with value labels (proportional row heights)
    int rowHeight = static_cast<int>(getHeight() * 0.055f);
    int labelWidth = static_cast<int>(getWidth() * 0.107f);
    int valueLabelWidth = static_cast<int>(getWidth() * 0.067f);

    auto rt60Row = leftColumn.removeFromTop(rowHeight);
    lowRT60Label.setBounds(rt60Row.removeFromLeft(labelWidth));
    lowRT60ValueLabel.setBounds(rt60Row.removeFromRight(valueLabelWidth));
    lowRT60Slider.setBounds(rt60Row);

    rt60Row = leftColumn.removeFromTop(rowHeight);
    midRT60Label.setBounds(rt60Row.removeFromLeft(labelWidth));
    midRT60ValueLabel.setBounds(rt60Row.removeFromRight(valueLabelWidth));
    midRT60Slider.setBounds(rt60Row);

    rt60Row = leftColumn.removeFromTop(rowHeight);
    highRT60Label.setBounds(rt60Row.removeFromLeft(labelWidth));
    highRT60ValueLabel.setBounds(rt60Row.removeFromRight(valueLabelWidth));
    highRT60Slider.setBounds(rt60Row);

    // Right column: Additional controls (proportional layout)
    int optionRowHeight = rowHeight;  // Same as left column (5.5% of height)
    int optionLabelWidth = static_cast<int>(getWidth() * 0.133f);  // 13.3% for labels
    int selectorWidth = static_cast<int>(getWidth() * 0.16f);  // 16% for selectors

    auto optionRow = rightColumn.removeFromTop(optionRowHeight);
    roomShapeLabel.setBounds(optionRow.removeFromLeft(optionLabelWidth));
    roomShapeSelector.setBounds(optionRow);

    optionRow = rightColumn.removeFromTop(optionRowHeight);
    oversamplingLabel.setBounds(optionRow.removeFromLeft(optionLabelWidth));
    oversamplingSelector.setBounds(optionRow.removeFromLeft(selectorWidth));

    optionRow = rightColumn.removeFromTop(optionRowHeight);
    infiniteLabel.setBounds(optionRow.removeFromLeft(optionLabelWidth));
    int buttonSize = static_cast<int>(getHeight() * 0.04f);  // 4% of height for button
    infiniteButton.setBounds(optionRow.removeFromLeft(buttonSize).reduced(0, static_cast<int>(getHeight() * 0.009f)));

    // Additional row for vintage and sync (proportional spacing)
    int controlSpacing = static_cast<int>(getHeight() * 0.018f);  // 1.8% spacing
    leftColumn.removeFromTop(controlSpacing);
    auto vintageRow = leftColumn.removeFromTop(optionRowHeight);
    int vintageLabelWidth = static_cast<int>(getWidth() * 0.107f);  // 10.7% (same as RT60 labels)
    vintageLabel.setBounds(vintageRow.removeFromLeft(vintageLabelWidth));
    vintageValueLabel.setBounds(vintageRow.removeFromRight(valueLabelWidth));
    vintageSlider.setBounds(vintageRow);

    rightColumn.removeFromTop(controlSpacing);
    auto syncRow = rightColumn.removeFromTop(optionRowHeight);
    predelayBeatsLabel.setBounds(syncRow.removeFromLeft(optionLabelWidth));
    predelayBeatsSelector.setBounds(syncRow.removeFromLeft(selectorWidth));
}

//==============================================================================
void StudioVerbAudioProcessorEditor::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &algorithmSelector)
    {
        // Don't reload preset list here - let timerCallback handle it to avoid race conditions
        // The timerCallback will detect the algorithm change and update accordingly
    }
    else if (comboBoxThatHasChanged == &presetSelector)
    {
        int selectedIndex = presetSelector.getSelectedId() - 1;
        if (selectedIndex >= 0)
        {
            // Find the actual preset index in the full list
            auto currentAlgo = static_cast<StudioVerbAudioProcessor::Algorithm>(algorithmSelector.getSelectedId() - 1);
            auto presetNames = audioProcessor.getPresetNamesForAlgorithm(currentAlgo);

            // Bounds check
            if (selectedIndex < presetNames.size())
            {
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
}

//==============================================================================
void StudioVerbAudioProcessorEditor::updatePresetList()
{
    presetSelector.clear();
    auto currentAlgo = static_cast<StudioVerbAudioProcessor::Algorithm>(algorithmSelector.getSelectedId() - 1);
    auto presetNames = audioProcessor.getPresetNamesForAlgorithm(currentAlgo);

    presetSelector.addItemList(presetNames, 1);

    // Auto-load first preset if available
    if (presetNames.size() > 0)
    {
        // Find and load the first preset for this algorithm
        const auto& presets = audioProcessor.getFactoryPresets();
        for (size_t i = 0; i < presets.size(); ++i)
        {
            if (presets[i].algorithm == currentAlgo)
            {
                audioProcessor.loadPreset(static_cast<int>(i));
                presetSelector.setSelectedId(1, juce::dontSendNotification);
                break;
            }
        }
    }
    else
    {
        presetSelector.setSelectedId(0, juce::dontSendNotification);
    }
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

        // Show room shape selector only for Room and Early Reflections algorithms
        bool showRoomShape = (currentAlgorithm == 0 || currentAlgorithm == 3);  // Room or Early Reflections
        roomShapeLabel.setVisible(showRoomShape);
        roomShapeSelector.setVisible(showRoomShape);
    }
}

//==============================================================================
void StudioVerbAudioProcessorEditor::updateValueLabels()
{
    // Basic parameters
    sizeValueLabel.setText(juce::String(sizeSlider.getValue(), 2), juce::dontSendNotification);
    dampValueLabel.setText(juce::String(dampSlider.getValue(), 2), juce::dontSendNotification);
    predelayValueLabel.setText(juce::String(predelaySlider.getValue(), 1) + " ms", juce::dontSendNotification);
    mixValueLabel.setText(juce::String(static_cast<int>(mixSlider.getValue() * 100)) + "%", juce::dontSendNotification);
    widthValueLabel.setText(juce::String(static_cast<int>(widthSlider.getValue() * 100)) + "%", juce::dontSendNotification);

    // Advanced RT60 parameters
    lowRT60ValueLabel.setText(juce::String(lowRT60Slider.getValue(), 2) + " s", juce::dontSendNotification);
    midRT60ValueLabel.setText(juce::String(midRT60Slider.getValue(), 2) + " s", juce::dontSendNotification);
    highRT60ValueLabel.setText(juce::String(highRT60Slider.getValue(), 2) + " s", juce::dontSendNotification);

    // Vintage parameter
    vintageValueLabel.setText(juce::String(static_cast<int>(vintageSlider.getValue() * 100)) + "%", juce::dontSendNotification);
}
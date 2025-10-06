/*
  ==============================================================================

    Plate Reverb - Plugin Editor
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Custom Look and Feel Implementation
//==============================================================================
PlateReverbLookAndFeel::PlateReverbLookAndFeel()
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

    setColour(juce::Label::textColourId, textColour);
}

void PlateReverbLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
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

//==============================================================================
// Main Editor Implementation
//==============================================================================
PlateReverbAudioProcessorEditor::PlateReverbAudioProcessorEditor(PlateReverbAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);
    setSize(700, 300);
    setResizable(false, false);

    // Size slider
    setupSlider(sizeSlider, sizeLabel, "SIZE");
    sizeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    sizeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "size", sizeSlider);

    // Decay slider
    setupSlider(decaySlider, decayLabel, "DECAY");
    decaySlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    decaySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "decay", decaySlider);

    // Damping slider
    setupSlider(dampingSlider, dampingLabel, "DAMPING");
    dampingSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    dampingSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "damping", dampingSlider);

    // Width slider
    setupSlider(widthSlider, widthLabel, "WIDTH");
    widthSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    widthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "width", widthSlider);

    // Mix slider
    setupSlider(mixSlider, mixLabel, "MIX");
    mixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "mix", mixSlider);

    // Value labels
    addAndMakeVisible(sizeValueLabel);
    addAndMakeVisible(decayValueLabel);
    addAndMakeVisible(dampingValueLabel);
    addAndMakeVisible(widthValueLabel);
    addAndMakeVisible(mixValueLabel);

    sizeValueLabel.setJustificationType(juce::Justification::centred);
    decayValueLabel.setJustificationType(juce::Justification::centred);
    dampingValueLabel.setJustificationType(juce::Justification::centred);
    widthValueLabel.setJustificationType(juce::Justification::centred);
    mixValueLabel.setJustificationType(juce::Justification::centred);

    sizeValueLabel.setFont(juce::Font(12.0f));
    decayValueLabel.setFont(juce::Font(12.0f));
    dampingValueLabel.setFont(juce::Font(12.0f));
    widthValueLabel.setFont(juce::Font(12.0f));
    mixValueLabel.setFont(juce::Font(12.0f));

    sizeValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    decayValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    dampingValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    widthValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    mixValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909090));

    // Start timer for updating value labels
    startTimer(50);
}

PlateReverbAudioProcessorEditor::~PlateReverbAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

//==============================================================================
void PlateReverbAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText)
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
void PlateReverbAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Luna unified background
    g.fillAll(juce::Colour(0xff1a1a1a));

    auto bounds = getLocalBounds();

    // Draw header with Luna styling
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(0, 0, bounds.getWidth(), 60);

    // Plugin name
    g.setFont(juce::Font(26.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xffe0e0e0));
    g.drawText("PLATE REVERB", 20, 10, 400, 30, juce::Justification::left);

    // Subtitle
    g.setFont(juce::Font(11.0f));
    g.setColour(juce::Colour(0xff909090));
    g.drawText("Dattorro Plate Algorithm", 20, 35, 400, 20, juce::Justification::left);

    // Company name
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff4a9eff));
    g.drawText("LUNA CO. AUDIO", bounds.getWidth() - 170, 20, 150, 20, juce::Justification::right);

    // Section divider
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawLine(0, 60, getWidth(), 60, 2.0f);
}

//==============================================================================
void PlateReverbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header
    int headerHeight = 70;
    bounds.removeFromTop(headerHeight);

    // Spacing
    bounds.removeFromTop(10);

    // Main controls - 6 knobs in a row
    auto controlsArea = bounds.removeFromTop(200).reduced(30, 10);

    int knobSize = 100;
    int labelHeight = 20;
    int valueHeight = 20;
    int totalKnobHeight = knobSize + labelHeight + valueHeight;
    int knobSpacing = (controlsArea.getWidth() - (knobSize * 5)) / 6;

    int xPos = knobSpacing;

    // Size knob
    auto sizeArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    sizeLabel.setBounds(sizeArea.removeFromTop(labelHeight));
    sizeSlider.setBounds(sizeArea.removeFromTop(knobSize));
    sizeValueLabel.setBounds(sizeArea.removeFromTop(valueHeight));
    xPos += knobSize + knobSpacing;

    // Decay knob
    auto decayArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    decayLabel.setBounds(decayArea.removeFromTop(labelHeight));
    decaySlider.setBounds(decayArea.removeFromTop(knobSize));
    decayValueLabel.setBounds(decayArea.removeFromTop(valueHeight));
    xPos += knobSize + knobSpacing;

    // Damping knob
    auto dampingArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    dampingLabel.setBounds(dampingArea.removeFromTop(labelHeight));
    dampingSlider.setBounds(dampingArea.removeFromTop(knobSize));
    dampingValueLabel.setBounds(dampingArea.removeFromTop(valueHeight));
    xPos += knobSize + knobSpacing;

    // Width knob
    auto widthArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    widthLabel.setBounds(widthArea.removeFromTop(labelHeight));
    widthSlider.setBounds(widthArea.removeFromTop(knobSize));
    widthValueLabel.setBounds(widthArea.removeFromTop(valueHeight));
    xPos += knobSize + knobSpacing;

    // Mix knob
    auto mixArea = juce::Rectangle<int>(xPos, controlsArea.getY(), knobSize, totalKnobHeight);
    mixLabel.setBounds(mixArea.removeFromTop(labelHeight));
    mixSlider.setBounds(mixArea.removeFromTop(knobSize));
    mixValueLabel.setBounds(mixArea.removeFromTop(valueHeight));
}

//==============================================================================
void PlateReverbAudioProcessorEditor::timerCallback()
{
    updateValueLabels();
}

//==============================================================================
void PlateReverbAudioProcessorEditor::updateValueLabels()
{
    sizeValueLabel.setText(juce::String(sizeSlider.getValue(), 2), juce::dontSendNotification);
    decayValueLabel.setText(juce::String(static_cast<int>(decaySlider.getValue() * 100)) + "%", juce::dontSendNotification);
    dampingValueLabel.setText(juce::String(static_cast<int>(dampingSlider.getValue() * 100)) + "%", juce::dontSendNotification);
    widthValueLabel.setText(juce::String(static_cast<int>(widthSlider.getValue() * 100)) + "%", juce::dontSendNotification);
    mixValueLabel.setText(juce::String(static_cast<int>(mixSlider.getValue() * 100)) + "%", juce::dontSendNotification);
}

/*
  ==============================================================================

    VintageVerb - Plugin Editor Implementation

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Custom Look and Feel Implementation
VintageVerbAudioProcessorEditor::VintageVerbLookAndFeel::VintageVerbLookAndFeel()
{
    // Vintage color scheme
    setColour (juce::Slider::thumbColourId, juce::Colour (0xff8b7355));
    setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff6b5d54));
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3d3d3d));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4a4a4a));
    setColour (juce::TextButton::textColourOffId, juce::Colour (0xffd4d4d4));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2a2a));
    setColour (juce::ComboBox::textColourId, juce::Colour (0xffd4d4d4));
}

void VintageVerbAudioProcessorEditor::VintageVerbLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider)
{
    auto radius = juce::jmin (width / 2, height / 2) - 4.0f;
    auto centreX = x + width * 0.5f;
    auto centreY = y + height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Background
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillEllipse (rx, ry, rw, rw);

    // Outline
    g.setColour (juce::Colour (0xff3d3d3d));
    g.drawEllipse (rx, ry, rw, rw, 2.0f);

    // Pointer
    juce::Path p;
    auto pointerLength = radius * 0.6f;
    auto pointerThickness = 3.0f;
    p.addRectangle (-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (centreX, centreY));

    g.setColour (juce::Colour (0xff8b7355));
    g.fillPath (p);

    // Center dot
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillEllipse (centreX - 3, centreY - 3, 6, 6);
}

void VintageVerbAudioProcessorEditor::VintageVerbLookAndFeel::drawLinearSlider (
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float minSliderPos, float maxSliderPos,
    const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearVertical)
    {
        // Draw track
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillRoundedRectangle (x + width * 0.4f, y, width * 0.2f, height, 2.0f);

        // Draw thumb
        auto thumbY = sliderPos;
        g.setColour (juce::Colour (0xff8b7355));
        g.fillRoundedRectangle (x + width * 0.25f, thumbY - 5, width * 0.5f, 10, 3.0f);
    }
}

// Level Meter Implementation
VintageVerbAudioProcessorEditor::LevelMeter::LevelMeter()
{
}

void VintageVerbAudioProcessorEditor::LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRoundedRectangle (bounds, 2.0f);

    // Level
    smoothedLevel = smoothedLevel * 0.8f + level * 0.2f;
    float meterHeight = bounds.getHeight() * smoothedLevel;

    // Gradient for level
    juce::ColourGradient gradient (juce::Colour (0xff00ff00), 0, bounds.getBottom(),
                                   juce::Colour (0xffff0000), 0, bounds.getY(), false);
    gradient.addColour (0.7, juce::Colour (0xffffff00));

    g.setGradientFill (gradient);
    g.fillRoundedRectangle (bounds.getX(), bounds.getBottom() - meterHeight,
                            bounds.getWidth(), meterHeight, 2.0f);

    // Border
    g.setColour (juce::Colour (0xff3d3d3d));
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
}

void VintageVerbAudioProcessorEditor::LevelMeter::setLevel (float newLevel)
{
    level = juce::jlimit (0.0f, 1.0f, newLevel);
    repaint();
}

// Reverb Visualizer Implementation
VintageVerbAudioProcessorEditor::ReverbVisualizer::ReverbVisualizer()
{
}

void VintageVerbAudioProcessorEditor::ReverbVisualizer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Draw frequency response curve
    g.setColour (juce::Colour (0xff8b7355));

    responseCurve.clear();
    float width = bounds.getWidth();
    float height = bounds.getHeight();

    for (int x = 0; x < width; ++x)
    {
        float freq = (x / width) * 20000.0f;
        float response = 1.0f - currentDamping * (freq / 20000.0f);
        response *= (1.0f + currentSize * 0.5f);
        response *= (1.0f + currentDiffusion * 0.3f);

        float y = bounds.getBottom() - (response * height * 0.8f + height * 0.1f);

        if (x == 0)
            responseCurve.startNewSubPath (x, y);
        else
            responseCurve.lineTo (x, y);
    }

    g.strokePath (responseCurve, juce::PathStrokeType (2.0f));

    // Draw grid
    g.setColour (juce::Colour (0x20ffffff));
    for (int i = 1; i < 4; ++i)
    {
        float y = bounds.getY() + (bounds.getHeight() / 4.0f) * i;
        g.drawHorizontalLine (static_cast<int>(y), bounds.getX(), bounds.getRight());
    }

    // Border
    g.setColour (juce::Colour (0xff3d3d3d));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void VintageVerbAudioProcessorEditor::ReverbVisualizer::updateDisplay (
    float size, float damping, float diffusion)
{
    currentSize = size;
    currentDamping = damping;
    currentDiffusion = diffusion;
    repaint();
}

//==============================================================================
VintageVerbAudioProcessorEditor::VintageVerbAudioProcessorEditor (VintageVerbAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&customLookAndFeel);

    // Set up main controls
    setupSlider (mixSlider, mixLabel, "Mix");
    setupSlider (sizeSlider, sizeLabel, "Size");
    setupSlider (attackSlider, attackLabel, "Attack");
    setupSlider (dampingSlider, dampingLabel, "Damping");
    setupSlider (predelaySlider, predelayLabel, "PreDelay");
    setupSlider (widthSlider, widthLabel, "Width");
    setupSlider (modulationSlider, modulationLabel, "Mod");

    // Set up EQ controls
    setupSlider (bassFreqSlider, bassFreqLabel, "Bass Hz");
    setupSlider (bassMulSlider, bassMulLabel, "Bass x");
    setupSlider (highFreqSlider, highFreqLabel, "High Hz");
    setupSlider (highMulSlider, highMulLabel, "High x");

    // Set up advanced controls
    setupSlider (densitySlider, densityLabel, "Density");
    setupSlider (diffusionSlider, diffusionLabel, "Diffusion");
    setupSlider (shapeSlider, shapeLabel, "Shape");
    setupSlider (spreadSlider, spreadLabel, "Spread");

    // Set up mode selectors
    reverbModeLabel.setText ("Mode", juce::dontSendNotification);
    reverbModeLabel.attachToComponent (&reverbModeSelector, false);
    addAndMakeVisible (reverbModeSelector);
    reverbModeSelector.addItemList ({"Concert Hall", "Bright Hall", "Plate", "Room",
                                    "Chamber", "Random Space", "Chorus Space", "Ambience",
                                    "Sanctuary", "Dirty Hall", "Dirty Plate", "Smooth Plate",
                                    "Smooth Room", "Smooth Random", "Nonlin", "Chaotic Hall",
                                    "Chaotic Chamber", "Chaotic Neutral", "Cathedral", "Palace",
                                    "Chamber 1979", "Hall 1984"}, 1);
    reverbModeSelector.setSelectedId (1);
    reverbModeSelector.addListener (this);

    colorModeLabel.setText ("Color", juce::dontSendNotification);
    colorModeLabel.attachToComponent (&colorModeSelector, false);
    addAndMakeVisible (colorModeSelector);
    colorModeSelector.addItemList ({"1970s", "1980s", "Now"}, 1);
    colorModeSelector.setSelectedId (3);

    routingModeLabel.setText ("Routing", juce::dontSendNotification);
    routingModeLabel.attachToComponent (&routingModeSelector, false);
    addAndMakeVisible (routingModeSelector);
    routingModeSelector.addItemList ({"Series", "Parallel", "A to B", "B to A"}, 1);
    routingModeSelector.setSelectedId (2);

    setupSlider (engineMixSlider, engineMixLabel, "Engine Mix");

    // Set up filter controls
    setupSlider (hpfFreqSlider, hpfFreqLabel, "HPF");
    setupSlider (lpfFreqSlider, lpfFreqLabel, "LPF");
    setupSlider (tiltGainSlider, tiltGainLabel, "Tilt", juce::Slider::LinearHorizontal);

    // Set up gain controls
    setupSlider (inputGainSlider, inputGainLabel, "In Gain");
    setupSlider (outputGainSlider, outputGainLabel, "Out Gain");

    // Set up preset management
    addAndMakeVisible (presetSelector);
    auto* presetManager = audioProcessor.getPresetManager();
    for (int i = 0; i < presetManager->getNumPresets(); ++i)
    {
        if (auto* preset = presetManager->getPreset(i))
            presetSelector.addItem (preset->name, i + 1);
    }
    presetSelector.onChange = [this] { loadPreset (presetSelector.getSelectedId() - 1); };

    savePresetButton.setButtonText ("Save");
    addAndMakeVisible (savePresetButton);

    loadPresetButton.setButtonText ("Load");
    addAndMakeVisible (loadPresetButton);

    // Set up meters
    addAndMakeVisible (inputMeterL);
    addAndMakeVisible (inputMeterR);
    addAndMakeVisible (outputMeterL);
    addAndMakeVisible (outputMeterR);

    // Set up visualizer
    addAndMakeVisible (reverbDisplay);

    // Create parameter attachments
    auto& params = audioProcessor.getAPVTS();
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "mix", mixSlider);
    sizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "size", sizeSlider);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "attack", attackSlider);
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "damping", dampingSlider);
    predelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "predelay", predelaySlider);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "width", widthSlider);
    modulationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "modulation", modulationSlider);
    bassFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "bassFreq", bassFreqSlider);
    bassMulAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "bassMul", bassMulSlider);
    highFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "highFreq", highFreqSlider);
    highMulAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "highMul", highMulSlider);
    densityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "density", densitySlider);
    diffusionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "diffusion", diffusionSlider);
    shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "shape", shapeSlider);
    spreadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "spread", spreadSlider);
    reverbModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (params, "reverbMode", reverbModeSelector);
    colorModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (params, "colorMode", colorModeSelector);
    routingModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (params, "routingMode", routingModeSelector);
    engineMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "engineMix", engineMixSlider);
    hpfFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "hpfFreq", hpfFreqSlider);
    lpfFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "lpfFreq", lpfFreqSlider);
    tiltGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "tiltGain", tiltGainSlider);
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "inputGain", inputGainSlider);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, "outputGain", outputGainSlider);

    setSize (1100, 750);
    startTimerHz (30);
}

VintageVerbAudioProcessorEditor::~VintageVerbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void VintageVerbAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Background gradient
    g.fillAll (juce::Colour (0xff2a2a2a));

    // Title area with larger font
    auto titleArea = getLocalBounds().removeFromTop (60);
    g.setColour (juce::Colour (0xff8b7355));
    g.setFont (32.0f);
    g.drawFittedText ("VintageVerb", titleArea,
                     juce::Justification::centred, 1);

    // Section dividers for better visual organization
    g.setColour (juce::Colour (0xff3d3d3d));
    g.drawLine (0, 60, getWidth(), 60, 2);
    g.drawLine (0, 200, getWidth(), 200, 1);  // After main controls
    g.drawLine (0, 360, getWidth(), 360, 1);  // After EQ/advanced
    g.drawLine (0, 500, getWidth(), 500, 1);  // After filters/gains

    // Section labels with better positioning
    g.setColour (juce::Colour (0xff6a6a6a));
    g.setFont (12.0f);
    g.drawText ("REVERB", 40, 65, 100, 20, juce::Justification::left);
    g.drawText ("EQ & TONE", 40, 230, 100, 20, juce::Justification::left);
    g.drawText ("FILTERS & OUTPUT", 40, 520, 150, 20, juce::Justification::left);
    g.drawText ("PRESETS", 40, 670, 100, 20, juce::Justification::left);
}

void VintageVerbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Title area
    bounds.removeFromTop(60);

    // Main reverb controls section (top)
    auto knobSize = 70;
    auto knobSpacing = 80;

    // Row 1: Main reverb parameters
    int row1Y = 90;
    mixSlider.setBounds(40, row1Y, knobSize, knobSize);
    sizeSlider.setBounds(40 + knobSpacing, row1Y, knobSize, knobSize);
    attackSlider.setBounds(40 + knobSpacing * 2, row1Y, knobSize, knobSize);
    dampingSlider.setBounds(40 + knobSpacing * 3, row1Y, knobSize, knobSize);
    predelaySlider.setBounds(40 + knobSpacing * 4, row1Y, knobSize, knobSize);
    widthSlider.setBounds(40 + knobSpacing * 5, row1Y, knobSize, knobSize);
    modulationSlider.setBounds(40 + knobSpacing * 6, row1Y, knobSize, knobSize);

    // Visualizer on right side
    reverbDisplay.setBounds(getWidth() - 320, 80, 280, 130);

    // Row 2: EQ controls
    int row2Y = 250;
    bassFreqSlider.setBounds(40, row2Y, knobSize, knobSize);
    bassMulSlider.setBounds(40 + knobSpacing, row2Y, knobSize, knobSize);
    highFreqSlider.setBounds(40 + knobSpacing * 2, row2Y, knobSize, knobSize);
    highMulSlider.setBounds(40 + knobSpacing * 3, row2Y, knobSize, knobSize);

    // Row 3: Advanced controls
    int row3Y = 360;
    densitySlider.setBounds(40, row3Y, knobSize, knobSize);
    diffusionSlider.setBounds(40 + knobSpacing, row3Y, knobSize, knobSize);
    shapeSlider.setBounds(40 + knobSpacing * 2, row3Y, knobSize, knobSize);
    spreadSlider.setBounds(40 + knobSpacing * 3, row3Y, knobSize, knobSize);

    // Mode selectors
    int selectorY = 460;
    reverbModeSelector.setBounds(40, selectorY, 180, 30);
    colorModeSelector.setBounds(240, selectorY, 140, 30);
    routingModeSelector.setBounds(400, selectorY, 140, 30);
    engineMixSlider.setBounds(560, selectorY - 10, 180, 50);

    // Row 4: Filters and output
    int row4Y = 540;
    hpfFreqSlider.setBounds(40, row4Y, knobSize, knobSize);
    lpfFreqSlider.setBounds(40 + knobSpacing, row4Y, knobSize, knobSize);
    tiltGainSlider.setBounds(40 + knobSpacing * 2, row4Y + 15, 160, 45);

    inputGainSlider.setBounds(40 + knobSpacing * 4, row4Y, knobSize, knobSize);
    outputGainSlider.setBounds(40 + knobSpacing * 5, row4Y, knobSize, knobSize);

    // Meters
    int meterX = 40 + knobSpacing * 6 + 30;
    inputMeterL.setBounds(meterX, row4Y, 25, 75);
    inputMeterR.setBounds(meterX + 30, row4Y, 25, 75);
    outputMeterL.setBounds(meterX + 70, row4Y, 25, 75);
    outputMeterR.setBounds(meterX + 100, row4Y, 25, 75);

    // Presets at bottom
    int presetY = getHeight() - 60;
    presetSelector.setBounds(40, presetY, 280, 30);
    savePresetButton.setBounds(340, presetY, 120, 30);
    loadPresetButton.setBounds(470, presetY, 120, 30);
}

void VintageVerbAudioProcessorEditor::timerCallback()
{
    // Update level meters
    inputMeterL.setLevel (audioProcessor.getInputLevel (0));
    inputMeterR.setLevel (audioProcessor.getInputLevel (1));
    outputMeterL.setLevel (audioProcessor.getOutputLevel (0));
    outputMeterR.setLevel (audioProcessor.getOutputLevel (1));

    // Update visualizer
    reverbDisplay.updateDisplay (sizeSlider.getValue(),
                                dampingSlider.getValue(),
                                diffusionSlider.getValue());
}

void VintageVerbAudioProcessorEditor::comboBoxChanged (juce::ComboBox* comboBoxThatHasChanged)
{
    // Handle combo box changes if needed
}

void VintageVerbAudioProcessorEditor::setupSlider (juce::Slider& slider, juce::Label& label,
                                                  const juce::String& text,
                                                  juce::Slider::SliderStyle style)
{
    slider.setSliderStyle (style);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 75, 20);  // Wider text box
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffd4d4d4));
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1a1a1a));
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff3a3a3a));
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    label.setFont (juce::Font (10.0f));
    label.attachToComponent (&slider, false);
    addAndMakeVisible (label);
}

void VintageVerbAudioProcessorEditor::loadPreset (int presetIndex)
{
    auto* presetManager = audioProcessor.getPresetManager();
    if (auto* preset = presetManager->getPreset (presetIndex))
    {
        presetManager->applyPreset (preset, audioProcessor.getAPVTS());
    }
}
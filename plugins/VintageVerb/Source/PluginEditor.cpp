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

    // Background circle with subtle gradient
    juce::ColourGradient knobGradient (juce::Colour (0xff2a2a2a), centreX, ry,
                                       juce::Colour (0xff0a0a0a), centreX, ry + rw, false);
    g.setGradientFill (knobGradient);
    g.fillEllipse (rx, ry, rw, rw);

    // Arc track
    juce::Path arcPath;
    arcPath.addCentredArc (centreX, centreY, radius - 2, radius - 2,
                          0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff303030));
    g.strokePath (arcPath, juce::PathStrokeType (2.0f));

    // Value arc
    juce::Path valuePath;
    valuePath.addCentredArc (centreX, centreY, radius - 2, radius - 2,
                            0.0f, rotaryStartAngle, angle, true);

    // Gradient for value arc (orange to dark orange like Valhalla)
    juce::ColourGradient valueGradient (juce::Colour (0xffff6b35), centreX - radius, centreY,
                                        juce::Colour (0xffcc4422), centreX + radius, centreY, false);
    g.setGradientFill (valueGradient);
    g.strokePath (valuePath, juce::PathStrokeType (3.0f));

    // Pointer dot (instead of line)
    float pointerRadius = 4.0f;
    float pointerDistance = radius - 8;
    float pointerX = centreX + pointerDistance * std::cos(angle - juce::MathConstants<float>::halfPi);
    float pointerY = centreY + pointerDistance * std::sin(angle - juce::MathConstants<float>::halfPi);

    g.setColour (juce::Colour (0xffffffff));
    g.fillEllipse (pointerX - pointerRadius, pointerY - pointerRadius,
                  pointerRadius * 2, pointerRadius * 2);

    // Inner shadow for depth
    g.setColour (juce::Colour (0x30000000));
    g.drawEllipse (rx + 1, ry + 1, rw - 2, rw - 2, 1.0f);

    // Outer rim highlight
    g.setColour (juce::Colour (0xff4a4a4a));
    g.drawEllipse (rx, ry, rw, rw, 1.0f);
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

// Decay Time Display Implementation
VintageVerbAudioProcessorEditor::DecayTimeDisplay::DecayTimeDisplay()
{
}

void VintageVerbAudioProcessorEditor::DecayTimeDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto centerX = bounds.getCentreX();
    auto centerY = bounds.getCentreY();

    // Background with subtle gradient
    juce::ColourGradient bgGradient (juce::Colour (0xff1a1a1a), centerX, bounds.getY(),
                                     juce::Colour (0xff0a0a0a), centerX, bounds.getBottom(), false);
    g.setGradientFill (bgGradient);
    g.fillRoundedRectangle (bounds, 8.0f);

    // Draw circular decay indicator (like Valhalla)
    float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.35f;

    // Outer ring
    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawEllipse (centerX - radius, centerY - radius - 10, radius * 2, radius * 2, 3.0f);

    // Decay arc (animated based on decay time)
    float arcAngle = juce::jmin(decayTimeSeconds / 10.0f, 1.0f) * juce::MathConstants<float>::twoPi * 0.75f;
    juce::Path decayArc;
    decayArc.addCentredArc (centerX, centerY - 10, radius, radius,
                           0.0f, -juce::MathConstants<float>::halfPi,
                           -juce::MathConstants<float>::halfPi + arcAngle, true);

    // Gradient for decay arc
    juce::ColourGradient arcGradient (juce::Colour (0xffff6b35), centerX, centerY - radius - 10,
                                      juce::Colour (0xff8b4513), centerX + radius, centerY - 10, false);
    g.setGradientFill (arcGradient);
    g.strokePath (decayArc, juce::PathStrokeType (4.0f));

    // Display decay time text
    g.setColour (juce::Colour (0xffe0e0e0));
    g.setFont (42.0f);

    juce::String timeText;
    if (isFrozen)
    {
        timeText = "FREEZE";
        g.setColour (juce::Colour (0xff00b4d8));
    }
    else if (decayTimeSeconds < 10.0f)
    {
        timeText = juce::String (decayTimeSeconds, 2) + " s";
    }
    else
    {
        timeText = juce::String (decayTimeSeconds, 1) + " s";
    }

    g.drawFittedText (timeText, bounds.reduced(10).toNearestInt(),
                     juce::Justification::centred, 1);

    // Small label
    g.setColour (juce::Colour (0xff808080));
    g.setFont (11.0f);
    g.drawText ("DECAY TIME", bounds.reduced(5).withTrimmedTop(bounds.getHeight() * 0.7f),
               juce::Justification::centred, false);

    // Border with subtle glow
    g.setColour (juce::Colour (0xff4a4a4a));
    g.drawRoundedRectangle (bounds.reduced(1), 8.0f, 1.0f);
}

void VintageVerbAudioProcessorEditor::DecayTimeDisplay::setDecayTime (float seconds)
{
    decayTimeSeconds = seconds;
    repaint();
}

void VintageVerbAudioProcessorEditor::DecayTimeDisplay::setFreeze (bool frozen)
{
    isFrozen = frozen;
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

    // Set up decay display
    addAndMakeVisible (decayDisplay);

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

    setSize (900, 600);
    startTimerHz (30);
}

VintageVerbAudioProcessorEditor::~VintageVerbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void VintageVerbAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Dark background with subtle gradient
    juce::ColourGradient bgGradient (juce::Colour (0xff1e1e1e), getWidth() / 2.0f, 0,
                                     juce::Colour (0xff0a0a0a), getWidth() / 2.0f, getHeight(), false);
    g.setGradientFill (bgGradient);
    g.fillAll();

    // Title area - more subtle
    auto titleArea = getLocalBounds().removeFromTop (45);

    // Title with gradient text effect
    g.setColour (juce::Colour (0xffff6b35));
    g.setFont (juce::Font ("Arial", 24.0f, juce::Font::bold));
    g.drawText ("VintageVerb", titleArea.reduced(20, 0),
               juce::Justification::left, false);

    // Subtitle
    g.setColour (juce::Colour (0xff808080));
    g.setFont (12.0f);
    g.drawText ("by Luna Co. Audio", titleArea.reduced(20, 0).withTrimmedLeft(150),
               juce::Justification::left, false);

    // Draw section backgrounds with subtle separation
    auto mainArea = getLocalBounds().withTrimmedTop(45);

    // Top section background (main controls)
    g.setColour (juce::Colour (0x10ffffff));
    g.fillRoundedRectangle (20, 55, 240, 200, 6);

    // Decay display background glow
    g.setColour (juce::Colour (0x15ff6b35));
    g.fillRoundedRectangle (270, 55, 200, 200, 6);

    // Right section (modulation/EQ)
    g.setColour (juce::Colour (0x10ffffff));
    g.fillRoundedRectangle (480, 55, 400, 200, 6);

    // Bottom section (filters/advanced)
    g.setColour (juce::Colour (0x08ffffff));
    g.fillRoundedRectangle (20, 270, 860, 240, 6);

    // Section labels
    g.setColour (juce::Colour (0xff606060));
    g.setFont (10.0f);
    g.drawText ("MAIN", 30, 60, 60, 15, juce::Justification::left);
    g.drawText ("MODULATION", 490, 60, 80, 15, juce::Justification::left);
    g.drawText ("EQ", 690, 60, 40, 15, juce::Justification::left);
    g.drawText ("DAMPING", 30, 275, 60, 15, juce::Justification::left);
    g.drawText ("SHAPE", 250, 275, 60, 15, juce::Justification::left);
    g.drawText ("DIFFUSION", 470, 275, 80, 15, juce::Justification::left);
    g.drawText ("FILTERS", 690, 275, 60, 15, juce::Justification::left);
}

void VintageVerbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(45);  // Title area

    const int knobSize = 65;
    const int smallKnobSize = 55;
    const int spacing = 75;

    // === TOP SECTION ===
    // Main controls on left (Mix, Size, PreDelay)
    int topY = 85;
    mixSlider.setBounds(35, topY, knobSize, knobSize);
    sizeSlider.setBounds(35, topY + spacing, knobSize, knobSize);
    predelaySlider.setBounds(115, topY, knobSize, knobSize);
    attackSlider.setBounds(115, topY + spacing, knobSize, knobSize);

    // Central decay display
    decayDisplay.setBounds(280, 65, 180, 180);

    // Modulation controls (Width, Mod)
    widthSlider.setBounds(500, topY, knobSize, knobSize);
    modulationSlider.setBounds(500, topY + spacing, knobSize, knobSize);

    // EQ controls (right side)
    bassFreqSlider.setBounds(590, topY, smallKnobSize, smallKnobSize);
    bassMulSlider.setBounds(655, topY, smallKnobSize, smallKnobSize);
    highFreqSlider.setBounds(590, topY + spacing, smallKnobSize, smallKnobSize);
    highMulSlider.setBounds(655, topY + spacing, smallKnobSize, smallKnobSize);

    // Tilt control
    tiltGainSlider.setBounds(730, topY + 30, 140, 45);

    // === BOTTOM SECTION ===
    int bottomY = 300;

    // Damping section
    dampingSlider.setBounds(35, bottomY, knobSize, knobSize);
    densitySlider.setBounds(115, bottomY, knobSize, knobSize);

    // Shape section
    shapeSlider.setBounds(265, bottomY, knobSize, knobSize);
    spreadSlider.setBounds(345, bottomY, knobSize, knobSize);

    // Diffusion section
    diffusionSlider.setBounds(485, bottomY, knobSize, knobSize);
    engineMixSlider.setBounds(565, bottomY + 10, 100, 45);

    // Filter section
    hpfFreqSlider.setBounds(700, bottomY, smallKnobSize, smallKnobSize);
    lpfFreqSlider.setBounds(765, bottomY, smallKnobSize, smallKnobSize);

    // Advanced settings row
    int advancedY = 390;
    reverbModeSelector.setBounds(35, advancedY, 180, 25);
    colorModeSelector.setBounds(230, advancedY, 120, 25);
    routingModeSelector.setBounds(365, advancedY, 120, 25);

    // Input/Output gains
    inputGainSlider.setBounds(520, advancedY - 10, 55, 55);
    outputGainSlider.setBounds(590, advancedY - 10, 55, 55);

    // Meters
    inputMeterL.setBounds(660, advancedY - 5, 20, 60);
    inputMeterR.setBounds(685, advancedY - 5, 20, 60);
    outputMeterL.setBounds(715, advancedY - 5, 20, 60);
    outputMeterR.setBounds(740, advancedY - 5, 20, 60);

    // Mode info display at bottom
    int bottomInfoY = 465;
    auto modeArea = juce::Rectangle<int>(35, bottomInfoY, 350, 25);
    auto colorArea = juce::Rectangle<int>(400, bottomInfoY, 250, 25);

    // Preset management at very bottom
    int presetY = getHeight() - 45;
    presetSelector.setBounds(35, presetY, 250, 25);
    savePresetButton.setBounds(300, presetY, 100, 25);
    loadPresetButton.setBounds(410, presetY, 100, 25);
}

void VintageVerbAudioProcessorEditor::timerCallback()
{
    // Update level meters
    inputMeterL.setLevel (audioProcessor.getInputLevel (0));
    inputMeterR.setLevel (audioProcessor.getInputLevel (1));
    outputMeterL.setLevel (audioProcessor.getOutputLevel (0));
    outputMeterR.setLevel (audioProcessor.getOutputLevel (1));

    // Update decay display based on size and damping
    // Calculate approximate decay time from size and damping
    float size = sizeSlider.getValue();
    float damping = dampingSlider.getValue();

    // Simplified RT60 calculation (approximate)
    float decayTime = size * 10.0f * (1.0f - damping * 0.5f);
    decayDisplay.setDecayTime (decayTime);

    // Check for freeze mode (when size is at max)
    decayDisplay.setFreeze (size >= 0.99f);
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
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 65, 18);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffd4d4d4));
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1a1a1a));
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff3a3a3a));
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, juce::Colour (0xff909090));
    label.setFont (juce::Font (9.0f));
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
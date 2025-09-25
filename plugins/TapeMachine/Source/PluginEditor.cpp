#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <ctime>

CustomLookAndFeel::CustomLookAndFeel()
{
    // Inherits vintage palette from LunaVintageLookAndFeel
    // Can add TapeMachine-specific customizations here if needed
}

CustomLookAndFeel::~CustomLookAndFeel() = default;

// Note: drawRotarySlider and drawToggleButton are inherited from LunaVintageLookAndFeel

ReelAnimation::ReelAnimation()
{
    startTimerHz(30);
}

ReelAnimation::~ReelAnimation()
{
    stopTimer();
}

void ReelAnimation::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto centre = bounds.getCentre();
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.45f;

    // Outer reel housing shadow
    g.setColour(juce::Colour(0x90000000));
    g.fillEllipse(centre.x - radius + 3, centre.y - radius + 3, radius * 2, radius * 2);

    // Metal reel body with gradient
    juce::ColourGradient reelGradient(
        juce::Colour(0xff6a5a4a), centre.x - radius, centre.y - radius,
        juce::Colour(0xff3a3028), centre.x + radius, centre.y + radius, true);
    g.setGradientFill(reelGradient);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

    // Inner ring
    g.setColour(juce::Colour(0xff2a2018));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 3.0f);

    // Tape on reel
    auto tapeRadius = radius * 0.8f;
    g.setColour(juce::Colour(0xff1a1510));
    g.fillEllipse(centre.x - tapeRadius, centre.y - tapeRadius, tapeRadius * 2, tapeRadius * 2);

    // Reel spokes
    g.setColour(juce::Colour(0xff4a3828));
    for (int i = 0; i < 3; ++i)
    {
        float spokeAngle = rotation + (i * 2.0f * juce::MathConstants<float>::pi / 3.0f);

        juce::Path spoke;
        spoke.addRectangle(-radius * 0.6f, -6, radius * 1.2f, 12);
        spoke.applyTransform(juce::AffineTransform::rotation(spokeAngle).translated(centre.x, centre.y));
        g.fillPath(spoke);
    }

    // Center hub with metallic finish
    auto hubRadius = radius * 0.2f;
    juce::ColourGradient hubGradient(
        juce::Colour(0xff8a7a6a), centre.x - hubRadius, centre.y - hubRadius,
        juce::Colour(0xff3a3028), centre.x + hubRadius, centre.y + hubRadius, false);
    g.setGradientFill(hubGradient);
    g.fillEllipse(centre.x - hubRadius, centre.y - hubRadius, hubRadius * 2, hubRadius * 2);

    // Center hole
    g.setColour(juce::Colour(0xff0a0a08));
    g.fillEllipse(centre.x - 8, centre.y - 8, 16, 16);
}

void ReelAnimation::timerCallback()
{
    rotation += rotationSpeed * 0.1f;
    if (rotation > 2.0f * juce::MathConstants<float>::pi)
        rotation -= 2.0f * juce::MathConstants<float>::pi;
    repaint();
}

void ReelAnimation::setSpeed(float speed)
{
    rotationSpeed = juce::jlimit(0.0f, 5.0f, speed);
}

// VUMeter implementation moved to GUI/VUMeter.cpp

TapeMachineAudioProcessorEditor::TapeMachineAudioProcessorEditor (TapeMachineAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel(&customLookAndFeel);

    setupComboBox(tapeMachineSelector, tapeMachineLabel, "MACHINE");
    tapeMachineSelector.addItem("Swiss 800", 1);
    tapeMachineSelector.addItem("Classic 102", 2);
    tapeMachineSelector.addItem("Hybrid Blend", 3);
    tapeMachineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeMachine", tapeMachineSelector);

    setupComboBox(tapeSpeedSelector, tapeSpeedLabel, "SPEED");
    tapeSpeedSelector.addItem("7.5 IPS", 1);
    tapeSpeedSelector.addItem("15 IPS", 2);
    tapeSpeedSelector.addItem("30 IPS", 3);
    tapeSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeSpeed", tapeSpeedSelector);

    setupComboBox(tapeTypeSelector, tapeTypeLabel, "TAPE TYPE");
    tapeTypeSelector.addItem("Type 456", 1);
    tapeTypeSelector.addItem("Type GP9", 2);
    tapeTypeSelector.addItem("Type 911", 3);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeType", tapeTypeSelector);

    setupSlider(inputGainSlider, inputGainLabel, "INPUT");
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "inputGain", inputGainSlider);

    setupSlider(saturationSlider, saturationLabel, "SATURATION");
    saturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "saturation", saturationSlider);

    setupSlider(biasSlider, biasLabel, "BIAS");
    biasAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "bias", biasSlider);

    setupSlider(highpassFreqSlider, highpassFreqLabel, "LOW CUT");
    highpassFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highpassFreq", highpassFreqSlider);

    setupSlider(lowpassFreqSlider, lowpassFreqLabel, "HIGH CUT");
    lowpassFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowpassFreq", lowpassFreqSlider);

    setupSlider(noiseAmountSlider, noiseAmountLabel, "NOISE");
    noiseAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "noiseAmount", noiseAmountSlider);

    setupSlider(wowFlutterSlider, wowFlutterLabel, "WOW/FLUTTER");
    wowFlutterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "wowFlutter", wowFlutterSlider);

    setupSlider(outputGainSlider, outputGainLabel, "OUTPUT");
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outputGain", outputGainSlider);

    noiseEnabledButton.setButtonText("OFF");
    noiseEnabledButton.setClickingTogglesState(true);
    noiseEnabledButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a2828));
    noiseEnabledButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff5a4838));
    noiseEnabledButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
    noiseEnabledButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffE8D4B0));
    noiseEnabledButton.onStateChange = [this]() {
        noiseEnabledButton.setButtonText(noiseEnabledButton.getToggleState() ? "ON" : "OFF");
    };
    addAndMakeVisible(noiseEnabledButton);
    noiseEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "noiseEnabled", noiseEnabledButton);

    addAndMakeVisible(leftReel);
    addAndMakeVisible(rightReel);
    leftReel.setSpeed(1.5f);
    rightReel.setSpeed(1.5f);

    addAndMakeVisible(mainVUMeter);
    startTimerHz(30);

    setSize(900, 650);
    setResizable(true, true);
    setResizeLimits(700, 500, 1400, 1000);
}

TapeMachineAudioProcessorEditor::~TapeMachineAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void TapeMachineAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffF8E4C0));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff3a2828));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff5a4838));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff3a3028));
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffE8D4B0));
    label.setFont(juce::Font(12.0f, juce::Font::bold));
    label.attachToComponent(&slider, false);
    addAndMakeVisible(label);
}

void TapeMachineAudioProcessorEditor::setupComboBox(juce::ComboBox& combo, juce::Label& label, const juce::String& text)
{
    combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff4a3838));
    combo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffF8E4C0));
    combo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff7a5838));
    combo.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffE8D4B0));
    combo.setColour(juce::ComboBox::focusedOutlineColourId, juce::Colour(0xffB8946a));
    combo.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(combo);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffE8D4B0));
    label.setFont(juce::Font(12.0f, juce::Font::bold));
    label.attachToComponent(&combo, false);
    addAndMakeVisible(label);
}

void TapeMachineAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Unified Luna background
    g.fillAll(juce::Colour(LunaLookAndFeel::BACKGROUND_COLOR));

    auto bounds = getLocalBounds();

    // Draw standard Luna header
    LunaLookAndFeel::drawPluginHeader(g, bounds, "TapeMachine", "Vintage Tape Emulation");

    // Add subtle vintage texture overlay for tape machine character
    for (int y = 50; y < getHeight(); y += 4)
    {
        g.setColour(juce::Colour(0x05000000));
        g.drawHorizontalLine(y, 0, getWidth());
    }

    // Title section area (already handled by header)
    auto titleArea = getLocalBounds().removeFromTop(50);

    // Title background panel
    juce::ColourGradient titleGradient(
        juce::Colour(0xff4a3828), titleArea.getX(), titleArea.getY(),
        juce::Colour(0xff2a2018), titleArea.getX(), titleArea.getBottom(), false);
    g.setGradientFill(titleGradient);
    g.fillRect(titleArea);

    // Title border
    g.setColour(juce::Colour(0xff5a4838));
    g.drawRect(titleArea.reduced(1), 2);

    // Main title
    g.setFont(juce::Font("Arial Black", 32.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xffE8D4B0));
    g.drawText("TAPE MACHINE", titleArea.reduced(10, 5).removeFromTop(35),
               juce::Justification::centred);

    // Company name in bottom right corner
    g.setFont(juce::Font("Arial", 11.0f, juce::Font::italic));
    g.setColour(juce::Colour(0xffB8A080));
    g.drawText("Luna Co. Audio", getLocalBounds().removeFromBottom(20).removeFromRight(120),
               juce::Justification::centredRight);

    // Transport section background
    auto transportArea = getLocalBounds().removeFromTop(220).withY(65);
    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(transportArea.reduced(10, 5).toFloat(), 8.0f);

    // Transport section frame
    g.setColour(juce::Colour(0xff4a3828));
    g.drawRoundedRectangle(transportArea.reduced(10, 5).toFloat(), 8.0f, 2.0f);

    // Control panel backgrounds
    auto controlArea = getLocalBounds().removeFromBottom(330);

    // Top control row background
    auto topControls = controlArea.removeFromTop(150);
    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(topControls.reduced(10, 5).toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff4a3828));
    g.drawRoundedRectangle(topControls.reduced(10, 5).toFloat(), 8.0f, 2.0f);

    // Bottom control row background
    auto bottomControls = controlArea.removeFromTop(150);
    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(bottomControls.reduced(10, 5).toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff4a3828));
    g.drawRoundedRectangle(bottomControls.reduced(10, 5).toFloat(), 8.0f, 2.0f);

    // Section labels
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff8a7050));
    g.drawText("TRANSPORT", 20, 70, 100, 20, juce::Justification::left);
    g.drawText("TONE SHAPING", 20, 250, 100, 20, juce::Justification::left);
    g.drawText("CHARACTER", 20, 400, 100, 20, juce::Justification::left);
}

void TapeMachineAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Title area
    area.removeFromTop(65);

    // Transport section with reels and VU meter
    auto transportArea = area.removeFromTop(220);
    transportArea.reduce(20, 10);

    // Reels on sides - larger
    auto reelSize = 140;
    leftReel.setBounds(transportArea.removeFromLeft(reelSize).reduced(5));
    rightReel.setBounds(transportArea.removeFromRight(reelSize).reduced(5));

    // VU meter in center - use MORE vertical space
    auto meterArea = transportArea.removeFromTop(160);
    mainVUMeter.setBounds(meterArea);  // Use full area, no reduction

    // Selectors below VU meter - ensure they're visible
    auto selectorArea = transportArea.removeFromTop(40);
    auto selectorWidth = selectorArea.getWidth() / 3;

    tapeMachineSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(5, 5));
    tapeSpeedSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(5, 5));
    tapeTypeSelector.setBounds(selectorArea.reduced(5, 5));

    // Control knobs
    auto controlArea = area.removeFromBottom(360);
    controlArea.reduce(30, 10);

    // Top row - Main controls
    auto topRow = controlArea.removeFromTop(160);
    topRow.removeFromTop(25); // Space for labels

    auto knobSize = 100;
    auto spacing = (topRow.getWidth() - (knobSize * 5)) / 6;

    topRow.removeFromLeft(spacing);
    inputGainSlider.setBounds(topRow.removeFromLeft(knobSize).withHeight(knobSize));
    topRow.removeFromLeft(spacing);
    saturationSlider.setBounds(topRow.removeFromLeft(knobSize).withHeight(knobSize));
    topRow.removeFromLeft(spacing);
    biasSlider.setBounds(topRow.removeFromLeft(knobSize).withHeight(knobSize));
    topRow.removeFromLeft(spacing);
    wowFlutterSlider.setBounds(topRow.removeFromLeft(knobSize).withHeight(knobSize));
    topRow.removeFromLeft(spacing);
    outputGainSlider.setBounds(topRow.removeFromLeft(knobSize).withHeight(knobSize));

    // Bottom row - Filter and noise controls
    auto bottomRow = controlArea.removeFromTop(150);
    bottomRow.removeFromTop(25); // Space for labels

    spacing = (bottomRow.getWidth() - (knobSize * 3) - 100) / 5;

    bottomRow.removeFromLeft(spacing);
    highpassFreqSlider.setBounds(bottomRow.removeFromLeft(knobSize).withHeight(knobSize));
    bottomRow.removeFromLeft(spacing);
    lowpassFreqSlider.setBounds(bottomRow.removeFromLeft(knobSize).withHeight(knobSize));
    bottomRow.removeFromLeft(spacing);
    noiseAmountSlider.setBounds(bottomRow.removeFromLeft(knobSize).withHeight(knobSize));
    bottomRow.removeFromLeft(spacing);

    // Noise enable button
    noiseEnabledButton.setBounds(bottomRow.removeFromLeft(100).withSizeKeepingCentre(80, 35));
}

void TapeMachineAudioProcessorEditor::timerCallback()
{
    // Get input levels AFTER gain staging to show tape saturation drive
    // This gives visual feedback of how hard you're hitting the tape
    float inputL = audioProcessor.getInputLevelL();
    float inputR = audioProcessor.getInputLevelR();

    // Update VU meter to show input drive level
    mainVUMeter.setLevels(inputL, inputR);

    // Update reel speeds based on transport
    float speed = audioProcessor.isProcessing() ? 1.5f : 0.0f;
    leftReel.setSpeed(speed);
    rightReel.setSpeed(speed);
}
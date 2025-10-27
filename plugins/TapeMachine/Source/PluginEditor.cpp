#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <ctime>

CustomLookAndFeel::CustomLookAndFeel()
{
    // Inherits vintage palette from LunaVintageLookAndFeel
    // TapeMachine-specific customizations
}

CustomLookAndFeel::~CustomLookAndFeel() = default;

void CustomLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider& slider)
{
    // Professional tape machine style rotary knob with 3D appearance
    auto radius = juce::jmin(width / 2, height / 2) - 6.0f;
    auto centreX = x + width * 0.5f;
    auto centreY = y + height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Drop shadow for depth
    g.setColour(juce::Colour(0x40000000));
    g.fillEllipse(rx + 3, ry + 3, rw, rw);

    // Knob body with metallic gradient
    juce::ColourGradient bodyGradient(
        juce::Colour(0xff4a4038), centreX - radius * 0.7f, centreY - radius * 0.7f,
        juce::Colour(0xff2a2018), centreX + radius * 0.7f, centreY + radius * 0.7f,
        true);
    g.setGradientFill(bodyGradient);
    g.fillEllipse(rx, ry, rw, rw);

    // Outer ring for definition
    g.setColour(juce::Colour(0xff6a5848));
    g.drawEllipse(rx, ry, rw, rw, 2.5f);

    // Inner ring detail
    g.setColour(juce::Colour(0xff1a1510));
    g.drawEllipse(rx + 4, ry + 4, rw - 8, rw - 8, 1.5f);

    // Center cap with gradient
    auto capRadius = radius * 0.25f;
    juce::ColourGradient capGradient(
        juce::Colour(0xff5a4838), centreX - capRadius, centreY - capRadius,
        juce::Colour(0xff2a2018), centreX + capRadius, centreY + capRadius,
        false);
    g.setGradientFill(capGradient);
    g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

    // Pointer - highly visible line style
    juce::Path pointer;
    auto pointerLength = radius * 0.75f;
    auto pointerThickness = 3.5f;

    // Main pointer line (bright cream color for maximum visibility)
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -radius + 6,
                                pointerThickness, pointerLength, 1.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(juce::Colour(0xffF8E4C0));
    g.fillPath(pointer);

    // Pointer outline for contrast against knob
    juce::Path pointerOutline;
    pointerOutline.addRoundedRectangle(-pointerThickness * 0.5f - 0.5f, -radius + 6,
                                       pointerThickness + 1.0f, pointerLength, 1.5f);
    pointerOutline.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(juce::Colour(0xff1a1510));
    g.strokePath(pointerOutline, juce::PathStrokeType(0.8f));
}

void CustomLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2);
    auto isOn = button.getToggleState();

    // LED-style indicator background
    if (isOn)
    {
        // Glowing effect when ON
        g.setColour(juce::Colour(0xff8a6a3a).withAlpha(0.3f));
        g.fillRoundedRectangle(bounds.expanded(2), 6.0f);
    }

    // Button body with gradient
    juce::ColourGradient buttonGradient(
        isOn ? juce::Colour(0xff6a5438) : juce::Colour(0xff3a2828),
        bounds.getCentreX(), bounds.getY(),
        isOn ? juce::Colour(0xff4a3828) : juce::Colour(0xff2a1818),
        bounds.getCentreX(), bounds.getBottom(),
        false);
    g.setGradientFill(buttonGradient);
    g.fillRoundedRectangle(bounds, 5.0f);

    // Border
    g.setColour(isOn ? juce::Colour(0xff8a6838) : juce::Colour(0xff5a4838));
    g.drawRoundedRectangle(bounds, 5.0f, 2.0f);

    // LED indicator dot on the left side
    auto ledSize = bounds.getHeight() * 0.35f;
    auto ledBounds = juce::Rectangle<float>(bounds.getX() + 8,
                                             bounds.getCentreY() - ledSize / 2,
                                             ledSize, ledSize);

    if (isOn)
    {
        // Glow
        g.setColour(juce::Colour(0xffaa8a4a).withAlpha(0.5f));
        g.fillEllipse(ledBounds.expanded(2));

        // LED on
        g.setColour(juce::Colour(0xffF8E4C0));
        g.fillEllipse(ledBounds);

        // Highlight
        g.setColour(juce::Colour(0xffffffff));
        g.fillEllipse(ledBounds.reduced(2).withY(ledBounds.getY() + 1));
    }
    else
    {
        // LED off (dark)
        g.setColour(juce::Colour(0xff2a2018));
        g.fillEllipse(ledBounds);
        g.setColour(juce::Colour(0xff4a3828));
        g.drawEllipse(ledBounds, 1.0f);
    }

    // Text - centered in remaining space
    auto textBounds = bounds.withTrimmedLeft(ledSize + 16);
    g.setColour(isOn ? juce::Colour(0xffF8E4C0) : juce::Colour(0xff888888));
    g.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
    g.drawText(button.getButtonText(), textBounds, juce::Justification::centred);
}

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

    autoCompButton.setButtonText("AUTO COMP");
    autoCompButton.setClickingTogglesState(true);
    autoCompButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a2828));
    autoCompButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff5a4838));
    autoCompButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
    autoCompButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffE8D4B0));
    addAndMakeVisible(autoCompButton);
    autoCompAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "autoComp", autoCompButton);

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

    // Company name centered at bottom
    g.setFont(juce::Font("Arial", 10.0f, juce::Font::italic));
    g.setColour(juce::Colour(0x88B8A080));  // More subtle/transparent
    g.drawText("Luna Co. Audio", getLocalBounds().removeFromBottom(18),
               juce::Justification::centred);

    // Transport section background (moved down below header)
    auto workArea = getLocalBounds();
    workArea.removeFromTop(50); // Skip header

    auto transportArea = workArea.removeFromTop(240);
    transportArea.reduce(15, 10);

    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(transportArea.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff4a3828));
    g.drawRoundedRectangle(transportArea.toFloat(), 8.0f, 2.0f);

    // Transport section - no label needed, visually distinct by VU meter and selectors

    // Main controls section
    workArea.removeFromTop(10);
    auto mainControlsArea = workArea.removeFromTop(150);
    mainControlsArea.reduce(15, 5);

    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(mainControlsArea.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff4a3828));
    g.drawRoundedRectangle(mainControlsArea.toFloat(), 8.0f, 2.0f);

    // Main controls section - no label needed, knobs are self-explanatory

    // Character controls section
    workArea.removeFromTop(10);
    auto characterArea = workArea.removeFromTop(150);
    characterArea.reduce(15, 5);

    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(characterArea.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff4a3828));
    g.drawRoundedRectangle(characterArea.toFloat(), 8.0f, 2.0f);

    // Character controls section - no label needed, knobs are self-explanatory
}

void TapeMachineAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Header area
    area.removeFromTop(50);

    // Transport section with reels and VU meter - ENLARGED
    auto transportArea = area.removeFromTop(240);
    transportArea.reduce(20, 12);

    // Reels on sides - slightly larger
    auto reelSize = 150;
    leftReel.setBounds(transportArea.removeFromLeft(reelSize).reduced(8));
    rightReel.setBounds(transportArea.removeFromRight(reelSize).reduced(8));

    // Center area for VU meter and selectors
    transportArea.removeFromTop(28); // Space for "TRANSPORT" label

    // VU meter in center - MUCH LARGER and more prominent
    auto meterArea = transportArea.removeFromTop(150);
    mainVUMeter.setBounds(meterArea.reduced(8, 3));

    // Selectors below VU meter
    transportArea.removeFromTop(12); // Gap for labels
    auto selectorArea = transportArea.removeFromTop(42); // Increased height for labels
    auto selectorWidth = selectorArea.getWidth() / 3;

    tapeMachineSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(6, 2));
    tapeSpeedSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(6, 2));
    tapeTypeSelector.setBounds(selectorArea.reduced(6, 2));

    area.removeFromTop(8); // Gap between sections

    // Main controls section
    auto mainControlsArea = area.removeFromTop(150);
    mainControlsArea.reduce(20, 8);
    mainControlsArea.removeFromTop(28); // Space for "MAIN CONTROLS" label

    auto knobSize = 108;
    auto mainSpacing = (mainControlsArea.getWidth() - (knobSize * 4)) / 5;

    mainControlsArea.removeFromLeft(mainSpacing);
    inputGainSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    biasSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    wowFlutterSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    outputGainSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));

    area.removeFromTop(8); // Gap between sections

    // Character & filtering section
    auto characterArea = area.removeFromTop(150);
    characterArea.reduce(20, 8);
    characterArea.removeFromTop(28); // Space for "CHARACTER & FILTERING" label

    // Center the 5 controls (3 knobs + 2 buttons: 110px + 120px)
    auto charSpacing = (characterArea.getWidth() - (knobSize * 3) - 230) / 6;

    characterArea.removeFromLeft(charSpacing);
    highpassFreqSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);
    lowpassFreqSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);
    noiseAmountSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);

    // Noise enable button - aligned with knobs, more prominent
    auto buttonArea = characterArea.removeFromLeft(110);
    noiseEnabledButton.setBounds(buttonArea.withSizeKeepingCentre(95, 45));
    characterArea.removeFromLeft(charSpacing);

    // Auto-comp button (wider to fit "AUTO COMP" text)
    auto autoCompButtonArea = characterArea.removeFromLeft(120);
    autoCompButton.setBounds(autoCompButtonArea.withSizeKeepingCentre(115, 45));
}

void TapeMachineAudioProcessorEditor::timerCallback()
{
    // Get input levels AFTER gain staging to show tape saturation drive
    // This gives visual feedback of how hard you're hitting the tape
    float inputL = audioProcessor.getInputLevelL();
    float inputR = audioProcessor.getInputLevelR();

    // Update VU meter to show tape drive (input level after input gain)
    mainVUMeter.setLevels(inputL, inputR);

    // Update reel speeds based on transport
    float speed = audioProcessor.isProcessing() ? 1.5f : 0.0f;
    leftReel.setSpeed(speed);
    rightReel.setSpeed(speed);
}
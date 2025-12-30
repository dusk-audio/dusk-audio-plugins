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

    // Pointer - highly visible line style with glow effect
    juce::Path pointer;
    auto pointerLength = radius * 0.75f;
    auto pointerThickness = 4.5f;  // Thicker for better visibility

    // Glow effect behind pointer (subtle warm glow)
    juce::Path glowPath;
    glowPath.addRoundedRectangle(-pointerThickness * 0.5f - 2.0f, -radius + 4,
                                  pointerThickness + 4.0f, pointerLength + 2.0f, 3.0f);
    glowPath.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(juce::Colour(0x30F8E4C0));  // Subtle cream glow
    g.fillPath(glowPath);

    // Main pointer line (bright off-white for maximum visibility)
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -radius + 6,
                                pointerThickness, pointerLength, 2.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(juce::Colour(0xffF5F0E6));  // Brighter off-white
    g.fillPath(pointer);

    // Pointer outline for contrast against knob
    juce::Path pointerOutline;
    pointerOutline.addRoundedRectangle(-pointerThickness * 0.5f - 0.5f, -radius + 6,
                                       pointerThickness + 1.0f, pointerLength, 2.0f);
    pointerOutline.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(juce::Colour(0xff1a1510));
    g.strokePath(pointerOutline, juce::PathStrokeType(1.0f));
}

void CustomLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    // Draw text shadow first for better readability against dark background
    auto bounds = label.getLocalBounds().toFloat();
    auto textColour = label.findColour(juce::Label::textColourId);
    auto font = label.getFont();

    g.setFont(font);

    // Shadow layer (darker, offset down-right)
    g.setColour(juce::Colour(0x80000000));  // Semi-transparent black
    g.drawText(label.getText(), bounds.translated(1.0f, 1.0f),
               label.getJustificationType(), true);

    // Subtle glow layer (warm tint)
    g.setColour(juce::Colour(0x18F8E4C0));  // Very subtle warm glow
    g.drawText(label.getText(), bounds.translated(-0.5f, -0.5f),
               label.getJustificationType(), true);

    // Main text
    g.setColour(textColour);
    g.drawText(label.getText(), bounds, label.getJustificationType(), true);
}

void CustomLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2);
    auto isOn = button.getToggleState();
    bool isNoiseButton = (button.getButtonText() == "ON" || button.getButtonText() == "OFF");
    bool isLinkButton = (button.getButtonText() == "LINK");

    if (isNoiseButton)
    {
        // Vintage rotary switch style for noise enable
        // Use the full width for the switch, leaving room for labels below
        float switchSize = juce::jmin(bounds.getWidth(), bounds.getHeight() - 14.0f);
        auto switchBounds = juce::Rectangle<float>(
            bounds.getCentreX() - switchSize / 2,
            bounds.getY(),
            switchSize, switchSize);

        // Shadow
        g.setColour(juce::Colour(0x40000000));
        g.fillEllipse(switchBounds.translated(2, 2));

        // Switch body - metallic gradient
        juce::ColourGradient bodyGradient(
            juce::Colour(0xff5a5048), switchBounds.getX(), switchBounds.getY(),
            juce::Colour(0xff2a2018), switchBounds.getRight(), switchBounds.getBottom(),
            true);
        g.setGradientFill(bodyGradient);
        g.fillEllipse(switchBounds);

        // Ring - brighter when on
        g.setColour(isOn ? juce::Colour(0xffaa9868) : juce::Colour(0xff4a4038));
        g.drawEllipse(switchBounds.reduced(1), 2.0f);

        // Position indicator (rotates based on state)
        // OFF = pointing left (-135 degrees), ON = pointing right (-45 degrees)
        float indicatorAngle = isOn ? -0.78f : -2.36f;
        float indicatorLength = switchSize * 0.30f;
        float cx = switchBounds.getCentreX();
        float cy = switchBounds.getCentreY();

        // Indicator line
        juce::Path indicator;
        indicator.addRoundedRectangle(-2.0f, -indicatorLength, 4.0f, indicatorLength, 1.5f);
        indicator.applyTransform(juce::AffineTransform::rotation(indicatorAngle).translated(cx, cy));

        g.setColour(isOn ? juce::Colour(0xffF8E4C0) : juce::Colour(0xff888888));
        g.fillPath(indicator);

        // OFF/ON labels below the switch, spread apart - larger and more visible
        float labelY = switchBounds.getBottom() + 3.0f;
        float labelWidth = 28.0f;
        g.setFont(juce::Font(11.0f, juce::Font::bold));

        // OFF label on left - better contrast when inactive
        g.setColour(isOn ? juce::Colour(0x80A09080) : juce::Colour(0xffF5F0E6));
        g.drawText("OFF",
                   juce::Rectangle<float>(cx - switchSize * 0.5f - 4, labelY, labelWidth, 14),
                   juce::Justification::centred);

        // ON label on right - better contrast when inactive
        g.setColour(isOn ? juce::Colour(0xffF5F0E6) : juce::Colour(0x80A09080));
        g.drawText("ON",
                   juce::Rectangle<float>(cx + switchSize * 0.5f - labelWidth + 4, labelY, labelWidth, 14),
                   juce::Justification::centred);
    }
    else if (isLinkButton)
    {
        // Chain link button style (keep existing)
        if (isOn)
        {
            g.setColour(juce::Colour(0xff8a6a3a).withAlpha(0.3f));
            g.fillRoundedRectangle(bounds.expanded(2), 6.0f);
        }

        juce::ColourGradient buttonGradient(
            isOn ? juce::Colour(0xff6a5438) : juce::Colour(0xff3a2828),
            bounds.getCentreX(), bounds.getY(),
            isOn ? juce::Colour(0xff4a3828) : juce::Colour(0xff2a1818),
            bounds.getCentreX(), bounds.getBottom(),
            false);
        g.setGradientFill(buttonGradient);
        g.fillRoundedRectangle(bounds, 5.0f);

        g.setColour(isOn ? juce::Colour(0xff8a6838) : juce::Colour(0xff5a4838));
        g.drawRoundedRectangle(bounds, 5.0f, 2.0f);

        // LED indicator
        auto ledSize = bounds.getHeight() * 0.35f;
        auto ledBounds = juce::Rectangle<float>(bounds.getX() + 8,
                                                 bounds.getCentreY() - ledSize / 2,
                                                 ledSize, ledSize);

        if (isOn)
        {
            g.setColour(juce::Colour(0xffaa8a4a).withAlpha(0.5f));
            g.fillEllipse(ledBounds.expanded(2));
            g.setColour(juce::Colour(0xffF8E4C0));
            g.fillEllipse(ledBounds);
            g.setColour(juce::Colour(0xffffffff));
            g.fillEllipse(ledBounds.reduced(2).withY(ledBounds.getY() + 1));
        }
        else
        {
            g.setColour(juce::Colour(0xff2a2018));
            g.fillEllipse(ledBounds);
            g.setColour(juce::Colour(0xff4a3828));
            g.drawEllipse(ledBounds, 1.0f);
        }

        // Chain icon
        auto iconArea = bounds.withTrimmedLeft(ledSize + 8);
        float cx = iconArea.getCentreX();
        float cy = bounds.getCentreY();

        g.setColour(isOn ? juce::Colour(0xffF8D080) : juce::Colour(0xff888888));

        float linkW = 16.0f;
        float linkH = 10.0f;
        float overlap = 6.0f;

        g.drawRoundedRectangle(cx - linkW + overlap/2, cy - linkH/2, linkW, linkH, 4.0f, 2.0f);
        g.drawRoundedRectangle(cx - overlap/2, cy - linkH/2, linkW, linkH, 4.0f, 2.0f);
    }
    else
    {
        // Default toggle button style
        if (isOn)
        {
            g.setColour(juce::Colour(0xff8a6a3a).withAlpha(0.3f));
            g.fillRoundedRectangle(bounds.expanded(2), 6.0f);
        }

        juce::ColourGradient buttonGradient(
            isOn ? juce::Colour(0xff6a5438) : juce::Colour(0xff3a2828),
            bounds.getCentreX(), bounds.getY(),
            isOn ? juce::Colour(0xff4a3828) : juce::Colour(0xff2a1818),
            bounds.getCentreX(), bounds.getBottom(),
            false);
        g.setGradientFill(buttonGradient);
        g.fillRoundedRectangle(bounds, 5.0f);

        g.setColour(isOn ? juce::Colour(0xff8a6838) : juce::Colour(0xff5a4838));
        g.drawRoundedRectangle(bounds, 5.0f, 2.0f);

        auto ledSize = bounds.getHeight() * 0.35f;
        auto textBounds = bounds.withTrimmedLeft(ledSize + 16);
        g.setColour(isOn ? juce::Colour(0xffF8E4C0) : juce::Colour(0xff888888));
        g.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
        g.drawText(button.getButtonText(), textBounds, juce::Justification::centred);
    }
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

    // Calculate tape radius based on tape amount
    // Minimum tape (hub only) = 0.25, Maximum tape (full reel) = 0.85
    float minTapeRatio = 0.25f;
    float maxTapeRatio = 0.85f;
    float tapeRadius = radius * (minTapeRatio + tapeAmount * (maxTapeRatio - minTapeRatio));

    // Outer reel housing shadow
    g.setColour(juce::Colour(0x90000000));
    g.fillEllipse(centre.x - radius + 3, centre.y - radius + 3, radius * 2, radius * 2);

    // Metal reel flange with gradient (the outer silver ring)
    juce::ColourGradient flangeGradient(
        juce::Colour(0xff8a8078), centre.x - radius, centre.y - radius,
        juce::Colour(0xff4a4540), centre.x + radius, centre.y + radius, true);
    g.setGradientFill(flangeGradient);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

    // Inner flange ring
    g.setColour(juce::Colour(0xff3a3530));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 2.0f);

    // Tape pack - dark brown/black with subtle gradient to show depth
    if (tapeAmount > 0.05f)
    {
        // Tape shadow (depth effect)
        g.setColour(juce::Colour(0xff0a0808));
        g.fillEllipse(centre.x - tapeRadius - 1, centre.y - tapeRadius + 1,
                      tapeRadius * 2 + 2, tapeRadius * 2);

        // Main tape pack with subtle radial gradient
        juce::ColourGradient tapeGradient(
            juce::Colour(0xff2a2420), centre.x, centre.y,
            juce::Colour(0xff1a1510), centre.x, centre.y - tapeRadius, true);
        g.setGradientFill(tapeGradient);
        g.fillEllipse(centre.x - tapeRadius, centre.y - tapeRadius,
                      tapeRadius * 2, tapeRadius * 2);

        // Tape edge highlight (shiny tape surface)
        g.setColour(juce::Colour(0x30ffffff));
        g.drawEllipse(centre.x - tapeRadius + 2, centre.y - tapeRadius + 2,
                      tapeRadius * 2 - 4, tapeRadius * 2 - 4, 1.0f);
    }

    // Reel spokes (visible through the tape hub area)
    float hubRadius = radius * 0.22f;
    g.setColour(juce::Colour(0xff5a4a3a));
    for (int i = 0; i < 3; ++i)
    {
        float spokeAngle = rotation + (i * 2.0f * juce::MathConstants<float>::pi / 3.0f);

        juce::Path spoke;
        // Spokes extend from hub to flange
        float spokeLength = radius * 0.72f;
        float spokeWidth = 8.0f;
        spoke.addRoundedRectangle(-spokeLength, -spokeWidth / 2, spokeLength * 2, spokeWidth, 2.0f);
        spoke.applyTransform(juce::AffineTransform::rotation(spokeAngle).translated(centre.x, centre.y));

        // Only draw spoke portions visible outside the tape
        g.saveState();
        // Clip to area outside tape pack
        juce::Path clipPath;
        clipPath.addEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);
        if (tapeAmount > 0.05f)
        {
            clipPath.setUsingNonZeroWinding(false);
            clipPath.addEllipse(centre.x - tapeRadius, centre.y - tapeRadius,
                               tapeRadius * 2, tapeRadius * 2);
        }
        g.reduceClipRegion(clipPath);
        g.fillPath(spoke);
        g.restoreState();
    }

    // Center hub with metallic finish
    juce::ColourGradient hubGradient(
        juce::Colour(0xffa09080), centre.x - hubRadius, centre.y - hubRadius,
        juce::Colour(0xff4a4038), centre.x + hubRadius, centre.y + hubRadius, false);
    g.setGradientFill(hubGradient);
    g.fillEllipse(centre.x - hubRadius, centre.y - hubRadius, hubRadius * 2, hubRadius * 2);

    // Hub ring detail
    g.setColour(juce::Colour(0xff3a3028));
    g.drawEllipse(centre.x - hubRadius, centre.y - hubRadius, hubRadius * 2, hubRadius * 2, 1.5f);

    // Center spindle hole
    float holeRadius = 6.0f;
    g.setColour(juce::Colour(0xff0a0a08));
    g.fillEllipse(centre.x - holeRadius, centre.y - holeRadius, holeRadius * 2, holeRadius * 2);

    // Spindle highlight
    g.setColour(juce::Colour(0x20ffffff));
    g.fillEllipse(centre.x - holeRadius + 1, centre.y - holeRadius + 1, holeRadius, holeRadius);
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

void ReelAnimation::setTapeAmount(float amount)
{
    tapeAmount = juce::jlimit(0.0f, 1.0f, amount);
}

// VUMeter implementation moved to GUI/VUMeter.cpp

TapeMachineAudioProcessorEditor::TapeMachineAudioProcessorEditor (TapeMachineAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel(&customLookAndFeel);

    setupComboBox(tapeMachineSelector, tapeMachineLabel, "MACHINE");
    tapeMachineSelector.addItem("Swiss 800", 1);
    tapeMachineSelector.addItem("Classic 102", 2);
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
    tapeTypeSelector.addItem("Type 250", 4);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeType", tapeTypeSelector);

    setupComboBox(oversamplingSelector, oversamplingLabel, "HQ");
    oversamplingSelector.addItem("2x", 1);
    oversamplingSelector.addItem("4x", 2);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "oversampling", oversamplingSelector);

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

    setupSlider(wowSlider, wowLabel, "WOW");
    wowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "wowAmount", wowSlider);

    setupSlider(flutterSlider, flutterLabel, "FLUTTER");
    flutterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "flutterAmount", flutterSlider);

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

    // Link button with chain icon - shows input/output are linked (auto-compensation)
    autoCompButton.setButtonText("LINK");
    autoCompButton.setClickingTogglesState(true);
    autoCompButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a2828));
    autoCompButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6a5838));
    autoCompButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
    autoCompButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffF8D080));  // Brighter gold when on
    addAndMakeVisible(autoCompButton);
    autoCompAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "autoComp", autoCompButton);

    // Label for auto-comp button
    autoCompLabel.setText("AUTO COMP", juce::dontSendNotification);
    autoCompLabel.setJustificationType(juce::Justification::centred);
    autoCompLabel.setColour(juce::Label::textColourId, juce::Colour(0xffE8D4B0));
    autoCompLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(autoCompLabel);

    addAndMakeVisible(leftReel);
    addAndMakeVisible(rightReel);
    leftReel.setIsSupplyReel(true);   // Left reel is supply
    rightReel.setIsSupplyReel(false); // Right reel is take-up
    // Both reels start with similar tape amount so they look the same (spokes visible)
    leftReel.setTapeAmount(0.5f);     // Medium tape amount - shows spokes
    rightReel.setTapeAmount(0.5f);    // Medium tape amount - shows spokes
    leftReel.setSpeed(1.5f);
    rightReel.setSpeed(1.5f);

    addAndMakeVisible(mainVUMeter);
    startTimerHz(30);

    setSize(800, 530);
    setResizable(true, true);
    setResizeLimits(600, 400, 1200, 850);
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
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff3a3028));
    addAndMakeVisible(slider);

    // Enhanced label with better contrast for readability
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    // Brighter text color for improved readability
    label.setColour(juce::Label::textColourId, juce::Colour(0xffF8E8D0));
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
    label.setFont(juce::Font(10.0f, juce::Font::bold));
    // Don't attach to component - we'll position manually in resized()
    addAndMakeVisible(label);
}

void TapeMachineAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Unified Luna background
    g.fillAll(juce::Colour(LunaLookAndFeel::BACKGROUND_COLOR));

    auto bounds = getLocalBounds();

    // Draw standard Luna header
    LunaLookAndFeel::drawPluginHeader(g, bounds, "TapeMachine", "Vintage Tape Emulation");

    // Set up clickable area for title (matches header drawing position)
    titleClickArea = juce::Rectangle<int>(10, 5, 180, 30);

    // Add subtle vintage texture overlay for tape machine character
    for (int y = 45; y < getHeight(); y += 4)
    {
        g.setColour(juce::Colour(0x05000000));
        g.drawHorizontalLine(y, 0, getWidth());
    }

    // Company name centered at bottom
    g.setFont(juce::Font("Arial", 10.0f, juce::Font::italic));
    g.setColour(juce::Colour(0x88B8A080));
    g.drawText("Luna Co. Audio", getLocalBounds().removeFromBottom(16),
               juce::Justification::centred);

    // Transport section background (scaled for compact layout)
    auto workArea = getLocalBounds();
    workArea.removeFromTop(45);

    auto transportArea = workArea.removeFromTop(185);
    transportArea.reduce(12, 6);

    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(transportArea.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff4a3828));
    g.drawRoundedRectangle(transportArea.toFloat(), 6.0f, 1.5f);

    // Main controls section
    workArea.removeFromTop(6);
    auto mainControlsArea = workArea.removeFromTop(120);
    mainControlsArea.reduce(12, 4);

    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(mainControlsArea.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff4a3828));
    g.drawRoundedRectangle(mainControlsArea.toFloat(), 6.0f, 1.5f);

    // VTM-style: The dimmed output knob is the main visual indicator
    // No additional graphics needed - the 50% alpha on the output slider
    // combined with the lit AUTO COMP button clearly shows the link

    // Character controls section
    workArea.removeFromTop(6);
    auto characterArea = workArea.removeFromTop(120);
    characterArea.reduce(12, 4);

    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(characterArea.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff4a3828));
    g.drawRoundedRectangle(characterArea.toFloat(), 6.0f, 1.5f);
}

void TapeMachineAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Header area (scaled from 50 to 45)
    area.removeFromTop(45);

    // Transport section with reels and VU meter - compact layout
    auto transportArea = area.removeFromTop(185);
    transportArea.reduce(15, 8);

    // Reels on sides - scaled down
    auto reelSize = 120;
    leftReel.setBounds(transportArea.removeFromLeft(reelSize).reduced(5));
    rightReel.setBounds(transportArea.removeFromRight(reelSize).reduced(5));

    // Center area for VU meter and selectors
    transportArea.removeFromTop(8);

    // VU meter in center - prominent but compact
    auto meterArea = transportArea.removeFromTop(120);
    mainVUMeter.setBounds(meterArea.reduced(5, 2));

    // Labels and selectors below VU meter (4 selectors: Machine, Speed, Tape Type, HQ)
    transportArea.removeFromTop(4);
    auto labelArea = transportArea.removeFromTop(14);
    auto selectorWidth = labelArea.getWidth() / 4;

    // Position labels above their combo boxes
    tapeMachineLabel.setBounds(labelArea.removeFromLeft(selectorWidth).reduced(4, 0));
    tapeSpeedLabel.setBounds(labelArea.removeFromLeft(selectorWidth).reduced(4, 0));
    tapeTypeLabel.setBounds(labelArea.removeFromLeft(selectorWidth).reduced(4, 0));
    oversamplingLabel.setBounds(labelArea.reduced(4, 0));

    transportArea.removeFromTop(2);
    auto selectorArea = transportArea.removeFromTop(32);
    selectorWidth = selectorArea.getWidth() / 4;

    tapeMachineSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(4, 2));
    tapeSpeedSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(4, 2));
    tapeTypeSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(4, 2));
    oversamplingSelector.setBounds(selectorArea.reduced(4, 2));

    area.removeFromTop(6);

    // Main controls section - compact with 5 knobs
    auto mainControlsArea = area.removeFromTop(120);
    mainControlsArea.reduce(15, 5);
    mainControlsArea.removeFromTop(18);

    auto knobSize = 80;  // Slightly smaller to fit 5 knobs
    auto mainSpacing = (mainControlsArea.getWidth() - (knobSize * 5)) / 6;

    mainControlsArea.removeFromLeft(mainSpacing);
    inputGainSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    biasSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    wowSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    flutterSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    outputGainSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));

    area.removeFromTop(6);

    // Character & filtering section - compact
    auto characterArea = area.removeFromTop(120);
    characterArea.reduce(15, 5);
    characterArea.removeFromTop(18);

    // Center the 5 controls (3 knobs + noise switch + link button: 70 + 100 = 170)
    auto charSpacing = (characterArea.getWidth() - (knobSize * 3) - 170) / 6;

    characterArea.removeFromLeft(charSpacing);
    highpassFreqSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);
    lowpassFreqSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);
    noiseAmountSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);

    // Noise enable switch - rotary switch style (square for circular appearance)
    auto buttonArea = characterArea.removeFromLeft(70);
    noiseEnabledButton.setBounds(buttonArea.withSizeKeepingCentre(50, 65));  // Taller to fit ON/OFF labels
    characterArea.removeFromLeft(charSpacing);

    // Link button - wider with centered icon, with label above
    auto autoCompButtonArea = characterArea.removeFromLeft(100);
    auto autoCompLabelArea = autoCompButtonArea.removeFromTop(16);
    autoCompLabel.setBounds(autoCompLabelArea);
    autoCompButton.setBounds(autoCompButtonArea.withSizeKeepingCentre(90, 38));

    // Keep supporters overlay sized to full window
    if (supportersOverlay)
        supportersOverlay->setBounds(getLocalBounds());
}

void TapeMachineAudioProcessorEditor::timerCallback()
{
    // Intelligently detect mono vs stereo track and update VU meter display
    // Mono tracks show single VU meter, stereo tracks show dual L/R meters
    bool isStereo = !audioProcessor.isMonoTrack();
    if (mainVUMeter.isStereoMode() != isStereo)
    {
        mainVUMeter.setStereoMode(isStereo);
    }

    // Get input levels AFTER gain staging to show tape saturation drive
    // This gives visual feedback of how hard you're hitting the tape
    float inputL = audioProcessor.getInputLevelL();
    float inputR = audioProcessor.getInputLevelR();

    // Update VU meter to show tape drive (input level after input gain)
    mainVUMeter.setLevels(inputL, inputR);

    // VTM-style: When auto-comp is enabled, input and output are linked (bidirectional)
    // You can adjust EITHER knob and the other follows to maintain unity gain
    auto* autoCompParam = audioProcessor.getAPVTS().getRawParameterValue("autoComp");
    auto* inputGainParam = audioProcessor.getAPVTS().getRawParameterValue("inputGain");
    auto* outputGainParam = audioProcessor.getAPVTS().getRawParameterValue("outputGain");
    bool autoCompEnabled = autoCompParam && autoCompParam->load() > 0.5f;

    if (autoCompEnabled && inputGainParam && outputGainParam && !isUpdatingGainSliders)
    {
        isUpdatingGainSliders = true;  // Prevent recursive updates

        float currentInputGainDB = inputGainParam->load();
        float currentOutputGainDB = outputGainParam->load();

        // Detect which knob was changed
        bool inputChanged = std::abs(currentInputGainDB - lastInputGainValue) > 0.01f;
        bool outputChanged = std::abs(currentOutputGainDB - lastOutputGainValue) > 0.01f;

        if (inputChanged && !outputChanged)
        {
            // Input was changed -> update output to be inverse
            float compensatedOutputDB = juce::jlimit(-12.0f, 12.0f, -currentInputGainDB);
            if (auto* param = audioProcessor.getAPVTS().getParameter("outputGain"))
            {
                param->setValueNotifyingHost(param->convertTo0to1(compensatedOutputDB));
            }
            lastOutputGainValue = compensatedOutputDB;
        }
        else if (outputChanged && !inputChanged)
        {
            // Output was changed -> update input to be inverse
            float compensatedInputDB = juce::jlimit(-12.0f, 12.0f, -currentOutputGainDB);
            if (auto* param = audioProcessor.getAPVTS().getParameter("inputGain"))
            {
                param->setValueNotifyingHost(param->convertTo0to1(compensatedInputDB));
            }
            lastInputGainValue = compensatedInputDB;
        }

        // Update last values for next comparison
        lastInputGainValue = inputGainParam->load();
        lastOutputGainValue = outputGainParam->load();

        isUpdatingGainSliders = false;
    }
    else if (!autoCompEnabled)
    {
        // When auto-comp is off, just track current values for when it's turned back on
        if (inputGainParam) lastInputGainValue = inputGainParam->load();
        if (outputGainParam) lastOutputGainValue = outputGainParam->load();
    }

    // Update reel speeds and tape amounts based on transport
    // Reel speed varies with wow amount for visual feedback
    bool isPlaying = audioProcessor.isProcessing();
    auto* wowParam = audioProcessor.getAPVTS().getRawParameterValue("wowAmount");
    float wowAmount = wowParam ? wowParam->load() : 0.0f;

    // Get tape speed setting (0 = 7.5 IPS, 1 = 15 IPS, 2 = 30 IPS)
    auto* tapeSpeedParam = audioProcessor.getAPVTS().getRawParameterValue("tapeSpeed");
    int tapeSpeedIndex = tapeSpeedParam ? static_cast<int>(tapeSpeedParam->load()) : 1;

    // Scale reel animation speed based on tape speed
    // 7.5 IPS = 1.0x, 15 IPS = 1.5x, 30 IPS = 2.0x visual speed
    float speedMultiplier = (tapeSpeedIndex == 0) ? 1.0f : (tapeSpeedIndex == 1) ? 1.5f : 2.0f;

    // Base speed when playing, modulated by wow for visual feedback
    // Wow creates slow pitch drift, so add subtle speed wobble
    float baseSpeed = isPlaying ? speedMultiplier : 0.0f;
    float wowWobble = 0.0f;
    if (isPlaying && wowAmount > 0.0f)
    {
        // Use a slow sine wave to create visual wow effect
        wowPhase += 0.02f;  // Slow rate (~0.3Hz at 30fps)
        if (wowPhase > juce::MathConstants<float>::twoPi)
            wowPhase -= juce::MathConstants<float>::twoPi;
        // Scale wobble by wow amount (0-100% -> 0-0.3 speed variation)
        wowWobble = std::sin(wowPhase) * (wowAmount * 0.003f);
    }
    float speed = baseSpeed + wowWobble;
    leftReel.setSpeed(speed);
    rightReel.setSpeed(speed);

    // Animate tape transfer: supply reel loses tape, take-up reel gains tape
    if (isPlaying)
    {
        // Transfer rate: roughly 30 minutes of "tape" over full animation cycle
        // At 30 fps, this gives a slow, realistic tape transfer
        const float transferRate = 0.0001f;

        float supplyTape = leftReel.getTapeAmount();
        float takeupTape = rightReel.getTapeAmount();

        // Transfer tape from supply to take-up
        if (supplyTape > 0.3f)
        {
            supplyTape -= transferRate;
            takeupTape += transferRate;

            leftReel.setTapeAmount(supplyTape);
            rightReel.setTapeAmount(takeupTape);
        }
        else
        {
            // Auto-rewind when tape runs out (loop the animation)
            // Both reels at medium amount so spokes are visible (matches initialization)
            leftReel.setTapeAmount(0.5f);
            rightReel.setTapeAmount(0.5f);
        }
    }
}

void TapeMachineAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
    {
        showSupportersPanel();
    }
}

void TapeMachineAudioProcessorEditor::showSupportersPanel()
{
    if (!supportersOverlay)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>("TapeMachine");
        supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
        addAndMakeVisible(supportersOverlay.get());
    }
    supportersOverlay->setBounds(getLocalBounds());
    supportersOverlay->toFront(true);
    supportersOverlay->setVisible(true);
}

void TapeMachineAudioProcessorEditor::hideSupportersPanel()
{
    if (supportersOverlay)
        supportersOverlay->setVisible(false);
}
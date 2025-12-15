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

    // Check if this is the LINK button - draw chain icon instead of text
    if (button.getButtonText() == "LINK")
    {
        // Draw chain link icon centered in the button (after LED area)
        auto iconArea = bounds.withTrimmedLeft(ledSize + 8);
        float cx = iconArea.getCentreX();
        float cy = bounds.getCentreY();

        g.setColour(isOn ? juce::Colour(0xffF8D080) : juce::Colour(0xff888888));

        // Draw two interlocking chain links
        float linkW = 16.0f;
        float linkH = 10.0f;
        float overlap = 6.0f;  // How much the links overlap

        // Left link
        g.drawRoundedRectangle(cx - linkW + overlap/2, cy - linkH/2, linkW, linkH, 4.0f, 2.0f);
        // Right link (overlapping)
        g.drawRoundedRectangle(cx - overlap/2, cy - linkH/2, linkW, linkH, 4.0f, 2.0f);
    }
    else
    {
        // Text - centered in remaining space
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

    // Link button with chain icon - shows input/output are linked
    autoCompButton.setButtonText("LINK");
    autoCompButton.setClickingTogglesState(true);
    autoCompButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a2828));
    autoCompButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6a5838));
    autoCompButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
    autoCompButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffF8D080));  // Brighter gold when on
    addAndMakeVisible(autoCompButton);
    autoCompAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "autoComp", autoCompButton);

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

    // Labels and selectors below VU meter
    transportArea.removeFromTop(4);
    auto labelArea = transportArea.removeFromTop(14);
    auto selectorWidth = labelArea.getWidth() / 3;

    // Position labels above their combo boxes
    tapeMachineLabel.setBounds(labelArea.removeFromLeft(selectorWidth).reduced(4, 0));
    tapeSpeedLabel.setBounds(labelArea.removeFromLeft(selectorWidth).reduced(4, 0));
    tapeTypeLabel.setBounds(labelArea.reduced(4, 0));

    transportArea.removeFromTop(2);
    auto selectorArea = transportArea.removeFromTop(32);
    selectorWidth = selectorArea.getWidth() / 3;

    tapeMachineSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(4, 2));
    tapeSpeedSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(4, 2));
    tapeTypeSelector.setBounds(selectorArea.reduced(4, 2));

    area.removeFromTop(6);

    // Main controls section - compact
    auto mainControlsArea = area.removeFromTop(120);
    mainControlsArea.reduce(15, 5);
    mainControlsArea.removeFromTop(18);

    auto knobSize = 85;
    auto mainSpacing = (mainControlsArea.getWidth() - (knobSize * 4)) / 5;

    mainControlsArea.removeFromLeft(mainSpacing);
    inputGainSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    biasSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    wowFlutterSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    outputGainSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));

    area.removeFromTop(6);

    // Character & filtering section - compact
    auto characterArea = area.removeFromTop(120);
    characterArea.reduce(15, 5);
    characterArea.removeFromTop(18);

    // Center the 5 controls (3 knobs + 2 button areas: 90 + 100 = 190)
    auto charSpacing = (characterArea.getWidth() - (knobSize * 3) - 190) / 6;

    characterArea.removeFromLeft(charSpacing);
    highpassFreqSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);
    lowpassFreqSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);
    noiseAmountSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);

    // Noise enable button - compact
    auto buttonArea = characterArea.removeFromLeft(90);
    noiseEnabledButton.setBounds(buttonArea.withSizeKeepingCentre(80, 38));
    characterArea.removeFromLeft(charSpacing);

    // Link button - wider with centered icon
    auto autoCompButtonArea = characterArea.removeFromLeft(100);
    autoCompButton.setBounds(autoCompButtonArea.withSizeKeepingCentre(90, 38));
}

void TapeMachineAudioProcessorEditor::timerCallback()
{
    // Intelligently detect mono vs stereo track and update VU meter display
    // Mono tracks show single VU meter, stereo tracks show dual L/R meters
    int numChannels = audioProcessor.getTotalNumInputChannels();
    bool isStereo = (numChannels > 1);
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

    // VTM-style: When auto-comp is enabled, output knob tracks inverse of input
    auto* autoCompParam = audioProcessor.getAPVTS().getRawParameterValue("autoComp");
    auto* inputGainParam = audioProcessor.getAPVTS().getRawParameterValue("inputGain");
    bool autoCompEnabled = autoCompParam && autoCompParam->load() > 0.5f;

    if (autoCompEnabled && inputGainParam)
    {
        // VTM-style: Output is exactly inverse of input for unity gain
        // When input is +6dB, output shows -6dB (net = 0dB through saturation)
        float inputGainDB = inputGainParam->load();
        float compensatedOutputDB = -inputGainDB;

        // Clamp to valid range (-12 to +12)
        compensatedOutputDB = juce::jlimit(-12.0f, 12.0f, compensatedOutputDB);

        // Update the output slider visually (read-only mode)
        outputGainSlider.setValue(compensatedOutputDB, juce::dontSendNotification);
        outputGainSlider.setEnabled(false);  // Can't be adjusted
    }
    else
    {
        outputGainSlider.setEnabled(true);
    }

    // Update reel speeds and tape amounts based on transport
    bool isPlaying = audioProcessor.isProcessing();
    float speed = isPlaying ? 1.5f : 0.0f;
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
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <ctime>

CustomLookAndFeel::CustomLookAndFeel()
{
    // Warm vintage color palette
    backgroundColour = juce::Colour(0xff2d2520);  // Dark brown
    knobColour = juce::Colour(0xff4a3828);        // Medium brown
    pointerColour = juce::Colour(0xffE8A628);     // Warm gold

    setColour(juce::Slider::thumbColourId, pointerColour);
    setColour(juce::Slider::rotarySliderFillColourId, pointerColour);
    setColour(juce::Slider::rotarySliderOutlineColourId, knobColour);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3028));
    setColour(juce::ComboBox::textColourId, juce::Colour(0xffE8D4B0));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffB8A080));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff3a3028));
    setColour(juce::PopupMenu::textColourId, juce::Colour(0xffE8D4B0));
    setColour(juce::Label::textColourId, juce::Colour(0xffE8D4B0));
}

CustomLookAndFeel::~CustomLookAndFeel() = default;

void CustomLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                        juce::Slider& slider)
{
    auto radius = (float) juce::jmin(width / 2, height / 2) - 4.0f;
    auto centreX = (float) x + (float) width  * 0.5f;
    auto centreY = (float) y + (float) height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Deep shadow for 3D effect
    g.setColour(juce::Colour(0x90000000));
    g.fillEllipse(rx + 4, ry + 4, rw, rw);

    // Metallic outer ring with gradient
    juce::ColourGradient outerRing(
        juce::Colour(0xff9a8468), centreX - radius, centreY - radius,
        juce::Colour(0xff4a3828), centreX + radius, centreY + radius, true);
    g.setGradientFill(outerRing);
    g.fillEllipse(rx - 4, ry - 4, rw + 8, rw + 8);

    // Inner ring highlight
    g.setColour(juce::Colour(0xffC4A878));
    g.drawEllipse(rx - 3, ry - 3, rw + 6, rw + 6, 1.5f);

    // Main knob body with vintage bakelite texture
    juce::ColourGradient bodyGradient(
        juce::Colour(0xff5a4030), centreX - radius * 0.7f, centreY - radius * 0.7f,
        juce::Colour(0xff2a1810), centreX + radius * 0.7f, centreY + radius * 0.7f, true);
    g.setGradientFill(bodyGradient);
    g.fillEllipse(rx, ry, rw, rw);

    // Inner detail ring
    g.setColour(juce::Colour(0xff3a2818));
    g.drawEllipse(rx + radius * 0.25f, ry + radius * 0.25f,
                  rw - radius * 0.5f, rw - radius * 0.5f, 2.0f);

    // Center cap with metallic finish
    auto capRadius = radius * 0.3f;
    juce::ColourGradient capGradient(
        juce::Colour(0xffA08860), centreX - capRadius, centreY - capRadius,
        juce::Colour(0xff504030), centreX + capRadius, centreY + capRadius, false);
    g.setGradientFill(capGradient);
    g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

    // Pointer line - classic cream color
    juce::Path pointer;
    auto pointerLength = radius * 0.75f;
    auto pointerWidth = 3.0f;
    pointer.addRectangle(-pointerWidth * 0.5f, -pointerLength, pointerWidth, pointerLength * 0.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    // Pointer shadow
    g.setColour(juce::Colour(0x80000000));
    auto shadowPointer = pointer;
    shadowPointer.applyTransform(juce::AffineTransform::translation(1, 1));
    g.fillPath(shadowPointer);

    // Main pointer
    g.setColour(juce::Colour(0xffF5E8D0));
    g.fillPath(pointer);

    // Position dot on pointer
    auto dotAngle = angle;
    auto dotDistance = radius * 0.65f;
    auto dotX = centreX + dotDistance * std::sin(dotAngle);
    auto dotY = centreY - dotDistance * std::cos(dotAngle);
    g.setColour(juce::Colour(0xffF5E8D0));
    g.fillEllipse(dotX - 3, dotY - 3, 6, 6);

    // Scale markings
    g.setColour(juce::Colour(0xffC4A878));
    for (int i = 0; i <= 10; ++i)
    {
        auto tickAngle = rotaryStartAngle + (i / 10.0f) * (rotaryEndAngle - rotaryStartAngle);
        auto tickLength = (i == 0 || i == 5 || i == 10) ? radius * 0.15f : radius * 0.1f;

        juce::Path tick;
        tick.addRectangle(-1.0f, -radius - 10, 2.0f, tickLength);
        tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(centreX, centreY));

        g.setColour(juce::Colour(0xffC4A878).withAlpha((i == 0 || i == 5 || i == 10) ? 1.0f : 0.6f));
        g.fillPath(tick);
    }

    // Center screw detail
    g.setColour(juce::Colour(0xff1a0a05));
    g.fillEllipse(centreX - 4, centreY - 4, 8, 8);
    g.setColour(juce::Colour(0xff7a6050));
    g.drawEllipse(centreX - 4, centreY - 4, 8, 8, 1.0f);
}

void CustomLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);

    // Vintage switch style
    if (button.getToggleState())
    {
        // On state - warm glow
        g.setColour(juce::Colour(0xffE8A628).withAlpha(0.3f));
        g.fillRoundedRectangle(bounds.expanded(2), 6.0f);

        g.setColour(juce::Colour(0xffE8A628));
        g.fillRoundedRectangle(bounds, 5.0f);

        g.setColour(juce::Colour(0xff2d2520));
        g.setFont(12.0f);
        g.drawText("ON", bounds, juce::Justification::centred);
    }
    else
    {
        // Off state - recessed look
        g.setColour(juce::Colour(0xff1a1510));
        g.fillRoundedRectangle(bounds, 5.0f);

        g.setColour(juce::Colour(0xff3a3028));
        g.fillRoundedRectangle(bounds.reduced(1), 4.0f);

        g.setColour(juce::Colour(0xff8a7050));
        g.setFont(12.0f);
        g.drawText("OFF", bounds, juce::Justification::centred);
    }

    if (shouldDrawButtonAsHighlighted)
    {
        g.setColour(juce::Colour(0xffE8A628).withAlpha(0.4f));
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
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

VUMeter::VUMeter()
{
    startTimerHz(30);
}

VUMeter::~VUMeter()
{
    stopTimer();
}

void VUMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    drawSingleVUMeter(g, bounds);
}

void VUMeter::timerCallback()
{
    // Smooth the levels for realistic needle movement
    smoothedLevelL = smoothedLevelL * smoothingFactor + targetLevelL * (1.0f - smoothingFactor);
    smoothedLevelR = smoothedLevelR * smoothingFactor + targetLevelR * (1.0f - smoothingFactor);
    currentLevelL = smoothedLevelL;
    currentLevelR = smoothedLevelR;

    // Peak hold decay
    if (peakHoldTimeL > 0.0f)
    {
        peakHoldTimeL -= 0.033f;
        if (peakHoldTimeL <= 0.0f)
            peakLevelL = currentLevelL;
    }

    if (peakHoldTimeR > 0.0f)
    {
        peakHoldTimeR -= 0.033f;
        if (peakHoldTimeR <= 0.0f)
            peakLevelR = currentLevelR;
    }

    repaint();
}

void VUMeter::setLevels(float leftLevel, float rightLevel)
{
    targetLevelL = juce::jlimit(0.0f, 1.0f, leftLevel);
    targetLevelR = juce::jlimit(0.0f, 1.0f, rightLevel);

    if (leftLevel > peakLevelL)
    {
        peakLevelL = leftLevel;
        peakHoldTimeL = 2.0f;
    }

    if (rightLevel > peakLevelR)
    {
        peakLevelR = rightLevel;
        peakHoldTimeR = 2.0f;
    }
}

void VUMeter::setPeakLevels(float leftPeak, float rightPeak)
{
    peakLevelL = juce::jlimit(0.0f, 1.0f, leftPeak);
    peakLevelR = juce::jlimit(0.0f, 1.0f, rightPeak);
    peakHoldTimeL = 2.0f;
    peakHoldTimeR = 2.0f;
}

void VUMeter::drawSingleVUMeter(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Vintage VU meter with warm colors
    // Outer bezel
    juce::ColourGradient bezelGradient(
        juce::Colour(0xff8a7a6a), bounds.getX(), bounds.getY(),
        juce::Colour(0xff3a3028), bounds.getRight(), bounds.getBottom(), true);
    g.setGradientFill(bezelGradient);
    g.fillRoundedRectangle(bounds, 8.0f);

    // Inner meter face
    auto meterFace = bounds.reduced(4);
    g.setColour(juce::Colour(0xff2a2018));
    g.fillRoundedRectangle(meterFace, 6.0f);

    // Meter background gradient
    juce::ColourGradient faceGradient(
        juce::Colour(0xff3a3028), meterFace.getX(), meterFace.getY(),
        juce::Colour(0xff1a1510), meterFace.getX(), meterFace.getBottom(), false);
    g.setGradientFill(faceGradient);
    g.fillRoundedRectangle(meterFace.reduced(2), 5.0f);

    // Glass effect
    auto glassArea = meterFace.reduced(4).removeFromTop(meterFace.getHeight() * 0.4f);
    juce::ColourGradient glassGradient(
        juce::Colour(0x20ffffff), glassArea.getX(), glassArea.getY(),
        juce::Colour(0x00ffffff), glassArea.getX(), glassArea.getBottom(), false);
    g.setGradientFill(glassGradient);
    g.fillRoundedRectangle(glassArea, 3.0f);

    auto centerX = meterFace.getCentreX();
    auto centerY = meterFace.getBottom() - 10;
    auto radius = meterFace.getWidth() * 0.7f;

    // Scale markings
    g.setFont(9.0f);
    for (int i = 0; i <= 10; ++i)
    {
        float angle = -2.356f + (i / 10.0f) * 1.571f;
        float tickLength = (i % 5 == 0) ? 12.0f : 8.0f;

        auto x1 = centerX + (radius - tickLength) * std::cos(angle);
        auto y1 = centerY + (radius - tickLength) * std::sin(angle);
        auto x2 = centerX + radius * std::cos(angle);
        auto y2 = centerY + radius * std::sin(angle);

        g.setColour(juce::Colour(0xffE8D4B0));
        g.drawLine(x1, y1, x2, y2, (i % 5 == 0) ? 2.0f : 1.0f);

        // Scale numbers
        if (i % 5 == 0)
        {
            int value = -20 + (i * 4);
            juce::String label = (value <= 0) ? juce::String(value) : "+" + juce::String(value);

            auto labelX = centerX + (radius - 25) * std::cos(angle) - 10;
            auto labelY = centerY + (radius - 25) * std::sin(angle) - 5;

            g.setColour(juce::Colour(0xffE8D4B0));
            g.drawText(label, labelX, labelY, 20, 10, juce::Justification::centred);
        }
    }

    // VU label
    g.setFont(juce::Font("Arial", 14.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xffE8D4B0));
    g.drawText("VU", meterFace.getX(), centerY - 45, meterFace.getWidth(), 20, juce::Justification::centred);

    // Red zone marking
    for (int i = 7; i <= 10; ++i)
    {
        float angle = -2.356f + (i / 10.0f) * 1.571f;
        auto x = centerX + (radius - 5) * std::cos(angle);
        auto y = centerY + (radius - 5) * std::sin(angle);

        g.setColour(juce::Colour(0xffcc3333));
        g.fillEllipse(x - 3, y - 3, 6, 6);
    }

    // Draw needles with realistic shadow
    auto needleLength = radius * 0.85f;

    // Left needle (warm amber)
    float needleAngleL = -2.356f + currentLevelL * 1.571f;

    // Shadow
    g.setColour(juce::Colour(0x60000000));
    g.drawLine(centerX + 2, centerY + 2,
               centerX + needleLength * std::cos(needleAngleL) + 2,
               centerY + needleLength * std::sin(needleAngleL) + 2, 3.0f);

    // Left needle
    g.setColour(juce::Colour(0xffE8A628));
    g.drawLine(centerX, centerY,
               centerX + needleLength * std::cos(needleAngleL),
               centerY + needleLength * std::sin(needleAngleL), 2.0f);

    // Right needle (warm green)
    float needleAngleR = -2.356f + currentLevelR * 1.571f;

    // Shadow
    g.setColour(juce::Colour(0x60000000));
    g.drawLine(centerX + 2, centerY + 2,
               centerX + needleLength * std::cos(needleAngleR) + 2,
               centerY + needleLength * std::sin(needleAngleR) + 2, 3.0f);

    // Right needle
    g.setColour(juce::Colour(0xff88C828));
    g.drawLine(centerX, centerY,
               centerX + needleLength * std::cos(needleAngleR),
               centerY + needleLength * std::sin(needleAngleR), 1.8f);

    // Needle pivot point
    juce::ColourGradient pivotGradient(
        juce::Colour(0xff8a7a6a), centerX - 5, centerY - 5,
        juce::Colour(0xff3a3028), centerX + 5, centerY + 5, true);
    g.setGradientFill(pivotGradient);
    g.fillEllipse(centerX - 5, centerY - 5, 10, 10);

    // L/R indicators
    g.setFont(10.0f);
    g.setColour(juce::Colour(0xffE8A628));
    g.drawText("L", meterFace.getX() + 10, meterFace.getBottom() - 25, 20, 15, juce::Justification::left);
    g.setColour(juce::Colour(0xff88C828));
    g.drawText("R", meterFace.getRight() - 30, meterFace.getBottom() - 25, 20, 15, juce::Justification::right);
}

TapeMachineAudioProcessorEditor::TapeMachineAudioProcessorEditor (TapeMachineAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel(&customLookAndFeel);

    setupComboBox(tapeMachineSelector, tapeMachineLabel, "MACHINE");
    tapeMachineSelector.addItem("Studer A800", 1);
    tapeMachineSelector.addItem("Ampex ATR-102", 2);
    tapeMachineSelector.addItem("Blend", 3);
    tapeMachineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeMachine", tapeMachineSelector);

    setupComboBox(tapeSpeedSelector, tapeSpeedLabel, "SPEED");
    tapeSpeedSelector.addItem("7.5 IPS", 1);
    tapeSpeedSelector.addItem("15 IPS", 2);
    tapeSpeedSelector.addItem("30 IPS", 3);
    tapeSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeSpeed", tapeSpeedSelector);

    setupComboBox(tapeTypeSelector, tapeTypeLabel, "TAPE TYPE");
    tapeTypeSelector.addItem("Ampex 456", 1);
    tapeTypeSelector.addItem("GP9", 2);
    tapeTypeSelector.addItem("BASF 911", 3);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeType", tapeTypeSelector);

    setupSlider(inputGainSlider, inputGainLabel, "INPUT");
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "inputGain", inputGainSlider);

    setupSlider(saturationSlider, saturationLabel, "SATURATION");
    saturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "saturation", saturationSlider);

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

    noiseEnabledButton.setButtonText("NOISE");
    addAndMakeVisible(noiseEnabledButton);
    noiseEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "noiseEnabled", noiseEnabledButton);

    addAndMakeVisible(leftReel);
    addAndMakeVisible(rightReel);
    leftReel.setSpeed(1.5f);
    rightReel.setSpeed(1.5f);

    addAndMakeVisible(mainVUMeter);
    startTimerHz(30);

    setSize(820, 580);
}

TapeMachineAudioProcessorEditor::~TapeMachineAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void TapeMachineAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffE8D4B0));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff2a2018));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff3a3028));
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffC4A878));
    label.setFont(juce::Font(11.0f, juce::Font::bold));
    label.attachToComponent(&slider, false);
    addAndMakeVisible(label);
}

void TapeMachineAudioProcessorEditor::setupComboBox(juce::ComboBox& combo, juce::Label& label, const juce::String& text)
{
    combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3028));
    combo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffE8D4B0));
    combo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff4a3828));
    combo.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffC4A878));
    combo.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(combo);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffC4A878));
    label.setFont(juce::Font(11.0f, juce::Font::bold));
    label.attachToComponent(&combo, false);
    addAndMakeVisible(label);
}

void TapeMachineAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Main background gradient
    juce::ColourGradient backgroundGradient(
        juce::Colour(0xff3a3028), 0, 0,
        juce::Colour(0xff2a2018), getWidth(), getHeight(), true);
    g.setGradientFill(backgroundGradient);
    g.fillAll();

    // Subtle texture overlay
    for (int y = 0; y < getHeight(); y += 4)
    {
        g.setColour(juce::Colour(0x08000000));
        g.drawHorizontalLine(y, 0, getWidth());
    }

    // Title section with vintage style
    auto titleArea = getLocalBounds().removeFromTop(65);

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

    // Subtitle
    g.setFont(juce::Font("Arial", 12.0f, juce::Font::italic));
    g.setColour(juce::Colour(0xffB8A080));
    g.drawText("Luna Co. Audio", titleArea, juce::Justification::centred);

    // Transport section background
    auto transportArea = getLocalBounds().removeFromTop(180).withY(65);
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
    auto transportArea = area.removeFromTop(180);
    transportArea.reduce(20, 10);

    // Reels on sides
    auto reelSize = 120;
    leftReel.setBounds(transportArea.removeFromLeft(reelSize).reduced(10));
    rightReel.setBounds(transportArea.removeFromRight(reelSize).reduced(10));

    // VU meter in center
    auto meterArea = transportArea.removeFromTop(100);
    mainVUMeter.setBounds(meterArea.reduced(20, 10));

    // Selectors below VU meter
    auto selectorArea = transportArea;
    selectorArea.removeFromTop(25);
    auto selectorWidth = selectorArea.getWidth() / 3;

    tapeMachineSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(10, 5));
    tapeSpeedSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(10, 5));
    tapeTypeSelector.setBounds(selectorArea.reduced(10, 5));

    // Control knobs
    auto controlArea = area.removeFromBottom(330);
    controlArea.reduce(30, 10);

    // Top row - Main controls
    auto topRow = controlArea.removeFromTop(150);
    topRow.removeFromTop(25); // Space for labels

    auto knobSize = 90;
    auto spacing = (topRow.getWidth() - (knobSize * 4)) / 5;

    topRow.removeFromLeft(spacing);
    inputGainSlider.setBounds(topRow.removeFromLeft(knobSize).withHeight(knobSize));
    topRow.removeFromLeft(spacing);
    saturationSlider.setBounds(topRow.removeFromLeft(knobSize).withHeight(knobSize));
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
    // Get actual audio levels from the processor
    float outputL = audioProcessor.getOutputLevelL();
    float outputR = audioProcessor.getOutputLevelR();

    // Update VU meter
    mainVUMeter.setLevels(outputL, outputR);

    // Update reel speeds based on transport
    float speed = audioProcessor.isProcessing() ? 1.5f : 0.0f;
    leftReel.setSpeed(speed);
    rightReel.setSpeed(speed);
}
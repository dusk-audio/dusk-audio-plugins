#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <ctime>

CustomLookAndFeel::CustomLookAndFeel()
{
    backgroundColour = juce::Colour(0xff2a2a2a);
    knobColour = juce::Colour(0xff5a5a5a);
    pointerColour = juce::Colour(0xffff6b35);

    setColour(juce::Slider::thumbColourId, pointerColour);
    setColour(juce::Slider::rotarySliderFillColourId, pointerColour);
    setColour(juce::Slider::rotarySliderOutlineColourId, knobColour);
    setColour(juce::ComboBox::backgroundColourId, knobColour);
    setColour(juce::ComboBox::textColourId, juce::Colours::lightgrey);
    setColour(juce::PopupMenu::backgroundColourId, backgroundColour);
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

    // Vintage-style shadow
    g.setColour(juce::Colour(0x60000000));
    g.fillEllipse(rx + 3, ry + 3, rw, rw);

    // Outer metallic bezel
    juce::ColourGradient bezel(juce::Colour(0xff8a7a6a), centreX - radius, centreY,
                               juce::Colour(0xff3a3028), centreX + radius, centreY, false);
    g.setGradientFill(bezel);
    g.fillEllipse(rx - 3, ry - 3, rw + 6, rw + 6);

    // Inner bezel highlight (brass-like)
    g.setColour(juce::Colour(0xffbaa080));
    g.drawEllipse(rx - 2, ry - 2, rw + 4, rw + 4, 1.0f);

    // Bakelite-style knob body with warm brown gradient
    juce::ColourGradient bodyGradient(juce::Colour(0xff4a3828), centreX - radius * 0.7f, centreY - radius * 0.7f,
                                      juce::Colour(0xff1a0a05), centreX + radius * 0.7f, centreY + radius * 0.7f, true);
    g.setGradientFill(bodyGradient);
    g.fillEllipse(rx, ry, rw, rw);

    // Inner ring detail
    g.setColour(juce::Colour(0xff2a1810));
    g.drawEllipse(rx + 4, ry + 4, rw - 8, rw - 8, 2.0f);

    // Center cap with vintage brass look
    auto capRadius = radius * 0.35f;
    juce::ColourGradient capGradient(juce::Colour(0xff8a7050), centreX - capRadius, centreY - capRadius,
                                     juce::Colour(0xff3a2010), centreX + capRadius, centreY + capRadius, false);
    g.setGradientFill(capGradient);
    g.fillEllipse(centreX - capRadius, centreY - capRadius, capRadius * 2, capRadius * 2);

    // Position indicator - cream colored vintage pointer
    juce::Path pointer;
    pointer.addRectangle(-2.0f, -radius + 6, 4.0f, radius * 0.4f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    // Black outline for visibility
    g.setColour(juce::Colour(0xff000000));
    g.strokePath(pointer, juce::PathStrokeType(1.0f));
    // Cream colored pointer
    g.setColour(juce::Colour(0xfff5f0e0));
    g.fillPath(pointer);

    // Tick marks around knob - vintage style
    for (int i = 0; i <= 10; ++i)
    {
        auto tickAngle = rotaryStartAngle + (i / 10.0f) * (rotaryEndAngle - rotaryStartAngle);
        auto tickLength = (i == 0 || i == 5 || i == 10) ? radius * 0.12f : radius * 0.08f;

        juce::Path tick;
        tick.addRectangle(-1.0f, -radius - 8, 2.0f, tickLength);
        tick.applyTransform(juce::AffineTransform::rotation(tickAngle).translated(centreX, centreY));

        g.setColour(juce::Colour(0xffd0c0a0).withAlpha(0.8f));
        g.fillPath(tick);
    }

    // Center screw detail
    g.setColour(juce::Colour(0xff1a0a05));
    g.fillEllipse(centreX - 3, centreY - 3, 6, 6);
    g.setColour(juce::Colour(0xff6a5040));
    g.drawEllipse(centreX - 3, centreY - 3, 6, 6, 0.5f);
}

void CustomLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                        bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);

    g.setColour(button.getToggleState() ? pointerColour : juce::Colour(0xff3a3a3a));
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(juce::Colour(0xff1a1a1a));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    g.setColour(button.getToggleState() ? juce::Colours::white : juce::Colours::grey);
    g.setFont(12.0f);
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred);
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
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.4f;

    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

    g.setColour(juce::Colour(0xff1a1a1a));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 2.0f);

    g.setColour(juce::Colour(0xff5a5a5a));
    for (int i = 0; i < 3; ++i)
    {
        float angle = rotation + (i * 2.0f * juce::MathConstants<float>::pi / 3.0f);
        float x1 = centre.x + std::cos(angle) * radius * 0.3f;
        float y1 = centre.y + std::sin(angle) * radius * 0.3f;
        float x2 = centre.x + std::cos(angle) * radius * 0.9f;
        float y2 = centre.y + std::sin(angle) * radius * 0.9f;

        g.drawLine(x1, y1, x2, y2, 3.0f);
    }

    g.setColour(juce::Colour(0xff0a0a0a));
    g.fillEllipse(centre.x - 10, centre.y - 10, 20, 20);
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

    // Peak hold decay for left channel
    if (peakHoldTimeL > 0.0f)
    {
        peakHoldTimeL -= 0.033f; // ~30Hz timer
        if (peakHoldTimeL <= 0.0f)
        {
            peakLevelL = currentLevelL;
        }
    }

    // Peak hold decay for right channel
    if (peakHoldTimeR > 0.0f)
    {
        peakHoldTimeR -= 0.033f; // ~30Hz timer
        if (peakHoldTimeR <= 0.0f)
        {
            peakLevelR = currentLevelR;
        }
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
        peakHoldTimeL = 2.0f; // Hold peak for 2 seconds
    }

    if (rightLevel > peakLevelR)
    {
        peakLevelR = rightLevel;
        peakHoldTimeR = 2.0f; // Hold peak for 2 seconds
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
    // Draw vintage VU meter background
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRoundedRectangle(bounds.reduced(2), 3.0f);

    auto meterArea = bounds.reduced(5);

    // Draw VU meter face gradient
    juce::ColourGradient faceGradient(
        juce::Colour(0xff3a3a3a), meterArea.getX(), meterArea.getY(),
        juce::Colour(0xff1a1a1a), meterArea.getX(), meterArea.getBottom(), false);
    g.setGradientFill(faceGradient);
    g.fillRoundedRectangle(meterArea, 2.0f);

    // Draw scale arc
    auto centerX = meterArea.getCentreX();
    auto centerY = meterArea.getBottom() - 5;
    auto radius = meterArea.getWidth() * 0.8f;

    // Draw scale markings
    g.setColour(juce::Colour(0xffcccccc));
    for (int i = 0; i <= 10; ++i)
    {
        float angle = -2.356f + (i / 10.0f) * 1.571f; // -135° to -45°
        float tickLength = (i % 5 == 0) ? 10.0f : 6.0f;

        auto x1 = centerX + (radius - tickLength) * std::cos(angle);
        auto y1 = centerY + (radius - tickLength) * std::sin(angle);
        auto x2 = centerX + radius * std::cos(angle);
        auto y2 = centerY + radius * std::sin(angle);

        g.drawLine(x1, y1, x2, y2, (i % 5 == 0) ? 1.5f : 1.0f);
    }

    // Draw VU labels
    g.setFont(8.0f);
    g.setColour(juce::Colour(0xffcccccc));
    g.drawText("-20", meterArea.getX() + 5, meterArea.getY() + 10, 20, 10, juce::Justification::left);
    g.drawText("0", meterArea.getCentreX() - 5, meterArea.getY() + 5, 10, 10, juce::Justification::centred);
    g.drawText("+3", meterArea.getRight() - 20, meterArea.getY() + 10, 15, 10, juce::Justification::right);

    // Draw red zone
    g.setColour(juce::Colour(0xffcc0000));
    for (int i = 8; i <= 10; ++i)
    {
        float angle = -2.356f + (i / 10.0f) * 1.571f;
        auto x = centerX + (radius - 8) * std::cos(angle);
        auto y = centerY + (radius - 8) * std::sin(angle);
        g.fillEllipse(x - 2, y - 2, 4, 4);
    }

    // Draw left needle (red)
    float needleAngleL = -2.356f + currentLevelL * 1.571f; // Map 0-1 to needle range
    auto needleLength = radius * 0.9f;

    // Left needle shadow
    g.setColour(juce::Colour(0x40000000));
    g.drawLine(centerX + 1, centerY + 1,
               centerX + needleLength * std::cos(needleAngleL) + 1,
               centerY + needleLength * std::sin(needleAngleL) + 1, 2.0f);

    // Left needle (red)
    g.setColour(juce::Colour(0xffcc3333));
    g.drawLine(centerX, centerY,
               centerX + needleLength * std::cos(needleAngleL),
               centerY + needleLength * std::sin(needleAngleL), 1.5f);

    // Draw right needle (green)
    float needleAngleR = -2.356f + currentLevelR * 1.571f;

    // Right needle shadow
    g.setColour(juce::Colour(0x40000000));
    g.drawLine(centerX + 1, centerY + 1,
               centerX + needleLength * std::cos(needleAngleR) + 1,
               centerY + needleLength * std::sin(needleAngleR) + 1, 2.0f);

    // Right needle (green)
    g.setColour(juce::Colour(0xff33cc33));
    g.drawLine(centerX, centerY,
               centerX + needleLength * std::cos(needleAngleR),
               centerY + needleLength * std::sin(needleAngleR), 1.3f);

    // Needle pivot
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillEllipse(centerX - 3, centerY - 3, 6, 6);

    // VU label with L/R indicators
    g.setFont(10.0f);
    g.setColour(juce::Colour(0xffffffff));
    g.drawText("VU", meterArea.getX(), meterArea.getBottom() - 15, meterArea.getWidth(), 10, juce::Justification::centred);

    // L/R labels
    g.setFont(8.0f);
    g.setColour(juce::Colour(0xffcc3333));
    g.drawText("L", meterArea.getX() + 5, meterArea.getBottom() - 25, 10, 10, juce::Justification::left);
    g.setColour(juce::Colour(0xff33cc33));
    g.drawText("R", meterArea.getRight() - 15, meterArea.getBottom() - 25, 10, 10, juce::Justification::right);
}

TapeMachineAudioProcessorEditor::TapeMachineAudioProcessorEditor (TapeMachineAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel(&customLookAndFeel);

    setupComboBox(tapeMachineSelector, tapeMachineLabel, "Machine");
    tapeMachineSelector.addItem("Studer A800", 1);
    tapeMachineSelector.addItem("Ampex ATR-102", 2);
    tapeMachineSelector.addItem("Blend", 3);
    tapeMachineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeMachine", tapeMachineSelector);

    setupComboBox(tapeSpeedSelector, tapeSpeedLabel, "Speed");
    tapeSpeedSelector.addItem("7.5 IPS", 1);
    tapeSpeedSelector.addItem("15 IPS", 2);
    tapeSpeedSelector.addItem("30 IPS", 3);
    tapeSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeSpeed", tapeSpeedSelector);

    setupComboBox(tapeTypeSelector, tapeTypeLabel, "Tape");
    tapeTypeSelector.addItem("Ampex 456", 1);
    tapeTypeSelector.addItem("GP9", 2);
    tapeTypeSelector.addItem("BASF 911", 3);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeType", tapeTypeSelector);

    setupSlider(inputGainSlider, inputGainLabel, "Input");
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "inputGain", inputGainSlider);

    setupSlider(saturationSlider, saturationLabel, "Saturation");
    saturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "saturation", saturationSlider);

    setupSlider(highpassFreqSlider, highpassFreqLabel, "HPF");
    highpassFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highpassFreq", highpassFreqSlider);

    setupSlider(lowpassFreqSlider, lowpassFreqLabel, "LPF");
    lowpassFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowpassFreq", lowpassFreqSlider);

    setupSlider(noiseAmountSlider, noiseAmountLabel, "Noise");
    noiseAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "noiseAmount", noiseAmountSlider);

    setupSlider(wowFlutterSlider, wowFlutterLabel, "Wow/Flutter");
    wowFlutterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "wowFlutter", wowFlutterSlider);

    setupSlider(outputGainSlider, outputGainLabel, "Output");
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outputGain", outputGainSlider);

    noiseEnabledButton.setButtonText("Noise");
    addAndMakeVisible(noiseEnabledButton);
    noiseEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "noiseEnabled", noiseEnabledButton);

    addAndMakeVisible(leftReel);
    addAndMakeVisible(rightReel);
    leftReel.setSpeed(1.5f);
    rightReel.setSpeed(1.5f);

    // Add single VU meter at top
    addAndMakeVisible(mainVUMeter);

    // Start timer for updating meters
    startTimerHz(30);

    setSize(900, 550);
}

TapeMachineAudioProcessorEditor::~TapeMachineAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void TapeMachineAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);  // Wider text box
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xfff5f0e0));  // Cream text
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1a1a1a));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff3a3a3a));
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffd0c0a0));  // Vintage cream label
    label.attachToComponent(&slider, false);
    addAndMakeVisible(label);
}

void TapeMachineAudioProcessorEditor::setupComboBox(juce::ComboBox& combo, juce::Label& label, const juce::String& text)
{
    addAndMakeVisible(combo);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.attachToComponent(&combo, false);
    addAndMakeVisible(label);
}

void TapeMachineAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));

    g.setColour(juce::Colour(0xff1a1a1a));
    g.drawRect(getLocalBounds(), 2);

    g.setColour(juce::Colours::lightgrey);
    g.setFont(24.0f);
    g.drawText("TAPE MACHINE", getLocalBounds().removeFromTop(40), juce::Justification::centred);

    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillRoundedRectangle(10, 50, getWidth() - 20, 120, 5);
    g.fillRoundedRectangle(10, 180, getWidth() - 20, 200, 5);
    g.fillRoundedRectangle(10, 390, getWidth() - 20, 100, 5);
}

void TapeMachineAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto topArea = area.removeFromTop(40);

    area.removeFromTop(20);

    auto reelArea = area.removeFromTop(120);
    reelArea.reduce(20, 10);
    auto reelWidth = reelArea.getWidth() / 5;

    leftReel.setBounds(reelArea.removeFromLeft(reelWidth).reduced(10));
    rightReel.setBounds(reelArea.removeFromRight(reelWidth).reduced(10));

    auto selectorArea = reelArea;
    selectorArea.removeFromTop(20);
    auto selectorWidth = selectorArea.getWidth() / 3;

    tapeMachineSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(5, 15));
    tapeSpeedSelector.setBounds(selectorArea.removeFromLeft(selectorWidth).reduced(5, 15));
    tapeTypeSelector.setBounds(selectorArea.reduced(5, 15));

    area.removeFromTop(20);

    auto controlArea = area.removeFromTop(200);
    controlArea.reduce(20, 20);

    auto knobSize = 80;
    auto knobRow1 = controlArea.removeFromTop(knobSize + 20);
    auto knobWidth = knobRow1.getWidth() / 4;

    inputGainSlider.setBounds(knobRow1.removeFromLeft(knobWidth).reduced(10, 0));
    saturationSlider.setBounds(knobRow1.removeFromLeft(knobWidth).reduced(10, 0));
    wowFlutterSlider.setBounds(knobRow1.removeFromLeft(knobWidth).reduced(10, 0));
    outputGainSlider.setBounds(knobRow1.reduced(10, 0));

    auto knobRow2 = controlArea;
    knobWidth = knobRow2.getWidth() / 4;

    highpassFreqSlider.setBounds(knobRow2.removeFromLeft(knobWidth).reduced(10, 0));
    lowpassFreqSlider.setBounds(knobRow2.removeFromLeft(knobWidth).reduced(10, 0));
    noiseAmountSlider.setBounds(knobRow2.removeFromLeft(knobWidth).reduced(10, 0));

    auto buttonArea = knobRow2;
    noiseEnabledButton.setBounds(buttonArea.getCentreX() - 40, buttonArea.getCentreY() - 15, 80, 30);

    // Position single VU meter at the top
    area.removeFromTop(10);
    auto meterArea = area.removeFromTop(80);  // Single meter at top
    mainVUMeter.setBounds(meterArea.reduced(150, 5));  // Center it with good margins
}

void TapeMachineAudioProcessorEditor::timerCallback()
{
    // Get actual audio levels from the processor
    float inputL = audioProcessor.getInputLevelL();
    float inputR = audioProcessor.getInputLevelR();
    float outputL = audioProcessor.getOutputLevelL();
    float outputR = audioProcessor.getOutputLevelR();

    // Update single meter with output levels
    mainVUMeter.setLevels(outputL, outputR);
}
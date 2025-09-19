#include "PluginProcessor.h"
#include "PluginEditor.h"

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

    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillEllipse(rx, ry, rw, rw);

    g.setColour(juce::Colour(0xff1a1a1a));
    g.drawEllipse(rx, ry, rw, rw, 2.0f);

    juce::Path p;
    auto pointerLength = radius * 0.8f;
    auto pointerThickness = 3.0f;
    p.addRectangle(-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
    p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    g.setColour(pointerColour);
    g.fillPath(p);

    g.setColour(juce::Colour(0xff0a0a0a));
    g.fillEllipse(centreX - 5.0f, centreY - 5.0f, 10.0f, 10.0f);
}

void CustomLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
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
    highpassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
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

    setSize(800, 500);
}

TapeMachineAudioProcessorEditor::~TapeMachineAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void TapeMachineAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
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
}
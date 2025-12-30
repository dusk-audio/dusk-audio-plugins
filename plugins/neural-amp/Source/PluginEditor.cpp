#include "PluginProcessor.h"
#include "PluginEditor.h"

// Look and Feel
NeuralAmpLookAndFeel::NeuralAmpLookAndFeel()
{
    setColour(juce::Slider::thumbColourId, ampGold);
    setColour(juce::Slider::rotarySliderFillColourId, ampGold);
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::grey);
    setColour(juce::Label::textColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, ampBrown);
    setColour(juce::TextButton::textColourOnId, ampGold);
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);
}

void NeuralAmpLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider)
{
    auto radius = (float)juce::jmin(width / 2, height / 2) - 4.0f;
    auto centreX = (float)x + (float)width * 0.5f;
    auto centreY = (float)y + (float)height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Knob body
    juce::ColourGradient gradient(juce::Colour(0xFF3A3A3A), centreX, centreY - radius,
                                   juce::Colour(0xFF1A1A1A), centreX, centreY + radius, false);
    g.setGradientFill(gradient);
    g.fillEllipse(rx, ry, rw, rw);

    // Knob edge
    g.setColour(juce::Colour(0xFF505050));
    g.drawEllipse(rx, ry, rw, rw, 2.0f);

    // Pointer
    juce::Path p;
    auto pointerLength = radius * 0.6f;
    auto pointerThickness = 3.0f;
    p.addRoundedRectangle(-pointerThickness * 0.5f, -radius + 4.0f,
                          pointerThickness, pointerLength, pointerThickness * 0.5f);
    p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    g.setColour(ampGold);
    g.fillPath(p);

    // Center cap
    g.setColour(juce::Colour(0xFF2A2A2A));
    g.fillEllipse(centreX - radius * 0.3f, centreY - radius * 0.3f,
                  radius * 0.6f, radius * 0.6f);
}

void NeuralAmpLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& backgroundColour,
                                                 bool shouldDrawButtonAsHighlighted,
                                                 bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

    auto baseColour = backgroundColour;
    if (shouldDrawButtonAsDown)
        baseColour = baseColour.brighter(0.1f);
    else if (shouldDrawButtonAsHighlighted)
        baseColour = baseColour.brighter(0.05f);

    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(juce::Colours::grey);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

// Editor
NeuralAmpAudioProcessorEditor::NeuralAmpAudioProcessorEditor(NeuralAmpAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Model section
    modelLabel.setText("MODEL:", juce::dontSendNotification);
    modelLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(modelLabel);

    modelNameLabel.setText(audioProcessor.getModelName(), juce::dontSendNotification);
    modelNameLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(modelNameLabel);

    loadModelButton.setButtonText("Load Model");
    loadModelButton.onClick = [this]() { loadModel(); };
    addAndMakeVisible(loadModelButton);

    // IR section
    irLabel.setText("CABINET IR:", juce::dontSendNotification);
    irLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(irLabel);

    irNameLabel.setText(audioProcessor.getIRName(), juce::dontSendNotification);
    irNameLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(irNameLabel);

    loadIRButton.setButtonText("Load IR");
    loadIRButton.onClick = [this]() { loadIR(); };
    addAndMakeVisible(loadIRButton);

    // Input controls
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(11.0f));
        addAndMakeVisible(label);
    };

    setupSlider(inputGainSlider, inputGainLabel, "INPUT");
    setupSlider(gateSlider, gateLabel, "GATE");
    setupSlider(bassSlider, bassLabel, "BASS");
    setupSlider(midSlider, midLabel, "MID");
    setupSlider(trebleSlider, trebleLabel, "TREBLE");
    setupSlider(lowCutSlider, lowCutLabel, "LOW CUT");
    setupSlider(highCutSlider, highCutLabel, "HI CUT");
    setupSlider(outputSlider, outputLabel, "OUTPUT");

    // Toggle buttons
    gateButton.setButtonText("Gate");
    addAndMakeVisible(gateButton);

    cabButton.setButtonText("Cab");
    addAndMakeVisible(cabButton);

    bypassButton.setButtonText("Bypass");
    addAndMakeVisible(bypassButton);

    // Attachments
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "input_gain", inputGainSlider);
    gateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "gate_threshold", gateSlider);
    gateEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "gate_enabled", gateButton);
    bassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "bass", bassSlider);
    midAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mid", midSlider);
    trebleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "treble", trebleSlider);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "low_cut", lowCutSlider);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "high_cut", highCutSlider);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "output_gain", outputSlider);
    cabEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "cab_enabled", cabButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "bypass", bypassButton);

    setSize(700, 500);
    startTimerHz(30);
}

NeuralAmpAudioProcessorEditor::~NeuralAmpAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void NeuralAmpAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background gradient
    juce::ColourGradient gradient(juce::Colour(0xFF2A2018), 0, 0,
                                   juce::Colour(0xFF1A1410), 0, (float)getHeight(), false);
    g.setGradientFill(gradient);
    g.fillAll();

    // Header
    g.setColour(juce::Colour(0xFFD4A84B));
    g.setFont(juce::Font(28.0f, juce::Font::bold));
    g.drawText("NEURAL AMP", 20, 10, 200, 30, juce::Justification::left);

    g.setFont(juce::Font(12.0f));
    g.drawText("Luna Co. Audio", getWidth() - 120, 15, 110, 20, juce::Justification::right);

    // Section backgrounds
    auto drawSection = [&g](juce::Rectangle<int> bounds, const juce::String& title)
    {
        g.setColour(juce::Colour(0x40000000));
        g.fillRoundedRectangle(bounds.toFloat(), 8.0f);

        g.setColour(juce::Colour(0x60D4A84B));
        g.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

        if (title.isNotEmpty())
        {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(title, bounds.getX() + 10, bounds.getY() + 5, bounds.getWidth() - 20, 16,
                       juce::Justification::centred);
        }
    };

    // Model section background
    drawSection(juce::Rectangle<int>(20, 50, getWidth() - 40, 60), "");

    // IR section background
    drawSection(juce::Rectangle<int>(20, 420, getWidth() - 40, 60), "");

    // Controls section
    drawSection(juce::Rectangle<int>(20, 120, 120, 280), "INPUT");
    drawSection(juce::Rectangle<int>(150, 120, 400, 280), "TONE STACK");
    drawSection(juce::Rectangle<int>(560, 120, 120, 280), "OUTPUT");

    // Draw meters
    drawMeter(g, juce::Rectangle<int>(25, 460, 80, 12), inputMeterLevel);
    drawMeter(g, juce::Rectangle<int>(getWidth() - 105, 460, 80, 12), outputMeterLevel);

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(10.0f);
    g.drawText("IN", 25, 473, 80, 15, juce::Justification::centred);
    g.drawText("OUT", getWidth() - 105, 473, 80, 15, juce::Justification::centred);
}

void NeuralAmpAudioProcessorEditor::resized()
{
    // Model section
    modelLabel.setBounds(30, 55, 60, 20);
    modelNameLabel.setBounds(95, 55, 400, 20);
    loadModelButton.setBounds(getWidth() - 130, 55, 100, 25);

    // IR section
    irLabel.setBounds(30, 425, 100, 20);
    irNameLabel.setBounds(135, 425, 380, 20);
    loadIRButton.setBounds(getWidth() - 130, 425, 100, 25);
    cabButton.setBounds(getWidth() - 130, 450, 100, 25);

    // Input section
    int knobSize = 70;
    int labelHeight = 18;

    inputGainSlider.setBounds(45, 150, knobSize, knobSize);
    inputGainLabel.setBounds(45, 220, knobSize, labelHeight);

    gateSlider.setBounds(45, 250, knobSize, knobSize);
    gateLabel.setBounds(45, 320, knobSize, labelHeight);
    gateButton.setBounds(50, 340, 60, 25);

    // Tone stack section
    int toneX = 170;
    int toneSpacing = 100;

    bassSlider.setBounds(toneX, 160, knobSize, knobSize);
    bassLabel.setBounds(toneX, 230, knobSize, labelHeight);

    midSlider.setBounds(toneX + toneSpacing, 160, knobSize, knobSize);
    midLabel.setBounds(toneX + toneSpacing, 230, knobSize, labelHeight);

    trebleSlider.setBounds(toneX + toneSpacing * 2, 160, knobSize, knobSize);
    trebleLabel.setBounds(toneX + toneSpacing * 2, 230, knobSize, labelHeight);

    lowCutSlider.setBounds(toneX + 50, 270, knobSize, knobSize);
    lowCutLabel.setBounds(toneX + 50, 340, knobSize, labelHeight);

    highCutSlider.setBounds(toneX + toneSpacing + 50, 270, knobSize, knobSize);
    highCutLabel.setBounds(toneX + toneSpacing + 50, 340, knobSize, labelHeight);

    // Output section
    outputSlider.setBounds(585, 160, knobSize, knobSize);
    outputLabel.setBounds(585, 230, knobSize, labelHeight);

    bypassButton.setBounds(575, 340, 80, 25);
}

void NeuralAmpAudioProcessorEditor::timerCallback()
{
    // Update meters with smoothing
    float newInputLevel = audioProcessor.getInputLevel();
    float newOutputLevel = audioProcessor.getOutputLevel();

    inputMeterLevel = inputMeterLevel * 0.8f + newInputLevel * 0.2f;
    outputMeterLevel = outputMeterLevel * 0.8f + newOutputLevel * 0.2f;

    // Update model/IR names
    modelNameLabel.setText(audioProcessor.getModelName(), juce::dontSendNotification);
    irNameLabel.setText(audioProcessor.getIRName(), juce::dontSendNotification);

    repaint();
}

void NeuralAmpAudioProcessorEditor::loadModel()
{
    // FileChooser must be stored as member variable to stay alive during async callback
    modelChooser = std::make_unique<juce::FileChooser>(
        "Select NAM Model",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.nam");

    auto chooserFlags = juce::FileBrowserComponent::openMode
                      | juce::FileBrowserComponent::canSelectFiles;

    modelChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile())
        {
            if (audioProcessor.loadNAMModel(file))
            {
                modelNameLabel.setText(audioProcessor.getModelName(), juce::dontSendNotification);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Load Failed",
                    "Could not load the NAM model file.");
            }
        }
    });
}

void NeuralAmpAudioProcessorEditor::loadIR()
{
    // FileChooser must be stored as member variable to stay alive during async callback
    irChooser = std::make_unique<juce::FileChooser>(
        "Select Cabinet IR",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.aiff;*.aif");

    auto chooserFlags = juce::FileBrowserComponent::openMode
                      | juce::FileBrowserComponent::canSelectFiles;

    irChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile())
        {
            if (audioProcessor.loadCabinetIR(file))
            {
                irNameLabel.setText(audioProcessor.getIRName(), juce::dontSendNotification);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Load Failed",
                    "Could not load the IR file.");
            }
        }
    });
}

void NeuralAmpAudioProcessorEditor::drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float level)
{
    // Background
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(bounds.toFloat(), 2.0f);

    // Level
    float dbLevel = juce::Decibels::gainToDecibels(level, -60.0f);
    float normalizedLevel = juce::jmap(dbLevel, -60.0f, 0.0f, 0.0f, 1.0f);
    normalizedLevel = juce::jlimit(0.0f, 1.0f, normalizedLevel);

    int meterWidth = static_cast<int>(bounds.getWidth() * normalizedLevel);

    juce::Colour meterColour = normalizedLevel < 0.7f ? juce::Colour(0xFF00AA00) :
                                normalizedLevel < 0.9f ? juce::Colour(0xFFAAAA00) :
                                juce::Colour(0xFFAA0000);

    g.setColour(meterColour);
    g.fillRoundedRectangle(bounds.toFloat().withWidth((float)meterWidth), 2.0f);
}

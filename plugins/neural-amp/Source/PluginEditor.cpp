#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Look and Feel
//==============================================================================
NeuralAmpLookAndFeel::NeuralAmpLookAndFeel()
{
    setColour(juce::Slider::thumbColourId, ampGold);
    setColour(juce::Slider::rotarySliderFillColourId, ampGold);
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::grey);
    setColour(juce::Label::textColourId, ampCream);
    setColour(juce::TextButton::buttonColourId, ampBrown);
    setColour(juce::TextButton::textColourOnId, ampGold);
    setColour(juce::TextButton::textColourOffId, ampCream);
}

void NeuralAmpLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider&)
{
    auto radius = (float)juce::jmin(width / 2, height / 2) - 2.0f;
    auto centreX = (float)x + (float)width * 0.5f;
    auto centreY = (float)y + (float)height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Outer shadow
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(rx + 2, ry + 2, rw, rw);

    // Knob base (dark metal)
    juce::ColourGradient baseGradient(juce::Colour(0xFF4A4A4A), centreX, centreY - radius,
                                       juce::Colour(0xFF1A1A1A), centreX, centreY + radius, false);
    g.setGradientFill(baseGradient);
    g.fillEllipse(rx, ry, rw, rw);

    // Knob highlight ring
    g.setColour(juce::Colour(0xFF606060));
    g.drawEllipse(rx + 1, ry + 1, rw - 2, rw - 2, 1.5f);

    // Inner groove
    g.setColour(juce::Colour(0xFF0A0A0A));
    g.drawEllipse(rx + radius * 0.15f, ry + radius * 0.15f,
                  rw - radius * 0.3f, rw - radius * 0.3f, 1.0f);

    // Pointer line (gold)
    juce::Path p;
    auto pointerLength = radius * 0.75f;
    auto pointerThickness = 3.5f;
    p.addRoundedRectangle(-pointerThickness * 0.5f, -radius + 6.0f,
                          pointerThickness, pointerLength - 10.0f, 1.5f);
    p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    // Pointer glow
    g.setColour(ampGold.withAlpha(0.3f));
    g.strokePath(p, juce::PathStrokeType(6.0f));

    g.setColour(ampGold);
    g.fillPath(p);

    // Center dot
    g.setColour(juce::Colour(0xFF3A3A3A));
    g.fillEllipse(centreX - 4.0f, centreY - 4.0f, 8.0f, 8.0f);
}

void NeuralAmpLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool)
{
    auto bounds = button.getLocalBounds().toFloat();
    auto isOn = button.getToggleState();

    // LED style button
    auto ledSize = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.4f;
    auto ledX = bounds.getCentreX() - ledSize * 0.5f;
    auto ledY = bounds.getY() + 4.0f;

    // LED glow
    if (isOn)
    {
        juce::ColourGradient glow(ledGreen.withAlpha(0.4f), ledX + ledSize * 0.5f, ledY + ledSize * 0.5f,
                                   ledGreen.withAlpha(0.0f), ledX + ledSize * 0.5f, ledY - ledSize, true);
        g.setGradientFill(glow);
        g.fillEllipse(ledX - ledSize * 0.5f, ledY - ledSize * 0.5f, ledSize * 2.0f, ledSize * 2.0f);
    }

    // LED bezel
    g.setColour(juce::Colour(0xFF2A2A2A));
    g.fillEllipse(ledX - 2, ledY - 2, ledSize + 4, ledSize + 4);

    // LED
    auto ledColour = isOn ? ledGreen : ledOff;
    if (shouldDrawButtonAsHighlighted)
        ledColour = ledColour.brighter(0.2f);

    juce::ColourGradient ledGradient(ledColour.brighter(0.3f), ledX, ledY,
                                      ledColour.darker(0.3f), ledX + ledSize, ledY + ledSize, false);
    g.setGradientFill(ledGradient);
    g.fillEllipse(ledX, ledY, ledSize, ledSize);

    // LED highlight
    g.setColour(juce::Colours::white.withAlpha(isOn ? 0.4f : 0.1f));
    g.fillEllipse(ledX + 2, ledY + 2, ledSize * 0.3f, ledSize * 0.3f);

    // Label
    g.setColour(ampCream);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText(button.getButtonText(), bounds.withTop(ledY + ledSize + 4),
               juce::Justification::centredTop);
}

void NeuralAmpLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& backgroundColour,
                                                 bool shouldDrawButtonAsHighlighted,
                                                 bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

    auto baseColour = backgroundColour;
    if (shouldDrawButtonAsDown)
        baseColour = baseColour.brighter(0.15f);
    else if (shouldDrawButtonAsHighlighted)
        baseColour = baseColour.brighter(0.08f);

    // Button shadow
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(bounds.translated(1, 1), 5.0f);

    // Button body gradient
    juce::ColourGradient gradient(baseColour.brighter(0.1f), 0, bounds.getY(),
                                   baseColour.darker(0.1f), 0, bounds.getBottom(), false);
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(bounds, 5.0f);

    // Top highlight
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawLine(bounds.getX() + 5, bounds.getY() + 1,
               bounds.getRight() - 5, bounds.getY() + 1, 1.0f);

    // Border
    g.setColour(ampGold.withAlpha(0.6f));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

//==============================================================================
// Editor
//==============================================================================
NeuralAmpAudioProcessorEditor::NeuralAmpAudioProcessorEditor(NeuralAmpAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Setup helper for sliders
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setPopupDisplayEnabled(true, true, this);
        addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(11.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, lookAndFeel.ampCream);
        addAndMakeVisible(label);
    };

    // Model section
    modelNameLabel.setText(audioProcessor.getModelName(), juce::dontSendNotification);
    modelNameLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    modelNameLabel.setColour(juce::Label::textColourId, lookAndFeel.ampGold);
    modelNameLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(modelNameLabel);

    loadModelButton.setButtonText("LOAD");
    loadModelButton.onClick = [this]() { loadModel(); };
    addAndMakeVisible(loadModelButton);

    // IR section
    irNameLabel.setText(audioProcessor.getIRName(), juce::dontSendNotification);
    irNameLabel.setFont(juce::Font(12.0f));
    irNameLabel.setColour(juce::Label::textColourId, lookAndFeel.ampCream);
    irNameLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(irNameLabel);

    loadIRButton.setButtonText("LOAD");
    loadIRButton.onClick = [this]() { loadIR(); };
    addAndMakeVisible(loadIRButton);

    // Input controls
    setupSlider(inputGainSlider, inputGainLabel, "INPUT");
    setupSlider(gateSlider, gateLabel, "GATE");

    // Tone stack
    setupSlider(bassSlider, bassLabel, "BASS");
    setupSlider(midSlider, midLabel, "MID");
    setupSlider(trebleSlider, trebleLabel, "TREBLE");

    // Filters
    setupSlider(lowCutSlider, lowCutLabel, "LOW CUT");
    setupSlider(highCutSlider, highCutLabel, "HIGH CUT");

    // Output
    setupSlider(outputSlider, outputLabel, "OUTPUT");

    // Toggle buttons
    gateButton.setButtonText("GATE");
    addAndMakeVisible(gateButton);

    cabButton.setButtonText("CAB");
    addAndMakeVisible(cabButton);

    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);

    // Meters
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);

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

    setSize(820, 450);
    startTimerHz(30);
}

NeuralAmpAudioProcessorEditor::~NeuralAmpAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void NeuralAmpAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Main background - amp tolex texture
    juce::ColourGradient bgGradient(juce::Colour(0xFF1E1814), 0, 0,
                                     juce::Colour(0xFF0E0C0A), 0, (float)getHeight(), false);
    g.setGradientFill(bgGradient);
    g.fillAll();

    // Amp face plate (brushed metal look)
    auto faceplate = getLocalBounds().reduced(15).toFloat();

    // Faceplate shadow
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(faceplate.translated(3, 3), 10.0f);

    // Faceplate gradient
    juce::ColourGradient plateGradient(juce::Colour(0xFF2A2420), faceplate.getX(), faceplate.getY(),
                                        juce::Colour(0xFF1A1612), faceplate.getX(), faceplate.getBottom(), false);
    g.setGradientFill(plateGradient);
    g.fillRoundedRectangle(faceplate, 10.0f);

    // Faceplate border
    g.setColour(juce::Colour(0xFF4A4440));
    g.drawRoundedRectangle(faceplate, 10.0f, 2.0f);

    // Inner bevel highlight
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawRoundedRectangle(faceplate.reduced(2), 8.0f, 1.0f);

    // Header area
    auto headerArea = faceplate.removeFromTop(60).reduced(10, 10);

    // Logo (clickable for supporters overlay)
    titleClickArea = juce::Rectangle<int>(25, 20, 250, 40);
    g.setColour(lookAndFeel.ampGold);
    g.setFont(juce::Font(32.0f, juce::Font::bold));
    g.drawText("NEURAL AMP", headerArea.removeFromLeft(250), juce::Justification::centredLeft);

    // Company name
    g.setFont(juce::Font(11.0f));
    g.setColour(lookAndFeel.ampCream.withAlpha(0.7f));
    g.drawText("Dusk Audio", getWidth() - 130, 25, 110, 20, juce::Justification::centredRight);

    // Model display area - wider with LOAD button inside
    auto modelArea = juce::Rectangle<int>(30, 55, 420, 35).toFloat();
    g.setColour(juce::Colour(0xFF0A0A0A));
    g.fillRoundedRectangle(modelArea, 5.0f);
    g.setColour(juce::Colour(0xFF3A3A3A));
    g.drawRoundedRectangle(modelArea, 5.0f, 1.0f);

    g.setColour(lookAndFeel.ampCream.withAlpha(0.5f));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("MODEL", modelArea.getX() + 10, modelArea.getY() + 3, 50, 12, juce::Justification::left);

    // Control panel areas
    int panelY = 100;
    int panelHeight = 225;

    // Draw section dividers
    auto drawPanel = [&](juce::Rectangle<int> bounds, const juce::String& title)
    {
        auto panelBounds = bounds.toFloat();

        // Panel shadow
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(panelBounds.translated(2, 2), 8.0f);

        // Panel background
        g.setColour(juce::Colour(0x35000000));
        g.fillRoundedRectangle(panelBounds, 8.0f);

        // Panel inner highlight
        g.setColour(juce::Colours::white.withAlpha(0.03f));
        g.drawRoundedRectangle(panelBounds.reduced(1), 7.0f, 1.0f);

        // Panel border
        g.setColour(lookAndFeel.ampGold.withAlpha(0.4f));
        g.drawRoundedRectangle(panelBounds, 8.0f, 1.0f);

        // Title with subtle glow
        g.setColour(lookAndFeel.ampGold.withAlpha(0.3f));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(title, panelBounds.getX(), panelBounds.getY() + 4,
                   panelBounds.getWidth(), 16, juce::Justification::centred);
        g.setColour(lookAndFeel.ampGold);
        g.drawText(title, panelBounds.getX(), panelBounds.getY() + 5,
                   panelBounds.getWidth(), 16, juce::Justification::centred);
    };

    // Input panel - includes meter on left side
    drawPanel(juce::Rectangle<int>(30, panelY, 150, panelHeight), "INPUT");

    // Tone stack panel
    drawPanel(juce::Rectangle<int>(190, panelY, 310, panelHeight), "TONE STACK");

    // Filters panel
    drawPanel(juce::Rectangle<int>(510, panelY, 160, panelHeight), "FILTERS");

    // Output panel - includes meter on right side
    drawPanel(juce::Rectangle<int>(680, panelY, 120, panelHeight), "OUTPUT");

    // Cabinet IR section - better positioned
    auto cabArea = juce::Rectangle<int>(30, 335, 450, 50).toFloat();

    // Shadow
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRoundedRectangle(cabArea.translated(2, 2), 6.0f);

    g.setColour(juce::Colour(0x35000000));
    g.fillRoundedRectangle(cabArea, 6.0f);
    g.setColour(lookAndFeel.ampGold.withAlpha(0.4f));
    g.drawRoundedRectangle(cabArea, 6.0f, 1.0f);

    g.setColour(lookAndFeel.ampGold);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("CABINET IR", cabArea.getX() + 75, cabArea.getY() + 8, 80, 12, juce::Justification::left);

    // Ventilation slots (decorative) - bottom right, properly aligned
    g.setColour(juce::Colour(0xFF080606));
    for (int i = 0; i < 8; ++i)
    {
        float slotX = getWidth() - 100.0f + i * 11.0f;
        g.fillRoundedRectangle(slotX, (float)getHeight() - 45.0f, 6.0f, 25.0f, 2.0f);
    }

    // Footer branding
    g.setColour(lookAndFeel.ampCream.withAlpha(0.25f));
    g.setFont(juce::Font(9.0f));
    g.drawText("Neural Amp Modeler", 30, getHeight() - 22, 150, 15, juce::Justification::left);
}

void NeuralAmpAudioProcessorEditor::resized()
{
    int knobSize = 70;
    int smallKnobSize = 58;
    int labelHeight = 16;
    int panelY = 105;

    // Model section - LOAD button inside black area
    modelNameLabel.setBounds(45, 68, 290, 20);
    loadModelButton.setBounds(370, 60, 70, 26);

    // Input section with integrated meter (panel is 150px wide at x=30)
    int inputPanelX = 30;
    int inputPanelW = 150;
    int meterWidth = 18;
    int meterHeight = 180;

    // Meter on left side of input panel
    inputMeter.setBounds(inputPanelX + 8, panelY + 25, meterWidth, meterHeight);

    // Knobs to the right of meter
    int inputKnobX = inputPanelX + meterWidth + 18;
    inputGainSlider.setBounds(inputKnobX, panelY + 25, knobSize, knobSize);
    inputGainLabel.setBounds(inputKnobX, panelY + 95, knobSize, labelHeight);

    gateSlider.setBounds(inputKnobX + 6, panelY + 112, smallKnobSize, smallKnobSize);
    gateLabel.setBounds(inputKnobX + 6, panelY + 170, smallKnobSize, labelHeight);

    gateButton.setBounds(inputKnobX + 11, panelY + 185, smallKnobSize - 10, 32);

    // Tone stack section - evenly spaced (panel is 310px wide at x=190)
    int tonePanelX = 190;
    int tonePanelW = 310;
    int toneSpacing = tonePanelW / 3;
    int toneY = panelY + 55;

    bassSlider.setBounds(tonePanelX + (toneSpacing - knobSize) / 2, toneY, knobSize, knobSize);
    bassLabel.setBounds(tonePanelX + (toneSpacing - knobSize) / 2, toneY + 70, knobSize, labelHeight);

    midSlider.setBounds(tonePanelX + toneSpacing + (toneSpacing - knobSize) / 2, toneY, knobSize, knobSize);
    midLabel.setBounds(tonePanelX + toneSpacing + (toneSpacing - knobSize) / 2, toneY + 70, knobSize, labelHeight);

    trebleSlider.setBounds(tonePanelX + toneSpacing * 2 + (toneSpacing - knobSize) / 2, toneY, knobSize, knobSize);
    trebleLabel.setBounds(tonePanelX + toneSpacing * 2 + (toneSpacing - knobSize) / 2, toneY + 70, knobSize, labelHeight);

    // Filters section (panel is 160px wide at x=510)
    int filterPanelX = 510;
    int filterPanelW = 160;
    int filterSpacing = filterPanelW / 2;

    lowCutSlider.setBounds(filterPanelX + (filterSpacing - smallKnobSize) / 2, toneY, smallKnobSize, smallKnobSize);
    lowCutLabel.setBounds(filterPanelX + (filterSpacing - smallKnobSize) / 2, toneY + 60, smallKnobSize, labelHeight);

    highCutSlider.setBounds(filterPanelX + filterSpacing + (filterSpacing - smallKnobSize) / 2, toneY, smallKnobSize, smallKnobSize);
    highCutLabel.setBounds(filterPanelX + filterSpacing + (filterSpacing - smallKnobSize) / 2, toneY + 60, smallKnobSize, labelHeight);

    // Output section with integrated meter (panel is 120px wide at x=680)
    int outputPanelX = 680;
    int outputPanelW = 120;

    // Knob on left side
    int outputKnobX = outputPanelX + 10;
    outputSlider.setBounds(outputKnobX, panelY + 28, knobSize, knobSize);
    outputLabel.setBounds(outputKnobX, panelY + 98, knobSize, labelHeight);

    bypassButton.setBounds(outputKnobX + 10, panelY + 145, 50, 55);

    // Meter on right side of output panel
    outputMeter.setBounds(outputPanelX + outputPanelW - meterWidth - 8, panelY + 25, meterWidth, meterHeight);

    // Cabinet IR section - properly aligned (panel at y=335, height=50)
    cabButton.setBounds(40, 345, 50, 35);
    irNameLabel.setBounds(165, 352, 220, 20);
    loadIRButton.setBounds(395, 347, 70, 26);

    // Supporters overlay
    if (supportersOverlay)
        supportersOverlay->setBounds(getLocalBounds());
}

void NeuralAmpAudioProcessorEditor::timerCallback()
{
    // Update meters with smoothing (processor returns gain, convert to dB)
    float newInputLevel = audioProcessor.getInputLevel();
    float newOutputLevel = audioProcessor.getOutputLevel();

    inputMeterLevel = inputMeterLevel * 0.85f + newInputLevel * 0.15f;
    outputMeterLevel = outputMeterLevel * 0.85f + newOutputLevel * 0.15f;

    // Convert gain to dB for the shared LEDMeter component
    float inputDb = juce::Decibels::gainToDecibels(inputMeterLevel, -60.0f);
    float outputDb = juce::Decibels::gainToDecibels(outputMeterLevel, -60.0f);

    inputMeter.setLevel(inputDb);
    outputMeter.setLevel(outputDb);

    // Update model/IR names
    modelNameLabel.setText(audioProcessor.getModelName(), juce::dontSendNotification);
    irNameLabel.setText(audioProcessor.getIRName(), juce::dontSendNotification);
}

void NeuralAmpAudioProcessorEditor::loadModel()
{
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

void NeuralAmpAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
        showSupportersPanel();
}

void NeuralAmpAudioProcessorEditor::showSupportersPanel()
{
    if (!supportersOverlay)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>("Neural Amp", JucePlugin_VersionString);
        supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
        addAndMakeVisible(supportersOverlay.get());
    }

    supportersOverlay->setBounds(getLocalBounds());
    supportersOverlay->toFront(true);
    supportersOverlay->setVisible(true);
}

void NeuralAmpAudioProcessorEditor::hideSupportersPanel()
{
    if (supportersOverlay)
        supportersOverlay->setVisible(false);
}

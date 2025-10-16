#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

//==============================================================================
// LevelMeter Implementation
HarmonicGeneratorAudioProcessorEditor::LevelMeter::LevelMeter()
    : levelL(0.0f), levelR(0.0f), smoothedLevelL(0.0f), smoothedLevelR(0.0f)
{
}

void HarmonicGeneratorAudioProcessorEditor::LevelMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRoundedRectangle(bounds, 2.0f);

    // Left channel meter
    float leftWidth = bounds.getWidth() * 0.45f;
    float meterHeight = smoothedLevelL * bounds.getHeight();

    g.setColour(juce::Colour(0xff4a9eff));
    g.fillRoundedRectangle(bounds.getX(), bounds.getBottom() - meterHeight,
                          leftWidth, meterHeight, 2.0f);

    // Right channel meter
    float rightX = bounds.getX() + bounds.getWidth() * 0.55f;
    meterHeight = smoothedLevelR * bounds.getHeight();

    g.setColour(juce::Colour(0xff4a9eff));
    g.fillRoundedRectangle(rightX, bounds.getBottom() - meterHeight,
                          leftWidth, meterHeight, 2.0f);

    // Border
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
}

void HarmonicGeneratorAudioProcessorEditor::LevelMeter::setStereoLevels(float left, float right)
{
    levelL = left;
    levelR = right;

    // Smooth the levels
    smoothedLevelL = smoothedLevelL * 0.7f + levelL * 0.3f;
    smoothedLevelR = smoothedLevelR * 0.7f + levelR * 0.3f;

    repaint();
}

//==============================================================================
// Editor Implementation
HarmonicGeneratorAudioProcessorEditor::HarmonicGeneratorAudioProcessorEditor(
    HarmonicGeneratorAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&customLookAndFeel);

    // Hardware Mode Selector (at the top!)
    hardwareModeLabel.setText("Hardware Mode", juce::dontSendNotification);
    hardwareModeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(hardwareModeLabel);

    addAndMakeVisible(hardwareModeSelector);

    // Populate and style the combo box with hardware modes
    hardwareModeSelector.addItem("Custom", 1);
    hardwareModeSelector.addItem("Studer A800", 2);
    hardwareModeSelector.addItem("Ampex ATR-102", 3);
    hardwareModeSelector.addItem("Tascam Porta", 4);
    hardwareModeSelector.addItem("Fairchild 670", 5);
    hardwareModeSelector.addItem("Pultec EQP-1A", 6);
    hardwareModeSelector.addItem("UA 610", 7);
    hardwareModeSelector.addItem("Neve 1073", 8);
    hardwareModeSelector.addItem("API 2500", 9);
    hardwareModeSelector.addItem("SSL 4000E", 10);
    hardwareModeSelector.addItem("Culture Vulture", 11);
    hardwareModeSelector.addItem("Decapitator", 12);
    hardwareModeSelector.addItem("HG-2 Black Box", 13);

    // Match Luna Co. styling
    hardwareModeSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    hardwareModeSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    hardwareModeSelector.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff808080));

    hardwareModeSelector.addListener(this);
    hardwareModeAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(
        *audioProcessor.apvts.getParameter("hardwareMode"), hardwareModeSelector);

    // Set initial selection to Custom (ID 1) if nothing is selected
    if (hardwareModeSelector.getSelectedId() == 0)
        hardwareModeSelector.setSelectedId(1, juce::dontSendNotification);

    // Main controls (always visible)
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label,
                              const juce::String& text, const juce::String& paramID)
    {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(label);

        return audioProcessor.apvts.getParameter(paramID);
    };

    // Drive, Output, Mix, Tone (main controls)
    auto* driveParam = setupSlider(driveSlider, driveLabel, "DRIVE", "drive");
    driveAttachment = std::make_unique<juce::SliderParameterAttachment>(*driveParam, driveSlider);

    auto* outputParam = setupSlider(outputGainSlider, outputGainLabel, "OUTPUT", "outputGain");
    outputGainAttachment = std::make_unique<juce::SliderParameterAttachment>(*outputParam, outputGainSlider);

    auto* mixParam = setupSlider(mixSlider, mixLabel, "MIX", "wetDryMix");
    mixAttachment = std::make_unique<juce::SliderParameterAttachment>(*mixParam, mixSlider);

    auto* toneParam = setupSlider(toneSlider, toneLabel, "TONE", "tone");
    toneAttachment = std::make_unique<juce::SliderParameterAttachment>(*toneParam, toneSlider);

    // Custom mode controls (hidden by default)
    auto* secondParam = setupSlider(secondHarmonicSlider, secondHarmonicLabel, "2nd", "secondHarmonic");
    secondHarmonicAttachment = std::make_unique<juce::SliderParameterAttachment>(*secondParam, secondHarmonicSlider);

    auto* thirdParam = setupSlider(thirdHarmonicSlider, thirdHarmonicLabel, "3rd", "thirdHarmonic");
    thirdHarmonicAttachment = std::make_unique<juce::SliderParameterAttachment>(*thirdParam, thirdHarmonicSlider);

    auto* fourthParam = setupSlider(fourthHarmonicSlider, fourthHarmonicLabel, "4th", "fourthHarmonic");
    fourthHarmonicAttachment = std::make_unique<juce::SliderParameterAttachment>(*fourthParam, fourthHarmonicSlider);

    auto* fifthParam = setupSlider(fifthHarmonicSlider, fifthHarmonicLabel, "5th", "fifthHarmonic");
    fifthHarmonicAttachment = std::make_unique<juce::SliderParameterAttachment>(*fifthParam, fifthHarmonicSlider);

    auto* evenParam = setupSlider(evenHarmonicsSlider, evenHarmonicsLabel, "Even", "evenHarmonics");
    evenHarmonicsAttachment = std::make_unique<juce::SliderParameterAttachment>(*evenParam, evenHarmonicsSlider);

    auto* oddParam = setupSlider(oddHarmonicsSlider, oddHarmonicsLabel, "Odd", "oddHarmonics");
    oddHarmonicsAttachment = std::make_unique<juce::SliderParameterAttachment>(*oddParam, oddHarmonicsSlider);

    auto* warmthParam = setupSlider(warmthSlider, warmthLabel, "Warmth", "warmth");
    warmthAttachment = std::make_unique<juce::SliderParameterAttachment>(*warmthParam, warmthSlider);

    auto* brightnessParam = setupSlider(brightnessSlider, brightnessLabel, "Bright", "brightness");
    brightnessAttachment = std::make_unique<juce::SliderParameterAttachment>(*brightnessParam, brightnessSlider);

    // Note: 2x Oversampling is now always enabled (no UI control needed)
    // This ensures alias-free harmonic generation at all times

    // Level meters
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);

    // Update visibility based on current mode
    updateControlsVisibility();

    // Start timer for level updates
    startTimer(30);

    setSize(700, 500);
}

HarmonicGeneratorAudioProcessorEditor::~HarmonicGeneratorAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void HarmonicGeneratorAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Unified Luna background
    g.fillAll(juce::Colour(0xff1a1a1a));  // Dark professional background

    auto bounds = getLocalBounds();

    // Draw header
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(0, 0, bounds.getWidth(), 55);

    // Plugin name
    g.setFont(juce::Font(juce::FontOptions(24.0f).withStyle("Bold")));
    g.setColour(juce::Colour(0xffe0e0e0));
    g.drawText("Hardware Saturation", 20, 10, 300, 30, juce::Justification::left);

    // Subtitle
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(juce::Colour(0xff909090));
    g.drawText("Analog Hardware Emulation", 20, 32, 250, 20, juce::Justification::left);

    // Current mode indicator (top right)
    if (hardwareModeSelector.getSelectedId() > 0)
    {
        juce::String modeName = hardwareModeSelector.getText();
        g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));

        // Background pill
        int textWidth = g.getCurrentFont().getStringWidth(modeName) + 20;
        g.setColour(juce::Colour(0xff3a3a3a));
        g.fillRoundedRectangle(bounds.getRight() - textWidth - 20, 12, textWidth, 26, 3);

        // Text
        g.setColour(juce::Colour(0xffe0e0e0));
        g.drawText(modeName, bounds.getRight() - textWidth - 20, 12, textWidth, 26,
                  juce::Justification::centred);
    }

    // Description text for hardware modes
    if (hardwareModeSelector.getSelectedId() > 1)
    {
        auto mode = static_cast<HardwareSaturation::Mode>(hardwareModeSelector.getSelectedId() - 2);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.setColour(juce::Colour(0xff707070));
        g.drawText(HardwareSaturation::getModeDescription(mode),
                  20, 100, getWidth() - 40, 30, juce::Justification::centred, true);
    }

    // Section labels
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.setColour(juce::Colour(0xff808080));
    g.drawText("MAIN CONTROLS", 20, 140, 150, 20, juce::Justification::left);

    if (hardwareModeSelector.getSelectedId() == 1)  // Custom mode
    {
        g.drawText("HARMONICS", 20, 300, 150, 20, juce::Justification::left);
        g.drawText("CHARACTER", 20, 400, 150, 20, juce::Justification::left);
    }
}

void HarmonicGeneratorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(55);  // Header area

    // Hardware mode selector (below header)
    auto modeArea = area.removeFromTop(50);
    modeArea.reduce(20, 10);
    hardwareModeLabel.setBounds(modeArea.removeFromLeft(120).removeFromTop(25));
    hardwareModeSelector.setBounds(modeArea.removeFromLeft(250).removeFromTop(25));

    area.removeFromTop(40);  // Description area

    // Main controls row (always visible)
    auto mainControlsArea = area.removeFromTop(120);
    mainControlsArea.reduce(20, 0);

    int controlWidth = mainControlsArea.getWidth() / 4;

    auto driveArea = mainControlsArea.removeFromLeft(controlWidth);
    driveLabel.setBounds(driveArea.removeFromTop(20));
    driveSlider.setBounds(driveArea.reduced(10, 0));

    auto outputArea = mainControlsArea.removeFromLeft(controlWidth);
    outputGainLabel.setBounds(outputArea.removeFromTop(20));
    outputGainSlider.setBounds(outputArea.reduced(10, 0));

    auto mixArea = mainControlsArea.removeFromLeft(controlWidth);
    mixLabel.setBounds(mixArea.removeFromTop(20));
    mixSlider.setBounds(mixArea.reduced(10, 0));

    auto toneArea = mainControlsArea.removeFromLeft(controlWidth);
    toneLabel.setBounds(toneArea.removeFromTop(20));
    toneSlider.setBounds(toneArea.reduced(10, 0));

    // Custom controls (only visible in Custom mode)
    if (hardwareModeSelector.getSelectedId() == 1)  // Custom mode
    {
        auto customArea = area.removeFromTop(200);
        customArea.reduce(20, 0);

        // Harmonics row
        auto harmonicsRow = customArea.removeFromTop(100);
        int harmWidth = harmonicsRow.getWidth() / 4;

        auto h2Area = harmonicsRow.removeFromLeft(harmWidth);
        secondHarmonicLabel.setBounds(h2Area.removeFromTop(20));
        secondHarmonicSlider.setBounds(h2Area.reduced(10, 0));

        auto h3Area = harmonicsRow.removeFromLeft(harmWidth);
        thirdHarmonicLabel.setBounds(h3Area.removeFromTop(20));
        thirdHarmonicSlider.setBounds(h3Area.reduced(10, 0));

        auto h4Area = harmonicsRow.removeFromLeft(harmWidth);
        fourthHarmonicLabel.setBounds(h4Area.removeFromTop(20));
        fourthHarmonicSlider.setBounds(h4Area.reduced(10, 0));

        auto h5Area = harmonicsRow.removeFromLeft(harmWidth);
        fifthHarmonicLabel.setBounds(h5Area.removeFromTop(20));
        fifthHarmonicSlider.setBounds(h5Area.reduced(10, 0));

        // Character controls row
        auto charRow = customArea.removeFromTop(100);
        int charWidth = charRow.getWidth() / 4;

        auto evenArea = charRow.removeFromLeft(charWidth);
        evenHarmonicsLabel.setBounds(evenArea.removeFromTop(20));
        evenHarmonicsSlider.setBounds(evenArea.reduced(10, 0));

        auto oddArea = charRow.removeFromLeft(charWidth);
        oddHarmonicsLabel.setBounds(oddArea.removeFromTop(20));
        oddHarmonicsSlider.setBounds(oddArea.reduced(10, 0));

        auto warmthArea = charRow.removeFromLeft(charWidth);
        warmthLabel.setBounds(warmthArea.removeFromTop(20));
        warmthSlider.setBounds(warmthArea.reduced(10, 0));

        auto brightArea = charRow.removeFromLeft(charWidth);
        brightnessLabel.setBounds(brightArea.removeFromTop(20));
        brightnessSlider.setBounds(brightArea.reduced(10, 0));
    }

    // Bottom area for meters (oversampling always enabled)
    area.removeFromTop(10);
    auto bottomArea = area.removeFromBottom(80);
    bottomArea.reduce(20, 10);

    auto meterArea = bottomArea.removeFromTop(50);
    inputMeter.setBounds(meterArea.removeFromLeft(30));
    meterArea.removeFromLeft(10);
    outputMeter.setBounds(meterArea.removeFromLeft(30));
}

void HarmonicGeneratorAudioProcessorEditor::timerCallback()
{
    // Update level meters
    inputMeter.setStereoLevels(audioProcessor.inputLevelL.load(),
                               audioProcessor.inputLevelR.load());
    outputMeter.setStereoLevels(audioProcessor.outputLevelL.load(),
                                audioProcessor.outputLevelR.load());
}

void HarmonicGeneratorAudioProcessorEditor::comboBoxChanged(juce::ComboBox* comboBox)
{
    if (comboBox == &hardwareModeSelector)
    {
        updateControlsVisibility();
        resized();
        repaint();
    }
}

void HarmonicGeneratorAudioProcessorEditor::updateControlsVisibility()
{
    // Show custom controls only in Custom mode (index 0/ID 1)
    bool showCustom = (hardwareModeSelector.getSelectedId() == 1);

    secondHarmonicSlider.setVisible(showCustom);
    secondHarmonicLabel.setVisible(showCustom);
    thirdHarmonicSlider.setVisible(showCustom);
    thirdHarmonicLabel.setVisible(showCustom);
    fourthHarmonicSlider.setVisible(showCustom);
    fourthHarmonicLabel.setVisible(showCustom);
    fifthHarmonicSlider.setVisible(showCustom);
    fifthHarmonicLabel.setVisible(showCustom);
    evenHarmonicsSlider.setVisible(showCustom);
    evenHarmonicsLabel.setVisible(showCustom);
    oddHarmonicsSlider.setVisible(showCustom);
    oddHarmonicsLabel.setVisible(showCustom);
    warmthSlider.setVisible(showCustom);
    warmthLabel.setVisible(showCustom);
    brightnessSlider.setVisible(showCustom);
    brightnessLabel.setVisible(showCustom);
}

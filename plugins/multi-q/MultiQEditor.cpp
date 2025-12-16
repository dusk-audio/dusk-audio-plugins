#include "MultiQEditor.h"

//==============================================================================
MultiQEditor::MultiQEditor(MultiQ& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Create graphic display
    graphicDisplay = std::make_unique<EQGraphicDisplay>(processor);
    addAndMakeVisible(graphicDisplay.get());
    graphicDisplay->onBandSelected = [this](int band) { onBandSelected(band); };

    // Create band enable buttons
    for (int i = 0; i < 8; ++i)
    {
        auto& btn = bandEnableButtons[static_cast<size_t>(i)];
        btn = std::make_unique<BandEnableButton>(i);
        addAndMakeVisible(btn.get());

        bandEnableAttachments[static_cast<size_t>(i)] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.parameters, ParamIDs::bandEnabled(i + 1), *btn);
    }

    // Selected band controls
    selectedBandLabel.setText("No Band Selected", juce::dontSendNotification);
    selectedBandLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    selectedBandLabel.setFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));
    addAndMakeVisible(selectedBandLabel);

    freqSlider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::TextBoxBelow);
    setupSlider(*freqSlider, " Hz");
    addAndMakeVisible(freqSlider.get());

    gainSlider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::TextBoxBelow);
    setupSlider(*gainSlider, " dB");
    addAndMakeVisible(gainSlider.get());

    qSlider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                              juce::Slider::TextBoxBelow);
    setupSlider(*qSlider, "");
    addAndMakeVisible(qSlider.get());

    slopeSelector = std::make_unique<juce::ComboBox>();
    slopeSelector->addItemList({"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"}, 1);
    addAndMakeVisible(slopeSelector.get());
    slopeSelector->setVisible(false);  // Only show for HPF/LPF

    setupLabel(freqLabel, "FREQ");
    setupLabel(gainLabel, "GAIN");
    setupLabel(qLabel, "Q");
    setupLabel(slopeLabel, "SLOPE");
    addAndMakeVisible(freqLabel);
    addAndMakeVisible(gainLabel);
    addAndMakeVisible(qLabel);
    addAndMakeVisible(slopeLabel);
    slopeLabel.setVisible(false);

    // Global controls
    masterGainSlider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                       juce::Slider::TextBoxBelow);
    setupSlider(*masterGainSlider, " dB");
    addAndMakeVisible(masterGainSlider.get());
    masterGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::masterGain, *masterGainSlider);

    setupLabel(masterGainLabel, "MASTER");
    addAndMakeVisible(masterGainLabel);

    bypassButton = std::make_unique<juce::ToggleButton>("BYPASS");
    addAndMakeVisible(bypassButton.get());
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::bypass, *bypassButton);

    hqButton = std::make_unique<juce::ToggleButton>("HQ");
    hqButton->setTooltip("Enable 2x oversampling for analog-matched response at high frequencies");
    addAndMakeVisible(hqButton.get());
    hqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::hqEnabled, *hqButton);

    processingModeSelector = std::make_unique<juce::ComboBox>();
    processingModeSelector->addItemList({"Stereo", "Left", "Right", "Mid", "Side"}, 1);
    addAndMakeVisible(processingModeSelector.get());
    processingModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::processingMode, *processingModeSelector);

    qCoupleModeSelector = std::make_unique<juce::ComboBox>();
    qCoupleModeSelector->addItemList({"Q-Couple: Off", "Proportional", "Light", "Medium", "Strong",
                                       "Asym Light", "Asym Medium", "Asym Strong"}, 1);
    qCoupleModeSelector->setTooltip("Automatic Q adjustment based on gain changes");
    addAndMakeVisible(qCoupleModeSelector.get());
    qCoupleModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::qCoupleMode, *qCoupleModeSelector);

    // Analyzer controls
    analyzerButton = std::make_unique<juce::ToggleButton>("Analyzer");
    addAndMakeVisible(analyzerButton.get());
    analyzerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::analyzerEnabled, *analyzerButton);

    analyzerPrePostButton = std::make_unique<juce::ToggleButton>("Pre");
    analyzerPrePostButton->setTooltip("Show spectrum before EQ (Pre) or after EQ (Post)");
    addAndMakeVisible(analyzerPrePostButton.get());
    analyzerPrePostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::analyzerPrePost, *analyzerPrePostButton);

    analyzerModeSelector = std::make_unique<juce::ComboBox>();
    analyzerModeSelector->addItemList({"Peak", "RMS"}, 1);
    addAndMakeVisible(analyzerModeSelector.get());
    analyzerModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::analyzerMode, *analyzerModeSelector);

    analyzerResolutionSelector = std::make_unique<juce::ComboBox>();
    analyzerResolutionSelector->addItemList({"Low", "Medium", "High"}, 1);
    addAndMakeVisible(analyzerResolutionSelector.get());
    analyzerResolutionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::analyzerResolution, *analyzerResolutionSelector);

    analyzerDecaySlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                          juce::Slider::TextBoxRight);
    analyzerDecaySlider->setTextValueSuffix(" dB/s");
    analyzerDecaySlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(analyzerDecaySlider.get());
    analyzerDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::analyzerDecay, *analyzerDecaySlider);

    displayScaleSelector = std::make_unique<juce::ComboBox>();
    displayScaleSelector->addItemList({"±12 dB", "±30 dB", "±60 dB", "Warped"}, 1);
    displayScaleSelector->onChange = [this]() {
        auto mode = static_cast<DisplayScaleMode>(displayScaleSelector->getSelectedItemIndex());
        graphicDisplay->setDisplayScaleMode(mode);
    };
    addAndMakeVisible(displayScaleSelector.get());
    displayScaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::displayScaleMode, *displayScaleSelector);
    
    // Sync initial display scale mode
    graphicDisplay->setDisplayScaleMode(
        static_cast<DisplayScaleMode>(displayScaleSelector->getSelectedItemIndex()));

    // Sync initial analyzer visibility
    auto* analyzerParam = processor.parameters.getRawParameterValue(ParamIDs::analyzerEnabled);
    if (analyzerParam)
        graphicDisplay->setAnalyzerVisible(analyzerParam->load() > 0.5f);

    // Meters
    inputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    addAndMakeVisible(inputMeter.get());

    outputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    addAndMakeVisible(outputMeter.get());

    // Supporters overlay
    supportersOverlay = std::make_unique<SupportersOverlay>("Multi-Q");
    supportersOverlay->setVisible(false);
    supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
    addChildComponent(supportersOverlay.get());

    // Add parameter listener
    processor.parameters.addParameterListener(ParamIDs::analyzerEnabled, this);

    // Set size constraints for resizable window
    setResizable(true, true);
    setResizeLimits(700, 450, 1600, 1000);

    // Set initial size
    setSize(900, 600);

    // Start timer for meter updates
    startTimerHz(30);
}

MultiQEditor::~MultiQEditor()
{
    stopTimer();
    processor.parameters.removeParameterListener(ParamIDs::analyzerEnabled, this);
    setLookAndFeel(nullptr);
}

//==============================================================================
void MultiQEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xFF1a1a1a));

    // Header area
    auto headerBounds = getLocalBounds().removeFromTop(50);
    g.setColour(juce::Colour(0xFF252525));
    g.fillRect(headerBounds);

    // Plugin title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(24.0f).withStyle("Bold")));
    titleClickArea = juce::Rectangle<int>(10, 10, 120, 30);
    g.drawText("Multi-Q", titleClickArea, juce::Justification::centredLeft);

    // Subtitle
    g.setColour(juce::Colour(0xFF888888));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("8-Band Parametric EQ", 10, 35, 150, 14, juce::Justification::centredLeft);

    // Luna Co. branding
    g.setColour(juce::Colour(0xFF666666));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("Luna Co. Audio", getWidth() - 100, 35, 90, 14, juce::Justification::centredRight);

    // Band buttons background
    auto bandButtonsArea = juce::Rectangle<int>(10, 50, getWidth() - 20, 38);
    g.setColour(juce::Colour(0xFF202020));
    g.fillRoundedRectangle(bandButtonsArea.toFloat(), 4.0f);

    // Control panel background
    auto controlPanelArea = juce::Rectangle<int>(10, getHeight() - 100, getWidth() - 20, 90);
    g.setColour(juce::Colour(0xFF202020));
    g.fillRoundedRectangle(controlPanelArea.toFloat(), 4.0f);

    // Separator lines
    g.setColour(juce::Colour(0xFF333333));
    g.drawHorizontalLine(50, 0, static_cast<float>(getWidth()));
}

void MultiQEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header controls (right side)
    auto headerBounds = bounds.removeFromTop(50);
    auto headerRight = headerBounds.removeFromRight(500).reduced(5);

    // Place header controls from right to left
    bypassButton->setBounds(headerRight.removeFromRight(70).reduced(2));
    hqButton->setBounds(headerRight.removeFromRight(50).reduced(2));
    processingModeSelector->setBounds(headerRight.removeFromRight(80).reduced(2));

    // Band enable buttons (icons inside buttons)
    auto bandButtonsArea = bounds.removeFromTop(38);
    int buttonWidth = 32;
    int buttonHeight = 30;
    int buttonSpacing = 6;
    int totalButtonsWidth = 8 * buttonWidth + 7 * buttonSpacing;
    int startX = (getWidth() - totalButtonsWidth) / 2;

    for (int i = 0; i < 8; ++i)
    {
        bandEnableButtons[static_cast<size_t>(i)]->setBounds(
            startX + i * (buttonWidth + buttonSpacing),
            bandButtonsArea.getY() + 4,
            buttonWidth, buttonHeight);
    }

    // Bottom control panel
    auto controlPanel = bounds.removeFromBottom(100);

    // Meters on sides
    auto meterWidth = 30;
    inputMeter->setBounds(controlPanel.removeFromLeft(meterWidth).reduced(5, 10));
    outputMeter->setBounds(controlPanel.removeFromRight(meterWidth).reduced(5, 10));

    // Selected band controls (left part of control panel)
    auto selectedBandArea = controlPanel.removeFromLeft(350);
    selectedBandLabel.setBounds(selectedBandArea.removeFromTop(25).reduced(10, 2));

    auto knobsArea = selectedBandArea;
    int knobWidth = 70;
    int labelHeight = 15;

    freqLabel.setBounds(knobsArea.getX() + 10, knobsArea.getY(), knobWidth, labelHeight);
    freqSlider->setBounds(knobsArea.getX() + 10, knobsArea.getY() + labelHeight, knobWidth, 55);

    gainLabel.setBounds(knobsArea.getX() + 90, knobsArea.getY(), knobWidth, labelHeight);
    gainSlider->setBounds(knobsArea.getX() + 90, knobsArea.getY() + labelHeight, knobWidth, 55);

    qLabel.setBounds(knobsArea.getX() + 170, knobsArea.getY(), knobWidth, labelHeight);
    qSlider->setBounds(knobsArea.getX() + 170, knobsArea.getY() + labelHeight, knobWidth, 55);

    slopeLabel.setBounds(knobsArea.getX() + 250, knobsArea.getY(), knobWidth, labelHeight);
    slopeSelector->setBounds(knobsArea.getX() + 250, knobsArea.getY() + labelHeight + 15, knobWidth + 20, 24);

    // Global controls (middle part)
    auto globalArea = controlPanel.removeFromLeft(150);
    masterGainLabel.setBounds(globalArea.getX() + 10, globalArea.getY(), 70, labelHeight);
    masterGainSlider->setBounds(globalArea.getX() + 10, globalArea.getY() + labelHeight, 70, 55);

    // Analyzer and options controls (right part)
    auto analyzerArea = controlPanel.reduced(5, 5);
    int ctrlY = analyzerArea.getY();
    int ctrlHeight = 24;
    int spacing = 4;
    int areaWidth = analyzerArea.getWidth();

    // Calculate column widths based on available space
    int col1Width = juce::jmin(160, areaWidth / 3);
    int col2Width = juce::jmin(120, areaWidth / 3);

    // Row 1: Q-Couple (wide dropdown) | Display Scale | Processing mode info area
    qCoupleModeSelector->setBounds(analyzerArea.getX(), ctrlY, col1Width, ctrlHeight);
    displayScaleSelector->setBounds(analyzerArea.getX() + col1Width + 5, ctrlY, col2Width, ctrlHeight);

    ctrlY += ctrlHeight + spacing;

    // Row 2: Analyzer toggle | Pre/Post | Mode selector
    analyzerButton->setBounds(analyzerArea.getX(), ctrlY, 80, ctrlHeight);
    analyzerPrePostButton->setBounds(analyzerArea.getX() + 85, ctrlY, 55, ctrlHeight);
    analyzerModeSelector->setBounds(analyzerArea.getX() + 145, ctrlY, 75, ctrlHeight);

    ctrlY += ctrlHeight + spacing;

    // Row 3: Resolution | Decay slider (wider)
    analyzerResolutionSelector->setBounds(analyzerArea.getX(), ctrlY, 90, ctrlHeight);
    int decaySliderX = analyzerArea.getX() + 95;
    int decaySliderWidth = juce::jmax(80, areaWidth - 95);  // Ensure minimum width and use remaining space
    analyzerDecaySlider->setBounds(decaySliderX, ctrlY, decaySliderWidth, ctrlHeight);
    // Graphic display (main area)
    auto displayBounds = bounds.reduced(10, 5);
    graphicDisplay->setBounds(displayBounds);

    // Supporters overlay
    supportersOverlay->setBounds(getLocalBounds());
}

void MultiQEditor::timerCallback()
{
    // Update meters
    float inL = processor.inputLevelL.load();
    float inR = processor.inputLevelR.load();
    float outL = processor.outputLevelL.load();
    float outR = processor.outputLevelR.load();

    inputMeter->setLevel((inL + inR) * 0.5f);
    outputMeter->setLevel((outL + outR) * 0.5f);

    // Update master gain for display overlay
    auto* masterParam = processor.parameters.getRawParameterValue(ParamIDs::masterGain);
    if (masterParam)
        graphicDisplay->setMasterGain(masterParam->load());
}

void MultiQEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == ParamIDs::analyzerEnabled)
    {
        const bool visible = newValue > 0.5f;
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<MultiQEditor>(this), visible]() {
            if (safeThis != nullptr && safeThis->graphicDisplay != nullptr)
                safeThis->graphicDisplay->setAnalyzerVisible(visible);
        });
    }
}

void MultiQEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
    {
        showSupportersPanel();
    }
}

//==============================================================================
void MultiQEditor::setupSlider(juce::Slider& slider, const juce::String& suffix)
{
    slider.setTextValueSuffix(suffix);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xFFCCCCCC));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF2a2a2a));
}

void MultiQEditor::setupLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colour(0xFF888888));
    label.setFont(juce::Font(juce::FontOptions(10.0f)));
    label.setJustificationType(juce::Justification::centred);
}

void MultiQEditor::updateSelectedBandControls()
{
    // Clear existing attachments
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    slopeAttachment.reset();

    if (selectedBand < 0 || selectedBand >= 8)
    {
        selectedBandLabel.setText("No Band Selected", juce::dontSendNotification);
        freqSlider->setEnabled(false);
        gainSlider->setEnabled(false);
        qSlider->setEnabled(false);
        slopeSelector->setVisible(false);
        slopeLabel.setVisible(false);
        return;
    }

    const auto& config = DefaultBandConfigs[static_cast<size_t>(selectedBand)];

    // Update label with band info
    juce::String bandName = "Band " + juce::String(selectedBand + 1) + ": " + config.name;
    selectedBandLabel.setText(bandName, juce::dontSendNotification);
    selectedBandLabel.setColour(juce::Label::textColourId, config.color);

    // Set knob colors
    freqSlider->setColour(juce::Slider::rotarySliderFillColourId, config.color);
    gainSlider->setColour(juce::Slider::rotarySliderFillColourId, config.color);
    qSlider->setColour(juce::Slider::rotarySliderFillColourId, config.color);

    // Enable controls and create attachments
    freqSlider->setEnabled(true);
    freqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandFreq(selectedBand + 1), *freqSlider);

    qSlider->setEnabled(true);
    qAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandQ(selectedBand + 1), *qSlider);

    // Gain is only for bands 2-7 (shelf and parametric)
    if (selectedBand >= 1 && selectedBand <= 6)
    {
        gainSlider->setEnabled(true);
        gainSlider->setVisible(true);
        gainLabel.setVisible(true);
        gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, ParamIDs::bandGain(selectedBand + 1), *gainSlider);
    }
    else
    {
        gainSlider->setEnabled(false);
        gainSlider->setVisible(false);
        gainLabel.setVisible(false);
    }

    // Slope is only for HPF (band 1) and LPF (band 8)
    if (selectedBand == 0 || selectedBand == 7)
    {
        slopeSelector->setVisible(true);
        slopeLabel.setVisible(true);
        slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, ParamIDs::bandSlope(selectedBand + 1), *slopeSelector);
    }
    else
    {
        slopeSelector->setVisible(false);
        slopeLabel.setVisible(false);
    }
}

void MultiQEditor::onBandSelected(int bandIndex)
{
    selectedBand = bandIndex;
    graphicDisplay->setSelectedBand(bandIndex);
    updateSelectedBandControls();
}

void MultiQEditor::showSupportersPanel()
{
    supportersOverlay->setVisible(true);
    supportersOverlay->toFront(true);
}

void MultiQEditor::hideSupportersPanel()
{
    supportersOverlay->setVisible(false);
}

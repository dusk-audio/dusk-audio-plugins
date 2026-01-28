#include "MultiQEditor.h"

//==============================================================================
MultiQEditor::MultiQEditor(MultiQ& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Create graphic display (Digital mode)
    graphicDisplay = std::make_unique<EQGraphicDisplay>(processor);
    addAndMakeVisible(graphicDisplay.get());
    graphicDisplay->onBandSelected = [this](int band) { onBandSelected(band); };

    // Create band strip component (Eventide SplitEQ-style for Digital mode)
    bandStrip = std::make_unique<BandStripComponent>(processor);
    bandStrip->onBandSelected = [this](int band) { onBandSelected(band); };
    addAndMakeVisible(bandStrip.get());

    // Create British mode curve display (4K-EQ style)
    britishCurveDisplay = std::make_unique<BritishEQCurveDisplay>(processor);
    britishCurveDisplay->setVisible(false);  // Hidden by default
    addAndMakeVisible(britishCurveDisplay.get());

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

    freqSlider = std::make_unique<LunaSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::TextBoxBelow);
    setupSlider(*freqSlider, "");
    // Custom frequency formatting: "10.07 kHz" or "250 Hz"
    freqSlider->textFromValueFunction = [](double value) {
        if (value >= 1000.0)
            return juce::String(value / 1000.0, 2) + " kHz";
        else if (value >= 100.0)
            return juce::String(static_cast<int>(value)) + " Hz";
        else
            return juce::String(value, 1) + " Hz";
    };
    addAndMakeVisible(freqSlider.get());

    gainSlider = std::make_unique<LunaSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::TextBoxBelow);
    setupSlider(*gainSlider, "");
    // Custom gain formatting: "+3.5 dB" or "-2.0 dB"
    gainSlider->textFromValueFunction = [](double value) {
        juce::String sign = value >= 0 ? "+" : "";
        return sign + juce::String(value, 1) + " dB";
    };
    addAndMakeVisible(gainSlider.get());

    qSlider = std::make_unique<LunaSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                              juce::Slider::TextBoxBelow);
    setupSlider(*qSlider, "");
    // Custom Q formatting: "0.71" (2 decimal places)
    qSlider->textFromValueFunction = [](double value) {
        return juce::String(value, 2);
    };
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
    masterGainSlider = std::make_unique<LunaSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                       juce::Slider::TextBoxBelow);
    setupSlider(*masterGainSlider, "");
    // Custom gain formatting for master (same as band gain)
    masterGainSlider->textFromValueFunction = [](double value) {
        juce::String sign = value >= 0 ? "+" : "";
        return sign + juce::String(value, 1) + " dB";
    };
    // Set a neutral white/gray color for master (global control, not band-specific)
    masterGainSlider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFFaabbcc));
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

    // EQ Type selector
    eqTypeSelector = std::make_unique<juce::ComboBox>();
    eqTypeSelector->addItemList({"Digital", "British", "Tube"}, 1);
    eqTypeSelector->setTooltip("EQ algorithm style");
    addAndMakeVisible(eqTypeSelector.get());
    eqTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::eqType, *eqTypeSelector);

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

    analyzerDecaySlider = std::make_unique<LunaSlider>(juce::Slider::LinearHorizontal,
                                                          juce::Slider::TextBoxRight);
    analyzerDecaySlider->setTextValueSuffix(" dB/s");
    analyzerDecaySlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(analyzerDecaySlider.get());
    analyzerDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::analyzerDecay, *analyzerDecaySlider);

    displayScaleSelector = std::make_unique<juce::ComboBox>();
    displayScaleSelector->addItemList({"±12 dB", "±24 dB", "±30 dB", "±60 dB", "Warped"}, 1);
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

    // Sync initial analyzer visibility for both Digital and British mode displays
    auto* analyzerParam = processor.parameters.getRawParameterValue(ParamIDs::analyzerEnabled);
    if (analyzerParam)
    {
        bool analyzerVisible = analyzerParam->load() > 0.5f;
        graphicDisplay->setAnalyzerVisible(analyzerVisible);
        britishCurveDisplay->setAnalyzerVisible(analyzerVisible);
    }

    // Meters with stereo mode enabled
    inputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    inputMeter->setStereoMode(true);  // Show L/R channels
    addAndMakeVisible(inputMeter.get());

    outputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    outputMeter->setStereoMode(true);  // Show L/R channels
    addAndMakeVisible(outputMeter.get());

    // Supporters overlay
    supportersOverlay = std::make_unique<SupportersOverlay>("Multi-Q", JucePlugin_VersionString);
    supportersOverlay->setVisible(false);
    supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
    addChildComponent(supportersOverlay.get());

    // Setup British mode controls
    setupBritishControls();

    // Setup Pultec/Tube mode controls
    setupPultecControls();

    // Setup British mode header controls (like 4K-EQ)
    britishCurveCollapseButton.setButtonText("Hide Graph");
    britishCurveCollapseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
    britishCurveCollapseButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa0a0a0));
    britishCurveCollapseButton.setTooltip("Show/Hide frequency response graph");
    britishCurveCollapseButton.onClick = [this]() {
        britishCurveCollapsed = !britishCurveCollapsed;
        britishCurveCollapseButton.setButtonText(britishCurveCollapsed ? "Show Graph" : "Hide Graph");

        // Toggle curve display visibility
        if (britishCurveDisplay)
            britishCurveDisplay->setVisible(!britishCurveCollapsed && isBritishMode);

        // Resize the window to match 4K-EQ behavior (smaller window when collapsed)
        int newHeight = britishCurveCollapsed ? 530 : 640;
        setSize(getWidth(), newHeight);
    };
    britishCurveCollapseButton.setVisible(false);
    addAndMakeVisible(britishCurveCollapseButton);

    britishBypassButton = std::make_unique<juce::ToggleButton>("BYPASS");
    britishBypassButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff404040));
    britishBypassButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    britishBypassButton->setClickingTogglesState(true);
    britishBypassButton->setTooltip("Bypass all EQ processing");
    britishBypassButton->setVisible(false);
    addAndMakeVisible(britishBypassButton.get());

    britishAutoGainButton = std::make_unique<juce::ToggleButton>("AUTO GAIN");
    britishAutoGainButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff404040));
    britishAutoGainButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    britishAutoGainButton->setClickingTogglesState(true);
    britishAutoGainButton->setTooltip("Auto Gain Compensation: Automatically adjusts output to maintain consistent loudness");
    britishAutoGainButton->setVisible(false);
    addAndMakeVisible(britishAutoGainButton.get());

    // Attach bypass button to the existing bypass parameter
    britishBypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::bypass, *britishBypassButton);

    // British mode header controls (A/B, Presets, Oversampling - like 4K-EQ)
    britishAbButton.setButtonText("A");
    britishAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));  // Green for A
    britishAbButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    britishAbButton.onClick = [this]() { toggleBritishAB(); };
    britishAbButton.setTooltip("A/B Comparison: Click to switch between two settings");
    britishAbButton.setVisible(false);
    addAndMakeVisible(britishAbButton);

    britishPresetSelector.addItem("Default", 1);
    britishPresetSelector.addItem("Warm Vocal", 2);
    britishPresetSelector.addItem("Bright Guitar", 3);
    britishPresetSelector.addItem("Punchy Drums", 4);
    britishPresetSelector.addItem("Full Bass", 5);
    britishPresetSelector.addItem("Air & Presence", 6);
    britishPresetSelector.addItem("Gentle Cut", 7);
    britishPresetSelector.addItem("Master Bus", 8);
    britishPresetSelector.setSelectedId(1);
    britishPresetSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    britishPresetSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    britishPresetSelector.setVisible(false);
    britishPresetSelector.onChange = [this]() { applyBritishPreset(britishPresetSelector.getSelectedId()); };
    addAndMakeVisible(britishPresetSelector);

    // Global oversampling selector (visible in all modes)
    oversamplingSelector.addItem("Oversample: Off", 1);
    oversamplingSelector.addItem("Oversample: 2x", 2);
    oversamplingSelector.setSelectedId(1);
    oversamplingSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    oversamplingSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    oversamplingSelector.setTooltip("Oversampling: Higher = better quality but more CPU");
    addAndMakeVisible(oversamplingSelector);

    // Bind to HQ parameter (Off = 0, 2x = 1)
    oversamplingSelector.onChange = [this]() {
        auto* param = processor.parameters.getParameter(ParamIDs::hqEnabled);
        if (param)
            param->setValueNotifyingHost(oversamplingSelector.getSelectedId() == 2 ? 1.0f : 0.0f);
    };

    // Set initial state from parameter
    if (processor.parameters.getRawParameterValue(ParamIDs::hqEnabled)->load() > 0.5f)
        oversamplingSelector.setSelectedId(2, juce::dontSendNotification);
    else
        oversamplingSelector.setSelectedId(1, juce::dontSendNotification);

    // Setup Tube mode header controls (A/B, HQ)
    tubeAbButton.setButtonText("A");
    tubeAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));  // Green for A
    tubeAbButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    tubeAbButton.onClick = [this]() { toggleAB(); };
    tubeAbButton.setTooltip("A/B Comparison: Click to switch between two settings");
    tubeAbButton.setVisible(false);
    addAndMakeVisible(tubeAbButton);

    tubeHqButton = std::make_unique<juce::ToggleButton>("HQ");
    tubeHqButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a5058));
    tubeHqButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    tubeHqButton->setTooltip("Enable 2x oversampling for high-quality processing");
    tubeHqButton->setVisible(false);
    addAndMakeVisible(tubeHqButton.get());
    tubeHqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::hqEnabled, *tubeHqButton);

    // Add parameter listeners
    processor.parameters.addParameterListener(ParamIDs::analyzerEnabled, this);
    processor.parameters.addParameterListener(ParamIDs::eqType, this);
    processor.parameters.addParameterListener(ParamIDs::britishMode, this);  // For Brown/Black badge update

    // Check initial EQ mode and update visibility
    auto* eqTypeParam = processor.parameters.getRawParameterValue(ParamIDs::eqType);
    if (eqTypeParam)
    {
        int eqTypeIndex = static_cast<int>(eqTypeParam->load());
        isBritishMode = (eqTypeIndex == 1);
        isPultecMode = (eqTypeIndex == 2);
    }
    updateEQModeVisibility();

    // Set size constraints for resizable window
    setResizable(true, true);
    setResizeLimits(800, 550, 1600, 1200);

    // Set initial size (fits all EQ modes within resize limits)
    setSize(950, 700);

    // Start timer for meter updates
    startTimerHz(30);
}

MultiQEditor::~MultiQEditor()
{
    stopTimer();
    processor.parameters.removeParameterListener(ParamIDs::analyzerEnabled, this);
    processor.parameters.removeParameterListener(ParamIDs::eqType, this);
    processor.parameters.removeParameterListener(ParamIDs::britishMode, this);
    setLookAndFeel(nullptr);
}

//==============================================================================
void MultiQEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xFF1a1a1a));

    auto bounds = getLocalBounds();

    if (isPultecMode)
    {
        // ===== TUBE MODE - DARK BLUE-GRAY STYLE =====

        // Dark blue-gray background
        g.setColour(juce::Colour(0xff31444b));
        g.fillAll();

        // ===== HEADER SECTION =====
        // Header with subtle gradient
        juce::ColourGradient headerGradient(
            juce::Colour(0xff3a5058), 0, 0,
            juce::Colour(0xff31444b), 0, 55, false);
        g.setGradientFill(headerGradient);
        g.fillRect(0, 0, bounds.getWidth(), 55);

        // Header bottom border
        g.setColour(juce::Colour(0xff4a6068));
        g.fillRect(0, 54, bounds.getWidth(), 1);

        // Plugin name (clickable - shows supporters panel)
        titleClickArea = juce::Rectangle<int>(105, 10, 200, 40);
        g.setFont(juce::Font(juce::FontOptions(24.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xffe0e0e0));
        g.drawText("Multi-Q", 105, 10, 200, 30, juce::Justification::left);

        // Subtitle
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.setColour(juce::Colour(0xff909090));
        g.drawText("Tube EQ", 105, 34, 100, 16, juce::Justification::left);

        // Luna Co. branding
        g.setColour(juce::Colour(0xff808080));
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        g.drawText("Luna Co. Audio", getWidth() - 105, 36, 90, 12, juce::Justification::centredRight);

        // Section labels for Low and High frequency bands - larger, more prominent
        // Calculate positions to center over respective knob pairs
        int mainX = 30;
        int rightPanelWidth = 100;
        int mainWidth = bounds.getWidth() - 60 - rightPanelWidth;
        int knobSize = 85;
        int knobSpacing = (mainWidth - 4 * knobSize) / 5;

        // LF knobs are at positions 1 and 2
        int knob1X = mainX + knobSpacing;
        int knob2X = mainX + 2 * knobSpacing + knobSize;
        int lfCenterX = (knob1X + knob2X + knobSize) / 2;  // Center between LF BOOST and LF ATTEN

        // HF knobs are at positions 3 and 4
        int knob3X = mainX + 3 * knobSpacing + 2 * knobSize;
        int knob4X = mainX + 4 * knobSpacing + 3 * knobSize;
        int hfCenterX = (knob3X + knob4X + knobSize) / 2;  // Center between HF BOOST and HF ATTEN

        g.setFont(juce::Font(juce::FontOptions(16.0f).withStyle("Bold")));

        // Draw with subtle shadow for depth
        g.setColour(juce::Colour(0x40000000));  // Shadow
        g.drawText("LOW FREQUENCY", lfCenterX - 99, 59, 200, 22, juce::Justification::centred);
        g.drawText("HIGH FREQUENCY", hfCenterX - 99, 59, 200, 22, juce::Justification::centred);

        g.setColour(juce::Colour(0xff70b0d0));  // Brighter blue accent
        g.drawText("LOW FREQUENCY", lfCenterX - 100, 58, 200, 22, juce::Justification::centred);
        g.drawText("HIGH FREQUENCY", hfCenterX - 100, 58, 200, 22, juce::Justification::centred);
    }
    else if (isBritishMode)
    {
        // ===== BRITISH MODE HEADER (4K-EQ style with gradient) =====
        // Draw header with subtle gradient like 4K-EQ
        juce::ColourGradient headerGradient(
            juce::Colour(0xff2d2d2d), 0, 0,
            juce::Colour(0xff252525), 0, 55, false);
        g.setGradientFill(headerGradient);
        g.fillRect(0, 0, bounds.getWidth(), 55);

        // Header bottom border
        g.setColour(juce::Colour(0xff3a3a3a));
        g.fillRect(0, 54, bounds.getWidth(), 1);

        // Plugin name (clickable - shows supporters panel) - 4K-EQ style positioning
        // Position after the Digital/British dropdown (which is at x=10, width=85)
        titleClickArea = juce::Rectangle<int>(105, 10, 200, 40);
        g.setFont(juce::Font(juce::FontOptions(24.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xffe0e0e0));
        g.drawText("Multi-Q", 105, 10, 200, 30, juce::Justification::left);

        // Subtitle with hint
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.setColour(juce::Colour(0xff909090));
        g.drawText("Console-Style Equalizer", 105, 32, 200, 20, juce::Justification::left);

        // Draw EQ Type badge (Brown/Black indicator) - positioned like 4K-EQ
        // Badge is to the left of the Brown/Black dropdown (which is at getWidth() - 110)
        auto* britishModeParam = processor.parameters.getRawParameterValue(ParamIDs::britishMode);
        bool isBlack = britishModeParam && britishModeParam->load() > 0.5f;

        g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        // Badge: 70px wide, ends 15px before dropdown (dropdown at getWidth()-110, so badge at getWidth()-110-15-70 = getWidth()-195)
        auto eqTypeRect = juce::Rectangle<float>(static_cast<float>(getWidth()) - 195.0f, 17.0f, 70.0f, 24.0f);

        // Draw button background with gradient
        juce::ColourGradient btnGradient(
            isBlack ? juce::Colour(0xff3a3a3a) : juce::Colour(0xff7a5a30),
            eqTypeRect.getX(), eqTypeRect.getY(),
            isBlack ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff5a4020),
            eqTypeRect.getX(), eqTypeRect.getBottom(), false);
        g.setGradientFill(btnGradient);
        g.fillRoundedRectangle(eqTypeRect, 4.0f);

        // Border
        g.setColour(isBlack ? juce::Colour(0xff505050) : juce::Colour(0xff9a7040));
        g.drawRoundedRectangle(eqTypeRect.reduced(0.5f), 4.0f, 1.0f);

        // Text
        g.setColour(juce::Colour(0xffe0e0e0));
        g.drawText(isBlack ? "BLACK" : "BROWN", eqTypeRect, juce::Justification::centred);
    }
    else
    {
        // ===== DIGITAL MODE HEADER =====
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
        g.drawText("Universal EQ", 10, 35, 150, 14, juce::Justification::centredLeft);

        // Luna Co. branding
        g.setColour(juce::Colour(0xFF666666));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("Luna Co. Audio", getWidth() - 100, 35, 90, 14, juce::Justification::centredRight);

        // Band buttons / toolbar background
        auto toolbarArea = juce::Rectangle<int>(10, 50, getWidth() - 20, 38);
        g.setColour(juce::Colour(0xFF202020));
        g.fillRoundedRectangle(toolbarArea.toFloat(), 4.0f);
    }

    if (isPultecMode)
    {
        // ===== PULTEC MODE PAINT - SECTION DIVIDERS =====
        // Calculate positions based on layout (must match layoutPultecControls)
        const int headerHeight = 55;
        const int labelHeight = 22;
        const int knobSize = 105;            // Must match layoutPultecControls
        const int smallKnobSize = 90;        // Row 3 knobs
        const int bottomMargin = 35;         // Must match layoutPultecControls
        const int rightPanelWidth = 125;

        int row1Height = knobSize + labelHeight;
        int row2Height = labelHeight + knobSize + 10;  // Frequency row with separators
        int row3Height = smallKnobSize + labelHeight;

        int totalContentHeight = row1Height + row2Height + row3Height;
        int availableHeight = getHeight() - headerHeight - bottomMargin;
        int extraSpace = availableHeight - totalContentHeight;
        int rowGap = extraSpace / 4;

        int row1Y = headerHeight + rowGap;
        int row2Y = row1Y + row1Height + rowGap;
        int row3Y = row2Y + row2Height + rowGap;

        int mainWidth = getWidth() - rightPanelWidth;
        int lineStartX = 40;
        int lineEndX = mainWidth - 30;

        // ===== SEPARATOR LINES FOR FREQUENCY ROW (Row 2) =====
        // Draw horizontal separator lines above and below the frequency row
        // to visually group it as a distinct section

        // Line ABOVE frequency row (after Row 1 labels)
        int separatorAboveY = row2Y - 8;
        g.setColour(juce::Colour(0x30ffffff));  // Subtle white
        g.drawLine(static_cast<float>(lineStartX), static_cast<float>(separatorAboveY),
                   static_cast<float>(lineEndX), static_cast<float>(separatorAboveY), 1.0f);

        // Line BELOW frequency row (before MID section)
        int separatorBelowY = row3Y - rowGap / 2;
        g.setColour(juce::Colour(0x30ffffff));  // Subtle white
        g.drawLine(static_cast<float>(lineStartX), static_cast<float>(separatorBelowY),
                   static_cast<float>(lineEndX), static_cast<float>(separatorBelowY), 1.0f);

        // Right panel vertical divider
        g.setColour(juce::Colour(0x40000000));
        g.fillRect(mainWidth - 5, headerHeight + 20, 1, getHeight() - headerHeight - 40);
        g.setColour(juce::Colour(0x30ffffff));
        g.fillRect(mainWidth - 4, headerHeight + 20, 1, getHeight() - headerHeight - 40);

        // "MID DIP/PEAK" section label - draw above the mid section
        g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xff70b0d0));  // Teal accent
        g.drawText("MID DIP/PEAK", 55, separatorBelowY + 8, 150, 16, juce::Justification::left);

        // ===== FOOTER BAR =====
        int footerHeight = 20;
        int footerY = getHeight() - footerHeight;
        g.setColour(juce::Colour(0xff1a2a30));  // Darker than background
        g.fillRect(0, footerY, getWidth(), footerHeight);

        // Footer text - plugin name
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.setColour(juce::Colour(0xff707070));  // Subtle gray
        g.drawText("Multi-Q | Vintage Tube EQ", 10, footerY, 200, footerHeight, juce::Justification::centredLeft);

        // Company name on right
        g.drawText("Luna Co. Audio", getWidth() - 120, footerY, 110, footerHeight, juce::Justification::centredRight);
    }
    else if (isBritishMode)
    {
        // ===== BRITISH MODE PAINT (4K-EQ style content area) =====
        // Draw section dividers and headers like 4K-EQ

        // Adjust content area based on curve visibility (like 4K-EQ)
        int contentTop = britishCurveCollapsed ? 65 : 170;  // Move up when curve is hidden
        int contentLeft = 45;
        int contentRight = getWidth() - 45;
        int contentWidth = contentRight - contentLeft;
        int numSections = 6;
        int sectionWidth = contentWidth / numSections;

        // Section boundaries
        int filtersEnd = contentLeft + sectionWidth;
        int lfEnd = filtersEnd + sectionWidth;
        int lmfEnd = lfEnd + sectionWidth;
        int hmfEnd = lmfEnd + sectionWidth;
        int hfEnd = hmfEnd + sectionWidth;

        // Draw section dividers (vertical lines)
        g.setColour(juce::Colour(0xFF3a3a3a));
        int dividerTop = contentTop;
        int dividerBottom = getHeight() - 20;

        g.fillRect(filtersEnd, dividerTop, 2, dividerBottom - dividerTop);
        g.fillRect(lfEnd, dividerTop, 2, dividerBottom - dividerTop);
        g.fillRect(lmfEnd, dividerTop, 2, dividerBottom - dividerTop);
        g.fillRect(hmfEnd, dividerTop, 2, dividerBottom - dividerTop);
        g.fillRect(hfEnd, dividerTop, 2, dividerBottom - dividerTop);

        // Draw section header backgrounds
        int labelY = contentTop + 5;
        int labelHeight = 22;

        g.setColour(juce::Colour(0xFF222222));
        g.fillRect(contentLeft, labelY - 2, sectionWidth, labelHeight);
        g.fillRect(filtersEnd + 2, labelY - 2, sectionWidth - 2, labelHeight);
        g.fillRect(lfEnd + 2, labelY - 2, sectionWidth - 2, labelHeight);
        g.fillRect(lmfEnd + 2, labelY - 2, sectionWidth - 2, labelHeight);
        g.fillRect(hmfEnd + 2, labelY - 2, sectionWidth - 2, labelHeight);
        g.fillRect(hfEnd + 2, labelY - 2, contentRight - hfEnd - 2, labelHeight);

        // Draw section header text (FILTERS, LF, LMF, HMF, HF, MASTER)
        g.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xffd0d0d0));
        g.drawText("FILTERS", contentLeft, labelY, sectionWidth, 20, juce::Justification::centred);
        g.drawText("LF", filtersEnd + 2, labelY, sectionWidth - 2, 20, juce::Justification::centred);
        g.drawText("LMF", lfEnd + 2, labelY, sectionWidth - 2, 20, juce::Justification::centred);
        g.drawText("HMF", lmfEnd + 2, labelY, sectionWidth - 2, 20, juce::Justification::centred);
        g.drawText("HF", hmfEnd + 2, labelY, sectionWidth - 2, 20, juce::Justification::centred);
        g.drawText("MASTER", hfEnd + 2, labelY, contentRight - hfEnd - 2, 20, juce::Justification::centred);

        // Draw meter labels (INPUT / OUTPUT) like 4K-EQ
        if (inputMeter)
        {
            // Get current levels for display
            float inL = processor.inputLevelL.load();
            float inR = processor.inputLevelR.load();
            float inputLevel = juce::jmax(inL, inR);
            LEDMeterStyle::drawMeterLabels(g, inputMeter->getBounds(), "INPUT", inputLevel);
        }

        if (outputMeter)
        {
            float outL = processor.outputLevelL.load();
            float outR = processor.outputLevelR.load();
            float outputLevel = juce::jmax(outL, outR);
            LEDMeterStyle::drawMeterLabels(g, outputMeter->getBounds(), "OUTPUT", outputLevel);
        }

        // Draw tick marks and value labels around knobs (SSL style)
        drawBritishKnobMarkings(g);

        // Knob labels are drawn in paintOverChildren() to ensure they appear on top
    }
    else
    {
        // ===== DIGITAL MODE PAINT (redesigned layout) =====
        // Constants matching resized() layout
        int controlBarHeight = 48;
        int bandStripHeight = 115;
        int toolbarHeight = 88;  // Header (50) + toolbar (38)
        int meterWidth = 28;
        int meterPadding = 8;

        // ===== BOTTOM CONTROL BAR BACKGROUND =====
        auto controlBarArea = juce::Rectangle<int>(
            0, getHeight() - controlBarHeight,
            getWidth(), controlBarHeight);

        // Dark control bar background with subtle gradient
        {
            juce::ColourGradient barGradient(
                juce::Colour(0xFF1e1e20), 0, static_cast<float>(controlBarArea.getY()),
                juce::Colour(0xFF161618), 0, static_cast<float>(controlBarArea.getBottom()),
                false);
            g.setGradientFill(barGradient);
            g.fillRect(controlBarArea);
        }

        // Subtle top border for control bar
        g.setColour(juce::Colour(0xFF2a2a2e));
        g.drawHorizontalLine(controlBarArea.getY(), 0, static_cast<float>(getWidth()));

        // ===== BOTTOM BAND STRIP BACKGROUND =====
        auto bandStripArea = juce::Rectangle<int>(
            meterWidth + meterPadding * 2 + 8,
            getHeight() - controlBarHeight - bandStripHeight - 5,
            getWidth() - (meterWidth + meterPadding * 2) * 2 - 16,
            bandStripHeight);
        // Band strip draws its own background

        // ===== METER AREAS =====
        int meterAreaWidth = meterWidth + meterPadding * 2;

        // Left meter area (input)
        auto leftMeterArea = juce::Rectangle<int>(
            0, toolbarHeight,
            meterAreaWidth, getHeight() - toolbarHeight - controlBarHeight - bandStripHeight - 10);

        // Right meter area (output)
        auto rightMeterArea = juce::Rectangle<int>(
            getWidth() - meterAreaWidth, toolbarHeight,
            meterAreaWidth, getHeight() - toolbarHeight - controlBarHeight - bandStripHeight - 10);

        // Draw meter backgrounds
        g.setColour(juce::Colour(0xFF161618));
        g.fillRect(leftMeterArea);
        g.fillRect(rightMeterArea);

        // ===== METER LABELS (above meters) =====
        g.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xFF808088));
        g.drawText("IN", leftMeterArea.getX(), toolbarHeight - 14, meterAreaWidth, 14, juce::Justification::centred);
        g.drawText("OUT", rightMeterArea.getX(), toolbarHeight - 14, meterAreaWidth, 14, juce::Justification::centred);

        // ===== CONTROL BAR LABELS =====
        int barY = controlBarArea.getY() + 6;

        // "ANALYZER" label in control bar (positioned before analyzer controls)
        // Based on resized(): OUTPUT (55) + knob (38) + spacing (20) + Q-Couple (115) + Scale (90) + spacing (20) = ~338
        int analyzerLabelX = 338;
        g.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xFF707078));
        g.drawText("ANALYZER", analyzerLabelX, barY + 6, 60, 14, juce::Justification::left);

        // Subtle vertical separator before analyzer section
        g.setColour(juce::Colour(0xFF2a2a2e));
        g.drawVerticalLine(analyzerLabelX - 10, static_cast<float>(barY + 2), static_cast<float>(barY + 34));
    }

    // Separator line only for digital mode
    if (!isBritishMode && !isPultecMode)
    {
        g.setColour(juce::Colour(0xFF333333));
        g.drawHorizontalLine(50, 0, static_cast<float>(getWidth()));
    }
}

void MultiQEditor::paintOverChildren(juce::Graphics& g)
{
    // Don't draw labels if supporters overlay is visible
    if (supportersOverlay && supportersOverlay->isVisible())
        return;

    // Draw British mode knob labels ON TOP of child components
    if (isBritishMode)
    {
        // Match 4K-EQ label style: 9pt bold, gray color
        g.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xffa0a0a0));

        // Helper to draw label below a slider
        auto drawLabelBelow = [&g](const juce::Slider* slider, const juce::String& text) {
            if (slider == nullptr || !slider->isVisible()) return;
            int labelWidth = 50;
            int labelHeight = 18;
            int yOffset = slider->getHeight() / 2 + 45;  // Match 4K-EQ positioning
            int x = slider->getX() + (slider->getWidth() - labelWidth) / 2;
            int y = slider->getY() + yOffset;
            g.drawText(text, x, y, labelWidth, labelHeight, juce::Justification::centred);
        };

        // FILTERS section
        drawLabelBelow(britishHpfFreqSlider.get(), "HPF");
        drawLabelBelow(britishLpfFreqSlider.get(), "LPF");
        drawLabelBelow(britishInputGainSlider.get(), "INPUT");

        // LF section
        drawLabelBelow(britishLfGainSlider.get(), "GAIN");
        drawLabelBelow(britishLfFreqSlider.get(), "FREQ");

        // LMF section
        drawLabelBelow(britishLmGainSlider.get(), "GAIN");
        drawLabelBelow(britishLmFreqSlider.get(), "FREQ");
        drawLabelBelow(britishLmQSlider.get(), "Q");

        // HMF section
        drawLabelBelow(britishHmGainSlider.get(), "GAIN");
        drawLabelBelow(britishHmFreqSlider.get(), "FREQ");
        drawLabelBelow(britishHmQSlider.get(), "Q");

        // HF section
        drawLabelBelow(britishHfGainSlider.get(), "GAIN");
        drawLabelBelow(britishHfFreqSlider.get(), "FREQ");

        // MASTER section
        drawLabelBelow(britishSaturationSlider.get(), "DRIVE");
        drawLabelBelow(britishOutputGainSlider.get(), "OUTPUT");
    }
}

void MultiQEditor::resized()
{
    auto bounds = getLocalBounds();

    if (isPultecMode)
    {
        // ===== VINTAGE PULTEC MODE LAYOUT =====
        layoutPultecControls();

        // Position EQ type selector in header (adjusted for chassis border)
        eqTypeSelector->setBounds(15, 14, 65, 28);

        // ===== TUBE MODE HEADER CONTROLS (right side) =====
        // Layout: [A/B button] [Oversampling] at right side of header
        int headerY = 14;
        int headerControlHeight = 28;
        int rightX = getWidth() - 15;

        // Oversampling selector (rightmost)
        oversamplingSelector.setBounds(rightX - 120, headerY, 120, headerControlHeight);
        rightX -= 125;

        // A/B button (left of oversampling)
        tubeAbButton.setBounds(rightX - 32, headerY, 32, headerControlHeight);

        // Hide the tube HQ button (replaced by global oversampling selector)
        if (tubeHqButton)
            tubeHqButton->setVisible(false);

        // Position meters along the sides (integrated into vintage aesthetic)
        // Meters run alongside the control area with recessed bezel look
        int meterY = 70;  // Start below header
        int meterWidth = 14;  // Narrow meters for vintage look
        int meterHeight = getHeight() - meterY - 25;
        juce::ignoreUnused(meterY, meterWidth, meterHeight);

        // Hide meters in Pultec mode (cleaner vintage look)
        // The vintage Pultec didn't have LED meters - we can keep level display via paint
        inputMeter->setVisible(false);
        outputMeter->setVisible(false);

        // Hide Digital mode toolbar controls in Pultec mode
        bypassButton->setVisible(false);
        hqButton->setVisible(false);
        processingModeSelector->setVisible(false);
    }
    else if (isBritishMode)
    {
        // ===== BRITISH MODE LAYOUT (4K-EQ style) =====
        // Header controls positioned like 4K-EQ (no toolbar row)

        int headerY = 15;
        int headerControlHeight = 28;
        int centerX = getWidth() / 2;

        // EQ Type selector (Digital/British) - positioned in header left area
        eqTypeSelector->setBounds(10, headerY, 85, headerControlHeight);

        // A/B button (left of center, like 4K-EQ)
        britishAbButton.setBounds(centerX - 250, headerY, 32, headerControlHeight);

        // Preset selector (center-left, like 4K-EQ)
        britishPresetSelector.setBounds(centerX - 210, headerY, 160, headerControlHeight);

        // Hide Graph button - positioned like 4K-EQ
        britishCurveCollapseButton.setBounds(centerX - 40, 17, 90, 24);

        // Oversampling selector (right of Hide Graph button)
        oversamplingSelector.setBounds(centerX + 60, headerY, 120, headerControlHeight);

        // Brown/Black mode selector - right area like 4K-EQ
        britishModeSelector->setBounds(getWidth() - 110, headerY, 95, headerControlHeight);

        // Hide Digital mode toolbar controls in British mode
        bypassButton->setVisible(false);
        hqButton->setVisible(false);
        processingModeSelector->setVisible(false);

        // Calculate curve display height based on collapsed state
        int curveHeight = britishCurveCollapsed ? 0 : 105;
        int curveY = 58;  // Just below header like 4K-EQ

        // Position British EQ curve display (like 4K-EQ)
        if (britishCurveDisplay && !britishCurveCollapsed)
        {
            int curveX = 35;
            int curveWidth = getWidth() - 70;
            britishCurveDisplay->setBounds(curveX, curveY, curveWidth, curveHeight);
        }

        // Adjust meter and content positions based on curve visibility
        int meterY = britishCurveCollapsed ? 80 : 185;  // Move up when curve is hidden
        int meterWidth = LEDMeterStyle::standardWidth;
        int meterHeight = getHeight() - meterY - LEDMeterStyle::valueHeight - LEDMeterStyle::labelSpacing - 10;
        inputMeter->setBounds(6, meterY, meterWidth, meterHeight);
        outputMeter->setBounds(getWidth() - meterWidth - 10, meterY, meterWidth, meterHeight);

        // Main content area (between meters) - adjusted based on curve visibility
        int contentLeft = 45;
        int contentRight = getWidth() - 45;
        int contentWidth = contentRight - contentLeft;
        int contentTop = britishCurveCollapsed ? 65 : 170;  // Adjust based on curve visibility

        // Section layout: FILTERS | LF | LMF | HMF | HF | MASTER
        int numSections = 6;
        int sectionWidth = contentWidth / numSections;

        // Calculate section boundaries
        int filtersStart = contentLeft;
        int filtersEnd = contentLeft + sectionWidth;
        int lfStart = filtersEnd;
        int lfEnd = lfStart + sectionWidth;
        int lmfStart = lfEnd;
        int lmfEnd = lmfStart + sectionWidth;
        int hmfStart = lmfEnd;
        int hmfEnd = hmfStart + sectionWidth;
        int hfStart = hmfEnd;
        int hfEnd = hfStart + sectionWidth;
        int masterStart = hfEnd;
        int masterEnd = contentRight;

        // Knob sizes and row spacing (larger knobs like 4K-EQ)
        int knobSize = 75;  // Larger knobs
        int knobRowHeight = 125;  // More space between rows
        int sectionLabelHeight = 30;
        int knobLabelHeight = 18;
        int knobLabelOffset = knobSize / 2 + 40;  // Position label below knob
        int labelY = contentTop + 5;
        int row1Y = contentTop + sectionLabelHeight + 25;
        int row2Y = row1Y + knobRowHeight;
        int row3Y = row2Y + knobRowHeight;
        int btnHeight = 25;

        // Helper to center a knob in a section
        auto centerKnobInSection = [&](juce::Slider& slider, int sectionStart, int sectionEnd, int y) {
            int sectionCenter = (sectionStart + sectionEnd) / 2;
            slider.setBounds(sectionCenter - knobSize / 2, y, knobSize, knobSize);
        };

        // Helper to position a label below a knob
        auto positionLabelBelowKnob = [&](juce::Label& label, const juce::Slider& slider) {
            int labelWidth = 50;
            label.setBounds(slider.getX() + (slider.getWidth() - labelWidth) / 2,
                           slider.getY() + knobLabelOffset,
                           labelWidth, knobLabelHeight);
        };

        // Helper to center a button in a section
        auto centerButtonInSection = [&](juce::ToggleButton& button, int sectionStart, int sectionEnd, int y, int width) {
            int sectionCenter = (sectionStart + sectionEnd) / 2;
            button.setBounds(sectionCenter - width / 2, y, width, btnHeight);
        };

        // ===== FILTERS SECTION =====
        britishFiltersLabel.setBounds(filtersStart, labelY, sectionWidth, 20);

        // HPF
        centerKnobInSection(*britishHpfFreqSlider, filtersStart, filtersEnd, row1Y);
        britishHpfEnableButton->setBounds(britishHpfFreqSlider->getRight() + 2,
                                           row1Y + (knobSize - btnHeight) / 2, 32, btnHeight);
        positionLabelBelowKnob(britishHpfKnobLabel, *britishHpfFreqSlider);

        // LPF
        centerKnobInSection(*britishLpfFreqSlider, filtersStart, filtersEnd, row2Y);
        britishLpfEnableButton->setBounds(britishLpfFreqSlider->getRight() + 2,
                                           row2Y + (knobSize - btnHeight) / 2, 32, btnHeight);
        positionLabelBelowKnob(britishLpfKnobLabel, *britishLpfFreqSlider);

        // Input gain
        centerKnobInSection(*britishInputGainSlider, filtersStart, filtersEnd, row3Y);
        positionLabelBelowKnob(britishInputKnobLabel, *britishInputGainSlider);

        // ===== LF SECTION =====
        britishLfLabel.setBounds(lfStart, labelY, sectionWidth, 20);
        centerKnobInSection(*britishLfGainSlider, lfStart, lfEnd, row1Y);
        positionLabelBelowKnob(britishLfGainKnobLabel, *britishLfGainSlider);
        centerKnobInSection(*britishLfFreqSlider, lfStart, lfEnd, row2Y);
        positionLabelBelowKnob(britishLfFreqKnobLabel, *britishLfFreqSlider);
        centerButtonInSection(*britishLfBellButton, lfStart, lfEnd, row3Y + 25, 60);

        // ===== LMF SECTION =====
        britishLmfLabel.setBounds(lmfStart, labelY, sectionWidth, 20);
        centerKnobInSection(*britishLmGainSlider, lmfStart, lmfEnd, row1Y);
        positionLabelBelowKnob(britishLmGainKnobLabel, *britishLmGainSlider);
        centerKnobInSection(*britishLmFreqSlider, lmfStart, lmfEnd, row2Y);
        positionLabelBelowKnob(britishLmFreqKnobLabel, *britishLmFreqSlider);
        centerKnobInSection(*britishLmQSlider, lmfStart, lmfEnd, row3Y);
        positionLabelBelowKnob(britishLmQKnobLabel, *britishLmQSlider);

        // ===== HMF SECTION =====
        britishHmfLabel.setBounds(hmfStart, labelY, sectionWidth, 20);
        centerKnobInSection(*britishHmGainSlider, hmfStart, hmfEnd, row1Y);
        positionLabelBelowKnob(britishHmGainKnobLabel, *britishHmGainSlider);
        centerKnobInSection(*britishHmFreqSlider, hmfStart, hmfEnd, row2Y);
        positionLabelBelowKnob(britishHmFreqKnobLabel, *britishHmFreqSlider);
        centerKnobInSection(*britishHmQSlider, hmfStart, hmfEnd, row3Y);
        positionLabelBelowKnob(britishHmQKnobLabel, *britishHmQSlider);

        // ===== HF SECTION =====
        britishHfLabel.setBounds(hfStart, labelY, sectionWidth, 20);
        centerKnobInSection(*britishHfGainSlider, hfStart, hfEnd, row1Y);
        positionLabelBelowKnob(britishHfGainKnobLabel, *britishHfGainSlider);
        centerKnobInSection(*britishHfFreqSlider, hfStart, hfEnd, row2Y);
        positionLabelBelowKnob(britishHfFreqKnobLabel, *britishHfFreqSlider);
        centerButtonInSection(*britishHfBellButton, hfStart, hfEnd, row3Y + 25, 60);

        // ===== MASTER SECTION =====
        britishMasterLabel.setBounds(masterStart, labelY, sectionWidth, 20);

        // BYPASS button (top of master section)
        centerButtonInSection(*britishBypassButton, masterStart, masterEnd, row1Y, 80);

        // AUTO GAIN button (below bypass)
        centerButtonInSection(*britishAutoGainButton, masterStart, masterEnd, row1Y + 40, 80);

        // Saturation/Drive (row 2)
        centerKnobInSection(*britishSaturationSlider, masterStart, masterEnd, row2Y);
        positionLabelBelowKnob(britishSatKnobLabel, *britishSaturationSlider);

        // Output gain (row 3)
        centerKnobInSection(*britishOutputGainSlider, masterStart, masterEnd, row3Y);
        positionLabelBelowKnob(britishOutputKnobLabel, *britishOutputGainSlider);
    }
    else
    {
        // ===== DIGITAL MODE LAYOUT (redesigned) =====
        // Layout:
        // +------------------------------------------------------------------------+
        // | Header: Multi-Q | [Digital v] | [band buttons] | Stereo | OVS | BYPASS |
        // +------------------------------------------------------------------------+
        // | IN |                                                              | OUT |
        // | [] |               Large EQ Graphic Display                       | [] |
        // | [] |                                                              | [] |
        // +------------------------------------------------------------------------+
        // | Band Strip (8 bands with freq/gain/Q)                                  |
        // +------------------------------------------------------------------------+
        // | OUTPUT [knob] | Q-Couple | Scale | ANALYZER [On] [Pre] [Mode] Decay    |
        // +------------------------------------------------------------------------+

        // Header (title area only)
        bounds.removeFromTop(50);

        // Toolbar row (EQ type, band buttons, and mode controls)
        auto toolbarArea = bounds.removeFromTop(38);
        int toolbarY = toolbarArea.getY() + 4;
        int controlHeight = 26;

        // EQ Type selector on the left
        eqTypeSelector->setBounds(15, toolbarY, 85, controlHeight);

        // Digital mode toolbar: Band enable buttons in the center
        int buttonWidth = 32;
        int buttonHeight = 30;
        int buttonSpacing = 10;
        int totalButtonsWidth = 8 * buttonWidth + 7 * buttonSpacing;
        int startX = (getWidth() - totalButtonsWidth) / 2;

        for (int i = 0; i < 8; ++i)
        {
            bandEnableButtons[static_cast<size_t>(i)]->setBounds(
                startX + i * (buttonWidth + buttonSpacing),
                toolbarY,
                buttonWidth, buttonHeight);
        }

        // Right side controls (Stereo, Oversampling, Bypass) on the toolbar
        int rightX = getWidth() - 15;
        bypassButton->setBounds(rightX - 70, toolbarY, 65, controlHeight);
        bypassButton->setVisible(true);
        rightX -= 75;
        oversamplingSelector.setBounds(rightX - 120, toolbarY, 120, controlHeight);
        rightX -= 125;
        processingModeSelector->setBounds(rightX - 75, toolbarY, 73, controlHeight);
        processingModeSelector->setVisible(true);
        hqButton->setVisible(false);

        // Hide old selected band controls (replaced by BandStripComponent)
        selectedBandLabel.setVisible(false);
        freqSlider->setVisible(false);
        gainSlider->setVisible(false);
        qSlider->setVisible(false);
        slopeSelector->setVisible(false);
        freqLabel.setVisible(false);
        gainLabel.setVisible(false);
        qLabel.setVisible(false);
        slopeLabel.setVisible(false);

        // ===== BOTTOM CONTROL BAR =====
        int controlBarHeight = 48;
        auto controlBarArea = bounds.removeFromBottom(controlBarHeight);

        // Control bar layout: OUTPUT | Q-Couple | Scale | ANALYZER section
        int barY = controlBarArea.getY() + 6;
        int barItemHeight = 24;
        int knobSize = 38;
        int spacing = 12;
        int barX = 15;

        // OUTPUT section: label + knob + value
        masterGainLabel.setText("OUTPUT", juce::dontSendNotification);
        masterGainLabel.setJustificationType(juce::Justification::centredLeft);
        masterGainLabel.setBounds(barX, barY + 4, 55, 16);
        masterGainLabel.setVisible(true);
        barX += 55;

        masterGainSlider->setBounds(barX, barY - 2, knobSize, knobSize);
        masterGainSlider->setVisible(true);
        barX += knobSize + spacing + 8;

        // Q-Couple dropdown
        qCoupleModeSelector->setBounds(barX, barY + 2, 110, barItemHeight - 2);
        barX += 115 + spacing;

        // Display Scale dropdown
        displayScaleSelector->setBounds(barX, barY + 2, 85, barItemHeight - 2);
        barX += 90 + spacing + 10;

        // ANALYZER section - compact horizontal layout
        // "ANALYZER" label drawn in paint() at this position
        barX += 65;  // Space for "ANALYZER" label

        int analyzerCtrlWidth = 50;
        int analyzerCtrlSpacing = 5;

        // Analyzer On/Off button
        analyzerButton->setBounds(barX, barY + 2, analyzerCtrlWidth, barItemHeight - 2);
        barX += analyzerCtrlWidth + analyzerCtrlSpacing;

        // Pre/Post button
        analyzerPrePostButton->setBounds(barX, barY + 2, 38, barItemHeight - 2);
        barX += 43 + analyzerCtrlSpacing;

        // Mode selector (Peak/RMS)
        analyzerModeSelector->setBounds(barX, barY + 2, 55, barItemHeight - 2);
        barX += 60 + analyzerCtrlSpacing;

        // Resolution selector (hidden in compact mode, or make it smaller)
        analyzerResolutionSelector->setBounds(barX, barY + 2, 50, barItemHeight - 2);
        barX += 55 + analyzerCtrlSpacing;

        // Decay slider
        analyzerDecaySlider->setBounds(barX, barY + 2, 80, barItemHeight - 2);

        // ===== BAND STRIP (above control bar) =====
        int bandStripHeight = 115;  // Increased from 95 for larger fonts
        auto bandStripArea = bounds.removeFromBottom(bandStripHeight).reduced(8, 5);
        bandStrip->setBounds(bandStripArea);
        bandStrip->setVisible(true);
        bandStrip->setSelectedBand(selectedBand);

        // ===== METERS ON SIDES =====
        int meterWidth = 28;  // Wider meters for better visibility
        int meterPadding = 8;

        // Input meter on left side
        auto leftMeterArea = bounds.removeFromLeft(meterWidth + meterPadding * 2);
        inputMeter->setBounds(leftMeterArea.getX() + meterPadding,
                              bounds.getY() + 5,
                              meterWidth,
                              bounds.getHeight() - 10);

        // Output meter on right side
        auto rightMeterArea = bounds.removeFromRight(meterWidth + meterPadding * 2);
        outputMeter->setBounds(rightMeterArea.getX() + meterPadding,
                               bounds.getY() + 5,
                               meterWidth,
                               bounds.getHeight() - 10);
    }

    // Graphic display (main area) - only in Digital mode
    if (!isBritishMode && !isPultecMode)
    {
        auto displayBounds = bounds.reduced(10, 5);
        graphicDisplay->setBounds(displayBounds);
    }

    // Supporters overlay
    supportersOverlay->setBounds(getLocalBounds());
}

void MultiQEditor::timerCallback()
{
    // Update meters with stereo levels
    float inL = processor.inputLevelL.load();
    float inR = processor.inputLevelR.load();
    float outL = processor.outputLevelL.load();
    float outR = processor.outputLevelR.load();

    inputMeter->setStereoLevels(inL, inR);
    outputMeter->setStereoLevels(outL, outR);

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
            if (safeThis != nullptr)
            {
                if (safeThis->graphicDisplay != nullptr)
                    safeThis->graphicDisplay->setAnalyzerVisible(visible);
                // Also sync British mode curve display analyzer
                if (safeThis->britishCurveDisplay != nullptr)
                    safeThis->britishCurveDisplay->setAnalyzerVisible(visible);
            }
        });
    }
    else if (parameterID == ParamIDs::eqType)
    {
        const int eqTypeIndex = static_cast<int>(newValue);
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<MultiQEditor>(this), eqTypeIndex]() {
            if (safeThis != nullptr)
            {
                // EQType: 0=Digital, 1=British, 2=Tube(Pultec)
                safeThis->isBritishMode = (eqTypeIndex == 1);
                safeThis->isPultecMode = (eqTypeIndex == 2);
                safeThis->updateEQModeVisibility();
                safeThis->resized();
                safeThis->repaint();
            }
        });
    }
    else if (parameterID == ParamIDs::britishMode)
    {
        // Brown/Black mode changed - repaint to update the badge
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<MultiQEditor>(this)]() {
            if (safeThis != nullptr)
                safeThis->repaint();
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
        selectedBandLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF888888));
        freqSlider->setEnabled(false);
        gainSlider->setEnabled(false);
        qSlider->setEnabled(false);
        slopeSelector->setVisible(false);
        slopeLabel.setVisible(false);
        repaint();  // Update the control panel tinting
        return;
    }

    const auto& config = DefaultBandConfigs[static_cast<size_t>(selectedBand)];

    // Update label with band info
    juce::String bandName = "Band " + juce::String(selectedBand + 1) + ": " + config.name;
    selectedBandLabel.setText(bandName, juce::dontSendNotification);
    selectedBandLabel.setColour(juce::Label::textColourId, config.color);

    // Set knob colors and update LookAndFeel
    freqSlider->setColour(juce::Slider::rotarySliderFillColourId, config.color);
    gainSlider->setColour(juce::Slider::rotarySliderFillColourId, config.color);
    qSlider->setColour(juce::Slider::rotarySliderFillColourId, config.color);

    // Update LookAndFeel with selected band color for consistent styling
    lookAndFeel.setSelectedBandColor(config.color);

    // Repaint to update the tinted control section background
    repaint();

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
    bandStrip->setSelectedBand(bandIndex);
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

//==============================================================================
// British Mode UI
//==============================================================================

void MultiQEditor::setupBritishControls()
{
    // Setup British mode sliders using FourKLookAndFeel (exact 4K-EQ style)
    // This helper sets up a knob exactly like 4K-EQ does
    auto setupBritishKnob = [this](std::unique_ptr<juce::Slider>& slider, const juce::String& name,
                                   bool centerDetented, juce::Colour color) {
        slider = std::make_unique<LunaSlider>();
        slider->setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider->setPopupDisplayEnabled(true, true, this);
        slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                     juce::MathConstants<float>::pi * 2.75f, true);
        slider->setScrollWheelEnabled(true);
        LunaSliderStyle::configureKnob(*slider);
        slider->setColour(juce::Slider::rotarySliderFillColourId, color);
        slider->setName(name);
        slider->setLookAndFeel(&fourKLookAndFeel);
        if (centerDetented)
            slider->setDoubleClickReturnValue(true, 0.0);
        slider->setVisible(false);
        addAndMakeVisible(slider.get());
    };

    // Helper to setup a British mode toggle button (4K-EQ style)
    auto setupBritishButton = [this](std::unique_ptr<juce::ToggleButton>& button, const juce::String& text) {
        button = std::make_unique<juce::ToggleButton>(text);
        button->setClickingTogglesState(true);
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff404040));
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffff3030));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
        button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));
        button->setLookAndFeel(&fourKLookAndFeel);
        button->setVisible(false);
        addAndMakeVisible(button.get());
    };

    // Helper to setup a knob label (4K-EQ style)
    auto setupKnobLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffa0a0a0));
        label.setInterceptsMouseClicks(false, false);
        label.setLookAndFeel(&fourKLookAndFeel);  // Use 4K-EQ style for consistent rendering
        label.setVisible(false);
        addAndMakeVisible(label);
    };

    // Color scheme (exact 4K-EQ colors)
    const juce::Colour gainColor(0xffdc3545);    // Red for gain
    const juce::Colour freqColor(0xff28a745);    // Green for frequency
    const juce::Colour qColor(0xff007bff);       // Blue for Q
    const juce::Colour filterColor(0xffb8860b);  // Brown/orange for filters
    const juce::Colour ioColor(0xff007bff);      // Blue for input/output
    const juce::Colour satColor(0xffff8c00);     // Orange for saturation

    // HPF/LPF
    setupBritishKnob(britishHpfFreqSlider, "hpf_freq", false, filterColor);
    setupBritishButton(britishHpfEnableButton, "IN");
    setupBritishKnob(britishLpfFreqSlider, "lpf_freq", false, filterColor);
    setupBritishButton(britishLpfEnableButton, "IN");

    // LF Band
    setupBritishKnob(britishLfGainSlider, "lf_gain", true, gainColor);
    setupBritishKnob(britishLfFreqSlider, "lf_freq", false, freqColor);
    setupBritishButton(britishLfBellButton, "BELL");

    // LM Band (orange/goldenrod like 4K-EQ LMF section)
    setupBritishKnob(britishLmGainSlider, "lmf_gain", true, juce::Colour(0xffff8c00));
    setupBritishKnob(britishLmFreqSlider, "lmf_freq", false, juce::Colour(0xffdaa520));
    setupBritishKnob(britishLmQSlider, "lmf_q", false, qColor);

    // HM Band (green/cyan like 4K-EQ HMF section)
    setupBritishKnob(britishHmGainSlider, "hmf_gain", true, juce::Colour(0xff28a745));
    setupBritishKnob(britishHmFreqSlider, "hmf_freq", false, juce::Colour(0xff20b2aa));
    setupBritishKnob(britishHmQSlider, "hmf_q", false, qColor);

    // HF Band (blue tones like 4K-EQ HF section)
    setupBritishKnob(britishHfGainSlider, "hf_gain", true, juce::Colour(0xff4169e1));
    setupBritishKnob(britishHfFreqSlider, "hf_freq", false, juce::Colour(0xff6495ed));
    setupBritishButton(britishHfBellButton, "BELL");

    // Global British controls
    britishModeSelector = std::make_unique<juce::ComboBox>();
    britishModeSelector->addItem("BROWN", 1);
    britishModeSelector->addItem("BLACK", 2);
    britishModeSelector->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    britishModeSelector->setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    britishModeSelector->setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff808080));
    britishModeSelector->setTooltip("Brown: E-Series (musical) | Black: G-Series (surgical)");
    britishModeSelector->setVisible(false);
    addAndMakeVisible(britishModeSelector.get());

    setupBritishKnob(britishSaturationSlider, "saturation", false, satColor);
    setupBritishKnob(britishInputGainSlider, "input_gain", true, ioColor);
    setupBritishKnob(britishOutputGainSlider, "output_gain", true, ioColor);

    // Section labels (4K-EQ style)
    auto setupSectionLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour(0xFFd0d0d0));
        label.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
        label.setJustificationType(juce::Justification::centred);
        label.setVisible(false);
        addAndMakeVisible(label);
    };

    setupSectionLabel(britishFiltersLabel, "FILTERS");
    setupSectionLabel(britishLfLabel, "LF");
    setupSectionLabel(britishLmfLabel, "LMF");
    setupSectionLabel(britishHmfLabel, "HMF");
    setupSectionLabel(britishHfLabel, "HF");
    setupSectionLabel(britishMasterLabel, "MASTER");

    // Knob labels (below each knob like 4K-EQ)
    setupKnobLabel(britishHpfKnobLabel, "HPF");
    setupKnobLabel(britishLpfKnobLabel, "LPF");
    setupKnobLabel(britishInputKnobLabel, "INPUT");
    setupKnobLabel(britishLfGainKnobLabel, "GAIN");
    setupKnobLabel(britishLfFreqKnobLabel, "FREQ");
    setupKnobLabel(britishLmGainKnobLabel, "GAIN");
    setupKnobLabel(britishLmFreqKnobLabel, "FREQ");
    setupKnobLabel(britishLmQKnobLabel, "Q");
    setupKnobLabel(britishHmGainKnobLabel, "GAIN");
    setupKnobLabel(britishHmFreqKnobLabel, "FREQ");
    setupKnobLabel(britishHmQKnobLabel, "Q");
    setupKnobLabel(britishHfGainKnobLabel, "GAIN");
    setupKnobLabel(britishHfFreqKnobLabel, "FREQ");
    setupKnobLabel(britishSatKnobLabel, "DRIVE");
    setupKnobLabel(britishOutputKnobLabel, "OUTPUT");

    // Create attachments
    britishHpfFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishHpfFreq, *britishHpfFreqSlider);
    britishHpfEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::britishHpfEnabled, *britishHpfEnableButton);
    britishLpfFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishLpfFreq, *britishLpfFreqSlider);
    britishLpfEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::britishLpfEnabled, *britishLpfEnableButton);

    britishLfGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishLfGain, *britishLfGainSlider);
    britishLfFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishLfFreq, *britishLfFreqSlider);
    britishLfBellAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::britishLfBell, *britishLfBellButton);

    britishLmGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishLmGain, *britishLmGainSlider);
    britishLmFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishLmFreq, *britishLmFreqSlider);
    britishLmQAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishLmQ, *britishLmQSlider);

    britishHmGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishHmGain, *britishHmGainSlider);
    britishHmFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishHmFreq, *britishHmFreqSlider);
    britishHmQAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishHmQ, *britishHmQSlider);

    britishHfGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishHfGain, *britishHfGainSlider);
    britishHfFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishHfFreq, *britishHfFreqSlider);
    britishHfBellAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::britishHfBell, *britishHfBellButton);

    britishModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::britishMode, *britishModeSelector);
    britishSaturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishSaturation, *britishSaturationSlider);
    britishInputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishInputGain, *britishInputGainSlider);
    britishOutputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishOutputGain, *britishOutputGainSlider);
}

void MultiQEditor::updateEQModeVisibility()
{
    // Determine if we're in Digital mode (neither British nor Pultec)
    bool isDigitalMode = !isBritishMode && !isPultecMode;

    // Digital mode controls (8-band parametric)
    for (auto& btn : bandEnableButtons)
        btn->setVisible(isDigitalMode);

    // Old selected band controls are replaced by BandStripComponent - always hidden in new layout
    selectedBandLabel.setVisible(false);
    freqSlider->setVisible(false);
    gainSlider->setVisible(false);
    qSlider->setVisible(false);
    freqLabel.setVisible(false);
    gainLabel.setVisible(false);
    qLabel.setVisible(false);

    // BandStripComponent (Eventide SplitEQ-style) - only in Digital mode
    bandStrip->setVisible(isDigitalMode);

    qCoupleModeSelector->setVisible(isDigitalMode);
    masterGainSlider->setVisible(isDigitalMode);
    masterGainLabel.setVisible(isDigitalMode);

    // Hide/show graphic displays based on mode
    graphicDisplay->setVisible(isDigitalMode);

    // British mode curve display (only visible if British mode and not collapsed)
    if (britishCurveDisplay)
        britishCurveDisplay->setVisible(isBritishMode && !britishCurveCollapsed);

    // Pultec mode curve display - always hidden in vintage layout
    if (pultecCurveDisplay)
        pultecCurveDisplay->setVisible(false);

    // Hide analyzer controls in British/Pultec modes
    analyzerButton->setVisible(isDigitalMode);
    analyzerPrePostButton->setVisible(isDigitalMode);
    analyzerModeSelector->setVisible(isDigitalMode);
    analyzerResolutionSelector->setVisible(isDigitalMode);
    analyzerDecaySlider->setVisible(isDigitalMode);
    displayScaleSelector->setVisible(isDigitalMode);

    // Meter visibility - hide in Pultec mode for clean vintage look
    inputMeter->setVisible(!isPultecMode);
    outputMeter->setVisible(!isPultecMode);

    // Also hide slope controls if switching away from Digital mode
    if (!isDigitalMode)
    {
        slopeSelector->setVisible(false);
        slopeLabel.setVisible(false);
    }

    // British mode controls
    britishHpfFreqSlider->setVisible(isBritishMode);
    britishHpfEnableButton->setVisible(isBritishMode);
    britishLpfFreqSlider->setVisible(isBritishMode);
    britishLpfEnableButton->setVisible(isBritishMode);

    britishLfGainSlider->setVisible(isBritishMode);
    britishLfFreqSlider->setVisible(isBritishMode);
    britishLfBellButton->setVisible(isBritishMode);

    britishLmGainSlider->setVisible(isBritishMode);
    britishLmFreqSlider->setVisible(isBritishMode);
    britishLmQSlider->setVisible(isBritishMode);

    britishHmGainSlider->setVisible(isBritishMode);
    britishHmFreqSlider->setVisible(isBritishMode);
    britishHmQSlider->setVisible(isBritishMode);

    britishHfGainSlider->setVisible(isBritishMode);
    britishHfFreqSlider->setVisible(isBritishMode);
    britishHfBellButton->setVisible(isBritishMode);

    britishModeSelector->setVisible(isBritishMode);
    britishSaturationSlider->setVisible(isBritishMode);
    britishInputGainSlider->setVisible(isBritishMode);
    britishOutputGainSlider->setVisible(isBritishMode);

    // British mode header/master controls
    britishBypassButton->setVisible(isBritishMode);
    britishAutoGainButton->setVisible(isBritishMode);

    // British mode header controls
    britishCurveCollapseButton.setVisible(isBritishMode);
    britishAbButton.setVisible(isBritishMode);
    britishPresetSelector.setVisible(isBritishMode);
    // oversamplingSelector is always visible (global control)

    // Section labels - we draw text in paint() so hide the Label components
    // (The old Labels aren't needed since we draw text directly in paint())
    britishFiltersLabel.setVisible(false);
    britishLfLabel.setVisible(false);
    britishLmfLabel.setVisible(false);
    britishHmfLabel.setVisible(false);
    britishHfLabel.setVisible(false);
    britishMasterLabel.setVisible(false);

    // British knob labels - now drawn directly in paint() for reliability
    // Hide the Label components to avoid double-rendering
    britishHpfKnobLabel.setVisible(false);
    britishLpfKnobLabel.setVisible(false);
    britishInputKnobLabel.setVisible(false);
    britishLfGainKnobLabel.setVisible(false);
    britishLfFreqKnobLabel.setVisible(false);
    britishLmGainKnobLabel.setVisible(false);
    britishLmFreqKnobLabel.setVisible(false);
    britishLmQKnobLabel.setVisible(false);
    britishHmGainKnobLabel.setVisible(false);
    britishHmFreqKnobLabel.setVisible(false);
    britishHmQKnobLabel.setVisible(false);
    britishHfGainKnobLabel.setVisible(false);
    britishHfFreqKnobLabel.setVisible(false);
    britishSatKnobLabel.setVisible(false);
    britishOutputKnobLabel.setVisible(false);

    // ============== PULTEC MODE CONTROLS ==============
    // Pultec knobs and selectors
    pultecLfBoostSlider->setVisible(isPultecMode);
    pultecLfFreqSelector->setVisible(isPultecMode);
    pultecLfAttenSlider->setVisible(isPultecMode);
    pultecHfBoostSlider->setVisible(isPultecMode);
    pultecHfBoostFreqSelector->setVisible(isPultecMode);
    pultecHfBandwidthSlider->setVisible(isPultecMode);
    pultecHfAttenSlider->setVisible(isPultecMode);
    pultecHfAttenFreqSelector->setVisible(isPultecMode);
    pultecInputGainSlider->setVisible(isPultecMode);
    pultecOutputGainSlider->setVisible(isPultecMode);
    pultecTubeDriveSlider->setVisible(isPultecMode);

    // Pultec section labels
    pultecLfLabel.setVisible(isPultecMode);
    pultecHfBoostLabel.setVisible(isPultecMode);
    pultecHfAttenLabel.setVisible(isPultecMode);
    pultecMasterLabel.setVisible(isPultecMode);

    // Pultec knob labels
    pultecLfBoostKnobLabel.setVisible(isPultecMode);
    pultecLfFreqKnobLabel.setVisible(isPultecMode);
    pultecLfAttenKnobLabel.setVisible(isPultecMode);
    pultecHfBoostKnobLabel.setVisible(isPultecMode);
    pultecHfBoostFreqKnobLabel.setVisible(isPultecMode);
    pultecHfBwKnobLabel.setVisible(isPultecMode);
    pultecHfAttenKnobLabel.setVisible(isPultecMode);
    pultecHfAttenFreqKnobLabel.setVisible(isPultecMode);
    pultecInputKnobLabel.setVisible(isPultecMode);
    pultecOutputKnobLabel.setVisible(isPultecMode);
    pultecTubeKnobLabel.setVisible(isPultecMode);

    // Pultec Mid Dip/Peak section controls
    if (pultecMidEnabledButton)
        pultecMidEnabledButton->setVisible(isPultecMode);
    if (pultecMidLowFreqSelector)
        pultecMidLowFreqSelector->setVisible(isPultecMode);
    if (pultecMidLowPeakSlider)
        pultecMidLowPeakSlider->setVisible(isPultecMode);
    if (pultecMidDipFreqSelector)
        pultecMidDipFreqSelector->setVisible(isPultecMode);
    if (pultecMidDipSlider)
        pultecMidDipSlider->setVisible(isPultecMode);
    if (pultecMidHighFreqSelector)
        pultecMidHighFreqSelector->setVisible(isPultecMode);
    if (pultecMidHighPeakSlider)
        pultecMidHighPeakSlider->setVisible(isPultecMode);

    // Pultec Mid section labels
    pultecMidLowFreqLabel.setVisible(isPultecMode);
    pultecMidLowPeakLabel.setVisible(isPultecMode);
    pultecMidDipFreqLabel.setVisible(isPultecMode);
    pultecMidDipLabel.setVisible(isPultecMode);
    pultecMidHighFreqLabel.setVisible(isPultecMode);
    pultecMidHighPeakLabel.setVisible(isPultecMode);

    // Tube mode header controls (A/B, HQ)
    tubeAbButton.setVisible(isPultecMode);
    if (tubeHqButton)
        tubeHqButton->setVisible(isPultecMode);
}

void MultiQEditor::layoutBritishControls()
{
    // Get the bounds for the control panel (bottom area)
    auto controlPanel = getLocalBounds();
    controlPanel.removeFromTop(50);  // Header
    controlPanel.removeFromTop(38);  // Toolbar
    controlPanel = controlPanel.removeFromBottom(100);

    // Remove meter areas
    controlPanel.removeFromLeft(30);   // Input meter
    controlPanel.removeFromRight(30);  // Output meter

    // Layout British controls in the control panel area
    // British mode has: FILTERS | LF | LMF | HMF | HF | MASTER sections
    int numSections = 6;
    int sectionWidth = controlPanel.getWidth() / numSections;
    int knobSize = 55;
    int knobY = controlPanel.getY() + 25;
    int labelY = controlPanel.getY() + 5;
    int labelHeight = 18;
    int btnHeight = 22;

    // Helper to center a control in a section
    auto centerInSection = [&](juce::Component& comp, int sectionIndex, int y, int width, int height) {
        int sectionStart = controlPanel.getX() + sectionIndex * sectionWidth;
        int sectionCenter = sectionStart + sectionWidth / 2;
        comp.setBounds(sectionCenter - width / 2, y, width, height);
    };

    // FILTERS section (index 0) - HPF and LPF stacked
    int filtersSectionX = controlPanel.getX();
    britishFiltersLabel.setBounds(filtersSectionX, labelY, sectionWidth, labelHeight);

    // HPF on top
    int hpfX = filtersSectionX + (sectionWidth - knobSize) / 2 - 20;
    britishHpfFreqSlider->setBounds(hpfX, knobY, knobSize, knobSize);
    britishHpfEnableButton->setBounds(hpfX + knobSize + 2, knobY + (knobSize - btnHeight) / 2, 35, btnHeight);

    // LF section (index 1)
    britishLfLabel.setBounds(controlPanel.getX() + sectionWidth, labelY, sectionWidth, labelHeight);
    centerInSection(*britishLfGainSlider, 1, knobY, knobSize, knobSize);

    // LMF section (index 2)
    britishLmfLabel.setBounds(controlPanel.getX() + 2 * sectionWidth, labelY, sectionWidth, labelHeight);
    centerInSection(*britishLmGainSlider, 2, knobY, knobSize, knobSize);

    // HMF section (index 3)
    britishHmfLabel.setBounds(controlPanel.getX() + 3 * sectionWidth, labelY, sectionWidth, labelHeight);
    centerInSection(*britishHmGainSlider, 3, knobY, knobSize, knobSize);

    // HF section (index 4)
    britishHfLabel.setBounds(controlPanel.getX() + 4 * sectionWidth, labelY, sectionWidth, labelHeight);
    centerInSection(*britishHfGainSlider, 4, knobY, knobSize, knobSize);

    // MASTER section (index 5) - Output and Saturation
    britishMasterLabel.setBounds(controlPanel.getX() + 5 * sectionWidth, labelY, sectionWidth, labelHeight);
    int masterSectionX = controlPanel.getX() + 5 * sectionWidth;
    int masterKnobX = masterSectionX + (sectionWidth - knobSize) / 2;
    britishOutputGainSlider->setBounds(masterKnobX, knobY, knobSize, knobSize);

    // Place Brown/Black selector and saturation in the toolbar area for British mode
    // These will be laid out in resized() alongside the EQ type selector
}

//==============================================================================
void MultiQEditor::applyBritishPreset(int presetId)
{
    // Validate presetId is within expected range (1-8)
    if (presetId < 1 || presetId > 8)
    {
        DBG("MultiQEditor::applyBritishPreset: Invalid presetId " + juce::String(presetId) + " (expected 1-8)");
        return;
    }

    // Helper to set parameter value with defensive checks
    auto setParam = [this](const juce::String& paramId, float value) {
        auto* param = processor.parameters.getParameter(paramId);
        if (param == nullptr)
        {
            DBG("MultiQEditor::applyBritishPreset: Parameter '" + paramId + "' not found");
            return;
        }
        // Clamp value to parameter's valid range before converting
        auto range = param->getNormalisableRange();
        float clampedValue = juce::jlimit(range.start, range.end, value);
        param->setValueNotifyingHost(param->convertTo0to1(clampedValue));
    };

    // Preset definitions: HPF freq, HPF on, LPF freq, LPF on,
    //                     LF gain, LF freq, LF bell,
    //                     LMF gain, LMF freq, LMF Q,
    //                     HMF gain, HMF freq, HMF Q,
    //                     HF gain, HF freq, HF bell,
    //                     Saturation, Input, Output

    switch (presetId)
    {
        case 1:  // Default - flat response
            setParam(ParamIDs::britishHpfFreq, 20.0f);
            setParam(ParamIDs::britishHpfEnabled, 0.0f);
            setParam(ParamIDs::britishLpfFreq, 20000.0f);
            setParam(ParamIDs::britishLpfEnabled, 0.0f);
            setParam(ParamIDs::britishLfGain, 0.0f);
            setParam(ParamIDs::britishLfFreq, 100.0f);
            setParam(ParamIDs::britishLfBell, 0.0f);
            setParam(ParamIDs::britishLmGain, 0.0f);
            setParam(ParamIDs::britishLmFreq, 400.0f);
            setParam(ParamIDs::britishLmQ, 1.0f);
            setParam(ParamIDs::britishHmGain, 0.0f);
            setParam(ParamIDs::britishHmFreq, 2000.0f);
            setParam(ParamIDs::britishHmQ, 1.0f);
            setParam(ParamIDs::britishHfGain, 0.0f);
            setParam(ParamIDs::britishHfFreq, 8000.0f);
            setParam(ParamIDs::britishHfBell, 0.0f);
            setParam(ParamIDs::britishSaturation, 0.0f);
            setParam(ParamIDs::britishInputGain, 0.0f);
            setParam(ParamIDs::britishOutputGain, 0.0f);
            break;

        case 2:  // Warm Vocal - presence boost, slight low cut
            setParam(ParamIDs::britishHpfFreq, 80.0f);
            setParam(ParamIDs::britishHpfEnabled, 1.0f);
            setParam(ParamIDs::britishLpfFreq, 16000.0f);
            setParam(ParamIDs::britishLpfEnabled, 1.0f);
            setParam(ParamIDs::britishLfGain, -2.0f);
            setParam(ParamIDs::britishLfFreq, 200.0f);
            setParam(ParamIDs::britishLfBell, 1.0f);
            setParam(ParamIDs::britishLmGain, 2.0f);
            setParam(ParamIDs::britishLmFreq, 800.0f);
            setParam(ParamIDs::britishLmQ, 1.5f);
            setParam(ParamIDs::britishHmGain, 3.0f);
            setParam(ParamIDs::britishHmFreq, 3500.0f);
            setParam(ParamIDs::britishHmQ, 1.2f);
            setParam(ParamIDs::britishHfGain, 2.0f);
            setParam(ParamIDs::britishHfFreq, 12000.0f);
            setParam(ParamIDs::britishHfBell, 0.0f);
            setParam(ParamIDs::britishSaturation, 15.0f);
            setParam(ParamIDs::britishInputGain, 0.0f);
            setParam(ParamIDs::britishOutputGain, 0.0f);
            break;

        case 3:  // Bright Guitar - aggressive highs, tight low end
            setParam(ParamIDs::britishHpfFreq, 100.0f);
            setParam(ParamIDs::britishHpfEnabled, 1.0f);
            setParam(ParamIDs::britishLpfFreq, 20000.0f);
            setParam(ParamIDs::britishLpfEnabled, 0.0f);
            setParam(ParamIDs::britishLfGain, -3.0f);
            setParam(ParamIDs::britishLfFreq, 150.0f);
            setParam(ParamIDs::britishLfBell, 1.0f);
            setParam(ParamIDs::britishLmGain, -2.0f);
            setParam(ParamIDs::britishLmFreq, 500.0f);
            setParam(ParamIDs::britishLmQ, 2.0f);
            setParam(ParamIDs::britishHmGain, 4.0f);
            setParam(ParamIDs::britishHmFreq, 3000.0f);
            setParam(ParamIDs::britishHmQ, 1.5f);
            setParam(ParamIDs::britishHfGain, 5.0f);
            setParam(ParamIDs::britishHfFreq, 10000.0f);
            setParam(ParamIDs::britishHfBell, 0.0f);
            setParam(ParamIDs::britishSaturation, 20.0f);
            setParam(ParamIDs::britishInputGain, 0.0f);
            setParam(ParamIDs::britishOutputGain, 0.0f);
            break;

        case 4:  // Punchy Drums - enhanced attack, controlled lows
            setParam(ParamIDs::britishHpfFreq, 60.0f);
            setParam(ParamIDs::britishHpfEnabled, 1.0f);
            setParam(ParamIDs::britishLpfFreq, 18000.0f);
            setParam(ParamIDs::britishLpfEnabled, 1.0f);
            setParam(ParamIDs::britishLfGain, 3.0f);
            setParam(ParamIDs::britishLfFreq, 80.0f);
            setParam(ParamIDs::britishLfBell, 0.0f);
            setParam(ParamIDs::britishLmGain, -4.0f);
            setParam(ParamIDs::britishLmFreq, 350.0f);
            setParam(ParamIDs::britishLmQ, 1.8f);
            setParam(ParamIDs::britishHmGain, 4.0f);
            setParam(ParamIDs::britishHmFreq, 4000.0f);
            setParam(ParamIDs::britishHmQ, 1.2f);
            setParam(ParamIDs::britishHfGain, 2.0f);
            setParam(ParamIDs::britishHfFreq, 8000.0f);
            setParam(ParamIDs::britishHfBell, 0.0f);
            setParam(ParamIDs::britishSaturation, 25.0f);
            setParam(ParamIDs::britishInputGain, 0.0f);
            setParam(ParamIDs::britishOutputGain, 0.0f);
            break;

        case 5:  // Full Bass - big low end, clarity on top
            setParam(ParamIDs::britishHpfFreq, 30.0f);
            setParam(ParamIDs::britishHpfEnabled, 1.0f);
            setParam(ParamIDs::britishLpfFreq, 12000.0f);
            setParam(ParamIDs::britishLpfEnabled, 1.0f);
            setParam(ParamIDs::britishLfGain, 6.0f);
            setParam(ParamIDs::britishLfFreq, 80.0f);
            setParam(ParamIDs::britishLfBell, 0.0f);
            setParam(ParamIDs::britishLmGain, -3.0f);
            setParam(ParamIDs::britishLmFreq, 250.0f);
            setParam(ParamIDs::britishLmQ, 1.5f);
            setParam(ParamIDs::britishHmGain, 2.0f);
            setParam(ParamIDs::britishHmFreq, 1500.0f);
            setParam(ParamIDs::britishHmQ, 1.0f);
            setParam(ParamIDs::britishHfGain, -2.0f);
            setParam(ParamIDs::britishHfFreq, 6000.0f);
            setParam(ParamIDs::britishHfBell, 0.0f);
            setParam(ParamIDs::britishSaturation, 30.0f);
            setParam(ParamIDs::britishInputGain, 0.0f);
            setParam(ParamIDs::britishOutputGain, -3.0f);
            break;

        case 6:  // Air & Presence - sparkle and definition
            setParam(ParamIDs::britishHpfFreq, 40.0f);
            setParam(ParamIDs::britishHpfEnabled, 1.0f);
            setParam(ParamIDs::britishLpfFreq, 20000.0f);
            setParam(ParamIDs::britishLpfEnabled, 0.0f);
            setParam(ParamIDs::britishLfGain, 0.0f);
            setParam(ParamIDs::britishLfFreq, 100.0f);
            setParam(ParamIDs::britishLfBell, 0.0f);
            setParam(ParamIDs::britishLmGain, -2.0f);
            setParam(ParamIDs::britishLmFreq, 600.0f);
            setParam(ParamIDs::britishLmQ, 1.2f);
            setParam(ParamIDs::britishHmGain, 3.0f);
            setParam(ParamIDs::britishHmFreq, 5000.0f);
            setParam(ParamIDs::britishHmQ, 1.0f);
            setParam(ParamIDs::britishHfGain, 5.0f);
            setParam(ParamIDs::britishHfFreq, 12000.0f);
            setParam(ParamIDs::britishHfBell, 0.0f);
            setParam(ParamIDs::britishSaturation, 10.0f);
            setParam(ParamIDs::britishInputGain, 0.0f);
            setParam(ParamIDs::britishOutputGain, 0.0f);
            break;

        case 7:  // Gentle Cut - subtle mud/harsh removal
            setParam(ParamIDs::britishHpfFreq, 50.0f);
            setParam(ParamIDs::britishHpfEnabled, 1.0f);
            setParam(ParamIDs::britishLpfFreq, 18000.0f);
            setParam(ParamIDs::britishLpfEnabled, 1.0f);
            setParam(ParamIDs::britishLfGain, -1.5f);
            setParam(ParamIDs::britishLfFreq, 200.0f);
            setParam(ParamIDs::britishLfBell, 1.0f);
            setParam(ParamIDs::britishLmGain, -2.5f);
            setParam(ParamIDs::britishLmFreq, 400.0f);
            setParam(ParamIDs::britishLmQ, 1.5f);
            setParam(ParamIDs::britishHmGain, -2.0f);
            setParam(ParamIDs::britishHmFreq, 2500.0f);
            setParam(ParamIDs::britishHmQ, 1.2f);
            setParam(ParamIDs::britishHfGain, -1.0f);
            setParam(ParamIDs::britishHfFreq, 8000.0f);
            setParam(ParamIDs::britishHfBell, 0.0f);
            setParam(ParamIDs::britishSaturation, 5.0f);
            setParam(ParamIDs::britishInputGain, 0.0f);
            setParam(ParamIDs::britishOutputGain, 1.0f);
            break;

        case 8:  // Master Bus - gentle glue and sheen
            setParam(ParamIDs::britishHpfFreq, 25.0f);
            setParam(ParamIDs::britishHpfEnabled, 1.0f);
            setParam(ParamIDs::britishLpfFreq, 20000.0f);
            setParam(ParamIDs::britishLpfEnabled, 0.0f);
            setParam(ParamIDs::britishLfGain, 1.0f);
            setParam(ParamIDs::britishLfFreq, 60.0f);
            setParam(ParamIDs::britishLfBell, 0.0f);
            setParam(ParamIDs::britishLmGain, -1.0f);
            setParam(ParamIDs::britishLmFreq, 300.0f);
            setParam(ParamIDs::britishLmQ, 0.8f);
            setParam(ParamIDs::britishHmGain, 0.5f);
            setParam(ParamIDs::britishHmFreq, 3000.0f);
            setParam(ParamIDs::britishHmQ, 0.7f);
            setParam(ParamIDs::britishHfGain, 1.5f);
            setParam(ParamIDs::britishHfFreq, 12000.0f);
            setParam(ParamIDs::britishHfBell, 0.0f);
            setParam(ParamIDs::britishSaturation, 8.0f);
            setParam(ParamIDs::britishInputGain, 0.0f);
            setParam(ParamIDs::britishOutputGain, 0.0f);
            break;

        default:
            break;
    }
}

//==============================================================================
void MultiQEditor::setupPultecControls()
{
    // Create Pultec curve display
    pultecCurveDisplay = std::make_unique<PultecCurveDisplay>(processor);
    pultecCurveDisplay->setVisible(false);
    addAndMakeVisible(pultecCurveDisplay.get());

    // Helper to setup Vintage Tube EQ-style rotary knob
    auto setupPultecKnob = [this](std::unique_ptr<juce::Slider>& slider, const juce::String& name) {
        slider = std::make_unique<LunaSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::NoTextBox);
        slider->setName(name);
        slider->setLookAndFeel(&vintageTubeLookAndFeel);
        slider->setVisible(false);
        addAndMakeVisible(slider.get());
    };

    // Helper to setup Vintage Tube EQ-style combo selector
    auto setupPultecSelector = [this](std::unique_ptr<juce::ComboBox>& combo) {
        combo = std::make_unique<juce::ComboBox>();
        combo->setLookAndFeel(&vintageTubeLookAndFeel);
        combo->setVisible(false);
        addAndMakeVisible(combo.get());
    };

    // Helper to setup knob label (light gray on dark background, larger font)
    auto setupKnobLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));  // Light gray text
        label.setFont(juce::Font(juce::FontOptions(15.0f).withStyle("Bold")));  // Larger, readable
        label.setVisible(false);
        addAndMakeVisible(label);
    };

    // LF Section
    setupPultecKnob(pultecLfBoostSlider, "lf_boost");
    setupPultecSelector(pultecLfFreqSelector);
    pultecLfFreqSelector->addItemList({"20 Hz", "30 Hz", "60 Hz", "100 Hz"}, 1);
    setupPultecKnob(pultecLfAttenSlider, "lf_atten");

    // HF Boost Section
    setupPultecKnob(pultecHfBoostSlider, "hf_boost");
    setupPultecSelector(pultecHfBoostFreqSelector);
    pultecHfBoostFreqSelector->addItemList({"3k", "4k", "5k", "8k", "10k", "12k", "16k"}, 1);
    setupPultecKnob(pultecHfBandwidthSlider, "hf_bandwidth");

    // HF Atten Section
    setupPultecKnob(pultecHfAttenSlider, "hf_atten");
    setupPultecSelector(pultecHfAttenFreqSelector);
    pultecHfAttenFreqSelector->addItemList({"5k", "10k", "20k"}, 1);

    // Global controls
    setupPultecKnob(pultecInputGainSlider, "input");
    setupPultecKnob(pultecOutputGainSlider, "output");
    setupPultecKnob(pultecTubeDriveSlider, "tube_drive");

    // Mid Section controls
    // Mid Enabled button (IN button) - bypasses the Mid Dip/Peak section only
    pultecMidEnabledButton = std::make_unique<juce::ToggleButton>("IN");
    pultecMidEnabledButton->setLookAndFeel(&vintageTubeLookAndFeel);
    pultecMidEnabledButton->setTooltip("Enable/disable Mid Dip/Peak section");
    pultecMidEnabledButton->setVisible(false);
    addAndMakeVisible(pultecMidEnabledButton.get());

    // Mid frequency dropdowns (matching style of other freq selectors)
    auto setupMidFreqSelector = [this](std::unique_ptr<juce::ComboBox>& selector) {
        selector = std::make_unique<juce::ComboBox>();
        selector->setLookAndFeel(&vintageTubeLookAndFeel);
        selector->setVisible(false);
        addAndMakeVisible(selector.get());
    };

    setupMidFreqSelector(pultecMidLowFreqSelector);
    pultecMidLowFreqSelector->addItem("200 Hz", 1);
    pultecMidLowFreqSelector->addItem("300 Hz", 2);
    pultecMidLowFreqSelector->addItem("500 Hz", 3);
    pultecMidLowFreqSelector->addItem("700 Hz", 4);
    pultecMidLowFreqSelector->addItem("1.0 kHz", 5);

    setupPultecKnob(pultecMidLowPeakSlider, "mid_low_peak");

    setupMidFreqSelector(pultecMidDipFreqSelector);
    pultecMidDipFreqSelector->addItem("200 Hz", 1);
    pultecMidDipFreqSelector->addItem("300 Hz", 2);
    pultecMidDipFreqSelector->addItem("500 Hz", 3);
    pultecMidDipFreqSelector->addItem("700 Hz", 4);
    pultecMidDipFreqSelector->addItem("1.0 kHz", 5);
    pultecMidDipFreqSelector->addItem("1.5 kHz", 6);
    pultecMidDipFreqSelector->addItem("2.0 kHz", 7);

    setupPultecKnob(pultecMidDipSlider, "mid_dip");

    setupMidFreqSelector(pultecMidHighFreqSelector);
    pultecMidHighFreqSelector->addItem("1.5 kHz", 1);
    pultecMidHighFreqSelector->addItem("2.0 kHz", 2);
    pultecMidHighFreqSelector->addItem("3.0 kHz", 3);
    pultecMidHighFreqSelector->addItem("4.0 kHz", 4);
    pultecMidHighFreqSelector->addItem("5.0 kHz", 5);

    setupPultecKnob(pultecMidHighPeakSlider, "mid_high_peak");

    // Section labels (light gray on dark background)
    auto setupSectionLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));  // Light gray text
        label.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
        label.setVisible(false);
        addAndMakeVisible(label);
    };

    setupSectionLabel(pultecLfLabel, "LOW FREQUENCY");
    setupSectionLabel(pultecHfBoostLabel, "HIGH FREQUENCY");
    setupSectionLabel(pultecHfAttenLabel, "ATTEN SEL");
    setupSectionLabel(pultecMasterLabel, "MASTER");

    // Knob labels
    setupKnobLabel(pultecLfBoostKnobLabel, "BOOST");
    setupKnobLabel(pultecLfFreqKnobLabel, "CPS");
    setupKnobLabel(pultecLfAttenKnobLabel, "ATTEN");
    setupKnobLabel(pultecHfBoostKnobLabel, "BOOST");
    setupKnobLabel(pultecHfBoostFreqKnobLabel, "KCS");
    setupKnobLabel(pultecHfBwKnobLabel, "HF BANDWIDTH");
    setupKnobLabel(pultecHfAttenKnobLabel, "ATTEN");
    setupKnobLabel(pultecHfAttenFreqKnobLabel, "KCS");
    setupKnobLabel(pultecInputKnobLabel, "INPUT");
    setupKnobLabel(pultecOutputKnobLabel, "OUTPUT");
    setupKnobLabel(pultecTubeKnobLabel, "DRIVE");

    // Mid section labels
    setupKnobLabel(pultecMidLowFreqLabel, "LOW FREQ");
    setupKnobLabel(pultecMidLowPeakLabel, "LOW PEAK");
    setupKnobLabel(pultecMidDipFreqLabel, "DIP FREQ");
    setupKnobLabel(pultecMidDipLabel, "DIP");
    setupKnobLabel(pultecMidHighFreqLabel, "HIGH FREQ");
    setupKnobLabel(pultecMidHighPeakLabel, "HIGH PEAK");

    // Create attachments
    pultecLfBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecLfBoostGain, *pultecLfBoostSlider);
    pultecLfFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecLfBoostFreq, *pultecLfFreqSelector);
    pultecLfAttenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecLfAttenGain, *pultecLfAttenSlider);
    pultecHfBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecHfBoostGain, *pultecHfBoostSlider);
    pultecHfBoostFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecHfBoostFreq, *pultecHfBoostFreqSelector);
    pultecHfBandwidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecHfBoostBandwidth, *pultecHfBandwidthSlider);
    pultecHfAttenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecHfAttenGain, *pultecHfAttenSlider);
    pultecHfAttenFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecHfAttenFreq, *pultecHfAttenFreqSelector);
    pultecInputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecInputGain, *pultecInputGainSlider);
    pultecOutputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecOutputGain, *pultecOutputGainSlider);
    pultecTubeDriveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecTubeDrive, *pultecTubeDriveSlider);

    // Mid section attachments
    pultecMidEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::pultecMidEnabled, *pultecMidEnabledButton);
    pultecMidLowFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecMidLowFreq, *pultecMidLowFreqSelector);
    pultecMidLowPeakAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecMidLowPeak, *pultecMidLowPeakSlider);
    pultecMidDipFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecMidDipFreq, *pultecMidDipFreqSelector);
    pultecMidDipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecMidDip, *pultecMidDipSlider);
    pultecMidHighFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecMidHighFreq, *pultecMidHighFreqSelector);
    pultecMidHighPeakAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecMidHighPeak, *pultecMidHighPeakSlider);
}

void MultiQEditor::layoutPultecControls()
{
    auto bounds = getLocalBounds();

    // ===== TUBE MODE LAYOUT =====
    // Reorganized layout per user request:
    // - Row 1: [LF BOOST] [LF ATTEN] [HF BOOST] [HF ATTEN]
    // - Row 2: Frequency row with separator lines: [LF FREQ] [HF BANDWIDTH] [HF FREQ] [ATTEN FREQ]
    // - Row 3: MID DIP/PEAK section
    // - Right panel: INPUT → OUTPUT → TUBE DRIVE (vertical signal flow)

    const int headerHeight = 55;
    const int labelHeight = 22;         // Height for knob labels
    const int knobSize = 105;           // Main knobs
    const int smallKnobSize = 90;       // Row 3 knobs (mid section)
    const int comboWidth = 90;          // Width for combo boxes
    const int comboHeight = 32;         // Height for combo boxes
    const int bottomMargin = 35;        // Margin at bottom for footer
    const int rightPanelWidth = 125;    // Right side panel for INPUT/OUTPUT/DRIVE

    // Margins - leave space for right panel
    int mainX = 30;
    int mainWidth = bounds.getWidth() - 60 - rightPanelWidth;

    // Calculate row heights
    // Row 1: 4 gain knobs with labels below
    int row1Height = knobSize + labelHeight;
    // Row 2: Frequency selectors + HF BANDWIDTH knob (with separator lines above and below)
    int row2Height = labelHeight + knobSize + 10;  // Extra padding for separators
    // Row 3: Mid section (smaller knobs)
    int row3Height = smallKnobSize + labelHeight;

    int totalContentHeight = row1Height + row2Height + row3Height;
    int availableHeight = bounds.getHeight() - headerHeight - bottomMargin;
    int extraSpace = availableHeight - totalContentHeight;
    int rowGap = extraSpace / 4;  // Distribute extra space

    // ============== ROW 1: MAIN GAIN CONTROLS (4 knobs) ==============
    int row1Y = headerHeight + rowGap;

    // Calculate even spacing for 4 knobs across main width
    int totalKnobWidth = 4 * knobSize;
    int knobSpacing = (mainWidth - totalKnobWidth) / 5;

    // LF BOOST (position 1)
    int knob1X = mainX + knobSpacing;
    pultecLfBoostSlider->setBounds(knob1X, row1Y, knobSize, knobSize);
    pultecLfBoostKnobLabel.setBounds(knob1X - 15, row1Y + knobSize + 2, knobSize + 30, labelHeight);
    pultecLfBoostKnobLabel.setText("LF BOOST", juce::dontSendNotification);

    // LF ATTEN (position 2)
    int knob2X = mainX + 2 * knobSpacing + knobSize;
    pultecLfAttenSlider->setBounds(knob2X, row1Y, knobSize, knobSize);
    pultecLfAttenKnobLabel.setBounds(knob2X - 15, row1Y + knobSize + 2, knobSize + 30, labelHeight);
    pultecLfAttenKnobLabel.setText("LF ATTEN", juce::dontSendNotification);

    // HF BOOST (position 3)
    int knob3X = mainX + 3 * knobSpacing + 2 * knobSize;
    pultecHfBoostSlider->setBounds(knob3X, row1Y, knobSize, knobSize);
    pultecHfBoostKnobLabel.setBounds(knob3X - 15, row1Y + knobSize + 2, knobSize + 30, labelHeight);
    pultecHfBoostKnobLabel.setText("HF BOOST", juce::dontSendNotification);

    // HF ATTEN (position 4)
    int knob4X = mainX + 4 * knobSpacing + 3 * knobSize;
    pultecHfAttenSlider->setBounds(knob4X, row1Y, knobSize, knobSize);
    pultecHfAttenKnobLabel.setBounds(knob4X - 15, row1Y + knobSize + 2, knobSize + 30, labelHeight);
    pultecHfAttenKnobLabel.setText("HF ATTEN", juce::dontSendNotification);

    // ============== ROW 2: FREQUENCY SELECTORS & HF BANDWIDTH (with separator lines) ==============
    // Layout: [LF FREQ] [HF BANDWIDTH] [HF FREQ] [ATTEN FREQ] evenly distributed
    int row2Y = row1Y + row1Height + rowGap;

    // 4 controls evenly spaced across the row
    int row2ControlWidth = knobSize;  // Same size as main knobs for consistency
    int row2TotalWidth = 4 * row2ControlWidth;
    int row2Spacing = (mainWidth - row2TotalWidth) / 5;

    // 1. LF FREQ selector (position 1)
    int lfFreqX = mainX + row2Spacing + (row2ControlWidth - comboWidth) / 2;
    pultecLfFreqKnobLabel.setBounds(mainX + row2Spacing, row2Y, row2ControlWidth, labelHeight);
    pultecLfFreqKnobLabel.setText("LF FREQ", juce::dontSendNotification);
    pultecLfFreqSelector->setBounds(lfFreqX, row2Y + labelHeight + 2, comboWidth, comboHeight);

    // 2. HF BANDWIDTH knob (position 2)
    int bwX = mainX + 2 * row2Spacing + row2ControlWidth;
    pultecHfBwKnobLabel.setBounds(bwX, row2Y, row2ControlWidth, labelHeight);
    pultecHfBwKnobLabel.setText("HF BANDWIDTH", juce::dontSendNotification);
    pultecHfBandwidthSlider->setBounds(bwX, row2Y + labelHeight + 2, row2ControlWidth, row2ControlWidth);

    // 3. HF FREQ selector (position 3)
    int hfBoostFreqX = mainX + 3 * row2Spacing + 2 * row2ControlWidth + (row2ControlWidth - comboWidth) / 2;
    pultecHfBoostFreqKnobLabel.setBounds(mainX + 3 * row2Spacing + 2 * row2ControlWidth, row2Y, row2ControlWidth, labelHeight);
    pultecHfBoostFreqKnobLabel.setText("HF FREQ", juce::dontSendNotification);
    pultecHfBoostFreqSelector->setBounds(hfBoostFreqX, row2Y + labelHeight + 2, comboWidth, comboHeight);

    // 4. ATTEN FREQ selector (position 4)
    int hfAttenFreqX = mainX + 4 * row2Spacing + 3 * row2ControlWidth + (row2ControlWidth - comboWidth) / 2;
    pultecHfAttenFreqKnobLabel.setBounds(mainX + 4 * row2Spacing + 3 * row2ControlWidth, row2Y, row2ControlWidth, labelHeight);
    pultecHfAttenFreqKnobLabel.setText("ATTEN FREQ", juce::dontSendNotification);
    pultecHfAttenFreqSelector->setBounds(hfAttenFreqX, row2Y + labelHeight + 2, comboWidth, comboHeight);

    // ============== ROW 3: MID DIP/PEAK SECTION (6 controls + IN toggle) ==============
    int row3Y = row2Y + row2Height + rowGap;

    // IN toggle button on the left
    int inButtonWidth = 45;
    int inButtonHeight = 40;
    int inButtonX = mainX - 10;
    int inButtonY = row3Y + (smallKnobSize - inButtonHeight) / 2;
    if (pultecMidEnabledButton)
        pultecMidEnabledButton->setBounds(inButtonX, inButtonY, inButtonWidth, inButtonHeight);

    // 6 controls evenly spaced after the IN button
    int midAreaX = mainX + inButtonWidth + 5;
    int midAreaWidth = mainWidth - inButtonWidth - 5;
    int midKnobSpacing = (midAreaWidth - 6 * smallKnobSize) / 7;

    // Dropdown width for frequency selectors
    int dropdownWidth = 80;
    int dropdownHeight = 24;

    // LOW FREQ dropdown (position 1)
    int midKnob1X = midAreaX + midKnobSpacing;
    if (pultecMidLowFreqSelector)
    {
        pultecMidLowFreqSelector->setBounds(midKnob1X + (smallKnobSize - dropdownWidth) / 2, row3Y + (smallKnobSize - dropdownHeight) / 2, dropdownWidth, dropdownHeight);
        pultecMidLowFreqLabel.setBounds(midKnob1X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        pultecMidLowFreqLabel.setText("LOW FREQ", juce::dontSendNotification);
    }

    // LOW PEAK knob (position 2)
    int midKnob2X = midAreaX + 2 * midKnobSpacing + smallKnobSize;
    if (pultecMidLowPeakSlider)
    {
        pultecMidLowPeakSlider->setBounds(midKnob2X, row3Y, smallKnobSize, smallKnobSize);
        pultecMidLowPeakLabel.setBounds(midKnob2X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        pultecMidLowPeakLabel.setText("LOW PEAK", juce::dontSendNotification);
    }

    // DIP FREQ dropdown (position 3)
    int midKnob3X = midAreaX + 3 * midKnobSpacing + 2 * smallKnobSize;
    if (pultecMidDipFreqSelector)
    {
        pultecMidDipFreqSelector->setBounds(midKnob3X + (smallKnobSize - dropdownWidth) / 2, row3Y + (smallKnobSize - dropdownHeight) / 2, dropdownWidth, dropdownHeight);
        pultecMidDipFreqLabel.setBounds(midKnob3X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        pultecMidDipFreqLabel.setText("DIP FREQ", juce::dontSendNotification);
    }

    // DIP knob (position 4)
    int midKnob4X = midAreaX + 4 * midKnobSpacing + 3 * smallKnobSize;
    if (pultecMidDipSlider)
    {
        pultecMidDipSlider->setBounds(midKnob4X, row3Y, smallKnobSize, smallKnobSize);
        pultecMidDipLabel.setBounds(midKnob4X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        pultecMidDipLabel.setText("DIP", juce::dontSendNotification);
    }

    // HIGH FREQ dropdown (position 5)
    int midKnob5X = midAreaX + 5 * midKnobSpacing + 4 * smallKnobSize;
    if (pultecMidHighFreqSelector)
    {
        pultecMidHighFreqSelector->setBounds(midKnob5X + (smallKnobSize - dropdownWidth) / 2, row3Y + (smallKnobSize - dropdownHeight) / 2, dropdownWidth, dropdownHeight);
        pultecMidHighFreqLabel.setBounds(midKnob5X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        pultecMidHighFreqLabel.setText("HIGH FREQ", juce::dontSendNotification);
    }

    // HIGH PEAK knob (position 6)
    int midKnob6X = midAreaX + 6 * midKnobSpacing + 5 * smallKnobSize;
    if (pultecMidHighPeakSlider)
    {
        pultecMidHighPeakSlider->setBounds(midKnob6X, row3Y, smallKnobSize, smallKnobSize);
        pultecMidHighPeakLabel.setBounds(midKnob6X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        pultecMidHighPeakLabel.setText("HIGH PEAK", juce::dontSendNotification);
    }

    // ============== RIGHT SIDE PANEL: INPUT → OUTPUT → TUBE DRIVE ==============
    // Vertical signal flow: INPUT at top, OUTPUT in middle, TUBE DRIVE at bottom
    int rightPanelX = bounds.getWidth() - rightPanelWidth;
    int rightKnobSize = 85;  // Knob size for right panel
    int rightSpacing = 12;   // Spacing between knobs
    int totalRightHeight = 3 * rightKnobSize + 2 * rightSpacing + 3 * labelHeight;
    int rightStartY = headerHeight + (availableHeight - totalRightHeight) / 2;  // Center vertically

    int rightCenterX = rightPanelX + (rightPanelWidth - rightKnobSize) / 2;

    // INPUT knob (top of right panel)
    int inputY = rightStartY;
    pultecInputGainSlider->setBounds(rightCenterX, inputY, rightKnobSize, rightKnobSize);
    pultecInputKnobLabel.setBounds(rightCenterX - 15, inputY + rightKnobSize + 2, rightKnobSize + 30, labelHeight);
    pultecInputKnobLabel.setText("INPUT", juce::dontSendNotification);

    // OUTPUT knob (middle of right panel)
    int outputY = inputY + rightKnobSize + labelHeight + rightSpacing;
    pultecOutputGainSlider->setBounds(rightCenterX, outputY, rightKnobSize, rightKnobSize);
    pultecOutputKnobLabel.setBounds(rightCenterX - 15, outputY + rightKnobSize + 2, rightKnobSize + 30, labelHeight);
    pultecOutputKnobLabel.setText("OUTPUT", juce::dontSendNotification);

    // TUBE DRIVE knob (bottom of right panel)
    int driveY = outputY + rightKnobSize + labelHeight + rightSpacing;
    pultecTubeDriveSlider->setBounds(rightCenterX, driveY, rightKnobSize, rightKnobSize);
    pultecTubeKnobLabel.setBounds(rightCenterX - 15, driveY + rightKnobSize + 2, rightKnobSize + 30, labelHeight);
    pultecTubeKnobLabel.setText("TUBE DRIVE", juce::dontSendNotification);

    // Hide unused labels (section labels are drawn in paint())
    pultecMasterLabel.setVisible(false);
    pultecLfLabel.setVisible(false);
    pultecHfBoostLabel.setVisible(false);
    pultecHfAttenLabel.setVisible(false);

    // Hide curve display (not used in Tube layout)
    if (pultecCurveDisplay)
        pultecCurveDisplay->setVisible(false);
}

//==============================================================================
// A/B Comparison Functions
//==============================================================================

void MultiQEditor::toggleAB()
{
    // Save current state to the current slot
    copyCurrentToState(isStateA ? stateA : stateB);

    // Toggle slot
    isStateA = !isStateA;

    // Apply the other state (if it exists)
    if (isStateA)
    {
        if (stateA.isValid())
            applyState(stateA);
        tubeAbButton.setButtonText("A");
        tubeAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));  // Green for A
    }
    else
    {
        if (stateB.isValid())
            applyState(stateB);
        tubeAbButton.setButtonText("B");
        tubeAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6a3a3a));  // Red for B
    }
}

void MultiQEditor::copyCurrentToState(juce::ValueTree& state)
{
    juce::MemoryBlock data;
    processor.getStateInformation(data);
    state = juce::ValueTree("MultiQState");
    state.setProperty("data", data.toBase64Encoding(), nullptr);
}

void MultiQEditor::applyState(const juce::ValueTree& state)
{
    if (!state.isValid())
        return;

    auto dataStr = state.getProperty("data").toString();
    juce::MemoryBlock data;
    data.fromBase64Encoding(dataStr);
    processor.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
}

void MultiQEditor::toggleBritishAB()
{
    // Save current state to the current slot
    copyCurrentToState(britishIsStateA ? britishStateA : britishStateB);

    // Toggle slot
    britishIsStateA = !britishIsStateA;

    // Apply the other state (if it exists)
    if (britishIsStateA)
    {
        if (britishStateA.isValid())
            applyState(britishStateA);
        britishAbButton.setButtonText("A");
        britishAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));  // Green for A
    }
    else
    {
        if (britishStateB.isValid())
            applyState(britishStateB);
        britishAbButton.setButtonText("B");
        britishAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6a3a3a));  // Red for B
    }
}

void MultiQEditor::drawBritishKnobMarkings(juce::Graphics& g)
{
    // SSL-style knob tick markings with value labels (matching 4K-EQ exactly)

    // Rotation range constants (must match setupBritishKnob rotaryParameters)
    const float startAngle = juce::MathConstants<float>::pi * 1.25f;  // 225° = 7 o'clock
    const float endAngle = juce::MathConstants<float>::pi * 2.75f;    // 495° = 5 o'clock
    const float totalRange = endAngle - startAngle;  // 270° total sweep

    // Helper to draw a single tick with label at the correct position
    auto drawTickAtValue = [&](juce::Rectangle<int> knobBounds, float value,
                               float minVal, float maxVal, float skew,
                               const juce::String& label, bool isCenter = false)
    {
        auto center = knobBounds.getCentre().toFloat();
        float radius = knobBounds.getWidth() / 2.0f + 3.0f;

        // Calculate the normalized position (0-1) for this value with skew
        float proportion = (value - minVal) / (maxVal - minVal);
        float normalizedPos = std::pow(proportion, skew);

        // Calculate angle and adjust for knob pointer coordinate system
        float angle = startAngle + totalRange * normalizedPos;
        float tickAngle = angle - juce::MathConstants<float>::halfPi;

        float tickLength = isCenter ? 5.0f : 3.0f;

        // Draw tick mark
        g.setColour(isCenter ? juce::Colour(0xff909090) : juce::Colour(0xff606060));
        float x1 = center.x + std::cos(tickAngle) * radius;
        float y1 = center.y + std::sin(tickAngle) * radius;
        float x2 = center.x + std::cos(tickAngle) * (radius + tickLength);
        float y2 = center.y + std::sin(tickAngle) * (radius + tickLength);
        g.drawLine(x1, y1, x2, y2, isCenter ? 1.5f : 1.0f);

        // Draw label if provided
        if (label.isNotEmpty())
        {
            g.setFont(juce::Font(juce::FontOptions(9.5f).withStyle("Bold")));

            float labelRadius = radius + tickLength + 10.0f;
            float labelX = center.x + std::cos(tickAngle) * labelRadius;
            float labelY = center.y + std::sin(tickAngle) * labelRadius;

            // Shadow
            g.setColour(juce::Colour(0xff000000));
            g.drawText(label, static_cast<int>(labelX) - 18 + 1, static_cast<int>(labelY) - 7 + 1, 36, 14, juce::Justification::centred);

            // Label
            g.setColour(juce::Colour(0xffd0d0d0));
            g.drawText(label, static_cast<int>(labelX) - 18, static_cast<int>(labelY) - 7, 36, 14, juce::Justification::centred);
        }
    };

    // Helper for linear (non-skewed) parameters
    auto drawTicksLinear = [&](juce::Rectangle<int> knobBounds,
                               const std::vector<std::pair<float, juce::String>>& ticks,
                               float minVal, float maxVal, bool hasCenter = false)
    {
        float centerVal = (minVal + maxVal) / 2.0f;
        for (const auto& tick : ticks)
        {
            bool isCenter = hasCenter && std::abs(tick.first - centerVal) < 0.01f;
            drawTickAtValue(knobBounds, tick.first, minVal, maxVal, 1.0f, tick.second, isCenter);
        }
    };

    // Helper for SSL-style evenly spaced ticks
    auto drawTicksEvenlySpaced = [&](juce::Rectangle<int> knobBounds,
                                      const std::vector<juce::String>& labels)
    {
        auto center = knobBounds.getCentre().toFloat();
        float radius = knobBounds.getWidth() / 2.0f + 3.0f;
        int numTicks = static_cast<int>(labels.size());

        for (int i = 0; i < numTicks; ++i)
        {
            float normalizedPos = (numTicks > 1) ? static_cast<float>(i) / static_cast<float>(numTicks - 1) : 0.0f;

            float angle = startAngle + totalRange * normalizedPos;
            float tickAngle = angle - juce::MathConstants<float>::halfPi;

            float tickLength = 3.0f;

            // Draw tick mark
            g.setColour(juce::Colour(0xff606060));
            float x1 = center.x + std::cos(tickAngle) * radius;
            float y1 = center.y + std::sin(tickAngle) * radius;
            float x2 = center.x + std::cos(tickAngle) * (radius + tickLength);
            float y2 = center.y + std::sin(tickAngle) * (radius + tickLength);
            g.drawLine(x1, y1, x2, y2, 1.0f);

            // Draw label
            if (labels[static_cast<size_t>(i)].isNotEmpty())
            {
                g.setFont(juce::Font(juce::FontOptions(9.5f).withStyle("Bold")));

                float labelRadius = radius + tickLength + 10.0f;
                float labelX = center.x + std::cos(tickAngle) * labelRadius;
                float labelY = center.y + std::sin(tickAngle) * labelRadius;

                // Shadow
                g.setColour(juce::Colour(0xff000000));
                g.drawText(labels[static_cast<size_t>(i)], static_cast<int>(labelX) - 18 + 1, static_cast<int>(labelY) - 7 + 1, 36, 14, juce::Justification::centred);

                // Label
                g.setColour(juce::Colour(0xffd0d0d0));
                g.drawText(labels[static_cast<size_t>(i)], static_cast<int>(labelX) - 18, static_cast<int>(labelY) - 7, 36, 14, juce::Justification::centred);
            }
        }
    };

    // ===== GAIN KNOBS (linear, -20 to +20 dB) =====
    std::vector<std::pair<float, juce::String>> gainTicks = {
        {-20.0f, "-20"}, {0.0f, "0"}, {20.0f, "+20"}
    };

    if (britishLfGainSlider) drawTicksLinear(britishLfGainSlider->getBounds(), gainTicks, -20.0f, 20.0f, true);
    if (britishLmGainSlider) drawTicksLinear(britishLmGainSlider->getBounds(), gainTicks, -20.0f, 20.0f, true);
    if (britishHmGainSlider) drawTicksLinear(britishHmGainSlider->getBounds(), gainTicks, -20.0f, 20.0f, true);
    if (britishHfGainSlider) drawTicksLinear(britishHfGainSlider->getBounds(), gainTicks, -20.0f, 20.0f, true);

    // ===== HPF (20-500Hz) - SSL 4000 E style evenly spaced =====
    if (britishHpfFreqSlider) drawTicksEvenlySpaced(britishHpfFreqSlider->getBounds(), {"20", "70", "120", "200", "300", "500"});

    // ===== LPF (3000-20000Hz) - SSL style evenly spaced =====
    if (britishLpfFreqSlider) drawTicksEvenlySpaced(britishLpfFreqSlider->getBounds(), {"3k", "5k", "8k", "12k", "20k"});

    // ===== LF Frequency (30-480Hz) - SSL 4000 E style evenly spaced =====
    if (britishLfFreqSlider) drawTicksEvenlySpaced(britishLfFreqSlider->getBounds(), {"30", "50", "100", "200", "300", "480"});

    // ===== LMF Frequency (200-2500Hz) - SSL 4000 E style evenly spaced =====
    if (britishLmFreqSlider) drawTicksEvenlySpaced(britishLmFreqSlider->getBounds(), {".2", ".5", ".8", "1", "2", "2.5"});

    // ===== HMF Frequency (600-7000Hz) - SSL 4000 E style evenly spaced =====
    if (britishHmFreqSlider) drawTicksEvenlySpaced(britishHmFreqSlider->getBounds(), {".6", "1.5", "3", "4.5", "6", "7"});

    // ===== HF Frequency (1500-16000Hz) - SSL 4000 E style evenly spaced =====
    if (britishHfFreqSlider) drawTicksEvenlySpaced(britishHfFreqSlider->getBounds(), {"1.5", "8", "10", "14", "16"});

    // ===== Q knobs (0.4-4.0, linear) =====
    std::vector<std::pair<float, juce::String>> qTicks = {
        {0.4f, ".4"}, {1.0f, "1"}, {2.0f, "2"}, {3.0f, "3"}, {4.0f, "4"}
    };
    if (britishLmQSlider) drawTicksLinear(britishLmQSlider->getBounds(), qTicks, 0.4f, 4.0f, false);
    if (britishHmQSlider) drawTicksLinear(britishHmQSlider->getBounds(), qTicks, 0.4f, 4.0f, false);

    // ===== Input gain (-12 to +12 dB, linear) =====
    std::vector<std::pair<float, juce::String>> inputGainTicks = {
        {-12.0f, "-12"}, {0.0f, "0"}, {12.0f, "+12"}
    };
    if (britishInputGainSlider) drawTicksLinear(britishInputGainSlider->getBounds(), inputGainTicks, -12.0f, 12.0f, true);

    // ===== Output gain (-12 to +12 dB, linear) =====
    std::vector<std::pair<float, juce::String>> outputGainTicks = {
        {-12.0f, "-12"}, {0.0f, "0"}, {12.0f, "+12"}
    };
    if (britishOutputGainSlider) drawTicksLinear(britishOutputGainSlider->getBounds(), outputGainTicks, -12.0f, 12.0f, true);

    // ===== Saturation/Drive (0-100%, linear) =====
    std::vector<std::pair<float, juce::String>> satTicks = {
        {0.0f, "0"}, {20.0f, "20"}, {40.0f, "40"}, {60.0f, "60"}, {80.0f, "80"}, {100.0f, "100"}
    };
    if (britishSaturationSlider) drawTicksLinear(britishSaturationSlider->getBounds(), satTicks, 0.0f, 100.0f, false);
}

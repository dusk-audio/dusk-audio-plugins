#include "MultiQEditor.h"

MultiQEditor::MultiQEditor(MultiQ& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lookAndFeel);

    userPresetManager = std::make_unique<UserPresetManager>("Multi-Q");

    graphicDisplay = std::make_unique<EQGraphicDisplay>(processor);
    addAndMakeVisible(graphicDisplay.get());
    graphicDisplay->onBandSelected = [this](int band) { onBandSelected(band); };

    bandDetailPanel = std::make_unique<BandDetailPanel>(processor);
    bandDetailPanel->onBandSelected = [this](int band) { onBandSelected(band); };
    addAndMakeVisible(bandDetailPanel.get());

    // Create British mode curve display (4K-EQ style)
    britishCurveDisplay = std::make_unique<BritishEQCurveDisplay>(processor);
    britishCurveDisplay->setVisible(false);  // Hidden by default
    addAndMakeVisible(britishCurveDisplay.get());

    for (int i = 0; i < 8; ++i)
    {
        auto& btn = bandEnableButtons[static_cast<size_t>(i)];
        btn = std::make_unique<BandEnableButton>(i);
        addAndMakeVisible(btn.get());

        bandEnableAttachments[static_cast<size_t>(i)] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.parameters, ParamIDs::bandEnabled(i + 1), *btn);
    }

    for (int i = 0; i < 8; ++i)
    {
        bandEnableButtons[static_cast<size_t>(i)]->onClick = [this, i]() {
            // Guard against calls during initialization
            if (graphicDisplay != nullptr && bandDetailPanel != nullptr)
                onBandSelected(i);
        };
    }

    // Selected band controls
    selectedBandLabel.setText("No Band Selected", juce::dontSendNotification);
    selectedBandLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    selectedBandLabel.setFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));
    addAndMakeVisible(selectedBandLabel);

    freqSlider = std::make_unique<DuskSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::TextBoxBelow);
    setupSlider(*freqSlider, "");
    freqSlider->setTooltip("Frequency: Center frequency of this band (Cmd/Ctrl+drag for fine control)");
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

    gainSlider = std::make_unique<DuskSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::TextBoxBelow);
    setupSlider(*gainSlider, "");
    gainSlider->setTooltip("Gain: Boost or cut at this frequency (Cmd/Ctrl+drag for fine control)");
    // Custom gain formatting: "+3.5 dB" or "-2.0 dB"
    gainSlider->textFromValueFunction = [](double value) {
        juce::String sign = value >= 0 ? "+" : "";
        return sign + juce::String(value, 1) + " dB";
    };
    addAndMakeVisible(gainSlider.get());

    qSlider = std::make_unique<DuskSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                              juce::Slider::TextBoxBelow);
    setupSlider(*qSlider, "");
    qSlider->setTooltip("Q: Bandwidth/resonance - higher = narrower (Cmd/Ctrl+drag for fine control)");
    // Custom Q formatting: "0.71" (2 decimal places)
    qSlider->textFromValueFunction = [](double value) {
        return juce::String(value, 2);
    };
    addAndMakeVisible(qSlider.get());

    slopeSelector = std::make_unique<juce::ComboBox>();
    slopeSelector->addItemList({"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct"}, 1);
    slopeSelector->setTooltip("Filter slope: Steeper = sharper cutoff (6-96 dB/octave)");
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
    masterGainSlider = std::make_unique<DuskSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                       juce::Slider::TextBoxBelow);
    setupSlider(*masterGainSlider, "");
    masterGainSlider->setTooltip("Master Gain: Output level adjustment (-24 to +24 dB)");
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
    bypassButton->setTooltip("Bypass all EQ processing (Shortcut: B)");
    addAndMakeVisible(bypassButton.get());
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::bypass, *bypassButton);

    hqButton = std::make_unique<juce::ToggleButton>("HQ");
    hqButton->setTooltip("Enable oversampling for analog-matched response at high frequencies");
    hqButton->setVisible(false);  // Replaced by oversamplingSelector
    addAndMakeVisible(hqButton.get());
    // Note: hqEnabled is now a Choice parameter, not Bool - no ButtonAttachment

    // Linear Phase controls (Digital mode only)
    linearPhaseButton = std::make_unique<juce::ToggleButton>("Linear Phase");
    linearPhaseButton->setTooltip("Enable linear phase FIR filtering (introduces latency, disables dynamics)");
    addAndMakeVisible(linearPhaseButton.get());
    linearPhaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::linearPhaseEnabled, *linearPhaseButton);

    linearPhaseQualitySelector = std::make_unique<juce::ComboBox>();
    linearPhaseQualitySelector->addItemList({"LP: Low Latency", "LP: Balanced", "LP: High Quality"}, 1);
    linearPhaseQualitySelector->setTooltip("Linear phase filter quality (affects latency: ~46ms / ~93ms / ~186ms at 44.1kHz)");
    addAndMakeVisible(linearPhaseQualitySelector.get());
    linearPhaseQualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::linearPhaseLength, *linearPhaseQualitySelector);

    // Auto-gain compensation button (maintains consistent loudness for A/B comparison)
    autoGainButton = std::make_unique<juce::ToggleButton>("Auto Gain");
    autoGainButton->setTooltip("Automatically compensate for EQ changes to maintain consistent loudness (for A/B comparison)");
    addAndMakeVisible(autoGainButton.get());
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::autoGainEnabled, *autoGainButton);

    processingModeSelector = std::make_unique<juce::ComboBox>();
    processingModeSelector->addItemList({"Stereo", "Left", "Right", "Mid", "Side"}, 1);
    processingModeSelector->setTooltip("Processing mode: Apply EQ to Stereo, Left, Right, Mid (center), or Side (stereo width)");
    addAndMakeVisible(processingModeSelector.get());
    processingModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::processingMode, *processingModeSelector);

    qCoupleModeSelector = std::make_unique<juce::ComboBox>();
    qCoupleModeSelector->addItemList({"Q-Couple: Off", "Proportional", "Light", "Medium", "Strong",
                                       "Asym Light", "Asym Medium", "Asym Strong", "Vintage"}, 1);
    qCoupleModeSelector->setTooltip("Q-Coupling: Automatically widens Q when gain increases for natural-sounding EQ curves");
    addAndMakeVisible(qCoupleModeSelector.get());
    qCoupleModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::qCoupleMode, *qCoupleModeSelector);

    // EQ Type selector (Digital includes per-band dynamics capability)
    eqTypeSelector = std::make_unique<juce::ComboBox>();
    eqTypeSelector->addItemList({"Digital", "Match", "British", "Tube"}, 1);
    eqTypeSelector->setTooltip("EQ mode: Digital (modern parametric), Match (spectrum matching), British (classic console), Tube (vintage tube)");
    addAndMakeVisible(eqTypeSelector.get());
    eqTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::eqType, *eqTypeSelector);

    // Cross-mode transfer button (transfers British/Tube curve to Digital mode bands)
    transferToDigitalButton.setTooltip("Transfer current EQ curve to Digital mode bands");
    transferToDigitalButton.onClick = [this]() { processor.transferCurrentEQToDigital(); };
    addAndMakeVisible(transferToDigitalButton);

    // Factory preset selector (Digital mode)
    presetSelector = std::make_unique<juce::ComboBox>();
    presetSelector->setTooltip("Factory and user presets");
    updatePresetSelector();
    presetSelector->onChange = [this]() { onPresetSelected(); };
    addAndMakeVisible(presetSelector.get());

    // Save preset button
    savePresetButton.setButtonText("Save");
    savePresetButton.setTooltip("Save current settings as a user preset");
    savePresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a5a8a));
    savePresetButton.onClick = [this]() { saveUserPreset(); };
    addAndMakeVisible(savePresetButton);

    // Undo/Redo buttons
    undoButton.setButtonText("↶");  // Unicode undo arrow
    undoButton.setTooltip("Undo (Cmd/Ctrl+Z)");
    undoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a4a5a));
    undoButton.onClick = [this]() {
        processor.getUndoManager().undo();
        updateUndoRedoButtons();
    };
    addAndMakeVisible(undoButton);

    redoButton.setButtonText("↷");  // Unicode redo arrow
    redoButton.setTooltip("Redo (Cmd/Ctrl+Shift+Z)");
    redoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a4a5a));
    redoButton.onClick = [this]() {
        processor.getUndoManager().redo();
        updateUndoRedoButtons();
    };
    addAndMakeVisible(redoButton);

    // Digital mode A/B comparison button
    digitalAbButton.setButtonText("A");
    digitalAbButton.setTooltip("A/B Comparison: Click to switch between two settings");
    digitalAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));
    digitalAbButton.onClick = [this]() { toggleDigitalAB(); };
    addAndMakeVisible(digitalAbButton);

    // Analyzer controls
    analyzerButton = std::make_unique<juce::ToggleButton>("Analyzer");
    analyzerButton->setTooltip("Show/hide real-time FFT spectrum analyzer (Shortcut: H)");
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
    analyzerModeSelector->setTooltip("Analyzer mode: Peak (fast transients) or RMS (average level)");
    addAndMakeVisible(analyzerModeSelector.get());
    analyzerModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::analyzerMode, *analyzerModeSelector);

    analyzerResolutionSelector = std::make_unique<juce::ComboBox>();
    analyzerResolutionSelector->addItemList({"Low", "Medium", "High"}, 1);
    analyzerResolutionSelector->setTooltip("FFT resolution: Higher = more frequency detail, more CPU");
    addAndMakeVisible(analyzerResolutionSelector.get());
    analyzerResolutionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::analyzerResolution, *analyzerResolutionSelector);

    // Spectrum smoothing selector
    analyzerSmoothingSelector = std::make_unique<juce::ComboBox>();
    analyzerSmoothingSelector->addItemList({"Off", "Light", "Medium", "Heavy"}, 1);
    analyzerSmoothingSelector->setTooltip("Spectrum smoothing: Smoother appearance with slower response");
    analyzerSmoothingSelector->onChange = [this]() {
        int idx = analyzerSmoothingSelector->getSelectedItemIndex();
        if (idx < 0 || idx > static_cast<int>(FFTAnalyzer::SmoothingMode::Heavy))
            return;
        auto mode = static_cast<FFTAnalyzer::SmoothingMode>(idx);
        if (graphicDisplay)
            graphicDisplay->setAnalyzerSmoothingMode(mode);
        if (britishCurveDisplay)
            britishCurveDisplay->setAnalyzerSmoothingMode(mode);
    };
    addAndMakeVisible(analyzerSmoothingSelector.get());
    analyzerSmoothingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::analyzerSmoothing, *analyzerSmoothingSelector);

    analyzerDecaySlider = std::make_unique<DuskSlider>(juce::Slider::LinearHorizontal,
                                                          juce::Slider::TextBoxRight);
    analyzerDecaySlider->setTextValueSuffix(" dB/s");
    analyzerDecaySlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    analyzerDecaySlider->setTooltip("Analyzer decay rate: How fast peaks fall (3-60 dB/s)");
    addAndMakeVisible(analyzerDecaySlider.get());
    analyzerDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::analyzerDecay, *analyzerDecaySlider);

    displayScaleSelector = std::make_unique<juce::ComboBox>();
    displayScaleSelector->addItemList({"+/-12 dB", "+/-24 dB", "+/-30 dB", "+/-60 dB", "Warped"}, 1);
    displayScaleSelector->setTooltip("Display scale: Range of visible gain (+/-12 to +/-60 dB)");
    displayScaleSelector->onChange = [this]() {
        int index = displayScaleSelector->getSelectedItemIndex();
        if (index < 0)
            return;
        auto mode = static_cast<DisplayScaleMode>(index);
        if (graphicDisplay)
            graphicDisplay->setDisplayScaleMode(mode);
        // Also update British mode display scale (uses equivalent enum)
        if (britishCurveDisplay)
            britishCurveDisplay->setDisplayScaleMode(static_cast<BritishDisplayScaleMode>(mode));
    };
    addAndMakeVisible(displayScaleSelector.get());
    displayScaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::displayScaleMode, *displayScaleSelector);

    // Sync initial display scale mode for both Digital and British displays
    int initialIndex = displayScaleSelector->getSelectedItemIndex();
    if (initialIndex >= 0)
    {
        auto initialMode = static_cast<DisplayScaleMode>(initialIndex);
        graphicDisplay->setDisplayScaleMode(initialMode);
        if (britishCurveDisplay)
            britishCurveDisplay->setDisplayScaleMode(static_cast<BritishDisplayScaleMode>(initialMode));
    }
    // Sync initial analyzer visibility for both Digital and British mode displays
    auto* analyzerParam = processor.parameters.getRawParameterValue(ParamIDs::analyzerEnabled);
    if (analyzerParam)
    {
        bool analyzerVisible = analyzerParam->load() > 0.5f;
        graphicDisplay->setAnalyzerVisible(analyzerVisible);
        if (britishCurveDisplay)
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

    // Setup Tube EQ/Tube mode controls
    setupTubeEQControls();

    // Setup Dynamic EQ mode controls
    setupDynamicControls();

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

        // Some hosts (particularly on Linux/X11) process resize requests asynchronously,
        // which can cause the layout to use stale bounds. Schedule a deferred relayout.
        auto safeThis = juce::Component::SafePointer<MultiQEditor>(this);
        juce::MessageManager::callAsync([safeThis, newHeight]() {
            if (safeThis != nullptr)
            {
                if (safeThis->getHeight() != newHeight)
                    safeThis->setSize(safeThis->getWidth(), newHeight);
                safeThis->resized();
                safeThis->repaint();
            }
        });
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
    britishAutoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::autoGainEnabled, *britishAutoGainButton);

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
    oversamplingSelector.addItem("Oversample: 4x", 3);
    oversamplingSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    oversamplingSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    oversamplingSelector.setTooltip("Oversampling: Higher = better quality but more CPU. 4x recommended for analog modes.");
    addAndMakeVisible(oversamplingSelector);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::hqEnabled, oversamplingSelector);

    // Setup Tube mode header controls (A/B, Preset, HQ)
    tubeAbButton.setButtonText("A");
    tubeAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));  // Green for A
    tubeAbButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    tubeAbButton.onClick = [this]() { toggleAB(); };
    tubeAbButton.setTooltip("A/B Comparison: Click to switch between two settings");
    tubeAbButton.setVisible(false);
    addAndMakeVisible(tubeAbButton);

    // Tube mode preset selector
    tubePresetSelector.addItem("Default", 1);
    tubePresetSelector.addItem("Warm Vocal", 2);
    tubePresetSelector.addItem("Vintage Bass", 3);
    tubePresetSelector.addItem("Silky Highs", 4);
    tubePresetSelector.addItem("Full Mix", 5);
    tubePresetSelector.addItem("Subtle Warmth", 6);
    tubePresetSelector.addItem("Mastering", 7);
    tubePresetSelector.setSelectedId(1);
    tubePresetSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    tubePresetSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    tubePresetSelector.setVisible(false);
    tubePresetSelector.onChange = [this]() { applyTubePreset(tubePresetSelector.getSelectedId()); };
    addAndMakeVisible(tubePresetSelector);

    tubeHqButton = std::make_unique<juce::ToggleButton>("HQ");
    tubeHqButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a5058));
    tubeHqButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    tubeHqButton->setTooltip("Enable oversampling for high-quality processing");
    tubeHqButton->setVisible(false);
    addAndMakeVisible(tubeHqButton.get());
    // Note: hqEnabled is now a Choice parameter, not Bool - no ButtonAttachment

    // Tube mode curve collapse button
    tubeEQCurveCollapseButton.setButtonText("Hide Graph");
    tubeEQCurveCollapseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
    tubeEQCurveCollapseButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa0a0a0));
    tubeEQCurveCollapseButton.setTooltip("Show/Hide frequency response graph");
    tubeEQCurveCollapseButton.onClick = [this]() {
        tubeEQCurveCollapsed = !tubeEQCurveCollapsed;
        tubeEQCurveCollapseButton.setButtonText(tubeEQCurveCollapsed ? "Show Graph" : "Hide Graph");

        if (tubeEQCurveDisplay)
            tubeEQCurveDisplay->setVisible(!tubeEQCurveCollapsed && isTubeEQMode);

        int newHeight = tubeEQCurveCollapsed ? 640 : 750;
        setSize(getWidth(), newHeight);

        // Some hosts (particularly on Linux/X11) process resize requests asynchronously,
        // which can cause the layout to use stale bounds. Schedule a deferred relayout.
        auto safeThis = juce::Component::SafePointer<MultiQEditor>(this);
        juce::MessageManager::callAsync([safeThis, newHeight]() {
            if (safeThis != nullptr)
            {
                if (safeThis->getHeight() != newHeight)
                    safeThis->setSize(safeThis->getWidth(), newHeight);
                safeThis->resized();
                safeThis->repaint();
            }
        });
    };
    tubeEQCurveCollapseButton.setVisible(false);
    addAndMakeVisible(tubeEQCurveCollapseButton);

    // Add parameter listeners
    processor.parameters.addParameterListener(ParamIDs::analyzerEnabled, this);
    processor.parameters.addParameterListener(ParamIDs::eqType, this);
    processor.parameters.addParameterListener(ParamIDs::britishMode, this);  // For Brown/Black badge update

    // Check initial EQ mode and update visibility
    auto* eqTypeParam = processor.parameters.getRawParameterValue(ParamIDs::eqType);
    if (eqTypeParam)
    {
        // EQType: 0=Digital, 1=Match, 2=British, 3=Tube EQ
        int eqTypeIndex = static_cast<int>(eqTypeParam->load());
        isMatchMode = (eqTypeIndex == static_cast<int>(EQType::Match));
        isBritishMode = (eqTypeIndex == static_cast<int>(EQType::British));
        isTubeEQMode = (eqTypeIndex == static_cast<int>(EQType::Tube));
    }
    if (bandDetailPanel)
        bandDetailPanel->setMatchMode(isMatchMode);
    updateEQModeVisibility();

    // Initialize resizable UI using shared helper (handles size persistence)
    // Default: 1050x700, Min: 1050x550, Max: 3840x2160 (supports up to 4K displays)
    // Minimum width 1050px prevents toolbar control overlap in Digital mode
    // (left controls end at x=314, right controls start at getWidth()-705)
    resizeHelper.initialize(this, &processor, 1050, 700, 1050, 550, 3840, 2160, false);
    setSize(resizeHelper.getStoredWidth(), resizeHelper.getStoredHeight());

    // Enable keyboard focus for shortcuts (1-8 select bands, D toggle dynamics, etc.)
    setWantsKeyboardFocus(true);

    // Start timer for meter updates
    startTimerHz(30);
}

MultiQEditor::~MultiQEditor()
{
    // Save window size for next session
    resizeHelper.saveSize();

    stopTimer();
    processor.parameters.removeParameterListener(ParamIDs::analyzerEnabled, this);
    processor.parameters.removeParameterListener(ParamIDs::eqType, this);
    processor.parameters.removeParameterListener(ParamIDs::britishMode, this);

    // Clear LookAndFeel references from child components before member LnF objects are destroyed.
    // (Declaration order already ensures safe destruction, but explicit cleanup is defensive.)
    auto clearLnF = [](juce::Component* c) { if (c) c->setLookAndFeel(nullptr); };
    // British mode controls (fourKLookAndFeel)
    clearLnF(britishHpfFreqSlider.get());
    clearLnF(britishLpfFreqSlider.get());
    clearLnF(britishLfGainSlider.get());
    clearLnF(britishLfFreqSlider.get());
    clearLnF(britishLmGainSlider.get());
    clearLnF(britishLmFreqSlider.get());
    clearLnF(britishLmQSlider.get());
    clearLnF(britishHmGainSlider.get());
    clearLnF(britishHmFreqSlider.get());
    clearLnF(britishHmQSlider.get());
    clearLnF(britishHfGainSlider.get());
    clearLnF(britishHfFreqSlider.get());
    clearLnF(britishSaturationSlider.get());
    clearLnF(britishInputGainSlider.get());
    clearLnF(britishOutputGainSlider.get());
    clearLnF(britishHpfEnableButton.get());
    clearLnF(britishLpfEnableButton.get());
    clearLnF(britishLfBellButton.get());
    clearLnF(britishHfBellButton.get());
    // Tube EQ mode controls (vintageTubeLookAndFeel)
    clearLnF(tubeEQLfBoostSlider.get());
    clearLnF(tubeEQLfFreqSelector.get());
    clearLnF(tubeEQLfAttenSlider.get());
    clearLnF(tubeEQHfBoostSlider.get());
    clearLnF(tubeEQHfBoostFreqSelector.get());
    clearLnF(tubeEQHfBandwidthSlider.get());
    clearLnF(tubeEQHfAttenSlider.get());
    clearLnF(tubeEQHfAttenFreqSelector.get());
    clearLnF(tubeEQInputGainSlider.get());
    clearLnF(tubeEQOutputGainSlider.get());
    clearLnF(tubeEQTubeDriveSlider.get());
    clearLnF(tubeEQMidEnabledButton.get());
    clearLnF(tubeEQMidLowFreqSelector.get());
    clearLnF(tubeEQMidLowPeakSlider.get());
    clearLnF(tubeEQMidDipFreqSelector.get());
    clearLnF(tubeEQMidDipSlider.get());
    clearLnF(tubeEQMidHighFreqSelector.get());
    clearLnF(tubeEQMidHighPeakSlider.get());

    setLookAndFeel(nullptr);
}

void MultiQEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xFF1a1a1a));

    auto bounds = getLocalBounds();

    // ===== UNIFIED HEADER FOR ALL MODES =====
    // Consistent 50px header with gradient background
    juce::ColourGradient headerGradient(
        juce::Colour(0xff2a2a2a), 0, 0,
        juce::Colour(0xff1f1f1f), 0, 50, false);
    g.setGradientFill(headerGradient);
    g.fillRect(0, 0, bounds.getWidth(), 50);

    // Header bottom border
    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillRect(0, 49, bounds.getWidth(), 1);

    // Plugin title (clickable - shows supporters panel)
    // Position after EQ type selector (which is at x=15, width=80)
    titleClickArea = juce::Rectangle<int>(100, 8, 150, 35);
    g.setFont(juce::Font(juce::FontOptions(22.0f).withStyle("Bold")));
    g.setColour(juce::Colour(0xffe8e8e8));
    g.drawText("Multi-Q", 100, 8, 150, 26, juce::Justification::left);

    // Mode-specific subtitle
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.setColour(juce::Colour(0xff808080));
    juce::String subtitle = isTubeEQMode ? "Tube EQ" : (isBritishMode ? "Console EQ" : (isMatchMode ? "Match EQ" : "Universal EQ"));
    g.drawText(subtitle, 100, 32, 120, 14, juce::Justification::left);

    // Dusk Audio branding (right side)
    g.setColour(juce::Colour(0xff606060));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("Dusk Audio", getWidth() - 100, 32, 90, 14, juce::Justification::centredRight);

    // ===== MODE-SPECIFIC TOOLBAR ROW (below header) =====
    if (isTubeEQMode)
    {
        // Tube mode: Darker blue-gray toolbar background
        g.setColour(juce::Colour(0xff2a3a40));
        g.fillRect(0, 50, bounds.getWidth(), 38);
        g.setColour(juce::Colour(0xff3a4a50));
        g.fillRect(0, 87, bounds.getWidth(), 1);

        // Dark blue-gray background for content area (below toolbar)
        g.setColour(juce::Colour(0xff31444b));
        g.fillRect(0, 88, bounds.getWidth(), bounds.getHeight() - 88);
    }
    else if (isBritishMode)
    {
        // British mode: Dark toolbar background
        g.setColour(juce::Colour(0xff222222));
        g.fillRect(0, 50, bounds.getWidth(), 38);
        g.setColour(juce::Colour(0xff3a3a3a));
        g.fillRect(0, 87, bounds.getWidth(), 1);
    }
    else
    {
        // Digital mode: Dark toolbar background
        g.setColour(juce::Colour(0xff1c1c1c));
        g.fillRect(0, 50, bounds.getWidth(), 38);
        g.setColour(juce::Colour(0xff333333));
        g.fillRect(0, 87, bounds.getWidth(), 1);
    }

    if (isTubeEQMode)
    {
        // ===== TUBE EQ MODE PAINT - SECTION DIVIDERS =====
        // Calculate positions based on layout (must match layoutTubeEQControls)
        const int headerHeight = tubeEQCurveCollapsed ? 88 : 200;
        const int labelHeight = 22;
        const int knobSize = 105;            // Must match layoutTubeEQControls
        const int smallKnobSize = 90;        // Row 3 knobs
        const int bottomMargin = 35;         // Must match layoutTubeEQControls
        const int rightPanelWidth = 125;
        const int meterReserve = 40;     // Must match layoutTubeEQControls

        int row1Height = knobSize + labelHeight;
        int row2Height = labelHeight + knobSize + 10;  // Frequency row with separators
        int row3Height = smallKnobSize + labelHeight;

        int totalContentHeight = row1Height + row2Height + row3Height;
        int availableHeight = getHeight() - headerHeight - bottomMargin;
        int extraSpace = availableHeight - totalContentHeight;
        int rowGap = juce::jmax(5, extraSpace / 4);

        int row1Y = headerHeight + rowGap;
        int row2Y = row1Y + row1Height + rowGap;
        int row3Y = row2Y + row2Height + rowGap;

        int mainWidth = getWidth() - rightPanelWidth - meterReserve;
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

        // Draw meter labels (INPUT / OUTPUT) for Tube EQ mode
        if (inputMeter && inputMeter->isVisible())
        {
            float inL = processor.inputLevelL.load();
            float inR = processor.inputLevelR.load();
            float inputLevel = juce::jmax(inL, inR);
            LEDMeterStyle::drawMeterLabels(g, inputMeter->getBounds(), "INPUT", inputLevel);
        }

        if (outputMeter && outputMeter->isVisible())
        {
            float outL = processor.outputLevelL.load();
            float outR = processor.outputLevelR.load();
            float outputLevel = juce::jmax(outL, outR);
            LEDMeterStyle::drawMeterLabels(g, outputMeter->getBounds(), "OUTPUT", outputLevel);
        }
    }
    else if (isBritishMode)
    {
        // ===== BRITISH MODE PAINT (4K-EQ style content area) =====
        // Draw section dividers and headers like 4K-EQ

        // Adjust content area based on curve visibility (like 4K-EQ)
        // Header (50) + toolbar (38) = 88, curve is 105px when visible
        int contentTop = britishCurveCollapsed ? 95 : 200;  // Move up when curve is hidden
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

        // Draw tick marks and value labels around knobs
        drawBritishKnobMarkings(g);

        // Knob labels are drawn in paintOverChildren() to ensure they appear on top
    }
    else
    {
        // ===== DIGITAL MODE PAINT (Waves F6 style layout) =====
        // Constants matching resized() layout
        int detailPanelHeight = 125;  // Controls area with 75px knobs + section headers
        int toolbarHeight = 88;  // Header (50) + toolbar (38)
        int meterWidth = 28;
        int meterPadding = 8;
        int meterAreaWidth = meterWidth + meterPadding * 2;

        // ===== METER AREAS =====
        // Left meter area (input)
        auto leftMeterArea = juce::Rectangle<int>(
            0, toolbarHeight,
            meterAreaWidth, getHeight() - toolbarHeight - detailPanelHeight);

        // Right meter area (output)
        auto rightMeterArea = juce::Rectangle<int>(
            getWidth() - meterAreaWidth, toolbarHeight,
            meterAreaWidth, getHeight() - toolbarHeight - detailPanelHeight);

        // Draw meter backgrounds
        g.setColour(juce::Colour(0xFF161618));
        g.fillRect(leftMeterArea);
        g.fillRect(rightMeterArea);

        // ===== METER LABELS (inside meter area, at top) =====
        g.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xFF808088));
        g.drawText("IN", leftMeterArea.getX(), toolbarHeight + 3, meterAreaWidth, 14, juce::Justification::centred);
        g.drawText("OUT", rightMeterArea.getX(), toolbarHeight + 3, meterAreaWidth, 14, juce::Justification::centred);
    }

    // Separator line only for digital mode
    if (!isBritishMode && !isTubeEQMode)
    {
        g.setColour(juce::Colour(0xFF333333));
        g.drawHorizontalLine(50, 0, static_cast<float>(getWidth()));

        // ===== FOOTER BAR - Centered band selection indicator =====
        int footerHeight = 28;
        int footerY = getHeight() - footerHeight;

        // Footer background
        g.setColour(juce::Colour(0xFF151517));
        g.fillRect(0, footerY, getWidth(), footerHeight);

        // Top border of footer
        g.setColour(juce::Colour(0xFF2a2a2c));
        g.drawHorizontalLine(footerY, 0, static_cast<float>(getWidth()));

        // Check if a band is selected
        if (selectedBand >= 0 && selectedBand < 8)
        {
            // Get band info for display
            const char* bandTypeNames[] = {"HPF", "Low Shelf", "Para 1", "Para 2", "Para 3", "Para 4", "High Shelf", "LPF"};

            // Draw centered band indicator: "Band 3 - Para 1"
            juce::String bandText = "Band " + juce::String(selectedBand + 1) + " - " + bandTypeNames[selectedBand];

            // Small color indicator dot
            int dotSize = 10;
            int textWidth = 150;
            int totalWidth = dotSize + 8 + textWidth;
            int startX = (getWidth() - totalWidth) / 2;

            g.setColour(DefaultBandConfigs[selectedBand].color);
            float dotY = static_cast<float>(footerY) + (static_cast<float>(footerHeight) - static_cast<float>(dotSize)) / 2.0f;
            g.fillEllipse(static_cast<float>(startX), dotY, static_cast<float>(dotSize), static_cast<float>(dotSize));

            g.setColour(juce::Colour(0xFF909090));
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(bandText, startX + dotSize + 8, footerY, textWidth, footerHeight, juce::Justification::centredLeft);
        }
        else
        {
            // No band selected - show hint
            g.setColour(juce::Colour(0xFF606060));
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText("Click a band node to edit", 0, footerY, getWidth(), footerHeight, juce::Justification::centred);
        }
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

    // ===== CLIP INDICATORS (all modes with visible meters) =====
    if (!isTubeEQMode)
    {
        bool inClip = processor.inputClipped.load(std::memory_order_relaxed);
        bool outClip = processor.outputClipped.load(std::memory_order_relaxed);
        drawClipIndicator(g, inputClipBounds, inClip);
        drawClipIndicator(g, outputClipBounds, outClip);
    }
}

void MultiQEditor::drawClipIndicator(juce::Graphics& g, juce::Rectangle<int> bounds, bool clipped)
{
    if (bounds.isEmpty())
        return;

    if (clipped)
    {
        // Red glow background
        g.setColour(juce::Colour(0x40ff0000));
        g.fillRoundedRectangle(bounds.toFloat().expanded(1.0f), 3.0f);

        // Bright red indicator
        g.setColour(juce::Colour(0xFFff2222));
        g.fillRoundedRectangle(bounds.toFloat(), 2.0f);

        // "CLIP" text
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(8.0f).withStyle("Bold")));
        g.drawText("CLIP", bounds, juce::Justification::centred);
    }
    else
    {
        // Dark unlit indicator
        g.setColour(juce::Colour(0xFF2a2a2c));
        g.fillRoundedRectangle(bounds.toFloat(), 2.0f);
    }
}

void MultiQEditor::resized()
{
    // Update the resize helper (positions corner handle, calculates scale factor)
    resizeHelper.updateResizer();

    // ===== UNIFIED TOOLBAR =====
    // Position all shared toolbar controls (A/B, Preset, BYPASS, OVS, Scale)
    // FIRST, before mode-specific layouts, to ensure consistent positioning
    layoutUnifiedToolbar();

    auto bounds = getLocalBounds();

    if (isTubeEQMode)
    {
        // ===== VINTAGE TUBE EQ MODE LAYOUT =====
        // Toolbar is handled by layoutUnifiedToolbar() above

        // Position Tube EQ curve display (full width, below toolbar)
        if (tubeEQCurveDisplay && !tubeEQCurveCollapsed)
        {
            int curveY = 88;
            int curveX = 0;
            int curveWidth = getWidth();
            int curveHeight = 105;
            tubeEQCurveDisplay->setBounds(curveX, curveY, curveWidth, curveHeight);
        }

        // Layout Tube EQ specific controls
        layoutTubeEQControls();

        // Hide the tube HQ button (replaced by global oversampling selector)
        if (tubeHqButton)
            tubeHqButton->setVisible(false);

        // Position meters in Tube EQ mode
        // Labels ("INPUT"/"OUTPUT") drawn 20px above meter by drawMeterLabels
        // When graph visible, curve bottom = 88+105 = 193, so meterY must be >= 215
        {
            int meterY = tubeEQCurveCollapsed ? 110 : 215;
            int meterWidth = LEDMeterStyle::standardWidth;
            int meterHeight = getHeight() - meterY - LEDMeterStyle::valueHeight - LEDMeterStyle::labelSpacing - 6;
            inputMeter->setBounds(4, meterY, meterWidth, meterHeight);
            outputMeter->setBounds(getWidth() - meterWidth - 4, meterY, meterWidth, meterHeight);
            inputClipBounds = {};
            outputClipBounds = {};
        }

        // Hide Digital mode toolbar controls in Tube EQ mode
        hqButton->setVisible(false);
    }
    else if (isBritishMode)
    {
        // ===== BRITISH MODE LAYOUT =====
        // Toolbar is handled by layoutUnifiedToolbar() above
        // Just layout British-specific content here

        // Hide Digital mode controls
        hqButton->setVisible(false);

        // Calculate curve display height based on collapsed state
        // Curve starts below toolbar (y=88) and is 105px tall
        int curveHeight = britishCurveCollapsed ? 0 : 105;
        int curveY = 88;

        // Position British EQ curve display (full width)
        if (britishCurveDisplay && !britishCurveCollapsed)
        {
            int curveX = 0;
            int curveWidth = getWidth();
            britishCurveDisplay->setBounds(curveX, curveY, curveWidth, curveHeight);
        }

        // Adjust meter and content positions based on curve visibility
        // Labels ("INPUT"/"OUTPUT") drawn 20px above meter by drawMeterLabels
        // When graph visible, curve bottom = 88+105 = 193, so meterY must be >= 193+20+2 = 215
        int meterY = britishCurveCollapsed ? 110 : 215;
        int meterWidth = LEDMeterStyle::standardWidth;
        // Leave room below for value text (drawn by drawMeterLabels): valueHeight(20) + labelSpacing(4) + margin(6)
        int meterHeight = getHeight() - meterY - LEDMeterStyle::valueHeight - LEDMeterStyle::labelSpacing - 6;
        inputMeter->setBounds(6, meterY, meterWidth, meterHeight);
        outputMeter->setBounds(getWidth() - meterWidth - 10, meterY, meterWidth, meterHeight);
        // No clip indicators in British mode (would overlap drawMeterLabels value text)
        inputClipBounds = {};
        outputClipBounds = {};

        // Main content area (between meters) - adjusted based on curve visibility
        int contentLeft = 45;
        int contentRight = getWidth() - 45;
        int contentWidth = contentRight - contentLeft;
        int contentTop = britishCurveCollapsed ? 95 : 200;

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

        // Knob sizes and dynamic row spacing (adapts to actual window height)
        int knobSize = 75;  // Larger knobs
        int sectionLabelHeight = 30;
        int knobLabelHeight = 18;
        int knobLabelOffset = knobSize / 2 + 40;  // Position label below knob
        int btnHeight = 25;
        int bottomMargin = 30;

        // Per-row visual height: from knob top to label bottom
        int rowVisualHeight = knobLabelOffset + knobLabelHeight;  // 95
        int totalContentHeight = sectionLabelHeight + 3 * rowVisualHeight;
        int availableHeight = getHeight() - contentTop - bottomMargin;
        int extraSpace = availableHeight - totalContentHeight;
        int rowGap = juce::jmax(5, extraSpace / 4);

        int labelY = contentTop + 5;
        int row1Y = contentTop + sectionLabelHeight + rowGap;
        int row2Y = row1Y + rowVisualHeight + rowGap;
        int row3Y = row2Y + rowVisualHeight + rowGap;

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
        // ===== DIGITAL MODE LAYOUT =====
        // Toolbar is handled by layoutUnifiedToolbar() above
        // Just layout Digital-specific content here

        // Header (title area only)
        bounds.removeFromTop(50);

        // Toolbar row
        bounds.removeFromTop(38);

        // Hide old HQ button (replaced by global oversampling selector)
        hqButton->setVisible(false);

        // Hide old selected band controls (replaced by BandDetailPanel)
        selectedBandLabel.setVisible(false);
        freqSlider->setVisible(false);
        gainSlider->setVisible(false);
        qSlider->setVisible(false);
        slopeSelector->setVisible(false);
        freqLabel.setVisible(false);
        gainLabel.setVisible(false);
        qLabel.setVisible(false);
        slopeLabel.setVisible(false);

        // Hide bottom control bar elements (moved to toolbar or removed for cleaner F6 style)
        masterGainLabel.setVisible(false);
        masterGainSlider->setVisible(false);
        qCoupleModeSelector->setVisible(false);
        analyzerButton->setVisible(false);
        analyzerPrePostButton->setVisible(false);
        analyzerModeSelector->setVisible(false);
        analyzerResolutionSelector->setVisible(false);
        analyzerDecaySlider->setVisible(false);

        // ===== BAND DETAIL PANEL (F6-style band selector + knob controls) =====
        int detailPanelHeight = 125;  // More room for section headers and larger band indicator
        auto detailPanelArea = bounds.removeFromBottom(detailPanelHeight);
        bandDetailPanel->setBounds(detailPanelArea);
        bandDetailPanel->setVisible(true);
        bandDetailPanel->setSelectedBand(selectedBand);

        // ===== METERS ON SIDES =====
        int meterWidth = 28;  // Wider meters for better visibility
        int meterPadding = 8;
        int labelOffset = 18;  // Space for IN/OUT labels above meters

        // Input meter on left side
        int clipHeight = 12;
        int clipGap = 2;
        auto leftMeterArea = bounds.removeFromLeft(meterWidth + meterPadding * 2);
        int meterH = bounds.getHeight() - labelOffset - 5 - clipHeight - clipGap;
        inputMeter->setBounds(leftMeterArea.getX() + meterPadding,
                              bounds.getY() + labelOffset,
                              meterWidth,
                              meterH);
        inputClipBounds = juce::Rectangle<int>(
            leftMeterArea.getX() + meterPadding,
            bounds.getY() + labelOffset + meterH + clipGap,
            meterWidth, clipHeight);

        // Output meter on right side
        auto rightMeterArea = bounds.removeFromRight(meterWidth + meterPadding * 2);
        outputMeter->setBounds(rightMeterArea.getX() + meterPadding,
                               bounds.getY() + labelOffset,
                               meterWidth,
                               meterH);
        outputClipBounds = juce::Rectangle<int>(
            rightMeterArea.getX() + meterPadding,
            bounds.getY() + labelOffset + meterH + clipGap,
            meterWidth, clipHeight);
    }

    // Graphic display (main area) - only in Digital mode
    if (!isBritishMode && !isTubeEQMode)
    {
        auto displayBounds = bounds.reduced(10, 5);
        graphicDisplay->setBounds(displayBounds);
    }

    // Dynamic controls layout in Digital mode (per-band dynamics)
    bool isDigitalStyleMode = !isBritishMode && !isTubeEQMode;
    if (isDigitalStyleMode)
        layoutDynamicControls();

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

    // Update clip indicators — repaint only when state changes
    bool inClip = processor.inputClipped.load(std::memory_order_relaxed);
    bool outClip = processor.outputClipped.load(std::memory_order_relaxed);
    if (inClip != lastInputClipState || outClip != lastOutputClipState)
    {
        lastInputClipState = inClip;
        lastOutputClipState = outClip;
        repaint(inputClipBounds);
        repaint(outputClipBounds);
    }

    // Update master gain for display overlay
    auto* masterParam = processor.parameters.getRawParameterValue(ParamIDs::masterGain);
    if (masterParam)
        graphicDisplay->setMasterGain(masterParam->load());

    // Update undo/redo button states
    updateUndoRedoButtons();
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
                // EQType: 0=Digital, 1=Match, 2=British, 3=Tube EQ
                safeThis->isMatchMode = (eqTypeIndex == static_cast<int>(EQType::Match));
                safeThis->isBritishMode = (eqTypeIndex == static_cast<int>(EQType::British));
                safeThis->isTubeEQMode = (eqTypeIndex == static_cast<int>(EQType::Tube));
                if (safeThis->bandDetailPanel)
                    safeThis->bandDetailPanel->setMatchMode(safeThis->isMatchMode);
                safeThis->updateEQModeVisibility();

                // Ensure window height meets minimum for current mode
                // Only shrink if necessary — preserve user's larger window size
                int minHeight = 640;
                if (safeThis->isBritishMode)
                    minHeight = safeThis->britishCurveCollapsed ? 530 : 640;
                else if (safeThis->isTubeEQMode)
                    minHeight = safeThis->tubeEQCurveCollapsed ? 640 : 750;

                int currentHeight = safeThis->getHeight();
                if (currentHeight < minHeight)
                    safeThis->setSize(safeThis->getWidth(), minHeight);

                safeThis->resized();
                safeThis->repaint();
            }
        });
    }
    else if (parameterID == ParamIDs::britishMode)
    {
        // Brown/Black mode changed - update button text and color
        const bool isBlack = newValue > 0.5f;
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<MultiQEditor>(this), isBlack]() {
            if (safeThis != nullptr && safeThis->britishModeButton != nullptr)
            {
                // Brown = warm E-Series (tan/brown), Black = surgical G-Series (dark charcoal)
                safeThis->britishModeButton->setButtonText(isBlack ? "Black" : "Brown");
                auto color = isBlack ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff8b6914);
                safeThis->britishModeButton->setColour(juce::TextButton::buttonColourId, color);
                safeThis->britishModeButton->setColour(juce::TextButton::buttonOnColourId, color);
                safeThis->britishModeButton->setColour(juce::TextButton::textColourOffId,
                    isBlack ? juce::Colour(0xffaaaaaa) : juce::Colour(0xffffffff));
            }
        });
    }
}

void MultiQEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
    {
        showSupportersPanel();
    }

    // Click clip indicators to reset
    if (inputClipBounds.contains(e.getPosition()))
    {
        processor.inputClipped.store(false, std::memory_order_relaxed);
        lastInputClipState = false;
        repaint(inputClipBounds);
    }
    if (outputClipBounds.contains(e.getPosition()))
    {
        processor.outputClipped.store(false, std::memory_order_relaxed);
        lastOutputClipState = false;
        repaint(outputClipBounds);
    }
}

bool MultiQEditor::keyPressed(const juce::KeyPress& key)
{
    // Undo/Redo shortcuts work in all modes
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
    {
        processor.getUndoManager().undo();
        updateUndoRedoButtons();
        return true;
    }
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        processor.getUndoManager().redo();
        updateUndoRedoButtons();
        return true;
    }

    // ===== GLOBAL SHORTCUTS (work in all modes) =====

    // B: Toggle bypass
    if (key.isKeyCode('B'))
    {
        auto* param = processor.parameters.getParameter(ParamIDs::bypass);
        if (param)
        {
            float currentValue = param->getValue();
            param->setValueNotifyingHost(currentValue > 0.5f ? 0.0f : 1.0f);
        }
        return true;
    }

    // H: Toggle analyzer
    if (key.isKeyCode('H'))
    {
        auto* param = processor.parameters.getParameter(ParamIDs::analyzerEnabled);
        if (param)
        {
            float currentValue = param->getValue();
            param->setValueNotifyingHost(currentValue > 0.5f ? 0.0f : 1.0f);
        }
        return true;
    }

    // L: Toggle linear phase
    if (key.isKeyCode('L'))
    {
        auto* param = processor.parameters.getParameter(ParamIDs::linearPhaseEnabled);
        if (param)
        {
            float currentValue = param->getValue();
            param->setValueNotifyingHost(currentValue > 0.5f ? 0.0f : 1.0f);
        }
        return true;
    }

    // Q: Cycle Q-coupling mode
    if (key.isKeyCode('Q') && !key.getModifiers().isShiftDown())
    {
        auto* param = processor.parameters.getParameter(ParamIDs::qCoupleMode);
        if (param)
        {
            // 9 modes: Off, Proportional, Light, Medium, Strong, Asym Light, Asym Medium, Asym Strong, Vintage
            int currentMode = static_cast<int>(param->getValue() * 8.0f + 0.5f);
            int nextMode = (currentMode + 1) % 9;
            param->setValueNotifyingHost(nextMode / 8.0f);
        }
        return true;
    }

    // M: Cycle processing mode (Stereo/Left/Right/Mid/Side)
    if (key.isKeyCode('M'))
    {
        auto* param = processor.parameters.getParameter(ParamIDs::processingMode);
        if (param)
        {
            // 5 modes: Stereo, Left, Right, Mid, Side
            int currentMode = static_cast<int>(param->getValue() * 4.0f + 0.5f);
            int nextMode = (currentMode + 1) % 5;
            param->setValueNotifyingHost(nextMode / 4.0f);
        }
        return true;
    }

    // Cmd+0: Reset window to default size
    if (key == juce::KeyPress('0', juce::ModifierKeys::commandModifier, 0))
    {
        setSize(1050, 700);
        return true;
    }

    // F: Toggle spectrum freeze (visual reference)
    if (key.isKeyCode('F'))
    {
        if (isBritishMode && britishCurveDisplay)
            britishCurveDisplay->toggleSpectrumFreeze();
        else if (graphicDisplay)
            graphicDisplay->toggleSpectrumFreeze();
        return true;
    }

    // ===== DIGITAL MODE ONLY SHORTCUTS =====
    if (isBritishMode || isTubeEQMode)
        return false;

    // Shift+1-8: Toggle band enable (without changing selection)
    if (key.getModifiers().isShiftDown())
    {
        int bandToToggle = -1;
        if (key.isKeyCode('1') || key.isKeyCode(juce::KeyPress::numberPad1)) bandToToggle = 0;
        else if (key.isKeyCode('2') || key.isKeyCode(juce::KeyPress::numberPad2)) bandToToggle = 1;
        else if (key.isKeyCode('3') || key.isKeyCode(juce::KeyPress::numberPad3)) bandToToggle = 2;
        else if (key.isKeyCode('4') || key.isKeyCode(juce::KeyPress::numberPad4)) bandToToggle = 3;
        else if (key.isKeyCode('5') || key.isKeyCode(juce::KeyPress::numberPad5)) bandToToggle = 4;
        else if (key.isKeyCode('6') || key.isKeyCode(juce::KeyPress::numberPad6)) bandToToggle = 5;
        else if (key.isKeyCode('7') || key.isKeyCode(juce::KeyPress::numberPad7)) bandToToggle = 6;
        else if (key.isKeyCode('8') || key.isKeyCode(juce::KeyPress::numberPad8)) bandToToggle = 7;

        if (bandToToggle >= 0)
        {
            auto* param = processor.parameters.getParameter(ParamIDs::bandEnabled(bandToToggle + 1));
            if (param)
            {
                float currentValue = param->getValue();
                param->setValueNotifyingHost(currentValue > 0.5f ? 0.0f : 1.0f);
            }
            return true;
        }
    }

    // Number keys 1-8: Select corresponding band (without Shift)
    if (!key.getModifiers().isShiftDown())
    {
        if (key.isKeyCode('1') || key.isKeyCode(juce::KeyPress::numberPad1))
        {
            onBandSelected(0);
            return true;
        }
        if (key.isKeyCode('2') || key.isKeyCode(juce::KeyPress::numberPad2))
        {
            onBandSelected(1);
            return true;
        }
        if (key.isKeyCode('3') || key.isKeyCode(juce::KeyPress::numberPad3))
        {
            onBandSelected(2);
            return true;
        }
        if (key.isKeyCode('4') || key.isKeyCode(juce::KeyPress::numberPad4))
        {
            onBandSelected(3);
            return true;
        }
        if (key.isKeyCode('5') || key.isKeyCode(juce::KeyPress::numberPad5))
        {
            onBandSelected(4);
            return true;
        }
        if (key.isKeyCode('6') || key.isKeyCode(juce::KeyPress::numberPad6))
        {
            onBandSelected(5);
            return true;
        }
        if (key.isKeyCode('7') || key.isKeyCode(juce::KeyPress::numberPad7))
        {
            onBandSelected(6);
            return true;
        }
        if (key.isKeyCode('8') || key.isKeyCode(juce::KeyPress::numberPad8))
        {
            onBandSelected(7);
            return true;
        }
    }

    // Shift+Tab: Cycle to previous band
    if (key.isKeyCode(juce::KeyPress::tabKey) && key.getModifiers().isShiftDown())
    {
        int prevBand = (selectedBand < 0) ? 7 : (selectedBand + 7) % 8;
        onBandSelected(prevBand);
        return true;
    }

    // Tab: Cycle to next band
    if (key.isKeyCode(juce::KeyPress::tabKey))
    {
        int nextBand = (selectedBand + 1) % 8;
        onBandSelected(nextBand);
        return true;
    }
    // D: Toggle dynamics for selected band
    if (key.isKeyCode('D') && selectedBand >= 0 && selectedBand < 8)
    {
        auto* param = processor.parameters.getParameter(ParamIDs::bandDynEnabled(selectedBand + 1));
        if (param)
        {
            float currentValue = param->getValue();
            param->setValueNotifyingHost(currentValue > 0.5f ? 0.0f : 1.0f);
        }
        return true;
    }

    // Delete/Backspace: Disable selected band
    if ((key.isKeyCode(juce::KeyPress::deleteKey) || key.isKeyCode(juce::KeyPress::backspaceKey))
        && selectedBand >= 0 && selectedBand < 8)
    {
        auto* param = processor.parameters.getParameter(ParamIDs::bandEnabled(selectedBand + 1));
        if (param)
            param->setValueNotifyingHost(0.0f);
        return true;
    }

    // R: Reset selected band to default
    if (key.isKeyCode('R') && selectedBand >= 0 && selectedBand < 8)
    {
        // Reset gain to 0 dB
        if (auto* gainParam = processor.parameters.getParameter(ParamIDs::bandGain(selectedBand + 1)))
            gainParam->setValueNotifyingHost(gainParam->getDefaultValue());

        // Reset Q to default
        if (auto* qParam = processor.parameters.getParameter(ParamIDs::bandQ(selectedBand + 1)))
            qParam->setValueNotifyingHost(qParam->getDefaultValue());

        return true;
    }

    // E: Enable/toggle selected band
    if (key.isKeyCode('E') && selectedBand >= 0 && selectedBand < 8)
    {
        auto* param = processor.parameters.getParameter(ParamIDs::bandEnabled(selectedBand + 1));
        if (param)
        {
            float currentValue = param->getValue();
            param->setValueNotifyingHost(currentValue > 0.5f ? 0.0f : 1.0f);
        }
        return true;
    }

    // S: Toggle solo for selected band
    if (key.isKeyCode('S') && selectedBand >= 0 && selectedBand < 8)
    {
        // Toggle solo: if this band is already soloed, turn off solo; otherwise solo it
        if (processor.isBandSoloed(selectedBand))
            processor.setSoloedBand(-1);  // Turn off solo
        else
            processor.setSoloedBand(selectedBand);  // Solo this band

        // Update the BandDetailPanel's solo button state
        if (bandDetailPanel)
            bandDetailPanel->setSelectedBand(selectedBand);  // Refreshes button state

        return true;
    }

    // A: Toggle A/B comparison
    if (key.isKeyCode('A'))
    {
        toggleDigitalAB();
        return true;
    }

    // Arrow keys: Adjust selected band parameters
    if (selectedBand >= 0 && selectedBand < 8)
    {
        // Shift modifier for fine control (smaller steps)
        float gainStep = key.getModifiers().isShiftDown() ? 0.5f : 1.0f;
        float freqMultiplier = key.getModifiers().isShiftDown() ? 1.02f : 1.1f;

        // Up arrow: Increase gain
        if (key.isKeyCode(juce::KeyPress::upKey))
        {
            auto* param = processor.parameters.getParameter(ParamIDs::bandGain(selectedBand + 1));
            if (param)
            {
                float currentGain = param->convertFrom0to1(param->getValue());
                float newGain = juce::jlimit(-24.0f, 24.0f, currentGain + gainStep);
                param->setValueNotifyingHost(param->convertTo0to1(newGain));
            }
            return true;
        }

        // Down arrow: Decrease gain
        if (key.isKeyCode(juce::KeyPress::downKey))
        {
            auto* param = processor.parameters.getParameter(ParamIDs::bandGain(selectedBand + 1));
            if (param)
            {
                float currentGain = param->convertFrom0to1(param->getValue());
                float newGain = juce::jlimit(-24.0f, 24.0f, currentGain - gainStep);
                param->setValueNotifyingHost(param->convertTo0to1(newGain));
            }
            return true;
        }

        // Right arrow: Increase frequency
        if (key.isKeyCode(juce::KeyPress::rightKey))
        {
            auto* param = processor.parameters.getParameter(ParamIDs::bandFreq(selectedBand + 1));
            if (param)
            {
                float currentFreq = param->convertFrom0to1(param->getValue());
                float newFreq = juce::jlimit(20.0f, 20000.0f, currentFreq * freqMultiplier);
                param->setValueNotifyingHost(param->convertTo0to1(newFreq));
            }
            return true;
        }

        // Left arrow: Decrease frequency
        if (key.isKeyCode(juce::KeyPress::leftKey))
        {
            auto* param = processor.parameters.getParameter(ParamIDs::bandFreq(selectedBand + 1));
            if (param)
            {
                float currentFreq = param->convertFrom0to1(param->getValue());
                float newFreq = juce::jlimit(20.0f, 20000.0f, currentFreq / freqMultiplier);
                param->setValueNotifyingHost(param->convertTo0to1(newFreq));
            }
            return true;
        }
    }

    return false;
}

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

    juce::String bandName = "Band " + juce::String(selectedBand + 1) + ": " + config.name;
    selectedBandLabel.setText(bandName, juce::dontSendNotification);
    selectedBandLabel.setColour(juce::Label::textColourId, config.color);

    freqSlider->setColour(juce::Slider::rotarySliderFillColourId, config.color);
    gainSlider->setColour(juce::Slider::rotarySliderFillColourId, config.color);
    qSlider->setColour(juce::Slider::rotarySliderFillColourId, config.color);

    lookAndFeel.setSelectedBandColor(config.color);

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
    bandDetailPanel->setSelectedBand(bandIndex);
    updateSelectedBandControls();

    if (!isBritishMode && !isTubeEQMode)
        updateDynamicAttachments();
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

// British Mode UI

void MultiQEditor::setupBritishControls()
{
    auto setupBritishKnob = [this](std::unique_ptr<juce::Slider>& slider, const juce::String& name,
                                   bool centerDetented, juce::Colour color) {
        slider = std::make_unique<DuskSlider>();
        slider->setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider->setPopupDisplayEnabled(true, true, this);
        slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                     juce::MathConstants<float>::pi * 2.75f, true);
        slider->setScrollWheelEnabled(true);
        // DuskSlider already has proper Cmd/Ctrl+drag fine control built-in
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
    britishHpfFreqSlider->setTooltip("High-pass filter frequency (18 dB/oct)");
    setupBritishButton(britishHpfEnableButton, "IN");
    britishHpfEnableButton->setTooltip("Enable high-pass filter");
    setupBritishKnob(britishLpfFreqSlider, "lpf_freq", false, filterColor);
    britishLpfFreqSlider->setTooltip("Low-pass filter frequency (12 dB/oct)");
    setupBritishButton(britishLpfEnableButton, "IN");
    britishLpfEnableButton->setTooltip("Enable low-pass filter");

    // LF Band
    setupBritishKnob(britishLfGainSlider, "lf_gain", true, gainColor);
    britishLfGainSlider->setTooltip("Low frequency gain (-24 to +24 dB)");
    setupBritishKnob(britishLfFreqSlider, "lf_freq", false, freqColor);
    britishLfFreqSlider->setTooltip("Low frequency center/corner frequency");
    setupBritishButton(britishLfBellButton, "BELL");
    britishLfBellButton->setTooltip("Toggle between shelf and bell (peaking) shape");

    // LM Band (orange/goldenrod like 4K-EQ LMF section)
    setupBritishKnob(britishLmGainSlider, "lmf_gain", true, juce::Colour(0xffff8c00));
    britishLmGainSlider->setTooltip("Low-mid frequency gain (-24 to +24 dB)");
    setupBritishKnob(britishLmFreqSlider, "lmf_freq", false, juce::Colour(0xffdaa520));
    britishLmFreqSlider->setTooltip("Low-mid frequency center frequency");
    setupBritishKnob(britishLmQSlider, "lmf_q", false, qColor);
    britishLmQSlider->setTooltip("Low-mid Q: Higher = narrower bandwidth");

    // HM Band (green/cyan like 4K-EQ HMF section)
    setupBritishKnob(britishHmGainSlider, "hmf_gain", true, juce::Colour(0xff28a745));
    britishHmGainSlider->setTooltip("High-mid frequency gain (-24 to +24 dB)");
    setupBritishKnob(britishHmFreqSlider, "hmf_freq", false, juce::Colour(0xff20b2aa));
    britishHmFreqSlider->setTooltip("High-mid frequency center frequency");
    setupBritishKnob(britishHmQSlider, "hmf_q", false, qColor);
    britishHmQSlider->setTooltip("High-mid Q: Higher = narrower bandwidth");

    // HF Band (blue tones like 4K-EQ HF section)
    setupBritishKnob(britishHfGainSlider, "hf_gain", true, juce::Colour(0xff4169e1));
    britishHfGainSlider->setTooltip("High frequency gain (-24 to +24 dB)");
    setupBritishKnob(britishHfFreqSlider, "hf_freq", false, juce::Colour(0xff6495ed));
    britishHfFreqSlider->setTooltip("High frequency center/corner frequency");
    setupBritishButton(britishHfBellButton, "BELL");
    britishHfBellButton->setTooltip("Toggle between shelf and bell (peaking) shape");

    // Global British controls - Brown/Black toggle button with color and text
    britishModeButton = std::make_unique<juce::TextButton>("Brown");
    britishModeButton->setTooltip("Console Mode: Brown (E-Series, warm/musical) / Black (G-Series, clean/surgical)\nClick to toggle");
    britishModeButton->setVisible(false);

    // Helper to update button text and color
    auto updateBritishModeButtonAppearance = [this](bool isBlack) {
        // Brown = warm E-Series (tan/brown), Black = surgical G-Series (dark charcoal)
        britishModeButton->setButtonText(isBlack ? "Black" : "Brown");
        britishModeButton->setColour(juce::TextButton::buttonColourId,
            isBlack ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff8b6914));
        britishModeButton->setColour(juce::TextButton::buttonOnColourId,
            isBlack ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff8b6914));
        britishModeButton->setColour(juce::TextButton::textColourOffId,
            isBlack ? juce::Colour(0xffaaaaaa) : juce::Colour(0xffffffff));
    };

    britishModeButton->onClick = [this, updateBritishModeButtonAppearance]() {
        // Toggle between Brown (0) and Black (1)
        auto* param = processor.parameters.getParameter(ParamIDs::britishMode);
        if (param)
        {
            float currentValue = param->getValue();
            float newValue = currentValue < 0.5f ? 1.0f : 0.0f;
            param->setValueNotifyingHost(newValue);
            updateBritishModeButtonAppearance(newValue > 0.5f);
        }
    };
    // Set initial button color based on current parameter value
    if (auto* param = processor.parameters.getParameter(ParamIDs::britishMode))
        updateBritishModeButtonAppearance(param->getValue() > 0.5f);
    addAndMakeVisible(britishModeButton.get());

    setupBritishKnob(britishSaturationSlider, "saturation", false, satColor);
    britishSaturationSlider->setTooltip("Console saturation: Adds harmonic distortion and warmth");
    setupBritishKnob(britishInputGainSlider, "input_gain", true, ioColor);
    britishInputGainSlider->setTooltip("Input gain: Drive into the EQ circuit (-24 to +24 dB)");
    setupBritishKnob(britishOutputGainSlider, "output_gain", true, ioColor);
    britishOutputGainSlider->setTooltip("Output gain: Final level adjustment (-24 to +24 dB)");

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

    // britishModeButton uses manual onClick handler - no attachment needed

    britishSaturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishSaturation, *britishSaturationSlider);
    britishInputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishInputGain, *britishInputGainSlider);
    britishOutputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::britishOutputGain, *britishOutputGainSlider);
}

void MultiQEditor::updateEQModeVisibility()
{
    // Determine if we're in Digital-style mode (Digital, Match, or Dynamic - same 8-band UI)
    bool isDigitalMode = !isBritishMode && !isTubeEQMode;  // Includes Match mode

    // Linear phase controls - only visible in Digital mode
    linearPhaseButton->setVisible(isDigitalMode);
    linearPhaseQualitySelector->setVisible(isDigitalMode);

    // Band enable buttons - visible in Digital mode (compact toolbar selector)
    for (auto& btn : bandEnableButtons)
        btn->setVisible(isDigitalMode);

    // Old selected band controls replaced by BandDetailPanel - always hidden
    selectedBandLabel.setVisible(false);
    freqSlider->setVisible(false);
    gainSlider->setVisible(false);
    qSlider->setVisible(false);
    freqLabel.setVisible(false);
    gainLabel.setVisible(false);
    qLabel.setVisible(false);

    // BandDetailPanel (Waves F6 style) - only in Digital mode
    bandDetailPanel->setVisible(isDigitalMode);

    // NOTE: A/B buttons, preset selectors, bypass, oversampling, and display scale
    // visibility is handled by layoutUnifiedToolbar() - DO NOT set visibility here!

    // Bottom bar controls removed for cleaner F6 style - always hidden in Digital mode
    qCoupleModeSelector->setVisible(false);
    masterGainSlider->setVisible(false);
    masterGainLabel.setVisible(false);
    analyzerButton->setVisible(false);
    analyzerPrePostButton->setVisible(false);
    analyzerModeSelector->setVisible(false);
    analyzerResolutionSelector->setVisible(false);
    analyzerDecaySlider->setVisible(false);
    // displayScaleSelector visibility is set in resized() for each mode

    // Hide/show graphic displays based on mode
    graphicDisplay->setVisible(isDigitalMode);

    // British mode curve display (only visible if British mode and not collapsed)
    if (britishCurveDisplay)
        britishCurveDisplay->setVisible(isBritishMode && !britishCurveCollapsed);

    // Tube EQ mode curve display (only visible if Tube mode and not collapsed)
    if (tubeEQCurveDisplay)
        tubeEQCurveDisplay->setVisible(isTubeEQMode && !tubeEQCurveCollapsed);

    // Meters visible in all modes
    inputMeter->setVisible(true);
    outputMeter->setVisible(true);

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

    britishModeButton->setVisible(isBritishMode);
    britishSaturationSlider->setVisible(isBritishMode);
    britishInputGainSlider->setVisible(isBritishMode);
    britishOutputGainSlider->setVisible(isBritishMode);

    // British mode header/master controls
    britishBypassButton->setVisible(isBritishMode);
    britishAutoGainButton->setVisible(isBritishMode);

    // NOTE: British A/B, preset selector, curve collapse button visibility
    // is handled by layoutUnifiedToolbar() - DO NOT set visibility here!

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

    // ============== TUBE EQ MODE CONTROLS ==============
    // Tube EQ knobs and selectors
    tubeEQLfBoostSlider->setVisible(isTubeEQMode);
    tubeEQLfFreqSelector->setVisible(isTubeEQMode);
    tubeEQLfAttenSlider->setVisible(isTubeEQMode);
    tubeEQHfBoostSlider->setVisible(isTubeEQMode);
    tubeEQHfBoostFreqSelector->setVisible(isTubeEQMode);
    tubeEQHfBandwidthSlider->setVisible(isTubeEQMode);
    tubeEQHfAttenSlider->setVisible(isTubeEQMode);
    tubeEQHfAttenFreqSelector->setVisible(isTubeEQMode);
    tubeEQInputGainSlider->setVisible(isTubeEQMode);
    tubeEQOutputGainSlider->setVisible(isTubeEQMode);
    tubeEQTubeDriveSlider->setVisible(isTubeEQMode);

    // Tube EQ section labels
    tubeEQLfLabel.setVisible(isTubeEQMode);
    tubeEQHfBoostLabel.setVisible(isTubeEQMode);
    tubeEQHfAttenLabel.setVisible(isTubeEQMode);
    tubeEQMasterLabel.setVisible(isTubeEQMode);

    // Tube EQ knob labels
    tubeEQLfBoostKnobLabel.setVisible(isTubeEQMode);
    tubeEQLfFreqKnobLabel.setVisible(isTubeEQMode);
    tubeEQLfAttenKnobLabel.setVisible(isTubeEQMode);
    tubeEQHfBoostKnobLabel.setVisible(isTubeEQMode);
    tubeEQHfBoostFreqKnobLabel.setVisible(isTubeEQMode);
    tubeEQHfBwKnobLabel.setVisible(isTubeEQMode);
    tubeEQHfAttenKnobLabel.setVisible(isTubeEQMode);
    tubeEQHfAttenFreqKnobLabel.setVisible(isTubeEQMode);
    tubeEQInputKnobLabel.setVisible(isTubeEQMode);
    tubeEQOutputKnobLabel.setVisible(isTubeEQMode);
    tubeEQTubeKnobLabel.setVisible(isTubeEQMode);

    // Tube EQ Mid Dip/Peak section controls
    if (tubeEQMidEnabledButton)
        tubeEQMidEnabledButton->setVisible(isTubeEQMode);
    if (tubeEQMidLowFreqSelector)
        tubeEQMidLowFreqSelector->setVisible(isTubeEQMode);
    if (tubeEQMidLowPeakSlider)
        tubeEQMidLowPeakSlider->setVisible(isTubeEQMode);
    if (tubeEQMidDipFreqSelector)
        tubeEQMidDipFreqSelector->setVisible(isTubeEQMode);
    if (tubeEQMidDipSlider)
        tubeEQMidDipSlider->setVisible(isTubeEQMode);
    if (tubeEQMidHighFreqSelector)
        tubeEQMidHighFreqSelector->setVisible(isTubeEQMode);
    if (tubeEQMidHighPeakSlider)
        tubeEQMidHighPeakSlider->setVisible(isTubeEQMode);

    // Tube EQ Mid section labels
    tubeEQMidLowFreqLabel.setVisible(isTubeEQMode);
    tubeEQMidLowPeakLabel.setVisible(isTubeEQMode);
    tubeEQMidDipFreqLabel.setVisible(isTubeEQMode);
    tubeEQMidDipLabel.setVisible(isTubeEQMode);
    tubeEQMidHighFreqLabel.setVisible(isTubeEQMode);
    tubeEQMidHighPeakLabel.setVisible(isTubeEQMode);

    // NOTE: Tube A/B, preset selector, HQ button visibility
    // is handled by layoutUnifiedToolbar() - DO NOT set visibility here!
    // (tubeHqButton is hidden in favor of global oversamplingSelector)

    // ============== PER-BAND DYNAMICS CONTROLS ==============
    // In Digital mode, dynamics controls are available for each band
    if (dynEnableButton)
        dynEnableButton->setVisible(isDigitalMode);
    if (dynThresholdSlider)
        dynThresholdSlider->setVisible(isDigitalMode);
    if (dynAttackSlider)
        dynAttackSlider->setVisible(isDigitalMode);
    if (dynReleaseSlider)
        dynReleaseSlider->setVisible(isDigitalMode);
    if (dynRangeSlider)
        dynRangeSlider->setVisible(isDigitalMode);

    dynSectionLabel.setVisible(isDigitalMode);
    dynThresholdLabel.setVisible(isDigitalMode);
    dynAttackLabel.setVisible(isDigitalMode);
    dynReleaseLabel.setVisible(isDigitalMode);
    dynRangeLabel.setVisible(isDigitalMode);

    // Update attachments when in Digital mode (dynamics are per-band)
    if (isDigitalMode)
        updateDynamicAttachments();
    else
    {
        // Clear attachments when not in Digital mode
        dynEnableAttachment.reset();
        dynThresholdAttachment.reset();
        dynAttackAttachment.reset();
        dynReleaseAttachment.reset();
        dynRangeAttachment.reset();
    }
}

void MultiQEditor::layoutUnifiedToolbar()
{
    // ===== UNIFIED TOOLBAR LAYOUT =====
    // This function positions ALL shared controls at CONSISTENT positions
    // across all EQ modes (Digital, British, Tube).
    //
    // Layout constants:
    // - Header: 0-50px (plugin title, EQ type selector)
    // - Toolbar: 50-88px (controls positioned here)
    //
    // Shared control positions (SAME in ALL modes):
    // - x=15: EQ Type selector (in header, y=12)
    // - x=15: A/B button (left-aligned below EQ type selector)
    // - x=48: Preset selector (right next to A/B button)
    // - Right-aligned: BYPASS, Oversampling, Display Scale

    constexpr int toolbarY = 56;         // Vertically centered in toolbar row
    constexpr int controlHeight = 26;
    constexpr int bypassOffset = 60;     // getWidth() - 60
    constexpr int ovsOffset = 210;       // getWidth() - 210 (wider for "Oversample: Off")
    constexpr int scaleOffset = 320;     // getWidth() - 320 (wider dropdown)

    // EQ Type selector (in header, same position for all modes)
    eqTypeSelector->setBounds(15, 12, 80, 26);

    // ===== RIGHT-ALIGNED SHARED CONTROLS (ALWAYS VISIBLE IN ALL MODES) =====

    // BYPASS button at right edge
    bypassButton->setBounds(getWidth() - bypassOffset, toolbarY, 55, controlHeight);
    bypassButton->setVisible(true);

    // Oversampling selector before BYPASS (wider to show "Oversample: Off")
    oversamplingSelector.setBounds(getWidth() - ovsOffset, toolbarY, 145, controlHeight);
    oversamplingSelector.setVisible(true);

    // Display Scale selector before Oversampling
    displayScaleSelector->setBounds(getWidth() - scaleOffset, toolbarY, 105, controlHeight);
    displayScaleSelector->setVisible(true);

    // ===== MODE-SPECIFIC LEFT-SIDE CONTROLS (same positions, different components) =====

    // Helper to hide all mode-specific toolbar controls
    auto hideAllModeControls = [&]() {
        // Digital controls
        digitalAbButton.setVisible(false);
        presetSelector->setVisible(false);
        processingModeSelector->setVisible(false);
        autoGainButton->setVisible(false);
        linearPhaseButton->setVisible(false);
        linearPhaseQualitySelector->setVisible(false);
        savePresetButton.setVisible(false);
        undoButton.setVisible(false);
        redoButton.setVisible(false);
        transferToDigitalButton.setVisible(false);

        // British controls
        britishAbButton.setVisible(false);
        britishPresetSelector.setVisible(false);
        britishCurveCollapseButton.setVisible(false);
        britishModeButton->setVisible(false);

        // Tube controls
        tubeAbButton.setVisible(false);
        tubePresetSelector.setVisible(false);
        tubeEQCurveCollapseButton.setVisible(false);
    };

    hideAllModeControls();

    if (isTubeEQMode)
    {
        // Tube mode: A/B, Preset selectors, and Hide Graph button
        tubeAbButton.setBounds(15, toolbarY, 28, controlHeight);
        tubeAbButton.setVisible(true);
        tubePresetSelector.setBounds(48, toolbarY, 150, controlHeight);
        tubePresetSelector.setVisible(true);
        tubeEQCurveCollapseButton.setBounds(203, toolbarY, 85, controlHeight);
        tubeEQCurveCollapseButton.setVisible(true);
        transferToDigitalButton.setBounds(293, toolbarY, 130, controlHeight);
        transferToDigitalButton.setVisible(true);
    }
    else if (isBritishMode)
    {
        // British mode: A/B, Preset, and Hide Graph button (left-aligned below EQ type selector)
        britishAbButton.setBounds(15, toolbarY, 28, controlHeight);
        britishAbButton.setVisible(true);
        britishPresetSelector.setBounds(48, toolbarY, 150, controlHeight);
        britishPresetSelector.setVisible(true);
        britishCurveCollapseButton.setBounds(203, toolbarY, 85, controlHeight);
        britishCurveCollapseButton.setVisible(true);
        transferToDigitalButton.setBounds(293, toolbarY, 130, controlHeight);
        transferToDigitalButton.setVisible(true);

        // British-specific: Brown/Black mode toggle with text and color
        britishModeButton->setBounds(getWidth() - 400, toolbarY, 70, controlHeight);
        britishModeButton->setVisible(true);
    }
    else if (isMatchMode)
    {
        // Match mode: A/B + Transfer→Digital + Undo/Redo, plus right-section controls
        digitalAbButton.setBounds(15, toolbarY, 28, controlHeight);
        digitalAbButton.setVisible(true);
        transferToDigitalButton.setBounds(48, toolbarY, 130, controlHeight);
        transferToDigitalButton.setVisible(true);
        undoButton.setBounds(183, toolbarY, 28, controlHeight);
        undoButton.setVisible(true);
        redoButton.setBounds(216, toolbarY, 28, controlHeight);
        redoButton.setVisible(true);

        int rightSectionEnd = getWidth() - scaleOffset - 5;

        processingModeSelector->setBounds(rightSectionEnd - 85, toolbarY, 82, controlHeight);
        processingModeSelector->setVisible(true);

        autoGainButton->setBounds(rightSectionEnd - 160, toolbarY, 72, controlHeight);
        autoGainButton->setVisible(true);

        linearPhaseButton->setBounds(rightSectionEnd - 245, toolbarY, 82, controlHeight);
        linearPhaseButton->setVisible(true);

        linearPhaseQualitySelector->setBounds(rightSectionEnd - 380, toolbarY, 130, controlHeight);
        linearPhaseQualitySelector->setVisible(true);
    }
    else
    {
        // Digital mode: A/B, Preset, Save, Undo, Redo (left-aligned below EQ type selector)
        digitalAbButton.setBounds(15, toolbarY, 28, controlHeight);
        digitalAbButton.setVisible(true);
        presetSelector->setBounds(48, toolbarY, 150, controlHeight);
        presetSelector->setVisible(true);
        savePresetButton.setBounds(203, toolbarY, 45, controlHeight);
        savePresetButton.setVisible(true);
        undoButton.setBounds(253, toolbarY, 28, controlHeight);
        undoButton.setVisible(true);
        redoButton.setBounds(286, toolbarY, 28, controlHeight);
        redoButton.setVisible(true);

        // Digital-specific right section (before shared right controls)
        int rightSectionEnd = getWidth() - scaleOffset - 5;

        processingModeSelector->setBounds(rightSectionEnd - 85, toolbarY, 82, controlHeight);
        processingModeSelector->setVisible(true);

        autoGainButton->setBounds(rightSectionEnd - 160, toolbarY, 72, controlHeight);
        autoGainButton->setVisible(true);

        linearPhaseButton->setBounds(rightSectionEnd - 245, toolbarY, 82, controlHeight);
        linearPhaseButton->setVisible(true);

        linearPhaseQualitySelector->setBounds(rightSectionEnd - 380, toolbarY, 130, controlHeight);
        linearPhaseQualitySelector->setVisible(true);
    }
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

void MultiQEditor::applyTubePreset(int presetId)
{
    // Validate presetId is within expected range (1-7)
    if (presetId < 1 || presetId > 7)
    {
        DBG("MultiQEditor::applyTubePreset: Invalid presetId " + juce::String(presetId) + " (expected 1-7)");
        return;
    }

    // Helper to set parameter value with defensive checks
    auto setParam = [this](const juce::String& paramId, float value) {
        auto* param = processor.parameters.getParameter(paramId);
        if (param == nullptr)
        {
            DBG("MultiQEditor::applyTubePreset: Parameter '" + paramId + "' not found");
            return;
        }
        auto range = param->getNormalisableRange();
        float clampedValue = juce::jlimit(range.start, range.end, value);
        param->setValueNotifyingHost(param->convertTo0to1(clampedValue));
    };

    // Tube EQ parameters: LF Boost, LF Atten, LF Freq, HF Boost, HF Freq, HF BW, HF Atten, HF Atten Freq,
    //                       Tube Drive, Input, Output, Mid section enabled

    switch (presetId)
    {
        case 1:  // Default - flat response
            setParam(ParamIDs::pultecLfBoostGain, 0.0f);
            setParam(ParamIDs::pultecLfAttenGain, 0.0f);
            setParam(ParamIDs::pultecLfBoostFreq, 2.0f);  // 60 Hz (index 2)
            setParam(ParamIDs::pultecHfBoostGain, 0.0f);
            setParam(ParamIDs::pultecHfBoostFreq, 0.0f);  // 3k Hz
            setParam(ParamIDs::pultecHfBoostBandwidth, 0.5f);
            setParam(ParamIDs::pultecHfAttenGain, 0.0f);
            setParam(ParamIDs::pultecHfAttenFreq, 0.0f);  // 5k Hz
            setParam(ParamIDs::pultecTubeDrive, 0.0f);
            setParam(ParamIDs::pultecInputGain, 0.0f);
            setParam(ParamIDs::pultecOutputGain, 0.0f);
            setParam(ParamIDs::pultecMidEnabled, 0.0f);
            break;

        case 2:  // Warm Vocal - boost lows, gentle air
            setParam(ParamIDs::pultecLfBoostGain, 3.0f);
            setParam(ParamIDs::pultecLfAttenGain, 0.0f);
            setParam(ParamIDs::pultecLfBoostFreq, 3.0f);  // 100 Hz (index 3)
            setParam(ParamIDs::pultecHfBoostGain, 4.0f);
            setParam(ParamIDs::pultecHfBoostFreq, 5.0f);  // 12 kHz (index 5)
            setParam(ParamIDs::pultecHfBoostBandwidth, 0.6f);
            setParam(ParamIDs::pultecHfAttenGain, 2.0f);
            setParam(ParamIDs::pultecHfAttenFreq, 1.0f);  // 10 kHz (index 1)
            setParam(ParamIDs::pultecTubeDrive, 0.2f);
            setParam(ParamIDs::pultecInputGain, 0.0f);
            setParam(ParamIDs::pultecOutputGain, 0.0f);
            setParam(ParamIDs::pultecMidEnabled, 0.0f);
            break;

        case 3:  // Vintage Bass - classic low-end trick
            setParam(ParamIDs::pultecLfBoostGain, 6.0f);
            setParam(ParamIDs::pultecLfAttenGain, 4.0f);  // Simultaneous boost & cut
            setParam(ParamIDs::pultecLfBoostFreq, 2.0f);  // 60 Hz (index 2)
            setParam(ParamIDs::pultecHfBoostGain, 0.0f);
            setParam(ParamIDs::pultecHfBoostFreq, 0.0f);
            setParam(ParamIDs::pultecHfBoostBandwidth, 0.5f);
            setParam(ParamIDs::pultecHfAttenGain, 3.0f);
            setParam(ParamIDs::pultecHfAttenFreq, 2.0f);  // 20k Hz
            setParam(ParamIDs::pultecTubeDrive, 0.3f);
            setParam(ParamIDs::pultecInputGain, 0.0f);
            setParam(ParamIDs::pultecOutputGain, 0.0f);
            setParam(ParamIDs::pultecMidEnabled, 0.0f);
            break;

        case 4:  // Silky Highs - smooth high-end boost
            setParam(ParamIDs::pultecLfBoostGain, 0.0f);
            setParam(ParamIDs::pultecLfAttenGain, 0.0f);
            setParam(ParamIDs::pultecLfBoostFreq, 2.0f);  // 60 Hz (index 2)
            setParam(ParamIDs::pultecHfBoostGain, 5.0f);
            setParam(ParamIDs::pultecHfBoostFreq, 3.0f);  // 8 kHz (index 3)
            setParam(ParamIDs::pultecHfBoostBandwidth, 0.7f);  // Wide bandwidth
            setParam(ParamIDs::pultecHfAttenGain, 0.0f);
            setParam(ParamIDs::pultecHfAttenFreq, 0.0f);
            setParam(ParamIDs::pultecTubeDrive, 0.15f);
            setParam(ParamIDs::pultecInputGain, 0.0f);
            setParam(ParamIDs::pultecOutputGain, 0.0f);
            setParam(ParamIDs::pultecMidEnabled, 0.0f);
            break;

        case 5:  // Full Mix - balanced enhancement
            setParam(ParamIDs::pultecLfBoostGain, 3.0f);
            setParam(ParamIDs::pultecLfAttenGain, 1.0f);
            setParam(ParamIDs::pultecLfBoostFreq, 2.0f);  // 60 Hz (index 2)
            setParam(ParamIDs::pultecHfBoostGain, 3.0f);
            setParam(ParamIDs::pultecHfBoostFreq, 5.0f);  // 12 kHz (index 5)
            setParam(ParamIDs::pultecHfBoostBandwidth, 0.5f);
            setParam(ParamIDs::pultecHfAttenGain, 1.0f);
            setParam(ParamIDs::pultecHfAttenFreq, 1.0f);  // 10k Hz
            setParam(ParamIDs::pultecTubeDrive, 0.25f);
            setParam(ParamIDs::pultecInputGain, 0.0f);
            setParam(ParamIDs::pultecOutputGain, 0.0f);
            setParam(ParamIDs::pultecMidEnabled, 0.0f);
            break;

        case 6:  // Subtle Warmth - gentle coloration
            setParam(ParamIDs::pultecLfBoostGain, 1.5f);
            setParam(ParamIDs::pultecLfAttenGain, 0.0f);
            setParam(ParamIDs::pultecLfBoostFreq, 3.0f);  // 100 Hz (index 3)
            setParam(ParamIDs::pultecHfBoostGain, 1.5f);
            setParam(ParamIDs::pultecHfBoostFreq, 5.0f);  // 12 kHz (index 5)
            setParam(ParamIDs::pultecHfBoostBandwidth, 0.5f);
            setParam(ParamIDs::pultecHfAttenGain, 0.5f);
            setParam(ParamIDs::pultecHfAttenFreq, 2.0f);  // 20k Hz
            setParam(ParamIDs::pultecTubeDrive, 0.1f);
            setParam(ParamIDs::pultecInputGain, 0.0f);
            setParam(ParamIDs::pultecOutputGain, 0.0f);
            setParam(ParamIDs::pultecMidEnabled, 0.0f);
            break;

        case 7:  // Mastering - subtle wide enhancement
            setParam(ParamIDs::pultecLfBoostGain, 2.0f);
            setParam(ParamIDs::pultecLfAttenGain, 1.0f);
            setParam(ParamIDs::pultecLfBoostFreq, 2.0f);  // 60 Hz (index 2)
            setParam(ParamIDs::pultecHfBoostGain, 2.0f);
            setParam(ParamIDs::pultecHfBoostFreq, 3.0f);  // 8 kHz (index 3)
            setParam(ParamIDs::pultecHfBoostBandwidth, 0.8f);  // Very wide
            setParam(ParamIDs::pultecHfAttenGain, 0.5f);
            setParam(ParamIDs::pultecHfAttenFreq, 2.0f);  // 20k Hz
            setParam(ParamIDs::pultecTubeDrive, 0.05f);  // Subtle tube warmth
            setParam(ParamIDs::pultecInputGain, 0.0f);
            setParam(ParamIDs::pultecOutputGain, 0.0f);
            setParam(ParamIDs::pultecMidEnabled, 0.0f);
            break;

        default:
            break;
    }
}

void MultiQEditor::setupTubeEQControls()
{
    // Create Tube EQ curve display
    tubeEQCurveDisplay = std::make_unique<TubeEQCurveDisplay>(processor);
    tubeEQCurveDisplay->setVisible(false);
    addAndMakeVisible(tubeEQCurveDisplay.get());

    // Helper to setup Vintage Tube EQ-style rotary knob
    auto setupTubeEQKnob = [this](std::unique_ptr<juce::Slider>& slider, const juce::String& name) {
        slider = std::make_unique<DuskSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::NoTextBox);
        slider->setName(name);
        slider->setLookAndFeel(&vintageTubeLookAndFeel);
        slider->setVisible(false);
        addAndMakeVisible(slider.get());
    };

    // Helper to setup Vintage Tube EQ-style combo selector
    auto setupTubeEQSelector = [this](std::unique_ptr<juce::ComboBox>& combo) {
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
    setupTubeEQKnob(tubeEQLfBoostSlider, "lf_boost");
    tubeEQLfBoostSlider->setTooltip("Low frequency boost (resonant LC network)");
    setupTubeEQSelector(tubeEQLfFreqSelector);
    tubeEQLfFreqSelector->addItemList({"20 Hz", "30 Hz", "60 Hz", "100 Hz"}, 1);
    tubeEQLfFreqSelector->setTooltip("Low frequency boost center frequency");
    setupTubeEQKnob(tubeEQLfAttenSlider, "lf_atten");
    tubeEQLfAttenSlider->setTooltip("Low frequency attenuation (shelf, below boost frequency)");

    // HF Boost Section
    setupTubeEQKnob(tubeEQHfBoostSlider, "hf_boost");
    tubeEQHfBoostSlider->setTooltip("High frequency boost (inductor-coupled)");
    setupTubeEQSelector(tubeEQHfBoostFreqSelector);
    tubeEQHfBoostFreqSelector->addItemList({"3k", "4k", "5k", "8k", "10k", "12k", "16k"}, 1);
    tubeEQHfBoostFreqSelector->setTooltip("High frequency boost center frequency");
    setupTubeEQKnob(tubeEQHfBandwidthSlider, "hf_bandwidth");
    tubeEQHfBandwidthSlider->setTooltip("High frequency boost bandwidth (sharp to broad)");

    // HF Atten Section
    setupTubeEQKnob(tubeEQHfAttenSlider, "hf_atten");
    tubeEQHfAttenSlider->setTooltip("High frequency attenuation (shelf cut)");
    setupTubeEQSelector(tubeEQHfAttenFreqSelector);
    tubeEQHfAttenFreqSelector->addItemList({"5k", "10k", "20k"}, 1);
    tubeEQHfAttenFreqSelector->setTooltip("High frequency attenuation corner frequency");

    // Global controls
    setupTubeEQKnob(tubeEQInputGainSlider, "input");
    tubeEQInputGainSlider->setTooltip("Input gain into the tube circuit");
    setupTubeEQKnob(tubeEQOutputGainSlider, "output");
    tubeEQOutputGainSlider->setTooltip("Output gain level");
    setupTubeEQKnob(tubeEQTubeDriveSlider, "tube_drive");
    tubeEQTubeDriveSlider->setTooltip("Tube drive: saturation and harmonic warmth");

    // Mid Section controls
    // Mid Enabled button (IN button) - bypasses the Mid Dip/Peak section only
    tubeEQMidEnabledButton = std::make_unique<juce::ToggleButton>("IN");
    tubeEQMidEnabledButton->setLookAndFeel(&vintageTubeLookAndFeel);
    tubeEQMidEnabledButton->setTooltip("Enable/disable Mid Dip/Peak section");
    tubeEQMidEnabledButton->setVisible(false);
    addAndMakeVisible(tubeEQMidEnabledButton.get());

    // Mid frequency dropdowns (matching style of other freq selectors)
    auto setupMidFreqSelector = [this](std::unique_ptr<juce::ComboBox>& selector) {
        selector = std::make_unique<juce::ComboBox>();
        selector->setLookAndFeel(&vintageTubeLookAndFeel);
        selector->setVisible(false);
        addAndMakeVisible(selector.get());
    };

    setupMidFreqSelector(tubeEQMidLowFreqSelector);
    tubeEQMidLowFreqSelector->addItem("200 Hz", 1);
    tubeEQMidLowFreqSelector->addItem("300 Hz", 2);
    tubeEQMidLowFreqSelector->addItem("500 Hz", 3);
    tubeEQMidLowFreqSelector->addItem("700 Hz", 4);
    tubeEQMidLowFreqSelector->addItem("1.0 kHz", 5);
    tubeEQMidLowFreqSelector->setTooltip("Mid low peak frequency");

    setupTubeEQKnob(tubeEQMidLowPeakSlider, "mid_low_peak");
    tubeEQMidLowPeakSlider->setTooltip("Mid low peak boost amount");

    setupMidFreqSelector(tubeEQMidDipFreqSelector);
    tubeEQMidDipFreqSelector->addItem("200 Hz", 1);
    tubeEQMidDipFreqSelector->addItem("300 Hz", 2);
    tubeEQMidDipFreqSelector->addItem("500 Hz", 3);
    tubeEQMidDipFreqSelector->addItem("700 Hz", 4);
    tubeEQMidDipFreqSelector->addItem("1.0 kHz", 5);
    tubeEQMidDipFreqSelector->addItem("1.5 kHz", 6);
    tubeEQMidDipFreqSelector->addItem("2.0 kHz", 7);
    tubeEQMidDipFreqSelector->setTooltip("Mid dip center frequency");

    setupTubeEQKnob(tubeEQMidDipSlider, "mid_dip");
    tubeEQMidDipSlider->setTooltip("Mid dip cut amount");

    setupMidFreqSelector(tubeEQMidHighFreqSelector);
    tubeEQMidHighFreqSelector->addItem("1.5 kHz", 1);
    tubeEQMidHighFreqSelector->addItem("2.0 kHz", 2);
    tubeEQMidHighFreqSelector->addItem("3.0 kHz", 3);
    tubeEQMidHighFreqSelector->addItem("4.0 kHz", 4);
    tubeEQMidHighFreqSelector->addItem("5.0 kHz", 5);
    tubeEQMidHighFreqSelector->setTooltip("Mid high peak frequency");

    setupTubeEQKnob(tubeEQMidHighPeakSlider, "mid_high_peak");
    tubeEQMidHighPeakSlider->setTooltip("Mid high peak boost amount");

    // Section labels (light gray on dark background)
    auto setupSectionLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));  // Light gray text
        label.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
        label.setVisible(false);
        addAndMakeVisible(label);
    };

    setupSectionLabel(tubeEQLfLabel, "LOW FREQUENCY");
    setupSectionLabel(tubeEQHfBoostLabel, "HIGH FREQUENCY");
    setupSectionLabel(tubeEQHfAttenLabel, "ATTEN SEL");
    setupSectionLabel(tubeEQMasterLabel, "MASTER");

    // Knob labels
    setupKnobLabel(tubeEQLfBoostKnobLabel, "BOOST");
    setupKnobLabel(tubeEQLfFreqKnobLabel, "CPS");
    setupKnobLabel(tubeEQLfAttenKnobLabel, "ATTEN");
    setupKnobLabel(tubeEQHfBoostKnobLabel, "BOOST");
    setupKnobLabel(tubeEQHfBoostFreqKnobLabel, "KCS");
    setupKnobLabel(tubeEQHfBwKnobLabel, "HF BANDWIDTH");
    setupKnobLabel(tubeEQHfAttenKnobLabel, "ATTEN");
    setupKnobLabel(tubeEQHfAttenFreqKnobLabel, "KCS");
    setupKnobLabel(tubeEQInputKnobLabel, "INPUT");
    setupKnobLabel(tubeEQOutputKnobLabel, "OUTPUT");
    setupKnobLabel(tubeEQTubeKnobLabel, "DRIVE");

    // Mid section labels
    setupKnobLabel(tubeEQMidLowFreqLabel, "LOW FREQ");
    setupKnobLabel(tubeEQMidLowPeakLabel, "LOW PEAK");
    setupKnobLabel(tubeEQMidDipFreqLabel, "DIP FREQ");
    setupKnobLabel(tubeEQMidDipLabel, "DIP");
    setupKnobLabel(tubeEQMidHighFreqLabel, "HIGH FREQ");
    setupKnobLabel(tubeEQMidHighPeakLabel, "HIGH PEAK");

    // Create attachments
    tubeEQLfBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecLfBoostGain, *tubeEQLfBoostSlider);
    tubeEQLfFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecLfBoostFreq, *tubeEQLfFreqSelector);
    tubeEQLfAttenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecLfAttenGain, *tubeEQLfAttenSlider);
    tubeEQHfBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecHfBoostGain, *tubeEQHfBoostSlider);
    tubeEQHfBoostFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecHfBoostFreq, *tubeEQHfBoostFreqSelector);
    tubeEQHfBandwidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecHfBoostBandwidth, *tubeEQHfBandwidthSlider);
    tubeEQHfAttenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecHfAttenGain, *tubeEQHfAttenSlider);
    tubeEQHfAttenFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecHfAttenFreq, *tubeEQHfAttenFreqSelector);
    tubeEQInputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecInputGain, *tubeEQInputGainSlider);
    tubeEQOutputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecOutputGain, *tubeEQOutputGainSlider);
    tubeEQTubeDriveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecTubeDrive, *tubeEQTubeDriveSlider);

    // Mid section attachments
    tubeEQMidEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::pultecMidEnabled, *tubeEQMidEnabledButton);
    tubeEQMidLowFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecMidLowFreq, *tubeEQMidLowFreqSelector);
    tubeEQMidLowPeakAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecMidLowPeak, *tubeEQMidLowPeakSlider);
    tubeEQMidDipFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecMidDipFreq, *tubeEQMidDipFreqSelector);
    tubeEQMidDipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecMidDip, *tubeEQMidDipSlider);
    tubeEQMidHighFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::pultecMidHighFreq, *tubeEQMidHighFreqSelector);
    tubeEQMidHighPeakAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::pultecMidHighPeak, *tubeEQMidHighPeakSlider);
}

void MultiQEditor::layoutTubeEQControls()
{
    auto bounds = getLocalBounds();

    // ===== TUBE MODE LAYOUT =====
    // Reorganized layout per user request:
    // - Row 1: [LF BOOST] [LF ATTEN] [HF BOOST] [HF ATTEN]
    // - Row 2: Frequency row with separator lines: [LF FREQ] [HF BANDWIDTH] [HF FREQ] [ATTEN FREQ]
    // - Row 3: MID DIP/PEAK section
    // - Right panel: INPUT → OUTPUT → TUBE DRIVE (vertical signal flow)

    const int headerHeight = tubeEQCurveCollapsed ? 88 : 200;  // 88 collapsed, 200 with curve
    const int labelHeight = 22;         // Height for knob labels
    const int knobSize = 105;           // Main knobs
    const int smallKnobSize = 90;       // Row 3 knobs (mid section)
    const int comboWidth = 90;          // Width for combo boxes
    const int comboHeight = 32;         // Height for combo boxes
    const int bottomMargin = 35;        // Margin at bottom for footer
    const int rightPanelWidth = 125;    // Right side panel for INPUT/OUTPUT/DRIVE
    const int meterReserve = 40;        // Space for output meter at right edge

    // Margins - leave space for meters and right panel
    int mainX = 30;
    int mainWidth = bounds.getWidth() - 60 - rightPanelWidth - meterReserve;

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
    int rowGap = juce::jmax(5, extraSpace / 4);  // Distribute extra space (min 5px if host constrains window)

    // ============== ROW 1: MAIN GAIN CONTROLS (4 knobs) ==============
    int row1Y = headerHeight + rowGap;

    // Calculate even spacing for 4 knobs across main width
    int totalKnobWidth = 4 * knobSize;
    int knobSpacing = (mainWidth - totalKnobWidth) / 5;

    // LF BOOST (position 1)
    int knob1X = mainX + knobSpacing;
    tubeEQLfBoostSlider->setBounds(knob1X, row1Y, knobSize, knobSize);
    tubeEQLfBoostKnobLabel.setBounds(knob1X - 15, row1Y + knobSize + 2, knobSize + 30, labelHeight);
    tubeEQLfBoostKnobLabel.setText("LF BOOST", juce::dontSendNotification);

    // LF ATTEN (position 2)
    int knob2X = mainX + 2 * knobSpacing + knobSize;
    tubeEQLfAttenSlider->setBounds(knob2X, row1Y, knobSize, knobSize);
    tubeEQLfAttenKnobLabel.setBounds(knob2X - 15, row1Y + knobSize + 2, knobSize + 30, labelHeight);
    tubeEQLfAttenKnobLabel.setText("LF ATTEN", juce::dontSendNotification);

    // HF BOOST (position 3)
    int knob3X = mainX + 3 * knobSpacing + 2 * knobSize;
    tubeEQHfBoostSlider->setBounds(knob3X, row1Y, knobSize, knobSize);
    tubeEQHfBoostKnobLabel.setBounds(knob3X - 15, row1Y + knobSize + 2, knobSize + 30, labelHeight);
    tubeEQHfBoostKnobLabel.setText("HF BOOST", juce::dontSendNotification);

    // HF ATTEN (position 4)
    int knob4X = mainX + 4 * knobSpacing + 3 * knobSize;
    tubeEQHfAttenSlider->setBounds(knob4X, row1Y, knobSize, knobSize);
    tubeEQHfAttenKnobLabel.setBounds(knob4X - 15, row1Y + knobSize + 2, knobSize + 30, labelHeight);
    tubeEQHfAttenKnobLabel.setText("HF ATTEN", juce::dontSendNotification);

    // ============== ROW 2: FREQUENCY SELECTORS & HF BANDWIDTH (with separator lines) ==============
    // Layout: [LF FREQ] [HF BANDWIDTH] [HF FREQ] [ATTEN FREQ] evenly distributed
    int row2Y = row1Y + row1Height + rowGap;

    // 4 controls evenly spaced across the row
    int row2ControlWidth = knobSize;  // Same size as main knobs for consistency
    int row2TotalWidth = 4 * row2ControlWidth;
    int row2Spacing = (mainWidth - row2TotalWidth) / 5;

    // 1. LF FREQ selector (position 1)
    int lfFreqX = mainX + row2Spacing + (row2ControlWidth - comboWidth) / 2;
    tubeEQLfFreqKnobLabel.setBounds(mainX + row2Spacing, row2Y, row2ControlWidth, labelHeight);
    tubeEQLfFreqKnobLabel.setText("LF FREQ", juce::dontSendNotification);
    tubeEQLfFreqSelector->setBounds(lfFreqX, row2Y + labelHeight + 2, comboWidth, comboHeight);

    // 2. HF BANDWIDTH knob (position 2)
    int bwX = mainX + 2 * row2Spacing + row2ControlWidth;
    tubeEQHfBwKnobLabel.setBounds(bwX, row2Y, row2ControlWidth, labelHeight);
    tubeEQHfBwKnobLabel.setText("HF BANDWIDTH", juce::dontSendNotification);
    tubeEQHfBandwidthSlider->setBounds(bwX, row2Y + labelHeight + 2, row2ControlWidth, row2ControlWidth);

    // 3. HF FREQ selector (position 3)
    int hfBoostFreqX = mainX + 3 * row2Spacing + 2 * row2ControlWidth + (row2ControlWidth - comboWidth) / 2;
    tubeEQHfBoostFreqKnobLabel.setBounds(mainX + 3 * row2Spacing + 2 * row2ControlWidth, row2Y, row2ControlWidth, labelHeight);
    tubeEQHfBoostFreqKnobLabel.setText("HF FREQ", juce::dontSendNotification);
    tubeEQHfBoostFreqSelector->setBounds(hfBoostFreqX, row2Y + labelHeight + 2, comboWidth, comboHeight);

    // 4. ATTEN FREQ selector (position 4)
    int hfAttenFreqX = mainX + 4 * row2Spacing + 3 * row2ControlWidth + (row2ControlWidth - comboWidth) / 2;
    tubeEQHfAttenFreqKnobLabel.setBounds(mainX + 4 * row2Spacing + 3 * row2ControlWidth, row2Y, row2ControlWidth, labelHeight);
    tubeEQHfAttenFreqKnobLabel.setText("ATTEN FREQ", juce::dontSendNotification);
    tubeEQHfAttenFreqSelector->setBounds(hfAttenFreqX, row2Y + labelHeight + 2, comboWidth, comboHeight);

    // ============== ROW 3: MID DIP/PEAK SECTION (6 controls + IN toggle) ==============
    int row3Y = row2Y + row2Height + rowGap;

    // IN toggle button on the left
    int inButtonWidth = 45;
    int inButtonHeight = 40;
    int inButtonX = 40;  // After input meter (ends at x=36)
    int inButtonY = row3Y + (smallKnobSize - inButtonHeight) / 2;
    if (tubeEQMidEnabledButton)
        tubeEQMidEnabledButton->setBounds(inButtonX, inButtonY, inButtonWidth, inButtonHeight);

    // 6 controls evenly spaced after the IN button
    int midAreaX = mainX + inButtonWidth + 5;
    int midAreaWidth = mainWidth - inButtonWidth - 5;
    int midKnobSpacing = (midAreaWidth - 6 * smallKnobSize) / 7;

    // Dropdown width for frequency selectors
    int dropdownWidth = 80;
    int dropdownHeight = 24;

    // LOW FREQ dropdown (position 1)
    int midKnob1X = midAreaX + midKnobSpacing;
    if (tubeEQMidLowFreqSelector)
    {
        tubeEQMidLowFreqSelector->setBounds(midKnob1X + (smallKnobSize - dropdownWidth) / 2, row3Y + (smallKnobSize - dropdownHeight) / 2, dropdownWidth, dropdownHeight);
        tubeEQMidLowFreqLabel.setBounds(midKnob1X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        tubeEQMidLowFreqLabel.setText("LOW FREQ", juce::dontSendNotification);
    }

    // LOW PEAK knob (position 2)
    int midKnob2X = midAreaX + 2 * midKnobSpacing + smallKnobSize;
    if (tubeEQMidLowPeakSlider)
    {
        tubeEQMidLowPeakSlider->setBounds(midKnob2X, row3Y, smallKnobSize, smallKnobSize);
        tubeEQMidLowPeakLabel.setBounds(midKnob2X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        tubeEQMidLowPeakLabel.setText("LOW PEAK", juce::dontSendNotification);
    }

    // DIP FREQ dropdown (position 3)
    int midKnob3X = midAreaX + 3 * midKnobSpacing + 2 * smallKnobSize;
    if (tubeEQMidDipFreqSelector)
    {
        tubeEQMidDipFreqSelector->setBounds(midKnob3X + (smallKnobSize - dropdownWidth) / 2, row3Y + (smallKnobSize - dropdownHeight) / 2, dropdownWidth, dropdownHeight);
        tubeEQMidDipFreqLabel.setBounds(midKnob3X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        tubeEQMidDipFreqLabel.setText("DIP FREQ", juce::dontSendNotification);
    }

    // DIP knob (position 4)
    int midKnob4X = midAreaX + 4 * midKnobSpacing + 3 * smallKnobSize;
    if (tubeEQMidDipSlider)
    {
        tubeEQMidDipSlider->setBounds(midKnob4X, row3Y, smallKnobSize, smallKnobSize);
        tubeEQMidDipLabel.setBounds(midKnob4X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        tubeEQMidDipLabel.setText("DIP", juce::dontSendNotification);
    }

    // HIGH FREQ dropdown (position 5)
    int midKnob5X = midAreaX + 5 * midKnobSpacing + 4 * smallKnobSize;
    if (tubeEQMidHighFreqSelector)
    {
        tubeEQMidHighFreqSelector->setBounds(midKnob5X + (smallKnobSize - dropdownWidth) / 2, row3Y + (smallKnobSize - dropdownHeight) / 2, dropdownWidth, dropdownHeight);
        tubeEQMidHighFreqLabel.setBounds(midKnob5X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        tubeEQMidHighFreqLabel.setText("HIGH FREQ", juce::dontSendNotification);
    }

    // HIGH PEAK knob (position 6)
    int midKnob6X = midAreaX + 6 * midKnobSpacing + 5 * smallKnobSize;
    if (tubeEQMidHighPeakSlider)
    {
        tubeEQMidHighPeakSlider->setBounds(midKnob6X, row3Y, smallKnobSize, smallKnobSize);
        tubeEQMidHighPeakLabel.setBounds(midKnob6X - 10, row3Y + smallKnobSize + 2, smallKnobSize + 20, labelHeight);
        tubeEQMidHighPeakLabel.setText("HIGH PEAK", juce::dontSendNotification);
    }

    // ============== RIGHT SIDE PANEL: INPUT → OUTPUT → TUBE DRIVE ==============
    // Vertical signal flow: INPUT at top, OUTPUT in middle, TUBE DRIVE at bottom
    int rightPanelX = bounds.getWidth() - rightPanelWidth - meterReserve;
    int rightKnobSize = 85;  // Knob size for right panel
    int rightSpacing = 12;   // Spacing between knobs
    int totalRightHeight = 3 * rightKnobSize + 2 * rightSpacing + 3 * labelHeight;
    int rightStartY = headerHeight + (availableHeight - totalRightHeight) / 2;  // Center vertically

    int rightCenterX = rightPanelX + (rightPanelWidth - rightKnobSize) / 2;

    // INPUT knob (top of right panel)
    int inputY = rightStartY;
    tubeEQInputGainSlider->setBounds(rightCenterX, inputY, rightKnobSize, rightKnobSize);
    tubeEQInputKnobLabel.setBounds(rightCenterX - 15, inputY + rightKnobSize + 2, rightKnobSize + 30, labelHeight);
    tubeEQInputKnobLabel.setText("INPUT", juce::dontSendNotification);

    // OUTPUT knob (middle of right panel)
    int outputY = inputY + rightKnobSize + labelHeight + rightSpacing;
    tubeEQOutputGainSlider->setBounds(rightCenterX, outputY, rightKnobSize, rightKnobSize);
    tubeEQOutputKnobLabel.setBounds(rightCenterX - 15, outputY + rightKnobSize + 2, rightKnobSize + 30, labelHeight);
    tubeEQOutputKnobLabel.setText("OUTPUT", juce::dontSendNotification);

    // TUBE DRIVE knob (bottom of right panel)
    int driveY = outputY + rightKnobSize + labelHeight + rightSpacing;
    tubeEQTubeDriveSlider->setBounds(rightCenterX, driveY, rightKnobSize, rightKnobSize);
    tubeEQTubeKnobLabel.setBounds(rightCenterX - 15, driveY + rightKnobSize + 2, rightKnobSize + 30, labelHeight);
    tubeEQTubeKnobLabel.setText("TUBE DRIVE", juce::dontSendNotification);

    // Hide unused labels (section labels are drawn in paint())
    tubeEQMasterLabel.setVisible(false);
    tubeEQLfLabel.setVisible(false);
    tubeEQHfBoostLabel.setVisible(false);
    tubeEQHfAttenLabel.setVisible(false);

    // Curve display visibility handled by updateEQModeVisibility()
    if (tubeEQCurveDisplay)
        tubeEQCurveDisplay->setVisible(isTubeEQMode && !tubeEQCurveCollapsed);
}

// A/B Comparison Functions

void MultiQEditor::toggleAB()
{
    // Tube/Tube EQ mode: save/restore only tubeEQ parameters
    auto tubeEQFilter = [](const juce::String& id) { return id.startsWith("pultec_"); };

    copyModeParamsToState(isStateA ? stateA : stateB, tubeEQFilter);

    isStateA = !isStateA;

    if (isStateA)
    {
        if (stateA.isValid())
            applyModeParams(stateA);
        tubeAbButton.setButtonText("A");
        tubeAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));
    }
    else
    {
        if (stateB.isValid())
            applyModeParams(stateB);
        tubeAbButton.setButtonText("B");
        tubeAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6a3a3a));
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

void MultiQEditor::copyModeParamsToState(juce::ValueTree& state,
                                          const std::function<bool(const juce::String&)>& filter)
{
    state = juce::ValueTree("ModeState");
    auto fullState = processor.parameters.copyState();
    for (int i = 0; i < fullState.getNumChildren(); ++i)
    {
        auto child = fullState.getChild(i);
        auto paramId = child.getProperty("id").toString();
        if (filter(paramId))
            state.addChild(child.createCopy(), -1, nullptr);
    }
}

void MultiQEditor::applyModeParams(const juce::ValueTree& state)
{
    if (!state.isValid() || state.getNumChildren() == 0)
        return;

    auto fullState = processor.parameters.copyState();
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto savedChild = state.getChild(i);
        auto id = savedChild.getProperty("id").toString();
        for (int j = 0; j < fullState.getNumChildren(); ++j)
        {
            auto existing = fullState.getChild(j);
            if (existing.getProperty("id").toString() == id)
            {
                existing.setProperty("value", savedChild.getProperty("value"), nullptr);
                break;
            }
        }
    }
    processor.parameters.replaceState(fullState);
}

void MultiQEditor::toggleBritishAB()
{
    // British mode: save/restore only british_ parameters
    auto britishFilter = [](const juce::String& id) { return id.startsWith("british_"); };

    copyModeParamsToState(britishIsStateA ? britishStateA : britishStateB, britishFilter);

    britishIsStateA = !britishIsStateA;

    if (britishIsStateA)
    {
        if (britishStateA.isValid())
            applyModeParams(britishStateA);
        britishAbButton.setButtonText("A");
        britishAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));
    }
    else
    {
        if (britishStateB.isValid())
            applyModeParams(britishStateB);
        britishAbButton.setButtonText("B");
        britishAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6a3a3a));
    }
}

void MultiQEditor::drawBritishKnobMarkings(juce::Graphics& g)
{
    // Knob tick markings with value labels (matching 4K-EQ)

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

    // Helper for evenly spaced ticks
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

    // ===== HPF (20-500Hz) =====
    if (britishHpfFreqSlider) drawTicksEvenlySpaced(britishHpfFreqSlider->getBounds(), {"20", "70", "120", "200", "300", "500"});

    // ===== LPF (3000-20000Hz) =====
    if (britishLpfFreqSlider) drawTicksEvenlySpaced(britishLpfFreqSlider->getBounds(), {"3k", "5k", "8k", "12k", "20k"});

    // ===== LF Frequency (30-480Hz) =====
    if (britishLfFreqSlider) drawTicksEvenlySpaced(britishLfFreqSlider->getBounds(), {"30", "50", "100", "200", "300", "480"});

    // ===== LMF Frequency (200-2500Hz) =====
    if (britishLmFreqSlider) drawTicksEvenlySpaced(britishLmFreqSlider->getBounds(), {".2", ".5", ".8", "1", "2", "2.5"});

    // ===== HMF Frequency (600-7000Hz) =====
    if (britishHmFreqSlider) drawTicksEvenlySpaced(britishHmFreqSlider->getBounds(), {".6", "1.5", "3", "4.5", "6", "7"});

    // ===== HF Frequency (1500-16000Hz) =====
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

// Dynamic EQ Mode Setup

void MultiQEditor::setupDynamicControls()
{
    // Helper to setup a dynamic mode slider (compact control bar style)
    auto setupDynSlider = [this](std::unique_ptr<juce::Slider>& slider, const juce::String& name,
                                  const juce::String& suffix, double min, double max, double def) {
        slider = std::make_unique<DuskSlider>(juce::Slider::LinearHorizontal,
                                                 juce::Slider::TextBoxRight);
        slider->setName(name);
        slider->setRange(min, max, 0.1);
        slider->setValue(def);
        slider->setTextValueSuffix(suffix);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 18);  // Compact text box
        slider->setVisible(false);
        addAndMakeVisible(slider.get());
    };

    // Helper to setup a dynamic mode label
    auto setupDynLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setFont(juce::Font(juce::FontOptions(11.0f)));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffa0a0a0));
        label.setVisible(false);
        addAndMakeVisible(label);
    };

    // Dynamics enable button (per-band)
    dynEnableButton = std::make_unique<juce::ToggleButton>("DYN");
    dynEnableButton->setTooltip("Enable dynamics processing for this band");
    dynEnableButton->setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff00cc66));
    dynEnableButton->setVisible(false);
    addAndMakeVisible(dynEnableButton.get());

    // Threshold slider (-60 to +12 dB)
    setupDynSlider(dynThresholdSlider, "dyn_threshold", " dB", -60.0, 12.0, 0.0);
    dynThresholdSlider->setTooltip("Dynamics threshold: Level above which gain reduction begins");
    dynThresholdSlider->textFromValueFunction = [](double value) {
        juce::String sign = value >= 0 ? "+" : "";
        return sign + juce::String(value, 1) + " dB";
    };

    // Attack slider (0.1 to 500 ms)
    setupDynSlider(dynAttackSlider, "dyn_attack", " ms", 0.1, 500.0, 10.0);
    dynAttackSlider->setTooltip("Attack time: How fast the dynamic EQ responds to transients");
    dynAttackSlider->setSkewFactorFromMidPoint(20.0);  // More resolution for fast attacks

    // Release slider (10 to 5000 ms)
    setupDynSlider(dynReleaseSlider, "dyn_release", " ms", 10.0, 5000.0, 100.0);
    dynReleaseSlider->setTooltip("Release time: How fast the dynamic EQ recovers after the signal drops");
    dynReleaseSlider->setSkewFactorFromMidPoint(200.0);  // More resolution for faster releases

    // Range slider (0 to 24 dB)
    setupDynSlider(dynRangeSlider, "dyn_range", " dB", 0.0, 24.0, 12.0);
    dynRangeSlider->setTooltip("Maximum gain change applied by the dynamic EQ");

    // Section label (compact)
    dynSectionLabel.setText("DYN", juce::dontSendNotification);
    dynSectionLabel.setJustificationType(juce::Justification::centred);
    dynSectionLabel.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    dynSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00cc66));  // Green accent
    dynSectionLabel.setVisible(false);
    addAndMakeVisible(dynSectionLabel);

    // Parameter labels (compact abbreviations for control bar)
    setupDynLabel(dynThresholdLabel, "Th");
    setupDynLabel(dynAttackLabel, "At");
    setupDynLabel(dynReleaseLabel, "Re");
    setupDynLabel(dynRangeLabel, "Rn");
}

void MultiQEditor::layoutDynamicControls()
{
    // Only layout if in Digital mode (which now includes per-band dynamics)
    if (isBritishMode || isTubeEQMode)
        return;

    // Hide old inline dynamics controls - they're now in BandDetailPanel
    dynSectionLabel.setVisible(false);
    dynEnableButton->setVisible(false);
    dynThresholdLabel.setVisible(false);
    dynThresholdSlider->setVisible(false);
    dynAttackLabel.setVisible(false);
    dynAttackSlider->setVisible(false);
    dynReleaseLabel.setVisible(false);
    dynReleaseSlider->setVisible(false);
    dynRangeLabel.setVisible(false);
    dynRangeSlider->setVisible(false);
}

void MultiQEditor::updateDynamicAttachments()
{
    dynEnableAttachment.reset();
    dynThresholdAttachment.reset();
    dynAttackAttachment.reset();
    dynReleaseAttachment.reset();
    dynRangeAttachment.reset();

    // Only create attachments if we have a valid selected band and are in Digital mode
    bool isDigitalStyleMode = !isBritishMode && !isTubeEQMode;
    if (!isDigitalStyleMode || selectedBand < 0 || selectedBand >= 8)
        return;

    int bandNum = selectedBand + 1;  // Parameters use 1-based indexing

    // Create new attachments for the selected band
    dynEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::bandDynEnabled(bandNum), *dynEnableButton);

    dynThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynThreshold(bandNum), *dynThresholdSlider);

    dynAttackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynAttack(bandNum), *dynAttackSlider);

    dynReleaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynRelease(bandNum), *dynReleaseSlider);

    dynRangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynRange(bandNum), *dynRangeSlider);
}

// Factory Preset Methods

void MultiQEditor::updatePresetSelector()
{
    if (!presetSelector)
        return;

    presetSelector->clear();

    // Add factory presets (IDs 1 to numFactoryPresets)
    int numFactoryPresets = processor.getNumPrograms();
    for (int i = 0; i < numFactoryPresets; ++i)
    {
        juce::String name = processor.getProgramName(i);
        if (name.isEmpty())
            name = "Preset " + juce::String(i);
        presetSelector->addItem(name, i + 1);  // ComboBox uses 1-based IDs
    }

    // Add user presets (IDs starting at 1001)
    if (userPresetManager)
    {
        auto userPresets = userPresetManager->loadUserPresets();
        if (!userPresets.empty())
        {
            presetSelector->addSeparator();
            presetSelector->addSectionHeading("User Presets");

            for (size_t i = 0; i < userPresets.size(); ++i)
            {
                // User preset IDs start at 1001
                presetSelector->addItem(userPresets[i].name, static_cast<int>(1001 + i));
            }
        }
    }

    // Set current selection
    if (numFactoryPresets > 0)
        presetSelector->setSelectedId(processor.getCurrentProgram() + 1, juce::dontSendNotification);
}

void MultiQEditor::refreshUserPresets()
{
    // Remember current selection
    int currentId = presetSelector ? presetSelector->getSelectedId() : 0;

    updatePresetSelector();

    // Restore selection if possible
    if (currentId > 0 && presetSelector)
        presetSelector->setSelectedId(currentId, juce::dontSendNotification);
}

void MultiQEditor::onPresetSelected()
{
    if (!presetSelector)
        return;

    int selectedId = presetSelector->getSelectedId();
    if (selectedId <= 0)
        return;

    if (selectedId >= 1001)
    {
        // User preset selected
        int userPresetIndex = selectedId - 1001;
        if (userPresetManager)
        {
            auto userPresets = userPresetManager->loadUserPresets();
            if (userPresetIndex >= 0 && userPresetIndex < static_cast<int>(userPresets.size()))
            {
                loadUserPreset(userPresets[static_cast<size_t>(userPresetIndex)].name);
            }
        }
    }
    else
    {
        // Factory preset selected
        int presetIndex = selectedId - 1;  // Convert to 0-based
        processor.setCurrentProgram(presetIndex);
    }
}

void MultiQEditor::saveUserPreset()
{
    if (!userPresetManager)
        return;

    // Show dialog to get preset name
    auto* dialog = new juce::AlertWindow("Save Preset",
                                          "Enter a name for this preset:",
                                          juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor("name", "My Preset", "Preset Name:");
    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // Use SafePointer to handle case where editor is deleted while dialog is open
    juce::Component::SafePointer<MultiQEditor> safeThis(this);

    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis, dialog](int result) mutable
        {
            // Dialog is auto-deleted by enterModalState (deleteWhenDismissed=true)
            // so we must extract name before any potential deletion
            juce::String name;
            if (result == 1)
                name = dialog->getTextEditorContents("name").trim();

            // Check if editor still exists
            if (safeThis == nullptr || name.isEmpty())
                return;

            // Check if preset exists
            if (safeThis->userPresetManager->presetExists(name))
            {
                // Ask to overwrite - use another SafePointer for nested callback
                juce::Component::SafePointer<MultiQEditor> safeThisInner(safeThis.getComponent());

                juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::QuestionIcon,
                    "Overwrite Preset?",
                    "A preset named \"" + name + "\" already exists. Overwrite it?",
                    "Overwrite", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create([safeThisInner, name](int confirmResult) {
                        if (confirmResult == 1 && safeThisInner != nullptr)
                        {
                            auto state = safeThisInner->processor.parameters.copyState();
                            if (safeThisInner->userPresetManager->saveUserPreset(name, state, MultiQ::PLUGIN_VERSION))
                            {
                                safeThisInner->refreshUserPresets();
                            }
                        }
                    }));
            }
            else
            {
                auto state = safeThis->processor.parameters.copyState();
                if (safeThis->userPresetManager->saveUserPreset(name, state, MultiQ::PLUGIN_VERSION))
                {
                    safeThis->refreshUserPresets();
                }
            }
        }), true);  // true = deleteWhenDismissed, so don't manually delete dialog
}

void MultiQEditor::loadUserPreset(const juce::String& name)
{
    if (!userPresetManager)
        return;

    auto state = userPresetManager->loadUserPreset(name);
    if (state.isValid())
    {
        processor.parameters.replaceState(state);
    }
}

void MultiQEditor::deleteUserPreset(const juce::String& name)
{
    if (!userPresetManager)
        return;

    userPresetManager->deleteUserPreset(name);
    refreshUserPresets();
}

// Undo/Redo System

void MultiQEditor::updateUndoRedoButtons()
{
    auto& undoManager = processor.getUndoManager();
    undoButton.setEnabled(undoManager.canUndo());
    redoButton.setEnabled(undoManager.canRedo());

    // Update button appearance based on enabled state
    undoButton.setAlpha(undoManager.canUndo() ? 1.0f : 0.4f);
    redoButton.setAlpha(undoManager.canRedo() ? 1.0f : 0.4f);
}

// Digital Mode A/B Comparison

void MultiQEditor::toggleDigitalAB()
{
    // Digital mode: save/restore band parameters + digital-mode globals
    auto digitalFilter = [](const juce::String& id) {
        return id.startsWith("band")
            || id == ParamIDs::masterGain
            || id == ParamIDs::qCoupleMode
            || id == ParamIDs::autoGainEnabled
            || id == ParamIDs::dynDetectionMode
            || id == ParamIDs::limiterEnabled
            || id == ParamIDs::limiterCeiling
            || id == ParamIDs::matchStrength;
    };

    copyModeParamsToState(digitalIsStateA ? digitalStateA : digitalStateB, digitalFilter);

    digitalIsStateA = !digitalIsStateA;

    if (digitalIsStateA)
    {
        if (digitalStateA.isValid())
            applyModeParams(digitalStateA);
        digitalAbButton.setButtonText("A");
        digitalAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));
    }
    else
    {
        if (digitalStateB.isValid())
            applyModeParams(digitalStateB);
        digitalAbButton.setButtonText("B");
        digitalAbButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6a3a3a));
    }
}

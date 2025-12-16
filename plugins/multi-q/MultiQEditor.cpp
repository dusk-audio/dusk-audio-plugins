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

    analyzerDecaySlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
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
        // ===== PULTEC/TUBE MODE HEADER (Vintage cream/ivory style) =====
        // Warm vintage background
        g.setColour(juce::Colour(0xFF2a2520));  // Dark warm brown
        g.fillRect(0, 0, bounds.getWidth(), 50);

        // Header bottom border with gold accent
        g.setColour(juce::Colour(0xFF8b7355));  // Antique brass
        g.fillRect(0, 49, bounds.getWidth(), 2);

        // Plugin name
        titleClickArea = juce::Rectangle<int>(105, 8, 200, 35);
        g.setFont(juce::Font(juce::FontOptions(26.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xFFe8dcc8));  // Cream/ivory
        g.drawText("Multi-Q", 105, 8, 200, 28, juce::Justification::left);

        // Subtitle
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.setColour(juce::Colour(0xFFb0a090));
        g.drawText("Tube EQ (Pultec-Style)", 105, 32, 200, 16, juce::Justification::left);

        // Draw "TUBE" badge
        g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        auto tubeRect = juce::Rectangle<float>(static_cast<float>(getWidth()) - 100.0f, 13.0f, 70.0f, 24.0f);

        // Gold/brass gradient
        juce::ColourGradient badgeGradient(
            juce::Colour(0xFF9a8050), tubeRect.getX(), tubeRect.getY(),
            juce::Colour(0xFF6a5030), tubeRect.getX(), tubeRect.getBottom(), false);
        g.setGradientFill(badgeGradient);
        g.fillRoundedRectangle(tubeRect, 4.0f);

        // Border
        g.setColour(juce::Colour(0xFFc0a060));
        g.drawRoundedRectangle(tubeRect.reduced(0.5f), 4.0f, 1.0f);

        // Text
        g.setColour(juce::Colour(0xFFf0e8d8));
        g.drawText("TUBE", tubeRect, juce::Justification::centred);

        // Luna Co. branding
        g.setColour(juce::Colour(0xFF8a7a6a));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("Luna Co. Audio", getWidth() - 190, 35, 80, 14, juce::Justification::centredRight);
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
        // ===== PULTEC MODE PAINT (Vintage style content area) =====
        // Warm control panel background
        int controlTop = pultecCurveCollapsed ? 55 : 305;
        auto controlPanelArea = juce::Rectangle<int>(5, controlTop, getWidth() - 10, getHeight() - controlTop - 10);
        g.setColour(juce::Colour(0xFF252015));  // Dark warm brown
        g.fillRoundedRectangle(controlPanelArea.toFloat(), 6.0f);

        // Section dividers and labels drawn in layoutPultecControls via label components
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
    }
    else
    {
        // ===== DIGITAL MODE PAINT =====
        // Control panel background
        auto controlPanelArea = juce::Rectangle<int>(10, getHeight() - 100, getWidth() - 20, 90);
        g.setColour(juce::Colour(0xFF202020));
        g.fillRoundedRectangle(controlPanelArea.toFloat(), 4.0f);
    }

    // Separator line only for digital mode
    if (!isBritishMode && !isPultecMode)
    {
        g.setColour(juce::Colour(0xFF333333));
        g.drawHorizontalLine(50, 0, static_cast<float>(getWidth()));
    }
}

void MultiQEditor::resized()
{
    auto bounds = getLocalBounds();

    if (isPultecMode)
    {
        // ===== PULTEC/TUBE MODE LAYOUT =====
        layoutPultecControls();

        // Position EQ type selector in header
        eqTypeSelector->setBounds(10, 15, 85, 28);

        // Position meters
        int meterY = pultecCurveCollapsed ? 80 : 320;
        int meterWidth = LEDMeterStyle::standardWidth;
        int meterHeight = getHeight() - meterY - LEDMeterStyle::valueHeight - LEDMeterStyle::labelSpacing - 10;
        inputMeter->setBounds(6, meterY, meterWidth, meterHeight);
        outputMeter->setBounds(getWidth() - meterWidth - 10, meterY, meterWidth, meterHeight);

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

        // Brown/Black mode selector - center-right area like 4K-EQ
        britishModeSelector->setBounds(getWidth() - 110, headerY, 95, headerControlHeight);

        // Hide Graph button - positioned like 4K-EQ (leaves 5px gap before badge)
        // Badge is at getWidth()-195, so button ends at getWidth()-200
        britishCurveCollapseButton.setBounds(getWidth() - 295, 17, 90, 24);

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
        // ===== DIGITAL MODE LAYOUT (original) =====
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
        int buttonSpacing = 6;
        int totalButtonsWidth = 8 * buttonWidth + 7 * buttonSpacing;
        int startX = (getWidth() - totalButtonsWidth) / 2;

        for (int i = 0; i < 8; ++i)
        {
            bandEnableButtons[static_cast<size_t>(i)]->setBounds(
                startX + i * (buttonWidth + buttonSpacing),
                toolbarY,
                buttonWidth, buttonHeight);
        }

        // Right side controls (Stereo, HQ, Bypass) on the toolbar
        int rightX = getWidth() - 15;
        bypassButton->setBounds(rightX - 70, toolbarY, 65, controlHeight);
        bypassButton->setVisible(true);
        rightX -= 75;
        hqButton->setBounds(rightX - 40, toolbarY, 38, controlHeight);
        hqButton->setVisible(true);
        rightX -= 45;
        processingModeSelector->setBounds(rightX - 75, toolbarY, 73, controlHeight);
        processingModeSelector->setVisible(true);

        // Bottom control panel
        auto controlPanel = bounds.removeFromBottom(100);

        // Meters on sides
        auto meterWidth = 30;
        inputMeter->setBounds(controlPanel.removeFromLeft(meterWidth).reduced(5, 10));
        outputMeter->setBounds(controlPanel.removeFromRight(meterWidth).reduced(5, 10));

        int labelHeight = 15;

        // Digital mode control panel layout (original)
        // Selected band controls (left part of control panel)
        auto selectedBandArea = controlPanel.removeFromLeft(350);
        selectedBandLabel.setBounds(selectedBandArea.removeFromTop(25).reduced(10, 2));

        auto knobsArea = selectedBandArea;
        int knobWidth = 70;

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

        // Row 1: Q-Couple (wide dropdown) | Display Scale
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
        int decaySliderWidth = juce::jmax(80, areaWidth - 95);
        analyzerDecaySlider->setBounds(decaySliderX, ctrlY, decaySliderWidth, ctrlHeight);
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

//==============================================================================
// British Mode UI
//==============================================================================

void MultiQEditor::setupBritishControls()
{
    // Setup British mode sliders using FourKLookAndFeel (exact 4K-EQ style)
    // This helper sets up a knob exactly like 4K-EQ does
    auto setupBritishKnob = [this](std::unique_ptr<juce::Slider>& slider, const juce::String& name,
                                   bool centerDetented, juce::Colour color) {
        slider = std::make_unique<juce::Slider>();
        slider->setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider->setPopupDisplayEnabled(true, true, this);
        slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                     juce::MathConstants<float>::pi * 2.75f, true);
        slider->setScrollWheelEnabled(true);
        slider->setVelocityBasedMode(true);
        slider->setVelocityModeParameters(1.0, 1, 0.1, false);
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

    selectedBandLabel.setVisible(isDigitalMode);
    freqSlider->setVisible(isDigitalMode);
    gainSlider->setVisible(isDigitalMode);
    qSlider->setVisible(isDigitalMode);
    freqLabel.setVisible(isDigitalMode);
    gainLabel.setVisible(isDigitalMode);
    qLabel.setVisible(isDigitalMode);
    qCoupleModeSelector->setVisible(isDigitalMode);
    masterGainSlider->setVisible(isDigitalMode);
    masterGainLabel.setVisible(isDigitalMode);

    // Hide/show graphic displays based on mode
    graphicDisplay->setVisible(isDigitalMode);

    // British mode curve display (only visible if British mode and not collapsed)
    if (britishCurveDisplay)
        britishCurveDisplay->setVisible(isBritishMode && !britishCurveCollapsed);

    // Pultec mode curve display (only visible if Pultec mode and not collapsed)
    if (pultecCurveDisplay)
        pultecCurveDisplay->setVisible(isPultecMode && !pultecCurveCollapsed);

    // Hide analyzer controls in British/Pultec modes
    analyzerButton->setVisible(isDigitalMode);
    analyzerPrePostButton->setVisible(isDigitalMode);
    analyzerModeSelector->setVisible(isDigitalMode);
    analyzerResolutionSelector->setVisible(isDigitalMode);
    analyzerDecaySlider->setVisible(isDigitalMode);
    displayScaleSelector->setVisible(isDigitalMode);

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

    // Section labels - we draw text in paint() so hide the Label components
    // (The old Labels aren't needed since we draw text directly in paint())
    britishFiltersLabel.setVisible(false);
    britishLfLabel.setVisible(false);
    britishLmfLabel.setVisible(false);
    britishHmfLabel.setVisible(false);
    britishHfLabel.setVisible(false);
    britishMasterLabel.setVisible(false);

    // British knob labels
    britishHpfKnobLabel.setVisible(isBritishMode);
    britishLpfKnobLabel.setVisible(isBritishMode);
    britishInputKnobLabel.setVisible(isBritishMode);
    britishLfGainKnobLabel.setVisible(isBritishMode);
    britishLfFreqKnobLabel.setVisible(isBritishMode);
    britishLmGainKnobLabel.setVisible(isBritishMode);
    britishLmFreqKnobLabel.setVisible(isBritishMode);
    britishLmQKnobLabel.setVisible(isBritishMode);
    britishHmGainKnobLabel.setVisible(isBritishMode);
    britishHmFreqKnobLabel.setVisible(isBritishMode);
    britishHmQKnobLabel.setVisible(isBritishMode);
    britishHfGainKnobLabel.setVisible(isBritishMode);
    britishHfFreqKnobLabel.setVisible(isBritishMode);
    britishSatKnobLabel.setVisible(isBritishMode);
    britishOutputKnobLabel.setVisible(isBritishMode);

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
void MultiQEditor::setupPultecControls()
{
    // Create Pultec curve display
    pultecCurveDisplay = std::make_unique<PultecCurveDisplay>(processor);
    pultecCurveDisplay->setVisible(false);
    addAndMakeVisible(pultecCurveDisplay.get());

    // Helper to setup Pultec-style rotary knob
    auto setupPultecKnob = [this](std::unique_ptr<juce::Slider>& slider, const juce::String& name) {
        slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::NoTextBox);
        slider->setName(name);
        slider->setLookAndFeel(&pultecLookAndFeel);
        slider->setVisible(false);
        addAndMakeVisible(slider.get());
    };

    // Helper to setup Pultec-style combo selector
    auto setupPultecSelector = [this](std::unique_ptr<juce::ComboBox>& combo) {
        combo = std::make_unique<juce::ComboBox>();
        combo->setLookAndFeel(&pultecLookAndFeel);
        combo->setVisible(false);
        addAndMakeVisible(combo.get());
    };

    // Helper to setup knob label
    auto setupKnobLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour(0xff3a3030));
        label.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
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

    // Section labels
    auto setupSectionLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour(0xff3a3030));
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
    setupKnobLabel(pultecHfBwKnobLabel, "BANDWIDTH");
    setupKnobLabel(pultecHfAttenKnobLabel, "ATTEN");
    setupKnobLabel(pultecHfAttenFreqKnobLabel, "KCS");
    setupKnobLabel(pultecInputKnobLabel, "INPUT");
    setupKnobLabel(pultecOutputKnobLabel, "OUTPUT");
    setupKnobLabel(pultecTubeKnobLabel, "DRIVE");

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
}

void MultiQEditor::layoutPultecControls()
{
    auto bounds = getLocalBounds();

    // Pultec layout - similar structure to British mode but with Pultec-style arrangement
    // Header: 50px, Curve display: ~250px, Controls: remaining

    const int headerHeight = 50;
    const int curveHeight = pultecCurveCollapsed ? 0 : 250;
    const int controlHeight = bounds.getHeight() - headerHeight - curveHeight - 20;

    // Curve display position
    if (pultecCurveDisplay)
    {
        pultecCurveDisplay->setBounds(10, headerHeight + 5, bounds.getWidth() - 20, curveHeight - 10);
    }

    // Control panel position
    int controlY = headerHeight + curveHeight + 10;
    auto controlPanel = juce::Rectangle<int>(10, controlY, bounds.getWidth() - 20, controlHeight);

    // Layout constants
    const int knobSize = 65;
    const int labelHeight = 16;
    const int labelY = controlPanel.getY();
    const int knobY = labelY + labelHeight + 8;
    const int knobLabelY = knobY + knobSize + 2;

    // Divide into sections: LF | HF Boost | HF Atten | Master
    const int numSections = 4;
    const int sectionWidth = controlPanel.getWidth() / numSections;

    auto centerInSection = [&](juce::Component& comp, int section, int y, int w, int h) {
        int sectionStart = controlPanel.getX() + section * sectionWidth;
        int sectionCenter = sectionStart + sectionWidth / 2;
        comp.setBounds(sectionCenter - w / 2, y, w, h);
    };

    auto positionLabelBelow = [&](juce::Label& label, juce::Slider& knob) {
        label.setBounds(knob.getX() - 10, knob.getBottom() + 2, knob.getWidth() + 20, 14);
    };

    // Section 0: LOW FREQUENCY
    pultecLfLabel.setBounds(controlPanel.getX(), labelY, sectionWidth, labelHeight);

    int lfSectionX = controlPanel.getX();
    int lfKnobSpacing = (sectionWidth - 2 * knobSize) / 3;

    // LF Boost knob
    pultecLfBoostSlider->setBounds(lfSectionX + lfKnobSpacing, knobY, knobSize, knobSize);
    positionLabelBelow(pultecLfBoostKnobLabel, *pultecLfBoostSlider);

    // LF Atten knob
    pultecLfAttenSlider->setBounds(lfSectionX + 2 * lfKnobSpacing + knobSize, knobY, knobSize, knobSize);
    positionLabelBelow(pultecLfAttenKnobLabel, *pultecLfAttenSlider);

    // LF Freq selector (centered below knobs)
    pultecLfFreqSelector->setBounds(lfSectionX + (sectionWidth - 80) / 2, knobY + knobSize + 20, 80, 24);
    pultecLfFreqKnobLabel.setBounds(pultecLfFreqSelector->getX() - 10, pultecLfFreqSelector->getBottom() + 2, 100, 14);

    // Section 1: HIGH FREQUENCY BOOST
    pultecHfBoostLabel.setBounds(controlPanel.getX() + sectionWidth, labelY, sectionWidth, labelHeight);

    int hfBoostSectionX = controlPanel.getX() + sectionWidth;
    int hfKnobSpacing = (sectionWidth - 3 * knobSize) / 4;

    // HF Boost knob
    pultecHfBoostSlider->setBounds(hfBoostSectionX + hfKnobSpacing, knobY, knobSize, knobSize);
    positionLabelBelow(pultecHfBoostKnobLabel, *pultecHfBoostSlider);

    // HF Bandwidth knob
    pultecHfBandwidthSlider->setBounds(hfBoostSectionX + 2 * hfKnobSpacing + knobSize, knobY, knobSize, knobSize);
    positionLabelBelow(pultecHfBwKnobLabel, *pultecHfBandwidthSlider);

    // HF Boost Freq selector
    pultecHfBoostFreqSelector->setBounds(hfBoostSectionX + (sectionWidth - 70) / 2, knobY + knobSize + 20, 70, 24);
    pultecHfBoostFreqKnobLabel.setBounds(pultecHfBoostFreqSelector->getX() - 5, pultecHfBoostFreqSelector->getBottom() + 2, 80, 14);

    // Section 2: HIGH FREQUENCY ATTEN
    pultecHfAttenLabel.setBounds(controlPanel.getX() + 2 * sectionWidth, labelY, sectionWidth, labelHeight);

    int hfAttenSectionX = controlPanel.getX() + 2 * sectionWidth;

    // HF Atten knob (centered)
    centerInSection(*pultecHfAttenSlider, 2, knobY, knobSize, knobSize);
    positionLabelBelow(pultecHfAttenKnobLabel, *pultecHfAttenSlider);

    // HF Atten Freq selector
    pultecHfAttenFreqSelector->setBounds(hfAttenSectionX + (sectionWidth - 70) / 2, knobY + knobSize + 20, 70, 24);
    pultecHfAttenFreqKnobLabel.setBounds(pultecHfAttenFreqSelector->getX() - 5, pultecHfAttenFreqSelector->getBottom() + 2, 80, 14);

    // Section 3: MASTER
    pultecMasterLabel.setBounds(controlPanel.getX() + 3 * sectionWidth, labelY, sectionWidth, labelHeight);

    int masterSectionX = controlPanel.getX() + 3 * sectionWidth;
    int masterKnobSpacing = (sectionWidth - 3 * knobSize) / 4;

    // Input gain knob
    pultecInputGainSlider->setBounds(masterSectionX + masterKnobSpacing, knobY, knobSize, knobSize);
    positionLabelBelow(pultecInputKnobLabel, *pultecInputGainSlider);

    // Tube drive knob
    pultecTubeDriveSlider->setBounds(masterSectionX + 2 * masterKnobSpacing + knobSize, knobY, knobSize, knobSize);
    positionLabelBelow(pultecTubeKnobLabel, *pultecTubeDriveSlider);

    // Output gain knob
    pultecOutputGainSlider->setBounds(masterSectionX + 3 * masterKnobSpacing + 2 * knobSize, knobY, knobSize, knobSize);
    positionLabelBelow(pultecOutputKnobLabel, *pultecOutputGainSlider);
}

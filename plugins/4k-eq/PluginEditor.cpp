#include "PluginEditor.h"

//==============================================================================
FourKEQEditor::FourKEQEditor(FourKEQ& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Initialize scalable resize helper with persistence
    // Base size: 950x640, Min: 760x512 (80%), Max: 1425x960 (150%)
    resizeHelper.initialize(this, &audioProcessor, 950, 640, 760, 512, 1425, 960, false);
    setSize(resizeHelper.getStoredWidth(), resizeHelper.getStoredHeight());

    // Get parameter references
    eqTypeParam = audioProcessor.parameters.getRawParameterValue("eq_type");
    bypassParam = audioProcessor.parameters.getRawParameterValue("bypass");

    // HPF Section
    setupKnob(hpfFreqSlider, "hpf_freq", "HPF");
    hpfFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hpf_freq", hpfFreqSlider);
    setupButton(hpfEnableButton, "IN");
    hpfEnableAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "hpf_enabled", hpfEnableButton);

    // LPF Section
    setupKnob(lpfFreqSlider, "lpf_freq", "LPF");
    lpfFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lpf_freq", lpfFreqSlider);
    setupButton(lpfEnableButton, "IN");
    lpfEnableAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "lpf_enabled", lpfEnableButton);

    // Input Gain (below filters)
    setupKnob(inputGainSlider, "input_gain", "INPUT", true);
    inputGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "input_gain", inputGainSlider);

    // LF Band
    setupKnob(lfGainSlider, "lf_gain", "GAIN", true);  // Center-detented
    lfGainSlider.setName("lf_gain");  // Set name for color detection
    lfGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lf_gain", lfGainSlider);

    setupKnob(lfFreqSlider, "lf_freq", "FREQ");
    lfFreqSlider.setName("lf_freq");
    lfFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lf_freq", lfFreqSlider);

    setupButton(lfBellButton, "BELL");
    lfBellAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "lf_bell", lfBellButton);

    // LM Band
    setupKnob(lmGainSlider, "lm_gain", "GAIN", true);
    lmGainSlider.setName("lmf_gain");  // Use lmf for lo-mid detection
    lmGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lm_gain", lmGainSlider);

    setupKnob(lmFreqSlider, "lm_freq", "FREQ");
    lmFreqSlider.setName("lmf_freq");
    lmFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lm_freq", lmFreqSlider);

    setupKnob(lmQSlider, "lm_q", "Q");
    lmQSlider.setName("lmf_q");
    lmQAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lm_q", lmQSlider);

    // HM Band
    setupKnob(hmGainSlider, "hm_gain", "GAIN", true);
    hmGainSlider.setName("hmf_gain");  // Use hmf for hi-mid detection
    hmGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hm_gain", hmGainSlider);

    setupKnob(hmFreqSlider, "hm_freq", "FREQ");
    hmFreqSlider.setName("hmf_freq");
    hmFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hm_freq", hmFreqSlider);

    setupKnob(hmQSlider, "hm_q", "Q");
    hmQSlider.setName("hmf_q");
    hmQAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hm_q", hmQSlider);

    // HF Band
    setupKnob(hfGainSlider, "hf_gain", "GAIN", true);
    hfGainSlider.setName("hf_gain");  // Set name for color detection
    hfGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hf_gain", hfGainSlider);

    setupKnob(hfFreqSlider, "hf_freq", "FREQ");
    hfFreqSlider.setName("hf_freq");
    hfFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hf_freq", hfFreqSlider);

    setupButton(hfBellButton, "BELL");
    hfBellAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "hf_bell", hfBellButton);

    // Master Section
    setupButton(bypassButton, "BYPASS");
    bypassButton.setClickingTogglesState(true);
    bypassAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "bypass", bypassButton);

    setupButton(autoGainButton, "AUTO GAIN");
    autoGainButton.setClickingTogglesState(true);
    autoGainAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "auto_gain", autoGainButton);

    // A/B Comparison button
    abButton.setButtonText("A");
    abButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));  // Green for A
    abButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    abButton.onClick = [this]() { toggleAB(); };
    abButton.setTooltip("A/B Comparison: Click to switch between two settings. Current settings are saved when switching.");
    addAndMakeVisible(abButton);

    // Initialize A state with current parameters
    stateA = audioProcessor.parameters.copyState();
    stateB = audioProcessor.parameters.copyState();

    setupKnob(outputGainSlider, "output_gain", "OUTPUT", true);
    outputGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "output_gain", outputGainSlider);

    setupKnob(saturationSlider, "saturation", "SAT");
    saturationAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "saturation", saturationSlider);

    // EQ Type selector (styled as SSL switch)
    eqTypeSelector.addItem("BROWN", 1);
    eqTypeSelector.addItem("BLACK", 2);
    eqTypeSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    eqTypeSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    eqTypeSelector.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff808080));
    addAndMakeVisible(eqTypeSelector);
    eqTypeAttachment = std::make_unique<ComboBoxAttachment>(
        audioProcessor.parameters, "eq_type", eqTypeSelector);

    // Preset selector
    for (int i = 0; i < audioProcessor.getNumPrograms(); ++i)
    {
        presetSelector.addItem(audioProcessor.getProgramName(i), i + 1);
    }
    presetSelector.setSelectedId(audioProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetSelector.onChange = [this]()
    {
        int presetIndex = presetSelector.getSelectedId() - 1;
        if (presetIndex >= 0 && presetIndex < audioProcessor.getNumPrograms())
        {
            audioProcessor.setCurrentProgram(presetIndex);
        }
    };
    presetSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    presetSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    presetSelector.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff808080));
    addAndMakeVisible(presetSelector);

    // Oversampling selector
    oversamplingSelector.addItem("Oversample: 2x", 1);
    oversamplingSelector.addItem("Oversample: 4x", 2);
    oversamplingSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    oversamplingSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    oversamplingSelector.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff808080));
    addAndMakeVisible(oversamplingSelector);
    oversamplingAttachment = std::make_unique<ComboBoxAttachment>(
        audioProcessor.parameters, "oversampling", oversamplingSelector);

    // Section labels removed - section headers at top are sufficient
    // (FILTERS, LF, LMF, HMF, HF labels not needed)

    // Setup parameter labels (small text below each knob like SSL)
    auto setupParamLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffa0a0a0));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
    };

    // Frequency range labels removed - tick marks now show all frequencies
    // Adding functional labels for all knobs

    setupParamLabel(hpfLabel, "HPF");
    setupParamLabel(lpfLabel, "LPF");
    setupParamLabel(inputLabel, "INPUT");
    setupParamLabel(lfGainLabel, "GAIN");
    setupParamLabel(lfFreqLabel, "FREQ");
    setupParamLabel(lmGainLabel, "GAIN");
    setupParamLabel(lmFreqLabel, "FREQ");
    setupParamLabel(lmQLabel, "Q");
    setupParamLabel(hmGainLabel, "GAIN");
    setupParamLabel(hmFreqLabel, "FREQ");
    setupParamLabel(hmQLabel, "Q");
    setupParamLabel(hfGainLabel, "GAIN");
    setupParamLabel(hfFreqLabel, "FREQ");
    setupParamLabel(outputLabel, "OUTPUT");
    setupParamLabel(satLabel, "DRIVE");

    // Add tooltips to all controls for better UX
    hpfFreqSlider.setTooltip("High-Pass Filter Frequency (20Hz - 500Hz)");
    hpfEnableButton.setTooltip("Enable/Disable High-Pass Filter (IN = active)");
    lpfFreqSlider.setTooltip("Low-Pass Filter Frequency (5kHz - 20kHz)");
    lpfEnableButton.setTooltip("Enable/Disable Low-Pass Filter (IN = active)");

    lfGainSlider.setTooltip("Low Frequency Gain (\u00B115dB)");
    lfFreqSlider.setTooltip("Low Frequency (30Hz - 450Hz)");
    lfBellButton.setTooltip("Toggle between Shelf and Bell curve");

    lmGainSlider.setTooltip("Low-Mid Frequency Gain (\u00B115dB)");
    lmFreqSlider.setTooltip("Low-Mid Frequency (200Hz - 2.5kHz)");
    lmQSlider.setTooltip("Low-Mid Q/Bandwidth (0.5 - 4.0)");

    hmGainSlider.setTooltip("High-Mid Frequency Gain (\u00B115dB)");
    hmFreqSlider.setTooltip("High-Mid Frequency (600Hz - 7kHz)");
    hmQSlider.setTooltip("High-Mid Q/Bandwidth (0.5 - 4.0)");

    hfGainSlider.setTooltip("High Frequency Gain (\u00B115dB)");
    hfFreqSlider.setTooltip("High Frequency (3kHz - 20kHz)");
    hfBellButton.setTooltip("Toggle between Shelf and Bell curve");

    outputGainSlider.setTooltip("Output Gain (\u00B118dB)");
    saturationSlider.setTooltip("Analog Saturation Amount (0-100%)");

    eqTypeSelector.setTooltip("Brown: E-Series (musical, fixed Q) | Black: G-Series (surgical, variable Q)");
    presetSelector.setTooltip("Select factory preset");
    oversamplingSelector.setTooltip("Oversampling (2x/4x): Eliminates aliasing for cleaner high-frequency EQ, at the cost of increased CPU usage");
    bypassButton.setTooltip("Bypass all EQ processing");
    autoGainButton.setTooltip("Auto Gain Compensation: Automatically adjusts output to maintain consistent loudness when boosting/cutting");

    // EQ Curve Display - add before meters so meters appear on top
    eqCurveDisplay = std::make_unique<EQCurveDisplay>(audioProcessor);
    addAndMakeVisible(eqCurveDisplay.get());

    // Collapse/expand button for EQ curve (in header)
    curveCollapseButton.setButtonText("Hide");
    curveCollapseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
    curveCollapseButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa0a0a0));
    curveCollapseButton.setTooltip("Show/Hide frequency response graph");
    curveCollapseButton.onClick = [this]() {
        isCurveCollapsed = !isCurveCollapsed;
        curveCollapseButton.setButtonText(isCurveCollapsed ? "Show" : "Hide");
        if (eqCurveDisplay)
            eqCurveDisplay->setVisible(!isCurveCollapsed);

        // Resize the window based on graph visibility
        int newHeight = isCurveCollapsed ? 530 : 640;
        setSize(getWidth(), newHeight);
    };
    addAndMakeVisible(curveCollapseButton);

    // Display scale selector for EQ graph dB range
    displayScaleSelector.addItem(juce::CharPointer_UTF8("\xc2\xb1" "12 dB"), 1);
    displayScaleSelector.addItem(juce::CharPointer_UTF8("\xc2\xb1" "24 dB"), 2);
    displayScaleSelector.addItem(juce::CharPointer_UTF8("\xc2\xb1" "30 dB"), 3);
    displayScaleSelector.addItem(juce::CharPointer_UTF8("\xc2\xb1" "60 dB"), 4);
    displayScaleSelector.addItem("Warped", 5);
    displayScaleSelector.setSelectedId(2);  // Default to ±24dB
    displayScaleSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    displayScaleSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffa0a0a0));
    displayScaleSelector.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff404040));
    displayScaleSelector.setTooltip("Select dB range for EQ graph visualization");
    displayScaleSelector.onChange = [this]() {
        if (eqCurveDisplay)
            eqCurveDisplay->setDisplayScaleMode(
                static_cast<DisplayScaleMode>(displayScaleSelector.getSelectedId() - 1));
    };
    addAndMakeVisible(displayScaleSelector);

    // Set initial bounds for EQ curve display so it's visible on first paint
    int curveX = 35;
    int curveY = 58;
    int curveWidth = 950 - 70;  // Initial width based on default size
    int curveHeight = 105;
    eqCurveDisplay->setBounds(curveX, curveY, curveWidth, curveHeight);

    // Professional LED meters - add LAST so they're on top of other components
    inputMeterL = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    outputMeterL = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    addAndMakeVisible(inputMeterL.get());
    addAndMakeVisible(outputMeterL.get());

    // Set initial bounds so meters are visible on first paint - use standard width
    int initialMeterY = 185;  // Start lower to make room for EQ curve and labels
    int initialMeterHeight = 640 - initialMeterY - LEDMeterStyle::valueHeight - LEDMeterStyle::labelSpacing - 10;
    inputMeterL->setBounds(6, initialMeterY, LEDMeterStyle::standardWidth, initialMeterHeight);
    // Output meter - center under "OUTPUT" label
    outputMeterL->setBounds(950 - LEDMeterStyle::standardWidth - 10, initialMeterY, LEDMeterStyle::standardWidth, initialMeterHeight);

    // Note: Value readout labels removed - the tick marks around knobs already
    // show the parameter range, and current values can be seen from knob position

    // Start timer for UI updates
    startTimerHz(30);
}

FourKEQEditor::~FourKEQEditor()
{
    // Save window size for next session
    resizeHelper.saveSize();

    // CRITICAL: Stop timer first to prevent callbacks during destruction
    // This was causing crashes when touching controls in Ableton
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void FourKEQEditor::paint(juce::Graphics& g)
{
    // Unified Luna background
    g.fillAll(juce::Colour(0xff1a1a1a));  // Dark professional background

    auto bounds = getLocalBounds();

    // Draw header with subtle gradient
    juce::ColourGradient headerGradient(
        juce::Colour(0xff2d2d2d), 0, 0,
        juce::Colour(0xff252525), 0, 55, false);
    g.setGradientFill(headerGradient);
    g.fillRect(0, 0, bounds.getWidth(), 55);

    // Header bottom border
    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillRect(0, 54, bounds.getWidth(), 1);

    // Plugin name (clickable - shows supporters panel)
    titleClickArea = juce::Rectangle<int>(60, 10, 200, 40);
    g.setFont(juce::Font(juce::FontOptions(24.0f).withStyle("Bold")));
    g.setColour(juce::Colour(0xffe0e0e0));
    g.drawText("4K EQ", 60, 10, 200, 30, juce::Justification::left);

    // Subtitle with hint
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(juce::Colour(0xff909090));
    g.drawText("Console-Style Equalizer", 60, 32, 200, 20, juce::Justification::left);

    // EQ Type indicator badge - styled as muted amber/gold for Brown, dark grey for Black
    bool isBlack = eqTypeParam != nullptr && eqTypeParam->load() > 0.5f;
    g.setFont(juce::Font(juce::FontOptions(resizeHelper.scaled(11.0f)).withStyle("Bold")));

    // Position badge to the left of the dropdown (dropdown is at getWidth() - scaled(110))
    // Badge positioned at getWidth() - scaled(190), matching scaled layout
    float badgeX = static_cast<float>(getWidth()) - resizeHelper.scaled(190.0f);
    float badgeY = resizeHelper.scaled(17.0f);
    float badgeW = resizeHelper.scaled(70.0f);
    float badgeH = resizeHelper.scaled(24.0f);
    auto eqTypeRect = juce::Rectangle<float>(badgeX, badgeY, badgeW, badgeH);

    // Draw button background with subtle gradient
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

    // EQ Curve display area background (only when visible)
    if (!isCurveCollapsed)
    {
        auto curveArea = juce::Rectangle<int>(40, 60, bounds.getWidth() - 130, 100);
        // Background is drawn by the EQCurveDisplay component itself
    }

    // Main content area - adjust based on curve visibility (matches contentY in resized())
    int contentTopOffset = isCurveCollapsed ? resizeHelper.scaled(65) : resizeHelper.scaled(200);
    bounds = getLocalBounds().withTrimmedTop(contentTopOffset);  // Account for header + optional curve

    // Section X boundaries - scale proportionally to window width (same as resized())
    float widthRatio = static_cast<float>(getWidth()) / 950.0f;

    int filtersEnd = static_cast<int>(195 * widthRatio);
    int bandWidth = static_cast<int>(132 * widthRatio);
    int lfEnd = filtersEnd + 2 + bandWidth;
    int lmfEnd = lfEnd + 2 + bandWidth;
    int hmfEnd = lmfEnd + 2 + bandWidth;
    int hfEnd = hmfEnd + 2 + bandWidth;

    // Section dividers - vertical lines
    g.setColour(juce::Colour(0xff3a3a3a));

    // Filters section divider
    g.fillRect(filtersEnd, bounds.getY(), 2, bounds.getHeight());

    // EQ bands dividers
    g.fillRect(lfEnd, bounds.getY(), 2, bounds.getHeight());
    g.fillRect(lmfEnd, bounds.getY(), 2, bounds.getHeight());
    g.fillRect(hmfEnd, bounds.getY(), 2, bounds.getHeight());
    g.fillRect(hfEnd, bounds.getY(), 2, bounds.getHeight());

    // Section headers - larger with subtle background for visibility
    float fontSize = resizeHelper.scaled(13.0f);
    g.setFont(juce::Font(juce::FontOptions(fontSize).withStyle("Bold")));

    int labelY = bounds.getY() + resizeHelper.scaled(6);
    int labelHeight = resizeHelper.scaled(22);
    int labelTextHeight = resizeHelper.scaled(20);

    // Draw subtle background strips for section headers
    // Start FILTERS bar at scaled x=30 to avoid covering "INPUT" label on left
    int filtersLabelStart = resizeHelper.scaled(30);
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(filtersLabelStart, labelY - 2, filtersEnd - filtersLabelStart, labelHeight);

    // Draw section header text
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("FILTERS", filtersLabelStart, labelY, filtersEnd - filtersLabelStart, labelTextHeight,
               juce::Justification::centred);

    int xPos = filtersEnd + 2;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, bandWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("LF", xPos, labelY, bandWidth, labelTextHeight,
               juce::Justification::centred);

    xPos = lfEnd + 2;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, bandWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("LMF", xPos, labelY, bandWidth, labelTextHeight,
               juce::Justification::centred);

    xPos = lmfEnd + 2;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, bandWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("HMF", xPos, labelY, bandWidth, labelTextHeight,
               juce::Justification::centred);

    xPos = hmfEnd + 2;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, bandWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("HF", xPos, labelY, bandWidth, labelTextHeight,
               juce::Justification::centred);

    xPos = hfEnd + 2;
    // MASTER label width: calculate from xPos to right edge, minus output meter space
    int masterWidth = bounds.getRight() - xPos - resizeHelper.scaled(56);
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, masterWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("MASTER", xPos, labelY, masterWidth, labelTextHeight,
               juce::Justification::centred);

    // Frequency range indicators
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.setColour(juce::Colour(0xff808080));

    // Draw knob scale markings around each knob
    drawKnobMarkings(g);

    // Draw meter labels and values using standard LEDMeterStyle
    if (inputMeterL)
    {
        LEDMeterStyle::drawMeterLabels(g, inputMeterL->getBounds(), "INPUT", displayedInputLevel);
    }

    if (outputMeterL)
    {
        LEDMeterStyle::drawMeterLabels(g, outputMeterL->getBounds(), "OUTPUT", displayedOutputLevel);
    }

}

void FourKEQEditor::showSupportersPanel()
{
    if (!supportersOverlay)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>("4K EQ", JucePlugin_VersionString);
        supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
        addAndMakeVisible(supportersOverlay.get());
    }
    supportersOverlay->setBounds(getLocalBounds());
    supportersOverlay->toFront(true);
    supportersOverlay->setVisible(true);
}

void FourKEQEditor::hideSupportersPanel()
{
    if (supportersOverlay)
        supportersOverlay->setVisible(false);
}

void FourKEQEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
    {
        showSupportersPanel();
    }
}

void FourKEQEditor::resized()
{
    // Update the resize helper (positions corner handle and calculates scale)
    resizeHelper.updateResizer();

    auto bounds = getLocalBounds();

    // Header controls - preset and oversampling selectors
    auto headerBounds = bounds.removeFromTop(resizeHelper.scaled(60));
    int centerX = headerBounds.getCentreX();

    // A/B button (far left of header controls) - shifted left for more room
    abButton.setBounds(centerX - resizeHelper.scaled(280), resizeHelper.scaled(15),
                       resizeHelper.scaled(32), resizeHelper.scaled(28));

    // Preset selector (left of center) - shifted left
    presetSelector.setBounds(centerX - resizeHelper.scaled(240), resizeHelper.scaled(15),
                            resizeHelper.scaled(200), resizeHelper.scaled(28));

    // Oversampling selector - shifted left
    oversamplingSelector.setBounds(centerX - resizeHelper.scaled(20), resizeHelper.scaled(15),
                                   resizeHelper.scaled(130), resizeHelper.scaled(28));

    // EQ Type selector in header (upper right)
    eqTypeSelector.setBounds(getWidth() - resizeHelper.scaled(110), resizeHelper.scaled(15),
                            resizeHelper.scaled(95), resizeHelper.scaled(28));

    // Position collapse button - shifted left for more room
    curveCollapseButton.setBounds(centerX + resizeHelper.scaled(120), resizeHelper.scaled(17),
                                  resizeHelper.scaled(70), resizeHelper.scaled(24));

    // Display scale selector - positioned after collapse button (only visible when graph is shown)
    if (!isCurveCollapsed)
    {
        displayScaleSelector.setBounds(centerX + resizeHelper.scaled(200), resizeHelper.scaled(17),
                                       resizeHelper.scaled(65), resizeHelper.scaled(24));
        displayScaleSelector.setVisible(true);
    }
    else
    {
        displayScaleSelector.setVisible(false);
    }

    // EQ Curve Display - spans across the top area below header
    // Increased height from 105 to 135 to use available space
    int curveHeight = isCurveCollapsed ? 0 : resizeHelper.scaled(135);
    if (eqCurveDisplay && !isCurveCollapsed)
    {
        int curveX = resizeHelper.scaled(35);
        int curveY = resizeHelper.scaled(58);
        int curveWidth = getWidth() - resizeHelper.scaled(70);
        eqCurveDisplay->setBounds(curveX, curveY, curveWidth, curveHeight);
    }

    // LED Meters - scale meter width and position
    int meterWidth = resizeHelper.scaled(LEDMeterStyle::standardWidth);
    // Adjust meter Y position based on curve visibility (matches contentY offset)
    int meterY = isCurveCollapsed ? resizeHelper.scaled(80) : resizeHelper.scaled(215);
    int meterHeight = getHeight() - meterY - resizeHelper.scaled(LEDMeterStyle::valueHeight + LEDMeterStyle::labelSpacing + 10);

    if (inputMeterL)
        inputMeterL->setBounds(resizeHelper.scaled(6), meterY, meterWidth, meterHeight);

    // Output meter - position at right edge
    if (outputMeterL)
        outputMeterL->setBounds(getWidth() - meterWidth - resizeHelper.scaled(10), meterY, meterWidth, meterHeight);

    // Scaled layout constants
    // Adjusted contentY to account for taller graph (135 instead of 105)
    int contentY = isCurveCollapsed ? resizeHelper.scaled(65) : resizeHelper.scaled(200);
    int sectionLabelHeight = resizeHelper.scaled(30);
    int knobSize = resizeHelper.scaled(75);
    int knobRowHeight = resizeHelper.scaled(125);

    // Section X boundaries - scale proportionally to window width
    // Original proportions at 950px: filters=0-195, bands=132px each, master=rest
    float widthRatio = static_cast<float>(getWidth()) / 950.0f;

    int filtersEnd = static_cast<int>(195 * widthRatio);
    int bandWidth = static_cast<int>(132 * widthRatio);
    int lfStart = filtersEnd + 2;
    int lfEnd = lfStart + bandWidth;
    int lmfStart = lfEnd + 2;
    int lmfEnd = lmfStart + bandWidth;
    int hmfStart = lmfEnd + 2;
    int hmfEnd = hmfStart + bandWidth;
    int hfStart = hmfEnd + 2;
    int hfEnd = hfStart + bandWidth;
    int masterStart = hfEnd + 2;
    int masterEnd = getWidth() - resizeHelper.scaled(56);

    // Helper to center a knob within a section
    auto centerKnobInSection = [&](juce::Slider& slider, int sectionStart, int sectionEnd, int yPos) {
        int sectionCenter = (sectionStart + sectionEnd) / 2;
        slider.setBounds(sectionCenter - knobSize / 2, yPos, knobSize, knobSize);
    };

    // Helper to center a button within a section
    auto centerButtonInSection = [&](juce::Component& button, int sectionStart, int sectionEnd, int yPos, int width, int height) {
        int sectionCenter = (sectionStart + sectionEnd) / 2;
        button.setBounds(sectionCenter - width / 2, yPos, width, height);
    };

    // ===== FILTERS SECTION =====
    // Center knobs under the FILTERS label (which starts at scaled(30), not at 0)
    int filtersLabelStart = resizeHelper.scaled(30);
    int y = contentY + sectionLabelHeight + resizeHelper.scaled(25);

    // Center the knobs under FILTERS label (IN buttons positioned next to labels below)
    centerKnobInSection(hpfFreqSlider, filtersLabelStart, filtersEnd, y);
    y += knobRowHeight;

    centerKnobInSection(lpfFreqSlider, filtersLabelStart, filtersEnd, y);
    y += knobRowHeight;

    // Input Gain - center under full FILTERS label (no IN button)
    centerKnobInSection(inputGainSlider, filtersLabelStart, filtersEnd, y);

    // ===== LF BAND =====
    y = contentY + sectionLabelHeight + resizeHelper.scaled(25);
    centerKnobInSection(lfGainSlider, lfStart, lfEnd, y);
    y += knobRowHeight;
    centerKnobInSection(lfFreqSlider, lfStart, lfEnd, y);
    y += knobRowHeight;
    centerButtonInSection(lfBellButton, lfStart, lfEnd, y + resizeHelper.scaled(20),
                         resizeHelper.scaled(60), resizeHelper.scaled(25));

    // ===== LMF BAND =====
    y = contentY + sectionLabelHeight + resizeHelper.scaled(25);
    centerKnobInSection(lmGainSlider, lmfStart, lmfEnd, y);
    y += knobRowHeight;
    centerKnobInSection(lmFreqSlider, lmfStart, lmfEnd, y);
    y += knobRowHeight;
    centerKnobInSection(lmQSlider, lmfStart, lmfEnd, y);

    // ===== HMF BAND =====
    y = contentY + sectionLabelHeight + resizeHelper.scaled(25);
    centerKnobInSection(hmGainSlider, hmfStart, hmfEnd, y);
    y += knobRowHeight;
    centerKnobInSection(hmFreqSlider, hmfStart, hmfEnd, y);
    y += knobRowHeight;
    centerKnobInSection(hmQSlider, hmfStart, hmfEnd, y);

    // ===== HF BAND =====
    y = contentY + sectionLabelHeight + resizeHelper.scaled(25);
    centerKnobInSection(hfGainSlider, hfStart, hfEnd, y);
    y += knobRowHeight;
    centerKnobInSection(hfFreqSlider, hfStart, hfEnd, y);
    y += knobRowHeight;
    centerButtonInSection(hfBellButton, hfStart, hfEnd, y + resizeHelper.scaled(20),
                         resizeHelper.scaled(60), resizeHelper.scaled(25));

    // ===== MASTER SECTION =====
    y = contentY + sectionLabelHeight + resizeHelper.scaled(25);
    centerButtonInSection(bypassButton, masterStart, masterEnd, y,
                         resizeHelper.scaled(80), resizeHelper.scaled(30));
    y += resizeHelper.scaled(40);
    centerButtonInSection(autoGainButton, masterStart, masterEnd, y,
                         resizeHelper.scaled(80), resizeHelper.scaled(30));
    y += resizeHelper.scaled(70);
    centerKnobInSection(saturationSlider, masterStart, masterEnd, y);

    // Output gain knob - align with Input knob (third row)
    int inputKnobY = contentY + sectionLabelHeight + resizeHelper.scaled(25) + knobRowHeight * 2;
    centerKnobInSection(outputGainSlider, masterStart, masterEnd, inputKnobY);

    // Position parameter labels below each knob (SSL style)
    auto positionLabelBelow = [&](juce::Label& label, const juce::Slider& slider) {
        int labelWidth = resizeHelper.scaled(50);
        int labelHeight = resizeHelper.scaled(18);
        int yOffset = slider.getHeight() / 2 + resizeHelper.scaled(45);
        label.setBounds(slider.getX() + (slider.getWidth() - labelWidth) / 2,
                       slider.getY() + yOffset, labelWidth, labelHeight);
    };

    auto positionLabelCloser = [&](juce::Label& label, const juce::Slider& slider) {
        int labelWidth = resizeHelper.scaled(60);
        int labelHeight = resizeHelper.scaled(18);
        int yOffset = slider.getHeight() / 2 + resizeHelper.scaled(38);
        label.setBounds(slider.getX() + (slider.getWidth() - labelWidth) / 2,
                       slider.getY() + yOffset, labelWidth, labelHeight);
    };

    // Filter section - position labels with IN buttons next to them
    positionLabelBelow(hpfLabel, hpfFreqSlider);
    positionLabelBelow(lpfLabel, lpfFreqSlider);
    positionLabelBelow(inputLabel, inputGainSlider);

    // Position IN buttons next to HPF and LPF labels (SSL style: "HPF [IN]")
    {
        int btnWidth = resizeHelper.scaled(28);
        int btnHeight = resizeHelper.scaled(18);
        int btnGap = resizeHelper.scaled(2);

        // HPF IN button - to the right of HPF label
        hpfEnableButton.setBounds(hpfLabel.getRight() + btnGap,
                                  hpfLabel.getY(),
                                  btnWidth, btnHeight);

        // LPF IN button - to the right of LPF label
        lpfEnableButton.setBounds(lpfLabel.getRight() + btnGap,
                                  lpfLabel.getY(),
                                  btnWidth, btnHeight);
    }

    // LF band
    positionLabelBelow(lfGainLabel, lfGainSlider);
    positionLabelBelow(lfFreqLabel, lfFreqSlider);

    // LMF band
    positionLabelBelow(lmGainLabel, lmGainSlider);
    positionLabelBelow(lmFreqLabel, lmFreqSlider);
    positionLabelBelow(lmQLabel, lmQSlider);

    // HMF band
    positionLabelBelow(hmGainLabel, hmGainSlider);
    positionLabelBelow(hmFreqLabel, hmFreqSlider);
    positionLabelBelow(hmQLabel, hmQSlider);

    // HF band
    positionLabelBelow(hfGainLabel, hfGainSlider);
    positionLabelBelow(hfFreqLabel, hfFreqSlider);

    // Master section
    positionLabelCloser(outputLabel, outputGainSlider);
    positionLabelCloser(satLabel, saturationSlider);

    // Supporters overlay
    if (supportersOverlay)
        supportersOverlay->setBounds(getLocalBounds());
}

void FourKEQEditor::timerCallback()
{
    // Safety check - ensure parameter pointers are valid
    if (eqTypeParam == nullptr || bypassParam == nullptr)
        return;

    // Only update when parameters change to reduce CPU usage
    float currentEqType = eqTypeParam->load();
    float currentBypass = bypassParam->load();

    bool needsUpdate = (currentEqType != lastEqType) || (currentBypass != lastBypass);

    if (needsUpdate)
    {
        // Update UI based on EQ type
        bool isBlack = currentEqType > 0.5f;
        lfBellButton.setVisible(isBlack);
        hfBellButton.setVisible(isBlack);
        lmQSlider.setVisible(true);  // Always visible in both modes
        hmQSlider.setVisible(true);

        // Cache current values
        lastEqType = currentEqType;
        lastBypass = currentBypass;

        // Repaint to update EQ type indicator
        repaint();
    }

    // Note: Value readout labels removed - tick marks show parameter range

    // Update LED meters directly (they handle their own ballistics internally)
    // Get current levels from processor
    float inL = audioProcessor.inputLevelL.load(std::memory_order_relaxed);
    float inR = audioProcessor.inputLevelR.load(std::memory_order_relaxed);
    float outL = audioProcessor.outputLevelL.load(std::memory_order_relaxed);
    float outR = audioProcessor.outputLevelR.load(std::memory_order_relaxed);

    // Get channel count from DAW (set in prepareToPlay)
    // Mono tracks = 1 channel (single bar), Stereo tracks = 2 channels (dual bars)
    bool isStereo = audioProcessor.getNumChannels() > 1;
    if (inputMeterL)
    {
        inputMeterL->setStereoMode(isStereo);
        inputMeterL->setStereoLevels(inL, inR);
    }
    if (outputMeterL)
    {
        outputMeterL->setStereoMode(isStereo);
        outputMeterL->setStereoLevels(outL, outR);
    }

    // Use maximum of L/R for displayed value labels
    float inputLevel = juce::jmax(inL, inR);
    float outputLevel = juce::jmax(outL, outR);

    // Update displayed level values (throttled for readability - ~3x per second)
    levelDisplayCounter++;
    if (levelDisplayCounter >= 10)  // 30Hz / 10 = 3Hz
    {
        levelDisplayCounter = 0;
        displayedInputLevel = inputLevel;
        displayedOutputLevel = outputLevel;
        repaint();  // Repaint to update value labels
    }
}

//==============================================================================
void FourKEQEditor::setupKnob(juce::Slider& slider, const juce::String& paramID,
                              const juce::String& label, bool centerDetented)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    // No text box - keep clean knob design
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    // Enable popup for value display and double-click text entry
    slider.setPopupDisplayEnabled(true, true, this);

    // Professional rotation range
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f, true);

    // Enable mouse wheel control for fine adjustments
    slider.setScrollWheelEnabled(true);

    // Professional knob behavior from shared Luna settings
    LunaSliderStyle::configureKnob(slider);

    // Color code knobs like the reference image
    if (label.contains("GAIN")) {
        // Red for gain knobs
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffdc3545));
    } else if (label.contains("FREQ")) {
        // Green for frequency knobs
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff28a745));
    } else if (label.contains("Q")) {
        // Blue for Q knobs
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff007bff));
    } else if (label.contains("HPF") || label.contains("LPF")) {
        // Brown/orange for filters
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffb8860b));
    } else if (label.contains("INPUT") || label.contains("OUTPUT")) {
        // Blue for input/output gain
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff007bff));
    } else if (label.contains("SAT")) {
        // Orange for saturation
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffff8c00));
    }

    // Double-click to reset - center-detented knobs reset to 0.0 (center), others to default
    if (centerDetented) {
        slider.setDoubleClickReturnValue(true, 0.0);
    } else {
        // Get parameter default from processor for non-center knobs
        auto* param = audioProcessor.parameters.getParameter(paramID);
        if (param) {
            float defaultValue = param->getDefaultValue();
            slider.setDoubleClickReturnValue(true, defaultValue);
        }
    }

    addAndMakeVisible(slider);
}

void FourKEQEditor::setupButton(juce::ToggleButton& button, const juce::String& text)
{
    button.setButtonText(text);
    button.setClickingTogglesState(true);

    // Professional button colors
    button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff404040));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffff3030));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));

    addAndMakeVisible(button);
}

void FourKEQEditor::drawKnobMarkings(juce::Graphics& g)
{
    // SSL-style knob tick markings with value labels
    // Labels are positioned at the correct angular positions based on parameter skew

    // Rotation range constants (must match setupKnob rotaryParameters)
    const float startAngle = juce::MathConstants<float>::pi * 1.25f;  // 225° = 7 o'clock
    const float endAngle = juce::MathConstants<float>::pi * 2.75f;    // 495° = 5 o'clock
    const float totalRange = endAngle - startAngle;  // 270° total sweep

    // Helper to calculate normalized position for a value with skew
    // Formula: normalizedPos = ((value - min) / (max - min))^skew
    auto valueToNormalized = [](float value, float minVal, float maxVal, float skew) -> float {
        float proportion = (value - minVal) / (maxVal - minVal);
        return std::pow(proportion, skew);
    };

    // Helper to draw a single tick with label at the correct skewed position
    auto drawTickAtValue = [&](juce::Rectangle<int> knobBounds, float value,
                               float minVal, float maxVal, float skew,
                               const juce::String& label, bool isCenter = false)
    {
        auto center = knobBounds.getCentre().toFloat();
        float radius = knobBounds.getWidth() / 2.0f + 3.0f;

        // Calculate the normalized position (0-1) for this value with skew
        float normalizedPos = valueToNormalized(value, minVal, maxVal, skew);

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
            g.drawText(label, labelX - 18 + 1, labelY - 7 + 1, 36, 14, juce::Justification::centred);

            // Label
            g.setColour(juce::Colour(0xffd0d0d0));
            g.drawText(label, labelX - 18, labelY - 7, 36, 14, juce::Justification::centred);
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

    // Helper for skewed parameters - positions ticks at actual parameter values
    auto drawTicksSkewed = [&](juce::Rectangle<int> knobBounds,
                               const std::vector<std::pair<float, juce::String>>& ticks,
                               float minVal, float maxVal, float skew)
    {
        for (const auto& tick : ticks)
        {
            drawTickAtValue(knobBounds, tick.first, minVal, maxVal, skew, tick.second, false);
        }
    };

    // Helper for SSL-style evenly spaced ticks - draws labels at equal angular intervals
    // The labels show what frequency you GET at each evenly-spaced position
    auto drawTicksEvenlySpaced = [&](juce::Rectangle<int> knobBounds,
                                      const std::vector<juce::String>& labels)
    {
        auto center = knobBounds.getCentre().toFloat();
        float radius = knobBounds.getWidth() / 2.0f + 3.0f;
        int numTicks = static_cast<int>(labels.size());

        for (int i = 0; i < numTicks; ++i)
        {
            // Evenly space ticks from 0.0 to 1.0
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
                g.drawText(labels[static_cast<size_t>(i)], labelX - 18 + 1, labelY - 7 + 1, 36, 14, juce::Justification::centred);

                // Label
                g.setColour(juce::Colour(0xffd0d0d0));
                g.drawText(labels[static_cast<size_t>(i)], labelX - 18, labelY - 7, 36, 14, juce::Justification::centred);
            }
        }
    };

    // ===== GAIN KNOBS (linear, -20 to +20 dB) =====
    std::vector<std::pair<float, juce::String>> gainTicks = {
        {-20.0f, "-20"}, {0.0f, "0"}, {20.0f, "+20"}
    };

    drawTicksLinear(lfGainSlider.getBounds(), gainTicks, -20.0f, 20.0f, true);
    drawTicksLinear(lmGainSlider.getBounds(), gainTicks, -20.0f, 20.0f, true);
    drawTicksLinear(hmGainSlider.getBounds(), gainTicks, -20.0f, 20.0f, true);
    drawTicksLinear(hfGainSlider.getBounds(), gainTicks, -20.0f, 20.0f, true);

    // ===== HPF (20-500Hz) - SSL 4000 E style evenly spaced =====
    drawTicksEvenlySpaced(hpfFreqSlider.getBounds(), {"20", "70", "120", "200", "300", "500"});

    // ===== LPF (3000-20000Hz) - SSL style evenly spaced =====
    drawTicksEvenlySpaced(lpfFreqSlider.getBounds(), {"3k", "5k", "8k", "12k", "20k"});

    // ===== LF Frequency (30-480Hz) - SSL 4000 E style evenly spaced =====
    drawTicksEvenlySpaced(lfFreqSlider.getBounds(), {"30", "50", "100", "200", "300", "480"});

    // ===== LMF Frequency (200-2500Hz) - SSL 4000 E style evenly spaced =====
    drawTicksEvenlySpaced(lmFreqSlider.getBounds(), {".2", ".5", ".8", "1", "2", "2.5"});

    // ===== HMF Frequency (600-7000Hz) - SSL 4000 E style evenly spaced =====
    drawTicksEvenlySpaced(hmFreqSlider.getBounds(), {".6", "1.5", "3", "4.5", "6", "7"});

    // ===== HF Frequency (1500-16000Hz) - SSL 4000 E style evenly spaced =====
    drawTicksEvenlySpaced(hfFreqSlider.getBounds(), {"1.5", "8", "10", "14", "16"});

    // ===== Q knobs (0.4-4.0, linear) =====
    std::vector<std::pair<float, juce::String>> qTicks = {
        {0.4f, ".4"}, {1.0f, "1"}, {2.0f, "2"}, {3.0f, "3"}, {4.0f, "4"}
    };
    drawTicksLinear(lmQSlider.getBounds(), qTicks, 0.4f, 4.0f, false);
    drawTicksLinear(hmQSlider.getBounds(), qTicks, 0.4f, 4.0f, false);

    // ===== Input gain (-12 to +12 dB, linear) =====
    std::vector<std::pair<float, juce::String>> inputGainTicks = {
        {-12.0f, "-12"}, {0.0f, "0"}, {12.0f, "+12"}
    };
    drawTicksLinear(inputGainSlider.getBounds(), inputGainTicks, -12.0f, 12.0f, true);

    // ===== Output gain (-12 to +12 dB, linear) =====
    std::vector<std::pair<float, juce::String>> outputGainTicks = {
        {-12.0f, "-12"}, {0.0f, "0"}, {12.0f, "+12"}
    };
    drawTicksLinear(outputGainSlider.getBounds(), outputGainTicks, -12.0f, 12.0f, true);

    // ===== Saturation/Drive (0-100%, linear) =====
    std::vector<std::pair<float, juce::String>> satTicks = {
        {0.0f, "0"}, {20.0f, "20"}, {40.0f, "40"}, {60.0f, "60"}, {80.0f, "80"}, {100.0f, "100"}
    };
    drawTicksLinear(saturationSlider.getBounds(), satTicks, 0.0f, 100.0f, false);
}

void FourKEQEditor::setupValueLabel(juce::Label& label)
{
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(juce::FontOptions(10.0f)));
    label.setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
    label.setColour(juce::Label::backgroundColourId, juce::Colour(0x00000000));
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);
}

juce::String FourKEQEditor::formatValue(float value, const juce::String& suffix)
{
    if (suffix.containsIgnoreCase("Hz"))
    {
        if (value >= 1000.0f)
            return juce::String(value / 1000.0f, 1) + " kHz";
        else
            return juce::String(static_cast<int>(value)) + " Hz";
    }
    else if (suffix.containsIgnoreCase("dB"))
    {
        juce::String sign = (value >= 0) ? "+" : "";
        return sign + juce::String(value, 1) + " dB";
    }
    else if (suffix.containsIgnoreCase("%"))
    {
        return juce::String(static_cast<int>(value)) + "%";
    }
    else
    {
        return juce::String(value, 2);
    }
}

void FourKEQEditor::updateValueLabels()
{
    // Helper to position value label below a slider
    auto positionValueLabel = [](juce::Label& label, const juce::Slider& slider, int yOffset = 48) {
        int labelWidth = 60;
        int labelHeight = 14;
        label.setBounds(slider.getX() + (slider.getWidth() - labelWidth) / 2,
                        slider.getY() + slider.getHeight() / 2 + yOffset,
                        labelWidth, labelHeight);
    };

    // Update and position all value labels
    // Filter section
    hpfValueLabel.setText(formatValue(hpfFreqSlider.getValue(), "Hz"), juce::dontSendNotification);
    positionValueLabel(hpfValueLabel, hpfFreqSlider);

    lpfValueLabel.setText(formatValue(lpfFreqSlider.getValue(), "Hz"), juce::dontSendNotification);
    positionValueLabel(lpfValueLabel, lpfFreqSlider);

    inputValueLabel.setText(formatValue(inputGainSlider.getValue(), "dB"), juce::dontSendNotification);
    positionValueLabel(inputValueLabel, inputGainSlider);

    // LF band
    lfGainValueLabel.setText(formatValue(lfGainSlider.getValue(), "dB"), juce::dontSendNotification);
    positionValueLabel(lfGainValueLabel, lfGainSlider);

    lfFreqValueLabel.setText(formatValue(lfFreqSlider.getValue(), "Hz"), juce::dontSendNotification);
    positionValueLabel(lfFreqValueLabel, lfFreqSlider);

    // LMF band
    lmGainValueLabel.setText(formatValue(lmGainSlider.getValue(), "dB"), juce::dontSendNotification);
    positionValueLabel(lmGainValueLabel, lmGainSlider);

    lmFreqValueLabel.setText(formatValue(lmFreqSlider.getValue(), "Hz"), juce::dontSendNotification);
    positionValueLabel(lmFreqValueLabel, lmFreqSlider);

    lmQValueLabel.setText(juce::String(lmQSlider.getValue(), 2), juce::dontSendNotification);
    positionValueLabel(lmQValueLabel, lmQSlider);

    // HMF band
    hmGainValueLabel.setText(formatValue(hmGainSlider.getValue(), "dB"), juce::dontSendNotification);
    positionValueLabel(hmGainValueLabel, hmGainSlider);

    hmFreqValueLabel.setText(formatValue(hmFreqSlider.getValue(), "Hz"), juce::dontSendNotification);
    positionValueLabel(hmFreqValueLabel, hmFreqSlider);

    hmQValueLabel.setText(juce::String(hmQSlider.getValue(), 2), juce::dontSendNotification);
    positionValueLabel(hmQValueLabel, hmQSlider);

    // HF band
    hfGainValueLabel.setText(formatValue(hfGainSlider.getValue(), "dB"), juce::dontSendNotification);
    positionValueLabel(hfGainValueLabel, hfGainSlider);

    hfFreqValueLabel.setText(formatValue(hfFreqSlider.getValue(), "Hz"), juce::dontSendNotification);
    positionValueLabel(hfFreqValueLabel, hfFreqSlider);

    // Master section
    outputValueLabel.setText(formatValue(outputGainSlider.getValue(), "dB"), juce::dontSendNotification);
    positionValueLabel(outputValueLabel, outputGainSlider, 42);

    satValueLabel.setText(formatValue(saturationSlider.getValue(), "%"), juce::dontSendNotification);
    positionValueLabel(satValueLabel, saturationSlider, 42);
}

//==============================================================================
// A/B Comparison Functions
//==============================================================================
void FourKEQEditor::toggleAB()
{
    // Save current state to the active slot
    copyCurrentToState(isStateA ? stateA : stateB);

    // Switch to the other state
    isStateA = !isStateA;

    // Apply the new state
    applyState(isStateA ? stateA : stateB);

    // Update button appearance
    if (isStateA)
    {
        abButton.setButtonText("A");
        abButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a6a3a));  // Green for A
    }
    else
    {
        abButton.setButtonText("B");
        abButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6a3a3a));  // Red for B
    }
}

void FourKEQEditor::copyCurrentToState(juce::ValueTree& state)
{
    state = audioProcessor.parameters.copyState();
}

void FourKEQEditor::applyState(const juce::ValueTree& state)
{
    audioProcessor.parameters.replaceState(state);
}
#include "PluginEditor.h"

//==============================================================================
FourKEQEditor::FourKEQEditor(FourKEQ& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Set editor size - increased height for EQ curve display
    setSize(950, 640);
    setResizable(true, true);
    setResizeLimits(800, 580, 1400, 800);

    // Get parameter references
    eqTypeParam = audioProcessor.parameters.getRawParameterValue("eq_type");
    bypassParam = audioProcessor.parameters.getRawParameterValue("bypass");

    // HPF Section
    setupKnob(hpfFreqSlider, "hpf_freq", "HPF");
    hpfFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hpf_freq", hpfFreqSlider);

    // LPF Section
    setupKnob(lpfFreqSlider, "lpf_freq", "LPF");
    lpfFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lpf_freq", lpfFreqSlider);

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
    lpfFreqSlider.setTooltip("Low-Pass Filter Frequency (5kHz - 20kHz)");

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

    // Set initial bounds so meters are visible on first paint
    int initialMeterY = 185;  // Start lower to make room for EQ curve and labels
    int initialMeterHeight = 640 - initialMeterY - 20;  // Adjusted for new height
    inputMeterL->setBounds(10, initialMeterY, 16, initialMeterHeight);
    outputMeterL->setBounds(950 - 16 - 10, initialMeterY, 16, initialMeterHeight);  // 10px from right edge

    // Note: Value readout labels removed - the tick marks around knobs already
    // show the parameter range, and current values can be seen from knob position

    // Start timer for UI updates
    startTimerHz(30);
}

FourKEQEditor::~FourKEQEditor()
{
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

    // Plugin name
    g.setFont(juce::Font(juce::FontOptions(24.0f).withStyle("Bold")));
    g.setColour(juce::Colour(0xffe0e0e0));
    g.drawText("4K EQ", 60, 10, 200, 30, juce::Justification::left);

    // Subtitle
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(juce::Colour(0xff909090));
    g.drawText("Console-Style Equalizer", 60, 32, 200, 20, juce::Justification::left);

    // Patreon credits in header (right side)
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.setColour(juce::Colour(0xff606060));
    g.drawText("Made with support from Patreon backers",
               bounds.getRight() - 260, 38, 160, 15,
               juce::Justification::right);

    // EQ Type indicator badge - styled as muted amber/gold for Brown, dark grey for Black
    bool isBlack = eqTypeParam->load() > 0.5f;
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));

    // Position badge to the left of the dropdown (dropdown is at getWidth() - 110)
    auto eqTypeRect = juce::Rectangle<float>(static_cast<float>(getWidth()) - 190.0f, 15.0f, 70.0f, 24.0f);

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

    // EQ Curve display area background
    auto curveArea = juce::Rectangle<int>(40, 60, bounds.getWidth() - 80, 100);

    // Main content area - starts below EQ curve with gap
    bounds = getLocalBounds().withTrimmedTop(170);  // Account for header + curve display + gap

    // Section dividers - vertical lines
    g.setColour(juce::Colour(0xff3a3a3a));

    // Filters section divider
    int filterWidth = 195;
    g.fillRect(filterWidth, bounds.getY(), 2, bounds.getHeight());

    // EQ bands dividers
    int bandWidth = 132;
    int xPos = filterWidth + 2;

    // After LF
    xPos += bandWidth;
    g.fillRect(xPos, bounds.getY(), 2, bounds.getHeight());

    // After LMF
    xPos += bandWidth + 2;
    g.fillRect(xPos, bounds.getY(), 2, bounds.getHeight());

    // After HMF
    xPos += bandWidth + 2;
    g.fillRect(xPos, bounds.getY(), 2, bounds.getHeight());

    // After HF
    xPos += bandWidth + 2;
    g.fillRect(xPos, bounds.getY(), 2, bounds.getHeight());

    // Section headers - larger with subtle background for visibility
    g.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));

    int labelY = bounds.getY() + 6;
    int labelHeight = 22;

    // Draw subtle background strips for section headers
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(0, labelY - 2, filterWidth, labelHeight);

    // Draw section header text
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("FILTERS", 0, labelY, 195, 20,
               juce::Justification::centred);

    xPos = 197;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, bandWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("LF", xPos, labelY, 132, 20,
               juce::Justification::centred);

    xPos += 134;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, bandWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("LMF", xPos, labelY, 132, 20,
               juce::Justification::centred);

    xPos += 134;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, bandWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("HMF", xPos, labelY, 132, 20,
               juce::Justification::centred);

    xPos += 134;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, bandWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("HF", xPos, labelY, 132, 20,
               juce::Justification::centred);

    xPos += 134;
    // MASTER label width: calculate from xPos to right edge, minus output meter space (16px + 10px margin + 30px padding = 56px)
    int masterWidth = bounds.getRight() - xPos - 56;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(xPos, labelY - 2, masterWidth, labelHeight);
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawText("MASTER", xPos, labelY, masterWidth, 20,
               juce::Justification::centred);

    // Frequency range indicators
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.setColour(juce::Colour(0xff808080));

    // Draw knob scale markings around each knob
    drawKnobMarkings(g);

    // Draw meter labels centered over meters, below header bar
    g.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
    g.setColour(juce::Colour(0xffe0e0e0));

    if (inputMeterL)
    {
        auto inputBounds = inputMeterL->getBounds();
        // Center label over meter, but keep within left edge (start at x=0)
        int labelX = std::max(0, inputBounds.getCentreX() - 20);  // 40px width / 2 = 20
        g.drawText("INPUT", labelX, inputBounds.getY() - 16,
                   40, 15, juce::Justification::centred);
    }

    if (outputMeterL)
    {
        auto outputBounds = outputMeterL->getBounds();
        // Center label over meter, ensuring it doesn't extend past right edge
        int labelWidth = 45;
        int labelX = std::min(getWidth() - labelWidth, outputBounds.getCentreX() - labelWidth / 2);
        g.drawText("OUTPUT", labelX, outputBounds.getY() - 16,
                   labelWidth, 15, juce::Justification::centred);
    }
}

void FourKEQEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header controls - preset and oversampling selectors
    auto headerBounds = bounds.removeFromTop(60);
    int centerX = headerBounds.getCentreX();

    // Preset selector (left of center)
    presetSelector.setBounds(centerX - 210, 15, 200, 28);

    // Oversampling selector (right of center) - wider for "Oversample:" text
    oversamplingSelector.setBounds(centerX + 10, 15, 130, 28);

    // EQ Type selector in header (upper right)
    eqTypeSelector.setBounds(getWidth() - 110, 15, 95, 28);

    // EQ Curve Display - spans across the top area below header
    if (eqCurveDisplay)
    {
        int curveX = 35;
        int curveY = 58;
        int curveWidth = getWidth() - 70;
        int curveHeight = 105;  // Taller to better fill the space
        eqCurveDisplay->setBounds(curveX, curveY, curveWidth, curveHeight);
    }

    // LED Meters - start below the curve display
    int meterWidth = 16;
    int meterY = 185;  // Start lower to make room for curve and labels
    int meterHeight = getHeight() - meterY - 20;

    if (inputMeterL)
        inputMeterL->setBounds(10, meterY, meterWidth, meterHeight);

    if (outputMeterL)
        outputMeterL->setBounds(getWidth() - meterWidth - 10, meterY, meterWidth, meterHeight);

    // Use absolute positioning based on section divider positions from paint()
    // This ensures knobs are perfectly centered between dividers

    int contentY = 170;  // Top of content area (below header + curve) - increased for spacing
    int sectionLabelHeight = 30;  // Space for section headers like "FILTERS", "LF", etc.
    int knobSize = 75;  // Slightly smaller knobs to prevent label overlap
    int knobRowHeight = 125;  // Vertical space for each knob row (knob + labels)

    // Section X boundaries (matching paint() divider positions exactly)
    int filtersStart = 0;
    int filtersEnd = 195;  // First divider
    int lfStart = filtersEnd + 2;
    int lfEnd = lfStart + 132;  // Second divider
    int lmfStart = lfEnd + 2;
    int lmfEnd = lmfStart + 132;  // Third divider
    int hmfStart = lmfEnd + 2;
    int hmfEnd = hmfStart + 132;  // Fourth divider
    int hfStart = hmfEnd + 2;
    int hfEnd = hfStart + 132;  // Fifth divider
    int masterStart = hfEnd + 2;
    int masterEnd = getWidth() - 56;  // Leave space for output meter

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
    int y = contentY + sectionLabelHeight + 25;  // Extra space to avoid overlap with section headers

    // HPF
    centerKnobInSection(hpfFreqSlider, filtersStart, filtersEnd, y);
    y += knobRowHeight;

    // LPF
    centerKnobInSection(lpfFreqSlider, filtersStart, filtersEnd, y);
    y += knobRowHeight;

    // Input Gain
    centerKnobInSection(inputGainSlider, filtersStart, filtersEnd, y);

    // ===== LF BAND =====
    y = contentY + sectionLabelHeight + 25;

    // LF Gain
    centerKnobInSection(lfGainSlider, lfStart, lfEnd, y);
    y += knobRowHeight;

    // LF Freq
    centerKnobInSection(lfFreqSlider, lfStart, lfEnd, y);
    y += knobRowHeight;

    // LF Bell button
    centerButtonInSection(lfBellButton, lfStart, lfEnd, y + 20, 60, 25);

    // ===== LMF BAND =====
    y = contentY + sectionLabelHeight + 25;

    // LMF Gain
    centerKnobInSection(lmGainSlider, lmfStart, lmfEnd, y);
    y += knobRowHeight;

    // LMF Freq
    centerKnobInSection(lmFreqSlider, lmfStart, lmfEnd, y);
    y += knobRowHeight;

    // LMF Q
    centerKnobInSection(lmQSlider, lmfStart, lmfEnd, y);

    // ===== HMF BAND =====
    y = contentY + sectionLabelHeight + 25;

    // HMF Gain
    centerKnobInSection(hmGainSlider, hmfStart, hmfEnd, y);
    y += knobRowHeight;

    // HMF Freq
    centerKnobInSection(hmFreqSlider, hmfStart, hmfEnd, y);
    y += knobRowHeight;

    // HMF Q
    centerKnobInSection(hmQSlider, hmfStart, hmfEnd, y);

    // ===== HF BAND =====
    y = contentY + sectionLabelHeight + 25;

    // HF Gain
    centerKnobInSection(hfGainSlider, hfStart, hfEnd, y);
    y += knobRowHeight;

    // HF Freq
    centerKnobInSection(hfFreqSlider, hfStart, hfEnd, y);
    y += knobRowHeight;

    // HF Bell button
    centerButtonInSection(hfBellButton, hfStart, hfEnd, y + 20, 60, 25);

    // ===== MASTER SECTION =====
    // EQ Type selector moved to header - start with buttons
    y = contentY + sectionLabelHeight + 25;

    // Bypass button
    centerButtonInSection(bypassButton, masterStart, masterEnd, y, 80, 30);
    y += 40;

    // Auto-gain button
    centerButtonInSection(autoGainButton, masterStart, masterEnd, y, 80, 30);
    y += 70;  // Increased gap to prevent overlap with drive knob labels

    // Drive/Saturation knob
    centerKnobInSection(saturationSlider, masterStart, masterEnd, y);

    // Output gain knob - align with Input knob (third row in filters section)
    int inputKnobY = contentY + sectionLabelHeight + 25 + knobRowHeight * 2;  // Same Y as input knob
    centerKnobInSection(outputGainSlider, masterStart, masterEnd, inputKnobY);

    // Position section labels
    // FILTERS label removed - section header at top is sufficient
    // filtersLabel.setBounds(10, filtersMidY - 10, 60, 20);

    // Band labels (LF, LMF, HMF, HF) removed - section headers are sufficient

    // Position parameter labels below each knob (SSL style)
    // Helper to position a label centered below a knob
    auto positionLabelBelow = [](juce::Label& label, const juce::Slider& slider) {
        int labelWidth = 50;
        int labelHeight = 18;
        // Position closer to the knob, just below tick marks (about 45 pixels from center)
        int yOffset = slider.getHeight() / 2 + 45;
        label.setBounds(slider.getX() + (slider.getWidth() - labelWidth) / 2,
                       slider.getY() + yOffset,
                       labelWidth, labelHeight);
    };

    // Helper for master section labels (OUTPUT/DRIVE) - positioned even closer
    auto positionLabelCloser = [](juce::Label& label, const juce::Slider& slider) {
        int labelWidth = 60;  // Slightly wider for OUTPUT
        int labelHeight = 18;
        // Position very close to the knob (about 38 pixels from center)
        int yOffset = slider.getHeight() / 2 + 38;
        label.setBounds(slider.getX() + (slider.getWidth() - labelWidth) / 2,
                       slider.getY() + yOffset,
                       labelWidth, labelHeight);
    };

    // Position all functional labels below knobs

    // Filter section
    positionLabelBelow(hpfLabel, hpfFreqSlider);
    positionLabelBelow(lpfLabel, lpfFreqSlider);
    positionLabelBelow(inputLabel, inputGainSlider);

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

    // Master section - positioned closer to knobs
    positionLabelCloser(outputLabel, outputGainSlider);
    positionLabelCloser(satLabel, saturationSlider);
}

void FourKEQEditor::timerCallback()
{
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
    // Get current levels from processor (L+R averaged for mono meter)
    float inL = audioProcessor.inputLevelL.load(std::memory_order_relaxed);
    float inR = audioProcessor.inputLevelR.load(std::memory_order_relaxed);
    float outL = audioProcessor.outputLevelL.load(std::memory_order_relaxed);
    float outR = audioProcessor.outputLevelR.load(std::memory_order_relaxed);

    // Average L/R channels for single meter display
    float inputAvg = (inL + inR) * 0.5f;
    float outputAvg = (outL + outR) * 0.5f;

    if (inputMeterL)
        inputMeterL->setLevel(inputAvg);

    if (outputMeterL)
        outputMeterL->setLevel(outputAvg);
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
    // The real SSL console shows specific dB/Hz/Q values around the knobs

    // Helper to draw tick marks with optional value labels (SSL style)
    auto drawTicksWithValues = [&g](juce::Rectangle<int> knobBounds,
                                     const std::vector<juce::String>& values,
                                     bool showCenterDot = false)
    {
        if (values.empty()) return;

        auto center = knobBounds.getCentre().toFloat();
        float radius = knobBounds.getWidth() / 2.0f + 3.0f;

        // Rotation range: 1.25π to 2.75π (270° sweep from 7 o'clock to 5 o'clock)
        // IMPORTANT: These angles match the rotaryParameters set in setupKnob()
        float startAngle = juce::MathConstants<float>::pi * 1.25f;  // 225° = 7 o'clock
        float endAngle = juce::MathConstants<float>::pi * 2.75f;    // 495° = 5 o'clock
        float totalRange = endAngle - startAngle;  // 270° total sweep

        int numTicks = values.size();

        for (int i = 0; i < numTicks; ++i)
        {
            // Calculate normalized position (0.0 to 1.0) for this tick
            float normalizedPos = static_cast<float>(i) / (numTicks - 1);

            // Calculate the angle for this tick position
            // This matches how JUCE's rotary slider calculates its rotation angle
            float angle = startAngle + totalRange * normalizedPos;

            // IMPORTANT: The knob pointer is drawn with an implicit -90° offset (pointing up at angle=0)
            // So we need to subtract π/2 from our tick mark angles to match the pointer's coordinate system
            float tickAngle = angle - juce::MathConstants<float>::halfPi;

            // Longer tick for center position (for center-detented knobs)
            bool isCenterTick = showCenterDot && (i == numTicks / 2);
            float tickLength = isCenterTick ? 5.0f : 3.0f;

            // Draw tick mark using adjusted angle that matches the knob pointer's coordinate system
            g.setColour(isCenterTick ? juce::Colour(0xff909090) : juce::Colour(0xff606060));
            float x1 = center.x + std::cos(tickAngle) * radius;
            float y1 = center.y + std::sin(tickAngle) * radius;
            float x2 = center.x + std::cos(tickAngle) * (radius + tickLength);
            float y2 = center.y + std::sin(tickAngle) * (radius + tickLength);
            g.drawLine(x1, y1, x2, y2, isCenterTick ? 1.5f : 1.0f);

            // Draw value label at all positions with non-empty values
            if (!values[i].isEmpty())
            {
                g.setFont(juce::Font(juce::FontOptions(9.5f).withStyle("Bold")));  // Larger, bold for readability

                float labelRadius = radius + tickLength + 10.0f;  // More space from knob
                // Use the same adjusted angle for label positioning
                float labelX = center.x + std::cos(tickAngle) * labelRadius;
                float labelY = center.y + std::sin(tickAngle) * labelRadius;

                // Draw label with dark shadow for strong contrast
                g.setColour(juce::Colour(0xff000000));
                g.drawText(values[i], labelX - 18 + 1, labelY - 7 + 1, 36, 14, juce::Justification::centred);

                // Draw bright label on top (much brighter than before)
                g.setColour(juce::Colour(0xffd0d0d0));  // Brighter grey, almost white
                g.drawText(values[i], labelX - 18, labelY - 7, 36, 14, juce::Justification::centred);
            }
        }
    };

    // ACTUAL PARAMETER RANGES (must match FourKEQ.cpp parameter definitions)
    // Gain knobs: -20 to +20 dB (center at 0 dB)
    std::vector<juce::String> gainValues = {"-20", "", "", "", "", "", "", "0", "", "", "", "", "", "", "+20"};

    // LF Frequency: 30, 50, 100, 200, 300, 400, 480 Hz
    std::vector<juce::String> lfFreqValues = {"30", "50", "100", "200", "300", "400", "480"};

    // LMF Frequency: 200, 300, 800, 1k, 1.5k, 2k, 2.5k Hz
    std::vector<juce::String> lmfFreqValues = {"200", "300", "800", "1k", "1.5k", "2k", "2.5k"};

    // HMF Frequency: 600, 800, 1.5k, 3k, 4.5k, 6k, 7k Hz
    std::vector<juce::String> hmfFreqValues = {"600", "800", "1.5k", "3k", "4.5k", "6k", "7k"};

    // HF Frequency: 1.5k, 2k, 5k, 8k, 10k, 14k, 16k Hz
    std::vector<juce::String> hfFreqValues = {"1.5k", "2k", "5k", "8k", "10k", "14k", "16k"};

    // Q values: 0.4-4.0 (SSL hardware realistic range)
    std::vector<juce::String> lmQValues = {"4", "3", "2", "1.5", "1", ".5", ".4"};
    std::vector<juce::String> hmQValues = {"4", "3", "2", "1.5", "1", ".5", ".4"};

    // HPF: 16, 20, 70, 120, 200, 300, 350 Hz
    std::vector<juce::String> hpfValues = {"16", "20", "70", "120", "200", "300", "350"};

    // LPF: 22k, 12k, 8k, 5k, 4k, 3.5k, 3k Hz
    std::vector<juce::String> lpfValues = {"22k", "12k", "8k", "5k", "4k", "3.5k", "3k"};

    // Draw tick marks with values for each knob
    drawTicksWithValues(hpfFreqSlider.getBounds(), hpfValues, false);
    drawTicksWithValues(lpfFreqSlider.getBounds(), lpfValues, false);

    drawTicksWithValues(lfGainSlider.getBounds(), gainValues, true);
    drawTicksWithValues(lfFreqSlider.getBounds(), lfFreqValues, false);

    drawTicksWithValues(lmGainSlider.getBounds(), gainValues, true);
    drawTicksWithValues(lmFreqSlider.getBounds(), lmfFreqValues, false);
    drawTicksWithValues(lmQSlider.getBounds(), lmQValues, false);

    drawTicksWithValues(hmGainSlider.getBounds(), gainValues, true);
    drawTicksWithValues(hmFreqSlider.getBounds(), hmfFreqValues, false);
    drawTicksWithValues(hmQSlider.getBounds(), hmQValues, false);

    drawTicksWithValues(hfGainSlider.getBounds(), gainValues, true);
    drawTicksWithValues(hfFreqSlider.getBounds(), hfFreqValues, false);

    // Master section - Output gain: -12 to +12 dB (actual range from line 207)
    std::vector<juce::String> outputGainValues = {"-12", "", "", "", "", "", "", "0", "", "", "", "", "", "", "+12"};
    drawTicksWithValues(outputGainSlider.getBounds(), outputGainValues, true);

    // Saturation (DRIVE): 0-100% in increments of 10
    std::vector<juce::String> satValues = {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"};
    drawTicksWithValues(saturationSlider.getBounds(), satValues, false);
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
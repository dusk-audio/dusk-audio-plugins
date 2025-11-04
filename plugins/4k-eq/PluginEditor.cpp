#include "PluginEditor.h"

//==============================================================================
FourKEQEditor::FourKEQEditor(FourKEQ& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Set editor size - increased height for tick marks and value readouts
    setSize(900, 540);
    setResizable(true, true);
    setResizeLimits(750, 480, 1400, 700);

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

    // Draw header
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(0, 0, bounds.getWidth(), 55);

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
    g.setColour(juce::Colour(0xff707070));
    g.drawText("Made with support from Patreon backers 💖",
               bounds.getRight() - 320, 38, 200, 15,
               juce::Justification::right);

    // EQ Type indicator
    bool isBlack = eqTypeParam->load() > 0.5f;
    g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    g.setColour(isBlack ? juce::Colour(0xff404040) : juce::Colour(0xff6B4423));
    g.fillRoundedRectangle(bounds.getRight() - 120, 12, 80, 26, 3);
    g.setColour(juce::Colour(0xffe0e0e0));
    g.drawText(isBlack ? "BLACK" : "BROWN",
               bounds.getRight() - 120, 12, 80, 26,
               juce::Justification::centred);

    // Main content area
    bounds = getLocalBounds().withTrimmedTop(60);

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

    // Section headers
    g.setColour(juce::Colour(0xffc0c0c0));  // Brighter color for better visibility
    g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));

    int labelY = bounds.getY() + 12;
    g.drawText("FILTERS", 0, labelY, 195, 20,
               juce::Justification::centred);

    xPos = 197;
    g.drawText("LF", xPos, labelY, 132, 20,
               juce::Justification::centred);

    xPos += 134;
    g.drawText("LMF", xPos, labelY, 132, 20,
               juce::Justification::centred);

    xPos += 134;
    g.drawText("HMF", xPos, labelY, 132, 20,
               juce::Justification::centred);

    xPos += 134;
    g.drawText("HF", xPos, labelY, 132, 20,
               juce::Justification::centred);

    xPos += 134;
    g.drawText("MASTER", xPos, labelY, bounds.getRight() - xPos, 20,
               juce::Justification::centred);

    // Frequency range indicators
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.setColour(juce::Colour(0xff808080));

    // Draw knob scale markings around each knob
    drawKnobMarkings(g);

    // Draw level meters like Universal Compressor:
    // Input meters on the LEFT side, Output meters on the RIGHT side
    int meterWidth = 10;
    int meterHeight = 38;
    int meterY = 11;
    int meterSpacing = 4;  // Small gap between L/R channels

    // INPUT METERS - Left side (after plugin title)
    // Start at x=15 (left edge padding)
    int inputMeterX = 15;
    drawLevelMeter(g, juce::Rectangle<int>(inputMeterX, meterY, meterWidth, meterHeight),
                  smoothedInputL, "IL");
    drawLevelMeter(g, juce::Rectangle<int>(inputMeterX + meterWidth + meterSpacing, meterY, meterWidth, meterHeight),
                  smoothedInputR, "IR");

    // OUTPUT METERS - Right side (before BROWN/BLACK indicator)
    // BROWN/BLACK indicator is at bounds.getRight() - 120
    // Place output meters at bounds.getRight() - 120 - (2 meters + gap) - 10px padding
    int outputMeterX = bounds.getRight() - 120 - (meterWidth * 2 + meterSpacing) - 10;
    drawLevelMeter(g, juce::Rectangle<int>(outputMeterX, meterY, meterWidth, meterHeight),
                  smoothedOutputL, "OL");
    drawLevelMeter(g, juce::Rectangle<int>(outputMeterX + meterWidth + meterSpacing, meterY, meterWidth, meterHeight),
                  smoothedOutputR, "OR");
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

    bounds.reduce(15, 10);

    // Filters section (left)
    auto filterSection = bounds.removeFromLeft(180);
    filterSection.removeFromTop(35);  // Section label space

    // HPF
    auto hpfBounds = filterSection.removeFromTop(160);  // Space for knob + labels
    hpfBounds.removeFromTop(5);  // Small gap from section label
    hpfFreqSlider.setBounds(hpfBounds.withSizeKeepingCentre(80, 80));

    // LPF
    auto lpfBounds = filterSection.removeFromTop(160);  // Space for knob + labels
    lpfBounds.removeFromTop(5);  // Small gap
    lpfFreqSlider.setBounds(lpfBounds.withSizeKeepingCentre(80, 80));

    bounds.removeFromLeft(15);  // Gap

    // LF Band
    auto lfSection = bounds.removeFromLeft(132);  // Match section header width
    lfSection.removeFromTop(35);  // Section label space

    auto lfGainBounds = lfSection.removeFromTop(130);  // Space for knob + labels
    lfGainBounds.removeFromTop(5);  // Small gap
    lfGainSlider.setBounds(lfGainBounds.withSizeKeepingCentre(80, 80));

    auto lfFreqBounds = lfSection.removeFromTop(130);  // Space for knob + labels
    lfFreqBounds.removeFromTop(30);  // More space below GAIN label
    lfFreqSlider.setBounds(lfFreqBounds.withSizeKeepingCentre(80, 80));

    lfSection.removeFromTop(10);  // Extra space below FREQ label
    lfBellButton.setBounds(lfSection.removeFromTop(35).withSizeKeepingCentre(60, 25));

    bounds.removeFromLeft(2);  // Just the divider width

    // LMF Band
    auto lmSection = bounds.removeFromLeft(132);  // Match section header width
    lmSection.removeFromTop(35);  // Section label space

    auto lmGainBounds = lmSection.removeFromTop(130);  // Space for knob + labels
    lmGainBounds.removeFromTop(5);  // Space for label - aligned with LF
    lmGainSlider.setBounds(lmGainBounds.withSizeKeepingCentre(80, 80));

    auto lmFreqBounds = lmSection.removeFromTop(130);  // Space for knob + labels
    lmFreqBounds.removeFromTop(30);  // More space below GAIN label
    lmFreqSlider.setBounds(lmFreqBounds.withSizeKeepingCentre(80, 80));

    auto lmQBounds = lmSection.removeFromTop(130);  // Space for knob + labels
    lmQBounds.removeFromTop(30);  // More space below FREQ label
    lmQSlider.setBounds(lmQBounds.withSizeKeepingCentre(80, 80));

    bounds.removeFromLeft(2);  // Just the divider width

    // HMF Band
    auto hmSection = bounds.removeFromLeft(132);  // Match section header width
    hmSection.removeFromTop(35);  // Section label space

    auto hmGainBounds = hmSection.removeFromTop(130);  // Space for knob + labels
    hmGainBounds.removeFromTop(5);  // Space for label - aligned with LF
    hmGainSlider.setBounds(hmGainBounds.withSizeKeepingCentre(80, 80));

    auto hmFreqBounds = hmSection.removeFromTop(130);  // Space for knob + labels
    hmFreqBounds.removeFromTop(30);  // More space below GAIN label
    hmFreqSlider.setBounds(hmFreqBounds.withSizeKeepingCentre(80, 80));

    auto hmQBounds = hmSection.removeFromTop(130);  // Space for knob + labels
    hmQBounds.removeFromTop(30);  // More space below FREQ label
    hmQSlider.setBounds(hmQBounds.withSizeKeepingCentre(80, 80));

    bounds.removeFromLeft(2);  // Just the divider width

    // HF Band
    auto hfSection = bounds.removeFromLeft(132);  // Match section header width
    hfSection.removeFromTop(35);  // Section label space

    auto hfGainBounds = hfSection.removeFromTop(130);  // Space for knob + labels
    hfGainBounds.removeFromTop(5);  // Space for label - aligned with LF
    hfGainSlider.setBounds(hfGainBounds.withSizeKeepingCentre(80, 80));

    auto hfFreqBounds = hfSection.removeFromTop(130);  // Space for knob + labels
    hfFreqBounds.removeFromTop(30);  // More space below GAIN label
    hfFreqSlider.setBounds(hfFreqBounds.withSizeKeepingCentre(80, 80));

    hfSection.removeFromTop(10);  // Extra space below FREQ label
    hfBellButton.setBounds(hfSection.removeFromTop(35).withSizeKeepingCentre(60, 25));

    bounds.removeFromLeft(12);

    // Master section
    auto masterSection = bounds;
    masterSection.removeFromTop(35);  // Section label space

    // EQ Type selector
    eqTypeSelector.setBounds(masterSection.removeFromTop(32).withSizeKeepingCentre(100, 28));

    masterSection.removeFromTop(15);  // Gap

    // Bypass button
    bypassButton.setBounds(masterSection.removeFromTop(35).withSizeKeepingCentre(80, 30));

    masterSection.removeFromTop(5);  // Small gap

    // Auto-gain button
    autoGainButton.setBounds(masterSection.removeFromTop(35).withSizeKeepingCentre(80, 30));

    masterSection.removeFromTop(5);  // Small gap

    // Output gain
    auto outputBounds = masterSection.removeFromTop(130);  // Space for knob + labels
    outputBounds.removeFromTop(20);  // Space for label
    outputGainSlider.setBounds(outputBounds.withSizeKeepingCentre(80, 80));

    // Saturation
    auto satBounds = masterSection.removeFromTop(130);  // Space for knob + labels
    satBounds.removeFromTop(20);  // Space for label
    saturationSlider.setBounds(satBounds.withSizeKeepingCentre(80, 80));

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
    }

    // Update level meters with ballistics (smooth attack/decay)
    const float attack = 0.7f;   // Fast rise
    const float decay = 0.95f;   // Slower fall

    // Get current levels from processor
    float inL = audioProcessor.inputLevelL.load(std::memory_order_relaxed);
    float inR = audioProcessor.inputLevelR.load(std::memory_order_relaxed);
    float outL = audioProcessor.outputLevelL.load(std::memory_order_relaxed);
    float outR = audioProcessor.outputLevelR.load(std::memory_order_relaxed);

    // Apply ballistics (attack/release)
    smoothedInputL = (inL > smoothedInputL) ?
        smoothedInputL * attack + inL * (1.0f - attack) :
        smoothedInputL * decay + inL * (1.0f - decay);

    smoothedInputR = (inR > smoothedInputR) ?
        smoothedInputR * attack + inR * (1.0f - attack) :
        smoothedInputR * decay + inR * (1.0f - decay);

    smoothedOutputL = (outL > smoothedOutputL) ?
        smoothedOutputL * attack + outL * (1.0f - attack) :
        smoothedOutputL * decay + outL * (1.0f - decay);

    smoothedOutputR = (outR > smoothedOutputR) ?
        smoothedOutputR * attack + outR * (1.0f - attack) :
        smoothedOutputR * decay + outR * (1.0f - decay);

    // Always repaint for meter updates
    repaint();
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
    } else if (label.contains("OUTPUT")) {
        // Blue for output
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

void FourKEQEditor::drawLevelMeter(juce::Graphics& g, juce::Rectangle<int> bounds,
                                   float levelDB, const juce::String& label)
{
    // Draw meter background
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(bounds);

    // Draw meter border
    g.setColour(juce::Colour(0xff505050));
    g.drawRect(bounds, 1);

    // Calculate meter fill height (-60dB to 0dB range)
    float normalizedLevel = juce::jmap(levelDB, -60.0f, 0.0f, 0.0f, 1.0f);
    normalizedLevel = juce::jlimit(0.0f, 1.0f, normalizedLevel);

    int fillHeight = static_cast<int>(normalizedLevel * bounds.getHeight());

    if (fillHeight > 0)
    {
        // Draw meter fill with gradient (green -> yellow -> red)
        juce::Rectangle<int> fillArea = bounds.withTop(bounds.getBottom() - fillHeight);

        // Color based on level
        juce::Colour meterColour;
        if (levelDB < -18.0f)
            meterColour = juce::Colour(0xff00ff00);  // Green
        else if (levelDB < -6.0f)
            meterColour = juce::Colour(0xffffff00);  // Yellow
        else if (levelDB < -3.0f)
            meterColour = juce::Colour(0xffff8800);  // Orange
        else
            meterColour = juce::Colour(0xffff0000);  // Red

        g.setColour(meterColour);
        g.fillRect(fillArea);
    }

    // Draw dB scale marks (-60, -30, -12, -6, -3, 0)
    g.setColour(juce::Colour(0xff606060));
    auto drawScaleMark = [&](float dbValue)
    {
        float normalized = juce::jmap(dbValue, -60.0f, 0.0f, 0.0f, 1.0f);
        int y = bounds.getBottom() - static_cast<int>(normalized * bounds.getHeight());
        g.drawHorizontalLine(y, bounds.getX(), bounds.getRight());
    };

    drawScaleMark(-60.0f);
    drawScaleMark(-30.0f);
    drawScaleMark(-12.0f);
    drawScaleMark(-6.0f);
    drawScaleMark(-3.0f);
    drawScaleMark(0.0f);

    // Draw label below meter
    g.setColour(juce::Colour(0xffa0a0a0));
    g.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
    g.drawText(label, bounds.getX(), bounds.getBottom() + 2, bounds.getWidth(), 12,
               juce::Justification::centred);
}
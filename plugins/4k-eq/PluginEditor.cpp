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
    setupButton(bypassButton, "IN");
    bypassButton.setClickingTogglesState(true);
    bypassAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "bypass", bypassButton);

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

    // Setup section labels (positioned to left of knob groups)
    auto setupSectionLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffb0b0b0));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
    };

    setupSectionLabel(filtersLabel, "FILTERS");
    setupSectionLabel(lfLabel, "LF");
    setupSectionLabel(lmfLabel, "LMF");
    setupSectionLabel(hmfLabel, "HMF");
    setupSectionLabel(hfLabel, "HF");

    // Setup parameter labels (small text below each knob like SSL)
    auto setupParamLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffa0a0a0));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
    };

    // Filter labels (SSL shows Hz ranges)
    setupParamLabel(hpfLabel, "20-500Hz");
    setupParamLabel(lpfLabel, "5k-20k");

    // LF band labels (SSL shows parameter name and range)
    setupParamLabel(lfGainLabel, "GAIN");
    setupParamLabel(lfFreqLabel, "30-450Hz");

    // LMF band labels
    setupParamLabel(lmGainLabel, "GAIN");
    setupParamLabel(lmFreqLabel, "200Hz-2.5k");
    setupParamLabel(lmQLabel, "Q");

    // HMF band labels
    setupParamLabel(hmGainLabel, "GAIN");
    setupParamLabel(hmFreqLabel, "600Hz-7k");
    setupParamLabel(hmQLabel, "Q");

    // HF band labels
    setupParamLabel(hfGainLabel, "GAIN");
    setupParamLabel(hfFreqLabel, "3k-20kHz");

    // Master section labels
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
    hpfFreqSlider.setBounds(hpfBounds.withSizeKeepingCentre(100, 100));  // Room for tick marks & value

    // LPF
    auto lpfBounds = filterSection.removeFromTop(160);  // Space for knob + labels
    lpfBounds.removeFromTop(5);  // Small gap
    lpfFreqSlider.setBounds(lpfBounds.withSizeKeepingCentre(100, 100));  // Room for tick marks & value

    bounds.removeFromLeft(15);  // Gap

    // LF Band
    auto lfSection = bounds.removeFromLeft(120);
    lfSection.removeFromTop(35);  // Section label space

    auto lfGainBounds = lfSection.removeFromTop(130);  // Space for knob + labels
    lfGainBounds.removeFromTop(5);  // Small gap
    lfGainSlider.setBounds(lfGainBounds.withSizeKeepingCentre(85, 85));  // Compact but room for tick marks

    auto lfFreqBounds = lfSection.removeFromTop(130);  // Space for knob + labels
    lfFreqBounds.removeFromTop(5);  // Small gap
    lfFreqSlider.setBounds(lfFreqBounds.withSizeKeepingCentre(85, 85));  // Compact but room for tick marks

    lfBellButton.setBounds(lfSection.removeFromTop(35).withSizeKeepingCentre(60, 25));

    bounds.removeFromLeft(12);

    // LMF Band
    auto lmSection = bounds.removeFromLeft(120);
    lmSection.removeFromTop(35);  // Section label space

    auto lmGainBounds = lmSection.removeFromTop(130);  // Increased for value readout space
    lmGainBounds.removeFromTop(20);  // Space for label
    lmGainSlider.setBounds(lmGainBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

    auto lmFreqBounds = lmSection.removeFromTop(130);  // Increased for value readout space
    lmFreqBounds.removeFromTop(20);  // Space for label
    lmFreqSlider.setBounds(lmFreqBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

    auto lmQBounds = lmSection.removeFromTop(130);  // Increased for value readout space
    lmQBounds.removeFromTop(20);  // Space for label
    lmQSlider.setBounds(lmQBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

    bounds.removeFromLeft(12);

    // HMF Band
    auto hmSection = bounds.removeFromLeft(120);
    hmSection.removeFromTop(35);  // Section label space

    auto hmGainBounds = hmSection.removeFromTop(130);  // Increased for value readout space
    hmGainBounds.removeFromTop(20);  // Space for label
    hmGainSlider.setBounds(hmGainBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

    auto hmFreqBounds = hmSection.removeFromTop(130);  // Increased for value readout space
    hmFreqBounds.removeFromTop(20);  // Space for label
    hmFreqSlider.setBounds(hmFreqBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

    auto hmQBounds = hmSection.removeFromTop(130);  // Increased for value readout space
    hmQBounds.removeFromTop(20);  // Space for label
    hmQSlider.setBounds(hmQBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

    bounds.removeFromLeft(12);

    // HF Band
    auto hfSection = bounds.removeFromLeft(120);
    hfSection.removeFromTop(35);  // Section label space

    auto hfGainBounds = hfSection.removeFromTop(130);  // Increased for value readout space
    hfGainBounds.removeFromTop(20);  // Space for label
    hfGainSlider.setBounds(hfGainBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

    auto hfFreqBounds = hfSection.removeFromTop(130);  // Increased for value readout space
    hfFreqBounds.removeFromTop(20);  // Space for label
    hfFreqSlider.setBounds(hfFreqBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

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

    masterSection.removeFromTop(10);  // Gap

    // Output gain
    auto outputBounds = masterSection.removeFromTop(130);  // Increased for value readout space
    outputBounds.removeFromTop(20);  // Space for label
    outputGainSlider.setBounds(outputBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

    // Saturation
    auto satBounds = masterSection.removeFromTop(130);  // Increased for value readout space
    satBounds.removeFromTop(20);  // Space for label
    saturationSlider.setBounds(satBounds.withSizeKeepingCentre(85, 85));  // Larger to include tick labels

    // Position section labels to the left of each knob group
    // FILTERS label - positioned vertically between HPF and LPF
    auto filtersMidY = (hpfFreqSlider.getY() + lpfFreqSlider.getY() + lpfFreqSlider.getHeight()) / 2;
    filtersLabel.setBounds(10, filtersMidY - 10, 60, 20);

    // LF label - positioned to left of LF gain knob
    lfLabel.setBounds(lfGainSlider.getX() - 35, lfGainSlider.getY() + 30, 30, 20);

    // LMF label - positioned to left of LM gain knob
    lmfLabel.setBounds(lmGainSlider.getX() - 40, lmGainSlider.getY() + 30, 35, 20);

    // HMF label - positioned to left of HM gain knob
    hmfLabel.setBounds(hmGainSlider.getX() - 40, hmGainSlider.getY() + 30, 35, 20);

    // HF label - positioned to left of HF gain knob
    hfLabel.setBounds(hfGainSlider.getX() - 35, hfGainSlider.getY() + 30, 30, 20);

    // Position parameter labels below each knob (SSL style)
    // Helper to position a label centered below a knob
    auto positionLabelBelow = [](juce::Label& label, const juce::Slider& slider) {
        int labelWidth = 50;
        int labelHeight = 18;
        int yOffset = slider.getHeight() / 2 + 5;  // Position below knob
        label.setBounds(slider.getX() + (slider.getWidth() - labelWidth) / 2,
                       slider.getY() + yOffset,
                       labelWidth, labelHeight);
    };

    // Filter labels
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

    // Master section
    positionLabelBelow(outputLabel, outputGainSlider);
    positionLabelBelow(satLabel, saturationSlider);
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

        repaint();  // Update bypass LED and other visuals

        // Cache current values
        lastEqType = currentEqType;
        lastBypass = currentBypass;
    }

}

//==============================================================================
void FourKEQEditor::setupKnob(juce::Slider& slider, const juce::String& paramID,
                              const juce::String& label, bool centerDetented)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled(false, false, this);

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

        // Rotation range: 1.25π to 2.75π (270° sweep)
        float startAngle = juce::MathConstants<float>::pi * 1.25f;
        float endAngle = juce::MathConstants<float>::pi * 2.75f;
        float totalRange = endAngle - startAngle;

        int numTicks = values.size();

        for (int i = 0; i < numTicks; ++i)
        {
            float normalizedPos = static_cast<float>(i) / (numTicks - 1);
            float angle = startAngle + totalRange * normalizedPos;

            // Longer tick for center position
            bool isCenterTick = showCenterDot && (i == numTicks / 2);
            float tickLength = isCenterTick ? 5.0f : 3.0f;

            // Draw tick mark
            g.setColour(isCenterTick ? juce::Colour(0xff909090) : juce::Colour(0xff606060));
            float x1 = center.x + std::cos(angle) * radius;
            float y1 = center.y + std::sin(angle) * radius;
            float x2 = center.x + std::cos(angle) * (radius + tickLength);
            float y2 = center.y + std::sin(angle) * (radius + tickLength);
            g.drawLine(x1, y1, x2, y2, isCenterTick ? 1.5f : 1.0f);

            // Draw value label at key positions (start, middle, end)
            if (!values[i].isEmpty() && (i == 0 || i == numTicks / 2 || i == numTicks - 1))
            {
                g.setFont(juce::Font(juce::FontOptions(8.0f)));
                g.setColour(juce::Colour(0xff808080));

                float labelRadius = radius + tickLength + 8.0f;
                float labelX = center.x + std::cos(angle) * labelRadius;
                float labelY = center.y + std::sin(angle) * labelRadius;

                // Draw label with shadow for contrast
                g.setColour(juce::Colour(0xff000000));
                g.drawText(values[i], labelX - 15 + 1, labelY - 6 + 1, 30, 12, juce::Justification::centred);
                g.setColour(juce::Colour(0xff909090));
                g.drawText(values[i], labelX - 15, labelY - 6, 30, 12, juce::Justification::centred);
            }
        }
    };

    // Gain knobs: -15 to +15 dB
    std::vector<juce::String> gainValues = {"-15", "", "", "", "", "", "", "0", "", "", "", "", "", "", "+15"};

    // LF Frequency: 30-450 Hz
    std::vector<juce::String> lfFreqValues = {"30", "", "", "", "", "", "200", "", "", "", "", "", "", "450", ""};

    // LMF Frequency: 200Hz - 2.5kHz
    std::vector<juce::String> lmfFreqValues = {"200", "", "", "", "", "800", "", "", "", "", "", "2.5k", "", "", ""};

    // HMF Frequency: 600Hz - 7kHz
    std::vector<juce::String> hmfFreqValues = {"600", "", "", "", "", "2k", "", "", "", "", "", "7k", "", "", ""};

    // HF Frequency: 3kHz - 20kHz
    std::vector<juce::String> hfFreqValues = {"3k", "", "", "", "", "", "10k", "", "", "", "", "", "", "20k", ""};

    // Q values: 0.4 - 4.0
    std::vector<juce::String> qValues = {"0.4", "", "", "", "", "", "2.0", "", "", "", "", "", "", "4.0", ""};

    // HPF: 20-500 Hz
    std::vector<juce::String> hpfValues = {"20", "", "", "", "", "200", "", "", "", "", "", "", "500", "", ""};

    // LPF: 5k-20k Hz
    std::vector<juce::String> lpfValues = {"5k", "", "", "", "", "", "12k", "", "", "", "", "", "", "20k", ""};

    // Draw tick marks with values for each knob
    drawTicksWithValues(hpfFreqSlider.getBounds(), hpfValues, false);
    drawTicksWithValues(lpfFreqSlider.getBounds(), lpfValues, false);

    drawTicksWithValues(lfGainSlider.getBounds(), gainValues, true);
    drawTicksWithValues(lfFreqSlider.getBounds(), lfFreqValues, false);

    drawTicksWithValues(lmGainSlider.getBounds(), gainValues, true);
    drawTicksWithValues(lmFreqSlider.getBounds(), lmfFreqValues, false);
    drawTicksWithValues(lmQSlider.getBounds(), qValues, false);

    drawTicksWithValues(hmGainSlider.getBounds(), gainValues, true);
    drawTicksWithValues(hmFreqSlider.getBounds(), hmfFreqValues, false);
    drawTicksWithValues(hmQSlider.getBounds(), qValues, false);

    drawTicksWithValues(hfGainSlider.getBounds(), gainValues, true);
    drawTicksWithValues(hfFreqSlider.getBounds(), hfFreqValues, false);

    // Master section - Output gain: -18 to +18 dB
    std::vector<juce::String> outputGainValues = {"-18", "", "", "", "", "", "", "0", "", "", "", "", "", "", "+18"};
    drawTicksWithValues(outputGainSlider.getBounds(), outputGainValues, true);

    // Saturation: 0-100%
    std::vector<juce::String> satValues = {"0", "", "", "", "", "", "", "50", "", "", "", "", "", "", "100"};
    drawTicksWithValues(saturationSlider.getBounds(), satValues, false);
}
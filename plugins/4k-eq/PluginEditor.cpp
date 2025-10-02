#include "PluginEditor.h"

//==============================================================================
FourKEQEditor::FourKEQEditor(FourKEQ& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Set editor size - better height for layout
    setSize(900, 520);
    setResizable(true, true);
    setResizeLimits(750, 450, 1400, 700);

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
    oversamplingSelector.addItem("2x", 1);
    oversamplingSelector.addItem("4x", 2);
    oversamplingSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    oversamplingSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    oversamplingSelector.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff808080));
    addAndMakeVisible(oversamplingSelector);
    oversamplingAttachment = std::make_unique<ComboBoxAttachment>(
        audioProcessor.parameters, "oversampling", oversamplingSelector);

    // Spectrum analyzer setup
    spectrumAnalyzer.setSampleRate(audioProcessor.getSampleRate());
    addAndMakeVisible(spectrumAnalyzer);
    spectrumAnalyzer.setVisible(false);  // Hidden by default

    spectrumButton.setButtonText("SPECTRUM");
    spectrumButton.onClick = [this]()
    {
        bool showSpectrum = spectrumButton.getToggleState();
        spectrumAnalyzer.setVisible(showSpectrum);

        // Auto-resize window to accommodate spectrum analyzer
        int baseHeight = 520;
        int spectrumHeight = showSpectrum ? 150 : 0;
        setSize(getWidth(), baseHeight + spectrumHeight);
    };
    addAndMakeVisible(spectrumButton);

    // Pre/Post spectrum toggle
    spectrumPrePostButton.setButtonText("PRE");
    spectrumPrePostButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    spectrumPrePostButton.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff28a745));
    addAndMakeVisible(spectrumPrePostButton);
    spectrumPrePostAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "spectrum_prepost", spectrumPrePostButton);

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
    oversamplingSelector.setTooltip("Oversampling: 2x or 4x for alias-free processing");
    bypassButton.setTooltip("Bypass all EQ processing");
    spectrumButton.setTooltip("Toggle real-time spectrum analyzer display");

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

    // Preset selector in header (centered at top)
    auto headerBounds = bounds.removeFromTop(60);
    int centerX = headerBounds.getCentreX();
    presetSelector.setBounds(centerX - 100, 15, 200, 28);

    bounds.reduce(15, 10);

    // Filters section (left)
    auto filterSection = bounds.removeFromLeft(180);
    filterSection.removeFromTop(35);  // Section label space

    // HPF
    auto hpfBounds = filterSection.removeFromTop(140);
    hpfBounds.removeFromTop(20);  // Space for label
    hpfFreqSlider.setBounds(hpfBounds.withSizeKeepingCentre(75, 75));

    // LPF
    auto lpfBounds = filterSection.removeFromTop(140);
    lpfBounds.removeFromTop(20);  // Space for label
    lpfFreqSlider.setBounds(lpfBounds.withSizeKeepingCentre(75, 75));

    bounds.removeFromLeft(15);  // Gap

    // LF Band
    auto lfSection = bounds.removeFromLeft(120);
    lfSection.removeFromTop(35);  // Section label space

    auto lfGainBounds = lfSection.removeFromTop(110);
    lfGainBounds.removeFromTop(20);  // Space for label
    lfGainSlider.setBounds(lfGainBounds.withSizeKeepingCentre(65, 65));

    auto lfFreqBounds = lfSection.removeFromTop(110);
    lfFreqBounds.removeFromTop(20);  // Space for label
    lfFreqSlider.setBounds(lfFreqBounds.withSizeKeepingCentre(65, 65));

    lfBellButton.setBounds(lfSection.removeFromTop(35).withSizeKeepingCentre(60, 25));

    bounds.removeFromLeft(12);

    // LMF Band
    auto lmSection = bounds.removeFromLeft(120);
    lmSection.removeFromTop(35);  // Section label space

    auto lmGainBounds = lmSection.removeFromTop(110);
    lmGainBounds.removeFromTop(20);  // Space for label
    lmGainSlider.setBounds(lmGainBounds.withSizeKeepingCentre(65, 65));

    auto lmFreqBounds = lmSection.removeFromTop(110);
    lmFreqBounds.removeFromTop(20);  // Space for label
    lmFreqSlider.setBounds(lmFreqBounds.withSizeKeepingCentre(65, 65));

    auto lmQBounds = lmSection.removeFromTop(110);
    lmQBounds.removeFromTop(20);  // Space for label
    lmQSlider.setBounds(lmQBounds.withSizeKeepingCentre(65, 65));

    bounds.removeFromLeft(12);

    // HMF Band
    auto hmSection = bounds.removeFromLeft(120);
    hmSection.removeFromTop(35);  // Section label space

    auto hmGainBounds = hmSection.removeFromTop(110);
    hmGainBounds.removeFromTop(20);  // Space for label
    hmGainSlider.setBounds(hmGainBounds.withSizeKeepingCentre(65, 65));

    auto hmFreqBounds = hmSection.removeFromTop(110);
    hmFreqBounds.removeFromTop(20);  // Space for label
    hmFreqSlider.setBounds(hmFreqBounds.withSizeKeepingCentre(65, 65));

    auto hmQBounds = hmSection.removeFromTop(110);
    hmQBounds.removeFromTop(20);  // Space for label
    hmQSlider.setBounds(hmQBounds.withSizeKeepingCentre(65, 65));

    bounds.removeFromLeft(12);

    // HF Band
    auto hfSection = bounds.removeFromLeft(120);
    hfSection.removeFromTop(35);  // Section label space

    auto hfGainBounds = hfSection.removeFromTop(110);
    hfGainBounds.removeFromTop(20);  // Space for label
    hfGainSlider.setBounds(hfGainBounds.withSizeKeepingCentre(65, 65));

    auto hfFreqBounds = hfSection.removeFromTop(110);
    hfFreqBounds.removeFromTop(20);  // Space for label
    hfFreqSlider.setBounds(hfFreqBounds.withSizeKeepingCentre(65, 65));

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
    auto outputBounds = masterSection.removeFromTop(110);
    outputBounds.removeFromTop(20);  // Space for label
    outputGainSlider.setBounds(outputBounds.withSizeKeepingCentre(65, 65));

    // Saturation
    auto satBounds = masterSection.removeFromTop(110);
    satBounds.removeFromTop(20);  // Space for label
    saturationSlider.setBounds(satBounds.withSizeKeepingCentre(65, 65));

    masterSection.removeFromTop(10);  // Gap

    // Oversampling
    oversamplingSelector.setBounds(masterSection.removeFromTop(32).withSizeKeepingCentre(80, 28));

    masterSection.removeFromTop(10);  // Gap

    // Spectrum buttons
    auto specButtonArea = masterSection.removeFromTop(30);
    spectrumButton.setBounds(specButtonArea.removeFromLeft(80).withSizeKeepingCentre(80, 26));
    spectrumPrePostButton.setBounds(specButtonArea.withSizeKeepingCentre(50, 26));

    // Spectrum analyzer (below all controls if visible)
    if (spectrumAnalyzer.isVisible())
    {
        auto specBounds = getLocalBounds();
        specBounds.removeFromTop(60);  // Below header

        // Start below the knobs - find the bottom of the deepest control section
        int controlsBottom = 0;
        controlsBottom = juce::jmax(controlsBottom, hpfFreqSlider.getBottom());
        controlsBottom = juce::jmax(controlsBottom, lpfFreqSlider.getBottom());
        controlsBottom = juce::jmax(controlsBottom, lfBellButton.getBottom());
        controlsBottom = juce::jmax(controlsBottom, lmQSlider.getBottom());
        controlsBottom = juce::jmax(controlsBottom, hmQSlider.getBottom());
        controlsBottom = juce::jmax(controlsBottom, hfBellButton.getBottom());
        controlsBottom = juce::jmax(controlsBottom, saturationSlider.getBottom());

        // Position spectrum below controls with gap
        specBounds.removeFromTop(controlsBottom - 60 + 15);  // +15 for gap
        specBounds.removeFromBottom(10);  // Bottom margin
        specBounds.reduce(10, 0);
        spectrumAnalyzer.setBounds(specBounds);
    }

    // Position labels manually below each knob
    auto positionLabel = [](juce::Label* label, const juce::Slider& slider, int yOffset = 5) {
        if (label && label->isVisible()) {
            auto sliderBounds = slider.getBounds();
            label->setBounds(sliderBounds.getX() - 10,
                           sliderBounds.getBottom() + yOffset,
                           sliderBounds.getWidth() + 20, 15);
        }
    };

    // Position all labels
    int labelIdx = 0;
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), hpfFreqSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), lpfFreqSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), lfGainSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), lfFreqSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), lmGainSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), lmFreqSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), lmQSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), hmGainSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), hmFreqSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), hmQSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), hfGainSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), hfFreqSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), outputGainSlider);
    if (labelIdx < knobLabels.size()) positionLabel(knobLabels[labelIdx++].get(), saturationSlider);
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

    // Update spectrum analyzer sample rate if changed
    double currentSampleRate = audioProcessor.getSampleRate();
    if (currentSampleRate > 0.0 && currentSampleRate != lastSampleRate)
    {
        spectrumAnalyzer.setSampleRate(currentSampleRate);
        lastSampleRate = currentSampleRate;
    }

    // Push audio data to spectrum analyzer (thread-safe)
    // Use pre-EQ buffer if toggle is on, otherwise post-EQ (default)
    if (spectrumAnalyzer.isVisible())
    {
        const juce::ScopedLock sl(audioProcessor.spectrumBufferLock);
        bool usePreEQ = spectrumPrePostButton.getToggleState();
        const auto& bufferToUse = usePreEQ ? audioProcessor.spectrumBufferPre
                                           : audioProcessor.spectrumBuffer;
        spectrumAnalyzer.pushBuffer(bufferToUse);
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

    // Create label - brighter and larger
    auto knobLabel = std::make_unique<juce::Label>();
    knobLabel->setText(label, juce::dontSendNotification);
    knobLabel->setJustificationType(juce::Justification::centred);
    knobLabel->setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
    knobLabel->setColour(juce::Label::textColourId, juce::Colour(0xffd0d0d0));
    knobLabel->setInterceptsMouseClicks(false, false);
    // Add label to editor and store it
    addAndMakeVisible(knobLabel.get());
    knobLabels.push_back(std::move(knobLabel));
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
    // SSL-style knob tick markings for professional console aesthetics
    // Different marking styles for different knob types

    g.setColour(juce::Colour(0xff505050));  // Subtle gray for ticks

    // Helper lambda to draw tick marks around a knob
    auto drawTicksForKnob = [&g](juce::Rectangle<int> knobBounds,
                                  bool isGainKnob,
                                  bool isQKnob = false,
                                  bool isFilterKnob = false)
    {
        auto center = knobBounds.getCentre().toFloat();
        float radius = knobBounds.getWidth() / 2.0f + 8.0f;  // Ticks outside knob

        // Rotation range matches setupKnob: 1.25π to 2.75π (270° total, -135° to +135°)
        float startAngle = juce::MathConstants<float>::pi * 1.25f;
        float endAngle = juce::MathConstants<float>::pi * 2.75f;
        float totalRange = endAngle - startAngle;

        // Different tick configurations for different knob types
        int numMainTicks, numMinorTicks;
        std::vector<float> labeledPositions;  // Normalized 0-1 positions for labeled ticks

        if (isGainKnob)
        {
            // Gain: -20dB to +20dB with center detent at 0dB
            // Major ticks at: -20, -12, -6, 0, +6, +12, +20
            numMainTicks = 7;
            numMinorTicks = 0;  // No minor ticks for cleaner look
            labeledPositions = {0.0f, 0.2f, 0.35f, 0.5f, 0.65f, 0.8f, 1.0f};
        }
        else if (isQKnob)
        {
            // Q: 0.4 to 5.0 - logarithmic feel
            // Major ticks at start, middle, end
            numMainTicks = 5;
            numMinorTicks = 0;
            labeledPositions = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        }
        else if (isFilterKnob)
        {
            // HPF/LPF: frequency ranges
            // Major ticks at key frequencies
            numMainTicks = 5;
            numMinorTicks = 4;  // Minor ticks between majors
            labeledPositions = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        }
        else
        {
            // Frequency knobs: logarithmic scale
            // Major ticks at octave points
            numMainTicks = 7;
            numMinorTicks = 0;
            labeledPositions = {0.0f, 0.17f, 0.33f, 0.5f, 0.67f, 0.83f, 1.0f};
        }

        // Draw main ticks
        for (int i = 0; i < numMainTicks; ++i)
        {
            float normalizedPos = static_cast<float>(i) / (numMainTicks - 1);
            float angle = startAngle + totalRange * normalizedPos;

            // Longer tick at center position (0dB for gain knobs)
            bool isCenterTick = isGainKnob && (i == numMainTicks / 2);
            float tickLength = isCenterTick ? 6.0f : 4.0f;
            float tickWidth = isCenterTick ? 1.5f : 1.0f;

            // Brighter tick at center
            if (isCenterTick)
                g.setColour(juce::Colour(0xff808080));
            else
                g.setColour(juce::Colour(0xff505050));

            float x1 = center.x + std::cos(angle) * radius;
            float y1 = center.y + std::sin(angle) * radius;
            float x2 = center.x + std::cos(angle) * (radius + tickLength);
            float y2 = center.y + std::sin(angle) * (radius + tickLength);

            g.drawLine(x1, y1, x2, y2, tickWidth);
        }

        // Draw minor ticks (if any)
        if (numMinorTicks > 0)
        {
            g.setColour(juce::Colour(0xff404040));  // Dimmer for minor ticks
            int totalTicks = (numMainTicks - 1) * (numMinorTicks + 1) + 1;

            for (int i = 0; i < totalTicks; ++i)
            {
                // Skip positions where main ticks are
                if (i % (numMinorTicks + 1) == 0)
                    continue;

                float normalizedPos = static_cast<float>(i) / (totalTicks - 1);
                float angle = startAngle + totalRange * normalizedPos;

                float x1 = center.x + std::cos(angle) * radius;
                float y1 = center.y + std::sin(angle) * radius;
                float x2 = center.x + std::cos(angle) * (radius + 2.5f);  // Shorter
                float y2 = center.y + std::sin(angle) * (radius + 2.5f);

                g.drawLine(x1, y1, x2, y2, 0.5f);
            }
        }
    };

    // Draw ticks for all knobs with appropriate styles
    // Filters section (75px knobs)
    drawTicksForKnob(hpfFreqSlider.getBounds(), false, false, true);
    drawTicksForKnob(lpfFreqSlider.getBounds(), false, false, true);

    // LF Band (65px knobs)
    drawTicksForKnob(lfGainSlider.getBounds(), true);   // Gain knob
    drawTicksForKnob(lfFreqSlider.getBounds(), false);  // Freq knob

    // LMF Band
    drawTicksForKnob(lmGainSlider.getBounds(), true);
    drawTicksForKnob(lmFreqSlider.getBounds(), false);
    drawTicksForKnob(lmQSlider.getBounds(), false, true);  // Q knob

    // HMF Band
    drawTicksForKnob(hmGainSlider.getBounds(), true);
    drawTicksForKnob(hmFreqSlider.getBounds(), false);
    drawTicksForKnob(hmQSlider.getBounds(), false, true);

    // HF Band
    drawTicksForKnob(hfGainSlider.getBounds(), true);
    drawTicksForKnob(hfFreqSlider.getBounds(), false);

    // Master section
    drawTicksForKnob(outputGainSlider.getBounds(), true);     // Output gain
    drawTicksForKnob(saturationSlider.getBounds(), false);    // Saturation (0-100%)
}
#include "PluginEditor.h"

//==============================================================================
FourKEQEditor::FourKEQEditor(FourKEQ& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Set editor size - more compact height
    setSize(800, 450);
    setResizable(true, true);
    setResizeLimits(600, 380, 1200, 600);

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

    // Oversampling selector
    oversamplingSelector.addItem("2x", 1);
    oversamplingSelector.addItem("4x", 2);
    oversamplingSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    oversamplingSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    addAndMakeVisible(oversamplingSelector);
    oversamplingAttachment = std::make_unique<ComboBoxAttachment>(
        audioProcessor.parameters, "oversampling", oversamplingSelector);

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
    g.fillAll(juce::Colour(LunaLookAndFeel::BACKGROUND_COLOR));

    auto bounds = getLocalBounds();

    // Draw standard Luna header
    LunaLookAndFeel::drawPluginHeader(g, bounds, "4K EQ", "SSL-Style Equalizer");

    // EQ Type indicator
    bool isBlack = eqTypeParam->load() > 0.5f;
    g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    g.setColour(isBlack ? juce::Colour(0xff404040) : juce::Colour(0xff6B4423));
    g.fillRoundedRectangle(bounds.getRight() - 120, 12, 80, 26, 3);
    g.setColour(juce::Colour(LunaLookAndFeel::TEXT_COLOR));
    g.drawText(isBlack ? "BLACK" : "BROWN",
               bounds.getRight() - 120, 12, 80, 26,
               juce::Justification::centred);

    // Main content area
    bounds = getLocalBounds().withTrimmedTop(55);

    // Section dividers - vertical lines
    g.setColour(juce::Colour(LunaLookAndFeel::BORDER_COLOR));

    // Filters section divider
    int filterWidth = 180;
    g.fillRect(filterWidth, bounds.getY(), 3, bounds.getHeight());

    // EQ bands dividers
    int bandWidth = 120;
    int xPos = filterWidth + 3;

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
    g.setColour(juce::Colour(0xff909090));
    g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));

    int labelY = bounds.getY() + 10;
    g.drawText("FILTERS", 0, labelY, filterWidth, 20,
               juce::Justification::centred);

    xPos = filterWidth + 3;
    g.drawText("LF", xPos, labelY, bandWidth, 20,
               juce::Justification::centred);

    xPos += bandWidth + 2;
    g.drawText("LMF", xPos, labelY, bandWidth, 20,
               juce::Justification::centred);

    xPos += bandWidth + 2;
    g.drawText("HMF", xPos, labelY, bandWidth, 20,
               juce::Justification::centred);

    xPos += bandWidth + 2;
    g.drawText("HF", xPos, labelY, bandWidth, 20,
               juce::Justification::centred);

    xPos += bandWidth + 2;
    g.drawText("MASTER", xPos, labelY, bounds.getRight() - xPos, 20,
               juce::Justification::centred);

    // Frequency range indicators
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.setColour(juce::Colour(0xff808080));

    // Draw knob scale markings around each knob
    drawKnobMarkings(g);

    // Bypass LED
    bool bypassed = bypassParam->load() > 0.5f;
    int ledX = bounds.getRight() - 40;
    int ledY = 15;

    if (!bypassed) {
        // Orange LED when active (Luna brand color)
        g.setColour(juce::Colour(LunaLookAndFeel::ACCENT_COLOR));
        g.fillEllipse(ledX, ledY, 12, 12);
        g.setColour(juce::Colour(LunaLookAndFeel::ACCENT_COLOR).withAlpha(0.3f));
        g.fillEllipse(ledX - 2, ledY - 2, 16, 16);
    } else {
        // Dark when bypassed
        g.setColour(juce::Colour(0xff400000));
        g.fillEllipse(ledX, ledY, 12, 12);
    }

    // Power indicator with Luna accent
    g.setColour(juce::Colour(LunaLookAndFeel::ACCENT_COLOR));
    g.fillEllipse(10, 15, 8, 8);
    g.setColour(juce::Colour(LunaLookAndFeel::TEXT_COLOR));
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.drawText("PWR", 20, 12, 30, 15, juce::Justification::centredLeft);
}

void FourKEQEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50);  // Reduced header space
    bounds.reduce(10, 10);

    // Filters section (left)
    auto filterSection = bounds.removeFromLeft(170);
    filterSection.removeFromTop(25);  // Reduced section label space

    // HPF
    auto hpfBounds = filterSection.removeFromTop(100);  // Reduced from 140
    hpfFreqSlider.setBounds(hpfBounds.withSizeKeepingCentre(80, 80));

    // LPF
    auto lpfBounds = filterSection.removeFromTop(100);  // Reduced from 140
    lpfFreqSlider.setBounds(lpfBounds.withSizeKeepingCentre(80, 80));

    bounds.removeFromLeft(10);  // Gap

    // LF Band
    auto lfSection = bounds.removeFromLeft(110);
    lfSection.removeFromTop(25);  // Reduced from 30

    auto lfGainBounds = lfSection.removeFromTop(75);  // Reduced from 90
    lfGainSlider.setBounds(lfGainBounds.withSizeKeepingCentre(70, 70));

    auto lfFreqBounds = lfSection.removeFromTop(75);  // Reduced from 90
    lfFreqSlider.setBounds(lfFreqBounds.withSizeKeepingCentre(70, 70));

    lfBellButton.setBounds(lfSection.removeFromTop(30).withSizeKeepingCentre(60, 25));

    bounds.removeFromLeft(10);

    // LMF Band
    auto lmSection = bounds.removeFromLeft(110);
    lmSection.removeFromTop(25);  // Reduced from 30

    auto lmGainBounds = lmSection.removeFromTop(75);  // Reduced from 90
    lmGainSlider.setBounds(lmGainBounds.withSizeKeepingCentre(70, 70));

    auto lmFreqBounds = lmSection.removeFromTop(75);  // Reduced from 90
    lmFreqSlider.setBounds(lmFreqBounds.withSizeKeepingCentre(70, 70));

    auto lmQBounds = lmSection.removeFromTop(75);  // Reduced from 90
    lmQSlider.setBounds(lmQBounds.withSizeKeepingCentre(70, 70));

    bounds.removeFromLeft(10);

    // HMF Band
    auto hmSection = bounds.removeFromLeft(110);
    hmSection.removeFromTop(25);  // Reduced from 30

    auto hmGainBounds = hmSection.removeFromTop(75);  // Reduced from 90
    hmGainSlider.setBounds(hmGainBounds.withSizeKeepingCentre(70, 70));

    auto hmFreqBounds = hmSection.removeFromTop(75);  // Reduced from 90
    hmFreqSlider.setBounds(hmFreqBounds.withSizeKeepingCentre(70, 70));

    auto hmQBounds = hmSection.removeFromTop(75);  // Reduced from 90
    hmQSlider.setBounds(hmQBounds.withSizeKeepingCentre(70, 70));

    bounds.removeFromLeft(10);

    // HF Band
    auto hfSection = bounds.removeFromLeft(110);
    hfSection.removeFromTop(25);  // Reduced from 30

    auto hfGainBounds = hfSection.removeFromTop(75);  // Reduced from 90
    hfGainSlider.setBounds(hfGainBounds.withSizeKeepingCentre(70, 70));

    auto hfFreqBounds = hfSection.removeFromTop(75);  // Reduced from 90
    hfFreqSlider.setBounds(hfFreqBounds.withSizeKeepingCentre(70, 70));

    hfBellButton.setBounds(hfSection.removeFromTop(30).withSizeKeepingCentre(60, 25));

    bounds.removeFromLeft(10);

    // Master section
    auto masterSection = bounds;
    masterSection.removeFromTop(25);  // Reduced from 30

    // EQ Type selector
    eqTypeSelector.setBounds(masterSection.removeFromTop(28).withSizeKeepingCentre(100, 25));

    // Bypass button
    bypassButton.setBounds(masterSection.removeFromTop(35).withSizeKeepingCentre(80, 30));

    // Output gain
    auto outputBounds = masterSection.removeFromTop(75);  // Reduced from 90
    outputGainSlider.setBounds(outputBounds.withSizeKeepingCentre(70, 70));

    // Saturation
    auto satBounds = masterSection.removeFromTop(75);  // Reduced from 90
    saturationSlider.setBounds(satBounds.withSizeKeepingCentre(70, 70));

    // Oversampling
    oversamplingSelector.setBounds(masterSection.removeFromTop(28).withSizeKeepingCentre(80, 25));
}

void FourKEQEditor::timerCallback()
{
    // Update UI based on EQ type
    bool isBlack = eqTypeParam->load() > 0.5f;
    lfBellButton.setVisible(isBlack);
    hfBellButton.setVisible(isBlack);
    lmQSlider.setVisible(true);  // Always visible in both modes
    hmQSlider.setVisible(true);

    repaint();  // Update bypass LED
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

    if (centerDetented) {
        slider.setDoubleClickReturnValue(true, 0.0);
    }

    addAndMakeVisible(slider);

    // Create label - brighter and larger
    auto knobLabel = std::make_unique<juce::Label>();
    knobLabel->setText(label, juce::dontSendNotification);
    knobLabel->setJustificationType(juce::Justification::centred);
    knobLabel->setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));  // Larger (was 9.0f)
    knobLabel->setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));  // Much brighter (was 0xffc0c0c0)
    knobLabel->attachToComponent(&slider, false);
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
    // This would draw scale markings around knobs
    // Left as placeholder for detailed scale graphics
}
#include "PluginEditor.h"

//==============================================================================
FourKEQEditor::FourKEQEditor(FourKEQ& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Set editor size - professional console proportions
    setSize(920, 420);
    setResizable(false, false);

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
    lfGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lf_gain", lfGainSlider);

    setupKnob(lfFreqSlider, "lf_freq", "FREQ");
    lfFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lf_freq", lfFreqSlider);

    setupButton(lfBellButton, "BELL");
    lfBellAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "lf_bell", lfBellButton);

    // LM Band
    setupKnob(lmGainSlider, "lm_gain", "GAIN", true);
    lmGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lm_gain", lmGainSlider);

    setupKnob(lmFreqSlider, "lm_freq", "FREQ");
    lmFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lm_freq", lmFreqSlider);

    setupKnob(lmQSlider, "lm_q", "Q");
    lmQAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "lm_q", lmQSlider);

    // HM Band
    setupKnob(hmGainSlider, "hm_gain", "GAIN", true);
    hmGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hm_gain", hmGainSlider);

    setupKnob(hmFreqSlider, "hm_freq", "FREQ");
    hmFreqAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hm_freq", hmFreqSlider);

    setupKnob(hmQSlider, "hm_q", "Q");
    hmQAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hm_q", hmQSlider);

    // HF Band
    setupKnob(hfGainSlider, "hf_gain", "GAIN", true);
    hfGainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "hf_gain", hfGainSlider);

    setupKnob(hfFreqSlider, "hf_freq", "FREQ");
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
    // SSL console background - authentic dark charcoal
    g.fillAll(juce::Colour(0xff2d2d2d));

    // Draw main panel with subtle gradient
    auto bounds = getLocalBounds();
    juce::ColourGradient backgroundGradient(
        juce::Colour(0xff353535), 0, 0,
        juce::Colour(0xff252525), 0, (float)bounds.getHeight(), false);
    g.setGradientFill(backgroundGradient);
    g.fillRect(bounds);

    // Top section - branding area
    auto topSection = bounds.removeFromTop(50);
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRect(topSection);

    // Beveled edge
    g.setColour(juce::Colour(0xff4a4a4a));
    g.drawLine(0, topSection.getBottom(), bounds.getWidth(), topSection.getBottom(), 2);

    // Logo and title
    g.setColour(juce::Colour(0xffe0e0e0));
    g.setFont(juce::Font(juce::FontOptions(24.0f).withStyle("Bold")));
    g.drawText("4K EQ", topSection.removeFromLeft(200),
               juce::Justification::centred);

    // Series indicator
    g.setFont(juce::Font(juce::FontOptions(16.0f)));
    g.setColour(juce::Colour(0xffa0a0a0));
    g.drawText("EQUALIZER", topSection.removeFromLeft(200),
               juce::Justification::centred);

    // EQ Type indicator with color coding
    bool isBlack = eqTypeParam->load() > 0.5f;
    g.setFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));
    g.setColour(isBlack ? juce::Colour(0xff303030) : juce::Colour(0xff8B5A2B));
    g.fillRoundedRectangle(topSection.getRight() - 180, 10, 100, 30, 3);
    g.setColour(juce::Colour(0xffe0e0e0));
    g.drawText(isBlack ? "BLACK" : "BROWN",
               topSection.getRight() - 180, 10, 100, 30,
               juce::Justification::centred);

    // Draw section panels
    bounds = getLocalBounds().withTrimmedTop(55);

    // Section dividers - vertical lines
    g.setColour(juce::Colour(0xff1a1a1a));

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
    g.setColour(juce::Colour(0xff606060));
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));

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
    g.setFont(juce::Font(juce::FontOptions(8.0f)));
    g.setColour(juce::Colour(0xff808080));

    // Draw knob scale markings around each knob
    drawKnobMarkings(g);

    // Bypass LED
    bool bypassed = bypassParam->load() > 0.5f;
    int ledX = bounds.getRight() - 40;
    int ledY = 15;

    if (!bypassed) {
        // Green LED when active
        g.setColour(juce::Colour(0xff00ff00));
        g.fillEllipse(ledX, ledY, 12, 12);
        g.setColour(juce::Colour(0x4000ff00));
        g.fillEllipse(ledX - 2, ledY - 2, 16, 16);
    } else {
        // Dark red when bypassed
        g.setColour(juce::Colour(0xff400000));
        g.fillEllipse(ledX, ledY, 12, 12);
    }

    // Power indicator
    g.setColour(juce::Colour(0xff00ff00));
    g.fillEllipse(10, 15, 8, 8);
    g.setColour(juce::Colour(0xffe0e0e0));
    g.setFont(juce::Font(juce::FontOptions(8.0f)));
    g.drawText("PWR", 20, 12, 30, 15, juce::Justification::centredLeft);
}

void FourKEQEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(60);  // Space for header
    bounds.reduce(10, 10);

    // Filters section (left)
    auto filterSection = bounds.removeFromLeft(170);
    filterSection.removeFromTop(30);  // Section label space

    // HPF
    auto hpfBounds = filterSection.removeFromTop(140);
    hpfFreqSlider.setBounds(hpfBounds.withSizeKeepingCentre(80, 80));

    // LPF
    auto lpfBounds = filterSection.removeFromTop(140);
    lpfFreqSlider.setBounds(lpfBounds.withSizeKeepingCentre(80, 80));

    bounds.removeFromLeft(10);  // Gap

    // LF Band
    auto lfSection = bounds.removeFromLeft(110);
    lfSection.removeFromTop(30);

    auto lfGainBounds = lfSection.removeFromTop(90);
    lfGainSlider.setBounds(lfGainBounds.withSizeKeepingCentre(70, 70));

    auto lfFreqBounds = lfSection.removeFromTop(90);
    lfFreqSlider.setBounds(lfFreqBounds.withSizeKeepingCentre(70, 70));

    lfBellButton.setBounds(lfSection.removeFromTop(40).withSizeKeepingCentre(60, 25));

    bounds.removeFromLeft(10);

    // LMF Band
    auto lmSection = bounds.removeFromLeft(110);
    lmSection.removeFromTop(30);

    auto lmGainBounds = lmSection.removeFromTop(90);
    lmGainSlider.setBounds(lmGainBounds.withSizeKeepingCentre(70, 70));

    auto lmFreqBounds = lmSection.removeFromTop(90);
    lmFreqSlider.setBounds(lmFreqBounds.withSizeKeepingCentre(70, 70));

    auto lmQBounds = lmSection.removeFromTop(90);
    lmQSlider.setBounds(lmQBounds.withSizeKeepingCentre(70, 70));

    bounds.removeFromLeft(10);

    // HMF Band
    auto hmSection = bounds.removeFromLeft(110);
    hmSection.removeFromTop(30);

    auto hmGainBounds = hmSection.removeFromTop(90);
    hmGainSlider.setBounds(hmGainBounds.withSizeKeepingCentre(70, 70));

    auto hmFreqBounds = hmSection.removeFromTop(90);
    hmFreqSlider.setBounds(hmFreqBounds.withSizeKeepingCentre(70, 70));

    auto hmQBounds = hmSection.removeFromTop(90);
    hmQSlider.setBounds(hmQBounds.withSizeKeepingCentre(70, 70));

    bounds.removeFromLeft(10);

    // HF Band
    auto hfSection = bounds.removeFromLeft(110);
    hfSection.removeFromTop(30);

    auto hfGainBounds = hfSection.removeFromTop(90);
    hfGainSlider.setBounds(hfGainBounds.withSizeKeepingCentre(70, 70));

    auto hfFreqBounds = hfSection.removeFromTop(90);
    hfFreqSlider.setBounds(hfFreqBounds.withSizeKeepingCentre(70, 70));

    hfBellButton.setBounds(hfSection.removeFromTop(40).withSizeKeepingCentre(60, 25));

    bounds.removeFromLeft(10);

    // Master section
    auto masterSection = bounds;
    masterSection.removeFromTop(30);

    // EQ Type selector
    eqTypeSelector.setBounds(masterSection.removeFromTop(30).withSizeKeepingCentre(100, 25));

    // Bypass button
    bypassButton.setBounds(masterSection.removeFromTop(40).withSizeKeepingCentre(80, 30));

    // Output gain
    auto outputBounds = masterSection.removeFromTop(90);
    outputGainSlider.setBounds(outputBounds.withSizeKeepingCentre(70, 70));

    // Saturation
    auto satBounds = masterSection.removeFromTop(90);
    saturationSlider.setBounds(satBounds.withSizeKeepingCentre(70, 70));

    // Oversampling
    oversamplingSelector.setBounds(masterSection.removeFromTop(30).withSizeKeepingCentre(80, 25));
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

    // Create label
    auto knobLabel = std::make_unique<juce::Label>();
    knobLabel->setText(label, juce::dontSendNotification);
    knobLabel->setJustificationType(juce::Justification::centred);
    knobLabel->setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
    knobLabel->setColour(juce::Label::textColourId, juce::Colour(0xffc0c0c0));
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
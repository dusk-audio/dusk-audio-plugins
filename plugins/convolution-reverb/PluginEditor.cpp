/*
  ==============================================================================

    Convolution Reverb - Plugin Editor
    Main UI for the convolution reverb
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

ConvolutionReverbEditor::ConvolutionReverbEditor(ConvolutionReverbProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);
    setResizable(false, false);

    // IR Browser
    irBrowser = std::make_unique<IRBrowser>();
    irBrowser->addListener(this);

    // Set IR directory
    auto irDir = audioProcessor.getCustomIRDirectory();
    if (!irDir.exists())
        irDir = audioProcessor.getDefaultIRDirectory();

    if (irDir.exists())
        irBrowser->setRootDirectory(irDir);

    addAndMakeVisible(irBrowser.get());

    // Waveform display
    waveformDisplay = std::make_unique<IRWaveformDisplay>();
    waveformDisplay->setWaveformColour(lookAndFeel.getWaveformColour());
    waveformDisplay->setEnvelopeColour(lookAndFeel.getEnvelopeColour());
    waveformDisplay->setBackgroundColour(lookAndFeel.getBackgroundColour());
    addAndMakeVisible(waveformDisplay.get());

    // IR name label
    irNameLabel = std::make_unique<juce::Label>("irName", "No IR Loaded");
    irNameLabel->setFont(juce::Font(13.0f, juce::Font::bold));
    irNameLabel->setColour(juce::Label::textColourId, lookAndFeel.getAccentColour());
    irNameLabel->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(irNameLabel.get());

    // Envelope controls
    attackSlider = std::make_unique<LunaSlider>();
    decaySlider = std::make_unique<LunaSlider>();
    lengthSlider = std::make_unique<LunaSlider>();
    attackLabel = std::make_unique<juce::Label>();
    decayLabel = std::make_unique<juce::Label>();
    lengthLabel = std::make_unique<juce::Label>();

    setupSlider(*attackSlider, *attackLabel, "ATTACK");
    setupSlider(*decaySlider, *decayLabel, "DECAY");
    setupSlider(*lengthSlider, *lengthLabel, "LENGTH", "%");

    reverseButton = std::make_unique<juce::ToggleButton>("REV");
    setupToggleButton(*reverseButton, "REV");

    // Main controls
    preDelaySlider = std::make_unique<LunaSlider>();
    widthSlider = std::make_unique<LunaSlider>();
    mixSlider = std::make_unique<LunaSlider>();
    preDelayLabel = std::make_unique<juce::Label>();
    widthLabel = std::make_unique<juce::Label>();
    mixLabel = std::make_unique<juce::Label>();

    setupSlider(*preDelaySlider, *preDelayLabel, "PRE-DELAY", "ms");
    setupSlider(*widthSlider, *widthLabel, "WIDTH");
    setupSlider(*mixSlider, *mixLabel, "MIX", "%");

    // Filter controls
    hpfSlider = std::make_unique<LunaSlider>();
    lpfSlider = std::make_unique<LunaSlider>();
    hpfLabel = std::make_unique<juce::Label>();
    lpfLabel = std::make_unique<juce::Label>();

    setupSlider(*hpfSlider, *hpfLabel, "HPF", "Hz");
    setupSlider(*lpfSlider, *lpfLabel, "LPF", "Hz");

    // EQ controls - simplified to just gain knobs (frequencies are fixed internally)
    eqLowFreqSlider = std::make_unique<LunaSlider>();
    eqLowGainSlider = std::make_unique<LunaSlider>();
    eqLowMidFreqSlider = std::make_unique<LunaSlider>();
    eqLowMidGainSlider = std::make_unique<LunaSlider>();
    eqHighMidFreqSlider = std::make_unique<LunaSlider>();
    eqHighMidGainSlider = std::make_unique<LunaSlider>();
    eqHighFreqSlider = std::make_unique<LunaSlider>();
    eqHighGainSlider = std::make_unique<LunaSlider>();
    eqLowLabel = std::make_unique<juce::Label>();
    eqLowMidLabel = std::make_unique<juce::Label>();
    eqHighMidLabel = std::make_unique<juce::Label>();
    eqHighLabel = std::make_unique<juce::Label>();

    // Only show gain controls (frequency sliders exist but are hidden)
    setupSlider(*eqLowGainSlider, *eqLowLabel, "LOW", "dB");
    setupSlider(*eqLowMidGainSlider, *eqLowMidLabel, "LO-MID", "dB");
    setupSlider(*eqHighMidGainSlider, *eqHighMidLabel, "HI-MID", "dB");
    setupSlider(*eqHighGainSlider, *eqHighLabel, "HIGH", "dB");

    // Frequency sliders are not visible (parameters still exist for internal use)
    eqLowFreqSlider->setVisible(false);
    eqLowMidFreqSlider->setVisible(false);
    eqHighMidFreqSlider->setVisible(false);
    eqHighFreqSlider->setVisible(false);

    // Latency toggle
    zeroLatencyButton = std::make_unique<juce::ToggleButton>("ZERO LAT");
    setupToggleButton(*zeroLatencyButton, "ZERO LAT");

    // IR Offset control - shortened label to fit
    irOffsetSlider = std::make_unique<LunaSlider>();
    irOffsetLabel = std::make_unique<juce::Label>();
    setupSlider(*irOffsetSlider, *irOffsetLabel, "OFFSET", "%");

    // Quality dropdown
    qualityComboBox = std::make_unique<juce::ComboBox>();
    qualityComboBox->addItem("Lo-Fi", 1);
    qualityComboBox->addItem("Low", 2);
    qualityComboBox->addItem("Medium", 3);
    qualityComboBox->addItem("High", 4);
    qualityComboBox->onChange = [this]() { updateQualityInfo(); };
    addAndMakeVisible(qualityComboBox.get());

    qualityLabel = std::make_unique<juce::Label>("", "QUALITY");
    qualityLabel->setFont(juce::Font(10.0f, juce::Font::bold));
    qualityLabel->setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    qualityLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(qualityLabel.get());

    qualityInfoLabel = std::make_unique<juce::Label>("", "48 kHz");
    qualityInfoLabel->setFont(juce::Font(9.0f));
    qualityInfoLabel->setColour(juce::Label::textColourId, lookAndFeel.getAccentColour());
    qualityInfoLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(qualityInfoLabel.get());

    // Stereo mode dropdown
    stereoModeComboBox = std::make_unique<juce::ComboBox>();
    stereoModeComboBox->addItem("True Stereo", 1);
    stereoModeComboBox->addItem("Mono-Stereo", 2);
    addAndMakeVisible(stereoModeComboBox.get());

    stereoModeLabel = std::make_unique<juce::Label>("", "STEREO");
    stereoModeLabel->setFont(juce::Font(10.0f, juce::Font::bold));
    stereoModeLabel->setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    stereoModeLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(stereoModeLabel.get());

    // A/B comparison controls
    abToggleButton = std::make_unique<juce::ToggleButton>("A/B");
    abToggleButton->setButtonText("A");
    abToggleButton->onClick = [this]()
    {
        if (isStateB)
        {
            // Save state B, switch to A
            saveCurrentStateToSlot(stateB);
            isStateB = false;
            loadStateFromSlot(stateA);
            abToggleButton->setButtonText("A");
        }
        else
        {
            // Save state A, switch to B
            saveCurrentStateToSlot(stateA);
            isStateB = true;
            loadStateFromSlot(stateB);
            abToggleButton->setButtonText("B");
        }
    };
    addAndMakeVisible(abToggleButton.get());

    abCopyButton = std::make_unique<juce::TextButton>("Copy");
    abCopyButton->onClick = [this]() { copyCurrentToOther(); };
    addAndMakeVisible(abCopyButton.get());

    // Mix wet/dry labels
    mixDryLabel = std::make_unique<juce::Label>("", "DRY");
    mixDryLabel->setFont(juce::Font(8.0f));
    mixDryLabel->setColour(juce::Label::textColourId, juce::Colour(0xff707070));
    mixDryLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(mixDryLabel.get());

    mixWetLabel = std::make_unique<juce::Label>("", "WET");
    mixWetLabel->setFont(juce::Font(8.0f));
    mixWetLabel->setColour(juce::Label::textColourId, juce::Colour(0xff707070));
    mixWetLabel->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mixWetLabel.get());

    // Volume Compensation toggle
    volumeCompButton = std::make_unique<juce::ToggleButton>("VOL COMP");
    setupToggleButton(*volumeCompButton, "VOL COMP");

    // Filter Envelope controls
    filterEnvButton = std::make_unique<juce::ToggleButton>("FILTER ENV");
    setupToggleButton(*filterEnvButton, "FILTER ENV");

    filterEnvInitSlider = std::make_unique<LunaSlider>();
    filterEnvEndSlider = std::make_unique<LunaSlider>();
    filterEnvAttackSlider = std::make_unique<LunaSlider>();
    filterEnvInitLabel = std::make_unique<juce::Label>();
    filterEnvEndLabel = std::make_unique<juce::Label>();
    filterEnvAttackLabel = std::make_unique<juce::Label>();

    setupSlider(*filterEnvInitSlider, *filterEnvInitLabel, "INIT", "Hz");
    setupSlider(*filterEnvEndSlider, *filterEnvEndLabel, "END", "Hz");
    setupSlider(*filterEnvAttackSlider, *filterEnvAttackLabel, "F.ATK");

    // Meters (stereo mode)
    inputMeter = std::make_unique<LEDMeter>();
    inputMeter->setStereoMode(true);
    outputMeter = std::make_unique<LEDMeter>();
    outputMeter->setStereoMode(true);
    addAndMakeVisible(inputMeter.get());
    addAndMakeVisible(outputMeter.get());

    inputMeterLabel = std::make_unique<juce::Label>("", "IN");
    inputMeterLabel->setFont(juce::Font(10.0f, juce::Font::bold));
    inputMeterLabel->setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    inputMeterLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(inputMeterLabel.get());

    outputMeterLabel = std::make_unique<juce::Label>("", "OUT");
    outputMeterLabel->setFont(juce::Font(10.0f, juce::Font::bold));
    outputMeterLabel->setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    outputMeterLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(outputMeterLabel.get());

    // Attachments
    auto& params = audioProcessor.getValueTreeState();

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "mix", *mixSlider);
    preDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "predelay", *preDelaySlider);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "attack", *attackSlider);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "decay", *decaySlider);
    lengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "length", *lengthSlider);
    reverseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "reverse", *reverseButton);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "width", *widthSlider);
    hpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "hpf_freq", *hpfSlider);
    lpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "lpf_freq", *lpfSlider);
    eqLowFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "eq_low_freq", *eqLowFreqSlider);
    eqLowGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "eq_low_gain", *eqLowGainSlider);
    eqLowMidFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "eq_lmid_freq", *eqLowMidFreqSlider);
    eqLowMidGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "eq_lmid_gain", *eqLowMidGainSlider);
    eqHighMidFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "eq_hmid_freq", *eqHighMidFreqSlider);
    eqHighMidGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "eq_hmid_gain", *eqHighMidGainSlider);
    eqHighFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "eq_high_freq", *eqHighFreqSlider);
    eqHighGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "eq_high_gain", *eqHighGainSlider);
    zeroLatencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "zero_latency", *zeroLatencyButton);

    // New parameter attachments
    irOffsetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "ir_offset", *irOffsetSlider);
    qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        params, "quality", *qualityComboBox);
    volumeCompAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "volume_comp", *volumeCompButton);
    filterEnvAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "filter_env_enabled", *filterEnvButton);
    filterEnvInitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "filter_env_init_freq", *filterEnvInitSlider);
    filterEnvEndAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "filter_env_end_freq", *filterEnvEndSlider);
    filterEnvAttackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "filter_env_attack", *filterEnvAttackSlider);
    stereoModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        params, "stereo_mode", *stereoModeComboBox);

    // Create value display labels
    createValueLabel(preDelayValueLabel);
    createValueLabel(widthValueLabel);
    createValueLabel(mixValueLabel);
    createValueLabel(attackValueLabel);
    createValueLabel(decayValueLabel);
    createValueLabel(lengthValueLabel);
    createValueLabel(hpfValueLabel);
    createValueLabel(lpfValueLabel);
    createValueLabel(eqLowValueLabel);
    createValueLabel(eqLowMidValueLabel);
    createValueLabel(eqHighMidValueLabel);
    createValueLabel(eqHighValueLabel);
    createValueLabel(irOffsetValueLabel);
    createValueLabel(filterEnvInitValueLabel);
    createValueLabel(filterEnvEndValueLabel);
    createValueLabel(filterEnvAttackValueLabel);

    // Add envelope parameter listeners
    attackSlider->onValueChange = [this]() { updateEnvelopeDisplay(); updateValueLabels(); };
    decaySlider->onValueChange = [this]() { updateEnvelopeDisplay(); updateValueLabels(); };
    lengthSlider->onValueChange = [this]() { updateEnvelopeDisplay(); updateValueLabels(); };
    reverseButton->onClick = [this]()
    {
        waveformDisplay->setReversed(reverseButton->getToggleState());
    };

    // Add value change listeners for all sliders
    preDelaySlider->onValueChange = [this]() { updateValueLabels(); };
    widthSlider->onValueChange = [this]() { updateValueLabels(); };
    mixSlider->onValueChange = [this]() { updateValueLabels(); };
    hpfSlider->onValueChange = [this]() { updateValueLabels(); repaint(); };  // Triggers EQ curve redraw
    lpfSlider->onValueChange = [this]() { updateValueLabels(); repaint(); };  // Triggers EQ curve redraw
    eqLowGainSlider->onValueChange = [this]() { updateValueLabels(); repaint(); };  // Triggers EQ curve redraw
    eqLowMidGainSlider->onValueChange = [this]() { updateValueLabels(); repaint(); };  // Triggers EQ curve redraw
    eqHighMidGainSlider->onValueChange = [this]() { updateValueLabels(); repaint(); };  // Triggers EQ curve redraw
    eqHighGainSlider->onValueChange = [this]() { updateValueLabels(); repaint(); };  // Triggers EQ curve redraw
    irOffsetSlider->onValueChange = [this]() { updateValueLabels(); };
    filterEnvInitSlider->onValueChange = [this]() { updateValueLabels(); };
    filterEnvEndSlider->onValueChange = [this]() { updateValueLabels(); };
    filterEnvAttackSlider->onValueChange = [this]() { updateValueLabels(); };

    // Initial waveform update
    updateWaveformDisplay();
    updateIRNameLabel();

    // Set size AFTER all components are created (setSize triggers resized())
    setSize(900, 700);

    // Initial value labels update
    updateValueLabels();

    startTimerHz(30);
}

ConvolutionReverbEditor::~ConvolutionReverbEditor()
{
    stopTimer();

    if (irBrowser != nullptr)
        irBrowser->removeListener(this);

    setLookAndFeel(nullptr);
}

void ConvolutionReverbEditor::setupSlider(juce::Slider& slider, juce::Label& label,
                                      const juce::String& labelText, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    LunaSliderStyle::configureKnob(slider);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f, true);
    if (suffix.isNotEmpty())
        slider.setTextValueSuffix(" " + suffix);
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setFont(juce::Font(10.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void ConvolutionReverbEditor::setupToggleButton(juce::ToggleButton& button, const juce::String& text)
{
    button.setButtonText(text);
    addAndMakeVisible(button);
}

void ConvolutionReverbEditor::paint(juce::Graphics& g)
{
    // Main background
    g.fillAll(lookAndFeel.getBackgroundColour());

    auto bounds = getLocalBounds();

    // Header
    auto headerBounds = bounds.removeFromTop(55);
    g.setColour(lookAndFeel.getPanelColour());
    g.fillRect(headerBounds);

    // Plugin name
    g.setFont(juce::Font(26.0f, juce::Font::bold));
    g.setColour(lookAndFeel.getTextColour());
    g.drawText("CONVOLUTION REVERB", headerBounds.reduced(20, 0).removeFromLeft(350),
               juce::Justification::centredLeft);

    // Subtitle
    g.setFont(juce::Font(11.0f));
    g.setColour(juce::Colour(0xff909090));
    g.drawText("Impulse Response Processor", 20, 32, 200, 20, juce::Justification::left);

    // Company name
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(lookAndFeel.getAccentColour());
    g.drawText("LUNA CO. AUDIO", headerBounds.removeFromRight(170).reduced(20, 0),
               juce::Justification::centredRight);

    // A/B toggle label
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff707070));
    g.drawText("A/B", 380, 18, 30, 15, juce::Justification::centred);

    // Header divider with gradient effect
    juce::ColourGradient dividerGrad(
        juce::Colour(0xff505050), 0, 55,
        juce::Colour(0xff2a2a2a), static_cast<float>(getWidth()), 55,
        false
    );
    g.setGradientFill(dividerGrad);
    g.fillRect(0.0f, 54.0f, static_cast<float>(getWidth()), 2.0f);

    // Subtle highlight below the dark line
    g.setColour(juce::Colour(0x18FFFFFF));
    g.fillRect(0.0f, 56.0f, static_cast<float>(getWidth()), 1.0f);

    // ========== SECTION BACKGROUND PANELS ==========
    // Very subtle semi-transparent overlay panels for visual grouping (~5-6% white overlay)
    auto sectionPanelColour = juce::Colour(0x0dFFFFFF);  // ~5% white overlay - very subtle
    auto sectionBorderColour = juce::Colour(0x15FFFFFF);  // ~8% white for border
    float cornerRadius = 5.0f;

    // Envelope section panel (around Attack, Decay, Length, IR Offset, Reverse)
    auto envelopePanelBounds = juce::Rectangle<float>(210, 285, 510, 105);
    g.setColour(sectionPanelColour);
    g.fillRoundedRectangle(envelopePanelBounds, cornerRadius);
    g.setColour(sectionBorderColour);
    g.drawRoundedRectangle(envelopePanelBounds, cornerRadius, 0.5f);

    // Filter Envelope section panel (10px gap from envelope panel)
    auto filterEnvPanelBounds = juce::Rectangle<float>(210, 400, 510, 105);
    g.setColour(sectionPanelColour);
    g.fillRoundedRectangle(filterEnvPanelBounds, cornerRadius);
    g.setColour(sectionBorderColour);
    g.drawRoundedRectangle(filterEnvPanelBounds, cornerRadius, 0.5f);

    // Right controls panel (Pre-delay, Width, Mix, toggles, dropdowns)
    auto rightControlsPanelBounds = juce::Rectangle<float>(725, 60, 170, 470);
    g.setColour(sectionPanelColour);
    g.fillRoundedRectangle(rightControlsPanelBounds, cornerRadius);
    g.setColour(sectionBorderColour);
    g.drawRoundedRectangle(rightControlsPanelBounds, cornerRadius, 0.5f);

    // Wet EQ section panel (bottom row of EQ knobs) - taller now that curve is in waveform area
    auto eqPanelBounds = juce::Rectangle<float>(5, 515, static_cast<float>(getWidth() - 10), 175);
    g.setColour(sectionPanelColour);
    g.fillRoundedRectangle(eqPanelBounds, cornerRadius);
    g.setColour(sectionBorderColour);
    g.drawRoundedRectangle(eqPanelBounds, cornerRadius, 0.5f);

    // ========== SECTION LABELS ==========
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff707070));  // Subtle label color

    // Envelope section label - positioned at top-left of panel, above the knobs
    g.drawText("ENVELOPE", envelopePanelBounds.getX() + 8, envelopePanelBounds.getY() + 4, 80, 12, juce::Justification::left);

    // Filter Envelope section label - positioned at top-left of panel
    g.drawText("FILTER ENVELOPE", filterEnvPanelBounds.getX() + 8, filterEnvPanelBounds.getY() + 4, 120, 12, juce::Justification::left);

    // EQ section label - positioned at top of EQ panel
    g.drawText("WET SIGNAL EQ", 55, 520, 120, 15, juce::Justification::left);

    // Note: EQ curve now displayed in waveform area via IR/EQ toggle

    // ========== SEPARATOR LINES ==========
    g.setColour(juce::Colour(0xff3a3a3a));

    // Vertical separator between browser and waveform
    g.drawLine(200, 65, 200, 530, 1.0f);

    // Vertical separator between waveform and controls
    g.drawLine(720, 65, 720, 530, 1.0f);
}

void ConvolutionReverbEditor::resized()
{
    auto bounds = getLocalBounds();

    // A/B buttons in header area
    abToggleButton->setBounds(410, 15, 40, 25);
    abCopyButton->setBounds(455, 15, 50, 25);

    // Skip header
    bounds.removeFromTop(60);

    // Main content area (taller to include filter envelope)
    auto contentBounds = bounds.removeFromTop(475);

    // IR Browser (left panel)
    auto browserBounds = contentBounds.removeFromLeft(195);
    irBrowser->setBounds(browserBounds.reduced(5));

    // Controls panel (right)
    auto controlsBounds = contentBounds.removeFromRight(175);
    controlsBounds.removeFromTop(10);

    int knobSize = 70;
    int labelHeight = 18;
    int spacing = 10;

    int valueHeight = 14;

    // Pre-delay
    auto preDelayArea = controlsBounds.removeFromTop(knobSize + labelHeight + valueHeight);
    preDelayLabel->setBounds(preDelayArea.removeFromTop(labelHeight));
    auto preDelayKnobArea = preDelayArea.removeFromTop(knobSize);
    preDelaySlider->setBounds(preDelayKnobArea.withSizeKeepingCentre(knobSize, knobSize));
    preDelayValueLabel->setBounds(preDelayArea.removeFromTop(valueHeight));

    controlsBounds.removeFromTop(spacing - valueHeight);

    // Width
    auto widthArea = controlsBounds.removeFromTop(knobSize + labelHeight + valueHeight);
    widthLabel->setBounds(widthArea.removeFromTop(labelHeight));
    auto widthKnobArea = widthArea.removeFromTop(knobSize);
    widthSlider->setBounds(widthKnobArea.withSizeKeepingCentre(knobSize, knobSize));
    widthValueLabel->setBounds(widthArea.removeFromTop(valueHeight));

    controlsBounds.removeFromTop(spacing - valueHeight);

    // Mix with Dry/Wet labels
    auto mixArea = controlsBounds.removeFromTop(knobSize + labelHeight + valueHeight + 12);
    mixLabel->setBounds(mixArea.removeFromTop(labelHeight));
    auto mixKnobArea = mixArea.removeFromTop(knobSize);
    mixSlider->setBounds(mixKnobArea.withSizeKeepingCentre(knobSize, knobSize));
    mixValueLabel->setBounds(mixArea.removeFromTop(valueHeight));
    // Dry/Wet labels below the mix value
    auto mixLabelsArea = mixArea.removeFromTop(12);
    mixDryLabel->setBounds(mixLabelsArea.removeFromLeft(mixLabelsArea.getWidth() / 2));
    mixWetLabel->setBounds(mixLabelsArea);

    controlsBounds.removeFromTop(spacing - 12);

    // Zero Latency button and Volume Compensation
    auto toggleRow1 = controlsBounds.removeFromTop(30);
    zeroLatencyButton->setBounds(toggleRow1.withSizeKeepingCentre(90, 28));
    controlsBounds.removeFromTop(5);

    auto toggleRow2 = controlsBounds.removeFromTop(30);
    volumeCompButton->setBounds(toggleRow2.withSizeKeepingCentre(90, 28));
    controlsBounds.removeFromTop(5);

    // Quality dropdown with info label
    auto qualityArea = controlsBounds.removeFromTop(58);
    qualityLabel->setBounds(qualityArea.removeFromTop(labelHeight));
    qualityComboBox->setBounds(qualityArea.removeFromTop(24).withSizeKeepingCentre(110, 24));
    qualityInfoLabel->setBounds(qualityArea.removeFromTop(14));

    controlsBounds.removeFromTop(8);  // Gap between dropdowns

    // Stereo mode dropdown - wider to fit "True Stereo"
    auto stereoArea = controlsBounds.removeFromTop(48);
    stereoModeLabel->setBounds(stereoArea.removeFromTop(labelHeight - 2));  // Slightly shorter label row
    stereoArea.removeFromTop(4);  // Gap between label and dropdown
    stereoModeComboBox->setBounds(stereoArea.removeFromTop(24).withSizeKeepingCentre(130, 24));

    // Center area (waveform and envelope)
    auto centerBounds = contentBounds.reduced(10);

    // IR name label
    auto nameBounds = centerBounds.removeFromTop(20);
    irNameLabel->setBounds(nameBounds);

    centerBounds.removeFromTop(5);

    // Waveform display
    auto waveformBounds = centerBounds.removeFromTop(180);
    waveformDisplay->setBounds(waveformBounds);

    centerBounds.removeFromTop(15);

    // Envelope controls row - offset down to avoid overlap with section label
    auto envelopeBounds = centerBounds.removeFromTop(100);
    int envKnobSize = 55;
    int envValueHeight = 14;
    int envKnobSpacing = (envelopeBounds.getWidth() - 5 * envKnobSize - 50) / 6;  // 5 knobs + reverse button

    int envX = envelopeBounds.getX() + envKnobSpacing;
    int envY = envelopeBounds.getY() + 16;  // Offset down to leave room for "ENVELOPE" section label

    // Attack
    attackLabel->setBounds(envX, envY, envKnobSize, labelHeight);
    attackSlider->setBounds(envX, envY + labelHeight, envKnobSize, envKnobSize);
    attackValueLabel->setBounds(envX, envY + labelHeight + envKnobSize, envKnobSize, envValueHeight);
    envX += envKnobSize + envKnobSpacing;

    // Decay
    decayLabel->setBounds(envX, envY, envKnobSize, labelHeight);
    decaySlider->setBounds(envX, envY + labelHeight, envKnobSize, envKnobSize);
    decayValueLabel->setBounds(envX, envY + labelHeight + envKnobSize, envKnobSize, envValueHeight);
    envX += envKnobSize + envKnobSpacing;

    // Length
    lengthLabel->setBounds(envX, envY, envKnobSize, labelHeight);
    lengthSlider->setBounds(envX, envY + labelHeight, envKnobSize, envKnobSize);
    lengthValueLabel->setBounds(envX, envY + labelHeight + envKnobSize, envKnobSize, envValueHeight);
    envX += envKnobSize + envKnobSpacing;

    // IR Offset
    irOffsetLabel->setBounds(envX, envY, envKnobSize, labelHeight);
    irOffsetSlider->setBounds(envX, envY + labelHeight, envKnobSize, envKnobSize);
    irOffsetValueLabel->setBounds(envX, envY + labelHeight + envKnobSize, envKnobSize, envValueHeight);
    envX += envKnobSize + envKnobSpacing;

    // Reverse button
    reverseButton->setBounds(envX, envY + labelHeight + 10, 50, 30);

    // Filter Envelope Section (with 15px gap from envelope section)
    centerBounds.removeFromTop(15);
    auto filterEnvBounds = centerBounds.removeFromTop(95);
    int filterKnobSize = 50;
    int filterKnobSpacing = (filterEnvBounds.getWidth() - 3 * filterKnobSize - 90) / 5;

    int filterX = filterEnvBounds.getX() + filterKnobSpacing;
    int filterY = filterEnvBounds.getY() + 16;  // Offset down to leave room for "FILTER ENVELOPE" section label

    // Filter Envelope Enable button - adjust position since we added offset
    filterEnvButton->setBounds(filterX, filterY + 10, 90, 30);
    filterX += 90 + filterKnobSpacing;

    // Filter Init Freq
    filterEnvInitLabel->setBounds(filterX, filterY, filterKnobSize, labelHeight);
    filterEnvInitSlider->setBounds(filterX, filterY + labelHeight, filterKnobSize, filterKnobSize);
    filterEnvInitValueLabel->setBounds(filterX, filterY + labelHeight + filterKnobSize, filterKnobSize, envValueHeight);
    filterX += filterKnobSize + filterKnobSpacing;

    // Filter End Freq
    filterEnvEndLabel->setBounds(filterX, filterY, filterKnobSize, labelHeight);
    filterEnvEndSlider->setBounds(filterX, filterY + labelHeight, filterKnobSize, filterKnobSize);
    filterEnvEndValueLabel->setBounds(filterX, filterY + labelHeight + filterKnobSize, filterKnobSize, envValueHeight);
    filterX += filterKnobSize + filterKnobSpacing;

    // Filter Attack
    filterEnvAttackLabel->setBounds(filterX, filterY, filterKnobSize, labelHeight);
    filterEnvAttackSlider->setBounds(filterX, filterY + labelHeight, filterKnobSize, filterKnobSize);
    filterEnvAttackValueLabel->setBounds(filterX, filterY + labelHeight + filterKnobSize, filterKnobSize, envValueHeight);

    // EQ Section (bottom) - more vertical space now that curve is in waveform area
    auto eqBounds = bounds.reduced(10);
    eqBounds.removeFromTop(25); // Space for section label only (curve is now in waveform area)
    eqBounds.removeFromBottom(8); // Bottom padding to prevent clipping

    int eqKnobSize = 50;  // Slightly smaller knobs to fit better
    int eqItemWidth = (eqBounds.getWidth() - 100) / 6; // Leave space for meters

    // Meters on left and right - reduced height to ensure L/R labels fit
    int meterWidth = 35;
    int meterHeight = eqBounds.getHeight() - 25;  // Reduced for proper bottom clearance

    auto leftMeterArea = eqBounds.removeFromLeft(meterWidth + 10);
    leftMeterArea.removeFromTop(5);  // Align meters with knobs
    inputMeterLabel->setBounds(leftMeterArea.removeFromTop(15));
    inputMeter->setBounds(leftMeterArea.removeFromTop(meterHeight).withSizeKeepingCentre(meterWidth, meterHeight));

    auto rightMeterArea = eqBounds.removeFromRight(meterWidth + 10);
    rightMeterArea.removeFromTop(5);  // Align meters with knobs
    outputMeterLabel->setBounds(rightMeterArea.removeFromTop(15));
    outputMeter->setBounds(rightMeterArea.removeFromTop(meterHeight).withSizeKeepingCentre(meterWidth, meterHeight));

    // EQ knobs - positioned below the curve
    int eqY = eqBounds.getY();
    int eqX = eqBounds.getX();
    int eqValueHeight = 14;

    // HPF
    hpfLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    hpfSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    hpfValueLabel->setBounds(eqX, eqY + labelHeight + eqKnobSize, eqItemWidth, eqValueHeight);
    eqX += eqItemWidth;

    // Low (gain only - frequency is fixed at 100Hz)
    eqLowLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    eqLowGainSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    eqLowValueLabel->setBounds(eqX, eqY + labelHeight + eqKnobSize, eqItemWidth, eqValueHeight);
    eqX += eqItemWidth;

    // Lo-Mid (gain only - frequency is fixed at 600Hz)
    eqLowMidLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    eqLowMidGainSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    eqLowMidValueLabel->setBounds(eqX, eqY + labelHeight + eqKnobSize, eqItemWidth, eqValueHeight);
    eqX += eqItemWidth;

    // Hi-Mid (gain only - frequency is fixed at 3kHz)
    eqHighMidLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    eqHighMidGainSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    eqHighMidValueLabel->setBounds(eqX, eqY + labelHeight + eqKnobSize, eqItemWidth, eqValueHeight);
    eqX += eqItemWidth;

    // High (gain only - frequency is fixed at 8kHz)
    eqHighLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    eqHighGainSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    eqHighValueLabel->setBounds(eqX, eqY + labelHeight + eqKnobSize, eqItemWidth, eqValueHeight);
    eqX += eqItemWidth;

    // LPF
    lpfLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    lpfSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    lpfValueLabel->setBounds(eqX, eqY + labelHeight + eqKnobSize, eqItemWidth, eqValueHeight);
}

void ConvolutionReverbEditor::timerCallback()
{
    // Apply any pending IR changes (deferred from audio thread for real-time safety)
    audioProcessor.applyPendingIRChanges();

    // Update meters (stereo L/R)
    float inputLevelL = audioProcessor.getInputLevelL();
    float inputLevelR = audioProcessor.getInputLevelR();
    float outputLevelL = audioProcessor.getOutputLevelL();
    float outputLevelR = audioProcessor.getOutputLevelR();

    // Smooth the meter values
    smoothedInputLevelL = smoothedInputLevelL * 0.8f + inputLevelL * 0.2f;
    smoothedInputLevelR = smoothedInputLevelR * 0.8f + inputLevelR * 0.2f;
    smoothedOutputLevelL = smoothedOutputLevelL * 0.8f + outputLevelL * 0.2f;
    smoothedOutputLevelR = smoothedOutputLevelR * 0.8f + outputLevelR * 0.2f;

    inputMeter->setStereoLevels(smoothedInputLevelL, smoothedInputLevelR);
    outputMeter->setStereoLevels(smoothedOutputLevelL, smoothedOutputLevelR);

    // Check if IR changed
    static juce::String lastIRName;
    juce::String currentIRName = audioProcessor.getCurrentIRName();

    if (currentIRName != lastIRName)
    {
        lastIRName = currentIRName;
        updateWaveformDisplay();
        updateIRNameLabel();
        updateQualityInfo();
    }

    // Update waveform display with current parameters
    waveformDisplay->setIROffset(static_cast<float>(irOffsetSlider->getValue()));
    waveformDisplay->setFilterEnvelope(
        filterEnvButton->getToggleState(),
        static_cast<float>(filterEnvInitSlider->getValue()),
        static_cast<float>(filterEnvEndSlider->getValue()),
        static_cast<float>(filterEnvAttackSlider->getValue())
    );

    // Update EQ parameters for waveform display's EQ curve view
    waveformDisplay->setEQParameters(
        static_cast<float>(hpfSlider->getValue()),
        static_cast<float>(lpfSlider->getValue()),
        static_cast<float>(eqLowGainSlider->getValue()),
        static_cast<float>(eqLowMidGainSlider->getValue()),
        static_cast<float>(eqHighMidGainSlider->getValue()),
        static_cast<float>(eqHighGainSlider->getValue())
    );
}

void ConvolutionReverbEditor::irFileSelected(const juce::File& file)
{
    audioProcessor.loadImpulseResponse(file);
    updateWaveformDisplay();
    updateIRNameLabel();
}

void ConvolutionReverbEditor::updateWaveformDisplay()
{
    if (audioProcessor.isIRLoaded())
    {
        waveformDisplay->setIRWaveform(audioProcessor.getCurrentIRWaveform(),
                                        audioProcessor.getCurrentIRSampleRate());
        updateEnvelopeDisplay();
    }
    else
    {
        waveformDisplay->clearWaveform();
    }
}

void ConvolutionReverbEditor::updateEnvelopeDisplay()
{
    waveformDisplay->setEnvelopeParameters(
        static_cast<float>(attackSlider->getValue()),
        static_cast<float>(decaySlider->getValue()),
        static_cast<float>(lengthSlider->getValue()));
}

void ConvolutionReverbEditor::updateIRNameLabel()
{
    if (audioProcessor.isIRLoaded())
    {
        irNameLabel->setText(audioProcessor.getCurrentIRName(), juce::dontSendNotification);
    }
    else
    {
        irNameLabel->setText("No IR Loaded", juce::dontSendNotification);
    }
}

void ConvolutionReverbEditor::createValueLabel(std::unique_ptr<juce::Label>& label)
{
    label = std::make_unique<juce::Label>();
    label->setFont(juce::Font(10.0f));
    // Full opacity for clear readability of parameter values
    label->setColour(juce::Label::textColourId, lookAndFeel.getAccentColour());
    label->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label.get());
}

juce::String ConvolutionReverbEditor::formatFrequency(float hz)
{
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, 1) + " kHz";
    else
        return juce::String(static_cast<int>(hz)) + " Hz";
}

juce::String ConvolutionReverbEditor::formatGain(float db)
{
    if (db >= 0.0f)
        return "+" + juce::String(db, 1) + " dB";
    else
        return juce::String(db, 1) + " dB";
}

juce::String ConvolutionReverbEditor::formatTime(float ms)
{
    if (ms >= 1000.0f)
        return juce::String(ms / 1000.0f, 2) + " s";
    else
        return juce::String(static_cast<int>(ms)) + " ms";
}

juce::String ConvolutionReverbEditor::formatPercent(float value)
{
    return juce::String(static_cast<int>(value * 100.0f)) + "%";
}

void ConvolutionReverbEditor::updateValueLabels()
{
    // Pre-delay (0-500 ms)
    preDelayValueLabel->setText(formatTime(static_cast<float>(preDelaySlider->getValue())),
                                 juce::dontSendNotification);

    // Width (0-200%, display as percentage)
    float widthPercent = static_cast<float>(widthSlider->getValue()) * 100.0f;
    widthValueLabel->setText(juce::String(static_cast<int>(widthPercent)) + "%",
                              juce::dontSendNotification);

    // Mix (0-100%)
    mixValueLabel->setText(formatPercent(static_cast<float>(mixSlider->getValue())),
                            juce::dontSendNotification);

    // Attack (0-1, display as 0-500ms)
    float attackMs = static_cast<float>(attackSlider->getValue()) * 500.0f;
    attackValueLabel->setText(formatTime(attackMs), juce::dontSendNotification);

    // Decay (0-1, display as percentage)
    decayValueLabel->setText(formatPercent(static_cast<float>(decaySlider->getValue())),
                              juce::dontSendNotification);

    // Length (0-1, display as percentage or seconds based on IR)
    float lengthVal = static_cast<float>(lengthSlider->getValue());
    float irLengthSec = audioProcessor.getCurrentIRLengthSeconds();
    if (irLengthSec > 0.0f)
    {
        float actualLength = lengthVal * irLengthSec;
        lengthValueLabel->setText(juce::String(actualLength, 1) + " s", juce::dontSendNotification);
    }
    else
    {
        lengthValueLabel->setText(formatPercent(lengthVal), juce::dontSendNotification);
    }

    // HPF (20-500 Hz)
    hpfValueLabel->setText(formatFrequency(static_cast<float>(hpfSlider->getValue())),
                            juce::dontSendNotification);

    // LPF (2000-20000 Hz)
    lpfValueLabel->setText(formatFrequency(static_cast<float>(lpfSlider->getValue())),
                            juce::dontSendNotification);

    // EQ gains (-12 to +12 dB)
    eqLowValueLabel->setText(formatGain(static_cast<float>(eqLowGainSlider->getValue())),
                              juce::dontSendNotification);
    eqLowMidValueLabel->setText(formatGain(static_cast<float>(eqLowMidGainSlider->getValue())),
                                 juce::dontSendNotification);
    eqHighMidValueLabel->setText(formatGain(static_cast<float>(eqHighMidGainSlider->getValue())),
                                  juce::dontSendNotification);
    eqHighValueLabel->setText(formatGain(static_cast<float>(eqHighGainSlider->getValue())),
                               juce::dontSendNotification);

    // IR Offset (0-0.5, display as 0-50%)
    float irOffsetVal = static_cast<float>(irOffsetSlider->getValue()) * 100.0f;
    irOffsetValueLabel->setText(juce::String(static_cast<int>(irOffsetVal)) + "%",
                                 juce::dontSendNotification);

    // Filter envelope frequencies
    filterEnvInitValueLabel->setText(formatFrequency(static_cast<float>(filterEnvInitSlider->getValue())),
                                      juce::dontSendNotification);
    filterEnvEndValueLabel->setText(formatFrequency(static_cast<float>(filterEnvEndSlider->getValue())),
                                     juce::dontSendNotification);

    // Filter attack (0-1, display as percentage)
    filterEnvAttackValueLabel->setText(formatPercent(static_cast<float>(filterEnvAttackSlider->getValue())),
                                        juce::dontSendNotification);
}

void ConvolutionReverbEditor::updateQualityInfo()
{
    // Calculate effective sample rate based on quality setting and IR sample rate
    double irSampleRate = audioProcessor.getCurrentIRSampleRate();
    if (irSampleRate <= 0)
        irSampleRate = 48000.0;

    int qualityIndex = qualityComboBox->getSelectedId() - 1;  // 0-3
    double effectiveRate = irSampleRate;

    switch (qualityIndex)
    {
        case 0: effectiveRate = irSampleRate / 4.0; break;  // Lo-Fi
        case 1: effectiveRate = irSampleRate / 2.0; break;  // Low
        case 2:
        case 3:
        default: effectiveRate = irSampleRate; break;       // Medium/High
    }

    // Format as kHz
    juce::String rateText;
    if (effectiveRate >= 1000.0)
        rateText = juce::String(effectiveRate / 1000.0, 1) + " kHz";
    else
        rateText = juce::String(static_cast<int>(effectiveRate)) + " Hz";

    qualityInfoLabel->setText(rateText, juce::dontSendNotification);
}

void ConvolutionReverbEditor::saveCurrentStateToSlot(ParameterState& slot)
{
    slot.values.clear();
    auto& params = audioProcessor.getValueTreeState();

    // Save all parameter values
    for (auto* param : params.processor.getParameters())
    {
        if (auto* rangedParam = dynamic_cast<juce::RangedAudioParameter*>(param))
        {
            slot.values[rangedParam->paramID] = rangedParam->getValue();
        }
    }
}

void ConvolutionReverbEditor::loadStateFromSlot(const ParameterState& slot)
{
    auto& params = audioProcessor.getValueTreeState();

    for (const auto& pair : slot.values)
    {
        if (auto* param = params.getParameter(pair.first))
        {
            param->setValueNotifyingHost(pair.second);
        }
    }
}

void ConvolutionReverbEditor::copyCurrentToOther()
{
    if (isStateB)
    {
        // Currently on B, copy to A
        saveCurrentStateToSlot(stateA);
    }
    else
    {
        // Currently on A, copy to B
        saveCurrentStateToSlot(stateB);
    }
}

void ConvolutionReverbEditor::drawEQCurve(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Get current EQ settings from sliders
    float hpfFreq = static_cast<float>(hpfSlider->getValue());
    float lpfFreq = static_cast<float>(lpfSlider->getValue());
    float lowGain = static_cast<float>(eqLowGainSlider->getValue());
    float lowMidGain = static_cast<float>(eqLowMidGainSlider->getValue());
    float highMidGain = static_cast<float>(eqHighMidGainSlider->getValue());
    float highGain = static_cast<float>(eqHighGainSlider->getValue());

    // Fixed EQ frequencies
    const float lowFreq = 100.0f;
    const float lowMidFreq = 600.0f;
    const float highMidFreq = 3000.0f;
    const float highFreq = 8000.0f;

    // Draw background
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Draw border
    g.setColour(juce::Colour(0xff2a2a2a));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    auto graphBounds = bounds.reduced(4, 6);
    float centreY = graphBounds.getCentreY();
    const float dbScale = 15.0f;  // ±15 dB visible range

    // Helper to convert dB to Y position
    auto dbToY = [&](float db) -> float {
        return centreY - (db / dbScale) * (graphBounds.getHeight() * 0.5f);
    };

    // Draw horizontal grid lines at 0dB, ±6dB
    g.setColour(juce::Colour(0xff2a2a2a));

    // 0dB line (center, slightly brighter)
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawHorizontalLine(static_cast<int>(centreY), graphBounds.getX(), graphBounds.getRight());

    // ±6dB lines
    g.setColour(juce::Colour(0xff282828));
    float y6dB = dbToY(6.0f);
    float yMinus6dB = dbToY(-6.0f);
    g.drawHorizontalLine(static_cast<int>(y6dB), graphBounds.getX(), graphBounds.getRight());
    g.drawHorizontalLine(static_cast<int>(yMinus6dB), graphBounds.getX(), graphBounds.getRight());

    // Draw frequency grid lines (vertical) with labels
    g.setColour(juce::Colour(0xff282828));
    const float freqMarkers[] = { 100.0f, 1000.0f, 10000.0f };
    const char* freqLabels[] = { "100", "1k", "10k" };
    for (int i = 0; i < 3; ++i)
    {
        float freq = freqMarkers[i];
        float normalizedFreq = (std::log10(freq) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        float x = graphBounds.getX() + normalizedFreq * graphBounds.getWidth();
        g.drawVerticalLine(static_cast<int>(x), graphBounds.getY(), graphBounds.getBottom());

        // Draw frequency label at bottom of curve area
        g.setColour(juce::Colour(0xff606060));
        g.setFont(juce::Font(8.0f));
        g.drawText(freqLabels[i], static_cast<int>(x) - 12, static_cast<int>(graphBounds.getBottom()) - 10, 24, 10, juce::Justification::centred);
        g.setColour(juce::Colour(0xff282828));  // Reset for next line
    }

    // Build frequency response path
    juce::Path responsePath;
    const int numPoints = 128;

    // Helper to convert frequency to X position (log scale)
    auto freqToX = [&](float freq) -> float {
        float normalizedFreq = (std::log10(freq) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        return graphBounds.getX() + normalizedFreq * graphBounds.getWidth();
    };

    // Calculate combined EQ response at each frequency
    auto calculateResponse = [&](float freq) -> float {
        float totalGain = 0.0f;

        // HPF response (12dB/oct slope approximation)
        if (hpfFreq > 20.0f && freq < hpfFreq * 4.0f)
        {
            float ratio = freq / hpfFreq;
            if (ratio < 1.0f)
                totalGain -= 12.0f * std::log2(1.0f / ratio);
        }

        // LPF response (12dB/oct slope approximation)
        if (lpfFreq < 20000.0f && freq > lpfFreq / 4.0f)
        {
            float ratio = freq / lpfFreq;
            if (ratio > 1.0f)
                totalGain -= 12.0f * std::log2(ratio);
        }

        // Low shelf (bell approximation centered at lowFreq)
        if (std::abs(lowGain) > 0.1f)
        {
            float octaves = std::log2(freq / lowFreq);
            float bell = std::exp(-octaves * octaves * 0.5f);  // Gaussian-like rolloff
            if (freq < lowFreq)
                totalGain += lowGain * (1.0f - bell * 0.5f);
            else
                totalGain += lowGain * bell;
        }

        // Low-mid peak
        if (std::abs(lowMidGain) > 0.1f)
        {
            float octaves = std::log2(freq / lowMidFreq);
            float bell = std::exp(-octaves * octaves * 2.0f);  // Q=1 approximation
            totalGain += lowMidGain * bell;
        }

        // High-mid peak
        if (std::abs(highMidGain) > 0.1f)
        {
            float octaves = std::log2(freq / highMidFreq);
            float bell = std::exp(-octaves * octaves * 2.0f);
            totalGain += highMidGain * bell;
        }

        // High shelf
        if (std::abs(highGain) > 0.1f)
        {
            float octaves = std::log2(freq / highFreq);
            float bell = std::exp(-octaves * octaves * 0.5f);
            if (freq > highFreq)
                totalGain += highGain * (1.0f - bell * 0.5f);
            else
                totalGain += highGain * bell;
        }

        return juce::jlimit(-dbScale, dbScale, totalGain);
    };

    // Build the path
    bool firstPoint = true;
    for (int i = 0; i < numPoints; ++i)
    {
        float normalizedPos = static_cast<float>(i) / (numPoints - 1);
        float freq = 20.0f * std::pow(1000.0f, normalizedPos);  // 20 Hz to 20 kHz log scale

        float x = freqToX(freq);
        float response = calculateResponse(freq);
        float y = dbToY(response);

        if (firstPoint)
        {
            responsePath.startNewSubPath(x, y);
            firstPoint = false;
        }
        else
        {
            responsePath.lineTo(x, y);
        }
    }

    // Draw filled area under curve
    juce::Path fillPath = responsePath;
    fillPath.lineTo(graphBounds.getRight(), centreY);
    fillPath.lineTo(graphBounds.getX(), centreY);
    fillPath.closeSubPath();

    juce::ColourGradient fillGrad(
        juce::Colour(0x284a9eff), graphBounds.getCentreX(), graphBounds.getY(),
        juce::Colour(0x0c4a9eff), graphBounds.getCentreX(), graphBounds.getBottom(),
        false
    );
    g.setGradientFill(fillGrad);
    g.fillPath(fillPath);

    // Draw glow/shadow behind the curve for depth
    g.setColour(juce::Colour(0x404a9eff));  // Soft blue glow
    g.strokePath(responsePath, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

    // Draw the main response curve - thicker 2px stroke
    g.setColour(juce::Colour(0xff4a9eff));  // Accent blue
    g.strokePath(responsePath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

    // Bright highlight on top of curve
    g.setColour(juce::Colour(0x806abeFF));  // Brighter blue, 50% opacity
    g.strokePath(responsePath, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
}

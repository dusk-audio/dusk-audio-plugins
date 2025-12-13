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
    attackSlider = std::make_unique<juce::Slider>();
    decaySlider = std::make_unique<juce::Slider>();
    lengthSlider = std::make_unique<juce::Slider>();
    attackLabel = std::make_unique<juce::Label>();
    decayLabel = std::make_unique<juce::Label>();
    lengthLabel = std::make_unique<juce::Label>();

    setupSlider(*attackSlider, *attackLabel, "ATTACK");
    setupSlider(*decaySlider, *decayLabel, "DECAY");
    setupSlider(*lengthSlider, *lengthLabel, "LENGTH", "%");

    reverseButton = std::make_unique<juce::ToggleButton>("REV");
    setupToggleButton(*reverseButton, "REV");

    // Main controls
    preDelaySlider = std::make_unique<juce::Slider>();
    widthSlider = std::make_unique<juce::Slider>();
    mixSlider = std::make_unique<juce::Slider>();
    preDelayLabel = std::make_unique<juce::Label>();
    widthLabel = std::make_unique<juce::Label>();
    mixLabel = std::make_unique<juce::Label>();

    setupSlider(*preDelaySlider, *preDelayLabel, "PRE-DELAY", "ms");
    setupSlider(*widthSlider, *widthLabel, "WIDTH");
    setupSlider(*mixSlider, *mixLabel, "MIX", "%");

    // Filter controls
    hpfSlider = std::make_unique<juce::Slider>();
    lpfSlider = std::make_unique<juce::Slider>();
    hpfLabel = std::make_unique<juce::Label>();
    lpfLabel = std::make_unique<juce::Label>();

    setupSlider(*hpfSlider, *hpfLabel, "HPF", "Hz");
    setupSlider(*lpfSlider, *lpfLabel, "LPF", "Hz");

    // EQ controls - simplified to just gain knobs (frequencies are fixed internally)
    eqLowFreqSlider = std::make_unique<juce::Slider>();
    eqLowGainSlider = std::make_unique<juce::Slider>();
    eqLowMidFreqSlider = std::make_unique<juce::Slider>();
    eqLowMidGainSlider = std::make_unique<juce::Slider>();
    eqHighMidFreqSlider = std::make_unique<juce::Slider>();
    eqHighMidGainSlider = std::make_unique<juce::Slider>();
    eqHighFreqSlider = std::make_unique<juce::Slider>();
    eqHighGainSlider = std::make_unique<juce::Slider>();
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

    // IR Offset control
    irOffsetSlider = std::make_unique<juce::Slider>();
    irOffsetLabel = std::make_unique<juce::Label>();
    setupSlider(*irOffsetSlider, *irOffsetLabel, "IR OFFSET", "%");

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

    filterEnvInitSlider = std::make_unique<juce::Slider>();
    filterEnvEndSlider = std::make_unique<juce::Slider>();
    filterEnvAttackSlider = std::make_unique<juce::Slider>();
    filterEnvInitLabel = std::make_unique<juce::Label>();
    filterEnvEndLabel = std::make_unique<juce::Label>();
    filterEnvAttackLabel = std::make_unique<juce::Label>();

    setupSlider(*filterEnvInitSlider, *filterEnvInitLabel, "INIT", "Hz");
    setupSlider(*filterEnvEndSlider, *filterEnvEndLabel, "END", "Hz");
    setupSlider(*filterEnvAttackSlider, *filterEnvAttackLabel, "F.ATK");

    // Meters
    inputMeter = std::make_unique<LEDMeter>();
    outputMeter = std::make_unique<LEDMeter>();
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
    hpfSlider->onValueChange = [this]() { updateValueLabels(); };
    lpfSlider->onValueChange = [this]() { updateValueLabels(); };
    eqLowGainSlider->onValueChange = [this]() { updateValueLabels(); };
    eqLowMidGainSlider->onValueChange = [this]() { updateValueLabels(); };
    eqHighMidGainSlider->onValueChange = [this]() { updateValueLabels(); };
    eqHighGainSlider->onValueChange = [this]() { updateValueLabels(); };
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

    // Section divider
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawLine(0, 55, static_cast<float>(getWidth()), 55, 2.0f);

    // Section labels
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff707070));

    // Envelope section label
    g.drawText("ENVELOPE", 230, 290, 100, 15, juce::Justification::left);

    // Filter Envelope section label
    g.drawText("FILTER ENVELOPE", 230, 400, 140, 15, juce::Justification::left);

    // EQ section label
    g.drawText("WET SIGNAL EQ", 45, 545, 120, 15, juce::Justification::left);

    // Control section separator lines
    g.setColour(juce::Colour(0xff3a3a3a));

    // Horizontal line above EQ section
    g.drawLine(10, 540, static_cast<float>(getWidth() - 10), 540, 1.0f);

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
    auto qualityArea = controlsBounds.removeFromTop(65);
    qualityLabel->setBounds(qualityArea.removeFromTop(labelHeight));
    qualityComboBox->setBounds(qualityArea.removeFromTop(24).withSizeKeepingCentre(100, 24));
    qualityInfoLabel->setBounds(qualityArea.removeFromTop(14));

    controlsBounds.removeFromTop(5);

    // Stereo mode dropdown
    auto stereoArea = controlsBounds.removeFromTop(50);
    stereoModeLabel->setBounds(stereoArea.removeFromTop(labelHeight));
    stereoModeComboBox->setBounds(stereoArea.withSizeKeepingCentre(100, 24));

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

    // Envelope controls row
    auto envelopeBounds = centerBounds.removeFromTop(100);
    int envKnobSize = 55;
    int envValueHeight = 14;
    int envKnobSpacing = (envelopeBounds.getWidth() - 5 * envKnobSize - 50) / 6;  // 5 knobs + reverse button

    int envX = envelopeBounds.getX() + envKnobSpacing;
    int envY = envelopeBounds.getY();

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

    // Filter Envelope Section
    centerBounds.removeFromTop(10);
    auto filterEnvBounds = centerBounds.removeFromTop(100);
    int filterKnobSize = 50;
    int filterKnobSpacing = (filterEnvBounds.getWidth() - 3 * filterKnobSize - 90) / 5;

    int filterX = filterEnvBounds.getX() + filterKnobSpacing;
    int filterY = filterEnvBounds.getY();

    // Filter Envelope Enable button
    filterEnvButton->setBounds(filterX, filterY + 25, 90, 30);
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

    // EQ Section (bottom)
    auto eqBounds = bounds.reduced(10);
    eqBounds.removeFromTop(20); // Section label space

    int eqKnobSize = 55;
    int eqItemWidth = (eqBounds.getWidth() - 100) / 6; // Leave space for meters

    // Meters on left and right
    int meterWidth = 35;
    int meterHeight = eqBounds.getHeight() - 20;

    auto leftMeterArea = eqBounds.removeFromLeft(meterWidth + 10);
    inputMeterLabel->setBounds(leftMeterArea.removeFromTop(15));
    inputMeter->setBounds(leftMeterArea.withSizeKeepingCentre(meterWidth, meterHeight));

    auto rightMeterArea = eqBounds.removeFromRight(meterWidth + 10);
    outputMeterLabel->setBounds(rightMeterArea.removeFromTop(15));
    outputMeter->setBounds(rightMeterArea.withSizeKeepingCentre(meterWidth, meterHeight));

    // EQ knobs
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

    // Update meters
    float inputLevel = audioProcessor.getInputLevel();
    float outputLevel = audioProcessor.getOutputLevel();

    // Smooth the meter values
    smoothedInputLevel = smoothedInputLevel * 0.8f + inputLevel * 0.2f;
    smoothedOutputLevel = smoothedOutputLevel * 0.8f + outputLevel * 0.2f;

    inputMeter->setLevel(smoothedInputLevel);
    outputMeter->setLevel(smoothedOutputLevel);

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

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

    // Add envelope parameter listeners
    attackSlider->onValueChange = [this]() { updateEnvelopeDisplay(); };
    decaySlider->onValueChange = [this]() { updateEnvelopeDisplay(); };
    lengthSlider->onValueChange = [this]() { updateEnvelopeDisplay(); };
    reverseButton->onClick = [this]()
    {
        waveformDisplay->setReversed(reverseButton->getToggleState());
    };

    // Initial waveform update
    updateWaveformDisplay();
    updateIRNameLabel();

    // Set size AFTER all components are created (setSize triggers resized())
    setSize(900, 600);

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

    // Section divider
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawLine(0, 55, static_cast<float>(getWidth()), 55, 2.0f);

    // Section labels
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff707070));

    // Envelope section label
    g.drawText("ENVELOPE", 230, 290, 100, 15, juce::Justification::left);

    // EQ section label
    g.drawText("WET SIGNAL EQ", 45, 445, 120, 15, juce::Justification::left);

    // Control section separator lines
    g.setColour(juce::Colour(0xff3a3a3a));

    // Horizontal line above EQ section
    g.drawLine(10, 440, static_cast<float>(getWidth() - 10), 440, 1.0f);

    // Vertical separator between browser and waveform
    g.drawLine(200, 65, 200, 430, 1.0f);

    // Vertical separator between waveform and controls
    g.drawLine(720, 65, 720, 430, 1.0f);
}

void ConvolutionReverbEditor::resized()
{
    auto bounds = getLocalBounds();

    // Skip header
    bounds.removeFromTop(60);

    // Main content area
    auto contentBounds = bounds.removeFromTop(375);

    // IR Browser (left panel)
    auto browserBounds = contentBounds.removeFromLeft(195);
    irBrowser->setBounds(browserBounds.reduced(5));

    // Controls panel (right)
    auto controlsBounds = contentBounds.removeFromRight(175);
    controlsBounds.removeFromTop(10);

    int knobSize = 70;
    int labelHeight = 18;
    int spacing = 10;

    // Pre-delay
    auto preDelayArea = controlsBounds.removeFromTop(knobSize + labelHeight);
    preDelayLabel->setBounds(preDelayArea.removeFromTop(labelHeight));
    preDelaySlider->setBounds(preDelayArea.withSizeKeepingCentre(knobSize, knobSize));

    controlsBounds.removeFromTop(spacing);

    // Width
    auto widthArea = controlsBounds.removeFromTop(knobSize + labelHeight);
    widthLabel->setBounds(widthArea.removeFromTop(labelHeight));
    widthSlider->setBounds(widthArea.withSizeKeepingCentre(knobSize, knobSize));

    controlsBounds.removeFromTop(spacing);

    // Mix
    auto mixArea = controlsBounds.removeFromTop(knobSize + labelHeight);
    mixLabel->setBounds(mixArea.removeFromTop(labelHeight));
    mixSlider->setBounds(mixArea.withSizeKeepingCentre(knobSize, knobSize));

    controlsBounds.removeFromTop(spacing);

    // Zero Latency button
    auto latencyArea = controlsBounds.removeFromTop(35);
    zeroLatencyButton->setBounds(latencyArea.withSizeKeepingCentre(80, 30));

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
    auto envelopeBounds = centerBounds.removeFromTop(90);
    int envKnobSize = 65;
    int envKnobSpacing = (envelopeBounds.getWidth() - 4 * envKnobSize) / 5;

    int envX = envelopeBounds.getX() + envKnobSpacing;

    // Attack
    attackLabel->setBounds(envX, envelopeBounds.getY(), envKnobSize, labelHeight);
    attackSlider->setBounds(envX, envelopeBounds.getY() + labelHeight, envKnobSize, envKnobSize);
    envX += envKnobSize + envKnobSpacing;

    // Decay
    decayLabel->setBounds(envX, envelopeBounds.getY(), envKnobSize, labelHeight);
    decaySlider->setBounds(envX, envelopeBounds.getY() + labelHeight, envKnobSize, envKnobSize);
    envX += envKnobSize + envKnobSpacing;

    // Length
    lengthLabel->setBounds(envX, envelopeBounds.getY(), envKnobSize, labelHeight);
    lengthSlider->setBounds(envX, envelopeBounds.getY() + labelHeight, envKnobSize, envKnobSize);
    envX += envKnobSize + envKnobSpacing;

    // Reverse button
    reverseButton->setBounds(envX, envelopeBounds.getY() + labelHeight + 15, envKnobSize, 35);

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

    // HPF
    hpfLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    hpfSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    eqX += eqItemWidth;

    // Low (gain only - frequency is fixed at 100Hz)
    eqLowLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    eqLowGainSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    eqX += eqItemWidth;

    // Lo-Mid (gain only - frequency is fixed at 600Hz)
    eqLowMidLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    eqLowMidGainSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    eqX += eqItemWidth;

    // Hi-Mid (gain only - frequency is fixed at 3kHz)
    eqHighMidLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    eqHighMidGainSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    eqX += eqItemWidth;

    // High (gain only - frequency is fixed at 8kHz)
    eqHighLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    eqHighGainSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
    eqX += eqItemWidth;

    // LPF
    lpfLabel->setBounds(eqX, eqY, eqItemWidth, labelHeight);
    lpfSlider->setBounds(eqX + (eqItemWidth - eqKnobSize) / 2, eqY + labelHeight, eqKnobSize, eqKnobSize);
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
    }
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

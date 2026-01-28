#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using namespace TapeMachineColors;

//==============================================================================
// Editor Constructor
//==============================================================================
TapeMachineAudioProcessorEditor::TapeMachineAudioProcessorEditor(TapeMachineAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&tapeMachineLookAndFeel);

    // Setup combo boxes
    setupComboBox(tapeMachineSelector, tapeMachineLabel, "MACHINE");
    tapeMachineSelector.addItem("Swiss 800", 1);
    tapeMachineSelector.addItem("Classic 102", 2);
    tapeMachineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeMachine", tapeMachineSelector);

    setupComboBox(tapeSpeedSelector, tapeSpeedLabel, "SPEED");
    tapeSpeedSelector.addItem("7.5 IPS", 1);
    tapeSpeedSelector.addItem("15 IPS", 2);
    tapeSpeedSelector.addItem("30 IPS", 3);
    tapeSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeSpeed", tapeSpeedSelector);

    setupComboBox(tapeTypeSelector, tapeTypeLabel, "TAPE TYPE");
    tapeTypeSelector.addItem("Type 456", 1);
    tapeTypeSelector.addItem("Type GP9", 2);
    tapeTypeSelector.addItem("Type 911", 3);
    tapeTypeSelector.addItem("Type 250", 4);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "tapeType", tapeTypeSelector);

    setupComboBox(oversamplingSelector, oversamplingLabel, "HQ");
    oversamplingSelector.addItem("1x", 1);
    oversamplingSelector.addItem("2x", 2);
    oversamplingSelector.addItem("4x", 3);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "oversampling", oversamplingSelector);

    setupComboBox(signalPathSelector, signalPathLabel, "SIGNAL PATH");
    signalPathSelector.addItem("Repro", 1);
    signalPathSelector.addItem("Sync", 2);
    signalPathSelector.addItem("Input", 3);
    signalPathSelector.addItem("Thru", 4);
    signalPathSelector.setTooltip("Signal Path\nRepro: Full tape processing (record→tape→playback)\nSync: Record head playback (more HF loss, for overdub sync)\nInput: Electronics only (no tape saturation/wow/flutter)\nThru: Complete bypass");
    signalPathAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "signalPath", signalPathSelector);

    setupComboBox(eqStandardSelector, eqStandardLabel, "EQ STD");
    eqStandardSelector.addItem("NAB", 1);
    eqStandardSelector.addItem("CCIR", 2);
    eqStandardSelector.addItem("AES", 3);
    eqStandardSelector.setTooltip("EQ Standard (Pre/De-emphasis)\nNAB: American (most HF pre-emphasis, warmest saturation)\nCCIR: European (moderate, balanced)\nAES: Modern (minimal, most transparent)");
    eqStandardAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getAPVTS(), "eqStandard", eqStandardSelector);

    // Setup sliders
    setupSlider(inputGainSlider, inputGainLabel, "INPUT");
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "inputGain", inputGainSlider);

    setupSlider(biasSlider, biasLabel, "BIAS");
    biasAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "bias", biasSlider);

    setupSlider(highpassFreqSlider, highpassFreqLabel, "LOW CUT");
    highpassFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "highpassFreq", highpassFreqSlider);

    setupSlider(lowpassFreqSlider, lowpassFreqLabel, "HIGH CUT");
    lowpassFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "lowpassFreq", lowpassFreqSlider);

    setupSlider(mixSlider, mixLabel, "MIX");
    mixSlider.setTooltip("Wet/Dry Mix\n0% = Dry, 100% = Fully processed");
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "mix", mixSlider);

    setupSlider(wowSlider, wowLabel, "WOW");
    wowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "wowAmount", wowSlider);

    setupSlider(flutterSlider, flutterLabel, "FLUTTER");
    flutterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "flutterAmount", flutterSlider);

    setupSlider(outputGainSlider, outputGainLabel, "OUTPUT");
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "outputGain", outputGainSlider);

    // Noise button
    noiseEnabledButton.setButtonText("OFF");
    noiseEnabledButton.setClickingTogglesState(true);
    noiseEnabledButton.setTooltip("Tape Noise Enable\nAdds authentic tape hiss");
    noiseEnabledButton.onStateChange = [this]() {
        noiseEnabledButton.setButtonText(noiseEnabledButton.getToggleState() ? "ON" : "OFF");
    };
    addAndMakeVisible(noiseEnabledButton);
    noiseEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "noiseEnabled", noiseEnabledButton);

    noiseLabel.setText("NOISE", juce::dontSendNotification);
    noiseLabel.setJustificationType(juce::Justification::centred);
    noiseLabel.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    noiseLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(noiseLabel);

    // Auto-comp button (Link)
    autoCompButton.setButtonText("LINK");
    autoCompButton.setClickingTogglesState(true);
    autoCompButton.setTooltip("Input/Output Link\nWhen ON: Output = -Input for unity gain");
    addAndMakeVisible(autoCompButton);
    autoCompAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "autoComp", autoCompButton);

    autoCompLabel.setText("AUTO COMP", juce::dontSendNotification);
    autoCompLabel.setJustificationType(juce::Justification::centred);
    autoCompLabel.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    autoCompLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(autoCompLabel);

    // Auto-cal button
    autoCalButton.setButtonText("AUTO CAL");
    autoCalButton.setClickingTogglesState(true);
    autoCalButton.setTooltip("Auto Calibration\nWhen ON: Automatically sets optimal bias");
    addAndMakeVisible(autoCalButton);
    autoCalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "autoCal", autoCalButton);

    autoCalLabel.setText("AUTO CAL", juce::dontSendNotification);
    autoCalLabel.setJustificationType(juce::Justification::centred);
    autoCalLabel.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    autoCalLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(autoCalLabel);

    // Reels
    addAndMakeVisible(leftReel);
    addAndMakeVisible(rightReel);
    leftReel.setIsSupplyReel(true);
    rightReel.setIsSupplyReel(false);
    leftReel.setTapeAmount(0.5f);
    rightReel.setTapeAmount(0.5f);
    leftReel.setSpeed(1.5f);
    rightReel.setSpeed(1.5f);

    // VU Meter
    addAndMakeVisible(mainVUMeter);

    // Initialize gain tracking
    if (auto* inputParam = audioProcessor.getAPVTS().getRawParameterValue("inputGain"))
        lastInputGainValue = inputParam->load();
    if (auto* outputParam = audioProcessor.getAPVTS().getRawParameterValue("outputGain"))
        lastOutputGainValue = outputParam->load();

    startTimerHz(30);

    setSize(800, 580);

    // Initialize scalable resize helper (replaces manual setResizable/setResizeLimits)
    // Base size: 800x580, Min: 640x464 (80%), Max: 1200x870 (150%)
    resizeHelper.initialize(this, 800, 580, 640, 464, 1200, 870);
}

TapeMachineAudioProcessorEditor::~TapeMachineAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void TapeMachineAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(textPrimary));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(panelDark));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(metalDark));

    // Professional knob behavior from shared Luna settings
    LunaSliderStyle::configureKnob(slider);

    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    label.setFont(juce::Font(12.0f, juce::Font::bold));
    label.attachToComponent(&slider, false);
    addAndMakeVisible(label);

    // Tooltips
    if (text == "INPUT")
        slider.setTooltip("Input Gain (-12 to +12 dB)\nDrives tape saturation");
    else if (text == "OUTPUT")
        slider.setTooltip("Output Gain (-12 to +12 dB)\nFinal level control");
    else if (text == "BIAS")
        slider.setTooltip("Tape Bias (0-100%)\nControls harmonic character");
    else if (text == "LOW CUT")
        slider.setTooltip("High-Pass Filter (20-500 Hz)");
    else if (text == "HIGH CUT")
        slider.setTooltip("Low-Pass Filter (3-20 kHz)");
    else if (text == "WOW")
        slider.setTooltip("Wow Amount (0-100%)\nSlow pitch drift");
    else if (text == "FLUTTER")
        slider.setTooltip("Flutter Amount (0-100%)\nFast pitch modulation");
}

void TapeMachineAudioProcessorEditor::setupComboBox(juce::ComboBox& combo, juce::Label& label, const juce::String& text)
{
    combo.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(combo);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    label.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(label);

    if (text == "MACHINE")
        combo.setTooltip("Tape Machine Model\nSwiss 800: Clean, precise\nClassic 102: Warm, punchy");
    else if (text == "SPEED")
        combo.setTooltip("Tape Speed\n7.5 IPS: More warmth\n15 IPS: Balanced\n30 IPS: Extended HF");
    else if (text == "TAPE TYPE")
        combo.setTooltip("Tape Formulation\nType 456: Classic warm\nType GP9: Modern\nType 911: German precision\nType 250: Vintage 70s");
    else if (text == "HQ")
        combo.setTooltip("Oversampling Quality\n2x: Good quality\n4x: Best anti-aliasing");
}

void TapeMachineAudioProcessorEditor::drawPanelBackground(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Main background
    g.fillAll(juce::Colour(background));

    // Subtle brushed texture
    juce::Random rng(42);
    g.setColour(juce::Colour(0x06000000));
    for (int y = 0; y < bounds.getHeight(); y += 3)
    {
        if (rng.nextFloat() < 0.6f)
            g.drawHorizontalLine(y, 0.0f, (float)bounds.getWidth());
    }

    // Subtle vignette
    juce::ColourGradient vignette(
        juce::Colour(0x00000000), bounds.getCentreX(), bounds.getCentreY(),
        juce::Colour(0x30000000), 0, 0,
        true);
    g.setGradientFill(vignette);
    g.fillRect(bounds);
}

void TapeMachineAudioProcessorEditor::drawVintageTexture(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Add subtle grain texture
    juce::Random rng(123);
    g.setColour(juce::Colour(0x08000000));
    for (int i = 0; i < 30; ++i)
    {
        float x = area.getX() + rng.nextFloat() * area.getWidth();
        float y = area.getY() + rng.nextFloat() * area.getHeight();
        g.fillEllipse(x, y, 1.5f, 1.5f);
    }
}

void TapeMachineAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Premium background with vintage texture
    drawPanelBackground(g);

    auto bounds = getLocalBounds();

    // Header area with nameplate - scaled
    auto headerArea = bounds.removeFromTop(resizeHelper.scaled(50));
    {
        // Header background
        g.setColour(juce::Colour(panelDark));
        g.fillRect(headerArea);

        // Nameplate - scaled
        auto nameplateArea = juce::Rectangle<float>(resizeHelper.scaled(10.0f), resizeHelper.scaled(8.0f),
                                                     resizeHelper.scaled(200.0f), resizeHelper.scaled(32.0f));
        TapeMachineLookAndFeel::drawNameplate(g, nameplateArea, "TapeMachine", resizeHelper.scaled(20.0f));

        // Subtitle - scaled
        g.setFont(juce::Font(resizeHelper.scaled(11.0f), juce::Font::italic));
        g.setColour(juce::Colour(textSecondary));
        g.drawText("Vintage Tape Emulation", resizeHelper.scaled(220), resizeHelper.scaled(14),
                   resizeHelper.scaled(200), resizeHelper.scaled(20), juce::Justification::centredLeft);

        // Set clickable area for supporters - scaled
        titleClickArea = juce::Rectangle<int>(resizeHelper.scaled(10), resizeHelper.scaled(8),
                                               resizeHelper.scaled(200), resizeHelper.scaled(32));

        // Separator line
        g.setColour(juce::Colour(metalDark));
        g.drawHorizontalLine(headerArea.getBottom() - 1, 0, (float)getWidth());
    }

    // Transport section (reels + VU + selectors) - scaled
    auto transportArea = bounds.removeFromTop(resizeHelper.scaled(235));
    transportArea.reduce(resizeHelper.scaled(12), resizeHelper.scaled(6));
    TapeMachineLookAndFeel::drawBeveledPanel(g, transportArea.toFloat(), resizeHelper.scaled(6.0f), resizeHelper.scaled(2.0f));
    drawVintageTexture(g, transportArea);

    // Main controls section - scaled
    bounds.removeFromTop(resizeHelper.scaled(6));
    auto mainControlsArea = bounds.removeFromTop(resizeHelper.scaled(120));
    mainControlsArea.reduce(resizeHelper.scaled(12), resizeHelper.scaled(4));
    TapeMachineLookAndFeel::drawBeveledPanel(g, mainControlsArea.toFloat(), resizeHelper.scaled(6.0f), resizeHelper.scaled(2.0f));
    drawVintageTexture(g, mainControlsArea);

    // Character controls section - scaled
    bounds.removeFromTop(resizeHelper.scaled(6));
    auto characterArea = bounds.removeFromTop(resizeHelper.scaled(120));
    characterArea.reduce(resizeHelper.scaled(12), resizeHelper.scaled(4));
    TapeMachineLookAndFeel::drawBeveledPanel(g, characterArea.toFloat(), resizeHelper.scaled(6.0f), resizeHelper.scaled(2.0f));
    drawVintageTexture(g, characterArea);

    // Footer with company name - scaled
    g.setFont(juce::Font(resizeHelper.scaled(10.0f), juce::Font::italic));
    g.setColour(juce::Colour(textSecondary).withAlpha(0.6f));
    g.drawText("Luna Co. Audio", getLocalBounds().removeFromBottom(resizeHelper.scaled(16)),
               juce::Justification::centred);
}

void TapeMachineAudioProcessorEditor::resized()
{
    // Update the resize helper (positions corner handle and calculates scale)
    resizeHelper.updateResizer();

    auto area = getLocalBounds();

    // Header - scaled
    area.removeFromTop(resizeHelper.scaled(50));

    // Transport section - scaled
    auto transportArea = area.removeFromTop(resizeHelper.scaled(235));
    transportArea.reduce(resizeHelper.scaled(15), resizeHelper.scaled(8));

    // Reels - scaled
    int reelSize = resizeHelper.scaled(120);
    leftReel.setBounds(transportArea.removeFromLeft(reelSize).reduced(resizeHelper.scaled(5)));
    rightReel.setBounds(transportArea.removeFromRight(reelSize).reduced(resizeHelper.scaled(5)));

    // VU meter - scaled
    transportArea.removeFromTop(resizeHelper.scaled(8));
    auto meterArea = transportArea.removeFromTop(resizeHelper.scaled(120));
    mainVUMeter.setBounds(meterArea.reduced(resizeHelper.scaled(5), resizeHelper.scaled(2)));

    // Selector row 1 - scaled
    transportArea.removeFromTop(resizeHelper.scaled(4));
    auto labelArea1 = transportArea.removeFromTop(resizeHelper.scaled(14));
    auto selectorWidth = labelArea1.getWidth() / 3;

    tapeMachineLabel.setBounds(labelArea1.removeFromLeft(selectorWidth).reduced(resizeHelper.scaled(4), 0));
    tapeSpeedLabel.setBounds(labelArea1.removeFromLeft(selectorWidth).reduced(resizeHelper.scaled(4), 0));
    tapeTypeLabel.setBounds(labelArea1.reduced(resizeHelper.scaled(4), 0));

    transportArea.removeFromTop(resizeHelper.scaled(2));
    auto selectorArea1 = transportArea.removeFromTop(resizeHelper.scaled(28));
    selectorWidth = selectorArea1.getWidth() / 3;

    tapeMachineSelector.setBounds(selectorArea1.removeFromLeft(selectorWidth).reduced(resizeHelper.scaled(4), resizeHelper.scaled(2)));
    tapeSpeedSelector.setBounds(selectorArea1.removeFromLeft(selectorWidth).reduced(resizeHelper.scaled(4), resizeHelper.scaled(2)));
    tapeTypeSelector.setBounds(selectorArea1.reduced(resizeHelper.scaled(4), resizeHelper.scaled(2)));

    // Selector row 2 - scaled
    transportArea.removeFromTop(resizeHelper.scaled(4));
    auto labelArea2 = transportArea.removeFromTop(resizeHelper.scaled(14));
    selectorWidth = labelArea2.getWidth() / 3;

    signalPathLabel.setBounds(labelArea2.removeFromLeft(selectorWidth).reduced(resizeHelper.scaled(4), 0));
    eqStandardLabel.setBounds(labelArea2.removeFromLeft(selectorWidth).reduced(resizeHelper.scaled(4), 0));
    oversamplingLabel.setBounds(labelArea2.reduced(resizeHelper.scaled(4), 0));

    transportArea.removeFromTop(resizeHelper.scaled(2));
    auto selectorArea2 = transportArea.removeFromTop(resizeHelper.scaled(28));
    selectorWidth = selectorArea2.getWidth() / 3;

    signalPathSelector.setBounds(selectorArea2.removeFromLeft(selectorWidth).reduced(resizeHelper.scaled(4), resizeHelper.scaled(2)));
    eqStandardSelector.setBounds(selectorArea2.removeFromLeft(selectorWidth).reduced(resizeHelper.scaled(4), resizeHelper.scaled(2)));
    oversamplingSelector.setBounds(selectorArea2.reduced(resizeHelper.scaled(4), resizeHelper.scaled(2)));

    area.removeFromTop(resizeHelper.scaled(6));

    // Main controls - scaled
    auto mainControlsArea = area.removeFromTop(resizeHelper.scaled(120));
    mainControlsArea.reduce(resizeHelper.scaled(15), resizeHelper.scaled(5));
    mainControlsArea.removeFromTop(resizeHelper.scaled(18));

    int knobSize = resizeHelper.scaled(80);
    int mainSpacing = (mainControlsArea.getWidth() - (knobSize * 5)) / 6;

    mainControlsArea.removeFromLeft(mainSpacing);
    inputGainSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    biasSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    wowSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    flutterSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));
    mainControlsArea.removeFromLeft(mainSpacing);
    outputGainSlider.setBounds(mainControlsArea.removeFromLeft(knobSize).withHeight(knobSize));

    area.removeFromTop(resizeHelper.scaled(6));

    // Character controls - scaled
    auto characterArea = area.removeFromTop(resizeHelper.scaled(120));
    characterArea.reduce(resizeHelper.scaled(15), resizeHelper.scaled(5));
    characterArea.removeFromTop(resizeHelper.scaled(18));

    int buttonAreaWidth = resizeHelper.scaled(280);
    int charSpacing = (characterArea.getWidth() - (knobSize * 3) - buttonAreaWidth) / 7;

    characterArea.removeFromLeft(charSpacing);
    highpassFreqSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);
    lowpassFreqSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);
    mixSlider.setBounds(characterArea.removeFromLeft(knobSize).withHeight(knobSize));
    characterArea.removeFromLeft(charSpacing);

    // Noise switch - scaled
    auto noiseButtonArea = characterArea.removeFromLeft(resizeHelper.scaled(80));
    auto noiseLabelArea = noiseButtonArea.removeFromTop(resizeHelper.scaled(16));
    noiseLabel.setBounds(noiseLabelArea);
    noiseEnabledButton.setBounds(noiseButtonArea.withSizeKeepingCentre(resizeHelper.scaled(60), resizeHelper.scaled(55)));
    characterArea.removeFromLeft(charSpacing);

    // Link button - scaled
    auto autoCompButtonArea = characterArea.removeFromLeft(resizeHelper.scaled(100));
    auto autoCompLabelArea = autoCompButtonArea.removeFromTop(resizeHelper.scaled(16));
    autoCompLabel.setBounds(autoCompLabelArea);
    autoCompButton.setBounds(autoCompButtonArea.withSizeKeepingCentre(resizeHelper.scaled(90), resizeHelper.scaled(38)));
    characterArea.removeFromLeft(charSpacing);

    // Auto cal button - scaled
    auto autoCalButtonArea = characterArea.removeFromLeft(resizeHelper.scaled(100));
    auto autoCalLabelArea = autoCalButtonArea.removeFromTop(resizeHelper.scaled(16));
    autoCalLabel.setBounds(autoCalLabelArea);
    autoCalButton.setBounds(autoCalButtonArea.withSizeKeepingCentre(resizeHelper.scaled(100), resizeHelper.scaled(38)));

    // Supporters overlay
    if (supportersOverlay)
        supportersOverlay->setBounds(getLocalBounds());
}

void TapeMachineAudioProcessorEditor::timerCallback()
{
    // Update VU meter stereo mode
    bool isStereo = !audioProcessor.isMonoTrack();
    if (mainVUMeter.isStereoMode() != isStereo)
        mainVUMeter.setStereoMode(isStereo);

    // Update VU levels
    float inputL = audioProcessor.getInputLevelL();
    float inputR = audioProcessor.getInputLevelR();
    mainVUMeter.setLevels(inputL, inputR);

    // Auto-comp bidirectional linking
    auto* autoCompParam = audioProcessor.getAPVTS().getRawParameterValue("autoComp");
    auto* inputGainParam = audioProcessor.getAPVTS().getRawParameterValue("inputGain");
    auto* outputGainParam = audioProcessor.getAPVTS().getRawParameterValue("outputGain");
    bool autoCompEnabled = autoCompParam && autoCompParam->load() > 0.5f;

    if (autoCompEnabled && inputGainParam && outputGainParam && !isUpdatingGainSliders)
    {
        isUpdatingGainSliders = true;

        float currentInputGainDB = inputGainParam->load();
        float currentOutputGainDB = outputGainParam->load();

        bool inputChanged = std::abs(currentInputGainDB - lastInputGainValue) > 0.01f;
        bool outputChanged = std::abs(currentOutputGainDB - lastOutputGainValue) > 0.01f;

        if (inputChanged && !outputChanged)
        {
            float compensatedOutputDB = juce::jlimit(-12.0f, 12.0f, -currentInputGainDB);
            if (auto* param = audioProcessor.getAPVTS().getParameter("outputGain"))
                param->setValueNotifyingHost(param->convertTo0to1(compensatedOutputDB));
            lastOutputGainValue = compensatedOutputDB;
        }
        else if (outputChanged && !inputChanged)
        {
            float compensatedInputDB = juce::jlimit(-12.0f, 12.0f, -currentOutputGainDB);
            if (auto* param = audioProcessor.getAPVTS().getParameter("inputGain"))
                param->setValueNotifyingHost(param->convertTo0to1(compensatedInputDB));
            lastInputGainValue = compensatedInputDB;
        }

        lastInputGainValue = inputGainParam->load();
        lastOutputGainValue = outputGainParam->load();
        isUpdatingGainSliders = false;
    }
    else if (!autoCompEnabled)
    {
        if (inputGainParam) lastInputGainValue = inputGainParam->load();
        if (outputGainParam) lastOutputGainValue = outputGainParam->load();
    }

    // Gray out bias when auto-cal is enabled
    auto* autoCalParam = audioProcessor.getAPVTS().getRawParameterValue("autoCal");
    bool autoCalEnabled = autoCalParam && autoCalParam->load() > 0.5f;
    biasSlider.setEnabled(!autoCalEnabled);
    biasSlider.setAlpha(autoCalEnabled ? 0.5f : 1.0f);

    // Reel animation
    bool isPlaying = audioProcessor.isProcessing();
    auto* wowParam = audioProcessor.getAPVTS().getRawParameterValue("wowAmount");
    float wowAmount = wowParam ? wowParam->load() : 0.0f;

    auto* tapeSpeedParam = audioProcessor.getAPVTS().getRawParameterValue("tapeSpeed");
    int tapeSpeedIndex = tapeSpeedParam ? static_cast<int>(tapeSpeedParam->load()) : 1;
    float speedMultiplier = (tapeSpeedIndex == 0) ? 1.0f : (tapeSpeedIndex == 1) ? 1.5f : 2.0f;

    float baseSpeed = isPlaying ? speedMultiplier : 0.0f;
    float wowWobble = 0.0f;
    if (isPlaying && wowAmount > 0.0f)
    {
        wowPhase += 0.02f;
        if (wowPhase > juce::MathConstants<float>::twoPi)
            wowPhase -= juce::MathConstants<float>::twoPi;
        wowWobble = std::sin(wowPhase) * (wowAmount * 0.003f);
    }

    float speed = baseSpeed + wowWobble;
    leftReel.setSpeed(speed);
    rightReel.setSpeed(speed);

    // Tape transfer animation
    if (isPlaying)
    {
        const float transferRate = 0.0001f;
        float supplyTape = leftReel.getTapeAmount();
        float takeupTape = rightReel.getTapeAmount();

        if (supplyTape > 0.3f)
        {
            supplyTape -= transferRate;
            takeupTape += transferRate;
            leftReel.setTapeAmount(supplyTape);
            rightReel.setTapeAmount(takeupTape);
        }
        else
        {
            leftReel.setTapeAmount(0.5f);
            rightReel.setTapeAmount(0.5f);
        }
    }
}

void TapeMachineAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
        showSupportersPanel();
}

void TapeMachineAudioProcessorEditor::showSupportersPanel()
{
    if (!supportersOverlay)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>("TapeMachine", JucePlugin_VersionString);
        supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
        addAndMakeVisible(supportersOverlay.get());
    }
    supportersOverlay->setBounds(getLocalBounds());
    supportersOverlay->toFront(true);
    supportersOverlay->setVisible(true);
}

void TapeMachineAudioProcessorEditor::hideSupportersPanel()
{
    if (supportersOverlay)
        supportersOverlay->setVisible(false);
}

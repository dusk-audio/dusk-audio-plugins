/*
  ==============================================================================

    PluginEditor.cpp
    Tape Echo - RE-201 Style Plugin UI

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#include "PluginEditor.h"

TapeEchoEditor::TapeEchoEditor(TapeEchoProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Tape visualization
    addAndMakeVisible(tapeViz);

    // Level meters (stereo LED meters matching Multi-Q/Multi-Comp style)
    inputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    inputMeter->setStereoMode(true);
    inputMeter->setRefreshRate(30.0f);
    addAndMakeVisible(inputMeter.get());

    outputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    outputMeter->setStereoMode(true);
    outputMeter->setRefreshRate(30.0f);
    addAndMakeVisible(outputMeter.get());

    // Setup main knobs
    setupKnob(inputKnob, inputLabel, "INPUT");
    setupKnob(repeatRateKnob, repeatRateLabel, "REPEAT");
    setupKnob(intensityKnob, intensityLabel, "INTENSITY");
    setupKnob(echoVolumeKnob, echoVolumeLabel, "ECHO");
    setupKnob(reverbVolumeKnob, reverbVolumeLabel, "REVERB");

    // Setup secondary knobs
    setupKnob(bassKnob, bassLabel, "BASS");
    setupKnob(trebleKnob, trebleLabel, "TREBLE");
    setupKnob(wowFlutterKnob, wowFlutterLabel, "W/F");
    setupKnob(dryWetKnob, dryWetLabel, "DRY/WET");

    // Setup tempo sync controls
    tempoSyncButton.setColour(juce::ToggleButton::textColourId, TapeEchoLookAndFeel::textColor);
    tempoSyncButton.setColour(juce::ToggleButton::tickColourId, TapeEchoLookAndFeel::accentColor);
    addAndMakeVisible(tempoSyncButton);

    // Note division dial - rotary with stepped values
    noteDivisionKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    noteDivisionKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 15);
    noteDivisionKnob.setRange(0.0, 14.0, 1.0);  // 15 divisions, stepped
    addAndMakeVisible(noteDivisionKnob);

    noteDivisionLabel.setText("NOTE", juce::dontSendNotification);
    noteDivisionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noteDivisionLabel);

    // Create mode buttons
    for (int i = 0; i < 12; ++i)
    {
        modeButtons[i] = std::make_unique<juce::TextButton>(juce::String(i + 1));
        modeButtons[i]->setClickingTogglesState(true);
        modeButtons[i]->setRadioGroupId(1);
        modeButtons[i]->onClick = [this, i]()
        {
            processor.getAPVTS().getParameter("mode")->setValueNotifyingHost(
                static_cast<float>(i) / 11.0f);
        };
        addAndMakeVisible(modeButtons[i].get());
    }

    // Create attachments
    auto& apvts = processor.getAPVTS();
    inputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "inputGain", inputKnob);
    repeatRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "repeatRate", repeatRateKnob);
    intensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "intensity", intensityKnob);
    echoVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "echoVolume", echoVolumeKnob);
    reverbVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "reverbVolume", reverbVolumeKnob);
    bassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "bass", bassKnob);
    trebleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "treble", trebleKnob);
    wowFlutterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "wowFlutter", wowFlutterKnob);
    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "dryWet", dryWetKnob);
    tempoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "tempoSync", tempoSyncButton);
    noteDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "noteDivision", noteDivisionKnob);

    // Timer for UI updates
    startTimerHz(30);

    setSize(600, 450);
}

TapeEchoEditor::~TapeEchoEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void TapeEchoEditor::paint(juce::Graphics& g)
{
    // Background gradient
    juce::ColourGradient bgGradient(
        TapeEchoLookAndFeel::primaryColor, 0, 0,
        TapeEchoLookAndFeel::primaryColor.darker(0.3f), 0, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGradient);
    g.fillAll();

    // Header
    g.setColour(TapeEchoLookAndFeel::darkBgColor);
    g.fillRect(0, 0, getWidth(), 45);

    // Title
    g.setColour(TapeEchoLookAndFeel::textColor);
    g.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    g.drawText("LUNA CO. AUDIO", 15, 10, 150, 25, juce::Justification::centredLeft, true);

    g.setFont(juce::FontOptions(18.0f).withStyle("Bold"));
    g.drawText("TAPE ECHO", getWidth() - 165, 10, 150, 25, juce::Justification::centredRight, true);

    // Section labels - positioned above each control section
    // Bottom section starts at y=290 (50 header + 120 viz + 120 knobs)
    const int labelY = 292;
    g.setColour(TapeEchoLookAndFeel::textColor.withAlpha(0.6f));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("MODE SELECT", 20, labelY, 100, 15, juce::Justification::centredLeft, true);
    g.drawText("SYNC", 215, labelY, 80, 15, juce::Justification::centred, true);
    g.drawText("TONE / MIX", getWidth() - 265, labelY, 100, 15, juce::Justification::centredLeft, true);

    // Draw meter labels (INPUT / OUTPUT) like Multi-Q/Multi-Comp
    if (inputMeter)
    {
        float inL = processor.getInputLevelL();
        float inR = processor.getInputLevelR();
        float inputLevel = juce::jmax(inL, inR);
        float inputDb = inputLevel > 0.0f ? juce::Decibels::gainToDecibels(inputLevel, -60.0f) : -60.0f;
        LEDMeterStyle::drawMeterLabels(g, inputMeter->getBounds(), "INPUT", inputDb);
    }

    if (outputMeter)
    {
        float outL = processor.getOutputLevelL();
        float outR = processor.getOutputLevelR();
        float outputLevel = juce::jmax(outL, outR);
        float outputDb = outputLevel > 0.0f ? juce::Decibels::gainToDecibels(outputLevel, -60.0f) : -60.0f;
        LEDMeterStyle::drawMeterLabels(g, outputMeter->getBounds(), "OUTPUT", outputDb);
    }

    // Bypass overlay when plugin is bypassed
    if (processor.getBypassParameter() != nullptr && processor.getBypassParameter()->getValue() > 0.5f)
    {
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillAll();
        g.setColour(TapeEchoLookAndFeel::textColor);
        g.setFont(juce::FontOptions(24.0f).withStyle("Bold"));
        g.drawText("BYPASSED", getLocalBounds(), juce::Justification::centred);
    }
}

void TapeEchoEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header area
    bounds.removeFromTop(50);

    // Tape visualization (top portion)
    auto vizArea = bounds.removeFromTop(120);
    tapeViz.setBounds(vizArea.reduced(15, 5));

    // Main knobs row
    auto mainKnobArea = bounds.removeFromTop(120);
    const int mainKnobWidth = mainKnobArea.getWidth() / 5;

    auto inputArea = mainKnobArea.removeFromLeft(mainKnobWidth);
    inputKnob.setBounds(inputArea.reduced(10, 0).removeFromTop(80).withTrimmedTop(10));
    inputLabel.setBounds(inputArea.removeFromBottom(20));

    auto repeatArea = mainKnobArea.removeFromLeft(mainKnobWidth);
    repeatRateKnob.setBounds(repeatArea.reduced(10, 0).removeFromTop(80).withTrimmedTop(10));
    repeatRateLabel.setBounds(repeatArea.removeFromBottom(20));

    auto intensityArea = mainKnobArea.removeFromLeft(mainKnobWidth);
    intensityKnob.setBounds(intensityArea.reduced(10, 0).removeFromTop(80).withTrimmedTop(10));
    intensityLabel.setBounds(intensityArea.removeFromBottom(20));

    auto echoArea = mainKnobArea.removeFromLeft(mainKnobWidth);
    echoVolumeKnob.setBounds(echoArea.reduced(10, 0).removeFromTop(80).withTrimmedTop(10));
    echoVolumeLabel.setBounds(echoArea.removeFromBottom(20));

    auto reverbArea = mainKnobArea;
    reverbVolumeKnob.setBounds(reverbArea.reduced(10, 0).removeFromTop(80).withTrimmedTop(10));
    reverbVolumeLabel.setBounds(reverbArea.removeFromBottom(20));

    // Bottom section
    auto bottomArea = bounds.removeFromTop(140);
    bottomArea.removeFromTop(20);  // Space for section labels

    // Mode selector (left side) - 4x3 grid
    auto modeArea = bottomArea.removeFromLeft(200).reduced(15, 5);
    const int buttonWidth = modeArea.getWidth() / 4;
    const int buttonHeight = modeArea.getHeight() / 3;

    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            int index = row * 4 + col;
            modeButtons[index]->setBounds(
                modeArea.getX() + col * buttonWidth + 2,
                modeArea.getY() + row * buttonHeight + 2,
                buttonWidth - 4,
                buttonHeight - 4);
        }
    }

    // Tempo sync controls (middle) - button and dial
    auto syncArea = bottomArea.removeFromLeft(100).reduced(5, 5);
    tempoSyncButton.setBounds(syncArea.removeFromTop(30).reduced(5, 0));
    auto noteKnobArea = syncArea.removeFromTop(60);
    noteDivisionKnob.setBounds(noteKnobArea.reduced(5, 0));
    noteDivisionLabel.setBounds(syncArea.removeFromTop(18));

    // Secondary knobs (right side) - 2x2 grid
    auto secondaryArea = bottomArea.removeFromRight(250).reduced(15, 5);
    const int secKnobWidth = secondaryArea.getWidth() / 2;
    const int secKnobHeight = secondaryArea.getHeight() / 2;

    // Top row
    auto bassArea = juce::Rectangle<int>(
        secondaryArea.getX(), secondaryArea.getY(), secKnobWidth, secKnobHeight);
    bassKnob.setBounds(bassArea.reduced(5).removeFromTop(50));
    bassLabel.setBounds(bassArea.removeFromBottom(18));

    auto trebleArea = juce::Rectangle<int>(
        secondaryArea.getX() + secKnobWidth, secondaryArea.getY(), secKnobWidth, secKnobHeight);
    trebleKnob.setBounds(trebleArea.reduced(5).removeFromTop(50));
    trebleLabel.setBounds(trebleArea.removeFromBottom(18));

    // Bottom row
    auto wfArea = juce::Rectangle<int>(
        secondaryArea.getX(), secondaryArea.getY() + secKnobHeight, secKnobWidth, secKnobHeight);
    wowFlutterKnob.setBounds(wfArea.reduced(5).removeFromTop(50));
    wowFlutterLabel.setBounds(wfArea.removeFromBottom(18));

    auto mixArea = juce::Rectangle<int>(
        secondaryArea.getX() + secKnobWidth, secondaryArea.getY() + secKnobHeight, secKnobWidth, secKnobHeight);
    dryWetKnob.setBounds(mixArea.reduced(5).removeFromTop(50));
    dryWetLabel.setBounds(mixArea.removeFromBottom(18));

    // LED meters - positioned on left and right edges of the main knob area
    // Using standard dimensions from LEDMeterStyle for consistency with Multi-Q/Multi-Comp
    const int meterWidth = LEDMeterStyle::standardWidth;
    const int meterHeight = 100;  // Good height for the knob row area
    const int meterY = 60;        // Below header, aligned with top of tape viz

    // Input meter on left edge
    inputMeter->setBounds(8, meterY, meterWidth, meterHeight);

    // Output meter on right edge
    outputMeter->setBounds(getWidth() - meterWidth - 8, meterY, meterWidth, meterHeight);
}

void TapeEchoEditor::timerCallback()
{
    // Update tape visualization
    tapeViz.setTapeSpeed(processor.getCurrentTapeSpeed());
    tapeViz.setFeedbackIntensity(processor.getFeedbackLevel());
    tapeViz.setCurrentMode(processor.getCurrentMode());
    tapeViz.setIsPlaying(processor.isProcessing());

    for (int i = 0; i < 3; ++i)
    {
        tapeViz.setHeadActive(i, processor.isHeadActive(i));
    }

    // Update mode button states
    updateModeButtons();

    // Update level meters (convert linear to dB for LEDMeter)
    float inL = processor.getInputLevelL();
    float inR = processor.getInputLevelR();
    float outL = processor.getOutputLevelL();
    float outR = processor.getOutputLevelR();

    // Convert to dB (-60 to +6 range expected by LEDMeter)
    auto toDb = [](float linear) {
        return linear > 0.0f ? juce::Decibels::gainToDecibels(linear, -60.0f) : -60.0f;
    };

    inputMeter->setStereoLevels(toDb(inL), toDb(inR));
    outputMeter->setStereoLevels(toDb(outL), toDb(outR));
}

void TapeEchoEditor::setupKnob(juce::Slider& knob, juce::Label& label, const juce::String& text)
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(knob);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void TapeEchoEditor::updateModeButtons()
{
    int currentMode = processor.getCurrentMode();
    for (int i = 0; i < 12; ++i)
    {
        modeButtons[i]->setToggleState(i + 1 == currentMode, juce::dontSendNotification);
    }
}

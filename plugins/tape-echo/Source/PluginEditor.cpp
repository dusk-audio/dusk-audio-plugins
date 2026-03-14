/*
  ==============================================================================

    PluginEditor.cpp
    Tape Echo - Tape Echo Plugin UI

    Copyright (c) 2025 Dusk Audio - All rights reserved.

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

    // User preset manager
    userPresetManager_ = std::make_unique<UserPresetManager> ("TapeEcho");

    // Preset browser (factory + user presets)
    presetBox_.setJustificationType (juce::Justification::centred);
    presetBox_.onChange = [this]
    {
        int id = presetBox_.getSelectedId();
        if (id >= 1001)
        {
            int userIdx = id - 1001;
            auto userPresets = userPresetManager_->loadUserPresets();
            if (userIdx >= 0 && userIdx < static_cast<int> (userPresets.size()))
                loadUserPreset (userPresets[static_cast<size_t> (userIdx)].name);
        }
        else if (id >= 2)
        {
            loadPreset (id - 2);
        }
        updateDeleteButtonVisibility();
    };
    addAndMakeVisible (presetBox_);
    refreshPresetList();

    // Save preset button
    savePresetButton_.setButtonText ("Save");
    savePresetButton_.onClick = [this] { saveUserPreset(); };
    addAndMakeVisible (savePresetButton_);

    // Delete preset button (only visible when a user preset is selected)
    deletePresetButton_.setButtonText ("Del");
    deletePresetButton_.onClick = [this]
    {
        int id = presetBox_.getSelectedId();
        if (id >= 1001)
        {
            int userIdx = id - 1001;
            auto userPresets = userPresetManager_->loadUserPresets();
            if (userIdx >= 0 && userIdx < static_cast<int> (userPresets.size()))
            {
                auto name = userPresets[static_cast<size_t> (userIdx)].name;
                juce::Component::SafePointer<TapeEchoEditor> safeThis (this);

                juce::AlertWindow::showOkCancelBox (
                    juce::MessageBoxIconType::WarningIcon,
                    "Delete Preset",
                    "Delete \"" + name + "\"?",
                    "Delete", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create ([safeThis, name] (int result) {
                        if (result == 1 && safeThis != nullptr)
                        {
                            safeThis->deleteUserPreset (name);
                            safeThis->updateDeleteButtonVisibility();
                        }
                    }));
            }
        }
    };
    addAndMakeVisible (deletePresetButton_);
    deletePresetButton_.setVisible (false);

    // Timer for UI updates
    startTimerHz(30);

    // Initialize resizable UI (600x450 base, range 480-900 width)
    resizeHelper.initialize(this, &processor, 600, 450, 480, 360, 900, 675, false);
    setSize(resizeHelper.getStoredWidth(), resizeHelper.getStoredHeight());
}

TapeEchoEditor::~TapeEchoEditor()
{
    resizeHelper.saveSize();
    stopTimer();
    setLookAndFeel(nullptr);
}

void TapeEchoEditor::paint(juce::Graphics& g)
{
    // Get scale factor for consistent sizing with resized()
    float scale = resizeHelper.getScaleFactor();
    auto scaled = [scale](int value) { return static_cast<int>(static_cast<float>(value) * scale); };

    // Background gradient
    juce::ColourGradient bgGradient(
        TapeEchoLookAndFeel::primaryColor, 0, 0,
        TapeEchoLookAndFeel::primaryColor.darker(0.3f), 0, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGradient);
    g.fillAll();

    // Header - use same scaled(50) as resized() for consistency
    int headerHeight = scaled(50);
    g.setColour(TapeEchoLookAndFeel::darkBgColor);
    g.fillRect(0, 0, getWidth(), headerHeight);

    // Title - scale font and positions for consistent sizing
    g.setColour(TapeEchoLookAndFeel::textColor);
    g.setFont(juce::FontOptions(14.0f * scale).withStyle("Bold"));
    g.drawText("DUSK AUDIO", scaled(15), scaled(10), scaled(150), scaled(25), juce::Justification::centredLeft, true);

    g.setFont(juce::FontOptions(18.0f * scale).withStyle("Bold"));
    g.drawText("TAPE ECHO", getWidth() - scaled(165), scaled(10), scaled(150), scaled(25), juce::Justification::centredRight, true);

    // Section labels - positioned above each control section
    // Bottom section starts at scaled(50 + 120 + 120) = scaled(290)
    const int labelY = scaled(292);
    g.setColour(TapeEchoLookAndFeel::textColor.withAlpha(0.6f));
    g.setFont(juce::FontOptions(10.0f * scale));
    g.drawText("MODE SELECT", scaled(20), labelY, scaled(100), scaled(15), juce::Justification::centredLeft, true);
    g.drawText("SYNC", scaled(215), labelY, scaled(80), scaled(15), juce::Justification::centred, true);
    g.drawText("TONE / MIX", getWidth() - scaled(265), labelY, scaled(100), scaled(15), juce::Justification::centredLeft, true);

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
    resizeHelper.updateResizer();
    float scale = resizeHelper.getScaleFactor();

    // Scale helper lambda
    auto scaled = [scale](int value) { return static_cast<int>(static_cast<float>(value) * scale); };

    auto bounds = getLocalBounds();

    // Header area
    bounds.removeFromTop(scaled(50));

    // Preset browser in header (centered between title texts)
    {
        int presetW = scaled(180);
        int presetH = scaled(22);
        int presetY = scaled(14);
        int saveW   = scaled(45);
        int delW    = scaled(35);
        int btnGap  = scaled(4);
        int totalPresetW = presetW + btnGap + saveW + btnGap + delW;
        int presetStartX = (getWidth() - totalPresetW) / 2;
        presetBox_.setBounds (presetStartX, presetY, presetW, presetH);
        savePresetButton_.setBounds (presetStartX + presetW + btnGap, presetY, saveW, presetH);
        deletePresetButton_.setBounds (presetStartX + presetW + btnGap + saveW + btnGap,
                                       presetY, delW, presetH);
    }

    // Tape visualization (top portion)
    auto vizArea = bounds.removeFromTop(scaled(120));
    tapeViz.setBounds(vizArea.reduced(scaled(15), scaled(5)));

    // Main knobs row
    auto mainKnobArea = bounds.removeFromTop(scaled(120));
    const int mainKnobWidth = mainKnobArea.getWidth() / 5;

    auto inputArea = mainKnobArea.removeFromLeft(mainKnobWidth);
    inputKnob.setBounds(inputArea.reduced(scaled(10), 0).removeFromTop(scaled(80)).withTrimmedTop(scaled(10)));
    inputLabel.setBounds(inputArea.removeFromBottom(scaled(20)));

    auto repeatArea = mainKnobArea.removeFromLeft(mainKnobWidth);
    repeatRateKnob.setBounds(repeatArea.reduced(scaled(10), 0).removeFromTop(scaled(80)).withTrimmedTop(scaled(10)));
    repeatRateLabel.setBounds(repeatArea.removeFromBottom(scaled(20)));

    auto intensityArea = mainKnobArea.removeFromLeft(mainKnobWidth);
    intensityKnob.setBounds(intensityArea.reduced(scaled(10), 0).removeFromTop(scaled(80)).withTrimmedTop(scaled(10)));
    intensityLabel.setBounds(intensityArea.removeFromBottom(scaled(20)));

    auto echoArea = mainKnobArea.removeFromLeft(mainKnobWidth);
    echoVolumeKnob.setBounds(echoArea.reduced(scaled(10), 0).removeFromTop(scaled(80)).withTrimmedTop(scaled(10)));
    echoVolumeLabel.setBounds(echoArea.removeFromBottom(scaled(20)));

    auto reverbArea = mainKnobArea;
    reverbVolumeKnob.setBounds(reverbArea.reduced(scaled(10), 0).removeFromTop(scaled(80)).withTrimmedTop(scaled(10)));
    reverbVolumeLabel.setBounds(reverbArea.removeFromBottom(scaled(20)));

    // Bottom section
    auto bottomArea = bounds.removeFromTop(scaled(140));
    bottomArea.removeFromTop(scaled(20));  // Space for section labels

    // Mode selector (left side) - 4x3 grid
    auto modeArea = bottomArea.removeFromLeft(scaled(200)).reduced(scaled(15), scaled(5));
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
    auto syncArea = bottomArea.removeFromLeft(scaled(100)).reduced(scaled(5), scaled(5));
    tempoSyncButton.setBounds(syncArea.removeFromTop(scaled(30)).reduced(scaled(5), 0));
    auto noteKnobArea = syncArea.removeFromTop(scaled(60));
    noteDivisionKnob.setBounds(noteKnobArea.reduced(scaled(5), 0));
    noteDivisionLabel.setBounds(syncArea.removeFromTop(scaled(18)));

    // Secondary knobs (right side) - 2x2 grid
    auto secondaryArea = bottomArea.removeFromRight(scaled(250)).reduced(scaled(15), scaled(5));
    const int secKnobWidth = secondaryArea.getWidth() / 2;
    const int secKnobHeight = secondaryArea.getHeight() / 2;

    // Top row
    auto bassArea = juce::Rectangle<int>(
        secondaryArea.getX(), secondaryArea.getY(), secKnobWidth, secKnobHeight);
    bassKnob.setBounds(bassArea.reduced(scaled(5)).removeFromTop(scaled(50)));
    bassLabel.setBounds(bassArea.removeFromBottom(scaled(18)));

    auto trebleArea = juce::Rectangle<int>(
        secondaryArea.getX() + secKnobWidth, secondaryArea.getY(), secKnobWidth, secKnobHeight);
    trebleKnob.setBounds(trebleArea.reduced(scaled(5)).removeFromTop(scaled(50)));
    trebleLabel.setBounds(trebleArea.removeFromBottom(scaled(18)));

    // Bottom row
    auto wfArea = juce::Rectangle<int>(
        secondaryArea.getX(), secondaryArea.getY() + secKnobHeight, secKnobWidth, secKnobHeight);
    wowFlutterKnob.setBounds(wfArea.reduced(scaled(5)).removeFromTop(scaled(50)));
    wowFlutterLabel.setBounds(wfArea.removeFromBottom(scaled(18)));

    auto mixArea = juce::Rectangle<int>(
        secondaryArea.getX() + secKnobWidth, secondaryArea.getY() + secKnobHeight, secKnobWidth, secKnobHeight);
    dryWetKnob.setBounds(mixArea.reduced(scaled(5)).removeFromTop(scaled(50)));
    dryWetLabel.setBounds(mixArea.removeFromBottom(scaled(18)));

    // LED meters - positioned on left and right edges of the main knob area
    // Using standard dimensions from LEDMeterStyle for consistency with Multi-Q/Multi-Comp
    const int meterWidth = scaled(LEDMeterStyle::standardWidth);
    const int meterHeight = scaled(100);  // Good height for the knob row area
    const int meterY = scaled(60);        // Below header, aligned with top of tape viz

    // Input meter on left edge
    inputMeter->setBounds(scaled(8), meterY, meterWidth, meterHeight);

    // Output meter on right edge
    outputMeter->setBounds(getWidth() - meterWidth - scaled(8), meterY, meterWidth, meterHeight);
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

// =============================================================================
// Preset Management
// =============================================================================

void TapeEchoEditor::loadPreset (int index)
{
    const auto& presets = TapeEchoPresets::getFactoryPresets();
    if (index >= 0 && index < static_cast<int> (presets.size()))
        presets[static_cast<size_t> (index)].applyTo (processor.getAPVTS());
}

void TapeEchoEditor::refreshPresetList()
{
    auto currentName = presetBox_.getText();
    presetBox_.clear (juce::dontSendNotification);

    // Factory presets grouped by category (IDs starting at 2)
    const auto& presets = TapeEchoPresets::getFactoryPresets();
    juce::String lastCategory;
    int id = 2;

    for (size_t i = 0; i < presets.size(); ++i)
    {
        juce::String cat (presets[i].category);
        if (cat != lastCategory)
        {
            presetBox_.addSeparator();
            presetBox_.addSectionHeading (cat);
            lastCategory = cat;
        }
        presetBox_.addItem (presets[i].name, id++);
    }

    // User presets (IDs starting at 1001)
    if (userPresetManager_)
    {
        auto userPresets = userPresetManager_->loadUserPresets();
        if (! userPresets.empty())
        {
            presetBox_.addSeparator();
            presetBox_.addSectionHeading ("User Presets");

            for (size_t i = 0; i < userPresets.size(); ++i)
                presetBox_.addItem (userPresets[i].name, static_cast<int> (1001 + i));
        }
    }

    // Restore selection by name
    for (int i = 0; i < presetBox_.getNumItems(); ++i)
    {
        if (presetBox_.getItemText (i) == currentName)
        {
            presetBox_.setSelectedItemIndex (i, juce::dontSendNotification);
            break;
        }
    }
}

void TapeEchoEditor::saveUserPreset()
{
    if (! userPresetManager_)
        return;

    auto* dialog = new juce::AlertWindow ("Save Preset",
                                           "Enter a name for this preset:",
                                           juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor ("name", "My Preset", "Preset Name:");
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TapeEchoEditor> safeThis (this);
    juce::Component::SafePointer<juce::AlertWindow> safeDialog (dialog);

    dialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis, safeDialog] (int result) mutable
        {
            juce::String name;
            if (result == 1 && safeDialog != nullptr)
                name = safeDialog->getTextEditorContents ("name").trim();

            if (safeThis == nullptr || name.isEmpty())
                return;

            if (safeThis->userPresetManager_->presetExists (name))
            {
                juce::Component::SafePointer<TapeEchoEditor> safeInner (safeThis.getComponent());

                juce::AlertWindow::showOkCancelBox (
                    juce::MessageBoxIconType::QuestionIcon,
                    "Overwrite Preset?",
                    "A preset named \"" + name + "\" already exists. Overwrite it?",
                    "Overwrite", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create ([safeInner, name] (int confirmResult) {
                        if (confirmResult == 1 && safeInner != nullptr)
                        {
                            auto state = safeInner->processor.getAPVTS().copyState();
                            if (safeInner->userPresetManager_->saveUserPreset (name, state, JucePlugin_VersionString))
                                safeInner->refreshPresetList();
                        }
                    }));
            }
            else
            {
                auto state = safeThis->processor.getAPVTS().copyState();
                if (safeThis->userPresetManager_->saveUserPreset (name, state, JucePlugin_VersionString))
                    safeThis->refreshPresetList();
            }
        }), true);
}

void TapeEchoEditor::loadUserPreset (const juce::String& name)
{
    if (! userPresetManager_)
        return;

    auto state = userPresetManager_->loadUserPreset (name);
    if (state.isValid())
        processor.getAPVTS().replaceState (state);
}

void TapeEchoEditor::deleteUserPreset (const juce::String& name)
{
    if (! userPresetManager_)
        return;

    userPresetManager_->deleteUserPreset (name);
    refreshPresetList();
}

void TapeEchoEditor::updateDeleteButtonVisibility()
{
    deletePresetButton_.setVisible (presetBox_.getSelectedId() >= 1001);
}

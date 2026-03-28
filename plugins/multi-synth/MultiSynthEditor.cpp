#include "MultiSynthEditor.h"

//==============================================================================
// WaveformDisplay
void WaveformDisplay::updateBuffer(const float* data, int size)
{
    numSamples = juce::jmin(size, static_cast<int>(samples.size()));
    for (int i = 0; i < numSamples; ++i)
        samples[static_cast<size_t>(i)] = data[i];
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(bg.darker(0.3f));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Grid lines
    g.setColour(accent.withAlpha(0.08f));
    float cy = bounds.getCentreY();
    float qh = bounds.getHeight() * 0.25f;
    g.drawHorizontalLine(static_cast<int>(cy - qh), bounds.getX(), bounds.getRight());
    g.drawHorizontalLine(static_cast<int>(cy + qh), bounds.getX(), bounds.getRight());
    g.setColour(accent.withAlpha(0.15f));
    g.drawHorizontalLine(static_cast<int>(cy), bounds.getX(), bounds.getRight());

    // Vertical grid (4 divisions)
    for (int i = 1; i < 4; ++i)
    {
        float gx = bounds.getX() + bounds.getWidth() * static_cast<float>(i) / 4.0f;
        g.setColour(accent.withAlpha(0.06f));
        g.drawVerticalLine(static_cast<int>(gx), bounds.getY(), bounds.getBottom());
    }

    if (numSamples < 2) return;

    // Waveform path — use only ~200 points for smooth rendering
    int step = juce::jmax(1, numSamples / 200);
    juce::Path wavePath;
    float w = bounds.getWidth(), cx2 = bounds.getX();

    bool started = false;
    for (int i = 0; i < numSamples; i += step)
    {
        float x = cx2 + (static_cast<float>(i) / static_cast<float>(numSamples - 1)) * w;
        float y = cy - samples[static_cast<size_t>(i)] * bounds.getHeight() * 0.42f;
        if (!started) { wavePath.startNewSubPath(x, y); started = true; }
        else wavePath.lineTo(x, y);
    }

    // Glow effect (wider, transparent stroke behind)
    g.setColour(accent.withAlpha(0.12f));
    g.strokePath(wavePath, juce::PathStrokeType(4.0f));

    // Main waveform stroke
    g.setColour(accent.withAlpha(0.8f));
    g.strokePath(wavePath, juce::PathStrokeType(2.0f));

    // Border
    g.setColour(accent.withAlpha(0.2f));
    g.drawRoundedRectangle(bounds, 4.0f, 0.5f);
}

//==============================================================================
// FilterResponseDisplay
void FilterResponseDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(bg.darker(0.3f));
    g.fillRoundedRectangle(bounds, 3.0f);

    // Draw approximate lowpass response curve
    juce::Path curve;
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float x0 = bounds.getX();
    float y0 = bounds.getY();

    // Normalize cutoff to 0-1 range (log scale, 20Hz-20kHz)
    float cutoffNorm = std::log2(juce::jlimit(20.0f, 20000.0f, cutoff) / 20.0f) / std::log2(1000.0f);
    cutoffNorm = juce::jlimit(0.0f, 1.0f, cutoffNorm);

    bool started = false;
    for (int i = 0; i <= static_cast<int>(w); ++i)
    {
        float xNorm = static_cast<float>(i) / w;
        // Simple lowpass approximation: flat below cutoff, rolls off above
        float freqNorm = xNorm; // 0=low, 1=high (log scale)
        float response = 1.0f;
        if (freqNorm > cutoffNorm)
        {
            float rolloff = (freqNorm - cutoffNorm) / (1.0f - cutoffNorm + 0.01f);
            response = std::exp(-rolloff * 4.0f); // ~24dB/oct approximation
        }
        // Resonance peak near cutoff
        float distToCutoff = std::abs(freqNorm - cutoffNorm);
        if (distToCutoff < 0.15f)
            response += res * 0.8f * (1.0f - distToCutoff / 0.15f);

        response = juce::jlimit(0.0f, 1.5f, response);
        float y = y0 + h - response * h * 0.6f - h * 0.05f;

        if (!started) { curve.startNewSubPath(x0 + static_cast<float>(i), y); started = true; }
        else curve.lineTo(x0 + static_cast<float>(i), y);
    }

    g.setColour(accent.withAlpha(0.5f));
    g.strokePath(curve, juce::PathStrokeType(1.5f));

    // Border
    g.setColour(accent.withAlpha(0.15f));
    g.drawRoundedRectangle(bounds, 3.0f, 0.5f);
}

//==============================================================================
// ModMatrixOverlay
ModMatrixOverlay::ModMatrixOverlay()
{
    setInterceptsMouseClicks(true, true);
    for (int i = 0; i < 8; ++i)
    {
        addAndMakeVisible(srcBoxes[i]);
        addAndMakeVisible(dstBoxes[i]);
        addAndMakeVisible(amtSliders[i]);
        amtSliders[i].setSliderStyle(juce::Slider::RotaryVerticalDrag);
        amtSliders[i].setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        amtSliders[i].setPopupDisplayEnabled(true, false, this);
    }
}

void ModMatrixOverlay::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xE0101010));
    g.fillAll();
    auto panel = getLocalBounds().reduced(40, 60);
    g.setColour(juce::Colour(0xFF1E1E1E));
    g.fillRoundedRectangle(panel.toFloat(), 10.0f);
    g.setColour(accent.withAlpha(0.5f));
    g.drawRoundedRectangle(panel.toFloat().reduced(0.5f), 10.0f, 1.5f);
    g.setFont(juce::Font(juce::FontOptions(16.0f).withStyle("Bold")));
    g.setColour(accent);
    g.drawText("MODULATION MATRIX", panel.getX(), panel.getY() + 10,
               panel.getWidth(), 24, juce::Justification::centred);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(juce::Colour(0xFF707070));
    g.drawText("Click outside to close", panel.getX(), panel.getBottom() - 28,
               panel.getWidth(), 20, juce::Justification::centred);
}

void ModMatrixOverlay::resized()
{
    auto panel = getLocalBounds().reduced(40, 60);
    int slotW = (panel.getWidth() - 20) / 4;
    int startY = panel.getY() + 44;
    int comboH = 22, knobS = 45;
    for (int i = 0; i < 8; ++i)
    {
        int col = i % 4, row = i / 4;
        int x = panel.getX() + 10 + col * slotW;
        int y = startY + row * (comboH * 2 + knobS + 16);
        srcBoxes[i].setBounds(x, y, slotW - 8, comboH);
        dstBoxes[i].setBounds(x, y + comboH + 2, slotW - 8, comboH);
        amtSliders[i].setBounds(x + (slotW - knobS) / 2, y + comboH * 2 + 6, knobS, knobS);
    }
}

void ModMatrixOverlay::mouseDown(const juce::MouseEvent& e)
{
    auto panel = getLocalBounds().reduced(40, 60);
    if (!panel.contains(e.getPosition()) && onDismiss)
        onDismiss();
}

//==============================================================================
MultiSynthEditor::MultiSynthEditor(MultiSynthProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    currentLAF = &cosmosLAF;
    setLookAndFeel(currentLAF);

    // Top bar
    modeSelector.addItemList({"Cosmos", "Oracle", "Mono", "Modular"}, 1);
    addAndMakeVisible(modeSelector);
    modeSelectorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getAPVTS(), ParamIDs::MODE, modeSelector);

    // Preset system
    userPresetManager = std::make_unique<UserPresetManager>("Multi-Synth");
    addAndMakeVisible(presetBox);
    refreshPresetList();
    presetBox.onChange = [this] {
        int id = presetBox.getSelectedId();
        if (id <= 0) return;
        if (id <= factoryPresetCount)
            processor.setCurrentProgram(id - 1);
        else
            loadUserPreset(presetBox.getText());
    };

    savePresetButton.onClick = [this] { saveUserPreset(); };
    addAndMakeVisible(savePresetButton);
    deletePresetButton.onClick = [this] { deleteUserPreset(); };
    addAndMakeVisible(deletePresetButton);

    oversamplingBox.addItemList({"1x", "2x", "4x"}, 1);
    addAndMakeVisible(oversamplingBox);
    setupComboBox(oversamplingBox, ParamIDs::OVERSAMPLING);
    modMatrixButton.onClick = [this] { modMatrixOverlay.setVisible(!modMatrixOverlay.isVisible()); };
    addAndMakeVisible(modMatrixButton);

    // === Oscillators ===
    osc1WaveBox.addItemList({"Saw", "Square", "Triangle", "Sine", "Pulse"}, 1);
    addAndMakeVisible(osc1WaveBox); setupComboBox(osc1WaveBox, ParamIDs::OSC1_WAVE);
    setupKnob(osc1LevelSlider, osc1LevelLbl, ParamIDs::OSC1_LEVEL, "Level");
    setupKnob(osc1DetuneSlider, osc1DetuneLbl, ParamIDs::OSC1_DETUNE, "Detune");
    setupKnob(osc1PWSlider, osc1PWLbl, ParamIDs::OSC1_PW, "PW");

    osc2WaveBox.addItemList({"Saw", "Square", "Triangle", "Sine", "Pulse"}, 1);
    addAndMakeVisible(osc2WaveBox); setupComboBox(osc2WaveBox, ParamIDs::OSC2_WAVE);
    setupKnob(osc2LevelSlider, osc2LevelLbl, ParamIDs::OSC2_LEVEL, "Level");
    setupKnob(osc2DetuneSlider, osc2DetuneLbl, ParamIDs::OSC2_DETUNE, "Detune");
    setupKnob(osc2SemiSlider, osc2SemiLbl, ParamIDs::OSC2_SEMI, "Semi");

    osc3WaveBox.addItemList({"Saw", "Square", "Triangle", "Sine"}, 1);
    addAndMakeVisible(osc3WaveBox); setupComboBox(osc3WaveBox, ParamIDs::OSC3_WAVE);
    setupKnob(osc3LevelSlider, osc3LevelLbl, ParamIDs::OSC3_LEVEL, "Osc3");

    subWaveBox.addItemList({"Square", "Sine"}, 1);
    addAndMakeVisible(subWaveBox); setupComboBox(subWaveBox, ParamIDs::SUB_WAVE);
    setupKnob(subLevelSlider, subLevelLbl, ParamIDs::SUB_LEVEL, "Sub");
    setupKnob(noiseLevelSlider, noiseLevelLbl, ParamIDs::NOISE_LEVEL, "Noise");
    setupKnob(crossModSlider, crossModLbl, ParamIDs::CROSS_MOD, "Mod");
    setupKnob(ringModSlider, ringModLbl, ParamIDs::RING_MOD, "Ring");
    setupKnob(fmAmountSlider, fmAmountLbl, ParamIDs::FM_AMOUNT, "FM");
    setupToggle(hardSyncButton, ParamIDs::HARD_SYNC, "Sync");

    // === Oracle poly-mod knobs ===
    setupKnob(pmFEnvOscASlider, pmFEnvOscALbl, ParamIDs::POLYMOD_FENV_OSCA, "FE→A");
    setupKnob(pmFEnvFiltSlider, pmFEnvFiltLbl, ParamIDs::POLYMOD_FENV_FILT, "FE→F");
    setupKnob(pmOscBOscASlider, pmOscBOscALbl, ParamIDs::POLYMOD_OSCB_OSCA, "OB→A");
    setupKnob(pmOscBPWMSlider,  pmOscBPWMLbl,  ParamIDs::POLYMOD_OSCB_PWM,  "OB→PW");

    // === Cosmos Chorus ===
    cosmosChorusBox.addItemList({"Off", "I", "II", "I+II"}, 1);
    addAndMakeVisible(cosmosChorusBox);
    setupComboBox(cosmosChorusBox, ParamIDs::COSMOS_CHORUS_MODE);

    // === Filter ===
    setupKnob(filterCutoffSlider, filterCutoffLbl, ParamIDs::FILTER_CUTOFF, "Cutoff");
    setupKnob(filterResSlider, filterResLbl, ParamIDs::FILTER_RESONANCE, "Res");
    setupKnob(filterHPSlider, filterHPLbl, ParamIDs::FILTER_HP_CUTOFF, "HP");
    setupKnob(filterEnvAmtSlider, filterEnvAmtLbl, ParamIDs::FILTER_ENV_AMT, "Env");

    // === Envelopes ===
    setupKnob(ampASlider, ampALbl, ParamIDs::AMP_ATTACK, "A");
    setupKnob(ampDSlider, ampDLbl, ParamIDs::AMP_DECAY, "D");
    setupKnob(ampSSlider, ampSLbl, ParamIDs::AMP_SUSTAIN, "S");
    setupKnob(ampRSlider, ampRLbl, ParamIDs::AMP_RELEASE, "R");
    ampCurveBox.addItemList({"Linear", "Exponential", "Logarithmic", "Analog RC"}, 1);
    addAndMakeVisible(ampCurveBox); setupComboBox(ampCurveBox, ParamIDs::AMP_CURVE);

    setupKnob(filtASlider, filtALbl, ParamIDs::FILT_ATTACK, "A");
    setupKnob(filtDSlider, filtDLbl, ParamIDs::FILT_DECAY, "D");
    setupKnob(filtSSlider, filtSLbl, ParamIDs::FILT_SUSTAIN, "S");
    setupKnob(filtRSlider, filtRLbl, ParamIDs::FILT_RELEASE, "R");
    filtCurveBox.addItemList({"Linear", "Exponential", "Logarithmic", "Analog RC"}, 1);
    addAndMakeVisible(filtCurveBox); setupComboBox(filtCurveBox, ParamIDs::FILT_CURVE);

    // === LFOs ===
    lfo1ShapeBox.addItemList({"Sine", "Tri", "Square", "S&H", "Rand"}, 1);
    addAndMakeVisible(lfo1ShapeBox); setupComboBox(lfo1ShapeBox, ParamIDs::LFO1_SHAPE);
    setupKnob(lfo1RateSlider, lfo1RateLbl, ParamIDs::LFO1_RATE, "Rate");
    setupKnob(lfo1FadeSlider, lfo1FadeLbl, ParamIDs::LFO1_FADE, "Fade");
    setupToggle(lfo1SyncButton, ParamIDs::LFO1_SYNC, "Sync");

    lfo2ShapeBox.addItemList({"Sine", "Tri", "Square", "S&H", "Rand"}, 1);
    addAndMakeVisible(lfo2ShapeBox); setupComboBox(lfo2ShapeBox, ParamIDs::LFO2_SHAPE);
    setupKnob(lfo2RateSlider, lfo2RateLbl, ParamIDs::LFO2_RATE, "Rate");
    setupKnob(lfo2FadeSlider, lfo2FadeLbl, ParamIDs::LFO2_FADE, "Fade");
    setupToggle(lfo2SyncButton, ParamIDs::LFO2_SYNC, "Sync");
    setupKnob(shRateSlider, shRateLbl, ParamIDs::SH_RATE, "S&H");

    // === Character / Unison ===
    setupKnob(portaSlider, portaLbl, ParamIDs::PORTA_TIME, "Porta");
    setupKnob(analogSlider, analogLbl, ParamIDs::ANALOG_AMT, "Analog");
    setupKnob(vintageSlider, vintageLbl, ParamIDs::VINTAGE, "Vintage");
    setupKnob(velSensSlider, velSensLbl, ParamIDs::VEL_SENS, "Vel");
    velCurveBox.addItemList({"Linear", "Soft", "Hard", "Fixed"}, 1);
    addAndMakeVisible(velCurveBox);
    setupComboBox(velCurveBox, ParamIDs::VEL_CURVE);
    setupToggle(legatoButton, ParamIDs::LEGATO, "Legato");
    glideModeBox.addItemList({"Time", "Rate"}, 1);
    addAndMakeVisible(glideModeBox);
    setupComboBox(glideModeBox, ParamIDs::GLIDE_MODE);
    setupKnob(unisonVoicesSlider, unisonVoicesLbl, ParamIDs::UNISON_VOICES, "Voices");
    setupKnob(unisonDetuneSlider, unisonDetuneLbl, ParamIDs::UNISON_DETUNE, "Detune");
    setupKnob(unisonSpreadSlider, unisonSpreadLbl, ParamIDs::UNISON_SPREAD, "Spread");

    // === Arpeggiator ===
    setupToggle(arpOnButton, ParamIDs::ARP_ON, "On");
    arpModeBox.addItemList({"Up", "Down", "Up/Dn", "Dn/Up", "Rand", "Order", "Chord"}, 1);
    addAndMakeVisible(arpModeBox); setupComboBox(arpModeBox, ParamIDs::ARP_MODE);
    arpRateBox.addItemList({"1/1","1/2","1/4","1/8","1/16","1/32",
                            "1/2D","1/4D","1/8D","1/16D","1/2T","1/4T","1/8T","1/16T"}, 1);
    addAndMakeVisible(arpRateBox); setupComboBox(arpRateBox, ParamIDs::ARP_RATE);
    setupKnob(arpOctaveSlider, arpOctaveLbl, ParamIDs::ARP_OCTAVE, "Oct");
    setupKnob(arpGateSlider, arpGateLbl, ParamIDs::ARP_GATE, "Gate");
    setupKnob(arpSwingSlider, arpSwingLbl, ParamIDs::ARP_SWING, "Swing");
    setupToggle(arpLatchButton, ParamIDs::ARP_LATCH, "Latch");
    arpVelModeBox.addItemList({"As Played", "Fixed", "Accent"}, 1);
    addAndMakeVisible(arpVelModeBox); setupComboBox(arpVelModeBox, ParamIDs::ARP_VEL_MODE);
    for (int i = 0; i < 16; ++i)
    {
        arpStepButtons[i].setButtonText(juce::String(i + 1));
        addAndMakeVisible(arpStepButtons[i]);
        buttonAttachments.push_back(
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor.getAPVTS(), ParamIDs::arpStep(i), arpStepButtons[i]));
    }

    // === Effects ===
    setupToggle(driveOnButton, ParamIDs::DRIVE_ON, "On");
    driveTypeBox.addItemList({"Soft", "Hard", "Tube"}, 1);
    addAndMakeVisible(driveTypeBox); setupComboBox(driveTypeBox, ParamIDs::DRIVE_TYPE);
    setupKnob(driveAmtSlider, driveAmtLbl, ParamIDs::DRIVE_AMT, "Drive");
    setupKnob(driveMixSlider, driveMixLbl, ParamIDs::DRIVE_MIX, "Mix");

    setupToggle(chorusOnButton, ParamIDs::CHORUS_ON, "On");
    setupKnob(chorusRateSlider, chorusRateLbl, ParamIDs::CHORUS_RATE, "Rate");
    setupKnob(chorusDepthSlider, chorusDepthLbl, ParamIDs::CHORUS_DEPTH, "Depth");
    setupKnob(chorusMixSlider, chorusMixLbl, ParamIDs::CHORUS_MIX, "Mix");

    setupToggle(delayOnButton, ParamIDs::DELAY_ON, "On");
    setupToggle(delaySyncButton, ParamIDs::DELAY_SYNC, "Sync");
    setupToggle(delayPPButton, ParamIDs::DELAY_PINGPONG, "PP");
    setupToggle(delayTapeButton, ParamIDs::DELAY_TAPE, "Tape");
    delayDivBox.addItemList({"1/1","1/2","1/4","1/8","1/16","1/32",
                             "1/2D","1/4D","1/8D","1/16D","1/2T","1/4T","1/8T","1/16T"}, 1);
    addAndMakeVisible(delayDivBox); setupComboBox(delayDivBox, ParamIDs::DELAY_RATE_DIV);
    setupKnob(delayTimeSlider, delayTimeLbl, ParamIDs::DELAY_TIME, "Time");
    setupKnob(delayFBSlider, delayFBLbl, ParamIDs::DELAY_FEEDBACK, "FB");
    setupKnob(delayMixSlider, delayMixLbl, ParamIDs::DELAY_MIX, "Mix");

    setupToggle(reverbOnButton, ParamIDs::REVERB_ON, "On");
    setupKnob(reverbSizeSlider, reverbSizeLbl, ParamIDs::REVERB_SIZE, "Size");
    setupKnob(reverbDecaySlider, reverbDecayLbl, ParamIDs::REVERB_DECAY, "Decay");
    setupKnob(reverbDampSlider, reverbDampLbl, ParamIDs::REVERB_DAMP, "Damp");
    setupKnob(reverbMixSlider, reverbMixLbl, ParamIDs::REVERB_MIX, "Mix");
    setupKnob(reverbPDSlider, reverbPDLbl, ParamIDs::REVERB_PREDELAY, "PreD");

    // === Master ===
    setupKnob(masterTuneSlider, masterTuneLbl, ParamIDs::MASTER_TUNE, "Tune");
    setupKnob(masterVolSlider, masterVolLbl, ParamIDs::MASTER_VOL, "Vol");
    setupKnob(masterPanSlider, masterPanLbl, ParamIDs::MASTER_PAN, "Pan");
    setupKnob(stereoWidthSlider, stereoWidthLbl, ParamIDs::STEREO_WIDTH, "Width");
    addAndMakeVisible(outputMeterL);
    addAndMakeVisible(outputMeterR);

    // === Oscilloscope ===
    addAndMakeVisible(waveformDisplay);

    // === Filter Response ===
    addAndMakeVisible(filterResponseDisplay);

    // === Mod Matrix Overlay ===
    juce::StringArray srcNames = {"None","LFO1","LFO2","Env2","ModWhl","AftT","Vel","Key","Rand","PBend","S&H"};
    juce::StringArray dstNames = {"None","O1Pit","O2Pit","O1PW","O2PW","FltCut","FltRes","Amp","Pan","L1Rt","L2Rt","FXMix","UniDt"};
    for (int i = 0; i < 8; ++i)
    {
        modMatrixOverlay.srcBoxes[i].addItemList(srcNames, 1);
        setupComboBox(modMatrixOverlay.srcBoxes[i], ParamIDs::modSlotSource(i));
        modMatrixOverlay.dstBoxes[i].addItemList(dstNames, 1);
        setupComboBox(modMatrixOverlay.dstBoxes[i], ParamIDs::modSlotDest(i));
        setupSlider(modMatrixOverlay.amtSliders[i], ParamIDs::modSlotAmount(i));
    }
    modMatrixOverlay.setVisible(false);
    modMatrixOverlay.onDismiss = [this] { modMatrixOverlay.setVisible(false); };
    addChildComponent(modMatrixOverlay);

    // === Supporters ===
    supportersOverlay.setPluginName("Multi-Synth");
    supportersOverlay.setVisible(false);
    supportersOverlay.onDismiss = [this] { supportersOverlay.setVisible(false); };
    addChildComponent(supportersOverlay);

    // Initialize
    scaleHelper.initialize(this, &processor,
                           kDefaultWidth, kDefaultHeight,
                           750, 600, 1500, 1200, true);
    setSize(scaleHelper.getStoredWidth(), scaleHelper.getStoredHeight());
    startTimerHz(30);
    updateModeVisibility();
}

MultiSynthEditor::~MultiSynthEditor()
{
    for (auto* child : getChildren())
        child->setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
    scaleHelper.saveSize();
}

//==============================================================================
void MultiSynthEditor::setupSlider(DuskSlider& slider, const juce::String& paramId)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setPopupDisplayEnabled(true, false, this);
    addAndMakeVisible(slider);
    sliderAttachments.push_back(
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getAPVTS(), paramId, slider));
}

void MultiSynthEditor::setupKnob(DuskSlider& slider, juce::Label& label,
                                   const juce::String& paramId, const juce::String& name)
{
    setupSlider(slider, paramId);
    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(juce::FontOptions(11.0f)));
    label.setColour(juce::Label::textColourId, juce::Colour(0xFFD0D0D0));
    addAndMakeVisible(label);
}

void MultiSynthEditor::setupComboBox(juce::ComboBox& box, const juce::String& paramId)
{
    comboAttachments.push_back(
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.getAPVTS(), paramId, box));
}

void MultiSynthEditor::setupToggle(juce::ToggleButton& button, const juce::String& paramId,
                                     const juce::String& text)
{
    button.setButtonText(text);
    addAndMakeVisible(button);
    buttonAttachments.push_back(
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.getAPVTS(), paramId, button));
}

//==============================================================================
void MultiSynthEditor::applyCurrentLookAndFeel()
{
    if (!currentLAF) return;
    setLookAndFeel(currentLAF);
    for (auto* child : getChildren())
    {
        child->setLookAndFeel(currentLAF);
        // Update label colors to match mode
        if (auto* label = dynamic_cast<juce::Label*>(child))
            label->setColour(juce::Label::textColourId, currentLAF->colors.text);
    }
    // Force relayout since fader/knob styles may have changed
    resized();
}

void MultiSynthEditor::updateModeVisibility()
{
    auto mode = processor.getCurrentMode();
    if (mode != lastMode)
    {
        lastMode = mode;
        switch (mode)
        {
            case MultiSynthDSP::SynthMode::Cosmos:  currentLAF = &cosmosLAF; break;
            case MultiSynthDSP::SynthMode::Oracle:  currentLAF = &oracleLAF; break;
            case MultiSynthDSP::SynthMode::Mono:    currentLAF = &monoLAF; break;
            case MultiSynthDSP::SynthMode::Modular: currentLAF = &modularLAF; break;
        }
        applyCurrentLookAndFeel();
        waveformDisplay.setAccentColor(currentLAF->colors.accent);
        waveformDisplay.setBackgroundColor(currentLAF->colors.sectionBackground);
        modMatrixOverlay.setAccentColor(currentLAF->colors.accent);
    }

    bool isCosmos  = (mode == MultiSynthDSP::SynthMode::Cosmos);
    bool isOracle  = (mode == MultiSynthDSP::SynthMode::Oracle);
    bool isMono    = (mode == MultiSynthDSP::SynthMode::Mono);
    bool isModular = (mode == MultiSynthDSP::SynthMode::Modular);

    osc3WaveBox.setVisible(isModular); osc3LevelSlider.setVisible(isModular); osc3LevelLbl.setVisible(isModular);
    subWaveBox.setVisible(isCosmos || isMono); subLevelSlider.setVisible(isCosmos || isMono); subLevelLbl.setVisible(isCosmos || isMono);
    cosmosChorusBox.setVisible(isCosmos);

    // Oracle poly-mod knobs
    pmFEnvOscASlider.setVisible(isOracle); pmFEnvOscALbl.setVisible(isOracle);
    pmFEnvFiltSlider.setVisible(isOracle); pmFEnvFiltLbl.setVisible(isOracle);
    pmOscBOscASlider.setVisible(isOracle); pmOscBOscALbl.setVisible(isOracle);
    pmOscBPWMSlider.setVisible(isOracle);  pmOscBPWMLbl.setVisible(isOracle);
    filterHPSlider.setVisible(isCosmos); filterHPLbl.setVisible(isCosmos);
    crossModSlider.setVisible(isCosmos || isOracle); crossModLbl.setVisible(isCosmos || isOracle);
    ringModSlider.setVisible(isMono || isModular); ringModLbl.setVisible(isMono || isModular);
    fmAmountSlider.setVisible(isModular); fmAmountLbl.setVisible(isModular);
    hardSyncButton.setVisible(isModular || isMono);
}

//==============================================================================
void MultiSynthEditor::paint(juce::Graphics& g)
{
    if (!currentLAF) return;
    int w = getWidth(), h = getHeight();
    int topBar = scaled(kTopBarH);
    float sf = scaleHelper.getScaleFactor();

    // Mode-specific background
    currentLAF->paintBackground(g, w, h);

    // Top bar
    auto modeColor = getModeColor();
    g.setColour(modeColor.withAlpha(0.1f));
    g.fillRect(0, 0, w, topBar);
    g.setColour(modeColor.withAlpha(0.3f));
    g.drawLine(0.0f, static_cast<float>(topBar), static_cast<float>(w), static_cast<float>(topBar), 1.0f);

    // Title
    g.setFont(juce::Font(juce::FontOptions(18.0f * sf).withStyle("Bold")));
    g.setColour(currentLAF->colors.text);
    g.drawText("Multi-Synth", scaled(8), 0, scaled(120), topBar, juce::Justification::centredLeft);

    // MIDI activity indicators (pitch bend / mod wheel)
    {
        float pb = processor.displayPitchBend.load(std::memory_order_relaxed);
        float mw = processor.displayModWheel.load(std::memory_order_relaxed);
        if (std::abs(pb) > 0.01f || mw > 0.01f)
        {
            g.setFont(juce::Font(juce::FontOptions(10.0f * sf)));
            g.setColour(currentLAF->colors.accent.withAlpha(0.7f));
            juce::String info;
            if (std::abs(pb) > 0.01f) info += "PB:" + juce::String(pb, 1) + " ";
            if (mw > 0.01f) info += "MW:" + juce::String(static_cast<int>(mw * 100)) + "%";
            g.drawText(info, w / 2 - scaled(60), 0, scaled(120), topBar, juce::Justification::centred);
        }
    }

    // Dispatch to per-mode painting
    switch (processor.getCurrentMode())
    {
        case MultiSynthDSP::SynthMode::Cosmos:  paintCosmos(g); break;
        case MultiSynthDSP::SynthMode::Oracle:  paintOracle(g); break;
        case MultiSynthDSP::SynthMode::Mono:    paintMono(g); break;
        case MultiSynthDSP::SynthMode::Modular: paintModular(g); break;
    }

    // Arp step dots (shared across modes)
    if (arpOnButton.getToggleState())
    {
        auto ab = sections.arp;
        int dotsY = ab.getBottom() - scaled(14);
        int dotsX = ab.getX() + scaled(10);
        int totalSteps = processor.arpTotalSteps.load(std::memory_order_relaxed);
        int curStep = processor.arpCurrentStep.load(std::memory_order_relaxed);
        int dotSz = scaled(5), dotGap = scaled(3);
        for (int i = 0; i < juce::jmin(totalSteps, 16); ++i)
        {
            g.setColour((i == curStep % juce::jmax(1, totalSteps)) ? modeColor : juce::Colour(0xff404040));
            g.fillEllipse(static_cast<float>(dotsX + i * (dotSz + dotGap)),
                          static_cast<float>(dotsY),
                          static_cast<float>(dotSz), static_cast<float>(dotSz));
        }
    }

    currentLAF->paintSpecialElements(g, getLocalBounds());
}

//==============================================================================
// Per-mode paint methods — each paints sections with hardware-accurate names and styling

void MultiSynthEditor::paintCosmos(juce::Graphics& g)
{
    float sf = scaleHelper.getScaleFactor();
    auto ps = [&](juce::Rectangle<int> b, const juce::String& t) {
        currentLAF->paintSection(g, b, t, sf);
    };

    // Jupiter-8 style: orange header strip across the top of Row 1
    int stripY = sections.oscillators.getY();
    int stripH = scaled(kSectionTitleH);
    int stripX = sections.oscillators.getX();
    int stripW = sections.scopeArea.getRight() - stripX;
    g.setColour(juce::Colour(0xFFE87030)); // Jupiter orange
    g.fillRect(stripX, stripY, stripW, stripH);

    // Subtle inner shadow at top of each section panel (darker gradient on the strip)
    {
        juce::ColourGradient shadow(juce::Colour(0x30000000), static_cast<float>(stripX), static_cast<float>(stripY),
                                     juce::Colours::transparentBlack, static_cast<float>(stripX), static_cast<float>(stripY + stripH / 3), false);
        g.setGradientFill(shadow);
        g.fillRect(stripX, stripY, stripW, stripH / 3);
    }

    g.setColour(juce::Colour(0xFFFFFFFF));
    g.setFont(juce::Font(juce::FontOptions(10.0f * sf).withStyle("Bold")));

    // Individual section labels on the orange strip (Jupiter-8 style separate names)
    g.drawText("LFO", sections.oscillators.getX() + scaled(4), stripY, scaled(40), stripH, juce::Justification::centredLeft);
    g.drawText("OSC 1", sections.oscillators.getX() + scaled(50), stripY, scaled(50), stripH, juce::Justification::centredLeft);
    g.drawText("OSC 2", sections.oscillators.getX() + scaled(200), stripY, scaled(50), stripH, juce::Justification::centredLeft);
    g.drawText("VCF", sections.filter.getX() + scaled(4), stripY, sections.filter.getWidth(), stripH, juce::Justification::centredLeft);
    g.drawText("ENV-1", sections.envelopes.getX() + scaled(4), stripY, scaled(50), stripH, juce::Justification::centredLeft);
    g.drawText("ENV-2", sections.envelopes.getX() + sections.envelopes.getWidth() / 2, stripY, scaled(50), stripH, juce::Justification::centredLeft);
    g.drawText("OUTPUT", sections.scopeArea.getX() + scaled(4), stripY, sections.scopeArea.getWidth(), stripH, juce::Justification::centredLeft);

    // Thin vertical separator lines between sections (Jupiter-8 panel divisions)
    g.setColour(juce::Colour(0x40FFFFFF));
    g.drawVerticalLine(sections.filter.getX(), static_cast<float>(stripY), static_cast<float>(stripY + stripH));
    g.drawVerticalLine(sections.envelopes.getX(), static_cast<float>(stripY), static_cast<float>(stripY + stripH));
    g.drawVerticalLine(sections.scopeArea.getX(), static_cast<float>(stripY), static_cast<float>(stripY + stripH));

    // Sections below the strip (no individual titles — the strip serves as header)
    ps(sections.oscillators.withTrimmedTop(stripH), "");
    ps(sections.filter.withTrimmedTop(stripH), "");
    ps(sections.envelopes.withTrimmedTop(stripH), "");
    ps(sections.scopeArea.withTrimmedTop(stripH), "");
    ps(sections.lfo, "LFO / UNISON");
    ps(sections.character, "CHORUS / CHARACTER");
    ps(sections.arp, "ARPEGGIATOR");
    ps(sections.drive, "DRIVE"); ps(sections.chorus, "CHORUS");
    ps(sections.delay, "DELAY"); ps(sections.reverb, "REVERB");
}

void MultiSynthEditor::paintOracle(juce::Graphics& g)
{
    float sf = scaleHelper.getScaleFactor();
    int w = getWidth(), h = getHeight();
    int woodW = scaled(25);

    // Walnut wood side cheeks
    for (int side = 0; side < 2; ++side)
    {
        int wx = (side == 0) ? 0 : w - woodW;
        juce::ColourGradient woodGrad(juce::Colour(0xFF4A3020), static_cast<float>(wx), 0,
                                       juce::Colour(0xFF2A1810), static_cast<float>(wx + woodW), 0, false);
        g.setGradientFill(woodGrad);
        g.fillRect(wx, scaled(kTopBarH), woodW, h - scaled(kTopBarH));

        // Wood grain lines
        juce::Random rng(side * 100 + 7);
        for (int i = 0; i < 20; ++i)
        {
            int ly = scaled(kTopBarH) + rng.nextInt(h - scaled(kTopBarH));
            g.setColour(juce::Colour(0xFF3A2818).withAlpha(0.15f));
            g.drawHorizontalLine(ly, static_cast<float>(wx), static_cast<float>(wx + woodW));
        }
    }

    // Gold/amber pinstripe along inner edge of each wood cheek
    g.setColour(juce::Colour(0x40AA8040));
    g.drawVerticalLine(woodW, static_cast<float>(scaled(kTopBarH)), static_cast<float>(h));
    g.drawVerticalLine(w - woodW - 1, static_cast<float>(scaled(kTopBarH)), static_cast<float>(h));

    auto ps = [&](juce::Rectangle<int> b, const juce::String& t) {
        currentLAF->paintSection(g, b, t, sf);
    };

    // Prophet-5 style: separate section labels with amber underline badge
    auto psWithBadge = [&](juce::Rectangle<int> b, const juce::String& t) {
        ps(b, t);
        // Thin amber horizontal line below the section title (Prophet-5 badge)
        int badgeY = b.getY() + scaled(kSectionTitleH) - 1;
        g.setColour(juce::Colour(0x50AA8040));
        g.drawHorizontalLine(badgeY, static_cast<float>(b.getX() + scaled(4)),
                             static_cast<float>(b.getX() + scaled(4) + juce::Font(juce::FontOptions(10.0f * sf)).getStringWidthFloat(t) + scaled(8)));
    };

    psWithBadge(sections.oscillators, "POLY-MOD / OSC A / OSC B / MIXER");
    psWithBadge(sections.filter, "FILTER");
    psWithBadge(sections.envelopes, "FILTER ENV / AMP ENV");
    psWithBadge(sections.scopeArea, "OUTPUT");
    psWithBadge(sections.lfo, "LFO / GLIDE");
    psWithBadge(sections.character, "UNISON / CHARACTER");
    ps(sections.arp, "ARPEGGIATOR");
    ps(sections.drive, "DRIVE"); ps(sections.chorus, "CHORUS");
    ps(sections.delay, "DELAY"); ps(sections.reverb, "REVERB");
}

void MultiSynthEditor::paintMono(juce::Graphics& g)
{
    float sf = scaleHelper.getScaleFactor();
    auto ps = [&](juce::Rectangle<int> b, const juce::String& t) {
        currentLAF->paintSection(g, b, t, sf);
    };

    // SH-2 style colored indicator dots next to section titles
    auto psWithDot = [&](juce::Rectangle<int> b, const juce::String& t, juce::Colour dotColour) {
        ps(b, t);
        // Tiny colored dot left of section title text (like SH-2 colored indicators)
        int dotX = b.getX() + scaled(2);
        int dotY = b.getY() + scaled(kSectionTitleH) / 2;
        float dotR = 3.0f * sf;
        g.setColour(dotColour);
        g.fillEllipse(static_cast<float>(dotX) - dotR, static_cast<float>(dotY) - dotR, dotR * 2, dotR * 2);
    };

    psWithDot(sections.oscillators, "VCO-1 / VCO-2 / MIXER", juce::Colour(0xFFE04040)); // red
    psWithDot(sections.filter, "VCF", juce::Colour(0xFFE0A020));                          // amber
    psWithDot(sections.envelopes, "ENV", juce::Colour(0xFF40C040));                        // green
    psWithDot(sections.scopeArea, "VCA / OUTPUT", juce::Colour(0xFF4080E0));               // blue
    ps(sections.lfo, "MODULATOR");
    ps(sections.character, "PORTAMENTO / CHARACTER");
    ps(sections.arp, "ARPEGGIO");
    ps(sections.drive, "DRIVE"); ps(sections.chorus, "CHORUS");
    ps(sections.delay, "DELAY"); ps(sections.reverb, "REVERB");

    // Section separator lines between VCO/VCF/ENV/VCA sections (thin white, SH-2 style)
    g.setColour(currentLAF->colors.text.withAlpha(0.12f));
    int sepTop = sections.oscillators.getY();
    int sepBot = sections.oscillators.getBottom();
    g.drawVerticalLine(sections.filter.getX() - 1, static_cast<float>(sepTop), static_cast<float>(sepBot));
    g.drawVerticalLine(sections.envelopes.getX() - 1, static_cast<float>(sepTop), static_cast<float>(sepBot));
    g.drawVerticalLine(sections.scopeArea.getX() - 1, static_cast<float>(sepTop), static_cast<float>(sepBot));

    // SH-2 branding area in bottom-right with horizontal line above
    int brandY = getHeight() - scaled(24);
    g.setColour(currentLAF->colors.text.withAlpha(0.2f));
    g.drawHorizontalLine(brandY, static_cast<float>(getWidth() - scaled(210)),
                         static_cast<float>(getWidth() - scaled(80)));
    g.setFont(juce::Font(juce::FontOptions(14.0f * sf).withStyle("Bold")));
    g.setColour(currentLAF->colors.text.withAlpha(0.6f));
    g.drawText("SYNTHESIZER", getWidth() - scaled(200), brandY + scaled(2),
               scaled(110), scaled(18), juce::Justification::centredRight);
}

void MultiSynthEditor::paintModular(juce::Graphics& g)
{
    float sf = scaleHelper.getScaleFactor();
    auto ps = [&](juce::Rectangle<int> b, const juce::String& t) {
        currentLAF->paintSection(g, b, t, sf);
    };
    ps(sections.oscillators, "VCO 1 / VCO 2 / VCO 3");
    ps(sections.filter, "VCF");
    ps(sections.envelopes, "ADSR 1 / ADSR 2");
    ps(sections.scopeArea, "VCA / OUTPUT");
    ps(sections.lfo, "LFO / S&H / NOISE");
    ps(sections.character, "RING MOD / CHARACTER");
    ps(sections.arp, "ARPEGGIATOR");
    ps(sections.drive, "DRIVE"); ps(sections.chorus, "CHORUS");
    ps(sections.delay, "DELAY"); ps(sections.reverb, "REVERB");

    // Decorative patch point circles (ARP 2600 style — input and output per section)
    auto drawPatchPt = [&](int px, int py) {
        g.setColour(currentLAF->colors.sectionBorder.withAlpha(0.5f));
        g.drawEllipse(static_cast<float>(px - 5), static_cast<float>(py - 5), 10.0f, 10.0f, 1.5f);
        g.setColour(currentLAF->colors.panelBackground);
        g.fillEllipse(static_cast<float>(px - 3), static_cast<float>(py - 3), 6.0f, 6.0f);
    };

    // Multiple patch points per section (input and output)
    int midY = (sections.oscillators.getY() + sections.oscillators.getBottom()) / 2;
    drawPatchPt(sections.oscillators.getRight() + scaled(3), midY - scaled(20)); // Osc out top
    drawPatchPt(sections.oscillators.getRight() + scaled(3), midY + scaled(20)); // Osc out bottom
    drawPatchPt(sections.filter.getX() - scaled(3), midY);                       // Filter in
    drawPatchPt(sections.filter.getRight() + scaled(3), midY);                    // Filter out
    drawPatchPt(sections.envelopes.getX() - scaled(3), sections.envelopes.getCentreY()); // Env in
    drawPatchPt(sections.envelopes.getRight() + scaled(3), sections.envelopes.getCentreY()); // Env out

    // Decorative dashed signal flow line: osc -> filter with arrow
    float dashLen = 4.0f * sf;
    g.setColour(currentLAF->colors.sectionBorder.withAlpha(0.2f));
    float lineY = static_cast<float>(midY);
    float lineStartX = static_cast<float>(sections.oscillators.getRight() + scaled(8));
    float lineEndX = static_cast<float>(sections.filter.getX() - scaled(2));
    for (float x = lineStartX; x < lineEndX - dashLen; x += dashLen * 2)
        g.drawLine(x, lineY, juce::jmin(x + dashLen, lineEndX), lineY, 1.0f);

    // Arrow head indicating signal direction (osc -> filter)
    {
        juce::Path arrow;
        float arrowX = lineEndX - 2.0f;
        arrow.addTriangle(arrowX - 5.0f * sf, lineY - 3.0f * sf,
                          arrowX - 5.0f * sf, lineY + 3.0f * sf,
                          arrowX, lineY);
        g.setColour(currentLAF->colors.sectionBorder.withAlpha(0.3f));
        g.fillPath(arrow);
    }

    // Decorative dashed signal flow line: filter -> envelope with arrow
    float lineY2 = static_cast<float>(midY);
    float line2StartX = static_cast<float>(sections.filter.getRight() + scaled(8));
    float line2EndX = static_cast<float>(sections.envelopes.getX() - scaled(2));
    g.setColour(currentLAF->colors.sectionBorder.withAlpha(0.2f));
    for (float x = line2StartX; x < line2EndX - dashLen; x += dashLen * 2)
        g.drawLine(x, lineY2, juce::jmin(x + dashLen, line2EndX), lineY2, 1.0f);

    // Arrow head (filter -> envelope)
    {
        juce::Path arrow;
        float arrowX = line2EndX - 2.0f;
        arrow.addTriangle(arrowX - 5.0f * sf, lineY2 - 3.0f * sf,
                          arrowX - 5.0f * sf, lineY2 + 3.0f * sf,
                          arrowX, lineY2);
        g.setColour(currentLAF->colors.sectionBorder.withAlpha(0.3f));
        g.fillPath(arrow);
    }
}

//==============================================================================
void MultiSynthEditor::resized()
{
    scaleHelper.updateResizer();
    int w = getWidth(), h = getHeight();
    int m = scaled(kMargin);      // outer margin
    int gap = scaled(kSectionGap);
    int pad = scaled(kSectionPad);
    int titH = scaled(kSectionTitleH);
    int K = scaled(kKnobSize);    // 70px primary knob
    int S = scaled(kSmallKnob);   // 55px secondary knob
    int L = scaled(kLabelH);      // 18px label height above knob
    int cH = scaled(kComboH);
    int tH = scaled(kToggleH);
    int topBar = scaled(kTopBarH);

    // A knob with its label takes: L (label) + K (knob)
    int knobWithLabel = L + K;
    int sKnobWithLabel = L + S;

    // === SET SLIDER STYLES BEFORE LAYOUT (so placeControl knows the style) ===
    {
        auto mode = processor.getCurrentMode();
        bool useFaders = (mode == MultiSynthDSP::SynthMode::Cosmos || mode == MultiSynthDSP::SynthMode::Mono);
        bool envAsFaders = (mode != MultiSynthDSP::SynthMode::Oracle);
        auto setStyle = [](DuskSlider& s, bool asFader) {
            s.setSliderStyle(asFader ? juce::Slider::LinearVertical : juce::Slider::RotaryVerticalDrag);
        };
        setStyle(filterCutoffSlider, useFaders);
        setStyle(filterResSlider, useFaders);
        setStyle(filterHPSlider, useFaders);
        setStyle(filterEnvAmtSlider, useFaders);
        setStyle(osc1LevelSlider, useFaders);
        setStyle(osc1DetuneSlider, useFaders);
        setStyle(osc1PWSlider, useFaders);
        setStyle(osc2LevelSlider, useFaders);
        setStyle(osc2DetuneSlider, useFaders);
        setStyle(osc2SemiSlider, useFaders);
        setStyle(ampASlider, envAsFaders);
        setStyle(ampDSlider, envAsFaders);
        setStyle(ampSSlider, envAsFaders);
        setStyle(ampRSlider, envAsFaders);
        setStyle(filtASlider, envAsFaders);
        setStyle(filtDSlider, envAsFaders);
        setStyle(filtSSlider, envAsFaders);
        setStyle(filtRSlider, envAsFaders);
    }

    // === TOP BAR ===
    modeSelector.setBounds(scaled(130), scaled(8), scaled(110), cH);
    presetBox.setBounds(scaled(248), scaled(8), scaled(170), cH);
    savePresetButton.setBounds(scaled(414), scaled(8), scaled(40), cH);
    deletePresetButton.setBounds(scaled(458), scaled(8), scaled(35), cH);
    oversamplingBox.setBounds(w - scaled(200), scaled(8), scaled(50), cH);
    modMatrixButton.setBounds(w - scaled(140), scaled(8), scaled(55), cH);

    // === SECTION BOUNDS (adjust for fader height when applicable) ===
    int row1Y = topBar + gap;
    int oscW = static_cast<int>(w * 0.44f);
    int outW = static_cast<int>(w * 0.22f);
    int midW = w - oscW - outW - 4 * m;

    // Heights adapt: fader controls are taller than knobs
    bool hasFaders = (osc1LevelSlider.getSliderStyle() == juce::Slider::LinearVertical);
    int ctrlWithLabel = hasFaders ? (L + scaled(90)) : knobWithLabel; // fader=90px, knob=70px
    int sCtrlWithLabel = (ampASlider.getSliderStyle() == juce::Slider::LinearVertical)
                         ? (L + scaled(90)) : sKnobWithLabel;
    int filtH = titH + pad + ctrlWithLabel * 2 + scaled(8);
    int envH = titH + pad + sCtrlWithLabel + cH + scaled(12);
    int row1H = filtH + gap + envH;

    sections.oscillators = { m, row1Y, oscW, row1H };
    sections.filter      = { m * 2 + oscW, row1Y, midW, filtH };
    sections.envelopes   = { m * 2 + oscW, row1Y + filtH + gap, midW, envH };
    sections.scopeArea   = { m * 3 + oscW + midW, row1Y, outW, row1H };

    int row2Y = row1Y + row1H + gap;
    int row2H = titH + pad + knobWithLabel + sKnobWithLabel + scaled(10);
    int thirdW = (w - 4 * m) / 3;
    sections.lfo       = { m, row2Y, thirdW, row2H };
    sections.character = { m * 2 + thirdW, row2Y, thirdW, row2H };
    sections.arp       = { m * 3 + thirdW * 2, row2Y, thirdW, row2H };

    int row3Y = row2Y + row2H + gap;
    int row3H = h - row3Y - m;
    int fxW = (w - 5 * m) / 4;
    sections.drive  = { m, row3Y, fxW, row3H };
    sections.chorus = { m * 2 + fxW, row3Y, fxW, row3H };
    sections.delay  = { m * 3 + fxW * 2, row3Y, fxW, row3H };
    sections.reverb = { m * 4 + fxW * 3, row3Y, fxW, row3H };

    // === FADER vs KNOB BOUNDS ===
    // When a slider is LinearVertical, it needs tall+narrow bounds (width~25, height~100)
    // When RotaryVerticalDrag, it needs square bounds (KxK)
    int faderW = scaled(24);   // narrow fader width
    int faderH = scaled(85);   // tall fader height
    int faderStep = scaled(55); // spacing between faders — must be >= label width to prevent overlap

    // Smart placement: use correct bounds based on current slider style
    auto placeControl = [&](DuskSlider& s, int x, int y, int knobSz) {
        if (s.getSliderStyle() == juce::Slider::LinearVertical)
            s.setBounds(x, y, faderW, faderH);
        else
            s.setBounds(x, y, knobSz, knobSz);
    };

    // Column width depends on whether controls are faders or knobs
    auto ctrlStep = [&](DuskSlider& s, int knobSz) -> int {
        return (s.getSliderStyle() == juce::Slider::LinearVertical)
               ? faderStep : (knobSz + scaled(kKnobSpacing));
    };

    // Explicit label placement — width matches spacing to prevent overlap
    auto placeLabel = [&](juce::Label& lbl, DuskSlider& slider) {
        auto sb = slider.getBounds();
        bool isFader = (slider.getSliderStyle() == juce::Slider::LinearVertical);
        int lblW = isFader ? scaled(50) : scaled(60); // Narrower for faders
        lbl.setBounds(sb.getCentreX() - lblW / 2, sb.getY() - L, lblW, L);
    };

    // === OSCILLATORS LAYOUT ===
    {
        int x0 = sections.oscillators.getX() + pad;
        int y0 = sections.oscillators.getY() + titH + pad;
        int step1 = ctrlStep(osc1LevelSlider, K);

        // OSC 1: wave combo, then controls
        osc1WaveBox.setBounds(x0, y0, scaled(100), cH);
        int ky = y0 + cH + scaled(4) + L;
        placeControl(osc1LevelSlider,  x0,            ky, K);
        placeLabel(osc1LevelLbl, osc1LevelSlider);
        placeControl(osc1DetuneSlider, x0 + step1,    ky, K);
        placeLabel(osc1DetuneLbl, osc1DetuneSlider);
        placeControl(osc1PWSlider,     x0 + step1 * 2, ky, K);
        placeLabel(osc1PWLbl, osc1PWSlider);

        // OSC 2: wave combo, then controls
        int ctrlH = (osc1LevelSlider.getSliderStyle() == juce::Slider::LinearVertical) ? faderH : K;
        int y2 = ky + ctrlH + scaled(8);
        osc2WaveBox.setBounds(x0, y2, scaled(100), cH);
        int ky2 = y2 + cH + scaled(4) + L;
        placeControl(osc2LevelSlider,  x0,            ky2, K);
        placeLabel(osc2LevelLbl, osc2LevelSlider);
        placeControl(osc2DetuneSlider, x0 + step1,    ky2, K);
        placeLabel(osc2DetuneLbl, osc2DetuneSlider);
        placeControl(osc2SemiSlider,   x0 + step1 * 2, ky2, K);
        placeLabel(osc2SemiLbl, osc2SemiSlider);

        // Mode-specific bottom row (always small knobs)
        int ctrlH2 = (osc2LevelSlider.getSliderStyle() == juce::Slider::LinearVertical) ? faderH : K;
        int y3 = ky2 + ctrlH2 + scaled(6);
        int mKw = S + scaled(6);

        subWaveBox.setBounds(x0, y3, scaled(80), cH);
        subLevelSlider.setBounds(x0 + scaled(75), y3 + L, S, S);
        placeLabel(subLevelLbl, subLevelSlider);
        osc3WaveBox.setBounds(x0, y3, scaled(80), cH);
        osc3LevelSlider.setBounds(x0 + scaled(75), y3 + L, S, S);
        placeLabel(osc3LevelLbl, osc3LevelSlider);
        noiseLevelSlider.setBounds(x0 + mKw * 2 + scaled(30), y3 + L, S, S);
        placeLabel(noiseLevelLbl, noiseLevelSlider);
        crossModSlider.setBounds(x0 + mKw * 3 + scaled(30), y3 + L, S, S);
        placeLabel(crossModLbl, crossModSlider);
        ringModSlider.setBounds(x0 + mKw * 3 + scaled(30), y3 + L, S, S);
        placeLabel(ringModLbl, ringModSlider);
        fmAmountSlider.setBounds(x0 + mKw * 4 + scaled(30), y3 + L, S, S);
        placeLabel(fmAmountLbl, fmAmountSlider);
        hardSyncButton.setBounds(x0 + mKw * 5 + scaled(30), y3 + L + scaled(12), scaled(50), tH);

        // Oracle poly-mod knobs (positioned after noise, replacing crossMod)
        int pmX = x0 + mKw * 2 + scaled(30);
        pmFEnvOscASlider.setBounds(pmX,            y3 + L, S, S);
        placeLabel(pmFEnvOscALbl, pmFEnvOscASlider);
        pmFEnvFiltSlider.setBounds(pmX + mKw,      y3 + L, S, S);
        placeLabel(pmFEnvFiltLbl, pmFEnvFiltSlider);
        pmOscBOscASlider.setBounds(pmX + mKw * 2,  y3 + L, S, S);
        placeLabel(pmOscBOscALbl, pmOscBOscASlider);
        pmOscBPWMSlider.setBounds(pmX + mKw * 3,   y3 + L, S, S);
        placeLabel(pmOscBPWMLbl, pmOscBPWMSlider);
    }

    // === FILTER LAYOUT ===
    {
        int x0 = sections.filter.getX() + pad;
        int y0 = sections.filter.getY() + titH + pad + L;
        int fStep = ctrlStep(filterCutoffSlider, K);

        placeControl(filterCutoffSlider, x0,           y0, K);
        placeLabel(filterCutoffLbl, filterCutoffSlider);
        placeControl(filterResSlider,    x0 + fStep,   y0, K);
        placeLabel(filterResLbl, filterResSlider);

        int ctrlH1 = (filterCutoffSlider.getSliderStyle() == juce::Slider::LinearVertical) ? faderH : K;
        int y2 = y0 + ctrlH1 + scaled(6) + L;
        placeControl(filterHPSlider,     x0,           y2, K);
        placeLabel(filterHPLbl, filterHPSlider);
        placeControl(filterEnvAmtSlider, x0 + fStep,   y2, K);
        placeLabel(filterEnvAmtLbl, filterEnvAmtSlider);

        // Filter response mini-display below filter knobs
        int ctrlH2 = (filterHPSlider.getSliderStyle() == juce::Slider::LinearVertical) ? faderH : K;
        int frY = y2 + ctrlH2 + scaled(4);
        filterResponseDisplay.setBounds(x0, frY, sections.filter.getWidth() - pad * 2, scaled(35));
    }

    // === ENVELOPES LAYOUT (2 groups of 4 ADSR) ===
    {
        int x0 = sections.envelopes.getX() + pad;
        int y0 = sections.envelopes.getY() + titH + scaled(18) + L;
        int eStep = ctrlStep(ampASlider, S);
        int halfW = sections.envelopes.getWidth() / 2 - pad;

        // Amp ADSR
        placeControl(ampASlider, x0,             y0, S);
        placeLabel(ampALbl, ampASlider);
        placeControl(ampDSlider, x0 + eStep,     y0, S);
        placeLabel(ampDLbl, ampDSlider);
        placeControl(ampSSlider, x0 + eStep * 2, y0, S);
        placeLabel(ampSLbl, ampSSlider);
        placeControl(ampRSlider, x0 + eStep * 3, y0, S);
        placeLabel(ampRLbl, ampRSlider);
        int envCtrlH = (ampASlider.getSliderStyle() == juce::Slider::LinearVertical) ? faderH : S;
        ampCurveBox.setBounds(x0, y0 + envCtrlH + scaled(4), scaled(100), cH);

        // Filter ADSR
        int fx = x0 + halfW + scaled(8);
        placeControl(filtASlider, fx,             y0, S);
        placeLabel(filtALbl, filtASlider);
        placeControl(filtDSlider, fx + eStep,     y0, S);
        placeLabel(filtDLbl, filtDSlider);
        placeControl(filtSSlider, fx + eStep * 2, y0, S);
        placeLabel(filtSLbl, filtSSlider);
        placeControl(filtRSlider, fx + eStep * 3, y0, S);
        placeLabel(filtRLbl, filtRSlider);
        filtCurveBox.setBounds(fx, y0 + envCtrlH + scaled(4), scaled(100), cH);
    }

    // === SCOPE + OUTPUT ===
    {
        int x0 = sections.scopeArea.getX() + pad;
        int y0 = sections.scopeArea.getY() + titH;
        int sw = sections.scopeArea.getWidth() - pad * 2;
        int scopeH = scaled(120);

        waveformDisplay.setBounds(x0, y0, sw, scopeH);

        int my = y0 + scopeH + scaled(10) + L;
        int volPanW = juce::jmin(K, (sw - scaled(8) * 3) / 4);
        masterTuneSlider.setBounds(x0, my, volPanW, volPanW);
        placeLabel(masterTuneLbl, masterTuneSlider);
        masterVolSlider.setBounds(x0 + (volPanW + scaled(8)), my, volPanW, volPanW);
        placeLabel(masterVolLbl, masterVolSlider);
        masterPanSlider.setBounds(x0 + (volPanW + scaled(8)) * 2, my, volPanW, volPanW);
        placeLabel(masterPanLbl, masterPanSlider);
        stereoWidthSlider.setBounds(x0 + (volPanW + scaled(8)) * 3, my, volPanW, volPanW);
        placeLabel(stereoWidthLbl, stereoWidthSlider);

        int meterY = my + volPanW + scaled(12);
        int meterH = sections.scopeArea.getBottom() - meterY - pad;
        if (meterH < scaled(30)) meterH = scaled(30);
        int meterX = x0 + (sw - scaled(kMeterW) * 2 - scaled(4)) / 2;
        outputMeterL.setBounds(meterX, meterY, scaled(kMeterW), meterH);
        outputMeterR.setBounds(meterX + scaled(kMeterW) + scaled(4), meterY, scaled(kMeterW), meterH);
    }

    // === LFO LAYOUT ===
    {
        int x0 = sections.lfo.getX() + pad;
        int y0 = sections.lfo.getY() + titH + pad;
        int kw = K + scaled(6);

        // LFO1 row
        lfo1ShapeBox.setBounds(x0, y0, scaled(70), cH);
        lfo1SyncButton.setBounds(x0 + scaled(75), y0, scaled(45), cH);
        lfo1RateSlider.setBounds(x0 + scaled(130), y0 + cH - K + L, K, K);
        placeLabel(lfo1RateLbl, lfo1RateSlider);
        lfo1FadeSlider.setBounds(x0 + scaled(130) + kw, y0 + cH - K + L, K, K);
        placeLabel(lfo1FadeLbl, lfo1FadeSlider);

        // LFO2 row
        int y2 = y0 + knobWithLabel + scaled(8);
        lfo2ShapeBox.setBounds(x0, y2, scaled(70), cH);
        lfo2SyncButton.setBounds(x0 + scaled(75), y2, scaled(45), cH);
        lfo2RateSlider.setBounds(x0 + scaled(130), y2 + cH - K + L, K, K);
        placeLabel(lfo2RateLbl, lfo2RateSlider);
        lfo2FadeSlider.setBounds(x0 + scaled(130) + kw, y2 + cH - K + L, K, K);
        placeLabel(lfo2FadeLbl, lfo2FadeSlider);

        // S&H Rate knob (after LFO2 controls)
        shRateSlider.setBounds(x0 + scaled(130) + kw * 2, y2 + cH - S + L, S, S);
        placeLabel(shRateLbl, shRateSlider);
    }

    // === CHARACTER / UNISON ===
    {
        int x0 = sections.character.getX() + pad;
        int y0 = sections.character.getY() + titH + pad + L;
        int ckw = S + scaled(8);

        portaSlider.setBounds(x0, y0, S, S);
        placeLabel(portaLbl, portaSlider);
        analogSlider.setBounds(x0 + ckw, y0, S, S);
        placeLabel(analogLbl, analogSlider);
        vintageSlider.setBounds(x0 + ckw * 2, y0, S, S);
        placeLabel(vintageLbl, vintageSlider);
        velSensSlider.setBounds(x0 + ckw * 3, y0, S, S);
        placeLabel(velSensLbl, velSensSlider);
        velCurveBox.setBounds(x0 + ckw * 3, y0 + S + scaled(2), scaled(70), cH);
        legatoButton.setBounds(x0 + ckw * 4, y0 + scaled(12), scaled(55), tH);
        glideModeBox.setBounds(x0 + ckw * 4, y0 + scaled(12) + tH + scaled(4), scaled(65), cH);

        int y2 = y0 + S + scaled(14) + L;
        unisonVoicesSlider.setBounds(x0, y2, S, S);
        placeLabel(unisonVoicesLbl, unisonVoicesSlider);
        unisonDetuneSlider.setBounds(x0 + ckw, y2, S, S);
        placeLabel(unisonDetuneLbl, unisonDetuneSlider);
        unisonSpreadSlider.setBounds(x0 + ckw * 2, y2, S, S);
        placeLabel(unisonSpreadLbl, unisonSpreadSlider);

        // Cosmos chorus selector (visible only in Cosmos mode)
        cosmosChorusBox.setBounds(x0 + ckw * 3 + scaled(8), y2, scaled(70), cH);
    }

    // === ARPEGGIATOR ===
    {
        int x0 = sections.arp.getX() + pad;
        int y0 = sections.arp.getY() + titH + pad;
        int ckw = S + scaled(8);

        arpOnButton.setBounds(x0, y0, scaled(35), tH);
        arpModeBox.setBounds(x0 + scaled(40), y0, scaled(85), cH);
        arpRateBox.setBounds(x0 + scaled(130), y0, scaled(65), cH);

        int ky = y0 + cH + scaled(8) + L;
        arpOctaveSlider.setBounds(x0, ky, S, S);
        placeLabel(arpOctaveLbl, arpOctaveSlider);
        arpGateSlider.setBounds(x0 + ckw, ky, S, S);
        placeLabel(arpGateLbl, arpGateSlider);
        arpSwingSlider.setBounds(x0 + ckw * 2, ky, S, S);
        placeLabel(arpSwingLbl, arpSwingSlider);
        arpLatchButton.setBounds(x0 + ckw * 3, ky + scaled(12), scaled(50), tH);
        arpVelModeBox.setBounds(x0 + ckw * 3, ky + scaled(36), scaled(80), cH);

        // Arp step mute buttons (16 small toggles in a row)
        int stepY = sections.arp.getBottom() - scaled(22);
        int stepBtnW = (sections.arp.getWidth() - pad * 2) / 16;
        for (int i = 0; i < 16; ++i)
            arpStepButtons[i].setBounds(sections.arp.getX() + pad + i * stepBtnW, stepY, stepBtnW, scaled(18));
    }

    // === EFFECTS STRIP ===
    auto layoutFX = [&](juce::Rectangle<int> bounds, juce::ToggleButton& onBtn,
                        std::initializer_list<std::pair<DuskSlider*, juce::Label*>> knobsAndLabels,
                        std::initializer_list<juce::Component*> extras)
    {
        int fx = bounds.getX() + pad;
        int fy = bounds.getY() + titH;

        onBtn.setBounds(fx, fy, scaled(35), tH);
        int ex = fx + scaled(40);
        for (auto* e : extras)
        {
            e->setBounds(ex, fy, scaled(45), cH);
            ex += scaled(50);
        }

        int ky = fy + tH + scaled(6) + L;
        int fkw = S + scaled(6);
        int i = 0;
        for (auto& [k, lbl] : knobsAndLabels)
        {
            k->setBounds(fx + i * fkw, ky, S, S);
            placeLabel(*lbl, *k);
            i++;
        }
    };

    layoutFX(sections.drive, driveOnButton,
             {{&driveAmtSlider, &driveAmtLbl}, {&driveMixSlider, &driveMixLbl}}, {&driveTypeBox});
    layoutFX(sections.chorus, chorusOnButton,
             {{&chorusRateSlider, &chorusRateLbl}, {&chorusDepthSlider, &chorusDepthLbl}, {&chorusMixSlider, &chorusMixLbl}}, {});
    layoutFX(sections.delay, delayOnButton,
             {{&delayTimeSlider, &delayTimeLbl}, {&delayFBSlider, &delayFBLbl}, {&delayMixSlider, &delayMixLbl}},
             {&delaySyncButton, &delayPPButton, &delayTapeButton, &delayDivBox});
    layoutFX(sections.reverb, reverbOnButton,
             {{&reverbSizeSlider, &reverbSizeLbl}, {&reverbDecaySlider, &reverbDecayLbl},
              {&reverbDampSlider, &reverbDampLbl}, {&reverbMixSlider, &reverbMixLbl},
              {&reverbPDSlider, &reverbPDLbl}}, {});

    // Overlays
    modMatrixOverlay.setBounds(getLocalBounds());
    supportersOverlay.setBounds(getLocalBounds());

    // (slider styles already set at the top of resized)
    juce::ignoreUnused(placeLabel, placeControl, ctrlStep);
}

//==============================================================================
// Slider style helpers
void MultiSynthEditor::setAllSlidersToKnobs()
{
    for (auto* child : getChildren())
        if (auto* slider = dynamic_cast<DuskSlider*>(child))
            slider->setSliderStyle(juce::Slider::RotaryVerticalDrag);
}

void MultiSynthEditor::setSliderAsFader(DuskSlider& s)
{
    s.setSliderStyle(juce::Slider::LinearVertical);
}

void MultiSynthEditor::layoutSharedLowerStrip() {}

//==============================================================================
// Preset management

void MultiSynthEditor::refreshPresetList()
{
    presetBox.clear(juce::dontSendNotification);

    auto& factory = MultiSynthProcessor::getFactoryPresetNames();
    factoryPresetCount = factory.size();

    // Add factory presets with mode group headers
    // Cosmos: 0-4, Oracle: 5-9, Mono: 10-14, Modular: 15-19, New: 20-35, Init: 36-39
    presetBox.addSectionHeading("Cosmos");
    for (int i = 0; i <= 4; ++i)
        presetBox.addItem(factory[i], i + 1);

    presetBox.addSectionHeading("Oracle");
    for (int i = 5; i <= 9; ++i)
        presetBox.addItem(factory[i], i + 1);

    presetBox.addSectionHeading("Mono");
    for (int i = 10; i <= 14; ++i)
        presetBox.addItem(factory[i], i + 1);

    presetBox.addSectionHeading("Modular");
    for (int i = 15; i <= 19; ++i)
        presetBox.addItem(factory[i], i + 1);

    presetBox.addSectionHeading("Multi-Mode");
    for (int i = 20; i <= 35; ++i)
        presetBox.addItem(factory[i], i + 1);

    presetBox.addSectionHeading("Init");
    for (int i = 36; i < factory.size(); ++i)
        presetBox.addItem(factory[i], i + 1);

    // User presets
    if (userPresetManager)
    {
        auto userPresets = userPresetManager->loadUserPresets();
        if (!userPresets.empty())
        {
            presetBox.addSectionHeading("User Presets");
            for (int i = 0; i < static_cast<int>(userPresets.size()); ++i)
                presetBox.addItem(userPresets[static_cast<size_t>(i)].name,
                                  factoryPresetCount + i + 1);
        }
    }
}

void MultiSynthEditor::saveUserPreset()
{
    if (!userPresetManager) return;

    auto callback = [safeThis = juce::Component::SafePointer(this)](const juce::String& name)
    {
        if (!safeThis || name.isEmpty()) return;
        auto state = safeThis->processor.getAPVTS().copyState();
        if (safeThis->userPresetManager->saveUserPreset(name, state))
            safeThis->refreshPresetList();
    };

    // Show a simple alert window to get the name
    auto* aw = new juce::AlertWindow("Save Preset", "Enter a name for the preset:",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", "", "Preset name:");
    aw->addButton("Save", 1);
    aw->addButton("Cancel", 0);

    aw->enterModalState(true, juce::ModalCallbackFunction::create(
        [aw, callback](int result) {
            if (result == 1)
                callback(aw->getTextEditorContents("name"));
            delete aw;
        }), false);
}

void MultiSynthEditor::loadUserPreset(const juce::String& name)
{
    if (!userPresetManager) return;
    auto state = userPresetManager->loadUserPreset(name);
    if (state.isValid())
        processor.getAPVTS().replaceState(state);
}

void MultiSynthEditor::deleteUserPreset()
{
    if (!userPresetManager) return;
    auto name = presetBox.getText();
    if (name.isEmpty()) return;

    // Only delete user presets (IDs above factoryPresetCount)
    int id = presetBox.getSelectedId();
    if (id <= factoryPresetCount) return; // Can't delete factory presets

    userPresetManager->deleteUserPreset(name);
    refreshPresetList();
}

// Per-mode layout refinements — called after the shared layout positions all controls.
// These adjust section titles in paint() via the section names used there,
// and ensure mode-specific controls are positioned correctly.

void MultiSynthEditor::layoutCosmos()
{
    // Cosmos: Juno-60 style — faders for osc/filter/env
    // The shared layout already handles fader bounds via placeControl()
    // Nothing additional needed — the paintCosmos() handles the orange header
}

void MultiSynthEditor::layoutOracle()
{
    // Oracle: Prophet-5 style — all rotary knobs, wood cheeks
    // Inset the oscillator and output sections to leave room for wood cheeks
    // (the painting handles the wood, but we could inset controls here)
}

void MultiSynthEditor::layoutMono()
{
    // Mono: SH-2 style — mix of knobs and faders
    // The shared layout handles this via the style switching
}

void MultiSynthEditor::layoutModular()
{
    // Modular: ARP 2600 style — knobs + patch points
    // The patch points are decorative only (painted in paintModular)
}

//==============================================================================
void MultiSynthEditor::mouseDown(const juce::MouseEvent& e)
{
    // Click on title shows supporters overlay
    auto titleBounds = juce::Rectangle<int>(scaled(8), 0, scaled(120), scaled(kTopBarH));
    if (titleBounds.contains(e.getPosition()))
    {
        supportersOverlay.setVisible(true);
        supportersOverlay.toFront(true);
    }
}

//==============================================================================
void MultiSynthEditor::timerCallback()
{
    outputMeterL.setLevel(processor.outputLevelL.load(std::memory_order_relaxed));
    outputMeterR.setLevel(processor.outputLevelR.load(std::memory_order_relaxed));

    // Oscilloscope — read ring buffer then trigger on rising zero-crossing
    int wp = processor.scopeWritePos.load(std::memory_order_relaxed);
    float tempBuf[MultiSynthProcessor::kScopeSize];
    for (int i = 0; i < MultiSynthProcessor::kScopeSize; ++i)
        tempBuf[i] = processor.scopeBuffer[(wp + i) % MultiSynthProcessor::kScopeSize];

    // Find first rising zero-crossing for stable display
    int triggerPoint = 0;
    int searchLen = MultiSynthProcessor::kScopeSize / 2; // Search first half
    for (int i = 1; i < searchLen; ++i)
    {
        if (tempBuf[i - 1] <= 0.0f && tempBuf[i] > 0.0f)
        {
            triggerPoint = i;
            break;
        }
    }

    // Display from trigger point, up to remaining samples
    int displayLen = MultiSynthProcessor::kScopeSize - triggerPoint;
    displayLen = juce::jmin(displayLen, MultiSynthProcessor::kScopeSize / 2);
    waveformDisplay.updateBuffer(tempBuf + triggerPoint, displayLen);
    waveformDisplay.repaint();

    // Update filter response display
    float fc = *processor.getAPVTS().getRawParameterValue(ParamIDs::FILTER_CUTOFF);
    float fr = *processor.getAPVTS().getRawParameterValue(ParamIDs::FILTER_RESONANCE);
    filterResponseDisplay.setParameters(fc, fr, 44100.0f);
    filterResponseDisplay.setAccentColor(currentLAF ? currentLAF->colors.accent : juce::Colour(0xFF6070DD));
    filterResponseDisplay.setBackgroundColor(currentLAF ? currentLAF->colors.sectionBackground : juce::Colour(0xFF1A1C2E));

    // Repaint top bar for MIDI activity indicators
    repaint(0, 0, getWidth(), scaled(kTopBarH));

    updateModeVisibility();
}

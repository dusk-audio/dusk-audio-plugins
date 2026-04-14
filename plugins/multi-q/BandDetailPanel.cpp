#include "BandDetailPanel.h"
#include "MultiQ.h"
#include "F6KnobLookAndFeel.h"

// Static instance of F6 knob look and feel (shared by all instances)
static F6KnobLookAndFeel f6KnobLookAndFeel;

BandDetailPanel::BandDetailPanel(MultiQ& p)
    : processor(p)
{
    setWantsKeyboardFocus(true);
    setupKnobs();
    setupMatchControls();
    updateAttachments();
    updateControlsForBandType();

    // Listen for dynamics enable changes to update opacity
    // Also listen for band enabled changes to update the indicator
    for (int i = 1; i <= 8; ++i)
    {
        processor.parameters.addParameterListener(ParamIDs::bandDynEnabled(i), this);
        processor.parameters.addParameterListener(ParamIDs::bandEnabled(i), this);
    }
}

BandDetailPanel::~BandDetailPanel()
{
    // Remove parameter listeners
    for (int i = 1; i <= 8; ++i)
    {
        processor.parameters.removeParameterListener(ParamIDs::bandDynEnabled(i), this);
        processor.parameters.removeParameterListener(ParamIDs::bandEnabled(i), this);
    }

    // Release LookAndFeel references before knobs are destroyed
    if (freqKnob) freqKnob->setLookAndFeel(nullptr);
    if (gainKnob) gainKnob->setLookAndFeel(nullptr);
    if (qKnob) qKnob->setLookAndFeel(nullptr);
    if (thresholdKnob) thresholdKnob->setLookAndFeel(nullptr);
    if (attackKnob) attackKnob->setLookAndFeel(nullptr);
    if (releaseKnob) releaseKnob->setLookAndFeel(nullptr);
    if (rangeKnob) rangeKnob->setLookAndFeel(nullptr);
    if (ratioKnob) ratioKnob->setLookAndFeel(nullptr);
    if (panKnob) panKnob->setLookAndFeel(nullptr);
}

void BandDetailPanel::setupBandButtons()
{
    // No longer using TextButtons - drawing manually in paint()
}

void BandDetailPanel::updateBandButtonColors()
{
    juce::Colour bandColor = getBandColor(selectedBand);
    freqKnob->setColour(juce::Slider::rotarySliderFillColourId, bandColor);
    qKnob->setColour(juce::Slider::rotarySliderFillColourId, bandColor);
    gainKnob->setColour(juce::Slider::rotarySliderFillColourId, bandColor);

    repaint();
}

void BandDetailPanel::setupKnobs()
{
    auto setupRotaryKnob = [this](std::unique_ptr<juce::Slider>& knob) {
        knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                               juce::Slider::NoTextBox);
        knob->setLookAndFeel(&f6KnobLookAndFeel);
        knob->setColour(juce::Slider::rotarySliderFillColourId, getBandColor(selectedBand));
        knob->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF404040));
        knob->onValueChange = [this]() { repaint(); };
        addAndMakeVisible(knob.get());
    };

    // EQ knobs
    setupRotaryKnob(freqKnob);
    freqKnob->setTooltip("Frequency: Center frequency of this band (20 Hz - 20 kHz)");
    setupRotaryKnob(gainKnob);
    gainKnob->setTooltip("Gain: Boost or cut at this frequency (-24 to +24 dB)");
    gainKnob->setDoubleClickReturnValue(true, 0.0);
    setupRotaryKnob(qKnob);
    qKnob->setTooltip("Q: Bandwidth/resonance - higher values = narrower bandwidth (0.1 - 100)");
    qKnob->setDoubleClickReturnValue(true, 0.707);

    // Slope selector for HPF/LPF
    slopeSelector = std::make_unique<juce::ComboBox>();
    slopeSelector->addItem("6 dB/oct", 1);
    slopeSelector->addItem("12 dB/oct", 2);
    slopeSelector->addItem("18 dB/oct", 3);
    slopeSelector->addItem("24 dB/oct", 4);
    slopeSelector->addItem("36 dB/oct", 5);
    slopeSelector->addItem("48 dB/oct", 6);
    slopeSelector->addItem("72 dB/oct", 7);
    slopeSelector->addItem("96 dB/oct", 8);
    slopeSelector->setTooltip("Filter slope: Steeper = sharper cutoff (6-96 dB/octave)");
    addAndMakeVisible(slopeSelector.get());

    // Dynamics knobs (use orange for dynamics section)
    auto setupDynKnob = [this](std::unique_ptr<juce::Slider>& knob) {
        knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                               juce::Slider::NoTextBox);
        knob->setLookAndFeel(&f6KnobLookAndFeel);
        knob->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFFff8844));
        knob->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF404040));
        knob->onValueChange = [this]() { repaint(); };
        addAndMakeVisible(knob.get());
    };

    setupDynKnob(thresholdKnob);
    thresholdKnob->setTooltip("Threshold: Level where dynamic gain reduction starts (-60 to +12 dB)");
    thresholdKnob->setDoubleClickReturnValue(true, 0.0);
    setupDynKnob(attackKnob);
    attackKnob->setTooltip("Attack: How fast gain reduction responds to level increases (0.1 - 500 ms)");
    attackKnob->setDoubleClickReturnValue(true, 10.0);
    setupDynKnob(releaseKnob);
    releaseKnob->setTooltip("Release: How fast gain returns after level drops (10 - 5000 ms)");
    releaseKnob->setDoubleClickReturnValue(true, 100.0);
    setupDynKnob(rangeKnob);
    rangeKnob->setTooltip("Range: Maximum amount of dynamic gain reduction (0 - 24 dB)");
    rangeKnob->setDoubleClickReturnValue(true, 12.0);
    setupDynKnob(ratioKnob);
    ratioKnob->setTooltip("Ratio: Compression ratio (1:1 = no compression, 20:1 = heavy limiting)");
    ratioKnob->setDoubleClickReturnValue(true, 4.0);

    // Toggle buttons
    dynButton = std::make_unique<juce::TextButton>("DYN");
    dynButton->setClickingTogglesState(true);
    dynButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF353535));
    dynButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF4488ff));
    dynButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF888888));
    dynButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    dynButton->setTooltip("Enable per-band dynamics processing (Shortcut: D)");
    addAndMakeVisible(dynButton.get());

    soloButton = std::make_unique<juce::TextButton>("SOLO");
    soloButton->setClickingTogglesState(true);
    soloButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF353535));
    soloButton->setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    soloButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF888888));
    soloButton->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    soloButton->setTooltip("Solo this EQ band (mute all other bands) (Shortcut: S)");
    soloButton->onClick = [this]() {
        int band = selectedBand.load();
        if (soloButton->getToggleState())
            processor.setSoloedBand(band);
        else
            processor.setSoloedBand(-1);  // No solo
    };
    addAndMakeVisible(soloButton.get());

    // Invert button (flips EQ gain boost↔cut)
    invertButton = std::make_unique<juce::TextButton>("INV");
    invertButton->setClickingTogglesState(true);
    invertButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF353535));
    invertButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFcc6622));
    invertButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF888888));
    invertButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    invertButton->setTooltip("Invert EQ gain (boost becomes cut)");
    invertButton->onClick = [this]() { repaint(); };
    addAndMakeVisible(invertButton.get());

    // Phase Invert button (flips polarity of band effect)
    phaseInvertButton = std::make_unique<juce::TextButton>(juce::String::charToString(0x00D8));
    phaseInvertButton->setClickingTogglesState(true);
    phaseInvertButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF353535));
    phaseInvertButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFcc2266));
    phaseInvertButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF888888));
    phaseInvertButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    phaseInvertButton->setTooltip("Flip polarity of this band's effect (phase invert)");
    addAndMakeVisible(phaseInvertButton.get());

    // Pan knob (stereo placement of band EQ effect)
    panKnob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                              juce::Slider::NoTextBox);
    panKnob->setLookAndFeel(&f6KnobLookAndFeel);
    panKnob->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF66aadd));
    panKnob->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF404040));
    panKnob->setTooltip("Pan band's EQ effect (L/R placement)");
    panKnob->setDoubleClickReturnValue(true, 0.0);
    panKnob->onValueChange = [this]() { repaint(); };
    addAndMakeVisible(panKnob.get());

    // Per-band channel routing selector (placed in PAN section)
    routingSelector = std::make_unique<juce::ComboBox>();
    routingSelector->addItem("Global", 1);
    routingSelector->addItem("Stereo", 2);
    routingSelector->addItem("Left", 3);
    routingSelector->addItem("Right", 4);
    routingSelector->addItem("Mid", 5);
    routingSelector->addItem("Side", 6);
    routingSelector->setTooltip("Per-band channel routing: Global follows the global mode, or override per band");
    addAndMakeVisible(routingSelector.get());
}

void BandDetailPanel::setupMatchControls()
{
    // Learn Current button — toggles learning on/off
    learnCurrentButton.setTooltip("Learn the spectrum of your current audio (play audio while learning)");
    learnCurrentButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a3a4a));
    learnCurrentButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff88aacc));
    learnCurrentButton.onClick = [this]() {
        if (processor.isMatchLearningCurrentOrPending())
        {
            // Stop current learning
            processor.stopLearning();
            stopTimer();
            updateLearnButtonStates();
        }
        else
        {
            // Start current learning (cancels any other pending learning)
            processor.startLearnCurrent();
            learnCurrentButton.setButtonText("Stop");
            learnCurrentButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffcc4444));
            // Reset reference button to its non-learning state
            learnReferenceButton.setButtonText(processor.hasMatchReferenceSpectrum() ? "Reference *" : "Learn Reference");
            learnReferenceButton.setColour(juce::TextButton::buttonColourId,
                processor.hasMatchReferenceSpectrum() ? juce::Colour(0xff44bb66) : juce::Colour(0xff2a4a3a));
            startTimer(100);
        }
    };
    learnCurrentButton.setVisible(false);
    addAndMakeVisible(learnCurrentButton);

    // Learn Reference button — toggles learning on/off
    learnReferenceButton.setTooltip("Learn the spectrum of the reference audio (play reference while learning)");
    learnReferenceButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a4a3a));
    learnReferenceButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff88ccaa));
    learnReferenceButton.onClick = [this]() {
        if (processor.isMatchLearningReferenceOrPending())
        {
            // Stop reference learning
            processor.stopLearning();
            stopTimer();
            updateLearnButtonStates();
        }
        else
        {
            // Start reference learning (cancels any other pending learning)
            processor.startLearnReference();
            learnReferenceButton.setButtonText("Stop");
            learnReferenceButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffcc4444));
            // Reset current button to its non-learning state
            learnCurrentButton.setButtonText(processor.hasMatchCurrentSpectrum() ? "Current *" : "Learn Current");
            learnCurrentButton.setColour(juce::TextButton::buttonColourId,
                processor.hasMatchCurrentSpectrum() ? juce::Colour(0xff4488cc) : juce::Colour(0xff2a3a4a));
            startTimer(100);
        }
    };
    learnReferenceButton.setVisible(false);
    addAndMakeVisible(learnReferenceButton);

    // Match (compute) button
    matchComputeButton.setTooltip("Compute correction curve from learned spectra and apply FIR filter");
    matchComputeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5a4030));
    matchComputeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffccaa88));
    matchComputeButton.setEnabled(false);
    matchComputeButton.onClick = [this]() {
        if (processor.computeMatchCorrection())
        {
            // Correction FIR computed and loaded — stay in Match mode.
            // The user can hear the match correction applied via convolution.
            // Use "Transfer to Digital" button to convert to parametric bands.
            matchComputeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffcc8844));
            matchComputeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            if (auto* parent = getParentComponent())
                parent->repaint();
        }
    };
    matchComputeButton.setVisible(false);
    addAndMakeVisible(matchComputeButton);

    // Clear button
    matchClearButton.setTooltip("Clear all learned spectra and correction curve");
    matchClearButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a4a4a));
    matchClearButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff999999));
    matchClearButton.onClick = [this]() {
        processor.clearMatchEQ();
        if (onMatchCleared) onMatchCleared();
        learnCurrentButton.setButtonText("Learn Current");
        learnCurrentButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a3a4a));
        learnCurrentButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff88aacc));
        learnReferenceButton.setButtonText("Learn Reference");
        learnReferenceButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a4a3a));
        learnReferenceButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff88ccaa));
        matchComputeButton.setEnabled(false);
        matchComputeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5a4030));
        matchComputeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffccaa88));
    };
    matchClearButton.setVisible(false);
    addAndMakeVisible(matchClearButton);

    // Limit Boost / Limit Cut toggles
    limitBoostButton.setClickingTogglesState(true);
    limitBoostButton.setTooltip("Limit maximum boost to +20 dB");
    limitBoostButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
    limitBoostButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff5588aa));
    limitBoostButton.setVisible(false);
    addAndMakeVisible(limitBoostButton);
    limitBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::matchLimitBoost, limitBoostButton);

    limitCutButton.setClickingTogglesState(true);
    limitCutButton.setTooltip("Limit maximum cut to -20 dB");
    limitCutButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
    limitCutButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff5588aa));
    limitCutButton.setVisible(false);
    addAndMakeVisible(limitCutButton);
    limitCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::matchLimitCut, limitCutButton);

    // Apply slider (-100% to +100%)
    matchApplySlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                       juce::Slider::TextBoxRight);
    matchApplySlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    matchApplySlider->setTooltip("Apply amount: 100% = full correction, 0% = bypass, negative = inverse");
    matchApplySlider->setVisible(false);
    matchApplySlider->onValueChange = [this]() {
        if (processor.hasMatchCorrectionCurve())
            processor.computeMatchCorrection();
    };
    addAndMakeVisible(matchApplySlider.get());
    matchApplyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::matchApply, *matchApplySlider);

    // Smoothing slider (1-24 semitones)
    matchSmoothingSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                           juce::Slider::TextBoxRight);
    matchSmoothingSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    matchSmoothingSlider->setTooltip("Smoothing: wider = smoother correction (in semitones, 12 = 1 octave)");
    matchSmoothingSlider->setVisible(false);
    matchSmoothingSlider->onValueChange = [this]() {
        if (processor.hasMatchCorrectionCurve())
            processor.computeMatchCorrection();
    };
    addAndMakeVisible(matchSmoothingSlider.get());
    matchSmoothingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::matchSmoothing, *matchSmoothingSlider);

    // Learning status label
    learningStatusLabel.setText("", juce::dontSendNotification);
    learningStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcccccc));
    learningStatusLabel.setFont(juce::FontOptions(10.0f));
    learningStatusLabel.setVisible(false);
    addAndMakeVisible(learningStatusLabel);
}

void BandDetailPanel::updateLearnButtonStates()
{
    // Current button
    learnCurrentButton.setButtonText(processor.hasMatchCurrentSpectrum() ? "Current *" : "Learn Current");
    learnCurrentButton.setColour(juce::TextButton::buttonColourId,
        processor.hasMatchCurrentSpectrum() ? juce::Colour(0xff4488cc) : juce::Colour(0xff2a3a4a));
    // Reference button
    learnReferenceButton.setButtonText(processor.hasMatchReferenceSpectrum() ? "Reference *" : "Learn Reference");
    learnReferenceButton.setColour(juce::TextButton::buttonColourId,
        processor.hasMatchReferenceSpectrum() ? juce::Colour(0xff44bb66) : juce::Colour(0xff2a4a3a));
    // Match button enable
    matchComputeButton.setEnabled(processor.hasMatchCurrentSpectrum() && processor.hasMatchReferenceSpectrum());
    learningStatusLabel.setText("", juce::dontSendNotification);
}

void BandDetailPanel::timerCallback()
{
    if (processor.isMatchLearningOrPending())
    {
        int frames = processor.getMatchLearningFrameCount();
        learningStatusLabel.setText(juce::String(frames) + " frames", juce::dontSendNotification);
    }
    else
    {
        stopTimer();
        updateLearnButtonStates();
    }
}

void BandDetailPanel::setMatchMode(bool isMatch)
{
    if (matchMode == isMatch)
        return;

    matchMode = isMatch;

    // In match mode, hide ALL non-match controls (EQ + dynamics + pan)
    bool showDyn = !matchMode;
    bool showEQ = !matchMode;
    freqKnob->setVisible(showEQ);
    qKnob->setVisible(showEQ);
    gainKnob->setVisible(showEQ);
    slopeSelector->setVisible(showEQ);
    invertButton->setVisible(showEQ);
    phaseInvertButton->setVisible(showEQ);
    panKnob->setVisible(showEQ);
    routingSelector->setVisible(showEQ);
    thresholdKnob->setVisible(showDyn);
    attackKnob->setVisible(showDyn);
    releaseKnob->setVisible(showDyn);
    rangeKnob->setVisible(showDyn);
    ratioKnob->setVisible(showDyn);
    dynButton->setVisible(showDyn);
    soloButton->setVisible(showEQ);

    learnCurrentButton.setVisible(matchMode);
    learnReferenceButton.setVisible(matchMode);
    matchComputeButton.setVisible(matchMode);
    matchClearButton.setVisible(matchMode);
    limitBoostButton.setVisible(matchMode);
    limitCutButton.setVisible(matchMode);
    matchApplySlider->setVisible(matchMode);
    matchSmoothingSlider->setVisible(matchMode);
    learningStatusLabel.setVisible(matchMode);

    // Resync match control states from processor when showing the match section
    if (matchMode)
    {
        updateLearnButtonStates();

        // If learning is in progress, restart the timer to update frame count
        if (processor.isMatchLearningOrPending())
            startTimer(100);
        else
            stopTimer();
    }
    else
    {
        stopTimer();
    }

    resized();
    repaint();
}

void BandDetailPanel::setSelectedBand(int bandIndex)
{
    if (bandIndex == selectedBand.load())
        return;

    selectedBand.store(juce::jlimit(-1, 7, bandIndex));
    updateAttachments();
    updateControlsForBandType();
    updateDynamicsOpacity();
    updateBandButtonColors();

    int band = selectedBand.load();
    if (band >= 0)
    {
        juce::Colour bandColor = getBandColor(band);
        dynButton->setColour(juce::TextButton::buttonOnColourId, bandColor);

        bool isSoloed = processor.isBandSoloed(band);
        soloButton->setToggleState(isSoloed, juce::dontSendNotification);
    }
    else
    {
        soloButton->setToggleState(false, juce::dontSendNotification);
    }

    // Recalculate layout (ensures knob bounds are set correctly)
    resized();
    repaint();
}

void BandDetailPanel::updateAttachments()
{
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    slopeAttachment.reset();
    invertAttachment.reset();
    phaseInvertAttachment.reset();
    panAttachment.reset();
    routingAttachment.reset();
    dynEnableAttachment.reset();
    threshAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();
    rangeAttachment.reset();
    ratioAttachment.reset();

    if (selectedBand < 0 || selectedBand >= 8)
        return;

    int bandNum = selectedBand + 1;

    // EQ parameter attachments
    freqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandFreq(bandNum), *freqKnob);

    qAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandQ(bandNum), *qKnob);

    BandType type = getBandType(selectedBand);

    if (type == BandType::HighPass || type == BandType::LowPass)
    {
        slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, ParamIDs::bandSlope(bandNum), *slopeSelector);
    }

    if (type != BandType::HighPass && type != BandType::LowPass)
    {
        gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, ParamIDs::bandGain(bandNum), *gainKnob);
    }

    // Invert, phase invert, and pan attachments
    invertAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::bandInvert(bandNum), *invertButton);
    phaseInvertAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::bandPhaseInvert(bandNum), *phaseInvertButton);
    panAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandPan(bandNum), *panKnob);
    routingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::bandChannelRouting(bandNum), *routingSelector);

    // Dynamics attachments
    dynEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::bandDynEnabled(bandNum), *dynButton);

    threshAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynThreshold(bandNum), *thresholdKnob);

    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynAttack(bandNum), *attackKnob);

    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynRelease(bandNum), *releaseKnob);

    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynRange(bandNum), *rangeKnob);

    ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynRatio(bandNum), *ratioKnob);
}

void BandDetailPanel::updateControlsForBandType()
{
    if (matchMode)
        return;  // All EQ controls are hidden in match mode — don't re-show them

    BandType type = getBandType(selectedBand);
    bool isFilter = (type == BandType::HighPass || type == BandType::LowPass);
    bool hasGain = (selectedBand >= 1 && selectedBand <= 6);  // Bands 2-7

    slopeSelector->setVisible(isFilter);
    gainKnob->setVisible(!isFilter);

    // Invert only makes sense for bands with gain (2-7), hide in match mode
    invertButton->setVisible(hasGain && !matchMode);
    phaseInvertButton->setVisible(!matchMode);
    panKnob->setVisible(!matchMode);
    routingSelector->setVisible(!matchMode);

    // Ensure the visible control is on top (z-order) and force repaint
    if (isFilter)
    {
        slopeSelector->toFront(false);
        slopeSelector->repaint();
    }
    else
    {
        gainKnob->toFront(false);
        gainKnob->repaint();
    }
}

void BandDetailPanel::updateDynamicsOpacity()
{
    bool dynEnabled = isDynamicsEnabled();
    float alpha = dynEnabled ? 1.0f : 0.3f;  // 30% opacity when disabled (more obvious disabled state)

    thresholdKnob->setAlpha(alpha);
    thresholdKnob->setEnabled(dynEnabled);
    attackKnob->setAlpha(alpha);
    attackKnob->setEnabled(dynEnabled);
    releaseKnob->setAlpha(alpha);
    releaseKnob->setEnabled(dynEnabled);
    rangeKnob->setAlpha(alpha);
    rangeKnob->setEnabled(dynEnabled);
    ratioKnob->setAlpha(alpha);
    ratioKnob->setEnabled(dynEnabled);
}

juce::Colour BandDetailPanel::getBandColor(int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= 8)
        return juce::Colours::grey;
    return DefaultBandConfigs[static_cast<size_t>(bandIndex)].color;
}

BandType BandDetailPanel::getBandType(int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= 8)
        return BandType::Parametric;
    return DefaultBandConfigs[static_cast<size_t>(bandIndex)].type;
}

bool BandDetailPanel::isDynamicsEnabled() const
{
    if (selectedBand < 0 || selectedBand >= 8)
        return false;

    auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandDynEnabled(selectedBand + 1));
    return param != nullptr && param->load() > 0.5f;
}

void BandDetailPanel::parameterChanged(const juce::String& parameterID, float /*newValue*/)
{
    // Cache selectedBand locally to avoid race with GUI thread updates
    int band = selectedBand.load();
    if (band < 0 || band >= 8)
        return;

    if (parameterID == ParamIDs::bandDynEnabled(band + 1))
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<BandDetailPanel>(this), band]() {
            if (safeThis != nullptr && safeThis->selectedBand.load() == band)
            {
                safeThis->updateDynamicsOpacity();
                safeThis->repaint();
            }
        });
    }

    if (parameterID == ParamIDs::bandEnabled(band + 1))
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<BandDetailPanel>(this), band]() {
            if (safeThis != nullptr && safeThis->selectedBand.load() == band)
                safeThis->repaint();
        });
    }
}

juce::Rectangle<int> BandDetailPanel::getBandButtonBounds() const
{
    int knobSize = 75;
    int bandIndicatorSize = 65;
    int knobY = 26;
    int margin = 10;

    int bandBoxX = margin + 4;
    int bandBoxY = knobY + (knobSize - bandIndicatorSize) / 2;

    return juce::Rectangle<int>(bandBoxX, bandBoxY, bandIndicatorSize, bandIndicatorSize);
}

void BandDetailPanel::mouseDown(const juce::MouseEvent& e)
{
    auto bandBox = getBandButtonBounds();
    if (bandBox.contains(e.getPosition()) && selectedBand >= 0 && selectedBand < 8)
    {
        // Toggle the band enabled parameter
        if (auto* param = processor.parameters.getParameter(ParamIDs::bandEnabled(selectedBand + 1)))
        {
            float currentValue = param->getValue();
            param->setValueNotifyingHost(currentValue > 0.5f ? 0.0f : 1.0f);
        }
    }
}

void BandDetailPanel::mouseMove(const juce::MouseEvent& /*e*/)
{
    // No hover effects needed anymore
}

void BandDetailPanel::mouseExit(const juce::MouseEvent&)
{
    // No hover effects needed anymore
}

bool BandDetailPanel::keyPressed(const juce::KeyPress& key)
{
    if (!hasKeyboardFocus(true))
        return false;

    int band = selectedBand.load();

    if (key == juce::KeyPress('d') || key == juce::KeyPress('D'))
    {
        if (dynButton && dynButton->isVisible())
            dynButton->setToggleState(!dynButton->getToggleState(), juce::sendNotification);
        return true;
    }

    if (key == juce::KeyPress('s') || key == juce::KeyPress('S'))
    {
        if (soloButton && soloButton->isVisible())
            soloButton->setToggleState(!soloButton->getToggleState(), juce::sendNotification);
        return true;
    }

    if (key == juce::KeyPress::leftKey)
    {
        int newBand = (band > 0) ? band - 1 : MultiQ::NUM_BANDS - 1;
        if (onBandSelected)
            onBandSelected(newBand);
        return true;
    }

    if (key == juce::KeyPress::rightKey)
    {
        int newBand = (band < MultiQ::NUM_BANDS - 1) ? band + 1 : 0;
        if (onBandSelected)
            onBandSelected(newBand);
        return true;
    }

    if (key.getTextCharacter() >= '1' && key.getTextCharacter() <= '8')
    {
        int newBand = key.getTextCharacter() - '1';
        if (onBandSelected)
            onBandSelected(newBand);
        return true;
    }

    return false;
}

void BandDetailPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xFF1a1a1c));
    g.fillRect(bounds);

    g.setColour(juce::Colour(0xFF3a3a3a));
    g.drawHorizontalLine(0, 0.0f, bounds.getWidth());

    int knobSize = 75;
    int knobSpacing = 10;
    int bandIndicatorSize = 65;
    int knobY = 26;
    int btnWidth = 48;
    int margin = 10;

    bool dynEnabled = isDynamicsEnabled();

    int eqKnobsWidth = (knobSize + knobSpacing) * 3;
    int btnColWidth = 75;  // Uniform width for INV/Ø, SOLO, routing column
    int eqInnerWidth = eqKnobsWidth + 10 + btnColWidth;
    int eqSectionWidth = bandIndicatorSize + 10 + eqInnerWidth + 10;

    int dynKnobsWidth = (knobSize + knobSpacing) * 5 - knobSpacing;
    int dynSectionWidth = dynKnobsWidth + 10 + btnWidth + 10;

    int panSectionWidth = knobSize + 20;

    int leftX = margin;
    int rightX = getWidth() - margin - dynSectionWidth;
    int panCenterX = (leftX + eqSectionWidth + rightX) / 2;
    int panSectionX = panCenterX - panSectionWidth / 2;

    // Section divider positions (midpoints between content edges)
    int divider1X = (leftX + eqSectionWidth + panSectionX) / 2;
    int divider2X = (panSectionX + panSectionWidth + rightX) / 2;

    float sectionTop = 4.0f;
    float sectionH = bounds.getHeight() - 8.0f;

    if (matchMode)
    {
        // Match mode: single full-width section
        juce::Rectangle<float> matchSection(static_cast<float>(margin), sectionTop,
                                             static_cast<float>(getWidth() - margin * 2), sectionH);
        g.setColour(juce::Colour(0xFF1e2825));
        g.fillRoundedRectangle(matchSection, 4.0f);

        g.setColour(juce::Colour(0xFF44aa88));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("MATCH EQ", margin + 10, 6, 60, 10, juce::Justification::centredLeft);
        return;
    }

    // ===== BAND INDICATOR =====
    int startX = leftX;
    int bandBoxX = startX + 4;
    int bandBoxY = knobY + (knobSize - bandIndicatorSize) / 2;
    juce::Rectangle<float> bandBox(static_cast<float>(bandBoxX), static_cast<float>(bandBoxY),
                                    static_cast<float>(bandIndicatorSize), static_cast<float>(bandIndicatorSize));

    int rawBand = selectedBand.load();
    if (rawBand >= 0 && rawBand < 8)
    {
        int bandIdx = rawBand;
        juce::Colour bandColor = getBandColor(bandIdx);

        bool bandEnabled = true;
        if (auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandEnabled(bandIdx + 1)))
            bandEnabled = param->load() > 0.5f;

        juce::Colour subtleBandColor;
        if (bandEnabled)
            subtleBandColor = bandColor.darker(0.5f);
        else
            subtleBandColor = bandColor.withSaturation(0.15f).darker(0.7f);

        g.setColour(subtleBandColor);
        g.fillRoundedRectangle(bandBox, 8.0f);

        g.setColour(bandEnabled ? bandColor.withAlpha(0.6f) : bandColor.withSaturation(0.2f).withAlpha(0.3f));
        g.drawRoundedRectangle(bandBox.reduced(1.0f), 7.0f, 2.0f);

        g.setColour(bandEnabled ? juce::Colours::white : juce::Colour(0xFF606060));
        g.setFont(juce::FontOptions(32.0f, juce::Font::bold));

        float gainReduction = processor.getDynamicGain(bandIdx);
        bool showGR = dynEnabled && bandEnabled && std::abs(gainReduction) > 0.1f;

        if (showGR)
        {
            auto numberRect = bandBox.toNearestInt().withTrimmedBottom(18);
            g.drawText(juce::String(bandIdx + 1), numberRect, juce::Justification::centred);

            juce::Colour grColor = juce::Colour(0xFFff6644);
            g.setColour(grColor);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            auto grRect = bandBox.toNearestInt().withTrimmedTop(38);
            g.drawText(juce::String(gainReduction, 1) + " dB", grRect, juce::Justification::centred);

            float glowIntensity = juce::jlimit(0.0f, 1.0f, std::abs(gainReduction) / 12.0f);
            if (glowIntensity > 0.05f)
            {
                g.setColour(grColor.withAlpha(glowIntensity * 0.5f));
                g.drawRoundedRectangle(bandBox.reduced(0.5f), 8.5f, 3.0f);
            }
        }
        else
        {
            g.drawText(juce::String(bandIdx + 1), bandBox.toNearestInt(), juce::Justification::centred);
        }
    }
    else
    {
        g.setColour(juce::Colour(0xFF2a2a2c));
        g.fillRoundedRectangle(bandBox, 8.0f);
        g.setColour(juce::Colour(0xFF3a3a3c));
        g.drawRoundedRectangle(bandBox.reduced(1.0f), 7.0f, 1.0f);
    }

    int eqBgX = startX + bandIndicatorSize + 6;
    int gap = 3;

    // ===== EQ SECTION BACKGROUND =====
    juce::Rectangle<float> eqSection(static_cast<float>(eqBgX), sectionTop,
                                      static_cast<float>(divider1X - eqBgX - gap), sectionH);
    g.setColour(juce::Colour(0xFF222225));
    g.fillRoundedRectangle(eqSection, 4.0f);

    g.setColour(juce::Colour(0xFF707070));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText("EQ", static_cast<int>(eqSection.getX()) + 6, 6, 20, 10, juce::Justification::centredLeft);

    // ===== PAN SECTION BACKGROUND =====
    juce::Rectangle<float> panSection(static_cast<float>(divider1X + gap), sectionTop,
                                       static_cast<float>(divider2X - divider1X - gap * 2), sectionH);
    g.setColour(juce::Colour(0xFF1e2228));
    g.fillRoundedRectangle(panSection, 4.0f);

    // ===== DYNAMICS SECTION BACKGROUND =====
    juce::Colour rightBgColor = dynEnabled ? juce::Colour(0xFF28231e) : juce::Colour(0xFF1e1e20);
    juce::Rectangle<float> rightSection(static_cast<float>(divider2X + gap), sectionTop,
                                         static_cast<float>(getWidth() - margin - divider2X - gap), sectionH);
    g.setColour(rightBgColor);
    g.fillRoundedRectangle(rightSection, 4.0f);
}

void BandDetailPanel::paintOverChildren(juce::Graphics& g)
{
    int knobSize = 75;
    int knobSpacing = 10;
    int bandIndicatorSize = 65;
    int knobY = 26;
    int btnWidth = 48;
    int margin = 10;

    BandType type = getBandType(selectedBand);
    bool isFilter = (type == BandType::HighPass || type == BandType::LowPass);
    bool dynEnabled = isDynamicsEnabled();

    // Match mode: no knob labels to draw
    if (matchMode)
        return;

    int eqKnobsWidth = (knobSize + knobSpacing) * 3;
    int btnColWidth = 75;  // Uniform width for INV/Ø, SOLO, routing column
    int eqInnerWidth = eqKnobsWidth + 10 + btnColWidth;
    int eqSectionTotalWidth = bandIndicatorSize + 10 + eqInnerWidth + 10;

    int dynKnobsWidth = (knobSize + knobSpacing) * 5 - knobSpacing;
    int dynSectionWidth = dynKnobsWidth + 10 + btnWidth + 10;
    int panSectionWidth = knobSize + 20;

    int leftX = margin;
    int rightX = getWidth() - margin - dynSectionWidth;
    (void)leftX; (void)rightX; (void)eqSectionTotalWidth; (void)panSectionWidth;

    auto formatFreq = [](double val) {
        if (val >= 10000.0) return juce::String(val / 1000.0, 1) + " kHz";
        if (val >= 1000.0) return juce::String(val / 1000.0, 2) + " kHz";
        return juce::String(static_cast<int>(val)) + " Hz";
    };
    auto formatGain = [](double val) {
        juce::String sign = val >= 0 ? "+" : "";
        return sign + juce::String(val, 1) + " dB";
    };
    auto formatQ = [](double val) { return juce::String(val, 2); };
    auto formatMs = [](double val) {
        if (val >= 1000.0) return juce::String(val / 1000.0, 1) + " s";
        return juce::String(static_cast<int>(val)) + " ms";
    };
    auto formatDb = [](double val) { return juce::String(static_cast<int>(val)) + " dB"; };

    // ===== EQ SECTION LABELS =====
    int eqStartX = leftX + bandIndicatorSize + 10 + 4;
    int currentX = eqStartX;

    drawKnobWithLabel(g, freqKnob.get(), "FREQ",
                      formatFreq(freqKnob->getValue()),
                      {currentX, knobY, knobSize, knobSize + 20}, false);
    currentX += knobSize + knobSpacing;

    drawKnobWithLabel(g, qKnob.get(), "Q",
                      formatQ(qKnob->getValue()),
                      {currentX, knobY, knobSize, knobSize + 20}, false);
    currentX += knobSize + knobSpacing;

    if (!isFilter)
    {
        bool invActive = false;
        int rawBand2 = selectedBand.load();
        if (rawBand2 >= 0 && rawBand2 < 8)
        {
            if (auto* p = processor.parameters.getRawParameterValue(ParamIDs::bandInvert(rawBand2 + 1)))
                invActive = p->load() > 0.5f;
        }

        if (invActive)
        {
            double gain = gainKnob->getValue();
            double invGain = -gain;
            juce::String invStr = (invGain >= 0 ? "+" : "") + juce::String(invGain, 1) + " dB";

            g.setColour(juce::Colour(0xFFcc6622));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText("GAIN (INV)", currentX - 10, knobY - 16, knobSize + 20, 14, juce::Justification::centred);

            float cx = currentX + knobSize / 2.0f;
            float cy = knobY + knobSize / 2.0f;
            g.drawText(invStr, juce::Rectangle<int>(static_cast<int>(cx - 35), static_cast<int>(cy - 7), 70, 14),
                       juce::Justification::centred);
        }
        else
        {
            drawKnobWithLabel(g, gainKnob.get(), "GAIN",
                              formatGain(gainKnob->getValue()),
                              {currentX, knobY, knobSize, knobSize + 20}, false);
        }
    }
    else
    {
        g.setColour(juce::Colour(0xFFb0b0b0));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("SLOPE", currentX, knobY - 14, knobSize, 14, juce::Justification::centred);
    }

    // ===== PAN SECTION LABEL =====
    if (panKnob->isVisible())
    {
        g.setColour(juce::Colour(0xFFb0b0b0));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        auto pb = panKnob->getBounds();
        g.drawText("PAN", pb.getX() - 10, knobY - 16, pb.getWidth() + 20, 14, juce::Justification::centred);

        double panVal = panKnob->getValue();
        juce::String panStr;
        int panPct = static_cast<int>(std::round(std::abs(panVal) * 100.0));
        if (panPct == 0)
            panStr = "C";
        else if (panVal < 0)
            panStr = juce::String(panPct) + "L";
        else
            panStr = juce::String(panPct) + "R";

        g.setColour(juce::Colour(0xFFe8e0d8));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        float cx = pb.getCentreX();
        float cy = pb.getCentreY();
        g.drawText(panStr, juce::Rectangle<int>(static_cast<int>(cx - 25), static_cast<int>(cy - 7), 50, 14),
                   juce::Justification::centred);
    }

    // ===== DYNAMICS SECTION LABELS =====
    int dynStartX = rightX + 6;
    currentX = dynStartX;

    if (matchMode)
    {
        g.setColour(juce::Colour(0xFF44aa88));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("MATCH EQ", rightX + 6, 6, 60, 10, juce::Justification::centredLeft);
    }
    else
    {
        {
            float alpha = dynEnabled ? 1.0f : 0.3f;
            g.setColour(juce::Colour(0xFFb0b0b0).withAlpha(alpha));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText("THRESH", currentX + 5, knobY - 14, knobSize, 14, juce::Justification::centred);

            g.setColour(juce::Colour(0xFFe8e0d8).withAlpha(alpha));
            float cx = currentX + knobSize / 2.0f;
            float cy = knobY + knobSize / 2.0f;
            g.drawText(formatDb(thresholdKnob->getValue()),
                       juce::Rectangle<int>(static_cast<int>(cx - 35), static_cast<int>(cy - 7), 70, 14),
                       juce::Justification::centred);
        }
        currentX += knobSize + knobSpacing;

        drawKnobWithLabel(g, attackKnob.get(), "ATTACK",
                          formatMs(attackKnob->getValue()),
                          {currentX, knobY, knobSize, knobSize + 20}, !dynEnabled);
        currentX += knobSize + knobSpacing;

        drawKnobWithLabel(g, releaseKnob.get(), "RELEASE",
                          formatMs(releaseKnob->getValue()),
                          {currentX, knobY, knobSize, knobSize + 20}, !dynEnabled);
        currentX += knobSize + knobSpacing;

        drawKnobWithLabel(g, rangeKnob.get(), "RANGE",
                          formatDb(rangeKnob->getValue()),
                          {currentX, knobY, knobSize, knobSize + 20}, !dynEnabled);
        currentX += knobSize + knobSpacing;

        {
            juce::String ratioStr = juce::String(ratioKnob->getValue(), 1) + ":1";
            drawKnobWithLabel(g, ratioKnob.get(), "RATIO", ratioStr,
                              {currentX, knobY, knobSize, knobSize + 20}, !dynEnabled);
        }

        g.setColour(dynEnabled ? juce::Colour(0xFFff8844) : juce::Colour(0xFF505050));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        int labelY = knobY + knobSize + 4;
        g.drawText("DYNAMICS", dynStartX, labelY, dynKnobsWidth, 14, juce::Justification::centred);
    }
}

void BandDetailPanel::drawKnobWithLabel(juce::Graphics& g, juce::Slider* knob,
                                         const juce::String& label, const juce::String& value,
                                         juce::Rectangle<int> bounds, bool dimmed)
{
    float alpha = dimmed ? 0.3f : 1.0f;
    int knobSize = 75;  // Match British mode knob size

    // Label above knob
    g.setColour(juce::Colour(0xFFb0b0b0).withAlpha(alpha));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(label, bounds.getX() - 10, bounds.getY() - 16, bounds.getWidth() + 20, 14,
               juce::Justification::centred);

    // F6 Style: Value INSIDE the knob center (on the tan/brown area)
    // Calculate center of the knob
    float centreX = bounds.getX() + knobSize / 2.0f;
    float centreY = bounds.getY() + knobSize / 2.0f;

    // Draw value text centered in the knob
    g.setColour(juce::Colour(0xFFe8e0d8).withAlpha(alpha));  // Light tan color for contrast
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));

    // Use a rectangle centered on the knob for the value text
    juce::Rectangle<int> valueRect(
        static_cast<int>(centreX - 35),
        static_cast<int>(centreY - 7),
        70, 14
    );
    g.drawText(value, valueRect, juce::Justification::centred);

    (void)knob;  // Unused for now
}

void BandDetailPanel::resized()
{
    int knobSize = 75;
    int knobSpacing = 10;
    int bandIndicatorSize = 65;
    int knobY = 26;
    int btnWidth = 48;
    int margin = 10;
    int slopeSelectorWidth = 95;
    int invBtnW = 28;
    int invBtnH = 22;
    int invBtnGap = 3;

    // Section layout math (must match paint)
    int eqKnobsWidth = (knobSize + knobSpacing) * 3;
    int btnColWidth = 75;  // Uniform width for INV/Ø, SOLO, routing column
    int eqInnerWidth = eqKnobsWidth + 10 + btnColWidth;
    int eqSectionTotalWidth = bandIndicatorSize + 10 + eqInnerWidth + 10;

    int dynKnobsWidth = (knobSize + knobSpacing) * 5 - knobSpacing;
    int dynSectionWidth = dynKnobsWidth + 10 + btnWidth + 10;
    int panSectionWidth = knobSize + 20;

    int leftX = margin;
    int rightX = getWidth() - margin - dynSectionWidth;
    int panCenterX = (leftX + eqSectionTotalWidth + rightX) / 2;
    int panXPos = panCenterX - panSectionWidth / 2;

    if (matchMode)
    {
        // Full-width match layout — no EQ, no pan, no band indicator
        int matchX = margin + 10;
        int matchW = getWidth() - margin * 2 - 20;
        int btnHeight = 28;
        int rowGap = 6;
        int row1Y = knobY;
        int row2Y = row1Y + btnHeight + rowGap;
        // Row 1: Learn buttons + status + Apply slider + Smoothing slider
        int learnBtnWidth = 130;
        int learnGap = 8;
        int statusWidth = 80;
        learnCurrentButton.setBounds(matchX, row1Y, learnBtnWidth, btnHeight);
        learnReferenceButton.setBounds(matchX + learnBtnWidth + learnGap, row1Y, learnBtnWidth, btnHeight);
        learningStatusLabel.setBounds(matchX + (learnBtnWidth + learnGap) * 2, row1Y, statusWidth, btnHeight);

        // Sliders fill remaining space on row 1
        int slidersStartX = matchX + (learnBtnWidth + learnGap) * 2 + statusWidth + 10;
        int slidersW = matchX + matchW - slidersStartX;
        int sliderW = (slidersW - 10) / 2;
        matchApplySlider->setBounds(slidersStartX, row1Y, sliderW, btnHeight);
        matchSmoothingSlider->setBounds(slidersStartX + sliderW + 10, row1Y, sliderW, btnHeight);

        // Row 2: Match + Limit+ + Limit- + Clear
        int matchBtnWidth = 90;
        int limitBtnWidth = 80;
        int clearBtnWidth = 80;
        int btnGap = 8;
        matchComputeButton.setBounds(matchX, row2Y, matchBtnWidth, btnHeight);
        limitBoostButton.setBounds(matchX + matchBtnWidth + btnGap, row2Y, limitBtnWidth, btnHeight);
        limitCutButton.setBounds(matchX + matchBtnWidth + limitBtnWidth + btnGap * 2, row2Y, limitBtnWidth, btnHeight);
        matchClearButton.setBounds(matchX + matchBtnWidth + limitBtnWidth * 2 + btnGap * 3, row2Y, clearBtnWidth, btnHeight);

        return;
    }

    // ===== EQ SECTION (left-justified) =====
    int eqStartX = leftX + bandIndicatorSize + 10 + 4;
    int currentX = eqStartX;

    freqKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    qKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    BandType type = getBandType(selectedBand);
    bool isFilter = (type == BandType::HighPass || type == BandType::LowPass);

    gainKnob->setBounds(currentX, knobY, knobSize, knobSize);
    gainKnob->setVisible(!isFilter);

    int selectorHeight = 26;
    int selectorY = knobY + (knobSize - selectorHeight) / 2;
    int selectorX = currentX + (knobSize - slopeSelectorWidth) / 2;
    slopeSelector->setBounds(selectorX, selectorY, slopeSelectorWidth, selectorHeight);
    slopeSelector->setVisible(isFilter);

    if (isFilter)
        slopeSelector->toFront(false);
    else
        gainKnob->toFront(false);

    currentX += knobSize + knobSpacing;

    // INV + Phase + SOLO + ROUTING buttons after the 3 EQ knobs
    {
        int btnStartX = currentX + 10;
        int colW = 75;  // Uniform width for all rows
        int fxBtnY = knobY + (knobSize - invBtnH * 3 - 4 * 2) / 2;
        int halfW = (colW - invBtnGap) / 2;
        invertButton->setBounds(btnStartX, fxBtnY, halfW, invBtnH);
        phaseInvertButton->setBounds(btnStartX + halfW + invBtnGap, fxBtnY, halfW, invBtnH);
        soloButton->setBounds(btnStartX, fxBtnY + invBtnH + 4, colW, invBtnH);
        routingSelector->setBounds(btnStartX, fxBtnY + (invBtnH + 4) * 2, colW, invBtnH);
    }

    // ===== PAN SECTION (center) =====
    int panKnobX = panXPos + (panSectionWidth - knobSize) / 2;
    panKnob->setBounds(panKnobX, knobY, knobSize, knobSize);

    // ===== DYNAMICS SECTION (right-justified) =====
    currentX = rightX + 6;

    thresholdKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    attackKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    releaseKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    rangeKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    ratioKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing + 6;

    int btnHeight = 22;
    int btnY = knobY + (knobSize - btnHeight * 2 - 4) / 2;

    dynButton->setBounds(currentX, btnY, btnWidth, btnHeight);
}

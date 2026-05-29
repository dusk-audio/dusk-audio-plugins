#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "ParamIDs.h"

// =============================================================================
// KnobWithLabel
// =============================================================================

void KnobWithLabel::init (juce::Component& parent,
                          juce::AudioProcessorValueTreeState& apvts,
                          const juce::String& paramID,
                          const juce::String& displayName,
                          const juce::String& suffix,
                          const juce::String& tooltip)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    if (tooltip.isNotEmpty())
        slider.setTooltip (tooltip);
    parent.addAndMakeVisible (slider);

    nameLabel.setText (displayName, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setInterceptsMouseClicks (false, false);
    nameLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId,
                         juce::Colour (DuskAmpLookAndFeel::kLabelText));
    parent.addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setInterceptsMouseClicks (false, false);
    valueLabel.setFont (juce::FontOptions (11.0f));
    valueLabel.setColour (juce::Label::textColourId,
                          juce::Colour (DuskAmpLookAndFeel::kValueText));
    parent.addAndMakeVisible (valueLabel);

    suffix_ = suffix;

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);
    boundParamID_ = paramID;

    applyFormatter();
}

void KnobWithLabel::rebind (juce::AudioProcessorValueTreeState& apvts,
                            const juce::String& paramID)
{
    if (paramID == boundParamID_)
        return;
    // Destroy the old attachment first so the slider isn't briefly bound
    // to two parameters at once. Then create the new attachment, which
    // will sync slider.value from the APVTS automatically.
    attachment.reset();
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);
    boundParamID_ = paramID;
    // SliderAttachment's ctor clears textFromValueFunction, so re-install.
    applyFormatter();
}

void KnobWithLabel::applyFormatter()
{
    auto sfx = suffix_;
    slider.textFromValueFunction = [sfx] (double v)
    {
        if (sfx == " dB")   return juce::String (v, 1) + " dB";
        if (sfx == " ms")   return juce::String (juce::roundToInt (v)) + " ms";
        if (sfx == " Hz")
            return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz"
                               : v < 100.0 ? juce::String (v, 2) + " Hz"
                                           : juce::String (juce::roundToInt (v)) + " Hz";
        if (sfx == " s")
            return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms"
                           : juce::String (v, 2) + " s";
        if (sfx == "%")     return juce::String (v * 100.0, 1) + "%";
        return juce::String (v, 2);
    };
}

// =============================================================================
// AmpModeSelector
// =============================================================================

AmpModeSelector::AmpModeSelector (juce::RangedAudioParameter& param)
    : param_ (param),
      attachment_ (param,
                   [this] (float v) {
                       currentIndex_ = juce::roundToInt (v);
                       repaint();
                   },
                   nullptr)
{
    currentIndex_ = juce::roundToInt (param.convertFrom0to1 (param.getValue()));
    setRepaintsOnMouseActivity (true);
}

void AmpModeSelector::resized()
{
    segmentBounds_.clear();
    auto bounds = getLocalBounds();
    int numSegs = labels_.size();
    int segW = bounds.getWidth() / numSegs;

    for (int i = 0; i < numSegs; ++i)
    {
        int x = i * segW;
        int w = (i == numSegs - 1) ? bounds.getWidth() - x : segW;
        segmentBounds_.push_back ({ x, 0, w, bounds.getHeight() });
    }
}

// =============================================================================
// PillButton — single-segment styled trigger that mirrors AmpModeSelector's
// pill aesthetic so the TUNER button doesn't look like a generic TextButton
// next to the DSP/NAM toggle.
// =============================================================================

void PillButton::paintButton (juce::Graphics& g,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown)
{
    float cornerRadius = static_cast<float> (getHeight()) * 0.35f;
    auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colour (DuskAmpLookAndFeel::kPanel));
    g.fillRoundedRectangle (bounds, cornerRadius);

    g.setColour (juce::Colour (DuskAmpLookAndFeel::kBorder));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);

    const bool active = active_ || shouldDrawButtonAsDown;

    if (active)
    {
        g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent));
        g.fillRoundedRectangle (bounds.reduced (2.0f), cornerRadius - 2.0f);
    }
    else if (shouldDrawButtonAsHighlighted)
    {
        g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent).withAlpha (0.15f));
        g.fillRoundedRectangle (bounds.reduced (2.0f), cornerRadius - 2.0f);
    }

    g.setColour (active ? juce::Colours::white
                 : shouldDrawButtonAsHighlighted ? juce::Colour (0xffd0d0d0)
                                                  : juce::Colour (DuskAmpLookAndFeel::kGroupText));
    g.setFont (juce::FontOptions (13.0f, active ? juce::Font::bold : juce::Font::plain));
    g.drawText (getButtonText(), bounds.toNearestInt(), juce::Justification::centred);
}

void AmpModeSelector::paint (juce::Graphics& g)
{
    float cornerRadius = static_cast<float> (getHeight()) * 0.35f;

    // Outer container background
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kPanel));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), cornerRadius);

    // Outer border
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kBorder));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), cornerRadius, 1.0f);

    for (int i = 0; i < static_cast<int> (segmentBounds_.size()); ++i)
    {
        auto seg = segmentBounds_[static_cast<size_t> (i)].toFloat();
        bool selected = (i == currentIndex_);
        bool hovered = segmentBounds_[static_cast<size_t> (i)].contains (getMouseXYRelative())
                       && ! selected;

        if (selected)
        {
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent));
            g.fillRoundedRectangle (seg.reduced (2.0f), cornerRadius - 2.0f);
        }
        else if (hovered)
        {
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent).withAlpha (0.15f));
            g.fillRoundedRectangle (seg.reduced (2.0f), cornerRadius - 2.0f);
        }

        g.setColour (selected ? juce::Colours::white
                     : hovered ? juce::Colour (0xffd0d0d0)
                               : juce::Colour (DuskAmpLookAndFeel::kGroupText));
        g.setFont (juce::FontOptions (13.0f, selected ? juce::Font::bold : juce::Font::plain));
        g.drawText (labels_[i], segmentBounds_[static_cast<size_t> (i)],
                    juce::Justification::centred);
    }
}

void AmpModeSelector::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < static_cast<int> (segmentBounds_.size()); ++i)
    {
        if (segmentBounds_[static_cast<size_t> (i)].contains (e.getPosition()))
        {
            if (i != currentIndex_)
            {
                currentIndex_ = i;
                attachment_.setValueAsCompleteGesture (static_cast<float> (i));
                repaint();
            }
            break;
        }
    }
}

// =============================================================================
// DuskAmpEditor
// =============================================================================

// Left BROWSERS sidebar + 3 rows of metallic cards. Height tuned so the
// cards hug their content (title + knob cluster) with only light breathing
// room — a taller window left ~120 px of dead space below every card.
static constexpr int kBaseWidth  = 1024;
static constexpr int kBaseHeight = 600;

DuskAmpEditor::DuskAmpEditor (DuskAmpProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    setLookAndFeel (&lnf_);

    // --- Init all knobs ---
    auto& params = p.parameters;

    inputGain_     .init (*this, params, DuskAmpParams::INPUT_GAIN,      "INPUT",      " dB",
        "Input gain adjustment");
    gateThreshold_ .init (*this, params, DuskAmpParams::GATE_THRESHOLD,  "GATE",       " dB",
        "Noise gate threshold. Below this level, signal is muted");
    gateRelease_   .init (*this, params, DuskAmpParams::GATE_RELEASE,    "RELEASE",    " ms",
        "Noise gate release time");
    preampGain_    .init (*this, params, DuskAmpParams::PREAMP_GAIN,     "GAIN",       "%",
        "Preamp drive amount. Higher = more distortion");
    bass_          .init (*this, params, DuskAmpParams::BASS,            "BASS",       "%",
        "Low frequency tone control");
    mid_           .init (*this, params, DuskAmpParams::MID,             "MID",        "%",
        "Mid frequency tone control");
    treble_        .init (*this, params, DuskAmpParams::TREBLE,          "TREBLE",     "%",
        "High frequency tone control");
    powerDrive_    .init (*this, params, DuskAmpParams::POWER_DRIVE,     "DRIVE",      "%",
        "Power amp drive. Adds compression and harmonic richness");
    presence_      .init (*this, params, DuskAmpParams::PRESENCE,        "PRESENCE",   "%",
        "Upper-mid emphasis in power amp stage");
    resonance_     .init (*this, params, DuskAmpParams::RESONANCE,       "RESONANCE",  "%",
        "Low-frequency emphasis in power amp stage");
    sag_           .init (*this, params, DuskAmpParams::SAG,             "SAG",        "%",
        "Power supply sag. Adds dynamic compression feel");
    cabMix_        .init (*this, params, DuskAmpParams::CAB_MIX,         "MIX",        "%",
        "Cabinet simulation wet/dry mix");
    cabHiCut_      .init (*this, params, DuskAmpParams::CAB_HICUT,       "HI CUT",    " Hz",
        "Cabinet high-frequency rolloff");
    cabLoCut_      .init (*this, params, DuskAmpParams::CAB_LOCUT,       "LO CUT",    " Hz",
        "Cabinet low-frequency rolloff");
    delayTime_     .init (*this, params, DuskAmpParams::DELAY_TIME,      "TIME",       " ms",
        "Delay time");
    delayFeedback_ .init (*this, params, DuskAmpParams::DELAY_FEEDBACK,  "FEEDBACK",   "%",
        "Delay feedback amount");
    delayMix_      .init (*this, params, DuskAmpParams::DELAY_MIX,       "MIX",        "%",
        "Delay wet/dry mix");
    reverbMix_     .init (*this, params, DuskAmpParams::REVERB_MIX,      "MIX",        "%",
        "Reverb wet/dry mix");
    reverbDecay_   .init (*this, params, DuskAmpParams::REVERB_DECAY,    "DECAY",      "%",
        "Reverb tail length");
    outputLevel_   .init (*this, params, DuskAmpParams::OUTPUT_LEVEL,    "OUTPUT",     " dB",
        "Master output level");

    // DRIVE-knob value formatter shows "OFF" when at zero. The default
    // formatter would print "0.0%" which reads as a non-engaged stage but
    // doesn't communicate the bypass state — "OFF" is unambiguous.
    powerDrive_.slider.textFromValueFunction = [] (double v)
    {
        if (v <= 1.0e-4) return juce::String ("OFF");
        return juce::String (v * 100.0, 1) + "%";
    };
    // Force initial label to reflect current value via the new formatter.
    powerDrive_.valueLabel.setText (powerDrive_.slider.getTextFromValue (powerDrive_.slider.getValue()),
                                     juce::dontSendNotification);

    // --- Mode selector (DSP / NAM) ---
    auto* modeParam = params.getParameter (DuskAmpParams::AMP_MODE);
    jassert (modeParam != nullptr);
    modeSelector_ = std::make_unique<AmpModeSelector> (*modeParam);
    addAndMakeVisible (*modeSelector_);

    // --- Channel selector ---
    channelBox_.addItemList ({ "Clean", "Crunch", "Lead" }, 1);
    channelBox_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (channelBox_);
    channelAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        params, DuskAmpParams::PREAMP_CHANNEL, channelBox_);

    // --- Tone type selector ---
    toneTypeBox_.addItemList ({ "American", "British", "AC" }, 1);
    toneTypeBox_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (toneTypeBox_);
    toneTypeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        params, DuskAmpParams::TONE_TYPE, toneTypeBox_);

    // --- Bright toggle ---
    brightButton_.setButtonText ("BRIGHT");
    brightButton_.setClickingTogglesState (true);
    addAndMakeVisible (brightButton_);
    brightAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::PREAMP_BRIGHT, brightButton_);

    // --- Cabinet enabled toggle ---
    cabEnabled_.setButtonText ("CABINET");
    cabEnabled_.setClickingTogglesState (true);
    cabEnabled_.setTooltip ("Toggle the cabinet IR convolution stage on / off.");
    addAndMakeVisible (cabEnabled_);
    cabEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::CAB_ENABLED, cabEnabled_);

    // --- Cabinet normalize toggle ---
    cabNormalize_.setButtonText ("NORM IR");
    cabNormalize_.setClickingTogglesState (true);
    cabNormalize_.setTooltip ("Normalize cab IR loudness to pre-cab level. Useful when the IR drops volume significantly.");
    addAndMakeVisible (cabNormalize_);
    cabNormalizeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::CAB_NORMALIZE, cabNormalize_);

    // --- Cab browser ---
    cabBrowser_.onFileSelected = [this] (const juce::File& file)
    {
        processorRef.loadCabinetIR (file);
        cabBrowser_.setLoadedFile (file);
    };
    addAndMakeVisible (cabBrowser_);

    // Restore loaded cab IR from saved state
    {
        auto savedPath = processorRef.parameters.state.getProperty ("cabIRPath", "").toString();
        if (savedPath.isNotEmpty())
        {
            juce::File f (savedPath);
            if (f.existsAsFile())
            {
                cabBrowser_.setRootDirectory (f.getParentDirectory());
                cabBrowser_.setLoadedFile (f);
            }
        }
    }

    // --- NAM browser ---
    namBrowser_.onFileSelected = [this] (const juce::File& file)
    {
        processorRef.loadNAMModel (file);
        namBrowser_.setLoadedFile (file);
    };
    addAndMakeVisible (namBrowser_);

    // Restore loaded NAM model from saved state
    {
        auto savedPath = processorRef.getNAMModelPath();
        if (savedPath.isNotEmpty())
        {
            juce::File f (savedPath);
            if (f.existsAsFile())
            {
                namBrowser_.setRootDirectory (f.getParentDirectory());
                namBrowser_.setLoadedFile (f);
            }
        }
    }

    // --- Delay enabled toggle ---
    delayEnabled_.setButtonText ("DELAY");
    delayEnabled_.setClickingTogglesState (true);
    delayEnabled_.setTooltip ("Click to bypass / enable the delay section.");
    delayEnabled_.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (delayEnabled_);
    delayEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::DELAY_ENABLED, delayEnabled_);

    // --- Reverb enabled toggle ---
    reverbEnabled_.setButtonText ("REVERB");
    reverbEnabled_.setClickingTogglesState (true);
    reverbEnabled_.setTooltip ("Click to bypass / enable the reverb section.");
    reverbEnabled_.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (reverbEnabled_);
    reverbEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::REVERB_ENABLED, reverbEnabled_);

    // --- Oversampling selector ---
    oversamplingBox_.addItemList ({ "2x", "4x", "8x" }, 1);
    oversamplingBox_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (oversamplingBox_);
    oversamplingAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        params, DuskAmpParams::OVERSAMPLING, oversamplingBox_);

    // --- Tuner button ---
    tunerButton_.setButtonText ("TUNER");
    tunerButton_.setTooltip ("Open chromatic tuner. Output mutes while open so you can tune in silence.");
    tunerButton_.onClick = [this] { showTunerPanel(); };
    addAndMakeVisible (tunerButton_);

    // A/B snapshot pills. Right-click is the explicit "save current to this
    // slot" gesture; left-click recalls (or, on an empty slot, captures).
    auto wireSlot = [this] (ABPillButton& btn, int slot, const char* label)
    {
        btn.setButtonText (label);
        btn.setTooltip ("Left-click: recall slot (captures current state on first click). "
                        "Right-click: snapshot current state into this slot.");
        btn.onClick = [this, slot]
        {
            if (processorRef.isABSlotPopulated (slot))
                processorRef.recallABSlot (slot);
            else
                processorRef.pushABSlot (slot);
        };
        btn.onCapture = [this, slot] { processorRef.pushABSlot (slot); };
        addAndMakeVisible (btn);
    };
    wireSlot (slotAButton_, 0, "A");
    wireSlot (slotBButton_, 1, "B");

    // --- User preset manager ---
    userPresetManager_ = std::make_unique<UserPresetManager> ("DuskAmp");

    // --- Preset browser ---
    presetBox_.setJustificationType (juce::Justification::centred);
    presetBox_.onChange = [this]
    {
        int id = presetBox_.getSelectedId();
        if (id >= 1 && id <= kNumFactoryPresets)
        {
            // Clear the user-IR-override flag first, so the preset's
            // TONE_TYPE swap re-triggers the bundled-IR auto-load. Each
            // factory preset is meant to come with its matching cab.
            processorRef.clearUserIROverride();
            kFactoryPresets[id - 1].applyTo (processorRef.parameters);
        }
        else if (id >= 1001)
        {
            int userIdx = id - 1001;
            auto userPresets = userPresetManager_->loadUserPresets();
            if (userIdx >= 0 && userIdx < static_cast<int> (userPresets.size()))
                loadUserPreset (userPresets[static_cast<size_t> (userIdx)].name);
        }
        updateDeleteButtonVisibility();
        capturePresetSnapshot();
    };
    addAndMakeVisible (presetBox_);

    // "*" indicator that lights up when the live state diverges from the
    // last-loaded preset. Painted in accent so it's visible without grabbing
    // attention when knobs are at preset defaults.
    presetDirtyLabel_.setText ("", juce::dontSendNotification);
    presetDirtyLabel_.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    presetDirtyLabel_.setColour (juce::Label::textColourId,
                                  juce::Colour (DuskAmpLookAndFeel::kAccent));
    presetDirtyLabel_.setJustificationType (juce::Justification::centredLeft);
    presetDirtyLabel_.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (presetDirtyLabel_);

    refreshPresetList();

    // Restore preset selection from saved state
    {
        auto savedName = processorRef.parameters.state.getProperty ("presetName", "").toString();
        if (savedName.isNotEmpty() && userPresetManager_)
        {
            auto userPresets = userPresetManager_->loadUserPresets();
            for (size_t i = 0; i < userPresets.size(); ++i)
            {
                if (userPresets[i].name == savedName)
                {
                    presetBox_.setSelectedId (static_cast<int> (1001 + i), juce::dontSendNotification);
                    break;
                }
            }
        }
        updateDeleteButtonVisibility();
    }

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
                juce::Component::SafePointer<DuskAmpEditor> safeThis (this);

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

    // --- Level meters ---
    inputMeter_.setStereoMode (true);
    inputMeter_.setRefreshRate (15.0f);
    addAndMakeVisible (inputMeter_);

    outputMeter_.setStereoMode (true);
    outputMeter_.setRefreshRate (15.0f);
    addAndMakeVisible (outputMeter_);

    // --- Scalable editor: 900x600 base, 70%-200%, fixed aspect ratio ---
    scaler_.initialize (this, &p, kBaseWidth, kBaseHeight,
                        static_cast<int> (kBaseWidth * 0.7f),
                        static_cast<int> (kBaseHeight * 0.7f),
                        kBaseWidth * 2, kBaseHeight * 2,
                        true);

    // --- Footer labels ---
    footerVersionLabel_.setText ("DuskAmp v" + juce::String (JucePlugin_VersionString),
                                  juce::dontSendNotification);
    footerVersionLabel_.setFont (juce::FontOptions (10.0f));
    footerVersionLabel_.setColour (juce::Label::textColourId,
                                    juce::Colour (DuskAmpLookAndFeel::kSubtleText));
    footerVersionLabel_.setJustificationType (juce::Justification::centredLeft);
    footerVersionLabel_.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (footerVersionLabel_);

    footerTooltipLabel_.setText ("", juce::dontSendNotification);
    footerTooltipLabel_.setFont (juce::FontOptions (10.0f));
    footerTooltipLabel_.setColour (juce::Label::textColourId,
                                    juce::Colour (DuskAmpLookAndFeel::kText).withAlpha (0.75f));
    footerTooltipLabel_.setJustificationType (juce::Justification::centred);
    footerTooltipLabel_.setInterceptsMouseClicks (false, false);
    footerTooltipLabel_.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (footerTooltipLabel_);

    footerCpuLabel_.setText ("CPU --", juce::dontSendNotification);
    footerCpuLabel_.setFont (juce::FontOptions (10.0f));
    footerCpuLabel_.setColour (juce::Label::textColourId,
                                juce::Colour (DuskAmpLookAndFeel::kSubtleText));
    footerCpuLabel_.setJustificationType (juce::Justification::centredRight);
    footerCpuLabel_.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (footerCpuLabel_);

    // If a previous version stored a window size that's now smaller than
    // the layout needs (post-UI-overhaul base is 820 tall vs prior 720),
    // grow on launch. ScalableEditorHelper will clamp future user resizes
    // to the min defined in initialize() above.
    int w = std::max (scaler_.getStoredWidth(),  kBaseWidth);
    int h = std::max (scaler_.getStoredHeight(), kBaseHeight);
    setSize (w, h);
    startTimerHz (30);

    // Establish the preset baseline AFTER the saved-state restore so the
    // "*" indicator stays dark when the user reopens a session that was
    // already at preset defaults. Any subsequent param tweak will mark dirty.
    capturePresetSnapshot();
}

DuskAmpEditor::~DuskAmpEditor()
{
    // Undo the mute that showTunerPanel() sets — otherwise closing the
    // editor window with the tuner open leaves the processor muted.
    processorRef.setTunerActive (false);
    scaler_.saveSize();
    setLookAndFeel (nullptr);
}

// =============================================================================
// Timer — update value labels & meters
// =============================================================================

void DuskAmpEditor::rebindToneStackForMode (int ampMode)
{
    // Idempotent guard — timer fires at 30 Hz; without this, every tick
    // walked through 7 KnobWithLabel::rebind() no-op checks plus opened
    // a brief window where the ComboBoxAttachment was destroyed before
    // its replacement was constructed. Bail out unless the mode flipped.
    if (toneTypeBoundMode_ == ampMode)
        return;

    auto& params = processorRef.parameters;
    // Per-mode params: tone stack (bass/mid/treble + tone-type) AND input/
    // output (input gain, gate threshold, gate release, output level). The
    // input/output split is a SAFETY measure — switching DSP↔NAM should
    // never blast the user when the two modes have different gain
    // calibrations (a hot NAM model with the DSP path's input gain could
    // damage hearing or speakers).
    //
    // rebindToneStackForMode runs on the message thread (timer callback /
    // mode-change notification). APVTS is thread-safe for reads from the
    // audio thread; tearing down a SliderAttachment here doesn't touch
    // audio-thread state.
    if (ampMode == 1) // NAM
    {
        bass_         .rebind (params, DuskAmpParams::BASS_NAM);
        mid_          .rebind (params, DuskAmpParams::MID_NAM);
        treble_       .rebind (params, DuskAmpParams::TREBLE_NAM);
        inputGain_    .rebind (params, DuskAmpParams::INPUT_GAIN_NAM);
        gateThreshold_.rebind (params, DuskAmpParams::GATE_THRESHOLD_NAM);
        gateRelease_  .rebind (params, DuskAmpParams::GATE_RELEASE_NAM);
        outputLevel_  .rebind (params, DuskAmpParams::OUTPUT_LEVEL_NAM);
        toneTypeAttachment_.reset();
        toneTypeAttachment_ = std::make_unique<
            juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            params, DuskAmpParams::TONE_TYPE_NAM, toneTypeBox_);
        toneTypeBoundMode_ = 1;
    }
    else // DSP
    {
        bass_         .rebind (params, DuskAmpParams::BASS);
        mid_          .rebind (params, DuskAmpParams::MID);
        treble_       .rebind (params, DuskAmpParams::TREBLE);
        inputGain_    .rebind (params, DuskAmpParams::INPUT_GAIN);
        gateThreshold_.rebind (params, DuskAmpParams::GATE_THRESHOLD);
        gateRelease_  .rebind (params, DuskAmpParams::GATE_RELEASE);
        outputLevel_  .rebind (params, DuskAmpParams::OUTPUT_LEVEL);
        toneTypeAttachment_.reset();
        toneTypeAttachment_ = std::make_unique<
            juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            params, DuskAmpParams::TONE_TYPE, toneTypeBox_);
        toneTypeBoundMode_ = 0;
    }
}

void DuskAmpEditor::timerCallback()
{
    // React to AmpMode changes by re-binding bass/mid/treble/tone-type to
    // the active mode's params. UI shows one set of knobs; the underlying
    // params are mode-independent so settings persist per mode.
    {
        auto* ampModeParam = processorRef.parameters.getRawParameterValue (DuskAmpParams::AMP_MODE);
        const int ampMode = ampModeParam != nullptr
                          ? static_cast<int> (ampModeParam->load())
                          : 0;
        rebindToneStackForMode (ampMode);
    }

    auto update = [] (KnobWithLabel& k)
    {
        k.valueLabel.setText (k.slider.getTextFromValue (k.slider.getValue()),
                              juce::dontSendNotification);
    };

    update (inputGain_);
    update (gateThreshold_);
    update (gateRelease_);
    update (preampGain_);
    update (bass_);
    update (mid_);
    update (treble_);
    update (powerDrive_);
    update (presence_);
    update (resonance_);
    update (sag_);
    update (cabMix_);
    update (cabHiCut_);
    update (cabLoCut_);
    update (delayTime_);
    update (delayFeedback_);
    update (delayMix_);
    update (reverbMix_);
    update (reverbDecay_);
    update (outputLevel_);

    // Footer text: prefer surfacing IR / NAM load status when it differs
    // from the boring idle/OK state; otherwise mirror the tooltip under
    // the mouse. Statuses appear immediately after a load attempt and
    // give the user feedback on background NAM parsing.
    {
        juce::String text;
        const juce::String irStatus  = processorRef.getLastIRStatus();
        const juce::String namStatus = processorRef.getLastNAMStatus();

        auto isInteresting = [] (const juce::String& s)
        {
            return s.isNotEmpty()
                && s != "idle"
                && (s.startsWith ("FAIL")
                    || s.startsWith ("loading")
                    || s.containsIgnoreCase ("alias")
                    || s.containsIgnoreCase ("warn"));
        };

        if (isInteresting (namStatus))      text = "NAM: " + namStatus;
        else if (isInteresting (irStatus))  text = "IR: "  + irStatus;
        else
        {
            // Fall back to tooltip-under-mouse mirror.
            if (auto* c = getComponentAt (getMouseXYRelative()))
                if (auto* tc = dynamic_cast<juce::TooltipClient*> (c))
                    text = tc->getTooltip();
        }

        if (text != footerTooltipLabel_.getText())
            footerTooltipLabel_.setText (text, juce::dontSendNotification);
    }

    // Dim cabinet knobs when cab is disabled
    bool cabOff = ! cabEnabled_.getToggleState();
    cabMix_.setDimmed (cabOff);
    cabHiCut_.setDimmed (cabOff);
    cabLoCut_.setDimmed (cabOff);
    cabBrowser_.setEnabled (! cabOff);
    cabBrowser_.setAlpha (cabOff ? 0.4f : 1.0f);
    cabNormalize_.setEnabled (! cabOff);
    cabNormalize_.setAlpha (cabOff ? 0.4f : 1.0f);

    // Dim delay knobs when delay is disabled
    bool delayOff = ! delayEnabled_.getToggleState();
    delayTime_.setDimmed (delayOff);
    delayFeedback_.setDimmed (delayOff);
    delayMix_.setDimmed (delayOff);

    // Dim reverb knobs when reverb is disabled
    bool reverbOff = ! reverbEnabled_.getToggleState();
    reverbMix_.setDimmed (reverbOff);
    reverbDecay_.setDimmed (reverbOff);

    // Preset dirty detection — compare live APVTS state against the snapshot
    // captured at the last preset load. Cheap (~30 floats); runs at 30 Hz.
    {
        const bool dirty = ! presetSnapshot_.empty() && ! currentStateMatchesSnapshot();
        if (dirty != presetDirty_)
        {
            presetDirty_ = dirty;
            presetDirtyLabel_.setText (dirty ? "*" : "", juce::dontSendNotification);
        }
    }

    // NAM mode: show/hide controls and trigger relayout when mode changes
    bool namMode = processorRef.parameters.getRawParameterValue (DuskAmpParams::AMP_MODE)->load() >= 0.5f;

    if (namMode != layoutIsNamMode_)
    {
        resized();
        repaint();
    }

    // Dim preamp controls in NAM mode (for any visible ones)
    preampGain_.setDimmed (namMode);
    preampGain_.slider.setEnabled (! namMode);
    channelBox_.setEnabled (! namMode);
    channelBox_.setAlpha (namMode ? 0.4f : 1.0f);
    brightButton_.setEnabled (! namMode);
    brightButton_.setAlpha (namMode ? 0.4f : 1.0f);

    // Update meters
    inputMeter_.setStereoLevels (processorRef.getInputLevelL(),
                                 processorRef.getInputLevelR());
    outputMeter_.setStereoLevels (processorRef.getOutputLevelL(),
                                  processorRef.getOutputLevelR());
    inputMeter_.repaint();
    outputMeter_.repaint();

    // Tuner overlay — refresh from atomic processor state when visible.
    if (tunerOverlay_ && tunerOverlay_->isVisible())
        tunerOverlay_->setDetected (processorRef.getDetectedHz(),
                                    processorRef.getDetectedLevel());

    // A/B pills follow the processor's active slot.
    const int activeSlot = processorRef.getABActiveSlot();
    slotAButton_.setActive (activeSlot == 0);
    slotBButton_.setActive (activeSlot == 1);
}

// =============================================================================
// Paint
// =============================================================================

void DuskAmpEditor::paint (juce::Graphics& g)
{
    auto sf = scaler_.getScaleFactor();

    // --- Subtle vertical gradient background ---
    {
        juce::ColourGradient bgGrad (
            juce::Colour (DuskAmpLookAndFeel::kBackground).darker (0.20f),
            0.0f, 0.0f,
            juce::Colour (DuskAmpLookAndFeel::kBackground).brighter (0.04f),
            0.0f, static_cast<float> (getHeight()),
            false);
        g.setGradientFill (bgGrad);
        g.fillAll();
    }

    // --- Brand block: logo + accent rule + tagline ---
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kText));
    g.setFont (juce::FontOptions (24.0f * sf, juce::Font::bold));
    titleClickArea_ = { scaler_.scaled (14), scaler_.scaled (8),
                        scaler_.scaled (180), scaler_.scaled (32) };
    g.drawText ("DUSKAMP", titleClickArea_, juce::Justification::centredLeft);
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent));
    g.fillRect (scaler_.scaled (14), scaler_.scaled (38),
                scaler_.scaled (52), scaler_.scaled (2));
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kSubtleText));
    g.setFont (juce::FontOptions (10.0f * sf));
    g.drawText ("GUITAR  AMP  SIMULATOR", scaler_.scaled (14), scaler_.scaled (42),
                scaler_.scaled (220), scaler_.scaled (14),
                juce::Justification::centredLeft);

    // --- BROWSERS sidebar — header + sub-panel titles ---
    if (! sidebarBounds_.isEmpty())
    {
        // "BROWSERS" header strip across top of sidebar.
        {
            auto hdr = juce::Rectangle<int> (sidebarBounds_.getX(), sidebarBounds_.getY(),
                                              sidebarBounds_.getWidth(), scaler_.scaled (22));
            juce::String spaced;
            const juce::String title = "BROWSERS";
            for (int i = 0; i < title.length(); ++i)
            {
                spaced += title[i];
                if (i < title.length() - 1) spaced += " ";
            }
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kValueText));
            g.setFont (juce::FontOptions (11.0f * sf, juce::Font::bold));
            g.drawText (spaced, hdr, juce::Justification::centredLeft);

            // Thin accent rule under header.
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent));
            g.fillRect (hdr.getX(), hdr.getBottom(), scaler_.scaled (36), scaler_.scaled (1));
        }

        auto drawSubPanel = [&] (juce::Rectangle<int> r, const juce::String& title)
        {
            if (r.isEmpty()) return;

            // Panel background — metallic vertical gradient.
            juce::ColourGradient panelGrad (
                juce::Colour (DuskAmpLookAndFeel::kPanel).brighter (0.04f),
                0.0f, float (r.getY()),
                juce::Colour (DuskAmpLookAndFeel::kPanel).darker (0.15f),
                0.0f, float (r.getBottom()),
                false);
            g.setGradientFill (panelGrad);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kBorder));
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);

            // Header title.
            auto hdr = juce::Rectangle<int> (r.getX() + scaler_.scaled (8), r.getY(),
                                              r.getWidth() - scaler_.scaled (16),
                                              scaler_.scaled (16));
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kLabelText));
            g.setFont (juce::FontOptions (9.5f * sf, juce::Font::bold));
            g.drawText (title, hdr, juce::Justification::centredLeft);
        };

        drawSubPanel (sidebarAmpListBounds_, "AMP MODELS");
        drawSubPanel (sidebarCabListBounds_, "CABINET IRs");
    }

    // --- Sub-card metallic panels with per-card titles. Cards touch with
    // no gap — only the outermost corners of each row are rounded so the
    // row reads as a single panel divided into titled sections.
    auto drawCardSegment = [&] (const juce::Rectangle<int>& r,
                                bool firstInRow, bool lastInRow,
                                const juce::String& title)
    {
        if (r.isEmpty()) return;
        auto rf = r.toFloat();
        constexpr float radius = 5.0f;

        juce::Path path;
        path.addRoundedRectangle (rf.getX(), rf.getY(),
                                   rf.getWidth(), rf.getHeight(),
                                   radius, radius,
                                   firstInRow, lastInRow,
                                   firstInRow, lastInRow);

        juce::ColourGradient panelGrad (
            juce::Colour (DuskAmpLookAndFeel::kPanel).darker (0.10f),
            0.0f, rf.getY(),
            juce::Colour (DuskAmpLookAndFeel::kPanel).brighter (0.06f),
            0.0f, rf.getY() + rf.getHeight() * 0.5f,
            false);
        panelGrad.addColour (1.0, juce::Colour (DuskAmpLookAndFeel::kPanel).darker (0.18f));
        g.setGradientFill (panelGrad);
        g.fillPath (path);

        g.setColour (juce::Colour (DuskAmpLookAndFeel::kBorder));
        g.strokePath (path, juce::PathStrokeType (1.0f));

        // Thin internal divider — shared rule between adjacent cards.
        if (! lastInRow)
        {
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kBorder).withAlpha (0.7f));
            g.drawLine (rf.getRight(), rf.getY() + 6.0f,
                        rf.getRight(), rf.getBottom() - 6.0f, 1.0f);
        }

        // Uniform 1-px bottom shadow on every card.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawLine (rf.getX() + 4.0f, rf.getBottom() + 1,
                    rf.getRight() - 4.0f, rf.getBottom() + 1, 1.0f);

        // --- Card title (top-left), tracked uppercase + accent rule ---
        if (title.isNotEmpty())
        {
            const int padX = scaler_.scaled (10);
            const int titleTop = r.getY() + scaler_.scaled (4);
            const int titleH = scaler_.scaled (12);

            juce::String spaced;
            for (int i = 0; i < title.length(); ++i)
            {
                spaced += title[i];
                if (i < title.length() - 1) spaced += " ";
            }
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kLabelText));
            g.setFont (juce::FontOptions (10.0f * sf, juce::Font::bold));
            g.drawText (spaced, r.getX() + padX, titleTop,
                        r.getWidth() - padX * 2, titleH,
                        juce::Justification::centredLeft);

            // 1-px accent rule under the title.
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent));
            g.fillRect (r.getX() + padX,
                        titleTop + titleH + scaler_.scaled (1),
                        scaler_.scaled (22),
                        scaler_.scaled (1));
        }
    };

    const juce::String ampTitle = layoutIsNamMode_ ? "INPUT" : "AMP";
    drawCardSegment (row1LeftCard_,   true,  false, ampTitle);
    drawCardSegment (row1RightCard_,  false, true,  "TONE");
    drawCardSegment (row2LeftCard_,   true,  false, "POWER");
    drawCardSegment (row2RightCard_,  false, true,  "CABINET");
    drawCardSegment (row3DelayCard_,  true,  false, "DELAY");
    drawCardSegment (row3ReverbCard_, false, false, "REVERB");
    drawCardSegment (row3OutputCard_, false, true,  "OUTPUT");

    // DSP-mode AMP-card selector captions — clarify that the left dropdown
    // is the amp MODEL (American/British/AC = tone circuit + power tubes +
    // cab) and the right is the gain CHANNEL. Drawn above each box from its
    // live bounds so the caption tracks the layout automatically.
    if (! layoutIsNamMode_)
    {
        auto caption = [&] (const juce::Component& box, const juce::String& text)
        {
            auto b = box.getBounds();
            if (b.isEmpty()) return;
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kGroupText));
            g.setFont (juce::FontOptions (9.0f * sf, juce::Font::bold));
            g.drawText (text,
                        b.getX(), b.getY() - scaler_.scaled (12),
                        b.getWidth(), scaler_.scaled (11),
                        juce::Justification::centred);
        };
        caption (toneTypeBox_, "MODEL");
        caption (channelBox_,  "CHANNEL");
    }
}

// =============================================================================
// Layout
// =============================================================================

void DuskAmpEditor::resized()
{
    scaler_.updateResizer();
    auto sf = scaler_.getScaleFactor();

    const int margin   = scaler_.scaled (10);
    const int meterGap = scaler_.scaled (6);

    // Mockup layout: left BROWSERS sidebar + main right area. Far-left
    // input meter is a thin sliver; output meter lives inside row 3.
    const int inMeterW    = scaler_.scaled (10);   // de-emphasised sliver
    const int sidebarW    = scaler_.scaled (210);
    const int sidebarGap  = scaler_.scaled (10);

    // No edge meter slivers — meters now live inside the OUTPUT card.
    juce::ignoreUnused (meterGap, inMeterW);
    const int sidebarX = margin;
    const int contentX = sidebarX + sidebarW + sidebarGap;
    const int contentW = getWidth() - contentX - margin;

    // --- Top bar ---
    int topBarH = scaler_.scaled (50);
    int presetW = scaler_.scaled (220);
    int presetH = scaler_.scaled (26);
    int presetY = scaler_.scaled (16);
    int saveW   = scaler_.scaled (50);
    int delW    = scaler_.scaled (36);
    int btnGap  = scaler_.scaled (4);

    int presetStartX = (getWidth() - presetW) / 2 - saveW / 2;
    int dirtyW = scaler_.scaled (14);
    presetBox_.setBounds (presetStartX, presetY, presetW, presetH);
    presetDirtyLabel_.setBounds (presetStartX - dirtyW - scaler_.scaled (2),
                                  presetY, dirtyW, presetH);
    savePresetButton_.setBounds (presetStartX + presetW + btnGap, presetY, saveW, presetH);
    deletePresetButton_.setBounds (presetStartX + presetW + btnGap + saveW + btnGap,
                                   presetY, delW, presetH);

    // Utility cluster on the far right of the top bar — A/B + tuner + OS.
    // DSP/NAM is no longer here; it moved to the top of the amp panel as a
    // primary navigation tab strip (see below).
    int osW    = scaler_.scaled (55);
    int tunerW = scaler_.scaled (60);
    int tunerGap = scaler_.scaled (4);
    int abW   = scaler_.scaled (28);
    int abGap = scaler_.scaled (12); // wider gap so A/B reads as its own cluster
    oversamplingBox_.setBounds (getWidth() - margin - osW, presetY, osW, presetH);
    tunerButton_.setBounds     (getWidth() - margin - osW - tunerGap - tunerW,
                                presetY, tunerW, presetH);
    slotBButton_.setBounds     (getWidth() - margin - osW - tunerGap - tunerW - abGap - abW,
                                presetY, abW, presetH);
    slotAButton_.setBounds     (getWidth() - margin - osW - tunerGap - tunerW - abGap - abW - btnGap - abW,
                                presetY, abW, presetH);

    // --- Mode tab strip (DSP / NAM) ---
    // Sits directly above the centre amp panel as the primary navigation for
    // the amp section. Spans the full centre-column width so the tabs read
    // as section header (not a hidden corner toggle). Main amp groups shift
    // down by the tab height so all 3 columns (input / amp / output) keep
    // their top alignment.
    int tabBarH = scaler_.scaled (32);
    int tabBarY = topBarH + margin;

    // --- Main area ---
    // === Wide signal-flow layout (Modern minimalist Phase R2) ===
    //
    //   topRow (signal flow L→R): INPUT col │ AMP+TONE col │ POWER AMP col │ OUTPUT col
    //   bottomRow:                CABINET (left half) │ EFFECTS (right half)
    //
    // No panel boxes — just section headers with horizontal rules underneath.

    // ===== Three-row flat layout (no cards, no per-feature panels) =====
    //
    // Body splits into 3 horizontal rows separated by thin rules. Each row
    // has a small label in its left gutter, controls fill the rest. All
    // knobs uniform size — equipment-strip aesthetic, not modular cards.

    const int footerReserve = scaler_.scaled (20);
    const int mainY = tabBarY + tabBarH + scaler_.scaled (4);
    const int bodyH = getHeight() - mainY - footerReserve - margin;
    const int rowGap = scaler_.scaled (10);

    // Three roughly-equal rows; row 2 slightly taller for IR list visibility.
    // Row 1 gets the most height — it hosts the 2-row AMP card (knob row +
    // model/channel selector row). Rows 2/3 are single knob rows.
    const int rowH1 = (bodyH - 2 * rowGap) * 36 / 100;
    const int rowH2 = (bodyH - 2 * rowGap) * 32 / 100;
    const int rowH3 = (bodyH - 2 * rowGap) - rowH1 - rowH2;

    const int row1Y = mainY;
    const int row2Y = row1Y + rowH1 + rowGap;
    const int row3Y = row2Y + rowH2 + rowGap;

    const bool namMode = processorRef.parameters.getRawParameterValue (
        DuskAmpParams::AMP_MODE)->load() >= 0.5f;
    layoutIsNamMode_ = namMode;

    // Cards now self-identify with per-card titles, so the rotated
    // right-edge row labels are dropped and the content area widens.
    const int rowsContentW = contentW;

    row1Bounds_ = { contentX, row1Y, rowsContentW, rowH1 };
    row2Bounds_ = { contentX, row2Y, rowsContentW, rowH2 };
    row3Bounds_ = { contentX, row3Y, rowsContentW, rowH3 };

    // Sidebar bounds — full body height to the left of contentX.
    sidebarBounds_ = { sidebarX, mainY, sidebarW, row3Y + rowH3 - mainY };
    const int sidebarHeaderH = scaler_.scaled (24);   // "BROWSERS" title row
    const int subPanelGap    = scaler_.scaled (8);
    if (namMode)
    {
        // Two sub-panels: AMP MODELS (top) + CABINET IRs (bottom).
        const int innerY = sidebarBounds_.getY() + sidebarHeaderH;
        const int innerH = sidebarBounds_.getHeight() - sidebarHeaderH;
        const int eachH  = (innerH - subPanelGap) / 2;
        sidebarAmpListBounds_ = { sidebarX, innerY, sidebarW, eachH };
        sidebarCabListBounds_ = { sidebarX, innerY + eachH + subPanelGap,
                                   sidebarW, innerH - eachH - subPanelGap };
    }
    else
    {
        // DSP mode: only CABINET IRs (no list of DSP voicings).
        sidebarAmpListBounds_ = {};
        sidebarCabListBounds_ = { sidebarX, sidebarBounds_.getY() + sidebarHeaderH,
                                   sidebarW, sidebarBounds_.getHeight() - sidebarHeaderH };
    }

    // Sub-panel headers reserve ~18 px from the top of each browser slot
    // for paint() to draw the "AMP MODELS" / "CABINET IRs" title. The
    // browser components themselves sit just below.
    const int subPanelHeaderH = scaler_.scaled (18);
    auto applyBrowserBounds = [subPanelHeaderH] (juce::Component& c,
                                                 juce::Rectangle<int> r)
    {
        if (r.isEmpty()) { c.setBounds (0, 0, 0, 0); c.setVisible (false); return; }
        c.setVisible (true);
        c.setBounds (r.withTrimmedTop (subPanelHeaderH));
    };
    applyBrowserBounds (namBrowser_, sidebarAmpListBounds_);
    applyBrowserBounds (cabBrowser_, sidebarCabListBounds_);

    // DSP/NAM tabs — narrower, centred above the AMP card.
    const int tabW = scaler_.scaled (280);
    modeSelector_->setBounds ((getWidth() - tabW) / 2, tabBarY, tabW, tabBarH);

    // Shared sizes — uniform knobs + toggles across rows.
    const int knobSize    = scaler_.scaled (56);
    const int widgetGap   = scaler_.scaled (8);
    const int ctrlH       = scaler_.scaled (26);
    const int dropdownW   = scaler_.scaled (72);
    const int toggleW     = scaler_.scaled (64);   // unified across all toggles

    // Each card reserves a top strip for its title; cluster anchors below.
    const int cardTitleH    = scaler_.scaled (18);
    const int cardTitleGap  = scaler_.scaled (4);
    const int clusterTopOff = cardTitleH + cardTitleGap;

    // Height of one knob cluster (name label + rotary + value label).
    const int clusterNameH  = juce::roundToInt (12.0f * sf);
    const int clusterValueH = juce::roundToInt (12.0f * sf);
    const int knobContentH  = clusterNameH + knobSize + clusterValueH + scaler_.scaled (2);

    // Vertically centre a content block of `contentH` in the card area below
    // the title strip — leftover space splits top+bottom so cards aren't
    // bottom-heavy. Never rides up into the title.
    auto centeredTop = [&] (int rowY, int rowH, int contentH)
    {
        const int avail = rowH - clusterTopOff;
        return rowY + clusterTopOff + std::max (0, (avail - contentH) / 2);
    };

    // (Sub-card padding no longer used — cards now touch with no inter-gap
    // and contents centre inside each equal-width slot.)

    // Place a knob (name label + rotary + value) at column X. If topAnchorY
    // is given, the cluster anchors there (top-down). Otherwise it
    // vertical-centres in the row.
    auto placeKnobAt = [&] (KnobWithLabel& k, int colX, int rowY, int rowH,
                             int knobPx, int topAnchorY = -1)
    {
        const int nameH  = juce::roundToInt (12.0f * sf);
        const int valueH = juce::roundToInt (12.0f * sf);
        const int totalH = nameH + knobPx + valueH + scaler_.scaled (2);

        int y = (topAnchorY >= 0)
            ? topAnchorY
            : rowY + std::max ((rowH - totalH) / 2, 0);

        k.nameLabel.setBounds  (colX, y, knobPx, nameH);
        y += nameH;
        k.slider.setBounds     (colX, y, knobPx, knobPx);
        y += knobPx + scaler_.scaled (2);
        k.valueLabel.setVisible (true);
        k.valueLabel.setBounds (colX, y, knobPx, valueH);
    };

    const int rowInsetL = scaler_.scaled (12);   // inset from row left edge
    const int rowInsetR = scaler_.scaled (12);

    // Cluster widths (computed up front, used for horizontal centring).
    // DSP AMP card uses a 2-row layout (knobs over selectors); its width is
    // the wider of the two rows. NAM AMP card is a single IN/GATE/REL row.
    const int ampKnobRowW = namMode ? (3 * knobSize + 2 * widgetGap)   // IN GATE REL
                                     : (4 * knobSize + 3 * widgetGap);  // + GAIN
    const int ampCtrlRowW = dropdownW + widgetGap + dropdownW + widgetGap + toggleW; // Model Channel Bright
    const int row1AmpClusterW = namMode ? ampKnobRowW
                                         : std::max (ampKnobRowW, ampCtrlRowW);
    // DSP TONE card = pure BASS/MID/TREBLE. NAM TONE card keeps the
    // tone-stack-type dropdown (NAM is the amp; this only shapes EQ).
    const int row1ToneClusterW = namMode
        ? (3 * knobSize + 2 * widgetGap + widgetGap + dropdownW)
        : (3 * knobSize + 2 * widgetGap);
    const int row2PowerClusterW = 4 * knobSize + 3 * widgetGap;
    const int row2CabTogW       = scaler_.scaled (72);
    const int row2CabClusterW   = row2CabTogW + widgetGap + 3 * knobSize + 2 * widgetGap;
    // Row-3 FX toggle is narrower than the global toggleW: row 3 packs 3
    // cards so each is only ~rowAvailW/3 wide, and the DELAY cluster
    // (toggle + 3 knobs) overflowed the card at toggleW=64. 52 keeps the
    // cluster inside the card third with margin; "DELAY"/"REVERB" still fit.
    const int row3FxTogW        = scaler_.scaled (52);
    const int row3DelayClusterW  = row3FxTogW + widgetGap + 3 * knobSize + 2 * widgetGap;
    const int row3ReverbClusterW = row3FxTogW + widgetGap + 2 * knobSize + widgetGap;

    // Each row fills the full available width with equal-width cards that
    // touch (no inter-card gap). Equal widths keep visual rhythm consistent
    // and the shared outer edges align all three rows.
    const int rowAvailW = (contentX + rowsContentW - rowInsetR) - (contentX + rowInsetL);

    auto layoutRowCards = [&] (int rowY, int rowH,
                                const std::vector<int>& clusterWidths,
                                std::vector<juce::Rectangle<int>*> outCards)
    {
        jassert (clusterWidths.size() == outCards.size());
        juce::ignoreUnused (clusterWidths);

        const int rowX0 = contentX + rowInsetL;
        const int n = static_cast<int> (outCards.size());
        int x = rowX0;
        for (int i = 0; i < n; ++i)
        {
            // Last card consumes any rounding remainder so the row ends
            // exactly on the right inset edge.
            const int cardW = (i == n - 1) ? (rowX0 + rowAvailW - x)
                                            : (rowAvailW / n);
            *outCards[i] = { x, rowY, cardW, rowH };
            x += cardW;
        }
    };

    auto clusterX = [&] (const juce::Rectangle<int>& card, int clusterW)
    {
        return card.getX() + (card.getWidth() - clusterW) / 2;
    };

    // Y for a small control (toggle / dropdown) vertically aligned with
    // the knob slider when the cluster is top-anchored at `topAnchor`.
    auto ctrlYAtKnob = [&] (int topAnchor, int ctrlSize)
    {
        const int nameH = juce::roundToInt (12.0f * sf);
        return topAnchor + nameH + (knobSize - ctrlSize) / 2;
    };

    const int meterColW = scaler_.scaled (12);   // I/O meter sliver in OUTPUT card

    // ----- ROW 1: AMP / NAM — 2 cards [AMP] [TONE] -------------------------
    {
        layoutRowCards (row1Y, rowH1, { row1AmpClusterW, row1ToneClusterW },
                         { &row1LeftCard_, &row1RightCard_ });

        const int knobTotalH = knobContentH;
        // TONE card content is one knob row in both modes; centre it.
        const int toneTop = centeredTop (row1Y, rowH1, knobTotalH);

        if (namMode)
        {
            // --- INPUT card: IN/GATE/REL single row (centred) ---
            const int inpTop = centeredTop (row1Y, rowH1, knobTotalH);
            int x = clusterX (row1LeftCard_, ampKnobRowW);
            placeKnobAt (inputGain_,     x, row1Y, rowH1, knobSize, inpTop); x += knobSize + widgetGap;
            placeKnobAt (gateThreshold_, x, row1Y, rowH1, knobSize, inpTop); x += knobSize + widgetGap;
            placeKnobAt (gateRelease_,   x, row1Y, rowH1, knobSize, inpTop);

            // DSP-only controls hidden + disabled (still bound to live params).
            preampGain_.slider.setVisible (false);
            preampGain_.nameLabel.setVisible (false);
            preampGain_.valueLabel.setVisible (false);
            channelBox_.setVisible (false);
            brightButton_.setVisible (false);
            preampGain_.slider.setEnabled (false);
            channelBox_.setEnabled (false);
            brightButton_.setEnabled (false);

            // --- TONE card: BASS/MID/TREBLE + tone-stack-type dropdown ---
            int tx = clusterX (row1RightCard_, row1ToneClusterW);
            placeKnobAt (bass_,   tx, row1Y, rowH1, knobSize, toneTop); tx += knobSize + widgetGap;
            placeKnobAt (mid_,    tx, row1Y, rowH1, knobSize, toneTop); tx += knobSize + widgetGap;
            placeKnobAt (treble_, tx, row1Y, rowH1, knobSize, toneTop); tx += knobSize + widgetGap;
            toneTypeBox_.setBounds (tx, ctrlYAtKnob (toneTop, ctrlH), dropdownW, ctrlH);
        }
        else
        {
            // --- AMP card: 2-row layout. Knobs on top, model/channel/bright
            // selectors below. The American/British/AC selector lives here
            // (not in TONE) because in DSP mode it picks the whole amp model
            // — tone circuit + power tubes + cab IR. The combined block
            // (knob row + caption + selector row) is vertically centred. ---
            const int captionH = scaler_.scaled (11);
            const int blockH   = knobTotalH + captionH + scaler_.scaled (4) + ctrlH;
            const int ampTop   = centeredTop (row1Y, rowH1, blockH);

            int x = clusterX (row1LeftCard_, ampKnobRowW);
            placeKnobAt (inputGain_,     x, row1Y, rowH1, knobSize, ampTop); x += knobSize + widgetGap;
            placeKnobAt (gateThreshold_, x, row1Y, rowH1, knobSize, ampTop); x += knobSize + widgetGap;
            placeKnobAt (gateRelease_,   x, row1Y, rowH1, knobSize, ampTop); x += knobSize + widgetGap;
            placeKnobAt (preampGain_,    x, row1Y, rowH1, knobSize, ampTop);

            const int ctrlRowY = ampTop + knobTotalH + captionH + scaler_.scaled (4);
            int cx = clusterX (row1LeftCard_, ampCtrlRowW);
            toneTypeBox_.setBounds (cx, ctrlRowY, dropdownW, ctrlH); cx += dropdownW + widgetGap;
            channelBox_.setBounds  (cx, ctrlRowY, dropdownW, ctrlH); cx += dropdownW + widgetGap;
            brightButton_.setBounds (cx, ctrlRowY, toggleW, ctrlH);

            preampGain_.slider.setVisible (true);
            preampGain_.nameLabel.setVisible (true);
            preampGain_.valueLabel.setVisible (true);
            channelBox_.setVisible (true);
            brightButton_.setVisible (true);
            preampGain_.slider.setEnabled (true);
            channelBox_.setEnabled (true);
            brightButton_.setEnabled (true);

            // --- TONE card: pure BASS/MID/TREBLE, vertically centred ---
            int tx = clusterX (row1RightCard_, row1ToneClusterW);
            placeKnobAt (bass_,   tx, row1Y, rowH1, knobSize, toneTop); tx += knobSize + widgetGap;
            placeKnobAt (mid_,    tx, row1Y, rowH1, knobSize, toneTop); tx += knobSize + widgetGap;
            placeKnobAt (treble_, tx, row1Y, rowH1, knobSize, toneTop);
        }
    }

    // ----- ROW 2: POWER + CAB — 2 cards [POWER] [CAB] ----------------------
    {
        layoutRowCards (row2Y, rowH2, { row2PowerClusterW, row2CabClusterW },
                         { &row2LeftCard_, &row2RightCard_ });

        const int topA = centeredTop (row2Y, rowH2, knobContentH);

        // --- POWER card content ---
        int x = clusterX (row2LeftCard_, row2PowerClusterW);
        placeKnobAt (powerDrive_, x, row2Y, rowH2, knobSize, topA); x += knobSize + widgetGap;
        placeKnobAt (presence_,   x, row2Y, rowH2, knobSize, topA); x += knobSize + widgetGap;
        placeKnobAt (resonance_,  x, row2Y, rowH2, knobSize, topA); x += knobSize + widgetGap;
        placeKnobAt (sag_,        x, row2Y, rowH2, knobSize, topA);

        // --- CAB card content ---
        int cx = clusterX (row2RightCard_, row2CabClusterW);
        const int togH    = scaler_.scaled (24);
        const int togGap  = scaler_.scaled (6);
        // Vertically centre the 2-toggle stack against the knob slider
        // band (so toggles align with knob bodies, not full row).
        const int togStackH = togH * 2 + togGap;
        const int togColTop = topA + juce::roundToInt (12.0f * sf)
                                 + (knobSize - togStackH) / 2;
        cabEnabled_.setBounds   (cx, togColTop,                row2CabTogW, togH);
        cabNormalize_.setBounds (cx, togColTop + togH + togGap, row2CabTogW, togH);
        cx += row2CabTogW + widgetGap;
        placeKnobAt (cabMix_,   cx, row2Y, rowH2, knobSize, topA); cx += knobSize + widgetGap;
        placeKnobAt (cabHiCut_, cx, row2Y, rowH2, knobSize, topA); cx += knobSize + widgetGap;
        placeKnobAt (cabLoCut_, cx, row2Y, rowH2, knobSize, topA);
    }

    // ----- ROW 3: FX + OUT — 3 cards [DELAY] [REVERB] [OUTPUT] -------------
    // OUTPUT card now holds the I/O meters inline (replacing the dropped
    // far-edge slivers).
    const int outputClusterWWithMeters =
        meterColW + widgetGap + knobSize + widgetGap + meterColW;
    {
        const int fxTogH = scaler_.scaled (24);

        layoutRowCards (row3Y, rowH3,
                         { row3DelayClusterW, row3ReverbClusterW, outputClusterWWithMeters },
                         { &row3DelayCard_, &row3ReverbCard_, &row3OutputCard_ });

        const int topA = centeredTop (row3Y, rowH3, knobContentH);

        // --- DELAY card content ---
        int x = clusterX (row3DelayCard_, row3DelayClusterW);
        delayEnabled_.setBounds (x, ctrlYAtKnob (topA, fxTogH), row3FxTogW, fxTogH);
        x += row3FxTogW + widgetGap;
        placeKnobAt (delayTime_,     x, row3Y, rowH3, knobSize, topA); x += knobSize + widgetGap;
        placeKnobAt (delayFeedback_, x, row3Y, rowH3, knobSize, topA); x += knobSize + widgetGap;
        placeKnobAt (delayMix_,      x, row3Y, rowH3, knobSize, topA);

        // --- REVERB card content ---
        int rx = clusterX (row3ReverbCard_, row3ReverbClusterW);
        reverbEnabled_.setBounds (rx, ctrlYAtKnob (topA, fxTogH), row3FxTogW, fxTogH);
        rx += row3FxTogW + widgetGap;
        placeKnobAt (reverbMix_,   rx, row3Y, rowH3, knobSize, topA); rx += knobSize + widgetGap;
        placeKnobAt (reverbDecay_, rx, row3Y, rowH3, knobSize, topA);

        // --- OUTPUT card content: [IN-meter] [OUTPUT knob] [OUT-meter] ---
        int ox = clusterX (row3OutputCard_, outputClusterWWithMeters);
        const int nameH = juce::roundToInt (12.0f * sf);
        const int valueH = juce::roundToInt (12.0f * sf);
        const int meterTop = topA + nameH;
        const int meterH   = knobSize + valueH + scaler_.scaled (2);

        inputMeter_.setBounds (ox, meterTop, meterColW, meterH);
        ox += meterColW + widgetGap;

        placeKnobAt (outputLevel_, ox, row3Y, rowH3, knobSize, topA);
        ox += knobSize + widgetGap;

        outputMeter_.setBounds (ox, meterTop, meterColW, meterH);
    }

    // ----- Footer strip -----------------------------------------------------
    const int footerH = scaler_.scaled (16);
    const int footerY = getHeight() - footerH - scaler_.scaled (2);
    const int footerVerW = scaler_.scaled (110);
    const int footerCpuW = scaler_.scaled (60);
    footerVersionLabel_.setBounds (contentX, footerY, footerVerW, footerH);
    footerCpuLabel_.setBounds (contentX + contentW - footerCpuW, footerY, footerCpuW, footerH);
    footerTooltipLabel_.setBounds (contentX + footerVerW + scaler_.scaled (6), footerY,
                                    contentW - footerVerW - footerCpuW - scaler_.scaled (12),
                                    footerH);

    // Input/output meters are positioned inside the OUTPUT card (see Row 3).
    juce::ignoreUnused (footerY);
}

// =============================================================================
// User Preset Management
// =============================================================================

void DuskAmpEditor::capturePresetSnapshot()
{
    presetSnapshot_.clear();
    for (auto* p : processorRef.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            presetSnapshot_.push_back (ranged->getValue());
    }
    presetDirty_ = false;
    presetDirtyLabel_.setText ("", juce::dontSendNotification);
}

bool DuskAmpEditor::currentStateMatchesSnapshot() const
{
    size_t idx = 0;
    for (auto* p : processorRef.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (idx >= presetSnapshot_.size())
                return false;
            // Compare normalised values with a tiny epsilon so float-roundtrip
            // through APVTS smoothing doesn't trip false-positive dirtiness.
            if (std::abs (ranged->getValue() - presetSnapshot_[idx]) > 1.0e-4f)
                return false;
            ++idx;
        }
    }
    return idx == presetSnapshot_.size();
}

void DuskAmpEditor::refreshPresetList()
{
    int currentId = presetBox_.getSelectedId();
    presetBox_.clear (juce::dontSendNotification);

    // Factory presets (IDs 1 to kNumFactoryPresets)
    presetBox_.addSectionHeading ("Factory Presets");
    for (int i = 0; i < kNumFactoryPresets; ++i)
        presetBox_.addItem (kFactoryPresets[i].name, i + 1);

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

    // Restore selection
    if (currentId > 0)
        presetBox_.setSelectedId (currentId, juce::dontSendNotification);
}

void DuskAmpEditor::saveUserPreset()
{
    if (! userPresetManager_)
        return;

    auto* dialog = new juce::AlertWindow ("Save Preset",
                                           "Enter a name for this preset:",
                                           juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor ("name", "My Preset", "Preset Name:");
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<DuskAmpEditor> safeThis (this);
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
                juce::Component::SafePointer<DuskAmpEditor> safeInner (safeThis.getComponent());

                juce::AlertWindow::showOkCancelBox (
                    juce::MessageBoxIconType::QuestionIcon,
                    "Overwrite Preset?",
                    "A preset named \"" + name + "\" already exists. Overwrite it?",
                    "Overwrite", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create ([safeInner, name] (int confirmResult) {
                        if (confirmResult == 1 && safeInner != nullptr)
                        {
                            auto state = safeInner->processorRef.parameters.copyState();
                            if (safeInner->userPresetManager_->saveUserPreset (name, state, JucePlugin_VersionString))
                                safeInner->refreshPresetList();
                        }
                    }));
            }
            else
            {
                auto state = safeThis->processorRef.parameters.copyState();
                if (safeThis->userPresetManager_->saveUserPreset (name, state, JucePlugin_VersionString))
                    safeThis->refreshPresetList();
            }
        }), true);
}

void DuskAmpEditor::loadUserPreset (const juce::String& name)
{
    if (! userPresetManager_)
        return;

    auto state = userPresetManager_->loadUserPreset (name);
    if (state.isValid())
    {
        processorRef.parameters.replaceState (state);
        processorRef.parameters.state.setProperty ("presetName", name, nullptr);
    }
}

void DuskAmpEditor::deleteUserPreset (const juce::String& name)
{
    if (! userPresetManager_)
        return;

    userPresetManager_->deleteUserPreset (name);
    refreshPresetList();
}

// =============================================================================
// Supporters Overlay
// =============================================================================

void DuskAmpEditor::mouseDown (const juce::MouseEvent& e)
{
    if (titleClickArea_.contains (e.getPosition()))
        showSupportersPanel();
}

void DuskAmpEditor::showSupportersPanel()
{
    if (! supportersOverlay_)
    {
        supportersOverlay_ = std::make_unique<SupportersOverlay> ("DuskAmp", JucePlugin_VersionString);
        supportersOverlay_->onDismiss = [this] { hideSupportersPanel(); };
        addAndMakeVisible (supportersOverlay_.get());
    }
    supportersOverlay_->setBounds (getLocalBounds());
    supportersOverlay_->toFront (true);
    supportersOverlay_->setVisible (true);
}

void DuskAmpEditor::hideSupportersPanel()
{
    if (supportersOverlay_)
        supportersOverlay_->setVisible (false);
}

void DuskAmpEditor::showTunerPanel()
{
    if (! tunerOverlay_)
    {
        tunerOverlay_ = std::make_unique<TunerOverlay>();
        tunerOverlay_->onDismiss = [this] { hideTunerPanel(); };
        addAndMakeVisible (tunerOverlay_.get());

        // Bind the overlay's reference-Hz editor to TUNER_REF_HZ so the
        // user's calibration persists in the host project state.
        tunerRefHzAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.parameters, DuskAmpParams::TUNER_REF_HZ,
            tunerOverlay_->getRefHzSlider());
    }
    tunerOverlay_->setBounds (getLocalBounds());
    tunerOverlay_->toFront (true);
    tunerOverlay_->setVisible (true);
    processorRef.setTunerActive (true); // mute output while tuning
    tunerButton_.setActive (true);
}

void DuskAmpEditor::hideTunerPanel()
{
    if (tunerOverlay_)
        tunerOverlay_->setVisible (false);
    processorRef.setTunerActive (false);
    tunerButton_.setActive (false);
}

void DuskAmpEditor::updateDeleteButtonVisibility()
{
    deletePresetButton_.setVisible (presetBox_.getSelectedId() >= 1001);
}

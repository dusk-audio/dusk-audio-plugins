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

    // Store suffix in name field for formatting
    valueLabel.setName (suffix);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);

    // Set textFromValueFunction AFTER attachment (attachment overwrites it)
    auto sfx = suffix;
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

static constexpr int kBaseWidth  = 1050;
static constexpr int kBaseHeight = 720;

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
    boostGain_     .init (*this, params, DuskAmpParams::BOOST_GAIN,       "GAIN",       "%",
        "Boost pedal drive amount");
    boostTone_     .init (*this, params, DuskAmpParams::BOOST_TONE,      "TONE",       "%",
        "Boost pedal tone (dark to bright)");
    boostLevel_    .init (*this, params, DuskAmpParams::BOOST_LEVEL,     "LEVEL",      "%",
        "Boost pedal output level");
    reverbMix_     .init (*this, params, DuskAmpParams::REVERB_MIX,      "MIX",        "%",
        "Reverb wet/dry mix");
    reverbDecay_   .init (*this, params, DuskAmpParams::REVERB_DECAY,    "DECAY",      "%",
        "Reverb tail length");
    reverbPreDelay_.init (*this, params, DuskAmpParams::REVERB_PREDELAY, "PRE-DLY",    " ms",
        "Reverb pre-delay time");
    reverbDamping_ .init (*this, params, DuskAmpParams::REVERB_DAMPING,  "DAMP",       "%",
        "Reverb high-frequency damping");
    reverbSize_    .init (*this, params, DuskAmpParams::REVERB_SIZE,     "SIZE",       "%",
        "Reverb room size");
    outputLevel_   .init (*this, params, DuskAmpParams::OUTPUT_LEVEL,    "OUTPUT",     " dB",
        "Master output level");

    // --- Mode selector (DSP / NAM) ---
    auto* modeParam = params.getParameter (DuskAmpParams::AMP_MODE);
    jassert (modeParam != nullptr);
    modeSelector_ = std::make_unique<AmpModeSelector> (*modeParam);
    addAndMakeVisible (*modeSelector_);

    // --- Amp type selector ---
    ampTypeBox_.addItemList ({ "Fender", "Marshall", "Vox" }, 1);
    ampTypeBox_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (ampTypeBox_);
    ampTypeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        params, DuskAmpParams::AMP_TYPE, ampTypeBox_);

    // --- Bright toggle ---
    brightButton_.setButtonText ("BRIGHT");
    brightButton_.setClickingTogglesState (true);
    addAndMakeVisible (brightButton_);
    brightAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::PREAMP_BRIGHT, brightButton_);

    // --- Cabinet enabled toggle ---
    cabEnabled_.setButtonText ("CAB");
    cabEnabled_.setClickingTogglesState (true);
    addAndMakeVisible (cabEnabled_);
    cabEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::CAB_ENABLED, cabEnabled_);

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

    // --- Boost enabled toggle ---
    boostEnabled_.setButtonText ("BOOST");
    boostEnabled_.setClickingTogglesState (true);
    addAndMakeVisible (boostEnabled_);
    boostEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::BOOST_ENABLED, boostEnabled_);

    // --- Delay enabled toggle ---
    delayEnabled_.setButtonText ("DELAY");
    delayEnabled_.setClickingTogglesState (true);
    addAndMakeVisible (delayEnabled_);
    delayEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::DELAY_ENABLED, delayEnabled_);

    // --- Delay type selector ---
    delayTypeBox_.addItemList ({ "Digital", "Analog", "Tape" }, 1);
    delayTypeBox_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (delayTypeBox_);
    delayTypeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        params, DuskAmpParams::DELAY_TYPE, delayTypeBox_);

    // --- Reverb enabled toggle ---
    reverbEnabled_.setButtonText ("REVERB");
    reverbEnabled_.setClickingTogglesState (true);
    addAndMakeVisible (reverbEnabled_);
    reverbEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::REVERB_ENABLED, reverbEnabled_);

    // --- Oversampling selector ---
    oversamplingBox_.addItemList ({ "2x", "4x" }, 1);
    oversamplingBox_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (oversamplingBox_);
    oversamplingAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        params, DuskAmpParams::OVERSAMPLING, oversamplingBox_);

    // --- User preset manager ---
    userPresetManager_ = std::make_unique<UserPresetManager> ("DuskAmp");

    // --- Preset browser ---
    presetBox_.setJustificationType (juce::Justification::centred);
    presetBox_.onChange = [this]
    {
        int id = presetBox_.getSelectedId();
        if (id >= 1 && id <= kNumFactoryPresets)
        {
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
    };
    addAndMakeVisible (presetBox_);
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

    setSize (scaler_.getStoredWidth(), scaler_.getStoredHeight());
    startTimerHz (30);
}

DuskAmpEditor::~DuskAmpEditor()
{
    scaler_.saveSize();
    setLookAndFeel (nullptr);
}

// =============================================================================
// Timer — update value labels & meters
// =============================================================================

void DuskAmpEditor::timerCallback()
{
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
    update (boostGain_);
    update (boostTone_);
    update (boostLevel_);
    update (reverbMix_);
    update (reverbDecay_);
    update (reverbPreDelay_);
    update (reverbDamping_);
    update (reverbSize_);
    update (outputLevel_);

    // Dim boost knobs when boost is disabled
    bool boostOff = ! boostEnabled_.getToggleState();
    boostGain_.setDimmed (boostOff);
    boostTone_.setDimmed (boostOff);
    boostLevel_.setDimmed (boostOff);

    // Dim cabinet knobs when cab is disabled
    bool cabOff = ! cabEnabled_.getToggleState();
    cabMix_.setDimmed (cabOff);
    cabHiCut_.setDimmed (cabOff);
    cabLoCut_.setDimmed (cabOff);
    cabBrowser_.setEnabled (! cabOff);
    cabBrowser_.setAlpha (cabOff ? 0.4f : 1.0f);

    // Dim delay knobs when delay is disabled
    bool delayOff = ! delayEnabled_.getToggleState();
    delayTime_.setDimmed (delayOff);
    delayFeedback_.setDimmed (delayOff);
    delayMix_.setDimmed (delayOff);
    delayTypeBox_.setEnabled (! delayOff);
    delayTypeBox_.setAlpha (delayOff ? 0.4f : 1.0f);

    // Dim reverb knobs when reverb is disabled
    bool reverbOff = ! reverbEnabled_.getToggleState();
    reverbMix_.setDimmed (reverbOff);
    reverbDecay_.setDimmed (reverbOff);
    reverbPreDelay_.setDimmed (reverbOff);
    reverbDamping_.setDimmed (reverbOff);
    reverbSize_.setDimmed (reverbOff);

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
    ampTypeBox_.setEnabled (! namMode);
    ampTypeBox_.setAlpha (namMode ? 0.4f : 1.0f);
    brightButton_.setEnabled (! namMode);
    brightButton_.setAlpha (namMode ? 0.4f : 1.0f);

    // Update meters
    inputMeter_.setStereoLevels (processorRef.getInputLevelL(),
                                 processorRef.getInputLevelR());
    outputMeter_.setStereoLevels (processorRef.getOutputLevelL(),
                                  processorRef.getOutputLevelR());
    inputMeter_.repaint();
    outputMeter_.repaint();
}

// =============================================================================
// Paint
// =============================================================================

static void drawGroupBox (juce::Graphics& g, juce::Rectangle<int> bounds,
                          const juce::String& title, bool centerTitle = false)
{
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kPanel));
    g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

    g.setColour (juce::Colour (DuskAmpLookAndFeel::kBorder));
    g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);

    g.setColour (juce::Colour (DuskAmpLookAndFeel::kGroupText));
    g.setFont (juce::FontOptions (10.0f));

    juce::String spaced;
    for (int i = 0; i < title.length(); ++i)
    {
        spaced += title[i];
        if (i < title.length() - 1)
            spaced += ' ';
    }

    auto titleArea = bounds.withHeight (20);
    if (centerTitle)
        g.drawText (spaced, titleArea, juce::Justification::centred);
    else
        g.drawText (spaced, titleArea.withTrimmedLeft (10), juce::Justification::centredLeft);

    int underlineW = juce::jmin (static_cast<int> (title.length()) * 12, bounds.getWidth() - 20);
    int underlineX = centerTitle ? bounds.getX() + (bounds.getWidth() - underlineW) / 2
                                 : bounds.getX() + 10;
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent).withAlpha (0.3f));
    g.fillRect (underlineX, bounds.getY() + 19, underlineW, 2);
}

void DuskAmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (DuskAmpLookAndFeel::kBackground));

    auto sf = scaler_.getScaleFactor();

    // Title
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kText));
    g.setFont (juce::FontOptions (22.0f * sf, juce::Font::bold));
    titleClickArea_ = { scaler_.scaled (12), scaler_.scaled (8),
                        scaler_.scaled (180), scaler_.scaled (30) };
    g.drawText ("DUSKAMP", titleClickArea_, juce::Justification::centredLeft);

    // Subtitle
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kSubtleText));
    g.setFont (juce::FontOptions (11.0f * sf));
    g.drawText ("Guitar Amp Simulator", scaler_.scaled (12), scaler_.scaled (30),
                scaler_.scaled (200), scaler_.scaled (16), juce::Justification::centredLeft);

    // Divider line under top bar
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kBorder));
    int dividerY = scaler_.scaled (48);
    int margin = scaler_.scaled (10);
    int meterW = scaler_.scaled (22);
    int meterGap = scaler_.scaled (6);
    int contentX = margin + meterW + meterGap;
    int contentW = getWidth() - 2 * (margin + meterW + meterGap);
    g.drawHorizontalLine (dividerY, static_cast<float> (contentX),
                          static_cast<float> (contentX + contentW));

    // Group boxes from stored bounds
    drawGroupBox (g, inputGroupBounds_, "INPUT", true);
    drawGroupBox (g, outputGroupBounds_, "OUTPUT", true);

    if (layoutIsNamMode_)
    {
        drawGroupBox (g, centerTopBounds_, "NAM MODEL");
        drawGroupBox (g, centerMidBounds_, "TONE");
    }
    else
    {
        drawGroupBox (g, centerTopBounds_, "AMP / TONE");
    }
    drawGroupBox (g, centerBotBounds_, "POWER AMP");
    drawGroupBox (g, boostGroupBounds_, "BOOST");
    drawGroupBox (g, cabGroupBounds_, "CABINET");
    drawGroupBox (g, delayGroupBounds_, "DELAY");
    drawGroupBox (g, reverbGroupBounds_, "REVERB");

    // Meter labels
    int mainY = inputGroupBounds_.getY();
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kGroupText));
    g.setFont (juce::FontOptions (8.0f * sf));
    g.drawText ("IN", margin, mainY - scaler_.scaled (12), meterW, scaler_.scaled (12),
                juce::Justification::centred);
    g.drawText ("OUT", getWidth() - margin - meterW, mainY - scaler_.scaled (12), meterW,
                scaler_.scaled (12), juce::Justification::centred);
}

// =============================================================================
// Layout
// =============================================================================

static void placeKnob (KnobWithLabel& k, juce::Rectangle<int> area, int knobSize,
                       float scaleFactor)
{
    int nameH  = juce::roundToInt (14.0f * scaleFactor);
    bool largeKnob = (knobSize >= juce::roundToInt (90.0f * scaleFactor));
    int valueH = largeKnob ? 0 : juce::roundToInt (14.0f * scaleFactor);
    int totalH = nameH + knobSize + valueH;

    int yPad = (area.getHeight() - totalH) / 2;
    auto col = area;
    if (yPad > 0)
        col.removeFromTop (yPad);

    k.nameLabel.setBounds (col.removeFromTop (nameH));

    auto knobArea = col.removeFromTop (knobSize);
    k.slider.setBounds (knobArea.withSizeKeepingCentre (knobSize, knobSize));

    if (largeKnob)
    {
        k.valueLabel.setBounds (0, 0, 0, 0);
        k.valueLabel.setVisible (false);
    }
    else
    {
        k.valueLabel.setVisible (true);
        k.valueLabel.setBounds (col.removeFromTop (juce::roundToInt (14.0f * scaleFactor)));
    }
}

static void layoutKnobsInGroup (juce::Rectangle<int> groupBounds, int topPad,
                                std::vector<std::pair<KnobWithLabel*, int>> knobs,
                                float scaleFactor)
{
    auto area = groupBounds.reduced (4, 0);
    area.removeFromTop (topPad);

    int numKnobs = static_cast<int> (knobs.size());
    int colW = area.getWidth() / numKnobs;

    for (auto& [knob, knobSize] : knobs)
    {
        auto col = area.removeFromLeft (colW);
        placeKnob (*knob, col, knobSize, scaleFactor);
    }
}

void DuskAmpEditor::resized()
{
    scaler_.updateResizer();
    auto sf = scaler_.getScaleFactor();

    int margin   = scaler_.scaled (10);
    int meterW   = scaler_.scaled (22);
    int meterGap = scaler_.scaled (6);
    int gap      = scaler_.scaled (10);
    int topPad   = scaler_.scaled (24);

    int contentX = margin + meterW + meterGap;
    int contentW = getWidth() - 2 * (margin + meterW + meterGap);

    // Knob sizes (scaled to ~10% of window width for main knobs)
    int smallKnob  = scaler_.scaled (60);
    int mediumKnob = scaler_.scaled (76);
    int largeKnob  = scaler_.scaled (100);

    // --- Top bar ---
    int topBarH = scaler_.scaled (50);
    int presetW = scaler_.scaled (220);
    int presetH = scaler_.scaled (26);
    int presetY = scaler_.scaled (16);
    int saveW   = scaler_.scaled (50);
    int delW    = scaler_.scaled (36);
    int btnGap  = scaler_.scaled (4);

    int presetStartX = (getWidth() - presetW) / 2 - saveW / 2;
    presetBox_.setBounds (presetStartX, presetY, presetW, presetH);
    savePresetButton_.setBounds (presetStartX + presetW + btnGap, presetY, saveW, presetH);
    deletePresetButton_.setBounds (presetStartX + presetW + btnGap + saveW + btnGap,
                                   presetY, delW, presetH);

    int modeW = scaler_.scaled (150);
    int modeH = scaler_.scaled (30);
    int modeY = presetY + (presetH - modeH) / 2;
    modeSelector_->setBounds (getWidth() - margin - meterW - modeW - scaler_.scaled (65),
                              modeY, modeW, modeH);

    int osW = scaler_.scaled (55);
    oversamplingBox_.setBounds (getWidth() - margin - meterW - osW, presetY, osW, presetH);

    // --- Main area ---
    int mainY = topBarH + margin;
    int bottomH = scaler_.scaled (180);
    int mainH = getHeight() - mainY - gap - bottomH - margin;

    // Proportional columns
    int usable = contentW - gap * 2;
    int leftColW  = static_cast<int> (usable * 0.12f);
    int rightColW = static_cast<int> (usable * 0.12f);
    int centerW   = usable - leftColW - rightColW;

    int inputX  = contentX;
    int centerX = inputX + leftColW + gap;
    int outputX = centerX + centerW + gap;

    // Store group bounds
    inputGroupBounds_  = { inputX, mainY, leftColW, mainH };
    outputGroupBounds_ = { outputX, mainY, rightColW, mainH };

    // --- LEFT: INPUT ---
    layoutKnobsInGroup ({ inputX, mainY, leftColW, mainH / 3 }, topPad,
        { { &inputGain_, mediumKnob } }, sf);
    layoutKnobsInGroup ({ inputX, mainY + mainH / 3, leftColW, mainH / 3 }, scaler_.scaled (4),
        { { &gateThreshold_, smallKnob } }, sf);
    layoutKnobsInGroup ({ inputX, mainY + 2 * mainH / 3, leftColW, mainH / 3 }, scaler_.scaled (4),
        { { &gateRelease_, smallKnob } }, sf);

    // --- RIGHT: OUTPUT ---
    layoutKnobsInGroup ({ outputX, mainY, rightColW, mainH }, topPad,
        { { &outputLevel_, largeKnob } }, sf);

    // --- CENTER: mode-dependent ---
    bool namMode = processorRef.parameters.getRawParameterValue (
        DuskAmpParams::AMP_MODE)->load() >= 0.5f;
    layoutIsNamMode_ = namMode;

    int controlsH = scaler_.scaled (28);
    int controlsGap = scaler_.scaled (6);

    if (namMode)
    {
        // 3-row: NAM BROWSER (30%) | TONE (35%) | POWER AMP (35%)
        int centerH = mainH;
        int namH  = static_cast<int> (centerH * 0.30f);
        int toneH = static_cast<int> (centerH * 0.35f);
        int powH  = centerH - namH - toneH - gap * 2;

        int toneY = mainY + namH + gap;
        int powY  = toneY + toneH + gap;

        centerTopBounds_ = { centerX, mainY, centerW, namH };
        centerMidBounds_ = { centerX, toneY, centerW, toneH };
        centerBotBounds_ = { centerX, powY, centerW, powH };

        // NAM browser: inside the top group
        namBrowser_.setBounds (centerX + scaler_.scaled (8), mainY + topPad,
                               centerW - scaler_.scaled (16), namH - topPad - scaler_.scaled (4));

        // Tone: bass, mid, treble (knobs take the top portion)
        int toneKnobH = toneH - controlsH - controlsGap - topPad;
        layoutKnobsInGroup ({ centerX, toneY, centerW, toneKnobH + topPad }, topPad,
            { { &bass_, mediumKnob }, { &mid_, mediumKnob }, { &treble_, mediumKnob } }, sf);

        // Tone type dropdown (pinned to bottom of TONE section)
        {
            int ctrlY = toneY + toneH - controlsH - scaler_.scaled (4);
            int ctrlW = scaler_.scaled (130);
            int ctrlX = centerX + (centerW - ctrlW) / 2;
            ampTypeBox_.setBounds (ctrlX, ctrlY, ctrlW, controlsH);
        }

        // Power amp
        layoutKnobsInGroup ({ centerX, powY, centerW, powH }, topPad,
            { { &powerDrive_, mediumKnob }, { &presence_, smallKnob },
              { &resonance_, smallKnob }, { &sag_, smallKnob } }, sf);

        // Hide DSP preamp controls
        preampGain_.slider.setVisible (false);
        preampGain_.nameLabel.setVisible (false);
        preampGain_.valueLabel.setVisible (false);
        ampTypeBox_.setVisible (false);
        brightButton_.setVisible (false);
        namBrowser_.setVisible (true);
    }
    else
    {
        // 2-row: AMP/TONE (45%) | POWER AMP (55%)
        int centerH = mainH;
        int ampToneH = static_cast<int> (centerH * 0.45f);
        int powH     = centerH - ampToneH - gap;
        int powY     = mainY + ampToneH + gap;

        centerTopBounds_ = { centerX, mainY, centerW, ampToneH };
        centerMidBounds_ = {}; // unused in DSP mode
        centerBotBounds_ = { centerX, powY, centerW, powH };

        // AMP/TONE: gain + bass/mid/treble (all medium)
        layoutKnobsInGroup ({ centerX, mainY, centerW, ampToneH - controlsH - controlsGap }, topPad,
            { { &preampGain_, mediumKnob }, { &bass_, mediumKnob },
              { &mid_, mediumKnob }, { &treble_, mediumKnob } }, sf);

        // Controls row: amp type + bright
        {
            int ctrlY = mainY + ampToneH - controlsH - scaler_.scaled (2);
            int ctrlW = (centerW - scaler_.scaled (16) - controlsGap) / 2;
            int ctrlX = centerX + scaler_.scaled (8);
            ampTypeBox_.setBounds (ctrlX, ctrlY, ctrlW, controlsH);
            brightButton_.setBounds (ctrlX + ctrlW + controlsGap, ctrlY, ctrlW, controlsH);
        }

        // Power amp
        layoutKnobsInGroup ({ centerX, powY, centerW, powH }, topPad,
            { { &powerDrive_, mediumKnob }, { &presence_, smallKnob },
              { &resonance_, smallKnob }, { &sag_, smallKnob } }, sf);

        // Show DSP preamp controls, hide NAM browser
        preampGain_.slider.setVisible (true);
        preampGain_.nameLabel.setVisible (true);
        preampGain_.valueLabel.setVisible (true);
        ampTypeBox_.setVisible (true);
        brightButton_.setVisible (true);
        namBrowser_.setVisible (false);
    }

    // --- Bottom row: BOOST | CABINET | DELAY | REVERB ---
    int bottomY = mainY + mainH + gap;
    int toggleW = scaler_.scaled (56);
    int toggleH = scaler_.scaled (22);
    int pad = scaler_.scaled (8);

    // 4-column layout: boost (15%) | cab (35%) | delay (25%) | reverb (25%)
    int boostW  = static_cast<int> (contentW * 0.15f);
    int cabW    = static_cast<int> (contentW * 0.35f);
    int delayW  = static_cast<int> (contentW * 0.25f);
    int reverbW = contentW - boostW - cabW - delayW - gap * 3;

    int boostX  = contentX;
    int cabX    = boostX + boostW + gap;
    int delayX  = cabX + cabW + gap;
    int reverbX = delayX + delayW + gap;

    boostGroupBounds_  = { boostX, bottomY, boostW, bottomH };
    cabGroupBounds_    = { cabX, bottomY, cabW, bottomH };
    delayGroupBounds_  = { delayX, bottomY, delayW, bottomH };
    reverbGroupBounds_ = { reverbX, bottomY, reverbW, bottomH };

    // BOOST section: toggle + 3 knobs
    {
        int innerX = boostX + pad;
        int innerY = bottomY + pad;
        boostEnabled_.setBounds (innerX, innerY, toggleW, toggleH);
        int kY = innerY + toggleH + scaler_.scaled (4);
        int kH = bottomH - toggleH - pad * 2 - scaler_.scaled (4);
        int colW = (boostW - pad * 2) / 3;
        placeKnob (boostGain_,  { innerX, kY, colW, kH }, smallKnob, sf);
        placeKnob (boostTone_,  { innerX + colW, kY, colW, kH }, smallKnob, sf);
        placeKnob (boostLevel_, { innerX + 2 * colW, kY, colW, kH }, smallKnob, sf);
    }

    // CABINET section: toggle + 3 knobs | browser
    {
        int innerX = cabX + pad;
        int innerY = bottomY + pad;
        cabEnabled_.setBounds (innerX, innerY, toggleW, toggleH);
        int kY = innerY + toggleH + scaler_.scaled (4);
        int kH = bottomH - toggleH - pad * 2 - scaler_.scaled (4);
        int knobColW = scaler_.scaled (62);
        placeKnob (cabMix_,   { innerX, kY, knobColW, kH }, smallKnob, sf);
        placeKnob (cabHiCut_, { innerX + knobColW, kY, knobColW, kH }, smallKnob, sf);
        placeKnob (cabLoCut_, { innerX + 2 * knobColW, kY, knobColW, kH }, smallKnob, sf);
        int browserX = innerX + 3 * knobColW + scaler_.scaled (4);
        int browserW = cabX + cabW - browserX - pad;
        cabBrowser_.setBounds (browserX, bottomY + pad, browserW, bottomH - pad * 2);
    }

    // DELAY section: toggle + type selector + 3 knobs
    {
        int innerX = delayX + pad;
        int innerY = bottomY + pad;
        delayEnabled_.setBounds (innerX, innerY, toggleW, toggleH);
        int typeW = delayW - pad * 2 - toggleW - scaler_.scaled (4);
        delayTypeBox_.setBounds (innerX + toggleW + scaler_.scaled (4), innerY, typeW, toggleH);
        int kY = innerY + toggleH + scaler_.scaled (4);
        int kH = bottomH - toggleH - pad * 2 - scaler_.scaled (4);
        int colW = (delayW - pad * 2) / 3;
        placeKnob (delayTime_,     { innerX, kY, colW, kH }, smallKnob, sf);
        placeKnob (delayFeedback_, { innerX + colW, kY, colW, kH }, smallKnob, sf);
        placeKnob (delayMix_,      { innerX + 2 * colW, kY, colW, kH }, smallKnob, sf);
    }

    // REVERB section: toggle + 5 knobs
    {
        int innerX = reverbX + pad;
        int innerY = bottomY + pad;
        reverbEnabled_.setBounds (innerX, innerY, toggleW, toggleH);
        int kY = innerY + toggleH + scaler_.scaled (4);
        int kH = bottomH - toggleH - pad * 2 - scaler_.scaled (4);
        int colW = (reverbW - pad * 2) / 5;
        placeKnob (reverbMix_,      { innerX, kY, colW, kH }, smallKnob, sf);
        placeKnob (reverbDecay_,    { innerX + colW, kY, colW, kH }, smallKnob, sf);
        placeKnob (reverbPreDelay_, { innerX + 2 * colW, kY, colW, kH }, smallKnob, sf);
        placeKnob (reverbDamping_,  { innerX + 3 * colW, kY, colW, kH }, smallKnob, sf);
        placeKnob (reverbSize_,     { innerX + 4 * colW, kY, colW, kH }, smallKnob, sf);
    }

    // --- Level meters ---
    int meterTop = mainY;
    int meterBot = getHeight() - margin;
    inputMeter_.setBounds (margin, meterTop, meterW, meterBot - meterTop);
    outputMeter_.setBounds (getWidth() - margin - meterW, meterTop, meterW, meterBot - meterTop);
}

// =============================================================================
// User Preset Management
// =============================================================================

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

void DuskAmpEditor::updateDeleteButtonVisibility()
{
    deletePresetButton_.setVisible (presetBox_.getSelectedId() >= 1001);
}

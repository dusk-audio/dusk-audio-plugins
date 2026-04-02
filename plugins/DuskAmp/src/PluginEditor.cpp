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
    drive_         .init (*this, params, DuskAmpParams::DRIVE,            "DRIVE",      "%",
        "Amp drive amount. Distributes gain across preamp and power amp stages");
    bass_          .init (*this, params, DuskAmpParams::BASS,            "BASS",       "%",
        "Low frequency tone control");
    mid_           .init (*this, params, DuskAmpParams::MID,             "MID",        "%",
        "Mid frequency tone control");
    treble_        .init (*this, params, DuskAmpParams::TREBLE,          "TREBLE",     "%",
        "High frequency tone control");
    presence_      .init (*this, params, DuskAmpParams::PRESENCE,        "PRESENCE",   "%",
        "Upper-mid emphasis in power amp stage");
    resonance_     .init (*this, params, DuskAmpParams::RESONANCE,       "RESONANCE",  "%",
        "Low-frequency emphasis in power amp stage");
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
    namInputLevel_ .init (*this, params, DuskAmpParams::NAM_INPUT_LEVEL, "IN LVL",    " dB",
        "NAM model input level. Adjust to match the expected input of your NAM profile");
    namOutputLevel_.init (*this, params, DuskAmpParams::NAM_OUTPUT_LEVEL,"OUT LVL",   " dB",
        "NAM model output level. Adjust to match loudness with other models");
    outputLevel_   .init (*this, params, DuskAmpParams::OUTPUT_LEVEL,    "OUTPUT",     " dB",
        "Master output level");

    // --- Mode selector (DSP / NAM) ---
    auto* modeParam = params.getParameter (DuskAmpParams::AMP_MODE);
    jassert (modeParam != nullptr);
    modeSelector_ = std::make_unique<AmpModeSelector> (*modeParam);
    addAndMakeVisible (*modeSelector_);

    // --- Amp model selector (Round / Chime / Punch) ---
    ampModelBox_.addItemList ({ "Blackface", "British Combo", "Plexi" }, 1);
    ampModelBox_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (ampModelBox_);
    ampModelAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        params, DuskAmpParams::AMP_MODEL, ampModelBox_);

    // --- Sag indicator ---
    sagIndicator_.setTooltip ("Power supply B+ voltage. Drops when driven hard (tube rectifier sag)");
    addAndMakeVisible (sagIndicator_);
    sagLabel_.setText ("SAG", juce::dontSendNotification);
    sagLabel_.setJustificationType (juce::Justification::centred);
    sagLabel_.setFont (juce::FontOptions (9.0f));
    sagLabel_.setColour (juce::Label::textColourId, juce::Colour (DuskAmpLookAndFeel::kGroupText));
    addAndMakeVisible (sagLabel_);

    // --- Power amp enabled toggle ---
    powerAmpEnabled_.setButtonText ("AMP");
    powerAmpEnabled_.setClickingTogglesState (true);
    addAndMakeVisible (powerAmpEnabled_);
    powerAmpEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::POWER_AMP_ENABLED, powerAmpEnabled_);

    // --- Cabinet enabled toggle ---
    cabEnabled_.setButtonText ("CAB");
    cabEnabled_.setClickingTogglesState (true);
    addAndMakeVisible (cabEnabled_);
    cabEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::CAB_ENABLED, cabEnabled_);

    cabAutoGain_.setButtonText ("AUTO");
    cabAutoGain_.setClickingTogglesState (true);
    cabAutoGain_.setTooltip ("Normalize IR loudness so different cabinet IRs play at similar levels");
    addAndMakeVisible (cabAutoGain_);
    cabAutoGainAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::CAB_AUTOGAIN, cabAutoGain_);

    // --- Cab browser ---
    cabBrowser_.onFileSelected = [this] (const juce::File& file)
    {
        processorRef.loadCabinetIR (file);
        cabBrowser_.setLoadedFile (file);
    };
    addAndMakeVisible (cabBrowser_);
    addAndMakeVisible (irWaveform_);

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
    addAndMakeVisible (delayEnabled_);
    delayEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::DELAY_ENABLED, delayEnabled_);

    // --- Reverb enabled toggle ---
    reverbEnabled_.setButtonText ("REVERB");
    reverbEnabled_.setClickingTogglesState (true);
    addAndMakeVisible (reverbEnabled_);
    reverbEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        params, DuskAmpParams::REVERB_ENABLED, reverbEnabled_);

    // --- Oversampling selector ---
    oversamplingBox_.addItemList ({ "2x", "4x", "8x" }, 1);
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
    update (drive_);
    update (bass_);
    update (mid_);
    update (treble_);
    update (presence_);
    update (resonance_);
    update (cabMix_);
    update (cabHiCut_);
    update (cabLoCut_);
    update (delayTime_);
    update (delayFeedback_);
    update (delayMix_);
    update (reverbMix_);
    update (reverbDecay_);
    update (outputLevel_);

    // Dim cabinet knobs when cab is disabled
    bool cabOff = ! cabEnabled_.getToggleState();
    cabMix_.setDimmed (cabOff);
    cabHiCut_.setDimmed (cabOff);
    cabLoCut_.setDimmed (cabOff);
    cabAutoGain_.setEnabled (! cabOff);
    cabAutoGain_.setAlpha (cabOff ? 0.4f : 1.0f);
    cabBrowser_.setEnabled (! cabOff);
    cabBrowser_.setAlpha (cabOff ? 0.4f : 1.0f);

    // Dim delay knobs when delay is disabled
    bool delayOff = ! delayEnabled_.getToggleState();
    delayTime_.setDimmed (delayOff);
    delayFeedback_.setDimmed (delayOff);
    delayMix_.setDimmed (delayOff);

    // Dim reverb knobs when reverb is disabled
    bool reverbOff = ! reverbEnabled_.getToggleState();
    reverbMix_.setDimmed (reverbOff);
    reverbDecay_.setDimmed (reverbOff);

    // NAM mode: show/hide controls and trigger relayout when mode changes
    bool namMode = processorRef.parameters.getRawParameterValue (DuskAmpParams::AMP_MODE)->load() >= 0.5f;
    int currentModel = static_cast<int> (processorRef.parameters.getRawParameterValue (DuskAmpParams::AMP_MODEL)->load());

    // Update UI accent color to reflect the current amp model
    lnf_.setAmpModelTheme (currentModel, namMode);

    if (namMode != layoutIsNamMode_ || currentModel != cachedAmpModel_)
    {
        cachedAmpModel_ = currentModel;
        if (namMode != layoutIsNamMode_)
            resized();
        repaint();
    }

    // Dim drive/model controls in NAM mode
    drive_.setDimmed (namMode);
    drive_.slider.setEnabled (! namMode);
    ampModelBox_.setEnabled (! namMode);
    ampModelBox_.setAlpha (namMode ? 0.4f : 1.0f);

    // Dim power amp knobs when power amp is disabled
    bool paOff = ! powerAmpEnabled_.getToggleState();
    presence_.setDimmed (paOff);
    resonance_.setDimmed (paOff);
    presence_.slider.setEnabled (! paOff);
    resonance_.slider.setEnabled (! paOff);

    // Update meters
    inputMeter_.setStereoLevels (processorRef.getInputLevelL(),
                                 processorRef.getInputLevelR());
    outputMeter_.setStereoLevels (processorRef.getOutputLevelL(),
                                  processorRef.getOutputLevelR());
    inputMeter_.repaint();
    outputMeter_.repaint();

    // Update sag indicator
    sagIndicator_.setSagLevel (processorRef.getSagLevel());

    // Update IR waveform thumbnail
    auto& cabIR = processorRef.getEngine().getCabinetIR();
    irWaveform_.setThumbnail (cabIR.getThumbnail(), cabIR.hasThumbnail());

    // Update NAM level value labels
    update (namInputLevel_);
    update (namOutputLevel_);
}

// =============================================================================
// Paint
// =============================================================================

static void drawGroupBox (juce::Graphics& g, juce::Rectangle<int> bounds,
                          const juce::String& title, bool centerTitle = false,
                          juce::uint32 accentColour = DuskAmpLookAndFeel::kAccent)
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
    g.setColour (juce::Colour (accentColour).withAlpha (0.3f));
    g.fillRect (underlineX, bounds.getY() + 19, underlineW, 2);
}

void DuskAmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (lnf_.getCurrentBackground()));

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

    // Version (bottom-right of top bar)
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kDimText));
    g.setFont (juce::FontOptions (9.0f * sf));
    g.drawText ("v" + juce::String (JucePlugin_VersionString),
                getWidth() - scaler_.scaled (80), scaler_.scaled (34),
                scaler_.scaled (70), scaler_.scaled (12), juce::Justification::centredRight);

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

    // Group boxes from stored bounds (accent color reflects current amp model)
    auto accent = lnf_.getCurrentAccent();
    drawGroupBox (g, inputGroupBounds_, "INPUT", true, accent);
    drawGroupBox (g, outputGroupBounds_, "OUTPUT", true, accent);

    if (layoutIsNamMode_)
    {
        drawGroupBox (g, centerTopBounds_, "NAM MODEL", false, accent);
        drawGroupBox (g, centerMidBounds_, "TONE", false, accent);
    }
    else
    {
        drawGroupBox (g, centerTopBounds_, "AMP / TONE", false, accent);
    }
    drawGroupBox (g, centerBotBounds_, "POWER AMP", false, accent);
    drawGroupBox (g, cabGroupBounds_, "CABINET", false, accent);
    drawGroupBox (g, fxGroupBounds_, "EFFECTS", false, accent);

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

        // NAM browser + level knobs: browser takes left ~70%, knobs take right ~30%
        {
            int pad = scaler_.scaled (8);
            int knobColW = scaler_.scaled (120);
            int browserW = centerW - knobColW - pad * 3;
            namBrowser_.setBounds (centerX + pad, mainY + topPad,
                                   browserW, namH - topPad - scaler_.scaled (4));

            // NAM input/output level knobs stacked vertically
            int knobX = centerX + pad + browserW + pad;
            int knobAreaH = namH - topPad - scaler_.scaled (4);
            int smallKnobH = knobAreaH / 2;
            int smallKnobSize = scaler_.scaled (44);

            auto layoutSmallKnob = [&] (KnobWithLabel& k, int y)
            {
                int cx = knobX + (knobColW - smallKnobSize) / 2;
                k.slider.setBounds (cx, y, smallKnobSize, smallKnobSize);
                k.nameLabel.setBounds (knobX, y + smallKnobSize, knobColW, scaler_.scaled (12));
                k.valueLabel.setBounds (knobX, y + smallKnobSize + scaler_.scaled (11), knobColW, scaler_.scaled (11));
            };

            layoutSmallKnob (namInputLevel_,  mainY + topPad);
            layoutSmallKnob (namOutputLevel_, mainY + topPad + smallKnobH);
        }

        // Tone: bass, mid, treble (knobs take the top portion)
        int toneKnobH = toneH - controlsH - controlsGap - topPad;
        layoutKnobsInGroup ({ centerX, toneY, centerW, toneKnobH + topPad }, topPad,
            { { &bass_, mediumKnob }, { &mid_, mediumKnob }, { &treble_, mediumKnob } }, sf);

        // Power amp toggle + knobs
        {
            int paToggleW = scaler_.scaled (50);
            int paToggleH = scaler_.scaled (22);
            powerAmpEnabled_.setBounds (centerX + scaler_.scaled (8), powY + topPad, paToggleW, paToggleH);
        }
        layoutKnobsInGroup ({ centerX, powY, centerW, powH }, topPad,
            { { &presence_, mediumKnob }, { &resonance_, mediumKnob } }, sf);

        // Hide DSP drive/model controls, show NAM controls
        drive_.slider.setVisible (false);
        drive_.nameLabel.setVisible (false);
        drive_.valueLabel.setVisible (false);
        ampModelBox_.setVisible (false);
        sagIndicator_.setVisible (false);
        sagLabel_.setVisible (false);
        namBrowser_.setVisible (true);
        namInputLevel_.slider.setVisible (true);
        namInputLevel_.nameLabel.setVisible (true);
        namInputLevel_.valueLabel.setVisible (true);
        namOutputLevel_.slider.setVisible (true);
        namOutputLevel_.nameLabel.setVisible (true);
        namOutputLevel_.valueLabel.setVisible (true);
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

        // AMP/TONE: drive + bass/mid/treble (all medium)
        layoutKnobsInGroup ({ centerX, mainY, centerW, ampToneH - controlsH - controlsGap }, topPad,
            { { &drive_, mediumKnob }, { &bass_, mediumKnob },
              { &mid_, mediumKnob }, { &treble_, mediumKnob } }, sf);

        // Controls row: amp model selector
        {
            int ctrlY = mainY + ampToneH - controlsH - scaler_.scaled (2);
            int ctrlW = scaler_.scaled (160);
            int ctrlX = centerX + (centerW - ctrlW) / 2;
            ampModelBox_.setBounds (ctrlX, ctrlY, ctrlW, controlsH);
        }

        // Power amp toggle + knobs
        {
            int paToggleW = scaler_.scaled (50);
            int paToggleH = scaler_.scaled (22);
            powerAmpEnabled_.setBounds (centerX + scaler_.scaled (8), powY + topPad, paToggleW, paToggleH);
        }
        layoutKnobsInGroup ({ centerX, powY, centerW, powH }, topPad,
            { { &presence_, mediumKnob }, { &resonance_, mediumKnob } }, sf);

        // Sag indicator: thin bar at the left edge of the power amp section
        {
            int sagW = scaler_.scaled (8);
            int sagH = powH - topPad * 2 - scaler_.scaled (14);
            int sagX = centerX + scaler_.scaled (60);
            int sagY = powY + topPad;
            sagIndicator_.setBounds (sagX, sagY, sagW, sagH);
            sagLabel_.setBounds (sagX - scaler_.scaled (4), sagY + sagH, sagW + scaler_.scaled (8), scaler_.scaled (12));
        }

        // Show DSP drive/model controls, hide NAM browser + level knobs
        drive_.slider.setVisible (true);
        drive_.nameLabel.setVisible (true);
        drive_.valueLabel.setVisible (true);
        ampModelBox_.setVisible (true);
        sagIndicator_.setVisible (true);
        sagLabel_.setVisible (true);
        namBrowser_.setVisible (false);
        namInputLevel_.slider.setVisible (false);
        namInputLevel_.nameLabel.setVisible (false);
        namInputLevel_.valueLabel.setVisible (false);
        namOutputLevel_.slider.setVisible (false);
        namOutputLevel_.nameLabel.setVisible (false);
        namOutputLevel_.valueLabel.setVisible (false);
    }

    // --- Bottom row ---
    // Give cabinet section ~60% of the width so IR names are readable
    int bottomY = mainY + mainH + gap;
    int cabW = static_cast<int> (contentW * 0.6f);
    int fxW  = contentW - cabW - gap;
    int fxX  = contentX + cabW + gap;

    cabGroupBounds_ = { contentX, bottomY, cabW, bottomH };
    fxGroupBounds_  = { fxX, bottomY, fxW, bottomH };

    // CABINET section: left column (toggles + knobs, centered) | right column (IR browser)
    {
        int cabX = contentX;
        int pad = scaler_.scaled (8);
        int innerY = bottomY + topPad;
        int toggleW = scaler_.scaled (50);
        int toggleH = scaler_.scaled (22);
        int knobColW = scaler_.scaled (58);
        int knobAreaH = bottomH - scaler_.scaled (8);

        // Split: 40% left for controls, 60% right for browser
        int leftColW = static_cast<int> (cabW * 0.38f);
        int browserX = cabX + leftColW + pad;
        int browserW = cabW - leftColW - pad * 2;

        // Left-align toggles and knobs within the left column
        int controlsX = cabX + pad;

        cabEnabled_.setBounds (controlsX, innerY, toggleW, toggleH);
        cabAutoGain_.setBounds (controlsX, innerY + toggleH + scaler_.scaled (2),
                                toggleW, toggleH);

        int knobStartX = controlsX + toggleW + btnGap;

        placeKnob (cabMix_,   { knobStartX, innerY, knobColW, knobAreaH }, smallKnob, sf);
        placeKnob (cabHiCut_, { knobStartX + knobColW, innerY, knobColW, knobAreaH }, smallKnob, sf);
        placeKnob (cabLoCut_, { knobStartX + 2 * knobColW, innerY, knobColW, knobAreaH }, smallKnob, sf);

        // IR waveform preview at the top of the browser area, browser below
        int waveformH = scaler_.scaled (36);
        int browserTopPad = scaler_.scaled (4);
        irWaveform_.setBounds (browserX, bottomY + browserTopPad, browserW, waveformH);
        cabBrowser_.setBounds (browserX, bottomY + browserTopPad + waveformH + scaler_.scaled (2),
                               browserW, bottomH - browserTopPad - waveformH - scaler_.scaled (10));
    }

    // EFFECTS section: delay | reverb (narrower)
    {
        int innerX = fxX + scaler_.scaled (6);
        int innerY = bottomY + topPad;  // push below group box title
        int toggleW = scaler_.scaled (55);
        int toggleH = scaler_.scaled (22);
        int usableW = fxW - scaler_.scaled (12);
        int halfW = usableW / 2;

        delayEnabled_.setBounds (innerX, innerY, toggleW, toggleH);
        int dKnobY = innerY + toggleH + scaler_.scaled (6);
        int dKnobH = bottomH - toggleH - scaler_.scaled (14);
        int dColW = halfW / 3;

        placeKnob (delayTime_,     { innerX, dKnobY, dColW, dKnobH }, smallKnob, sf);
        placeKnob (delayFeedback_, { innerX + dColW, dKnobY, dColW, dKnobH }, smallKnob, sf);
        placeKnob (delayMix_,      { innerX + 2 * dColW, dKnobY, dColW, dKnobH }, smallKnob, sf);

        int revX = innerX + halfW + scaler_.scaled (4);
        reverbEnabled_.setBounds (revX, innerY, toggleW, toggleH);
        int rColW = (usableW - halfW - scaler_.scaled (4)) / 2;

        placeKnob (reverbMix_,   { revX, dKnobY, rColW, dKnobH }, smallKnob, sf);
        placeKnob (reverbDecay_, { revX + rColW, dKnobY, rColW, dKnobH }, smallKnob, sf);
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

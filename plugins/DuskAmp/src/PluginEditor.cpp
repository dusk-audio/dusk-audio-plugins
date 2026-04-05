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
    // Monospaced font for value readouts — prevents jitter when dragging knobs
    valueLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
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
            // Active pill with accent fill + inner shadow (pressed look)
            auto pill = seg.reduced (2.0f);
            float pillR = cornerRadius - 2.0f;

            g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent));
            g.fillRoundedRectangle (pill, pillR);

            // Inner shadow top edge
            g.setColour (juce::Colour (0x20000000));
            g.fillRoundedRectangle (pill.withHeight (pill.getHeight() * 0.4f), pillR);

            // Subtle glow beneath
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent).withAlpha (0.15f));
            g.fillRoundedRectangle (pill.expanded (2.0f, 1.0f), pillR + 2.0f);
        }
        else if (hovered)
        {
            g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent).withAlpha (0.10f));
            g.fillRoundedRectangle (seg.reduced (2.0f), cornerRadius - 2.0f);
        }

        // Text: white for active, dimmer for inactive
        g.setColour (selected ? juce::Colours::white
                     : hovered ? juce::Colour (0xffa0a0a0)
                               : juce::Colour (DuskAmpLookAndFeel::kDimText));
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
    toneCut_       .init (*this, params, DuskAmpParams::TONE_CUT,        "TONE CUT",   "%",
        "High frequency cut (AC30 master section)");
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
    cabMicPos_     .init (*this, params, DuskAmpParams::CAB_MIC_POS,    "MIC POS",   "%",
        "Mic position: 0% = off-axis (dark), 100% = on-axis (bright)");
    // Mic position knob hidden for now — DSP still active, can re-enable later
    cabMicPos_.slider.setVisible (false);
    cabMicPos_.nameLabel.setVisible (false);
    cabMicPos_.valueLabel.setVisible (false);
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

    // --- Mode selector (DSP / NAM) ---
    auto* modeParam = params.getParameter (DuskAmpParams::AMP_MODE);
    jassert (modeParam != nullptr);
    modeSelector_ = std::make_unique<AmpModeSelector> (*modeParam);
    addAndMakeVisible (*modeSelector_);

    // --- Amp type selector ---
    ampTypeBox_.addItemList ({ "American Clean", "Class A Chime", "British Crunch" }, 1);
    ampTypeBox_.setJustificationType (juce::Justification::centredLeft);
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

    // --- Cabinet normalize toggle ---
    cabNormalize_.setButtonText ("NORM");
    cabNormalize_.setClickingTogglesState (true);
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
    update (cabMicPos_);
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
    cabMicPos_.setDimmed (cabOff);
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

    // NAM mode or amp type change: trigger relayout
    bool namMode = processorRef.parameters.getRawParameterValue (DuskAmpParams::AMP_MODE)->load() >= 0.5f;
    int ampType = static_cast<int> (processorRef.parameters.getRawParameterValue (DuskAmpParams::AMP_TYPE)->load());

    if (namMode != layoutIsNamMode_ || ampType != layoutAmpType_)
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
    auto bf = bounds.toFloat();
    float cr = 6.0f;

    // Panel fill
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kPanel));
    g.fillRoundedRectangle (bf, cr);

    // Inset shadow: dark top edge, light bottom edge (hardware recessed feel)
    g.setColour (juce::Colour (0x18000000));
    g.drawRoundedRectangle (bf.reduced (0.5f).translated (0.0f, 0.5f), cr, 1.0f);
    g.setColour (juce::Colour (0x0cffffff));
    g.drawRoundedRectangle (bf.reduced (0.5f).translated (0.0f, -0.5f), cr, 0.5f);

    // Outer border
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kBorder));
    g.drawRoundedRectangle (bf.reduced (0.5f), cr, 1.0f);

    // Section header — semi-bold, tighter tracking, anchored top-left
    juce::String spaced;
    for (int i = 0; i < title.length(); ++i)
    {
        spaced += title[i];
        if (i < title.length() - 1)
            spaced += ' ';
    }

    g.setColour (juce::Colour (DuskAmpLookAndFeel::kGroupText));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));

    auto titleArea = bounds.withHeight (20);
    if (centerTitle)
        g.drawText (spaced, titleArea, juce::Justification::centred);
    else
        g.drawText (spaced, titleArea.withTrimmedLeft (10), juce::Justification::centredLeft);

    // Accent underline
    int underlineW = juce::jmin (static_cast<int> (title.length()) * 12, bounds.getWidth() - 20);
    int underlineX = centerTitle ? bounds.getX() + (bounds.getWidth() - underlineW) / 2
                                 : bounds.getX() + 10;
    g.setColour (juce::Colour (DuskAmpLookAndFeel::kAccent).withAlpha (0.25f));
    g.fillRect (underlineX, bounds.getY() + 19, underlineW, 1);
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
        drawGroupBox (g, centerMidBounds_, "CABINET");
        if (! centerBotBounds_.isEmpty())
            drawGroupBox (g, centerBotBounds_, "TONE");
    }
    else
    {
        drawGroupBox (g, centerTopBounds_, "AMP / TONE");
        drawGroupBox (g, centerBotBounds_, "POWER AMP");
        drawGroupBox (g, cabGroupBounds_, "CABINET");
    }
    drawGroupBox (g, fxGroupBounds_, "EFFECTS");

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
    if (numKnobs == 0) return;

    juce::FlexBox fb;
    fb.flexDirection = juce::FlexBox::Direction::row;
    fb.alignItems = juce::FlexBox::AlignItems::center;

    // ≤2 knobs: center them together instead of spreading to edges
    fb.justifyContent = (numKnobs <= 2)
        ? juce::FlexBox::JustifyContent::center
        : juce::FlexBox::JustifyContent::spaceAround;

    int maxKnobW = std::min (area.getWidth() / numKnobs,
                             static_cast<int> (120.0f * scaleFactor));
    float margin = (numKnobs <= 2) ? 15.0f * scaleFactor : 0.0f;

    for (auto& [knob, knobSize] : knobs)
    {
        (void) knob;
        fb.items.add (juce::FlexItem()
                          .withWidth (static_cast<float> (maxKnobW))
                          .withHeight (static_cast<float> (area.getHeight()))
                          .withMargin (juce::FlexItem::Margin (0, margin, 0, margin)));
    }

    fb.performLayout (area.toFloat());

    for (int i = 0; i < numKnobs; ++i)
    {
        auto itemBounds = fb.items[static_cast<size_t> (i)].currentBounds.toNearestInt();
        placeKnob (*knobs[static_cast<size_t> (i)].first, itemBounds,
                   knobs[static_cast<size_t> (i)].second, scaleFactor);
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
    int currentAmpType = static_cast<int> (processorRef.parameters.getRawParameterValue (
        DuskAmpParams::AMP_TYPE)->load());
    layoutIsNamMode_ = namMode;
    layoutAmpType_ = currentAmpType;

    // Tone knob visibility depends on mode:
    //   NAM mode: always Bass + Mid + Treble (generic EQ shaping, no Tone Cut)
    //   DSP mode: amp-type-dependent
    //     Fender (0): Bass + Treble only
    //     AC30 (1):   Bass + Tone Cut + Treble
    //     Marshall (2): Bass + Mid + Treble
    bool showMid     = namMode || (currentAmpType == 2);
    bool showToneCut = !namMode && (currentAmpType == 1);

    auto setKnobVisible = [] (KnobWithLabel& k, bool visible) {
        k.slider.setVisible (visible);
        k.nameLabel.setVisible (visible);
        k.valueLabel.setVisible (visible);
    };

    setKnobVisible (mid_, showMid);
    setKnobVisible (toneCut_, showToneCut);

    int controlsH = scaler_.scaled (28);
    int controlsGap = scaler_.scaled (6);

    // Build the tone knob list
    std::vector<std::pair<KnobWithLabel*, int>> toneKnobs;
    toneKnobs.push_back ({ &bass_, mediumKnob });
    if (showMid)     toneKnobs.push_back ({ &mid_, mediumKnob });
    if (showToneCut) toneKnobs.push_back ({ &toneCut_, mediumKnob });
    toneKnobs.push_back ({ &treble_, mediumKnob });

    if (namMode)
    {
        // NAM layout: center = NAM MODEL (top) + CABINET (bottom)
        //             bottom row = TONE (left) + EFFECTS (right)
        int centerH = mainH;
        int namH = static_cast<int> (centerH * 0.55f);
        int cabCenterH = centerH - namH - gap;
        int cabCenterY = mainY + namH + gap;

        centerTopBounds_ = { centerX, mainY, centerW, namH };
        centerMidBounds_ = { centerX, cabCenterY, centerW, cabCenterH };
        centerBotBounds_ = {};  // unused — no power amp in NAM mode

        // NAM browser
        namBrowser_.setBounds (centerX + scaler_.scaled (8), mainY + topPad,
                               centerW - scaler_.scaled (16), namH - topPad - scaler_.scaled (4));

        // Cabinet controls inside center-mid area
        {
            int cabX = centerX;
            int cabBoundsW = centerW;
            int pad = scaler_.scaled (6);
            int toggleW = scaler_.scaled (80);
            int normW = scaler_.scaled (95);
            int toggleH = scaler_.scaled (22);
            int toggleGap = scaler_.scaled (4);

            int knobColW = scaler_.scaled (68);
            int knobAreaTop = cabCenterY + topPad;
            int knobAreaH = cabCenterH - topPad - toggleH - toggleGap - pad;

            placeKnob (cabMix_,    { cabX + pad, knobAreaTop, knobColW, knobAreaH }, smallKnob, sf);
            placeKnob (cabHiCut_,  { cabX + pad + knobColW, knobAreaTop, knobColW, knobAreaH }, smallKnob, sf);
            placeKnob (cabLoCut_,  { cabX + pad + 2 * knobColW, knobAreaTop, knobColW, knobAreaH }, smallKnob, sf);
            // cabMicPos_ hidden — mic model bypassed in ProceduralCab

            // Toggles at bottom-left
            int toggleY = cabCenterY + cabCenterH - toggleH - pad;
            cabEnabled_.setBounds (cabX + pad, toggleY, toggleW, toggleH);
            cabNormalize_.setBounds (cabX + pad + toggleW + toggleGap, toggleY, normW, toggleH);

            // Browser fills right side
            int browserX = cabX + pad + 3 * knobColW + pad;
            int browserW = cabX + cabBoundsW - browserX - pad;
            cabBrowser_.setBounds (browserX, cabCenterY + topPad, browserW, cabCenterH - topPad - pad);
        }

        // Hide DSP-only controls
        setKnobVisible (preampGain_, false);
        setKnobVisible (powerDrive_, false);
        setKnobVisible (presence_, false);
        setKnobVisible (resonance_, false);
        setKnobVisible (sag_, false);
        ampTypeBox_.setVisible (false);
        brightButton_.setVisible (false);
        namBrowser_.setVisible (true);

        // Flag: cabinet is in center (skip bottom-row cab layout)
        cabInCenter_ = true;
    }
    else
    {
        cabInCenter_ = false;

        // 2-row: AMP/TONE (45%) | POWER AMP (55%)
        int centerH = mainH;
        int ampToneH = static_cast<int> (centerH * 0.45f);
        int powH     = centerH - ampToneH - gap;
        int powY     = mainY + ampToneH + gap;

        centerTopBounds_ = { centerX, mainY, centerW, ampToneH };
        centerMidBounds_ = {}; // unused in DSP mode
        centerBotBounds_ = { centerX, powY, centerW, powH };

        // AMP/TONE: gain + amp-type-dependent tone knobs
        std::vector<std::pair<KnobWithLabel*, int>> ampToneKnobs;
        ampToneKnobs.push_back ({ &preampGain_, mediumKnob });
        ampToneKnobs.insert (ampToneKnobs.end(), toneKnobs.begin(), toneKnobs.end());

        layoutKnobsInGroup ({ centerX, mainY, centerW, ampToneH - controlsH - controlsGap }, topPad,
            ampToneKnobs, sf);

        // Controls row: amp type + bright toggle (bright only for Marshall)
        bool showBright = (currentAmpType == 2);  // Marshall only
        {
            int ctrlY = mainY + ampToneH - controlsH - scaler_.scaled (2);
            int ctrlX = centerX + scaler_.scaled (8);
            int totalCtrlW = centerW - scaler_.scaled (16);

            if (showBright)
            {
                int ampTypeW = (totalCtrlW - controlsGap) * 2 / 3;
                int brightW  = totalCtrlW - ampTypeW - controlsGap;
                ampTypeBox_.setBounds (ctrlX, ctrlY, ampTypeW, controlsH);
                brightButton_.setBounds (ctrlX + ampTypeW + controlsGap, ctrlY, brightW, controlsH);
            }
            else
            {
                ampTypeBox_.setBounds (ctrlX, ctrlY, totalCtrlW, controlsH);
            }
        }

        // Power amp
        layoutKnobsInGroup ({ centerX, powY, centerW, powH }, topPad,
            { { &powerDrive_, mediumKnob }, { &presence_, smallKnob },
              { &resonance_, smallKnob }, { &sag_, smallKnob } }, sf);

        // Show DSP controls, hide NAM browser
        setKnobVisible (preampGain_, true);
        setKnobVisible (powerDrive_, true);
        setKnobVisible (presence_, true);
        setKnobVisible (resonance_, true);
        setKnobVisible (sag_, true);
        ampTypeBox_.setVisible (true);
        brightButton_.setVisible (showBright);
        namBrowser_.setVisible (false);
    }

    // --- Bottom row ---
    int bottomY = mainY + mainH + gap;

    if (cabInCenter_)
    {
        // NAM mode: bottom row = TONE (left) + EFFECTS (right)
        // Cabinet is already laid out in the center area above
        int toneW = (contentW - gap) / 2;
        int fxW   = contentW - toneW - gap;
        int fxX   = contentX + toneW + gap;

        cabGroupBounds_ = {};  // drawn in center as centerMidBounds_
        fxGroupBounds_  = { fxX, bottomY, fxW, bottomH };

        // TONE section in bottom-left
        juce::Rectangle<int> toneBotBounds = { contentX, bottomY, toneW, bottomH };
        centerBotBounds_ = toneBotBounds;  // reuse for paint

        int toneKnobH = bottomH - controlsH - scaler_.scaled (10) - topPad;
        layoutKnobsInGroup ({ contentX, bottomY, toneW, toneKnobH + topPad }, topPad,
            toneKnobs, sf);

        // Amp type dropdown not shown in NAM mode (already hidden above)
    }
    else
    {
        // DSP mode: bottom row = CABINET (left) + EFFECTS (right)
        int cabW = (contentW - gap) / 2;
        int fxW  = contentW - cabW - gap;
        int fxX  = contentX + cabW + gap;

        cabGroupBounds_ = { contentX, bottomY, cabW, bottomH };
        fxGroupBounds_  = { fxX, bottomY, fxW, bottomH };

        // CABINET section: knobs left, browser right, toggles at bottom
        {
            int cabX = contentX;
            int pad = scaler_.scaled (6);
            int toggleW = scaler_.scaled (80);
            int toggleH = scaler_.scaled (22);
            int toggleGap = scaler_.scaled (4);

            int knobColW = scaler_.scaled (68);
            int knobAreaTop = bottomY + topPad;
            int knobAreaH = bottomH - topPad - toggleH - toggleGap - pad;

            placeKnob (cabMix_,    { cabX + pad, knobAreaTop, knobColW, knobAreaH }, smallKnob, sf);
            placeKnob (cabHiCut_,  { cabX + pad + knobColW, knobAreaTop, knobColW, knobAreaH }, smallKnob, sf);
            placeKnob (cabLoCut_,  { cabX + pad + 2 * knobColW, knobAreaTop, knobColW, knobAreaH }, smallKnob, sf);
            // cabMicPos_ hidden — mic model bypassed in ProceduralCab

            int toggleY = bottomY + bottomH - toggleH - pad;
            int normW = scaler_.scaled (95);
            cabEnabled_.setBounds (cabX + pad, toggleY, toggleW, toggleH);
            cabNormalize_.setBounds (cabX + pad + toggleW + toggleGap, toggleY, normW, toggleH);

            int browserX = cabX + pad + 3 * knobColW + pad;
            int browserW = cabX + cabW - browserX - pad;
            cabBrowser_.setBounds (browserX, bottomY + topPad, browserW, bottomH - topPad - pad);
        }
    }

    // fxX/fxW for effects section (computed above in both branches)
    int fxX, fxW;
    if (cabInCenter_)
    {
        int toneW = (contentW - gap) / 2;
        fxW = contentW - toneW - gap;
        fxX = contentX + toneW + gap;
    }
    else
    {
        int cabW = (contentW - gap) / 2;
        fxW = contentW - cabW - gap;
        fxX = contentX + cabW + gap;
    }

    // EFFECTS section: two sub-groups, toggles centered under each knob cluster
    {
        int pad = scaler_.scaled (6);
        int toggleW = scaler_.scaled (80);
        int toggleH = scaler_.scaled (22);
        int toggleGap = scaler_.scaled (4);
        int dividerGap = scaler_.scaled (6);

        // Split effects panel into delay (left) and reverb (right) sub-rectangles
        int innerW = fxW - pad * 2;
        int delayW = static_cast<int> (innerW * 0.58f);  // 3 knobs get more space
        int reverbW = innerW - delayW - dividerGap;

        int dX = fxX + pad;
        int revX = dX + delayW + dividerGap;

        int knobAreaTop = bottomY + topPad;
        int knobAreaH = bottomH - topPad - toggleH - toggleGap - pad;

        // Delay knobs (3 columns within delay sub-rect)
        int dColW = delayW / 3;
        placeKnob (delayTime_,     { dX, knobAreaTop, dColW, knobAreaH }, smallKnob, sf);
        placeKnob (delayFeedback_, { dX + dColW, knobAreaTop, dColW, knobAreaH }, smallKnob, sf);
        placeKnob (delayMix_,      { dX + 2 * dColW, knobAreaTop, dColW, knobAreaH }, smallKnob, sf);

        // Reverb knobs (2 columns within reverb sub-rect)
        int rColW = reverbW / 2;
        placeKnob (reverbMix_,   { revX, knobAreaTop, rColW, knobAreaH }, smallKnob, sf);
        placeKnob (reverbDecay_, { revX + rColW, knobAreaTop, rColW, knobAreaH }, smallKnob, sf);

        // Toggles centered horizontally under their knob clusters
        int toggleY = bottomY + bottomH - toggleH - pad;
        int delayCenterX = dX + (delayW - toggleW) / 2;
        int reverbCenterX = revX + (reverbW - toggleW) / 2;
        delayEnabled_.setBounds (delayCenterX, toggleY, toggleW, toggleH);
        reverbEnabled_.setBounds (reverbCenterX, toggleY, toggleW, toggleH);
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

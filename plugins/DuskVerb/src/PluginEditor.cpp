#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "VVERTapData.h"
#include "dsp/AlgorithmConfig.h"

namespace {
    // Returns the modeBox_ id (1=FDN, 2=Dattorro, 3=QuadTank) that matches the
    // engine type baked into the AlgorithmConfig for the given algorithm index.
    int engineModeIdForAlgorithm (int algorithmIndex)
    {
        const auto& cfg = getAlgorithmConfig (algorithmIndex);
        if (cfg.useDattorroTank) return 2;
        if (cfg.useQuadTank)     return 3;
        return 1;  // FDN default
    }
}

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
                         juce::Colour (DuskVerbLookAndFeel::kLabelText));
    parent.addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setInterceptsMouseClicks (false, false);
    valueLabel.setFont (juce::FontOptions (11.0f));
    valueLabel.setColour (juce::Label::textColourId,
                          juce::Colour (DuskVerbLookAndFeel::kValueText));
    parent.addAndMakeVisible (valueLabel);

    // Store suffix in name field for formatting
    valueLabel.setName (suffix);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);

    // Set textFromValueFunction AFTER attachment (attachment overwrites it)
    auto sfx = suffix;
    slider.textFromValueFunction = [sfx] (double v)
    {
        if (sfx == " s")
            return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms"
                           : juce::String (v, 2) + " s";
        if (sfx == " ms")   return juce::String (juce::roundToInt (v)) + " ms";
        if (sfx == " Hz")
            return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz"
                               : v < 100.0 ? juce::String (v, 2) + " Hz"
                                           : juce::String (juce::roundToInt (v)) + " Hz";
        if (sfx == "x")     return juce::String (v, 2) + "x";
        if (sfx == "%")     return juce::String (v * 100.0, 1) + "%";
        return juce::String (v, 2);
    };
}

void KnobWithLabel::initWithSentinel (juce::Component& parent,
                                      juce::AudioProcessorValueTreeState& apvts,
                                      const juce::String& paramID,
                                      const juce::String& displayName,
                                      const juce::String& suffix,
                                      double sentinelValue,
                                      const juce::String& tooltip)
{
    init (parent, apvts, paramID, displayName, suffix, tooltip);

    // Override the formatter so sentinel values display "default".
    auto sfx = suffix;
    slider.textFromValueFunction = [sfx, sentinelValue] (double v)
    {
        if (std::abs (v - sentinelValue) < 0.001)
            return juce::String ("default");
        if (sfx == " Hz")
            return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz"
                               : v < 100.0 ? juce::String (v, 2) + " Hz"
                                           : juce::String (juce::roundToInt (v)) + " Hz";
        if (sfx == " ms")  return juce::String (juce::roundToInt (v)) + " ms";
        if (sfx == " dB")  return juce::String (v, 1) + " dB";
        if (sfx == "x")    return juce::String (v, 2) + "x";
        return juce::String (v, 2);
    };
    // Force the value label to refresh immediately so the text reflects
    // the sentinel-aware formatter at construction time.
    slider.updateText();
}

// =============================================================================
// DuskVerbLookAndFeel
// =============================================================================

DuskVerbLookAndFeel::DuskVerbLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (kBackground));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0161630));
    setColour (juce::TooltipWindow::textColourId, juce::Colour (kText));
    setColour (juce::TooltipWindow::outlineColourId, juce::Colour (kBorder));

    // ComboBox: subtle border, no accent outline
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (kPanel));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (kBorder));
    setColour (juce::ComboBox::textColourId, juce::Colour (kText));
    setColour (juce::ComboBox::arrowColourId, juce::Colour (kGroupText));

    // TextButton: quiet styling for Save/Delete
    setColour (juce::TextButton::buttonColourId, juce::Colour (kPanel));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (kPanel));
    setColour (juce::TextButton::textColourOffId, juce::Colour (kGroupText));
    setColour (juce::TextButton::textColourOnId, juce::Colour (kValueText));
}

void DuskVerbLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                            int width, int height,
                                            float sliderPos,
                                            float rotaryStartAngle,
                                            float rotaryEndAngle,
                                            juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    float diameter = std::min (bounds.getWidth(), bounds.getHeight());
    auto centre = bounds.getCentre();
    float radius = diameter * 0.5f;

    bool isHovered  = slider.isMouseOverOrDragging();
    bool isDragging = slider.isMouseButtonDown();

    // Active glow ring when dragging
    if (isDragging)
    {
        float glowRadius = radius;
        g.setColour (juce::Colour (kAccent).withAlpha (0.12f));
        g.fillEllipse (centre.x - glowRadius, centre.y - glowRadius,
                       glowRadius * 2.0f, glowRadius * 2.0f);
    }

    // Outer dark ring
    float outerRadius = radius - 2.0f;
    g.setColour (juce::Colour (0xff0d0d1a));
    g.fillEllipse (centre.x - outerRadius, centre.y - outerRadius,
                   outerRadius * 2.0f, outerRadius * 2.0f);

    // Knob body (brightens on hover)
    float knobRadius = outerRadius - 3.0f;
    g.setColour (isHovered ? juce::Colour (kKnobFill).brighter (0.15f)
                           : juce::Colour (kKnobFill));
    g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius,
                   knobRadius * 2.0f, knobRadius * 2.0f);

    // Arc track (background)
    float arcRadius = outerRadius - 1.5f;
    float lineW = 3.0f;
    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff2a2a3e));
    g.strokePath (trackArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    // Filled arc with gradient (darker at start → brighter at current position)
    float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    if (angle > rotaryStartAngle + 0.01f)
    {
        juce::Path filledArc;
        filledArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                                 0.0f, rotaryStartAngle, angle, true);

        auto accentCol = juce::Colour (kAccent);
        juce::ColourGradient arcGradient (
            accentCol.darker (0.3f),
            centre.x + arcRadius * std::sin (rotaryStartAngle),
            centre.y - arcRadius * std::cos (rotaryStartAngle),
            isDragging ? accentCol.brighter (0.2f) : accentCol,
            centre.x + arcRadius * std::sin (angle),
            centre.y - arcRadius * std::cos (angle),
            false);
        g.setGradientFill (arcGradient);
        g.strokePath (filledArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
    }

    // Dot indicator at current position (brighter when dragging)
    float dotRadius = 3.0f;
    float dotDist = knobRadius - 6.0f;
    float dotX = centre.x + dotDist * std::sin (angle);
    float dotY = centre.y - dotDist * std::cos (angle);
    g.setColour (isDragging ? juce::Colours::white : juce::Colour (kText));
    g.fillEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);

    // Draw value text inside large knobs
    if (diameter >= 70.0f)
    {
        auto text = slider.getTextFromValue (slider.getValue());
        g.setColour (juce::Colour (kValueText));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text, bounds.toNearestInt(), juce::Justification::centred);
    }
}

void DuskVerbLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (label.getFont());
    g.drawFittedText (label.getText(), label.getLocalBounds(),
                      label.getJustificationType(), 1);
}

void DuskVerbLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                            bool /*shouldDrawButtonAsHighlighted*/,
                                            bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (4.0f);
    bool on = button.getToggleState();
    auto accent = juce::Colour (kAccent);
    float cornerSize = bounds.getHeight() * 0.5f;

    if (on)
    {
        // Active glow (2px larger pill behind)
        g.setColour (accent.withAlpha (0.4f));
        g.fillRoundedRectangle (bounds.expanded (2.0f), cornerSize + 2.0f);

        // Filled pill
        g.setColour (accent);
        g.fillRoundedRectangle (bounds, cornerSize);

        // Text
        g.setColour (juce::Colours::white);
    }
    else
    {
        // Inactive background
        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (bounds, cornerSize);

        // Border
        g.setColour (juce::Colour (kBorder));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerSize, 1.0f);

        // Text
        g.setColour (juce::Colour (kGroupText));
    }

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred);
}

// =============================================================================
// Value formatting
// =============================================================================

static juce::String formatValue (const juce::Slider& s, const juce::String& suffix)
{
    double v = s.getValue();

    if (suffix == " s")
    {
        if (v < 1.0)
            return juce::String (juce::roundToInt (v * 1000.0)) + " ms";
        return juce::String (v, 2) + " s";
    }
    if (suffix == " ms")
        return juce::String (juce::roundToInt (v)) + " ms";
    if (suffix == " Hz")
    {
        if (v >= 1000.0)
            return juce::String (v / 1000.0, 2) + " kHz";
        if (v < 100.0)
            return juce::String (v, 2) + " Hz";
        return juce::String (juce::roundToInt (v)) + " Hz";
    }
    if (suffix == "x")
        return juce::String (v, 2) + "x";
    if (suffix == "%")
        return juce::String (v * 100.0, 1) + "%";

    return juce::String (v, 2);
}

// =============================================================================
// DuskVerbEditor
// =============================================================================

static constexpr int kBaseWidth  = 780;
static constexpr int kBaseHeight = 580;

DuskVerbEditor::DuskVerbEditor (DuskVerbProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    setLookAndFeel (&lnf_);

    preDelay_  .init (*this, p.parameters, "predelay",   "PRE-DELAY",    " ms",
        "Delay before reverb starts. Creates space between dry signal and reverb tail");
    diffusion_ .init (*this, p.parameters, "diffusion",  "DIFFUSION",    "%",
        "Smears the reverb onset. Low = grainy echoes, High = smooth wash");
    decay_     .init (*this, p.parameters, "decay",      "DECAY",        " s",
        "Reverb tail length (RT60)");
    size_      .init (*this, p.parameters, "size",       "SIZE",         "%",
        "Virtual room size. Affects echo density and spacing");
    bassMult_  .init (*this, p.parameters, "bass_mult",  "BASS MULT",    "x",
        "Low-frequency decay multiplier. >1x = bass rings longer than mids");
    trebleMult_.init (*this, p.parameters, "damping",    "TREBLE MULT",  "x",
        "High-frequency decay multiplier. <1x = natural air absorption");
    crossover_ .init (*this, p.parameters, "crossover",  "CROSSOVER",    " Hz",
        "Frequency where bass and treble decay multipliers split");
    modDepth_  .init (*this, p.parameters, "mod_depth",  "DEPTH",        "%",
        "Chorus-like modulation depth. Reduces metallic ringing");
    modRate_   .init (*this, p.parameters, "mod_rate",   "RATE",         " Hz",
        "Speed of internal pitch modulation");
    erLevel_   .init (*this, p.parameters, "er_level",   "LEVEL",        "%",
        "Early reflections level. First echoes that define room shape");
    erSize_    .init (*this, p.parameters, "er_size",    "SIZE",         "%",
        "Early reflection spacing. Larger = bigger perceived room");
    mix_       .init (*this, p.parameters, "mix",        "DRY/WET",      "%",
        "Balance between dry input and reverb. Use BUS mode for send/return");
    loCut_     .init (*this, p.parameters, "lo_cut",     "LO CUT",       " Hz",
        "High-pass filter on reverb output. Removes low-end rumble");
    hiCut_     .init (*this, p.parameters, "hi_cut",     "HI CUT",       " Hz",
        "Low-pass filter on reverb output. Darkens the reverb");
    width_     .init (*this, p.parameters, "width",      "WIDTH",        "%",
        "Stereo width: 0% mono, 100% normal, 150% wide");

    // --- Advanced panel knobs (sentinel-default: -1 means use preset value) ---
    advInputOnset_   .init (*this, p.parameters, "input_onset",        "INPUT ONSET", " ms",
        "Adds an attack ramp on the input. Softens transients feeding the reverb");
    advSoftOnset_    .init (*this, p.parameters, "soft_onset",         "SOFT ONSET",  " ms",
        "Smooths the DattorroTank output transient. Longer = less aggressive onset");
    advDelayScale_   .init (*this, p.parameters, "delay_scale",        "DELAY SCALE", "x",
        "DattorroTank loop-length multiplier. Shifts mode spacing / perceived size");
    advNoiseMod_     .initWithSentinel (*this, p.parameters, "noise_mod",          "NOISE MOD", "",    -1.0,
        "Per-sample noise modulation depth in samples. Higher = more mode smearing");
    advAirDamping_   .initWithSentinel (*this, p.parameters, "air_damping",        "AIR DAMP",  "",    -1.0,
        "Feedback HF damping strength above the high-crossover. Lower = darker tail");
    advHighCrossover_.initWithSentinel (*this, p.parameters, "high_crossover",     "HI XOVER",  " Hz", -1.0,
        "Split frequency between mid-band and air-band damping");
    advStructHFDamp_ .initWithSentinel (*this, p.parameters, "structural_hf_damp", "STRUCT HF", " Hz", -1.0,
        "Structural HF damping cutoff inside the feedback loops");
    advInlineDiff_   .initWithSentinel (*this, p.parameters, "inline_diffusion",   "TANK DIFF", "",    -1.0,
        "Diffusion amount inside the tank (per-tank allpass coefficient)");

    advOutLowShelfDb_ .init (*this, p.parameters, "output_low_shelf_db",  "LOW dB",   " dB",
        "Output low-shelf gain. Positive boosts low end; negative cuts");
    advOutHighShelfDb_.init (*this, p.parameters, "output_high_shelf_db", "HI dB",    " dB",
        "Output high-shelf gain. Positive boosts air; negative cuts");
    advOutHighShelfHz_.init (*this, p.parameters, "output_high_shelf_hz", "HI Hz",    " Hz",
        "Output high-shelf corner frequency (0 = bypass)");
    advOutMidEqDb_    .init (*this, p.parameters, "output_mid_eq_db",     "MID dB",   " dB",
        "Output mid-band peaking gain. Narrow scoop/boost in the mids");
    advOutMidEqHz_    .init (*this, p.parameters, "output_mid_eq_hz",     "MID Hz",   " Hz",
        "Output mid-band peaking frequency (0 = bypass)");

    advStereoCoupling_.initWithSentinel (*this, p.parameters, "stereo_coupling", "STEREO",   "",    -2.0,
        "Crossfeed between L/R feedback paths. Lower = wider; higher = more mono");
    advERCrossfeed_   .initWithSentinel (*this, p.parameters, "er_crossfeed",    "ER XFEED", "",    -1.0,
        "Early reflection L/R crossfeed amount");
    advChorusDepth_   .initWithSentinel (*this, p.parameters, "chorus_depth",    "CHO DEPTH", "",   -1.0,
        "Tail chorus modulation depth (adds movement to the tail)");
    advChorusRate_    .initWithSentinel (*this, p.parameters, "chorus_rate",     "CHO RATE", " Hz", -1.0,
        "Tail chorus modulation rate");

    advLimiterThresh_ .init (*this, p.parameters, "limiter_thresh", "LIMITER",  " dB",
        "Output peak limiter threshold (0 dB = disabled)");
    advDecayBoost_    .initWithSentinel (*this, p.parameters, "decay_boost", "D.BOOST", "", -1.0,
        "Short-decay boost amount. Extends perceived tail without extending RT60");
    advTerminalFactor_.init (*this, p.parameters, "terminal_factor", "TERM FAC", "",
        "Terminal-decay accelerator. >0 hard-truncates tail below the terminal threshold");

    // Hide advanced knobs initially — main tab is active by default.
    for (auto* k : { &advInputOnset_, &advSoftOnset_, &advDelayScale_, &advNoiseMod_,
                     &advAirDamping_, &advHighCrossover_, &advStructHFDamp_, &advInlineDiff_,
                     &advOutLowShelfDb_, &advOutHighShelfDb_, &advOutHighShelfHz_,
                     &advOutMidEqDb_, &advOutMidEqHz_,
                     &advStereoCoupling_, &advERCrossfeed_, &advChorusDepth_, &advChorusRate_,
                     &advLimiterThresh_, &advDecayBoost_, &advTerminalFactor_ })
    {
        k->slider    .setVisible (false);
        k->nameLabel .setVisible (false);
        k->valueLabel.setVisible (false);
    }

    // User preset manager
    userPresetManager_ = std::make_unique<UserPresetManager> ("DuskVerb");

    // Preset browser (factory + user presets)
    presetBox_.setJustificationType (juce::Justification::centred);
    presetBox_.setTextWhenNothingSelected ("Preset");
    presetBox_.onChange = [this]
    {
        int id = presetBox_.getSelectedId();
        if (id >= 1001)
        {
            // User preset
            int userIdx = id - 1001;
            auto userPresets = userPresetManager_->loadUserPresets();
            if (userIdx >= 0 && userIdx < static_cast<int> (userPresets.size()))
                loadUserPreset (userPresets[static_cast<size_t> (userIdx)].name);
        }
        else if (id >= 2)
        {
            loadPreset (id - 2);
        }
        // loadPreset() now syncs modeBox_ to the preset's engine type, so we
        // do not clear it here (previous behaviour was to wipe it to "Mode").
        updateDeleteButtonVisibility();
    };
    addAndMakeVisible (presetBox_);
    refreshPresetList();

    // Mode dropdown — engine-type shortcut (swaps engine, keeps user's knobs)
    modeBox_.setJustificationType (juce::Justification::centred);
    modeBox_.setTextWhenNothingSelected ("Mode");
    modeBox_.addItem ("FDN",      1);
    modeBox_.addItem ("Dattorro", 2);
    modeBox_.addItem ("QuadTank", 3);
    modeBox_.setTooltip ("Swap reverb engine. Knob positions are preserved — pick a preset or "
                         "tweak knobs to taste. FDN=Vocal Hall, Dattorro=Drum Room, QuadTank=Bright Chamber.");
    modeBox_.onChange = [this] { selectEngineMode (modeBox_.getSelectedId()); };
    addAndMakeVisible (modeBox_);

    // Restore preset selection from saved state
    {
        auto savedName = processorRef.parameters.state.getProperty ("presetName", "").toString();
        if (savedName.isNotEmpty())
        {
            const auto& presets = getFactoryPresets();
            for (size_t i = 0; i < presets.size(); ++i)
            {
                if (juce::String (presets[i].name) == savedName)
                {
                    presetBox_.setSelectedId (static_cast<int> (i) + 2, juce::dontSendNotification);
                    break;
                }
            }
            if (presetBox_.getSelectedId() == 0 && userPresetManager_)
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
        }
        updateDeleteButtonVisibility();

        // Sync the Mode dropdown from whatever algorithm the host restored.
        if (auto* algoParam = processorRef.parameters.getRawParameterValue ("algorithm"))
        {
            int algoIdx = static_cast<int> (algoParam->load());
            modeBox_.setSelectedId (engineModeIdForAlgorithm (algoIdx),
                                    juce::dontSendNotification);
        }
    }

    // Prev/next preset nav buttons
    prevPresetButton_.setButtonText ("<");
    prevPresetButton_.setTooltip ("Previous preset");
    prevPresetButton_.onClick = [this] { stepFactoryPreset (-1); };
    addAndMakeVisible (prevPresetButton_);

    nextPresetButton_.setButtonText (">");
    nextPresetButton_.setTooltip ("Next preset");
    nextPresetButton_.onClick = [this] { stepFactoryPreset (+1); };
    addAndMakeVisible (nextPresetButton_);

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
                juce::Component::SafePointer<DuskVerbEditor> safeThis (this);

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

    // MAIN / ADVANCED tab toggle (top-right of header). ADVANCED exposes the
    // per-preset override knobs (air damping, output EQ, dynamics, etc.) that
    // already exist in the processor as sentinel-default APVTS params.
    mainTabButton_.setButtonText ("MAIN");
    mainTabButton_.setClickingTogglesState (true);
    mainTabButton_.setRadioGroupId (1001);
    mainTabButton_.setToggleState (true, juce::dontSendNotification);
    mainTabButton_.setTooltip ("Standard reverb controls");
    mainTabButton_.onClick = [this] { setActiveTab (false); };
    addAndMakeVisible (mainTabButton_);

    advancedTabButton_.setButtonText ("ADV");
    advancedTabButton_.setClickingTogglesState (true);
    advancedTabButton_.setRadioGroupId (1001);
    advancedTabButton_.setToggleState (false, juce::dontSendNotification);
    advancedTabButton_.setTooltip ("Per-preset override controls (damping, EQ, dynamics, spatial)");
    advancedTabButton_.onClick = [this] { setActiveTab (true); };
    addAndMakeVisible (advancedTabButton_);

    // Pre-delay sync
    predelaySyncBox_.addItemList ({ "Free", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }, 1);
    predelaySyncBox_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (predelaySyncBox_);
    predelaySyncAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.parameters, "predelay_sync", predelaySyncBox_);

    // Freeze button (inside TIME group)
    freezeButton_.setButtonText ("FREEZE");
    freezeButton_.setName ("freeze");
    freezeButton_.setClickingTogglesState (true);
    addAndMakeVisible (freezeButton_);
    freezeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.parameters, "freeze", freezeButton_);

    // Bus mode button (inside OUTPUT group)
    busModeButton_.setButtonText ("BUS");
    busModeButton_.setName ("bus_mode");
    busModeButton_.setClickingTogglesState (true);
    addAndMakeVisible (busModeButton_);
    busModeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.parameters, "bus_mode", busModeButton_);

    // Level meters
    inputMeter_.setStereoMode (true);
    inputMeter_.setRefreshRate (15.0f);
    addAndMakeVisible (inputMeter_);

    outputMeter_.setStereoMode (true);
    outputMeter_.setRefreshRate (15.0f);
    addAndMakeVisible (outputMeter_);

    // Scalable editor: 780x580 base, 70%-200%, fixed aspect ratio
    scaler_.initialize (this, &p, kBaseWidth, kBaseHeight,
                        static_cast<int> (kBaseWidth * 0.7f),
                        static_cast<int> (kBaseHeight * 0.7f),
                        kBaseWidth * 2, kBaseHeight * 2,
                        true);

    setSize (scaler_.getStoredWidth(), scaler_.getStoredHeight());
    startTimerHz (15);
}

DuskVerbEditor::~DuskVerbEditor()
{
    scaler_.saveSize();
    setLookAndFeel (nullptr);
}

void DuskVerbEditor::timerCallback()
{
    auto update = [] (KnobWithLabel& k)
    {
        k.valueLabel.setText (formatValue (k.slider, k.valueLabel.getName()),
                              juce::dontSendNotification);
    };

    update (preDelay_);
    update (diffusion_);
    update (decay_);
    update (size_);
    update (bassMult_);
    update (trebleMult_);
    update (crossover_);
    update (modDepth_);
    update (modRate_);
    update (erLevel_);
    update (erSize_);
    update (mix_);
    update (loCut_);
    update (hiCut_);
    update (width_);

    // Advanced panel knobs — use the slider's own textFromValueFunction
    // (set in init/initWithSentinel) so sentinel values display as "default".
    auto updateAdv = [] (KnobWithLabel& k)
    {
        k.valueLabel.setText (k.slider.getTextFromValue (k.slider.getValue()),
                              juce::dontSendNotification);
    };
    updateAdv (advInputOnset_);
    updateAdv (advSoftOnset_);
    updateAdv (advDelayScale_);
    updateAdv (advNoiseMod_);
    updateAdv (advAirDamping_);
    updateAdv (advHighCrossover_);
    updateAdv (advStructHFDamp_);
    updateAdv (advInlineDiff_);
    updateAdv (advOutLowShelfDb_);
    updateAdv (advOutHighShelfDb_);
    updateAdv (advOutHighShelfHz_);
    updateAdv (advOutMidEqDb_);
    updateAdv (advOutMidEqHz_);
    updateAdv (advStereoCoupling_);
    updateAdv (advERCrossfeed_);
    updateAdv (advChorusDepth_);
    updateAdv (advChorusRate_);
    updateAdv (advLimiterThresh_);
    updateAdv (advDecayBoost_);
    updateAdv (advTerminalFactor_);

    // Gray out mix knob when bus mode is active
    bool busMode = busModeButton_.getToggleState();
    mix_.slider.setEnabled (! busMode);
    mix_.slider.setAlpha (busMode ? 0.3f : 1.0f);
    mix_.nameLabel.setAlpha (busMode ? 0.3f : 1.0f);
    if (busMode)
        mix_.valueLabel.setText ("100% (Bus)", juce::dontSendNotification);

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
                          const juce::String& title, int titleBandH)
{
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kPanel));
    g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

    g.setColour (juce::Colour (DuskVerbLookAndFeel::kBorder));
    g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);

    // Group title with letter spacing, left-aligned. Title band must match the
    // `topPad` value used by `layoutKnobsInGroup` — otherwise the knob name
    // labels end up overlapping the title at smaller scale factors (since
    // topPad scales with the editor but a hardcoded band height wouldn't).
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kGroupText));
    g.setFont (juce::FontOptions (10.0f));

    juce::String spaced;
    for (int i = 0; i < title.length(); ++i)
    {
        spaced += title[i];
        if (i < title.length() - 1)
            spaced += ' ';
    }

    auto titleArea = bounds.withHeight (titleBandH).withTrimmedLeft (10);
    g.drawText (spaced, titleArea, juce::Justification::centredLeft);
}

void DuskVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (DuskVerbLookAndFeel::kBackground));

    auto sf = scaler_.getScaleFactor();
    int margin   = scaler_.scaled (10);
    int meterW   = scaler_.scaled (22);
    int meterGap = scaler_.scaled (6);
    int contentX = margin + meterW + meterGap;
    int contentW = getWidth() - contentX - margin - meterW - meterGap;

    // Title
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kText));
    g.setFont (juce::FontOptions (22.0f * sf, juce::Font::bold));
    g.drawText ("DUSKVERB", 0, scaler_.scaled (8), getWidth(), scaler_.scaled (24),
                juce::Justification::centred);

    // Subtitle
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kSubtleText));
    g.setFont (juce::FontOptions (11.0f * sf));
    g.drawText ("Algorithmic Reverb", 0, scaler_.scaled (30), getWidth(), scaler_.scaled (16),
                juce::Justification::centred);

    // Divider line
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kBorder));
    int dividerY = scaler_.scaled (46);
    g.drawHorizontalLine (dividerY,
                          static_cast<float> (contentX),
                          static_cast<float> (contentX + contentW));

    // --- Group box positions (must match resized()) ---
    // topY and titleBandH MUST stay in sync with the values used in resized()
    // — otherwise the group title band and the knob name labels fall out of
    // alignment and overlap visually.
    int topY       = scaler_.scaled (84);
    int gap        = scaler_.scaled (8);
    int titleBandH = scaler_.scaled (20);  // keep == topPad in resized()

    // Proportional split matching resized() (base: 200 top + 250 bottom = 450)
    int availH  = getHeight() - topY - gap - margin;
    int topRowH = juce::roundToInt (availH * (200.0f / 450.0f));
    int bottomH = availH - topRowH;
    int bottomY = topY + topRowH + gap;

    int topUsable  = contentW - gap * 2;
    int inputW     = static_cast<int> (topUsable * 0.28f);
    int timeW      = static_cast<int> (topUsable * 0.36f);
    int characterW = topUsable - inputW - timeW;

    int inputX     = contentX;
    int timeX      = inputX + inputW + gap;
    int characterX = timeX + timeW + gap;

    if (! showingAdvanced_)
    {
        drawGroupBox (g, { inputX, topY, inputW, topRowH }, "INPUT", titleBandH);
        drawGroupBox (g, { timeX, topY, timeW, topRowH }, "TIME", titleBandH);
        drawGroupBox (g, { characterX, topY, characterW, topRowH }, "CHARACTER", titleBandH);
    }
    else
    {
        int advGroupW = (contentW - gap) / 2;
        int onsetX    = contentX;
        int dampingX  = onsetX + advGroupW + gap;
        drawGroupBox (g, { onsetX,   topY, advGroupW, topRowH }, "ONSET",   titleBandH);
        drawGroupBox (g, { dampingX, topY, advGroupW, topRowH }, "DAMPING", titleBandH);
    }

    int bottomUsable = contentW - gap * 3;
    int modW    = static_cast<int> (bottomUsable * 0.22f);
    int erW     = static_cast<int> (bottomUsable * 0.22f);
    int eqW     = static_cast<int> (bottomUsable * 0.20f);
    int outputW = bottomUsable - modW - erW - eqW;

    int modX    = contentX;
    int erX     = modX + modW + gap;
    int eqX     = erX + erW + gap;
    int outputX = eqX + eqW + gap;

    if (! showingAdvanced_)
    {
        drawGroupBox (g, { modX, bottomY, modW, bottomH }, "MODULATION", titleBandH);
        drawGroupBox (g, { erX, bottomY, erW, bottomH }, "EARLY REFLECTIONS", titleBandH);
        drawGroupBox (g, { eqX, bottomY, eqW, bottomH }, "OUTPUT EQ", titleBandH);
        drawGroupBox (g, { outputX, bottomY, outputW, bottomH }, "OUTPUT", titleBandH);
    }
    else
    {
        int advBottomUsable = contentW - gap * 2;
        int advEqW          = static_cast<int> (advBottomUsable * 0.45f);
        int advSpatialW     = static_cast<int> (advBottomUsable * 0.30f);
        int advDynW         = advBottomUsable - advEqW - advSpatialW;
        int advEqX          = contentX;
        int advSpatialX     = advEqX + advEqW + gap;
        int advDynX         = advSpatialX + advSpatialW + gap;
        drawGroupBox (g, { advEqX,      bottomY, advEqW,      bottomH }, "OUTPUT EQ", titleBandH);
        drawGroupBox (g, { advSpatialX, bottomY, advSpatialW, bottomH }, "SPATIAL",   titleBandH);
        drawGroupBox (g, { advDynX,     bottomY, advDynW,     bottomH }, "DYNAMICS",  titleBandH);
    }

    // Meter labels
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kGroupText));
    g.setFont (juce::FontOptions (8.0f * sf));
    g.drawText ("IN", margin, topY - scaler_.scaled (12), meterW, scaler_.scaled (12),
                juce::Justification::centred);
    g.drawText ("OUT", getWidth() - margin - meterW, topY - scaler_.scaled (12), meterW,
                scaler_.scaled (12), juce::Justification::centred);
}

// =============================================================================
// Layout
// =============================================================================

static void placeKnob (KnobWithLabel& k, juce::Rectangle<int> area, int knobSize,
                       float scaleFactor)
{
    int nameH  = juce::roundToInt (14.0f * scaleFactor);
    bool largeKnob = (knobSize >= juce::roundToInt (70.0f * scaleFactor));
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

void DuskVerbEditor::resized()
{
    scaler_.updateResizer();
    auto sf = scaler_.getScaleFactor();

    int margin   = scaler_.scaled (10);
    int meterW   = scaler_.scaled (22);
    int meterGap = scaler_.scaled (6);
    int contentX = margin + meterW + meterGap;
    int contentW = getWidth() - contentX - margin - meterW - meterGap;

    // Title click area for supporters overlay
    int titleW = scaler_.scaled (200);
    titleClickArea_ = { (getWidth() - titleW) / 2, scaler_.scaled (6),
                         titleW, scaler_.scaled (38) };

    // --- Header: [main][adv] | mode | preset | < > | save | del ---
    int presetW = scaler_.scaled (200);
    int presetH = scaler_.scaled (24);
    int presetY = scaler_.scaled (52);
    int modeW   = scaler_.scaled (90);
    int tabW    = scaler_.scaled (50);
    int arrowW  = scaler_.scaled (24);
    int saveW   = scaler_.scaled (50);
    int delW    = scaler_.scaled (36);
    int btnGap  = scaler_.scaled (4);
    int tabGap  = scaler_.scaled (1);   // tabs sit tight together like the arrows
    int arrowGap = scaler_.scaled (1);  // arrows sit tight together as a pair
    int totalPresetW = tabW + tabGap + tabW + btnGap
                       + modeW + btnGap + presetW + btnGap
                       + arrowW + arrowGap + arrowW + btnGap
                       + saveW + btnGap + delW;
    int x = (getWidth() - totalPresetW) / 2;
    mainTabButton_    .setBounds (x, presetY, tabW, presetH);    x += tabW + tabGap;
    advancedTabButton_.setBounds (x, presetY, tabW, presetH);    x += tabW + btnGap;
    modeBox_.setBounds (x, presetY, modeW, presetH);             x += modeW + btnGap;
    presetBox_.setBounds (x, presetY, presetW, presetH);         x += presetW + btnGap;
    prevPresetButton_.setBounds (x, presetY, arrowW, presetH);   x += arrowW + arrowGap;
    nextPresetButton_.setBounds (x, presetY, arrowW, presetH);   x += arrowW + btnGap;
    savePresetButton_.setBounds (x, presetY, saveW, presetH);    x += saveW + btnGap;
    deletePresetButton_.setBounds (x, presetY, delW, presetH);

    // --- Knob sizes (3 tiers) ---
    int smallKnob  = scaler_.scaled (52);
    int mediumKnob = scaler_.scaled (64);
    int largeKnob  = scaler_.scaled (80);

    // --- Vertical layout: proportional split avoids rounding-error accumulation ---
    // Header row ends at y=76 (52 + 24). Previously knob groups started at y=112
    // (36 px empty band under the algorithm strip that used to live there).
    // Compact to y=84 so the empty band shrinks to ~8 px and the knob rows get
    // the reclaimed ~28 px, making the panel feel denser and less empty.
    int topY   = scaler_.scaled (84);
    int gap    = scaler_.scaled (8);
    int topPad = scaler_.scaled (20);

    // Split available height proportionally (base: 200 top + 250 bottom = 450)
    int availH  = getHeight() - topY - gap - margin;
    int topRowH = juce::roundToInt (availH * (200.0f / 450.0f));
    int bottomH = availH - topRowH;
    int bottomY = topY + topRowH + gap;

    int topUsable  = contentW - gap * 2;
    int inputW     = static_cast<int> (topUsable * 0.28f);
    int timeW      = static_cast<int> (topUsable * 0.36f);
    int characterW = topUsable - inputW - timeW;

    int inputX     = contentX;
    int timeX      = inputX + inputW + gap;
    int characterX = timeX + timeW + gap;

    // Pre-delay sync dropdown (bottom of INPUT group) — calculate first to reserve space
    int syncH = scaler_.scaled (20);
    int syncGap = scaler_.scaled (4);
    {
        int syncW = inputW - scaler_.scaled (16);
        int syncX = inputX + scaler_.scaled (8);
        int syncY = topY + topRowH - syncH - scaler_.scaled (2);
        predelaySyncBox_.setBounds (syncX, syncY, syncW, syncH);
    }

    // INPUT group: Pre-Delay (small), Diffusion (small) — shrink area to clear dropdown
    layoutKnobsInGroup ({ inputX, topY, inputW, topRowH - syncH - syncGap }, topPad,
        { { &preDelay_, smallKnob }, { &diffusion_, smallKnob } }, sf);

    // Freeze button (bottom of TIME group) — calculate first to reserve space
    int freezeH = scaler_.scaled (22);
    int freezeGap = scaler_.scaled (4);
    {
        int freezeW = timeW - scaler_.scaled (16);
        int freezeX = timeX + scaler_.scaled (8);
        int freezeY = topY + topRowH - freezeH - scaler_.scaled (2);
        freezeButton_.setBounds (freezeX, freezeY, freezeW, freezeH);
    }

    // TIME group: Decay (LARGE), Size (medium) — shrink area to clear freeze button
    layoutKnobsInGroup ({ timeX, topY, timeW, topRowH - freezeH - freezeGap }, topPad,
        { { &decay_, largeKnob }, { &size_, mediumKnob } }, sf);

    // CHARACTER group: Bass Mult (small), Treble Mult (small), Crossover (small)
    layoutKnobsInGroup ({ characterX, topY, characterW, topRowH }, topPad,
        { { &bassMult_, smallKnob }, { &trebleMult_, smallKnob }, { &crossover_, smallKnob } }, sf);

    // --- Bottom row ---
    int bottomUsable = contentW - gap * 3;
    int modW    = static_cast<int> (bottomUsable * 0.22f);
    int erW     = static_cast<int> (bottomUsable * 0.22f);
    int eqW     = static_cast<int> (bottomUsable * 0.20f);
    int outputW = bottomUsable - modW - erW - eqW;

    int modX    = contentX;
    int erX     = modX + modW + gap;
    int eqX     = erX + erW + gap;
    int outputX = eqX + eqW + gap;

    // MODULATION group: Depth (small), Rate (small)
    layoutKnobsInGroup ({ modX, bottomY, modW, bottomH }, topPad,
        { { &modDepth_, smallKnob }, { &modRate_, smallKnob } }, sf);

    // EARLY REFLECTIONS group: Level (small), Size (small)
    layoutKnobsInGroup ({ erX, bottomY, erW, bottomH }, topPad,
        { { &erLevel_, smallKnob }, { &erSize_, smallKnob } }, sf);

    // OUTPUT EQ group: Lo Cut (small), Hi Cut (small)
    layoutKnobsInGroup ({ eqX, bottomY, eqW, bottomH }, topPad,
        { { &loCut_, smallKnob }, { &hiCut_, smallKnob } }, sf);

    // Bus mode toggle (bottom of OUTPUT group) — calculate first to reserve space
    int busH = scaler_.scaled (22);
    {
        int busW = outputW - scaler_.scaled (16);
        int busX = outputX + scaler_.scaled (8);
        int busY = bottomY + bottomH - busH - scaler_.scaled (2);
        busModeButton_.setBounds (busX, busY, busW, busH);
    }

    // OUTPUT group: Mix (LARGE), Width (medium) — shrink area to clear bus button
    layoutKnobsInGroup ({ outputX, bottomY, outputW, bottomH - busH - freezeGap }, topPad,
        { { &mix_, largeKnob }, { &width_, mediumKnob } }, sf);

    // --- Advanced panel layout (ONSET + DAMPING top row for Phase 2) ---
    // Knobs sit in the same rectangles as the main panel but visibility is
    // toggled by setActiveTab().
    int advGroupW = (contentW - gap) / 2;
    int onsetX    = contentX;
    int dampingX  = onsetX + advGroupW + gap;

    layoutKnobsInGroup ({ onsetX, topY, advGroupW, topRowH }, topPad,
        { { &advInputOnset_, smallKnob }, { &advSoftOnset_, smallKnob },
          { &advDelayScale_, smallKnob }, { &advNoiseMod_, smallKnob } }, sf);

    layoutKnobsInGroup ({ dampingX, topY, advGroupW, topRowH }, topPad,
        { { &advAirDamping_, smallKnob }, { &advHighCrossover_, smallKnob },
          { &advStructHFDamp_, smallKnob }, { &advInlineDiff_, smallKnob } }, sf);

    // Advanced bottom row: OUTPUT EQ (5) | SPATIAL (4) | DYNAMICS (3), split 45/30/25.
    int advBottomUsable = contentW - gap * 2;
    int advEqW          = static_cast<int> (advBottomUsable * 0.45f);
    int advSpatialW     = static_cast<int> (advBottomUsable * 0.30f);
    int advDynW         = advBottomUsable - advEqW - advSpatialW;
    int advEqX          = contentX;
    int advSpatialX     = advEqX + advEqW + gap;
    int advDynX         = advSpatialX + advSpatialW + gap;

    layoutKnobsInGroup ({ advEqX, bottomY, advEqW, bottomH }, topPad,
        { { &advOutLowShelfDb_,  smallKnob }, { &advOutHighShelfDb_, smallKnob },
          { &advOutHighShelfHz_, smallKnob }, { &advOutMidEqDb_,     smallKnob },
          { &advOutMidEqHz_,     smallKnob } }, sf);

    layoutKnobsInGroup ({ advSpatialX, bottomY, advSpatialW, bottomH }, topPad,
        { { &advStereoCoupling_, smallKnob }, { &advERCrossfeed_, smallKnob },
          { &advChorusDepth_,    smallKnob }, { &advChorusRate_,  smallKnob } }, sf);

    layoutKnobsInGroup ({ advDynX, bottomY, advDynW, bottomH }, topPad,
        { { &advLimiterThresh_, smallKnob }, { &advDecayBoost_, smallKnob },
          { &advTerminalFactor_, smallKnob } }, sf);

    // Level meters (full height of content area)
    int meterTop = topY;
    int meterBot = getHeight() - margin;
    inputMeter_.setBounds (margin, meterTop, meterW, meterBot - meterTop);
    outputMeter_.setBounds (getWidth() - margin - meterW, meterTop, meterW, meterBot - meterTop);
}

void DuskVerbEditor::loadPreset (int index)
{
    const auto& presets = getFactoryPresets();
    if (index >= 0 && index < static_cast<int> (presets.size()))
    {
        const auto& preset = presets[static_cast<size_t> (index)];
        preset.applyTo (processorRef.parameters);
        processorRef.parameters.state.setProperty ("presetName",
            juce::String (preset.name), nullptr);

        // Set preset_id to trigger per-preset ER tap loading in processBlock
        if (auto* p = processorRef.parameters.getParameter ("preset_id"))
            p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (index + 1)));

        // Sync Mode dropdown with the preset's underlying engine type
        modeBox_.setSelectedId (engineModeIdForAlgorithm (preset.algorithm),
                                juce::dontSendNotification);
    }
}

void DuskVerbEditor::stepFactoryPreset (int delta)
{
    const int factoryCount = static_cast<int> (getFactoryPresets().size());
    if (factoryCount <= 0)
        return;

    // Current selection: factory IDs are 2..factoryCount+1. Anything else (user
    // preset, Mode dropdown, nothing selected) means the user is outside the
    // factory list, so stepping should land on preset[0] or preset[last] directly
    // rather than advancing past an implicit "current = 0".
    int id = presetBox_.getSelectedId();
    int nextIdx;
    if (id >= 2 && id < 2 + factoryCount)
    {
        int currentIdx = id - 2;
        nextIdx = (currentIdx + delta + factoryCount) % factoryCount;
    }
    else
    {
        nextIdx = (delta >= 0) ? 0 : factoryCount - 1;
    }
    presetBox_.setSelectedId (nextIdx + 2, juce::sendNotificationSync);
}

void DuskVerbEditor::selectEngineMode (int modeId)
{
    if (modeId < 1 || modeId > 3)
        return;

    // Mode → anchor preset (one representative per engine type). Looked up by
    // name so the mapping survives reordering in AlgorithmConfig's lookup table.
    static const juce::String kAnchorName[] = {
        "Vocal Hall",     // FDN
        "Drum Room",      // Dattorro
        "Bright Chamber"  // QuadTank
    };

    const auto& presets = getFactoryPresets();
    int anchorIdx = -1;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (kAnchorName[modeId - 1] == presets[i].name)
        {
            anchorIdx = static_cast<int> (i);
            break;
        }
    }
    if (anchorIdx < 0)
        return;

    // Load the anchor preset's full parameter set so Mode immediately produces
    // an audible, well-defined reverb for the chosen engine type. Earlier
    // versions only swapped the algorithm index and left other knobs alone,
    // which on a fresh plugin (bus_mode=false, mix=0.35) yielded near-silent
    // wet output and looked like "Mode produces no reverb".
    presets[static_cast<size_t> (anchorIdx)].applyTo (processorRef.parameters);
    if (auto* p = processorRef.parameters.getParameter ("preset_id"))
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (anchorIdx + 1)));

    // Mode supersedes any explicit preset selection — keep the preset
    // dropdown showing "Preset" rather than the loaded anchor's name.
    presetBox_.setSelectedId (0, juce::dontSendNotification);
    processorRef.parameters.state.setProperty ("presetName", "", nullptr);
    updateDeleteButtonVisibility();
}

void DuskVerbEditor::setActiveTab (bool advanced)
{
    if (showingAdvanced_ == advanced)
        return;
    showingAdvanced_ = advanced;

    // IMPORTANT: apply visibility AFTER resized(), because placeKnob()
    // unconditionally calls valueLabel.setVisible(true) during layout — setting
    // visibility before resized() would be clobbered.
    resized();

    auto setKnobVisible = [] (KnobWithLabel& k, bool v)
    {
        k.slider    .setVisible (v);
        k.nameLabel .setVisible (v);
        k.valueLabel.setVisible (v);
    };

    const bool mainVisible = ! advanced;
    for (auto* k : { &preDelay_, &diffusion_, &decay_, &size_, &bassMult_, &trebleMult_,
                     &crossover_, &modDepth_, &modRate_, &erLevel_, &erSize_, &mix_,
                     &loCut_, &hiCut_, &width_ })
        setKnobVisible (*k, mainVisible);

    freezeButton_    .setVisible (mainVisible);
    busModeButton_   .setVisible (mainVisible);
    predelaySyncBox_ .setVisible (mainVisible);

    const bool advVisible = advanced;
    for (auto* k : { &advInputOnset_, &advSoftOnset_, &advDelayScale_, &advNoiseMod_,
                     &advAirDamping_, &advHighCrossover_, &advStructHFDamp_, &advInlineDiff_,
                     &advOutLowShelfDb_, &advOutHighShelfDb_, &advOutHighShelfHz_,
                     &advOutMidEqDb_, &advOutMidEqHz_,
                     &advStereoCoupling_, &advERCrossfeed_, &advChorusDepth_, &advChorusRate_,
                     &advLimiterThresh_, &advDecayBoost_, &advTerminalFactor_ })
        setKnobVisible (*k, advVisible);

    repaint();
}

// =============================================================================
// User Preset Management
// =============================================================================

void DuskVerbEditor::refreshPresetList()
{
    int currentId = presetBox_.getSelectedId();
    presetBox_.clear (juce::dontSendNotification);

    // Factory presets grouped by category (IDs starting at 2)
    const auto& presets = getFactoryPresets();
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

    // Restore selection
    if (currentId > 0)
        presetBox_.setSelectedId (currentId, juce::dontSendNotification);
}

void DuskVerbEditor::saveUserPreset()
{
    if (! userPresetManager_)
        return;

    auto* dialog = new juce::AlertWindow ("Save Preset",
                                           "Enter a name for this preset:",
                                           juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor ("name", "My Preset", "Preset Name:");
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<DuskVerbEditor> safeThis (this);
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
                juce::Component::SafePointer<DuskVerbEditor> safeInner (safeThis.getComponent());

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

void DuskVerbEditor::loadUserPreset (const juce::String& name)
{
    if (! userPresetManager_)
        return;

    auto state = userPresetManager_->loadUserPreset (name);
    if (state.isValid())
    {
        processorRef.parameters.replaceState (state);
        processorRef.setGainTrim (0.0f);
        processorRef.parameters.state.setProperty ("presetName", name, nullptr);
        processorRef.parameters.state.setProperty ("gainTrim", 0.0f, nullptr);
    }
}

void DuskVerbEditor::deleteUserPreset (const juce::String& name)
{
    if (! userPresetManager_)
        return;

    userPresetManager_->deleteUserPreset (name);
    refreshPresetList();
}

// =============================================================================
// Supporters Overlay
// =============================================================================

void DuskVerbEditor::mouseDown (const juce::MouseEvent& e)
{
    if (titleClickArea_.contains (e.getPosition()))
        showSupportersPanel();
}

void DuskVerbEditor::showSupportersPanel()
{
    if (! supportersOverlay_)
    {
        supportersOverlay_ = std::make_unique<SupportersOverlay> ("DuskVerb", JucePlugin_VersionString);
        supportersOverlay_->onDismiss = [this] { hideSupportersPanel(); };
        addAndMakeVisible (supportersOverlay_.get());
    }
    supportersOverlay_->setBounds (getLocalBounds());
    supportersOverlay_->toFront (true);
    supportersOverlay_->setVisible (true);
}

void DuskVerbEditor::hideSupportersPanel()
{
    if (supportersOverlay_)
        supportersOverlay_->setVisible (false);
}

void DuskVerbEditor::updateDeleteButtonVisibility()
{
    deletePresetButton_.setVisible (presetBox_.getSelectedId() >= 1001);
}

#include "PluginEditor.h"
#include "FactoryPresets.h"

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
    nameLabel.setFont (juce::FontOptions (10.0f));
    nameLabel.setColour (juce::Label::textColourId,
                         juce::Colour (DuskVerbLookAndFeel::kSubtleText));
    parent.addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setInterceptsMouseClicks (false, false);
    valueLabel.setFont (juce::FontOptions (11.0f));
    valueLabel.setColour (juce::Label::textColourId,
                          juce::Colour (0xffd0d0d0));  // brighter than name label
    parent.addAndMakeVisible (valueLabel);

    // Store suffix in name field for formatting
    valueLabel.setName (suffix);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);
}

// =============================================================================
// AlgorithmSelector
// =============================================================================

AlgorithmSelector::AlgorithmSelector (juce::RangedAudioParameter& param)
    : param_ (param),
      attachment_ (param,
                   [this] (float v) {
                       currentIndex_ = juce::roundToInt (param_.convertFrom0to1 (v));
                       repaint();
                   },
                   nullptr)
{
    currentIndex_ = juce::roundToInt (param.convertFrom0to1 (param.getValue()));
    setRepaintsOnMouseActivity (true);
}

void AlgorithmSelector::resized()
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

void AlgorithmSelector::paint (juce::Graphics& g)
{
    float cornerRadius = static_cast<float> (getHeight()) * 0.35f;

    // Outer container background
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kPanel));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), cornerRadius);

    // Outer border
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kBorder));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), cornerRadius, 1.0f);

    for (int i = 0; i < static_cast<int> (segmentBounds_.size()); ++i)
    {
        auto seg = segmentBounds_[static_cast<size_t> (i)].toFloat();
        bool selected = (i == currentIndex_);
        bool hovered = segmentBounds_[static_cast<size_t> (i)].contains (getMouseXYRelative())
                       && ! selected;

        if (selected)
        {
            g.setColour (juce::Colour (DuskVerbLookAndFeel::kAccent));
            g.fillRoundedRectangle (seg.reduced (2.0f), cornerRadius - 2.0f);
        }
        else if (hovered)
        {
            g.setColour (juce::Colour (DuskVerbLookAndFeel::kAccent).withAlpha (0.15f));
            g.fillRoundedRectangle (seg.reduced (2.0f), cornerRadius - 2.0f);
        }

        g.setColour (selected ? juce::Colour (DuskVerbLookAndFeel::kText)
                              : juce::Colour (DuskVerbLookAndFeel::kSubtleText));
        g.setFont (juce::FontOptions (11.0f, selected ? juce::Font::bold : juce::Font::plain));
        g.drawText (labels_[i], segmentBounds_[static_cast<size_t> (i)],
                    juce::Justification::centred);
    }
}

void AlgorithmSelector::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < static_cast<int> (segmentBounds_.size()); ++i)
    {
        if (segmentBounds_[static_cast<size_t> (i)].contains (e.getPosition()))
        {
            if (i != currentIndex_)
            {
                currentIndex_ = i;
                attachment_.setValueAsCompleteGesture (param_.convertTo0to1 (static_cast<float> (i)));
                repaint();
            }
            break;
        }
    }
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

    auto accentColour = button.getName() == "freeze"
                            ? juce::Colour (kFreezeOn)
                            : juce::Colour (kAccent);

    // Background pill
    float cornerSize = bounds.getHeight() * 0.5f;
    g.setColour (on ? accentColour.withAlpha (0.3f) : juce::Colour (kPanel));
    g.fillRoundedRectangle (bounds, cornerSize);

    // Border
    g.setColour (on ? accentColour : juce::Colour (kBorder));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerSize, 1.0f);

    // Text
    g.setColour (on ? accentColour : juce::Colour (kSubtleText));
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
            return juce::String (v * 1000.0, 0) + " ms";
        return juce::String (v, 1) + " s";
    }
    if (suffix == " ms")
        return juce::String (v, 0) + " ms";
    if (suffix == " Hz")
    {
        if (v >= 1000.0)
            return juce::String (v / 1000.0, 1) + " kHz";
        return juce::String (v, 0) + " Hz";
    }
    if (suffix == "x")
        return juce::String (v, 2) + "x";
    if (suffix == "%")
        return juce::String (v * 100.0, 0) + "%";

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
        "Stereo width: 0% mono, 100% normal, 200% hyper-wide");

    // Algorithm selector (segmented button strip)
    auto* algoParam = p.parameters.getParameter ("algorithm");
    jassert (algoParam != nullptr);
    algorithmSelector_ = std::make_unique<AlgorithmSelector> (*algoParam);
    addAndMakeVisible (*algorithmSelector_);

    // User preset manager
    userPresetManager_ = std::make_unique<UserPresetManager> ("DuskVerb");

    // Preset browser (factory + user presets)
    presetBox_.setJustificationType (juce::Justification::centred);
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
        updateDeleteButtonVisibility();
    };
    addAndMakeVisible (presetBox_);
    refreshPresetList();

    // Save preset button
    savePresetButton_.setButtonText ("Save");
    savePresetButton_.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a5a8a));
    savePresetButton_.onClick = [this] { saveUserPreset(); };
    addAndMakeVisible (savePresetButton_);

    // Delete preset button (only visible when a user preset is selected)
    deletePresetButton_.setButtonText ("Del");
    deletePresetButton_.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff5a3a3a));
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
                          const juce::String& title)
{
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kPanel));
    g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

    g.setColour (juce::Colour (DuskVerbLookAndFeel::kBorder));
    g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);

    // Group title with letter spacing, left-aligned
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kSubtleText));
    g.setFont (juce::FontOptions (10.0f));

    juce::String spaced;
    for (int i = 0; i < title.length(); ++i)
    {
        spaced += title[i];
        if (i < title.length() - 1)
            spaced += ' ';
    }

    auto titleArea = bounds.withHeight (20).withTrimmedLeft (10);
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
    int topY    = scaler_.scaled (112);
    int topRowH = scaler_.scaled (200);
    int gap     = scaler_.scaled (8);

    int topUsable  = contentW - gap * 2;
    int inputW     = static_cast<int> (topUsable * 0.28f);
    int timeW      = static_cast<int> (topUsable * 0.36f);
    int characterW = topUsable - inputW - timeW;

    int inputX     = contentX;
    int timeX      = inputX + inputW + gap;
    int characterX = timeX + timeW + gap;

    drawGroupBox (g, { inputX, topY, inputW, topRowH }, "INPUT");
    drawGroupBox (g, { timeX, topY, timeW, topRowH }, "TIME");
    drawGroupBox (g, { characterX, topY, characterW, topRowH }, "CHARACTER");

    int bottomY = topY + topRowH + gap;
    int bottomH = getHeight() - bottomY - margin;

    int bottomUsable = contentW - gap * 3;
    int modW    = static_cast<int> (bottomUsable * 0.22f);
    int erW     = static_cast<int> (bottomUsable * 0.22f);
    int eqW     = static_cast<int> (bottomUsable * 0.20f);
    int outputW = bottomUsable - modW - erW - eqW;

    int modX    = contentX;
    int erX     = modX + modW + gap;
    int eqX     = erX + erW + gap;
    int outputX = eqX + eqW + gap;

    drawGroupBox (g, { modX, bottomY, modW, bottomH }, "MODULATION");
    drawGroupBox (g, { erX, bottomY, erW, bottomH }, "EARLY REFLECTIONS");
    drawGroupBox (g, { eqX, bottomY, eqW, bottomH }, "OUTPUT EQ");
    drawGroupBox (g, { outputX, bottomY, outputW, bottomH }, "OUTPUT");

    // Meter labels
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kSubtleText));
    g.setFont (juce::FontOptions (8.0f * sf));
    g.drawText ("IN", margin, topY - scaler_.scaled (12), meterW, scaler_.scaled (12),
                juce::Justification::centred);
    g.drawText ("OUT", getWidth() - margin - meterW, topY - scaler_.scaled (12), meterW,
                scaler_.scaled (12), juce::Justification::centred);
}

// =============================================================================
// Layout
// =============================================================================

static void placeKnob (KnobWithLabel& k, juce::Rectangle<int> area, int knobSize)
{
    int nameH  = 14;
    int valueH = 14;
    int totalH = nameH + knobSize + valueH;

    int yPad = (area.getHeight() - totalH) / 2;
    auto col = area;
    if (yPad > 0)
        col.removeFromTop (yPad);

    k.nameLabel.setBounds (col.removeFromTop (nameH));

    auto knobArea = col.removeFromTop (knobSize);
    k.slider.setBounds (knobArea.withSizeKeepingCentre (knobSize, knobSize));

    k.valueLabel.setBounds (col.removeFromTop (valueH));
}

static void layoutKnobsInGroup (juce::Rectangle<int> groupBounds, int topPad,
                                std::vector<std::pair<KnobWithLabel*, int>> knobs)
{
    auto area = groupBounds.reduced (4, 0);
    area.removeFromTop (topPad);

    int numKnobs = static_cast<int> (knobs.size());
    int colW = area.getWidth() / numKnobs;

    for (auto& [knob, knobSize] : knobs)
    {
        auto col = area.removeFromLeft (colW);
        placeKnob (*knob, col, knobSize);
    }
}

void DuskVerbEditor::resized()
{
    scaler_.updateResizer();

    int margin   = scaler_.scaled (10);
    int meterW   = scaler_.scaled (22);
    int meterGap = scaler_.scaled (6);
    int contentX = margin + meterW + meterGap;
    int contentW = getWidth() - contentX - margin - meterW - meterGap;

    // Title click area for supporters overlay
    int titleW = scaler_.scaled (200);
    titleClickArea_ = { (getWidth() - titleW) / 2, scaler_.scaled (6),
                         titleW, scaler_.scaled (38) };

    // --- Header: preset + save/delete buttons + algorithm strip ---
    int presetW = scaler_.scaled (200);
    int presetH = scaler_.scaled (24);
    int presetY = scaler_.scaled (52);
    int saveW   = scaler_.scaled (50);
    int delW    = scaler_.scaled (36);
    int btnGap  = scaler_.scaled (4);
    int totalPresetW = presetW + btnGap + saveW + btnGap + delW;
    int presetStartX = (getWidth() - totalPresetW) / 2;
    presetBox_.setBounds (presetStartX, presetY, presetW, presetH);
    savePresetButton_.setBounds (presetStartX + presetW + btnGap, presetY, saveW, presetH);
    deletePresetButton_.setBounds (presetStartX + presetW + btnGap + saveW + btnGap,
                                   presetY, delW, presetH);

    int algoW = scaler_.scaled (500);
    int algoH = scaler_.scaled (28);
    int algoY = scaler_.scaled (80);
    algorithmSelector_->setBounds ((getWidth() - algoW) / 2, algoY, algoW, algoH);

    // --- Knob sizes (3 tiers) ---
    int smallKnob  = scaler_.scaled (52);
    int mediumKnob = scaler_.scaled (64);
    int largeKnob  = scaler_.scaled (80);

    // --- Top row ---
    int topY    = scaler_.scaled (112);
    int topRowH = scaler_.scaled (200);
    int gap     = scaler_.scaled (8);
    int topPad  = scaler_.scaled (20);

    int topUsable  = contentW - gap * 2;
    int inputW     = static_cast<int> (topUsable * 0.28f);
    int timeW      = static_cast<int> (topUsable * 0.36f);
    int characterW = topUsable - inputW - timeW;

    int inputX     = contentX;
    int timeX      = inputX + inputW + gap;
    int characterX = timeX + timeW + gap;

    // INPUT group: Pre-Delay (small), Diffusion (small)
    layoutKnobsInGroup ({ inputX, topY, inputW, topRowH }, topPad,
        { { &preDelay_, smallKnob }, { &diffusion_, smallKnob } });

    // Pre-delay sync dropdown (bottom of INPUT group)
    {
        int syncH = scaler_.scaled (20);
        int syncW = inputW - scaler_.scaled (16);
        int syncX = inputX + scaler_.scaled (8);
        int syncY = topY + topRowH - syncH - scaler_.scaled (6);
        predelaySyncBox_.setBounds (syncX, syncY, syncW, syncH);
    }

    // TIME group: Decay (LARGE), Size (LARGE)
    layoutKnobsInGroup ({ timeX, topY, timeW, topRowH }, topPad,
        { { &decay_, largeKnob }, { &size_, largeKnob } });

    // Freeze button (bottom of TIME group)
    {
        int freezeH = scaler_.scaled (22);
        int freezeW = timeW - scaler_.scaled (16);
        int freezeX = timeX + scaler_.scaled (8);
        int freezeY = topY + topRowH - freezeH - scaler_.scaled (6);
        freezeButton_.setBounds (freezeX, freezeY, freezeW, freezeH);
    }

    // CHARACTER group: Bass Mult (small), Treble Mult (small), Crossover (small)
    layoutKnobsInGroup ({ characterX, topY, characterW, topRowH }, topPad,
        { { &bassMult_, smallKnob }, { &trebleMult_, smallKnob }, { &crossover_, smallKnob } });

    // --- Bottom row ---
    int bottomY = topY + topRowH + gap;
    int bottomH = getHeight() - bottomY - margin;

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
        { { &modDepth_, smallKnob }, { &modRate_, smallKnob } });

    // EARLY REFLECTIONS group: Level (small), Size (small)
    layoutKnobsInGroup ({ erX, bottomY, erW, bottomH }, topPad,
        { { &erLevel_, smallKnob }, { &erSize_, smallKnob } });

    // OUTPUT EQ group: Lo Cut (small), Hi Cut (small)
    layoutKnobsInGroup ({ eqX, bottomY, eqW, bottomH }, topPad,
        { { &loCut_, smallKnob }, { &hiCut_, smallKnob } });

    // OUTPUT group: Mix (LARGE), Width (medium)
    layoutKnobsInGroup ({ outputX, bottomY, outputW, bottomH }, topPad,
        { { &mix_, largeKnob }, { &width_, mediumKnob } });

    // Bus mode toggle (bottom of OUTPUT group)
    {
        int busH = scaler_.scaled (22);
        int busW = outputW - scaler_.scaled (16);
        int busX = outputX + scaler_.scaled (8);
        int busY = bottomY + bottomH - busH - scaler_.scaled (6);
        busModeButton_.setBounds (busX, busY, busW, busH);
    }

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
        presets[static_cast<size_t> (index)].applyTo (processorRef.parameters);
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
        processorRef.parameters.replaceState (state);
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

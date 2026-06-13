#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "../../shared/DuskLookAndFeel.h"   // ValueEditor::popUp

#include <algorithm>
#include <cmath>

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
    // Disable JUCE's built-in double-click-resets-to-default. We use double-
    // click for the value-editor popup instead. Without this, JUCE's
    // SliderAttachment installs the param's default as the reset value, and
    // double-click would call setValue(default, sendNotificationSync) BEFORE
    // our MouseListener opens the editor — which then async-defers another
    // setValue(default) that stomps any drag the user does next. Net effect
    // (without this line): double-click + drag → knob moves visually but
    // value snaps back to default.
    slider.setDoubleClickReturnValue (false, 0.0);
    if (tooltip.isNotEmpty())
        slider.setTooltip (tooltip);
    parent.addAndMakeVisible (slider);

    // NAME — small, dim, all-caps via the source string. Sits at the top of
    // the column above the value readout. Lower-tier visually so the value
    // is the dominant element.
    nameLabel.setText (displayName, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setInterceptsMouseClicks (false, false);
    nameLabel.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId,
                         juce::Colour (DuskVerbLookAndFeel::kGroupText));
    parent.addAndMakeVisible (nameLabel);

    // VALUE — accent colour, ABOVE the knob. Slightly larger than the name
    // but not so big that it competes with the knob itself for attention.
    // setInterceptsMouseClicks(true, false) — captures double-clicks for the
    // ValueEditor popup but doesn't block child events (no children anyway).
    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setInterceptsMouseClicks (true, false);
    valueLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    valueLabel.setColour (juce::Label::textColourId,
                          juce::Colour (DuskVerbLookAndFeel::kAccent));
    parent.addAndMakeVisible (valueLabel);

    // Double-click → spawn ValueEditor popup over the value label. Wired via
    // a small adapter MouseListener so we don't have to subclass juce::Slider
    // or juce::Label. addMouseListener is additive — the slider's normal
    // drag/click behaviour is preserved, we just intercept double-click on
    // top of it. Both the knob body AND the value label trigger the popup
    // (per user spec) and the editor anchors over the value label so it
    // appears exactly where the visible value text lives.
    struct ValueEditorTrigger : public juce::MouseListener
    {
        juce::Slider* slider = nullptr;
        juce::Component* anchor = nullptr;
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (slider != nullptr && anchor != nullptr)
                ValueEditor::popUp (*slider, *anchor);
        }
    };
    valueEditorTrigger = std::make_unique<ValueEditorTrigger>();
    auto* trig = static_cast<ValueEditorTrigger*> (valueEditorTrigger.get());
    trig->slider = &slider;
    trig->anchor = &valueLabel;
    slider    .addMouseListener (trig, false);
    valueLabel.addMouseListener (trig, false);

    valueLabel.setName (suffix);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);

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
        if (sfx == " dB")   return juce::String (v, 1) + " dB";
        if (sfx == "x")     return juce::String (v, 2) + "x";
        if (sfx == "%")     return juce::String (v * 100.0, 1) + "%";
        return juce::String (v, 2);
    };
}

void KnobWithLabel::setAccent (juce::Colour accent)
{
    currentAccent = accent;
    valueLabel.setColour (juce::Label::textColourId, accent);
    valueLabel.repaint();
    slider.repaint();   // arc colour comes from LookAndFeel; force redraw
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

    setColour (juce::ComboBox::backgroundColourId, juce::Colour (kPanel));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (kBorder));
    setColour (juce::ComboBox::textColourId, juce::Colour (kText));
    setColour (juce::ComboBox::arrowColourId, juce::Colour (kGroupText));

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

    if (isDragging)
    {
        float glowRadius = radius;
        g.setColour (currentAccent_.withAlpha (0.12f));
        g.fillEllipse (centre.x - glowRadius, centre.y - glowRadius,
                       glowRadius * 2.0f, glowRadius * 2.0f);
    }

    float outerRadius = radius - 2.0f;
    g.setColour (juce::Colour (0xff0d0d1a));
    g.fillEllipse (centre.x - outerRadius, centre.y - outerRadius,
                   outerRadius * 2.0f, outerRadius * 2.0f);

    float knobRadius = outerRadius - 3.0f;
    g.setColour (isHovered ? juce::Colour (kKnobFill).brighter (0.15f)
                           : juce::Colour (kKnobFill));
    g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius,
                   knobRadius * 2.0f, knobRadius * 2.0f);

    float arcRadius = outerRadius - 1.5f;
    float lineW = 3.0f;
    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff2a2a3e));
    g.strokePath (trackArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    if (angle > rotaryStartAngle + 0.01f)
    {
        juce::Path filledArc;
        filledArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                                 0.0f, rotaryStartAngle, angle, true);

        auto accentCol = currentAccent_;
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

    float dotRadius = 3.0f;
    float dotDist = knobRadius - 6.0f;
    float dotX = centre.x + dotDist * std::sin (angle);
    float dotY = centre.y - dotDist * std::cos (angle);
    g.setColour (isDragging ? juce::Colours::white : juce::Colour (kText));
    g.fillEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);

    // Value text is no longer drawn inside the knob — KnobWithLabel renders
    // it ABOVE the knob in the accent colour for stronger visual hierarchy.
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
        g.setColour (accent.withAlpha (0.4f));
        g.fillRoundedRectangle (bounds.expanded (2.0f), cornerSize + 2.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (bounds, cornerSize);
        g.setColour (juce::Colours::white);
    }
    else
    {
        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (bounds, cornerSize);
        g.setColour (juce::Colour (kBorder));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerSize, 1.0f);
        g.setColour (juce::Colour (kGroupText));
    }

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred);
}

// =============================================================================
// Custom dark dropdown popup (preset / algorithm / sync menus). Replaces the
// default LookAndFeel_V4 grey popup so every dropdown matches the plugin: dark
// panel + border, rounded hover fill in the live engine accent, accent dot for
// the selected item, slim submenu arrow.
// =============================================================================

juce::Font DuskVerbLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (15.0f));
}

void DuskVerbLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (juce::Colour (kBackground));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (juce::Colour (kBorder));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}

void DuskVerbLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                                     int standardMenuItemHeight,
                                                     int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth  = 60;
        idealHeight = 8;
        return;
    }

    const auto font = getPopupMenuFont();
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text, 0.0f, 0.0f);
    const int textW = (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth());

    idealHeight = standardMenuItemHeight > 0 ? juce::jmax (standardMenuItemHeight, 28) : 28;
    idealWidth  = textW + 52;   // left tick gutter + right submenu-arrow padding
}

void DuskVerbLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                             bool isSeparator, bool isActive, bool isHighlighted,
                                             bool isTicked, bool hasSubMenu, const juce::String& text,
                                             const juce::String& /*shortcutKeyText*/,
                                             const juce::Drawable* /*icon*/, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        auto line = area.toFloat().reduced (10.0f, 0.0f).withHeight (1.0f)
                        .withY ((float) area.getCentreY());
        g.setColour (juce::Colour (kSeparator));
        g.fillRect (line);
        return;
    }

    auto r = area.reduced (4, 1);
    const auto accent = getCurrentAccent();

    if (isHighlighted && isActive)
    {
        g.setColour (accent.withAlpha (0.22f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }

    juce::Colour col = isActive ? juce::Colour (kText) : juce::Colour (kDimText);
    if (textColour != nullptr)        col = *textColour;
    if (isHighlighted && isActive)    col = juce::Colour (kValueText);

    auto content = r.reduced (10, 0);

    if (isTicked)
    {
        const float d = 5.0f;
        g.setColour (accent);
        g.fillEllipse ((float) content.getX(), (float) content.getCentreY() - d * 0.5f, d, d);
    }
    content.removeFromLeft (16);   // tick gutter

    g.setColour (col);
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, content, juce::Justification::centredLeft, 1);

    if (hasSubMenu)
    {
        const float h = (float) r.getHeight() * 0.26f;
        const float x = (float) r.getRight() - 12.0f;
        const float yc = (float) r.getCentreY();
        juce::Path arrow;
        arrow.addTriangle (x, yc - h, x, yc + h, x + h, yc);
        g.setColour (col.withAlpha (0.75f));
        g.fillPath (arrow);
    }
}

// =============================================================================
// Value formatting (used by the timer to refresh the value label under each knob)
// =============================================================================

namespace
{
    juce::String formatValue (const juce::Slider& s, const juce::String& suffix)
    {
        double v = s.getValue();
        if (suffix == " s")
            return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms"
                           : juce::String (v, 2) + " s";
        if (suffix == " ms")  return juce::String (juce::roundToInt (v)) + " ms";
        if (suffix == " Hz")
            return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz"
                               : v < 100.0 ? juce::String (v, 2) + " Hz"
                                           : juce::String (juce::roundToInt (v)) + " Hz";
        if (suffix == " dB")  return juce::String (v, 1) + " dB";
        if (suffix == "x")    return juce::String (v, 2) + "x";
        if (suffix == "%")    return juce::String (v * 100.0, 1) + "%";
        return juce::String (v, 2);
    }

    void drawGroupBox (juce::Graphics& g, juce::Rectangle<int> bounds,
                       const juce::String& title, int titleBandH)
    {
        // Panel fill BRIGHTER than the editor background so the group reads
        // as a contained surface. Previous "darker than background" attempt
        // collapsed into the background colour and made the layout look flat.
        g.setColour (juce::Colour (DuskVerbLookAndFeel::kPanel));
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

        // 1 px border — gives each group a clear edge.
        g.setColour (juce::Colour (DuskVerbLookAndFeel::kBorder));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);

        // Group title — bold and clearly readable, horizontally centred
        // over the panel so the section anchors the eye symmetrically.
        g.setColour (juce::Colour (DuskVerbLookAndFeel::kLabelText));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, bounds.removeFromTop (titleBandH).reduced (8, 0),
                    juce::Justification::centred);
    }
}

// =============================================================================
// DuskVerbEditor
// =============================================================================

// Tightened 2026-05-24: previous 1400×640 left ~30% empty whitespace inside
// every panel column (knobs vertically over-centred). Pass 2 cuts height
// to 470 + bumps knob diameters (62/78 → 76/92). Pass 3 nudges height
// back to 510 so the DECAY hero (limited by knob-area H) can grow to ~135 px.
// Pass 4 pushes further (510 → 560) and bumps knobMed 76 → 84 so the hero
// grows to ~159 px (vs MIX at 92) while the secondary-tier knobs eat more of
// their column yPad. Pass 5 keeps the FREEZE strip at 22 px (was briefly
// shrunk to 16) so FREEZE / BUS / GATE all render at the same visual height.
static constexpr int kBaseWidth  = 1400;   // widened 1240->1400 for the bottom-row knobs
static constexpr int kBaseHeight = 720;    // 560->720: added a 3rd row (MACRO: Tone/Character/Duck)

DuskVerbEditor::DuskVerbEditor (DuskVerbProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    setLookAndFeel (&lnf_);

    // ---- 16 main knobs ----
    preDelay_ .init (*this, p.parameters, "predelay",  "PRE-DELAY",   " ms",
        "Delay before reverb starts. Creates space between dry signal and reverb tail");
    decay_    .init (*this, p.parameters, "decay",
        "Reverb tail length (RT60). Hero control — drag vertically.");
    size_     .init (*this, p.parameters, "size",      "SIZE",        "%",
        "Virtual room size — affects echo density and spacing");
    modDepth_ .init (*this, p.parameters, "mod_depth", "DEPTH",       "%",
        "Chorus-like modulation depth. Reduces metallic ringing");
    modRate_  .init (*this, p.parameters, "mod_rate",  "RATE",        " Hz",
        "Speed of internal pitch modulation");
    damping_  .init (*this, p.parameters, "damping",   "TREBLE MULT", "x",
        "High-frequency decay multiplier. <1× = natural air absorption");
    bassMult_ .init (*this, p.parameters, "bass_mult", "BASS MULT",   "x",
        "Low-frequency decay multiplier. >1× = bass rings longer than mids");
    midMult_  .init (*this, p.parameters, "mid_mult",  "MID MULT",    "x",
        "Mid-band decay multiplier (between low and high crossovers). "
        "1.0× = natural rate. >1× = mids ring longer; <1× = mids decay faster.");
    crossover_.init (*this, p.parameters, "crossover", "LOW XOVER",   " Hz",
        "Bass↔mid split frequency. Below this, bass multiplier applies.");
    highCrossover_.init (*this, p.parameters, "high_crossover", "HIGH XOVER", " Hz",
        "Mid↔treble split frequency. Above this, treble multiplier applies.");
    saturation_.init (*this, p.parameters, "saturation", "SATURATION", "%",
        "In-loop tanh drive. 0% = clean (transparent reverb). "
        "100% = warm analog-style saturation on every loop pass.");
    diffusion_.init (*this, p.parameters, "diffusion", "DIFFUSION",   "%",
        "Tail density. Low = sparse, audible echo grain. High = smooth dense wash. "
        "Affects both input transient smear and in-loop tank density across all engines.");
    erLevel_  .init (*this, p.parameters, "er_level",  "ER LEVEL",    "%",
        "Early reflections level. First echoes that define room shape");
    erSize_   .init (*this, p.parameters, "er_size",   "ER SIZE",     "%",
        "Early reflection spacing. Larger = bigger perceived room");
    mix_      .init (*this, p.parameters, "mix",       "DRY/WET",     "%",
        "Balance between dry input and reverb. Use BUS mode for send/return");
    loCut_    .init (*this, p.parameters, "lo_cut",    "LO CUT",      " Hz",
        "High-pass filter on the wet signal");
    hiCut_    .init (*this, p.parameters, "hi_cut",    "HI CUT",      " Hz",
        "Low-pass filter on the wet signal");
    monoBelow_.init (*this, p.parameters, "mono_below","MONO <",      " Hz",
        "Sums the wet signal to MONO below this cutoff. 20 Hz = bypass. "
        "Use 80-150 Hz to keep low-end punch tight in a mix.");
    width_    .init (*this, p.parameters, "width",     "WIDTH",       "%",
        "Stereo width of the wet signal");
    gainTrim_ .init (*this, p.parameters, "gain_trim", "TRIM",        " dB",
        "Output gain offset applied after the dry/wet mix");
    duck_     .init (*this, p.parameters, "duck",      "DUCK",        "%",
        "Ducks the wet tail while the dry input is loud, then lets it swell "
        "back as the source decays. 0 = off. Keeps vocals/drums up front.");
    tone_     .init (*this, p.parameters, "tone",      "TONE",        "",
        "Global spectral tilt over any space: left = darker, centre = preset "
        "as-voiced, right = brighter. Layers on top of the preset's voicing.");
    character_.init (*this, p.parameters, "character", "CHARACTER",   "%",
        "Adds movement + grit: more modulation/chorus and saturation as you turn "
        "it up. 0 = the preset as-voiced.");

    // ---- Algorithm selector + topology glyph ----
    // The glyph reads the current algorithm and renders a tiny topology icon
    // (2 dots / 6 dots / 4-grid / 16-grid) so the user sees which engine is
    // active at a glance, not just its name. It updates whenever the
    // dropdown changes.
    algorithmBox_.setJustificationType (juce::Justification::centred);
    algorithmBox_.setTooltip ("Reverb space. Switching here changes the DSP without "
                              "overwriting your knob values.");
    // Curated palette: only the engines flagged `visible` are offered (the
    // dead/redundant slots are hidden). Item IDs stay engineIndex+1 so they map
    // directly onto the 14-wide `algorithm` choice param — we sync manually
    // (a ComboBoxAttachment is position-based and can't handle a sparse list).
    for (int i = 0; i < getNumAlgorithms(); ++i)
        if (getAlgorithmConfig (i).visible)
            algorithmBox_.addItem (getAlgorithmConfig (i).name, i + 1);
    addAndMakeVisible (algorithmBox_);
    {
        const int curIdx = static_cast<int> (p.parameters.getRawParameterValue ("algorithm")->load());
        // A hidden engine loaded from saved state isn't in the curated list —
        // add it so the box isn't blank and the selection stays recoverable.
        if (curIdx >= 0 && curIdx < getNumAlgorithms()
            && algorithmBox_.indexOfItemId (curIdx + 1) < 0)
            algorithmBox_.addItem (getAlgorithmConfig (curIdx).name, curIdx + 1);
        algorithmBox_.setSelectedId (curIdx + 1, juce::dontSendNotification);
    }

    addAndMakeVisible (engineGlyph_);
    {
        const auto initialEngine = getAlgorithmConfig (
            static_cast<int> (p.parameters.getRawParameterValue ("algorithm")->load())).engine;
        engineGlyph_.setEngine (initialEngine);
        applyEngineAccent (initialEngine);
    }
    algorithmBox_.onChange = [this, &p]
    {
        const int idx = algorithmBox_.getSelectedId() - 1;
        if (idx >= 0 && idx < getNumAlgorithms())
        {
            // Manual sync (no attachment): write the 14-wide choice param by index.
            if (auto* ap = p.parameters.getParameter ("algorithm"))
                ap->setValueNotifyingHost (ap->convertTo0to1 ((float) idx));

            const auto e = getAlgorithmConfig (idx).engine;
            engineGlyph_.setEngine (e);
            applyEngineAccent (e);   // recolour all knobs / value labels / tail meter
        }
    };


    // ---- User preset manager ----
    userPresetManager_ = std::make_unique<UserPresetManager> ("DuskVerb");

    presetBox_.setJustificationType (juce::Justification::centred);
    presetBox_.setTextWhenNothingSelected ("Preset");
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
    presetBox_.setTooltip ("Factory and user presets. Use < / > to step through, "
                           "Save to capture the current settings.");
    // Replace the default flat dropdown with a categorized PopupMenu — each
    // category (Plates, Halls, Ambient, etc.) becomes a nested submenu so
    // the top-level menu fits in a small popup instead of one giant scroll.
    presetBox_.menuBuilder = [this] { return buildPresetMenu(); };
    addAndMakeVisible (presetBox_);
    refreshPresetList();

    // Restore preset selection from saved state.
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
                        presetBox_.setSelectedId (static_cast<int> (1001 + i),
                                                  juce::dontSendNotification);
                        break;
                    }
                }
            }
        }
        updateDeleteButtonVisibility();
    }

    prevPresetButton_.setButtonText ("<");
    prevPresetButton_.setTooltip ("Previous preset");
    prevPresetButton_.onClick = [this] { stepFactoryPreset (-1); };
    addAndMakeVisible (prevPresetButton_);

    nextPresetButton_.setButtonText (">");
    nextPresetButton_.setTooltip ("Next preset");
    nextPresetButton_.onClick = [this] { stepFactoryPreset (+1); };
    addAndMakeVisible (nextPresetButton_);

    savePresetButton_.setButtonText ("Save");
    savePresetButton_.setTooltip ("Save the current settings as a user preset");
    savePresetButton_.onClick = [this] { saveUserPreset(); };
    addAndMakeVisible (savePresetButton_);

    deletePresetButton_.setButtonText ("Del");
    deletePresetButton_.setTooltip ("Delete the selected user preset (factory presets cannot be deleted)");
    deletePresetButton_.onClick = [this]
    {
        int id = presetBox_.getSelectedId();
        if (id < 1001) return;
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
                juce::ModalCallbackFunction::create ([safeThis, name] (int result)
                {
                    if (result == 1 && safeThis != nullptr)
                    {
                        safeThis->deleteUserPreset (name);
                        safeThis->updateDeleteButtonVisibility();
                    }
                }));
        }
    };
    addAndMakeVisible (deletePresetButton_);
    deletePresetButton_.setVisible (false);

    // Pre-delay sync — combo + small "SYNC" caption above so it isn't visually orphaned
    // next to the labelled PRE-DELAY knob.
    predelaySyncLabel_.setText ("SYNC", juce::dontSendNotification);
    predelaySyncLabel_.setJustificationType (juce::Justification::centred);
    predelaySyncLabel_.setInterceptsMouseClicks (false, false);
    predelaySyncLabel_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    predelaySyncLabel_.setColour (juce::Label::textColourId,
                                   juce::Colour (DuskVerbLookAndFeel::kLabelText));
    addAndMakeVisible (predelaySyncLabel_);

    predelaySyncBox_.addItemList ({ "Free", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }, 1);
    predelaySyncBox_.setJustificationType (juce::Justification::centred);
    predelaySyncBox_.setTooltip ("Tempo-sync pre-delay to the host's BPM. "
                                 "When set to a note value, overrides the PRE-DELAY knob.");
    addAndMakeVisible (predelaySyncBox_);
    predelaySyncAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.parameters, "predelay_sync", predelaySyncBox_);

    // Freeze
    freezeButton_.setButtonText ("FREEZE");
    freezeButton_.setName ("freeze");
    freezeButton_.setClickingTogglesState (true);
    freezeButton_.setTooltip ("Freeze the reverb tail — input is muted and the existing tail "
                              "loops indefinitely. Useful for ambient pads and risers.");
    addAndMakeVisible (freezeButton_);
    freezeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.parameters, "freeze", freezeButton_);

    // Gate (NonLinear-engine only — visible always, but only changes the
    // sound on NonLinear presets. Default ON for backward-compat).
    gateButton_.setButtonText ("GATE");
    gateButton_.setName ("gate_enabled");
    gateButton_.setClickingTogglesState (true);
    gateButton_.setTooltip ("Gate (NonLinear engine only): when ON the FIR envelope shapes "
                            "the per-tap gains (the gated/RMX16 sound). When OFF the envelope "
                            "is bypassed and you hear the underlying 256-tap dense FIR wash "
                            "with no gating character. No effect on other engines.");
    addAndMakeVisible (gateButton_);
    gateAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.parameters, "gate_enabled", gateButton_);

    // Bus mode
    busModeButton_.setButtonText ("BUS");
    busModeButton_.setName ("bus_mode");
    busModeButton_.setClickingTogglesState (true);
    busModeButton_.setTooltip ("Bus mode — outputs 100% wet signal regardless of DRY/WET. "
                               "Use on a send/return aux with the DRY/WET knob disabled.");
    addAndMakeVisible (busModeButton_);
    busModeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.parameters, "bus_mode", busModeButton_);

    // Live output tail meter — sits between the header dropdowns and the
    // group panels. Pre-fill with -100 dB so the curve starts flat at the
    // bottom rather than spiking from the default 0 dB on the first frame.
    addAndMakeVisible (tailMeter_);
    for (int i = 0; i < 200; ++i)
        tailMeter_.pushFrame (-100.0f);

    // Meters
    inputMeter_.setStereoMode (true);
    inputMeter_.setRefreshRate (15.0f);
    addAndMakeVisible (inputMeter_);

    outputMeter_.setStereoMode (true);
    outputMeter_.setRefreshRate (15.0f);
    addAndMakeVisible (outputMeter_);

    // uiVersion 4: pre-v4 builds shipped layouts at 1400×640 (v0), 1240×540
    // (v1), 1240×470 (v2), 1240×510 (v3). ScalableEditorHelper now resets
    // the persisted size to the new defaults whenever the stored uiVersion
    // is older than the value passed here — no more maintaining an explicit
    // list of prior defaults inside this file. Bump this when a future
    // layout pass should auto-reset existing sessions.
    scaler_.initialize (this, &p, kBaseWidth, kBaseHeight,
                        static_cast<int> (kBaseWidth * 0.7f),
                        static_cast<int> (kBaseHeight * 0.7f),
                        kBaseWidth * 2, kBaseHeight * 2,
                        true, /* uiVersion */ 4);

    setSize (scaler_.getStoredWidth(), scaler_.getStoredHeight());
    startTimerHz (15);

    // Re-apply engine accent NOW that every component is constructed and
    // addAndMakeVisible'd. The early call inside the algorithmBox_ setup
    // block runs before gateButton_ is added, so its setVisible(false)
    // there gets overwritten by the later addAndMakeVisible (which forces
    // visible). Without this second call, sessions saved with a non-RMX16
    // algorithm reopen with the GATE button showing.
    const auto currentEngine = getAlgorithmConfig (
        static_cast<int> (p.parameters.getRawParameterValue ("algorithm")->load())).engine;
    applyEngineAccent (currentEngine);
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
        // If a per-engine value override is installed (NonLinear gate
        // re-mapping), use it; otherwise fall back to the standard
        // suffix-based formatter. The override receives the raw APVTS
        // value and returns the displayed string.
        const juce::String text = k.valueOverride
            ? k.valueOverride (k.slider.getValue())
            : formatValue (k.slider, k.valueLabel.getName());
        k.valueLabel.setText (text, juce::dontSendNotification);
        // Re-assert accent every tick. The value-editor overlay's focus loss
        // was flipping labels to white and not restoring (Logic + JUCE Label
        // interaction). Cheap to re-set the colour each tick — it's a no-op
        // when nothing has changed.
        if (k.valueLabel.findColour (juce::Label::textColourId) != k.currentAccent)
            k.valueLabel.setColour (juce::Label::textColourId, k.currentAccent);
    };

    // decay_ is a HeroDecay — self-updates via slider.onValueChange → repaint.
    update (preDelay_);  update (size_);      update (modDepth_);  update (modRate_);
    update (damping_);   update (bassMult_);  update (midMult_);   update (crossover_);
    update (highCrossover_); update (saturation_); update (diffusion_);
    update (erLevel_);   update (erSize_);    update (mix_);       update (loCut_);
    update (hiCut_);     update (monoBelow_); update (width_);     update (gainTrim_);
    update (duck_);      update (tone_);      update (character_);

    // Manual algorithm-dropdown sync (no ComboBoxAttachment): reflect external
    // changes to the choice param (preset load, automation, host) into the box
    // selection + engine accent.
    {
        const int algoIdx = static_cast<int> (processorRef.parameters.getRawParameterValue ("algorithm")->load());
        if (algoIdx >= 0 && algoIdx < getNumAlgorithms())
        {
            // Hidden engine selected externally (saved state / automation):
            // add it on the fly so the box never shows blank.
            if (algorithmBox_.indexOfItemId (algoIdx + 1) < 0)
                algorithmBox_.addItem (getAlgorithmConfig (algoIdx).name, algoIdx + 1);
            if (algorithmBox_.getSelectedId() != algoIdx + 1)
                algorithmBox_.setSelectedId (algoIdx + 1, juce::dontSendNotification);

            const auto e = getAlgorithmConfig (algoIdx).engine;
            if (e != currentEngine_)
            {
                engineGlyph_.setEngine (e);
                applyEngineAccent (e);
            }
        }
    }

    inputMeter_.setStereoLevels  (processorRef.getInputLevelL(),  processorRef.getInputLevelR());
    outputMeter_.setStereoLevels (processorRef.getOutputLevelL(), processorRef.getOutputLevelR());

    tailMeter_.pushFrame (juce::jmax (processorRef.getOutputLevelL(),
                                       processorRef.getOutputLevelR()));
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

    // Brand wordmark — large, bold, slightly compressed for visual character.
    // The plugin name is the brand; no need to explain "Algorithmic Reverb"
    // (external reference, Crystalline, Blackhole all skip the explanatory subtitle).
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kText));
    juce::Font titleFont (juce::FontOptions (32.0f * sf, juce::Font::bold));
    titleFont.setHorizontalScale (0.95f);
    g.setFont (titleFont);
    g.drawText ("DUSKVERB", 0, scaler_.scaled (4), getWidth(), scaler_.scaled (40),
                juce::Justification::centred);

    g.setColour (juce::Colour (DuskVerbLookAndFeel::kBorder));
    int dividerY = scaler_.scaled (46);
    g.drawHorizontalLine (dividerY,
                          static_cast<float> (contentX),
                          static_cast<float> (contentX + contentW));

    int topY       = scaler_.scaled (128);
    int gap        = scaler_.scaled (8);   // MUST match resized()
    int titleBandH = scaler_.scaled (16);

    int availH  = getHeight() - topY - gap - margin;
    // Three rows: top (INPUT/TIME/DAMPING), mid (MOD/ER/FILTER/OUTPUT), and a
    // compact MACRO row (Tone/Character/Duck) at the bottom.
    int macroRowH = juce::roundToInt (availH * 0.22f);
    int knobAreaH = availH - macroRowH - gap;
    int topRowH = juce::roundToInt ((knobAreaH - gap) * 0.5f);
    int bottomH = (knobAreaH - gap) - topRowH;
    int bottomY = topY + topRowH + gap;
    int macroY  = bottomY + bottomH + gap;

    // Group panel widths MUST match resized() exactly — knobs are placed in
    // resized() using these same percentages.
    // INPUT 26 / TIME 28 / DAMPING 46 — INPUT narrowed (only 2 knobs + SYNC
    // combo) so DAMPING's 5-knob row gets its full breathing room.
    int topUsable  = contentW - gap * 2;
    int inputW     = static_cast<int> (topUsable * 0.26f);
    int timeW      = static_cast<int> (topUsable * 0.28f);
    int characterW = topUsable - inputW - timeW;

    int inputX     = contentX;
    int timeX      = inputX + inputW + gap;
    int characterX = timeX + timeW + gap;

    drawGroupBox (g, { inputX,     topY, inputW,     topRowH }, "INPUT",     titleBandH);
    drawGroupBox (g, { timeX,      topY, timeW,      topRowH }, "TIME",      titleBandH);
    drawGroupBox (g, { characterX, topY, characterW, topRowH }, "DAMPING",   titleBandH);

    // ER widened to 26% (was 20%) for 3 knobs (added DIFFUSION).
    // MOD trimmed to 18% (only 2 knobs); FILTER stays at 28%; OUTPUT keeps
    // the largest share for its 3 knobs + bus button.
    int bottomUsable = contentW - gap * 3;
    // Section widths proportional to knob count (MOD 2 / ER 3 / FILTER 3 /
    // OUTPUT 4 = 12) so the per-knob column width — and therefore the knob
    // padding — is uniform across the whole bottom row.
    int modW    = static_cast<int> (bottomUsable * (2.0f / 12.0f));
    int erW     = static_cast<int> (bottomUsable * (3.0f / 12.0f));
    int eqW     = static_cast<int> (bottomUsable * (3.0f / 12.0f));
    int outputW = bottomUsable - modW - erW - eqW;   // 4/12 — uniform per-knob width

    int modX    = contentX;
    int erX     = modX + modW + gap;
    int eqX     = erX + erW + gap;
    int outputX = eqX + eqW + gap;

    // MODULATION group is re-titled "GATE" on the NonLinear engine since
    // the knobs in there are mapped to ATTACK/RELEASE/DENSITY (gate
    // controls), not modulation. Other engines keep "MODULATION".
    const char* modulationTitle = (currentEngine_ == EngineType::NonLinear)
                                ? "GATE" : "MODULATION";
    drawGroupBox (g, { modX,    bottomY, modW,    bottomH }, modulationTitle,     titleBandH);
    drawGroupBox (g, { erX,     bottomY, erW,     bottomH }, "EARLY REFLECTIONS", titleBandH);
    drawGroupBox (g, { eqX,     bottomY, eqW,     bottomH }, "FILTER",            titleBandH);
    drawGroupBox (g, { outputX, bottomY, outputW, bottomH }, "OUTPUT",            titleBandH);
    drawGroupBox (g, { contentX, macroY, contentW, macroRowH }, "MACRO",          titleBandH);

    // IN / OUT labels — bigger and brighter, sitting just above the meters
    // and clear of the tail-meter ribbon above. Wider than the meter column
    // so the text isn't squashed into the 22 px LED width.
    const int labelY     = topY - scaler_.scaled (16);
    const int labelH_in  = scaler_.scaled (14);
    const int labelW_in  = scaler_.scaled (40);
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kLabelText));
    g.setFont (juce::FontOptions (12.0f * sf, juce::Font::bold));
    g.drawText ("IN",  margin - (labelW_in - meterW) / 2,
                labelY, labelW_in, labelH_in, juce::Justification::centred);
    g.drawText ("OUT", getWidth() - margin - meterW - (labelW_in - meterW) / 2,
                labelY, labelW_in, labelH_in, juce::Justification::centred);
}

namespace
{
    void placeKnob (KnobWithLabel& k, juce::Rectangle<int> area, int knobSize, float scaleFactor)
    {
        // Logic-style stack: NAME (small dim) → VALUE (large accent) → KNOB.
        // Putting the value ABOVE the knob makes the readout the dominant
        // element of each control column; the rotary becomes a tactile
        // affordance underneath.
        const int nameH  = juce::roundToInt (12.0f * scaleFactor);
        const int valueH = juce::roundToInt (18.0f * scaleFactor);
        const int gap    = juce::roundToInt (2.0f  * scaleFactor);
        const int totalH = nameH + gap + valueH + gap + knobSize;

        const int yPad = (area.getHeight() - totalH) / 2;
        auto col = area;
        if (yPad > 0) col.removeFromTop (yPad);

        k.nameLabel.setBounds (col.removeFromTop (nameH));
        col.removeFromTop (gap);
        k.valueLabel.setVisible (true);
        k.valueLabel.setBounds (col.removeFromTop (valueH));
        col.removeFromTop (gap);
        auto knobArea = col.removeFromTop (knobSize);
        k.slider.setBounds (knobArea.withSizeKeepingCentre (knobSize, knobSize));
    }

    void layoutKnobsInGroup (juce::Rectangle<int> groupBounds, int topPad,
                             std::vector<std::pair<KnobWithLabel*, int>> knobs,
                             float scaleFactor)
    {
        auto area = groupBounds.reduced (4, 0);
        area.removeFromTop (topPad);
        int colW = area.getWidth() / static_cast<int> (knobs.size());
        for (auto& [knob, knobSize] : knobs)
        {
            auto col = area.removeFromLeft (colW);
            placeKnob (*knob, col, knobSize, scaleFactor);
        }
    }
}

void DuskVerbEditor::resized()
{
    auto sf = scaler_.getScaleFactor();

    int margin   = scaler_.scaled (10);
    int meterW   = scaler_.scaled (22);
    int meterGap = scaler_.scaled (6);
    int contentX = margin + meterW + meterGap;
    int contentW = getWidth() - contentX - margin - meterW - meterGap;

    // Header strip — single horizontally-centred cluster:
    //   [glyph] [algorithm] [<] [preset] [>] [Save] [Del]
    // Glyph sits to the LEFT of the algorithm dropdown so the user sees the
    // engine topology icon right next to its name. Total cluster width grows
    // by `btnW + 4` when Delete is visible; recompute every resize so the
    // row stays centred in both states without jumping.
    int headerY = scaler_.scaled (52);
    int headerH = scaler_.scaled (24);

    int glyphW  = scaler_.scaled (24);
    int algoW   = scaler_.scaled (180);
    int navBtnW = scaler_.scaled (24);
    int presetW = scaler_.scaled (210);
    int btnW    = scaler_.scaled (52);
    int gapGl   = scaler_.scaled (4);
    int gapAlgo = scaler_.scaled (10);
    int gapTiny = 2;
    int gapSave = scaler_.scaled (10);

    int clusterW = glyphW + gapGl + algoW + gapAlgo + navBtnW + gapTiny
                 + presetW + gapTiny + navBtnW + gapSave + btnW;
    if (deletePresetButton_.isVisible())
        clusterW += 4 + btnW;

    int x = contentX + (contentW - clusterW) / 2;

    engineGlyph_       .setBounds (x, headerY, glyphW, headerH);  x += glyphW + gapGl;
    algorithmBox_      .setBounds (x, headerY, algoW, headerH);   x += algoW + gapAlgo;
    prevPresetButton_  .setBounds (x, headerY, navBtnW, headerH); x += navBtnW + gapTiny;
    presetBox_         .setBounds (x, headerY, presetW, headerH); x += presetW + gapTiny;
    nextPresetButton_  .setBounds (x, headerY, navBtnW, headerH); x += navBtnW + gapSave;
    savePresetButton_  .setBounds (x, headerY, btnW,    headerH); x += btnW + 4;
    deletePresetButton_.setBounds (x, headerY, btnW,    headerH);

    // Tail meter — slim ribbon under the header dropdowns. Spans the full
    // width of the group panels below it (no width cap) so it reads as the
    // visual roof of the layout rather than a centred badge.
    {
        const int meterY = scaler_.scaled (80);
        const int meterH = scaler_.scaled (28);
        tailMeter_.setBounds (contentX, meterY, contentW, meterH);
    }

    // Group rows start below the tail meter (ends y=108) plus the IN / OUT
    // label band (y=112-124) plus a small gap.
    int topY       = scaler_.scaled (128);
    int gap        = scaler_.scaled (8);
    int titleBandH = scaler_.scaled (16);
    int topPad     = titleBandH + scaler_.scaled (2);

    int availH  = getHeight() - topY - gap - margin;
    // Three rows: top (INPUT/TIME/DAMPING), mid (MOD/ER/FILTER/OUTPUT), and a
    // compact MACRO row (Tone/Character/Duck) at the bottom.
    int macroRowH = juce::roundToInt (availH * 0.22f);
    int knobAreaH = availH - macroRowH - gap;
    int topRowH = juce::roundToInt ((knobAreaH - gap) * 0.5f);
    int bottomH = (knobAreaH - gap) - topRowH;
    int bottomY = topY + topRowH + gap;
    int macroY  = bottomY + bottomH + gap;

    // Meters span the full content area.
    inputMeter_.setBounds  (margin,                       topY, meterW, availH + gap);
    outputMeter_.setBounds (getWidth() - margin - meterW, topY, meterW, availH + gap);

    // 26 / 28 / 46 split — INPUT narrowed (only 2 knobs + SYNC combo) so
    // DAMPING's 5-knob row gets its full breathing room. MUST stay in
    // lock-step with paint().
    int topUsable  = contentW - gap * 2;
    int inputW     = static_cast<int> (topUsable * 0.26f);
    int timeW      = static_cast<int> (topUsable * 0.28f);
    int characterW = topUsable - inputW - timeW;

    int inputX     = contentX;
    int timeX      = inputX + inputW + gap;
    int characterX = timeX + timeW + gap;

    // Single secondary tier — every knob except the DECAY hero is knobMed
    // (incl. the OUTPUT row, now widened to fit 4 at full size). DECAY's
    // concentric-ring rendering uses heroSize (computed below) which dominates
    // at ~165 px diameter. Pass 4 bumped knobMed 76 → 84 to soak up the
    // remaining column yPad in non-hero panels.
    int knobMed = juce::roundToInt (84.0f * sf);

    // INPUT: PRE-DELAY knob | SATURATION knob | SYNC label+combo.
    // The two knobs sit adjacent so the row reads as a coherent pair, and
    // the SYNC stack lives at the right edge where its label/combo have
    // natural breathing room (previously sandwiched between two knobs,
    // which crowded the combo box width).
    {
        auto inputArea = juce::Rectangle<int> (inputX, topY, inputW, topRowH).reduced (4, 0);
        inputArea.removeFromTop (topPad);

        int colW = inputArea.getWidth() / 3;
        placeKnob (preDelay_,  inputArea.removeFromLeft (colW), knobMed, sf);
        placeKnob (saturation_, inputArea.removeFromLeft (colW), knobMed, sf);

        // Right column: SYNC label + combo, vertically centred.
        auto syncCol = inputArea.reduced (4, 0);
        const int labelH = scaler_.scaled (14);
        const int comboH = scaler_.scaled (24);
        const int stackH = labelH + scaler_.scaled (4) + comboH;
        const int yPad = std::max ((syncCol.getHeight() - stackH) / 2, 0);
        syncCol.removeFromTop (yPad);
        predelaySyncLabel_.setBounds (syncCol.removeFromTop (labelH));
        syncCol.removeFromTop (scaler_.scaled (4));
        predelaySyncBox_.setBounds (syncCol.removeFromTop (comboH));
    }

    // TIME: decay + size knobs + bottom strip split into [DECAY LOCK | FREEZE].
    // LOCK sits under DECAY so the visual relationship is unmistakable.
    {
        auto timeArea = juce::Rectangle<int> (timeX, topY, timeW, topRowH).reduced (4, 0);
        timeArea.removeFromTop (topPad);
        // FREEZE strip 22 px — matches BUS and GATE strip heights so all
        // three toggle buttons read as the same visual element.
        auto knobArea = timeArea.removeFromTop (timeArea.getHeight() - scaler_.scaled (22));

        // Hero DECAY takes the LEFT 70% of the row, SIZE takes the right 30%.
        // The hero is the visual centrepiece of the entire plugin — its
        // concentric-ring rendering doesn't fit the standard placeKnob
        // template, so it's positioned manually and centered in its column.
        const int heroW = juce::roundToInt (knobArea.getWidth() * 0.70f);
        auto heroArea  = knobArea.removeFromLeft (heroW);
        const int heroSize = std::min (heroArea.getWidth(), heroArea.getHeight());
        decay_.setBounds (heroArea.withSizeKeepingCentre (heroSize, heroSize));

        // SIZE renders at the secondary tier (knobMed) so DECAY's
        // concentric-ring hero is the unambiguously largest knob on the UI.
        // Previously SIZE shared knobBig with MIX; visually it competed with
        // the hero for top-row dominance.
        placeKnob (size_, knobArea, knobMed, sf);

        // FREEZE spans the full bottom strip of the TIME group. (8, 2) inset
        // matches the BUS and GATE buttons so all three toggles render at the
        // same visual height.
        freezeButton_.setBounds (timeArea.reduced (8, 2));
    }

    // DAMPING: full 3-band (BASS/MID/TREBLE multipliers + LOW/HIGH crossovers).
    // Five knobs ordered low→high so the damping curve reads left-to-right.
    // DIFFUSION moved to the EARLY REFLECTIONS group below — both are early-
    // density character — to keep DAMPING uncrowded with proper label space.
    layoutKnobsInGroup ({ characterX, topY, characterW, topRowH }, topPad,
                        { { &bassMult_,      knobMed },
                          { &midMult_,       knobMed },
                          { &damping_,       knobMed },
                          { &crossover_,     knobMed },
                          { &highCrossover_, knobMed } }, sf);

    // ER widened to 26% (was 20%) for 3 knobs (added DIFFUSION).
    // MOD trimmed to 18% (only 2 knobs); FILTER stays at 28%; OUTPUT keeps
    // the largest share for its 3 knobs + bus button.
    int bottomUsable = contentW - gap * 3;
    // Section widths proportional to knob count (MOD 2 / ER 3 / FILTER 3 /
    // OUTPUT 4 = 12) so the per-knob column width — and therefore the knob
    // padding — is uniform across the whole bottom row.
    int modW    = static_cast<int> (bottomUsable * (2.0f / 12.0f));
    int erW     = static_cast<int> (bottomUsable * (3.0f / 12.0f));
    int eqW     = static_cast<int> (bottomUsable * (3.0f / 12.0f));
    int outputW = bottomUsable - modW - erW - eqW;   // 4/12 — uniform per-knob width

    int modX    = contentX;
    int erX     = modX + modW + gap;
    int eqX     = erX + erW + gap;
    int outputX = eqX + eqW + gap;

    {
        // Reserve a bottom strip in the MODULATION/GATE group for the GATE
        // toggle button ONLY when the GATE button is visible (NonLinear
        // engine). On every other engine the GATE button is hidden and the
        // strip carve wasted vertical space. Now the knob area takes the
        // full panel height when GATE is hidden.
        juce::Rectangle<int> modPanel { modX, bottomY, modW, bottomH };
        if (gateButton_.isVisible())
        {
            const int gateButtonH = scaler_.scaled (22);
            auto modKnobArea = modPanel.removeFromTop (modPanel.getHeight() - gateButtonH);
            layoutKnobsInGroup (modKnobArea, topPad,
                                { { &modDepth_, knobMed }, { &modRate_, knobMed } }, sf);
            gateButton_.setBounds (modPanel.reduced (8, 2));
        }
        else
        {
            layoutKnobsInGroup (modPanel, topPad,
                                { { &modDepth_, knobMed }, { &modRate_, knobMed } }, sf);
        }
    }

    layoutKnobsInGroup ({ erX, bottomY, erW, bottomH }, topPad,
                        { { &erLevel_, knobMed }, { &erSize_, knobMed }, { &diffusion_, knobMed } }, sf);

    layoutKnobsInGroup ({ eqX, bottomY, eqW, bottomH }, topPad,
                        { { &loCut_, knobMed }, { &hiCut_, knobMed }, { &monoBelow_, knobMed } }, sf);

    // OUTPUT: 3 knobs across top + bottom strip split into [MIX LOCK | BUS].
    // LOCK sits under MIX (left third) so the visual relationship is clear.
    {
        auto outArea = juce::Rectangle<int> (outputX, bottomY, outputW, bottomH).reduced (4, 0);
        outArea.removeFromTop (topPad);
        int knobAreaH = outArea.getHeight() - scaler_.scaled (22);
        auto knobArea = outArea.removeFromTop (knobAreaH);
        // 3 even columns: MIX / WIDTH / TRIM (DUCK moved to the MACRO row).
        const int outCol = knobArea.getWidth() / 3;
        placeKnob (mix_,      knobArea.removeFromLeft (outCol), knobMed, sf);
        placeKnob (width_,    knobArea.removeFromLeft (outCol), knobMed, sf);
        placeKnob (gainTrim_, knobArea,                         knobMed, sf);
        busModeButton_.setBounds (outArea.reduced (8, 2));
    }

    // MACRO row (full content width): TONE / CHARACTER / DUCK — global morphers
    // that layer on top of every preset.
    layoutKnobsInGroup ({ contentX, macroY, contentW, macroRowH }, topPad,
                        { { &tone_, knobMed }, { &character_, knobMed }, { &duck_, knobMed } }, sf);

    titleClickArea_ = { 0, 0, getWidth(), scaler_.scaled (52) };

    if (supportersOverlay_)
        supportersOverlay_->setBounds (getLocalBounds());
}

void DuskVerbEditor::loadPreset (int index)
{
    const auto& presets = getFactoryPresets();
    if (index < 0 || index >= static_cast<int> (presets.size()))
        return;

    processorRef.applyFactoryPreset (presets[static_cast<size_t> (index)]);
    processorRef.parameters.state.setProperty ("presetName",
                                                presets[static_cast<size_t> (index)].name,
                                                nullptr);
}

void DuskVerbEditor::stepFactoryPreset (int delta)
{
    const auto& presets = getFactoryPresets();
    if (presets.empty()) return;

    int total = static_cast<int> (presets.size());
    int currentId = presetBox_.getSelectedId();
    int currentIdx = (currentId >= 2 && currentId < 2 + total) ? currentId - 2 : 0;
    int next = (currentIdx + delta + total) % total;
    presetBox_.setSelectedId (next + 2, juce::sendNotificationSync);
}

void DuskVerbEditor::refreshPresetList()
{
    presetBox_.clear (juce::dontSendNotification);
    const auto& presets = getFactoryPresets();

    // Items are still added flat so ComboBox knows the ID-to-text mapping for
    // setSelectedId() and step navigation. The categorized presentation lives
    // in buildPresetMenu() / CategoryComboBox::showPopup() — section headings
    // are NOT added here because the ComboBox's auto-built popup is bypassed.
    for (size_t i = 0; i < presets.size(); ++i)
        presetBox_.addItem (presets[i].name, static_cast<int> (i) + 2);

    if (userPresetManager_)
    {
        auto userPresets = userPresetManager_->loadUserPresets();
        for (size_t i = 0; i < userPresets.size(); ++i)
            presetBox_.addItem (userPresets[i].name, static_cast<int> (1001 + i));
    }
}

juce::PopupMenu DuskVerbEditor::buildPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lnf_);   // render the popup + submenus with our dark dropdown L&F
    const auto& presets = getFactoryPresets();
    const int currentId = presetBox_.getSelectedId();

    // Group presets contiguously by category and emit one submenu per category.
    // Categories appear in the order they first show up in the source array.
    juce::String currentCategory;
    juce::PopupMenu categorySub;
    auto flushCategory = [&]
    {
        if (currentCategory.isNotEmpty())
            menu.addSubMenu (currentCategory, categorySub);
        categorySub.clear();
    };
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const juce::String cat = presets[i].category;
        if (cat != currentCategory)
        {
            flushCategory();
            currentCategory = cat;
        }
        const int id = static_cast<int> (i) + 2;
        categorySub.addItem (id, presets[i].name, /*enabled*/ true,
                             /*ticked*/ id == currentId);
    }
    flushCategory();

    if (userPresetManager_)
    {
        auto userPresets = userPresetManager_->loadUserPresets();
        if (! userPresets.empty())
        {
            juce::PopupMenu userSub;
            for (size_t i = 0; i < userPresets.size(); ++i)
            {
                const int id = static_cast<int> (1001 + i);
                userSub.addItem (id, userPresets[i].name, true, id == currentId);
            }
            menu.addSubMenu ("User", userSub);
        }
    }
    return menu;
}

// CategoryComboBox::showPopup — overrides the default flat popup with a
// categorized one (categories as nested submenus). On selection, route the
// chosen ID through setSelectedId() so the existing onChange callback fires
// exactly as if the user had picked from a flat list.
//
// hidePopup() in the async callback is ESSENTIAL: ComboBox tracks an
// internal "menu is showing" flag (set when showPopup runs, cleared by
// hidePopup). JUCE's default ComboBox::showPopup uses a static
// popupMenuFinishedCallback that calls hidePopup() on dismiss. Without
// that call, the second click after a selection finds the flag still set
// and silently no-ops — the dropdown works once per editor lifetime,
// then stops responding until the editor is reopened.
void DuskVerbEditor::CategoryComboBox::showPopup()
{
    if (! menuBuilder)
    {
        juce::ComboBox::showPopup();
        return;
    }
    auto menu = menuBuilder();
    juce::Component::SafePointer<CategoryComboBox> safe (this);
    menu.showMenuAsync (
        juce::PopupMenu::Options()
            .withTargetComponent (this)
            .withMinimumWidth (getWidth()),
        [safe] (int result)
        {
            if (safe != nullptr)
            {
                safe->hidePopup();
                if (result > 0)
                    safe->setSelectedId (result, juce::sendNotification);
            }
        });
}

void DuskVerbEditor::saveUserPreset()
{
    juce::Component::SafePointer<DuskVerbEditor> safeThis (this);
    auto* aw = new juce::AlertWindow ("Save Preset", "Name:",
                                       juce::MessageBoxIconType::QuestionIcon);
    aw->addTextEditor ("name", "");
    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // Center over the plugin window. JUCE's default places the AlertWindow
    // at the centre of the user's display, which is jarring inside a DAW —
    // it can land on a different monitor than the plugin editor. Position
    // BEFORE enterModalState so the very first paint already lands at the
    // right spot (no perceptible jump). centreAroundComponent works in
    // screen coordinates derived from `this`, so it follows the host
    // window even on multi-monitor setups.
    aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, aw] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owner (aw);
            if (result != 1 || safeThis == nullptr) return;
            auto name = owner->getTextEditorContents ("name").trim();
            if (name.isEmpty()) return;
            auto& proc = safeThis->processorRef;
            safeThis->userPresetManager_->saveUserPreset (name, proc.parameters.copyState());
            proc.parameters.state.setProperty ("presetName", name, nullptr);
            safeThis->refreshPresetList();
        }), true);
}

void DuskVerbEditor::loadUserPreset (const juce::String& name)
{
    if (! userPresetManager_) return;
    auto tree = userPresetManager_->loadUserPreset (name);
    if (! (tree.isValid() && tree.hasType (processorRef.parameters.state.getType())))
        return;

    processorRef.parameters.replaceState (tree);

    processorRef.parameters.state.setProperty ("presetName", name, nullptr);
}

void DuskVerbEditor::deleteUserPreset (const juce::String& name)
{
    if (userPresetManager_)
    {
        userPresetManager_->deleteUserPreset (name);
        refreshPresetList();
        presetBox_.setSelectedId (0, juce::dontSendNotification);
    }
}

void DuskVerbEditor::updateDeleteButtonVisibility()
{
    const bool wasVisible = deletePresetButton_.isVisible();
    const bool nowVisible = presetBox_.getSelectedId() >= 1001;
    deletePresetButton_.setVisible (nowVisible);
    // Re-centre the header cluster when Delete appears/disappears so the row
    // doesn't jump asymmetrically (cluster width depends on Delete visibility).
    if (wasVisible != nowVisible)
        resized();
}

void DuskVerbEditor::mouseDown (const juce::MouseEvent& e)
{
    if (titleClickArea_.contains (e.getPosition()))
    {
        if (supportersOverlay_) hideSupportersPanel();
        else                    showSupportersPanel();
    }
}

void DuskVerbEditor::showSupportersPanel()
{
    if (supportersOverlay_) return;
    supportersOverlay_ = std::make_unique<SupportersOverlay> ("DuskVerb", JucePlugin_VersionString);
    // Wire the overlay's mouseDown-to-dismiss callback. SupportersOverlay's
    // built-in mouseDown checks `onDismiss` and fires it; without the wiring
    // the overlay catches all clicks (it sets interceptsMouseClicks(true))
    // and never closes — the user is stuck.
    juce::Component::SafePointer<DuskVerbEditor> safeThis (this);
    supportersOverlay_->onDismiss = [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->hideSupportersPanel();
    };
    addAndMakeVisible (*supportersOverlay_);
    supportersOverlay_->setBounds (getLocalBounds());
}

void DuskVerbEditor::hideSupportersPanel()
{
    supportersOverlay_.reset();
}

void DuskVerbEditor::applyEngineAccent (EngineType engine)
{
    const juce::Colour accent = getEngineAccent (engine);

    // 1) LookAndFeel — drives the rotary arc colour for every knob and the
    //    "active" pill colour for FREEZE / BUS toggles.
    lnf_.setCurrentAccent (accent);

    // 2) Per-knob value labels — they hold a hard-coded text colour, so we
    //    have to push the accent into each one explicitly. The HeroDecay
    //    paints itself directly from the LookAndFeel so it's already covered
    //    by step 1 (just needs a repaint).
    // CRITICAL: any new knob added to the editor MUST be appended here, or
    // its value label will stay frozen at the orange `kAccent` from init()
    // while every other label tracks the engine's current accent.
    for (auto* k : { &preDelay_, &size_, &modDepth_, &modRate_,
                     &damping_,  &bassMult_,  &midMult_,       &crossover_,
                     &highCrossover_, &saturation_, &diffusion_,
                     &erLevel_, &erSize_, &mix_, &loCut_, &hiCut_,
                     &monoBelow_, &width_, &gainTrim_, &duck_, &tone_, &character_ })
        k->setAccent (accent);

    // 3) Components that paint their own accent regions.
    decay_      .repaint();
    engineGlyph_.repaint();
    tailMeter_  .repaint();

    // 4) Per-engine knob name relabel — some engines hijack the universal
    //    knobs under engine-specific semantics (the underlying APVTS values
    //    don't change, only the display label + tooltip shift). The default
    //    branch restores all the original name strings so flipping back to
    //    a "standard" engine fully resets the UI.
    currentEngine_ = engine;   // remembered for paint()'s group-title switch
    const bool isSpring    = (engine == EngineType::Spring);
    const bool isNonLinear = (engine == EngineType::NonLinear);
    const bool isShimmer   = (engine == EngineType::Shimmer);

    // mod_depth hijacked by Spring (SPRING LEN), Shimmer (PITCH), NonLinear (ATTACK)
    modDepth_.nameLabel.setText (isSpring    ? "SPRING LEN"
                                : isShimmer   ? "PITCH"
                                : isNonLinear ? "ATTACK"
                                              : "DEPTH",
                                 juce::dontSendNotification);
    modRate_ .nameLabel.setText (isSpring    ? "DRIP"
                                : isShimmer   ? "FEEDBACK"
                                : isNonLinear ? "RELEASE"
                                              : "RATE",
                                 juce::dontSendNotification);
    modDepth_.slider.setTooltip (isSpring    ? "Spring Length: read-position LFO depth (subtle wobble that gives the tank its 'drip' character)"
                                : isShimmer   ? "Pitch: in-loop pitch interval (0 = unity, 50% = +12 semitones / +1 octave, 100% = +24 / +2 octaves)"
                                : isNonLinear ? "Attack: gate open time (1 - 50 ms). 1-3 ms gives the classic Phil Collins instant-snap; longer for a softer breathing feel."
                                              : "Modulation Depth");
    modRate_ .slider.setTooltip (isSpring    ? "Drip: spring-tank LFO rate (Hz)"
                                : isShimmer   ? "Feedback: cascade strength (0 = single pitched pass, 95% = long cascading octaves a la Eno/Lanois). Higher feedback builds more shimmer at the cost of slower decay."
                                : isNonLinear ? "Release: gate close time (5 - 2000 ms). Short values (5-50 ms) = the classic 80s gated-snare cliff; longer values let the hall tail fade naturally after the gate."
                                              : "Modulation Rate (Hz)");

    // diffusion hijacked by Spring (CHIRP) and NonLinear (HOLD); Shimmer
    // ignores diffusion so we keep its label generic
    diffusion_.nameLabel.setText (isNonLinear ? "HOLD"
                                 : isSpring   ? "CHIRP"
                                              : "DIFFUSION",
                                  juce::dontSendNotification);
    diffusion_.slider.setTooltip (isNonLinear ? "Hold: how long the gate stays fully open after the dry input drops below threshold (0 - 500 ms). 100-200 ms is classic gated-snare territory."
                                 : isSpring   ? "Chirp: dispersion-AP coefficient — 0 = plain delay, 1 = full Fender 'boing' on transients"
                                              : "Diffusion: smear amount before the late tank");

    // mid_mult hijacked by NonLinear → THRESHOLD (the gate's sidechain
    // sensitivity). On other engines mid_mult is the 3-band mid damping
    // multiplier and stays labeled "MID MULT".
    midMult_.nameLabel.setText (isNonLinear ? "THRESHOLD" : "MID MULT",
                                 juce::dontSendNotification);
    midMult_.slider.setTooltip (isNonLinear ? "Threshold: dry-input level above which the gate opens. -32 dB is typical for snare; lower triggers on quieter hits, higher only on loud transients."
                                            : "Mid-band damping multiplier (3-band damping)");

    // DECAY hero stays "DECAY" — it controls the hall reverb's RT60 (FDN
    // setDecayTime) on the NonLinear engine, just like every other engine.
    decay_.setDisplayName ("DECAY");

    // ── Per-engine value-text overrides ──
    // NonLinear v7 architecture: mod_depth/mod_rate/diffusion/mid_mult are
    // re-purposed as gate parameters. Display them in their actual meaningful
    // units (ms / dB) instead of the default raw-APVTS formatters.
    if (isNonLinear)
    {
        modDepth_.valueOverride = [] (double v)
        {
            // ATTACK: 1 + depth*49 ms (matches NonLinearEngine::setModDepth)
            const int ms = juce::roundToInt (1.0 + juce::jlimit (0.0, 1.0, v) * 49.0);
            return juce::String (ms) + " ms";
        };
        modRate_.valueOverride = [] (double v)
        {
            // RELEASE: 5 + (Hz-0.1)/9.9 * 1995 ms — across mod_rate's 0.1-10 Hz
            // APVTS range, full 5-2000 ms gate release range.
            const double hz = juce::jlimit (0.1, 10.0, v);
            const int ms = juce::roundToInt (5.0 + (hz - 0.1) / 9.9 * 1995.0);
            return juce::String (ms) + " ms";
        };
        diffusion_.valueOverride = [] (double v)
        {
            // HOLD: diffusion * 500 ms (matches NonLinearEngine::setTankDiffusion)
            const int ms = juce::roundToInt (juce::jlimit (0.0, 1.0, v) * 500.0);
            return juce::String (ms) + " ms";
        };
        midMult_.valueOverride = [] (double v)
        {
            // THRESHOLD: mid_mult 0.1..1.5 → -60..0 dB
            const double m = juce::jlimit (0.1, 1.5, v);
            const double dB = -60.0 + (m - 0.1) / 1.4 * 60.0;
            return juce::String (dB, 1) + " dB";
        };
    }
    else if (isShimmer)
    {
        modDepth_.valueOverride = [] (double v)
        {
            // PITCH: depth 0..1 → 0..24 semitones (matches ShimmerEngine::setModDepth)
            const int semis = juce::roundToInt (juce::jlimit (0.0, 1.0, v) * 24.0);
            return (semis > 0 ? juce::String ("+") : juce::String()) + juce::String (semis) + " st";
        };
        modRate_.valueOverride = [] (double v)
        {
            // FEEDBACK: 0.1..10 Hz → 0..95% (matches ShimmerEngine::setModRate
            // → feedbackGain_, presented as a percentage of kFeedbackMax = 0.95).
            const double hz = juce::jlimit (0.1, 10.0, v);
            const int pct = juce::roundToInt ((hz - 0.1) / 9.9 * 95.0);
            return juce::String (pct) + " %";
        };
        diffusion_.valueOverride = nullptr;
        midMult_  .valueOverride = nullptr;
    }
    else
    {
        modDepth_.valueOverride = nullptr;
        modRate_ .valueOverride = nullptr;
        diffusion_.valueOverride = nullptr;
        midMult_  .valueOverride = nullptr;
    }

    // GATE button only makes sense on the NonLinear engine — it controls the
    // FIR envelope bypass. On other engines it's a no-op so we hide it
    // entirely (the MODULATION group goes back to its original look with
    // just DEPTH/RATE knobs and no button below them).
    gateButton_.setVisible (isNonLinear);

    // The MOD-panel layout in resized() now carves a strip for the GATE
    // button only when it is visible. setVisible() does not re-fire
    // resized(), so we trigger one here to keep the panel bounds + the
    // gate button's own bounds in sync with the new visibility state.
    // Without this, switching to NonLinear leaves the GATE button at its
    // previous (often invisible) bounds, and switching away from
    // NonLinear leaves the MOD knobs sized to the smaller gate-strip
    // layout.
    //
    // Guard against the ctor's pre-setSize() invocation: resized() with
    // getWidth() == 0 would walk every child with a degenerate rectangle.
    // The ctor's setSize() (after this returns) fires the authoritative
    // layout pass anyway.
    if (getWidth() > 0 && getHeight() > 0)
        resized();

    repaint();   // catches FREEZE / BUS toggle redraw via LookAndFeel
}

// =============================================================================
// EngineGlyph
// =============================================================================

void EngineGlyph::paint (juce::Graphics& g)
{
    // Subtle panel background so the glyph reads as a control element rather
    // than floating decoration.
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kPanel).darker (0.2f));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kBorder));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    auto inner = bounds.reduced (4.0f);
    const float w = inner.getWidth();
    const float h = inner.getHeight();
    const float cx = inner.getCentreX();
    const float cy = inner.getCentreY();

    // Pull the live engine accent from the LookAndFeel — the colour shifts
    // when the user switches algorithms.
    auto accent = juce::Colour (DuskVerbLookAndFeel::kAccent);
    if (auto* lnf = dynamic_cast<DuskVerbLookAndFeel*> (&getLookAndFeel()))
        accent = lnf->getCurrentAccent();
    g.setColour (accent);

    auto dot = [&] (float x, float y, float r)
    {
        g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
    };
    auto link = [&] (float x1, float y1, float x2, float y2, float thickness)
    {
        g.drawLine (x1, y1, x2, y2, thickness);
    };

    switch (engine_)
    {
        case EngineType::Dattorro:
        {
            // Two big dots cross-coupled (figure-8 of 2 APs).
            const float r = std::min (w, h) * 0.18f;
            const float spread = w * 0.28f;
            dot (cx - spread, cy, r);
            dot (cx + spread, cy, r);
            link (cx - spread, cy, cx + spread, cy, 1.0f);
            break;
        }
        case EngineType::SixAPTank:
        {
            // Six small dots in a chain — density cascade.
            const float r = std::min (w, h) * 0.10f;
            for (int i = 0; i < 6; ++i)
            {
                const float t = (i + 0.5f) / 6.0f;
                const float x = inner.getX() + t * w;
                dot (x, cy, r);
                if (i > 0)
                    link (inner.getX() + ((i - 1) + 0.5f) / 6.0f * w + r,
                          cy,
                          x - r, cy, 0.6f);
            }
            break;
        }
        case EngineType::QuadTank:
        {
            // 2×2 grid — 4 cross-coupled tanks.
            const float r = std::min (w, h) * 0.13f;
            const float dx = w * 0.20f;
            const float dy = h * 0.20f;
            dot (cx - dx, cy - dy, r);
            dot (cx + dx, cy - dy, r);
            dot (cx - dx, cy + dy, r);
            dot (cx + dx, cy + dy, r);
            // Diagonal cross to suggest coupling.
            link (cx - dx, cy - dy, cx + dx, cy + dy, 0.6f);
            link (cx + dx, cy - dy, cx - dx, cy + dy, 0.6f);
            break;
        }
        case EngineType::FDN:
        {
            // 4×4 grid — 16-channel matrix.
            const float r = std::min (w, h) * 0.06f;
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col)
                {
                    const float x = inner.getX() + (col + 0.5f) / 4.0f * w;
                    const float y = inner.getY() + (row + 0.5f) / 4.0f * h;
                    dot (x, y, r);
                }
            break;
        }
        case EngineType::Spring:
        {
            // Zigzag stylised spring — 5 peaks across the width, vertically
            // centered. Reads as a side-view of a coiled spring.
            const int zigzags = 6;
            const float ampY = h * 0.22f;
            juce::Path p;
            for (int i = 0; i <= zigzags; ++i)
            {
                const float t = static_cast<float> (i) / static_cast<float> (zigzags);
                const float x = inner.getX() + t * w;
                const float y = cy + ((i % 2 == 0) ? -ampY : ampY);
                if (i == 0) p.startNewSubPath (x, y);
                else        p.lineTo (x, y);
            }
            g.strokePath (p, juce::PathStrokeType (1.4f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
            break;
        }
        case EngineType::NonLinear:
        {
            // Square-wave envelope — flat top then sharp drop, the iconic
            // gated-reverb amplitude contour. Two flat segments connected
            // by a vertical cliff visually communicates "non-decay then cut."
            const float topY    = cy - h * 0.22f;
            const float bottomY = cy + h * 0.22f;
            const float xStart  = inner.getX();
            const float xCliff  = inner.getX() + w * 0.65f;   // gate edge at 65%
            const float xEnd    = inner.getRight();
            juce::Path p;
            p.startNewSubPath (xStart,  bottomY);   // baseline pre-input
            p.lineTo          (xStart,  topY);      // sharp leading edge (input hits)
            p.lineTo          (xCliff,  topY);      // plateau across gate
            p.lineTo          (xCliff,  bottomY);   // gate cliff
            p.lineTo          (xEnd,    bottomY);   // baseline tail
            g.strokePath (p, juce::PathStrokeType (1.4f,
                                                    juce::PathStrokeType::mitered,
                                                    juce::PathStrokeType::rounded));
            break;
        }
        case EngineType::Shimmer:
        {
            // Ascending-arrow staircase — three "steps" rising left-to-right,
            // with a final arrowhead. Visually communicates "feedback that
            // climbs" — the cascading-octaves shimmer character.
            const float xStart  = inner.getX();
            const float xEnd    = inner.getRight();
            const float baseY   = cy + h * 0.30f;
            const float topY    = cy - h * 0.30f;
            juce::Path p;
            const int kSteps = 3;
            const float stepW = (xEnd - xStart) * 0.85f / static_cast<float>(kSteps);
            const float stepH = (baseY - topY) / static_cast<float>(kSteps);
            float x = xStart;
            float y = baseY;
            p.startNewSubPath (x, y);
            for (int i = 0; i < kSteps; ++i)
            {
                x += stepW;            p.lineTo (x, y);
                y -= stepH;            p.lineTo (x, y);
            }
            // Arrowhead at the top-right corner pointing up-right
            const float ah = h * 0.10f;
            p.lineTo (x + ah * 0.7f, y + ah);
            p.startNewSubPath (x, y);
            p.lineTo (x + ah,        y + ah * 0.7f);
            g.strokePath (p, juce::PathStrokeType (1.4f,
                                                    juce::PathStrokeType::mitered,
                                                    juce::PathStrokeType::rounded));
            break;
        }
        case EngineType::DattorroVintage:
        {
            // 4 × 4 dot grid — visually communicates the 16-line FDN density.
            const float gridW = inner.getWidth() * 0.85f;
            const float gridH = h * 0.70f;
            const float x0    = cx - gridW * 0.5f;
            const float y0    = cy - gridH * 0.5f;
            const float dotR  = h * 0.05f;
            for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
            {
                const float dx = x0 + (gridW * col) / 3.0f;
                const float dy = y0 + (gridH * row) / 3.0f;
                g.fillEllipse (dx - dotR, dy - dotR, dotR * 2.0f, dotR * 2.0f);
            }
            break;
        }
        case EngineType::DenseHall:
        default:
        {
            // Generic engine cue for engines without a bespoke glyph
            // (VintageTank / ReverseRoom / AccurateHall / SparseField / DenseHall) — a
            // centred ring so the header still shows an icon instead of empty space.
            const float r = std::min (w, h) * 0.30f;
            g.drawEllipse (inner.getCentreX() - r, inner.getCentreY() - r,
                           r * 2.0f, r * 2.0f, 1.5f);
            break;
        }
    }
}

// =============================================================================
// TailMeter
// =============================================================================

void TailMeter::pushFrame (float outputLevelDb)
{
    // Shift left, append newest frame on the right.
    for (int i = 0; i < kNumFrames - 1; ++i)
        history_[static_cast<size_t> (i)] = history_[static_cast<size_t> (i + 1)];
    history_[kNumFrames - 1] = outputLevelDb;
    repaint();
}

void TailMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Recessed dark panel matching the group containers' style language.
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kPanel).darker (0.4f));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kBorder));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    auto plot = bounds.reduced (4.0f);
    if (plot.isEmpty()) return;

    // Map dB to plot Y. -60 dB at the bottom, 0 dB at the top, with a soft
    // curve so the visually-interesting -40..0 range gets most of the height.
    auto dbToY = [&] (float db) -> float
    {
        const float clamped = juce::jlimit (-60.0f, 0.0f, db);
        const float norm    = (clamped + 60.0f) / 60.0f;
        const float curved  = std::pow (norm, 1.6f);
        return plot.getBottom() - curved * plot.getHeight();
    };

    // Filled area under the level curve.
    const float xStep = plot.getWidth() / static_cast<float> (kNumFrames - 1);
    juce::Path fill;
    fill.startNewSubPath (plot.getX(), plot.getBottom());
    for (int i = 0; i < kNumFrames; ++i)
    {
        const float x = plot.getX() + xStep * static_cast<float> (i);
        const float y = dbToY (history_[static_cast<size_t> (i)]);
        fill.lineTo (x, y);
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    // Live engine accent — same source as the knob arcs and value readouts.
    auto accent = juce::Colour (DuskVerbLookAndFeel::kAccent);
    if (auto* lnf = dynamic_cast<DuskVerbLookAndFeel*> (&getLookAndFeel()))
        accent = lnf->getCurrentAccent();

    juce::ColourGradient grad (accent,
                               plot.getX(), plot.getY(),
                               accent.withAlpha (0.15f),
                               plot.getX(), plot.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (fill);

    // Bright stroke along the leading edge for definition.
    juce::Path edge;
    edge.startNewSubPath (plot.getX(), dbToY (history_[0]));
    for (int i = 1; i < kNumFrames; ++i)
    {
        const float x = plot.getX() + xStep * static_cast<float> (i);
        const float y = dbToY (history_[static_cast<size_t> (i)]);
        edge.lineTo (x, y);
    }
    g.setColour (accent.brighter (0.2f));
    g.strokePath (edge, juce::PathStrokeType (1.2f));
}

// =============================================================================
// HeroDecay
// =============================================================================

HeroDecay::HeroDecay()
{
    addAndMakeVisible (slider);
    // Whenever the value changes (via attachment OR user drag) we need to
    // repaint the rings so they reflect the new RT60.
    slider.onValueChange = [this] { repaint(); };
}

void HeroDecay::init (juce::Component& parent,
                      juce::AudioProcessorValueTreeState& apvts,
                      const juce::String& paramID,
                      const juce::String& tooltip)
{
    if (tooltip.isNotEmpty())
        slider.setTooltip (tooltip);
    parent.addAndMakeVisible (*this);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);

    // Double-click anywhere on the hero → spawn the ValueEditor popup over
    // the value-text region at the bottom of the component (NOT the full
    // bounds — covering the rings with a giant text field looks awful).
    // The MouseListener pattern matches what KnobWithLabel uses; the popup
    // helper handles parsing, clamping, commit-on-Enter etc.
    struct HeroValueEditorTrigger : public juce::MouseListener
    {
        HeroDecay* hero = nullptr;
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (hero == nullptr) return;
            // Compute the bottom-strip "value text" area inside HeroDecay.
            // Mirrors the layout in HeroDecay::paint — keep in sync if that
            // changes (NAME at top 10 %, rings in middle, VALUE at bottom 14 %).
            auto bounds = hero->getLocalBounds();
            const int valueH = juce::jmax (16, juce::roundToInt (bounds.getHeight() * 0.14f));
            auto valueArea = bounds.removeFromBottom (valueH);
            ValueEditor::popUp (hero->slider, *hero, valueArea);
        }
    };
    auto* trig = new HeroValueEditorTrigger();
    trig->hero = this;
    valueEditorTrigger.reset (trig);
    slider.addMouseListener (trig, false);
}

void HeroDecay::resized()
{
    // The slider sits invisibly over the entire bounds so any drag inside
    // the visualisation area changes the value.
    slider.setBounds (getLocalBounds());
}

void HeroDecay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty()) return;

    // Engine accent — same source as the rest of the UI.
    auto accent = juce::Colour (DuskVerbLookAndFeel::kAccent);
    if (auto* lnf = dynamic_cast<DuskVerbLookAndFeel*> (&getLookAndFeel()))
        accent = lnf->getCurrentAccent();

    // ---- Layout: NAME (top) → ring area (middle) → VALUE (bottom) ----
    const int   labelH = juce::jmax (12, juce::roundToInt (bounds.getHeight() * 0.10f));
    const int   valueH = juce::jmax (16, juce::roundToInt (bounds.getHeight() * 0.14f));
    auto nameRow  = bounds.removeFromTop    (static_cast<float> (labelH));
    auto valueRow = bounds.removeFromBottom (static_cast<float> (valueH));
    auto ringArea = bounds.reduced (4.0f);

    // ---- NAME ----
    g.setColour (juce::Colour (DuskVerbLookAndFeel::kGroupText));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (displayName, nameRow.toNearestInt(), juce::Justification::centred);

    // ---- Concentric rings ----
    const auto centre  = ringArea.getCentre();
    const float maxR   = std::min (ringArea.getWidth(), ringArea.getHeight()) * 0.5f;

    // Map the slider's normalised value (already log-skewed by APVTS) to
    // a ring count between 2 (very short decay) and 14 (very long decay).
    const auto& range = slider.getNormalisableRange();
    const float norm  = range.convertTo0to1 (static_cast<float> (slider.getValue()));
    const int numRings = 2 + juce::roundToInt (norm * 12.0f);

    // Outer dark disc — gives the rings a contained "well".
    g.setColour (juce::Colour (0xff0d0d1a));
    g.fillEllipse (centre.x - maxR, centre.y - maxR, maxR * 2.0f, maxR * 2.0f);

    // Rings: outer = bright, inner = dimmer. Spacing is geometric so the
    // visual gets denser toward the centre — reads as "decaying energy".
    for (int i = 0; i < numRings; ++i)
    {
        const float t      = (i + 1.0f) / static_cast<float> (numRings + 1);
        const float radius = maxR * (1.0f - t * 0.85f);
        const float alpha  = 0.35f + (1.0f - t) * 0.55f;
        g.setColour (accent.withAlpha (alpha));
        g.drawEllipse (centre.x - radius, centre.y - radius,
                       radius * 2.0f, radius * 2.0f, 1.4f);
    }

    // Centre dot — anchor.
    const float dotR = 4.0f;
    g.setColour (accent);
    g.fillEllipse (centre.x - dotR, centre.y - dotR, dotR * 2.0f, dotR * 2.0f);

    // Value-angle marker — same convention as the secondary rotary knobs:
    // a small bright dot on the outer ring at the slider's rotation angle.
    // Tells the user where DECAY sits within its range at a glance.
    constexpr float kRotaryStart = -2.356f;  // ~−135° (7 o'clock)
    constexpr float kRotaryEnd   =  2.356f;  // ~+135° (5 o'clock)
    const float angle = kRotaryStart + norm * (kRotaryEnd - kRotaryStart);
    const float markerR = maxR * 0.96f;
    const float mx = centre.x + markerR * std::sin (angle);
    const float my = centre.y - markerR * std::cos (angle);
    const float mr = 4.0f;
    g.setColour (juce::Colours::white);
    g.fillEllipse (mx - mr, my - mr, mr * 2.0f, mr * 2.0f);

    // ---- VALUE text ----
    juce::String valueText;
    const double v = slider.getValue();
    if (v < 1.0)
        valueText = juce::String (juce::roundToInt (v * 1000.0)) + " ms";
    else
        valueText = juce::String (v, 2) + " s";

    g.setColour (accent);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (valueText, valueRow.toNearestInt(), juce::Justification::centred);
}


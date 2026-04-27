#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
struct LEDMeterStyle
{
    static constexpr int standardWidth = 32;
    static constexpr int meterAreaWidth = 60;
    static constexpr int labelHeight = 18;
    static constexpr int valueHeight = 22;
    static constexpr int labelSpacing = 4;
    static constexpr float labelFontSize = 12.0f;
    static constexpr float valueFontSize = 12.0f;

    static inline juce::Colour getLabelColor() { return juce::Colour(0xffe0e0e0); }
    static inline juce::Colour getValueColor() { return juce::Colour(0xffcccccc); }

    static void drawMeterLabels(juce::Graphics& g,
                                 juce::Rectangle<int> meterBounds,
                                 const juce::String& label,
                                 float levelDb,
                                 float scaleFactor = 1.0f)
    {
        g.setFont(juce::Font(juce::FontOptions(labelFontSize * scaleFactor).withStyle("Bold")));
        g.setColour(getLabelColor());

        int labelWidth = static_cast<int>(50 * scaleFactor);
        int labelX = meterBounds.getCentreX() - labelWidth / 2;
        g.drawText(label, labelX, meterBounds.getY() - static_cast<int>((labelHeight + labelSpacing) * scaleFactor),
                   labelWidth, static_cast<int>(labelHeight * scaleFactor), juce::Justification::centred);

        g.setFont(juce::Font(juce::FontOptions(valueFontSize * scaleFactor).withStyle("Bold")));
        g.setColour(getValueColor());

        juce::String valueText = juce::String(levelDb, 1) + " dB";
        g.drawText(valueText, labelX, meterBounds.getBottom() + static_cast<int>(labelSpacing * scaleFactor),
                   labelWidth, static_cast<int>(valueHeight * scaleFactor), juce::Justification::centred);
    }
};

//==============================================================================
// Slider with Shift+drag fine control and Ctrl/Cmd+click reset.
class DuskSlider : public juce::Slider
{
public:
    DuskSlider()
    {
        initDuskSlider();
    }

    explicit DuskSlider(const juce::String& componentName) : juce::Slider(componentName)
    {
        initDuskSlider();
    }

    DuskSlider(SliderStyle style, TextEntryBoxPosition textBoxPosition)
        : juce::Slider(style, textBoxPosition)
    {
        initDuskSlider();
    }

private:
    void initDuskSlider()
    {
        setVelocityBasedMode(false);
    }

public:
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            if (isDoubleClickReturnEnabled())
            {
                setValue(getDoubleClickReturnValue(), juce::sendNotificationSync);
                // Don't initiate drag, but still handle focus
                juce::Component::mouseDown(e);
                return;
            }
        }

        setVelocityBasedMode(false);
        lastDragProportion = valueToProportionOfLength(getValue());
        lastDragY = e.position.y;
        lastDragX = e.position.x;

        juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!isEnabled())
        {
            juce::Slider::mouseDrag(e);
            return;
        }

        bool fineMode = e.mods.isShiftDown();
        double pixelDiff = 0.0;
        auto style = getSliderStyle();

        if (style == RotaryVerticalDrag || style == Rotary ||
            style == LinearVertical || style == LinearBarVertical)
        {
            pixelDiff = lastDragY - e.position.y;
        }
        else if (style == RotaryHorizontalDrag ||
                 style == LinearHorizontal || style == LinearBar)
        {
            pixelDiff = e.position.x - lastDragX;
        }
        else if (style == RotaryHorizontalVerticalDrag)
        {
            pixelDiff = (e.position.x - lastDragX) + (lastDragY - e.position.y);
        }
        else
        {
            juce::Slider::mouseDrag(e);
            return;
        }

        double sensitivity = fineMode ? 600.0 : 200.0;
        double proportionDelta = pixelDiff / sensitivity;
        lastDragProportion = juce::jlimit(0.0, 1.0, lastDragProportion + proportionDelta);
        double newValue = proportionOfLengthToValue(lastDragProportion);

        setValue(newValue, juce::sendNotificationSync);

        lastDragY = e.position.y;
        lastDragX = e.position.x;
    }

    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override
    {
        if (!isEnabled() || !isScrollWheelEnabled())
        {
            juce::Slider::mouseWheelMove(e, wheel);
            return;
        }

        bool fineMode = e.mods.isShiftDown();

        float wheelDelta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX)
                           ? wheel.deltaY
                           : -wheel.deltaX;

        if (wheel.isReversed)
            wheelDelta = -wheelDelta;

        double sensitivity = fineMode ? 0.033 : 0.10;
        double proportionDelta = wheelDelta * sensitivity;

        double currentProportion = valueToProportionOfLength(getValue());
        double newProportion = juce::jlimit(0.0, 1.0,
                                            currentProportion + proportionDelta);
        double newValue = proportionOfLengthToValue(newProportion);

        double interval = getInterval();
        if (interval > 0)
        {
            double diff = newValue - getValue();
            if (std::abs(diff) > 0.0 && std::abs(diff) < interval)
                newValue = getValue() + interval * (diff < 0 ? -1.0 : 1.0);
        }

        setValue(snapValue(newValue, notDragging), juce::sendNotificationSync);
    }

    // Double-click anywhere on the slider → spawn the shared ValueEditor
    // popup so the user can type an exact value. Anchored over the slider
    // itself by default; plugins that have a separate value-label can call
    // ValueEditor::popUp(slider, &valueLabel) from their own double-click
    // handler instead.
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    double lastDragProportion = 0.0;
    float lastDragY = 0.0f;
    float lastDragX = 0.0f;
};

//==============================================================================
// Shared "type a value" popup. Spawns a temporary juce::TextEditor over an
// anchor component, pre-fills with the slider's current displayed value,
// commits on Enter / focus-loss, cancels on Escape. Smart-parses common
// unit suffixes (k/kHz/Hz/ms/dB/%/x/s) so the user can type "1.5k" or
// "85%" intuitively. Out-of-range values are silently clamped.
//
// Usage:
//   • DuskSlider's own mouseDoubleClick hooks this automatically (anchor =
//     the slider itself).
//   • Custom layouts (e.g. DuskVerb's KnobWithLabel) call popUp explicitly
//     from their own listeners, passing the value-label as the anchor so
//     the editor lands exactly over the visible value text.
class ValueEditor
{
public:
    static void popUp (juce::Slider& slider, juce::Component& anchor)
    {
        popUp (slider, anchor, anchor.getLocalBounds());
    }

    // Overload: anchor over a sub-rectangle within `anchor`'s coordinate
    // space. Useful when the anchor is a large composite component (e.g.
    // DuskVerb's HeroDecay) and we only want the editor to land on the
    // visible value-text region rather than the entire control surface.
    static void popUp (juce::Slider& slider,
                       juce::Component& anchor,
                       juce::Rectangle<int> anchorLocalArea)
    {
        auto* topLevel = anchor.getTopLevelComponent();
        if (topLevel == nullptr)
            return;

        // Bail if there's already an editor in flight for this slider — the
        // tag avoids double-popups when both the slider and label fire
        // double-click in quick succession.
        const juce::int64 sliderTag = reinterpret_cast<juce::int64> (&slider);
        for (int i = 0; i < topLevel->getNumChildComponents(); ++i)
        {
            auto* c = topLevel->getChildComponent (i);
            if (c != nullptr
                && static_cast<juce::int64> (c->getProperties().getWithDefault ("dusk_valeditor_for",
                                                                                juce::var (juce::int64 (0))))
                   == sliderTag)
            {
                c->grabKeyboardFocus();
                return;
            }
        }

        // Use a Component-owned editor so JUCE handles destruction when its
        // parent goes away (e.g. Logic destroys the plugin window). The raw
        // pointer survives only as long as topLevel owns it; we hand-off
        // ownership immediately via addAndMakeVisible.
        auto* editor = new juce::TextEditor();
        editor->setText (slider.getTextFromValue (slider.getValue()), false);
        editor->setMultiLine (false);
        editor->setReturnKeyStartsNewLine (false);
        editor->setEscapeAndReturnKeysConsumed (true);
        editor->setBorder ({});
        editor->setIndents (4, 2);
        editor->setJustification (juce::Justification::centred);
        editor->setFont (juce::FontOptions (14.0f, juce::Font::bold));
        editor->setSelectAllWhenFocused (true);
        editor->setColour (juce::TextEditor::backgroundColourId,     juce::Colour (0xff1a1a2e));
        editor->setColour (juce::TextEditor::textColourId,           juce::Colours::white);
        editor->setColour (juce::TextEditor::outlineColourId,        juce::Colour (0xff404060));
        editor->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xffe89c4f));
        editor->setColour (juce::TextEditor::highlightColourId,      juce::Colour (0x66e89c4f));
        editor->setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);

        editor->getProperties().set ("dusk_valeditor_for", reinterpret_cast<juce::int64> (&slider));

        // Convert anchor's local sub-rect to top-level coordinates and pad
        // slightly so the editor frame is comfortably tappable.
        auto bounds = topLevel->getLocalArea (&anchor, anchorLocalArea).expanded (4, 2);
        editor->setBounds (bounds);

        topLevel->addAndMakeVisible (editor);
        editor->grabKeyboardFocus();

        // Hardened lifecycle. Three failure modes we previously hit:
        //   1) commit → setValue → APVTS sync notify → editor onFocusLost
        //      fires commit again recursively → both cleanups queue
        //      `delete editor` via callAsync → DOUBLE FREE.
        //   2) Logic destroys the plugin window between commit and the
        //      callAsync firing → editor already deleted by parent's
        //      destructor → callAsync's delete is USE-AFTER-FREE.
        //   3) sendNotificationSync invokes value-change listeners on the
        //      message thread synchronously; if any listener tears down
        //      part of the UI (Advanced tab rebuild on certain params),
        //      the editor's parent goes away mid-callback.
        //
        // Fix:
        //   • SafePointer<TextEditor> survives parent-destruction without
        //     dangling — every access to the editor checks .getComponent()
        //   • SafePointer<Slider> guards against the slider being
        //     destroyed before commit fires
        //   • shared `state` flag short-circuits reentrant commit/cleanup
        //   • sendNotificationAsync defers the param update one message-
        //     loop tick → no reentrancy from listener chains
        //   • cleanup deletes via deleteSelf() → JUCE's safe-delete that
        //     handles the case where the editor was already destroyed
        juce::Component::SafePointer<juce::TextEditor> safeEditor (editor);
        juce::Component::SafePointer<juce::Slider>     safeSlider (&slider);
        auto state = std::make_shared<bool> (false);

        auto cleanup = [safeEditor, state]()
        {
            if (*state) return;
            *state = true;

            if (auto* e = safeEditor.getComponent())
            {
                e->onReturnKey = nullptr;
                e->onFocusLost = nullptr;
                e->onEscapeKey = nullptr;

                // Defer deletion to the next message-loop tick so we don't
                // delete from within the editor's own callback. SafePointer
                // protects against parent-destruction in the meantime.
                juce::MessageManager::callAsync ([safeEditor]()
                {
                    if (auto* e2 = safeEditor.getComponent())
                    {
                        if (auto* parent = e2->getParentComponent())
                            parent->removeChildComponent (e2);
                        delete e2;
                    }
                });
            }
        };

        auto commit = [safeEditor, safeSlider, cleanup, state]()
        {
            if (*state) return;

            auto* e = safeEditor.getComponent();
            if (e == nullptr) { cleanup(); return; }

            const auto text = e->getText().trim();
            if (text.isNotEmpty())
            {
                if (auto* s = safeSlider.getComponent())
                {
                    const double parsed = parseSmart (text, *s);
                    if (std::isfinite (parsed))
                    {
                        const double clamped = juce::jlimit (s->getMinimum(),
                                                             s->getMaximum(), parsed);
                        // ASYNC notification — defers the param change one
                        // message-loop tick so no listener fires while we're
                        // still inside this callback. Critical for Logic where
                        // the sync chain crosses APVTS → host → automation
                        // listeners → repaint cascades.
                        s->setValue (clamped, juce::sendNotificationAsync);
                    }
                }
            }
            cleanup();
        };

        editor->onReturnKey = commit;
        editor->onFocusLost = commit;
        editor->onEscapeKey = cleanup;
    }

private:
    // Parse text like "1.5k", "500hz", "-3.5dB", "85%", "60ms" into a numeric
    // value in the slider's native units. Heuristics:
    //   • "k" or "kHz" suffix multiplies by 1000
    //   • "%" suffix (or any % anywhere in the slider's display text) treats
    //     the input as percent → multiplies by 0.01
    //   • "ms" suffix when the slider's range max ≤ 60 (i.e. a seconds-scale
    //     knob like DECAY) divides by 1000 to convert ms → s
    //   • Other unit suffixes (Hz, dB, x, s) are stripped as decoration
    static double parseSmart (juce::String text, juce::Slider& slider)
    {
        text = text.toLowerCase().trim();
        if (text.isEmpty())
            return slider.getValue();

        const juce::String displayed = slider.getTextFromValue (slider.getValue()).toLowerCase();
        bool isPercent = displayed.containsChar ('%');
        // Treat slider as seconds-scale when its max ≤ 60 AND its display
        // ends with a real time-unit token. Catches DuskVerb's DECAY knob
        // whose display auto-switches between "ms" and "s" depending on
        // value. Require an *ending* token (not contains) so unrelated text
        // — e.g. a hypothetical "samples"-suffixed knob — can't false-match
        // by virtue of containing the letter 's'.
        const bool endsWithSecondsToken = displayed.endsWith ("ms")
                                        || displayed.endsWith ("milliseconds")
                                        || displayed.endsWith ("sec")
                                        || displayed.endsWith ("secs")
                                        || displayed.endsWith ("seconds")
                                        || (displayed.endsWithChar ('s')
                                            && ! displayed.endsWith ("samples")
                                            && ! displayed.endsWith ("sample"));
        const bool isSecondsScale = slider.getMaximum() <= 60.0 && endsWithSecondsToken;

        double multiplier = 1.0;

        if (text.endsWithChar ('%'))
        {
            text = text.dropLastCharacters (1).trim();
            isPercent = true;
        }

        if (text.endsWith ("khz"))
        {
            multiplier *= 1000.0;
            text = text.dropLastCharacters (3).trim();
        }
        else if (text.endsWith ("hz"))
        {
            text = text.dropLastCharacters (2).trim();
        }
        else if (text.endsWith ("ms"))
        {
            if (isSecondsScale) multiplier *= 0.001;
            text = text.dropLastCharacters (2).trim();
        }
        else if (text.endsWith ("db"))
        {
            text = text.dropLastCharacters (2).trim();
        }
        else if (text.endsWithChar ('k'))
        {
            multiplier *= 1000.0;
            text = text.dropLastCharacters (1).trim();
        }
        else if (text.endsWithChar ('s') || text.endsWithChar ('x'))
        {
            text = text.dropLastCharacters (1).trim();
        }

        double value = text.getDoubleValue() * multiplier;
        if (isPercent) value *= 0.01;
        return value;
    }
};

inline void DuskSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    ValueEditor::popUp (*this, *this);
}

//==============================================================================
struct DuskTooltips
{
    static inline const juce::String fineControlHint = " (Shift+drag for fine control)";
    static inline const juce::String resetHint = " (Ctrl/Cmd+click to reset)";

    static inline const juce::String bypass = "Bypass all processing (Shortcut: B)";
    static inline const juce::String analyzer = "Show/hide real-time FFT spectrum analyzer (Shortcut: H)";
    static inline const juce::String abComparison = "A/B Comparison: Click to switch between two settings (Shortcut: A)";
    static inline const juce::String hqMode = "Enable 2x oversampling for analog-matched response at high frequencies";

    static inline const juce::String frequency = "Frequency: Center frequency of this band";
    static inline const juce::String gain = "Gain: Boost or cut at this frequency";
    static inline const juce::String qBandwidth = "Q: Bandwidth/resonance - higher values = narrower bandwidth";
    static inline const juce::String filterSlope = "Filter slope: Steeper = sharper cutoff";

    static inline const juce::String dynThreshold = "Threshold: Level where dynamic gain reduction starts";
    static inline const juce::String dynAttack = "Attack: How fast gain reduction responds to level increases";
    static inline const juce::String dynRelease = "Release: How fast gain returns after level drops";
    static inline const juce::String dynRange = "Range: Maximum amount of dynamic gain reduction";

    static juce::String withFineControl(const juce::String& tooltip) { return tooltip + fineControlHint; }
    static juce::String withReset(const juce::String& tooltip) { return tooltip + resetHint; }
    static juce::String withAllHints(const juce::String& tooltip) { return tooltip + fineControlHint + resetHint; }
};

class DuskLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DuskLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1a1a1a));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff4a9eff));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff0f0f0f));
        setColour(juce::Label::textColourId, juce::Colours::white);
    }

    ~DuskLookAndFeel() override = default;
};

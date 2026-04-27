#pragma once

#include "PluginProcessor.h"
#include "dsp/AlgorithmConfig.h"
#include "LEDMeter.h"
#include "ScalableEditorHelper.h"
#include "UserPresetManager.h"
#include "SupportersOverlay.h"

#include <array>

// Forward declaration so the engine-accent helper can take an EngineType.
enum class EngineType : int;

// Per-engine accent colour. The current engine paints its arcs / value
// readouts / tail meter in this hue so the plugin's *visual identity*
// shifts with the algorithm.
inline juce::Colour getEngineAccent (EngineType engine)
{
    switch (engine)
    {
        case EngineType::Dattorro:        return juce::Colour (0xffffb84d);  // warm gold — vintage plate
        case EngineType::ModernSpace6AP:  return juce::Colour (0xffd950c0);  // deep magenta — lush halls
        case EngineType::QuadTank:        return juce::Colour (0xff4dd99e);  // emerald — natural rooms
        case EngineType::FDN:             return juce::Colour (0xffff7a3d);  // orange — realistic / Bricasti-like
        case EngineType::Spring:          return juce::Colour (0xff4dd9b8);  // teal — springy mechanical character
        case EngineType::NonLinear:       return juce::Colour (0xffe85a3a);  // red-orange — gated drum punch
        case EngineType::Shimmer:         return juce::Colour (0xffd47de8);  // lavender-pink — ethereal cascade
    }
    return juce::Colour (0xffff7a3d);
}

class DuskVerbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DuskVerbLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    // Live-mutable accent so we can recolour the entire UI when the user
    // switches engines. Defaults to the legacy orange so first paint matches.
    void          setCurrentAccent (juce::Colour c) { currentAccent_ = c; }
    juce::Colour  getCurrentAccent() const          { return currentAccent_; }

    static constexpr juce::uint32 kBackground   = 0xff1a1a2e;
    static constexpr juce::uint32 kPanel        = 0xff16213e;
    static constexpr juce::uint32 kAccent       = 0xffff7a3d;
    static constexpr juce::uint32 kKnobFill     = 0xff0f3460;
    static constexpr juce::uint32 kBorder       = 0xff353560;
    static constexpr juce::uint32 kSeparator    = 0xff242440;

private:
    juce::Colour currentAccent_ = juce::Colour (kAccent);

public:

    static constexpr juce::uint32 kValueText    = 0xfff0f0f0;
    static constexpr juce::uint32 kLabelText    = 0xffb0b0b8;
    static constexpr juce::uint32 kGroupText    = 0xff9898a0;
    static constexpr juce::uint32 kDimText      = 0xff555555;

    static constexpr juce::uint32 kText         = 0xffe0e0e0;
    static constexpr juce::uint32 kSubtleText   = 0xff888888;
};

struct KnobWithLabel
{
    juce::Slider slider;
    juce::Label  nameLabel;
    juce::Label  valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    // Owns the MouseListener that detects double-click on either the slider
    // or the value label and spawns the shared ValueEditor popup. Type is
    // erased to juce::MouseListener so PluginEditor.h doesn't have to pull
    // in DuskLookAndFeel.h.
    std::unique_ptr<juce::MouseListener> valueEditorTrigger;

    // Stored accent so it survives any code path that resets label colours
    // (e.g. the value-editor close path was leaving labels white because the
    // overlay TextEditor's focus loss touched the parent label colours).
    // The timer callback re-asserts this colour on every tick.
    juce::Colour currentAccent { 0xffff7a3d };

    void init (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramID, const juce::String& displayName,
               const juce::String& suffix, const juce::String& tooltip = {});

    // Re-tint the value readout when the engine accent changes. Knob arc
    // colour follows the LookAndFeel which is updated separately, so the
    // whole knob recolours coherently after the accent is swapped.
    void setAccent (juce::Colour accent);
};

// Slider subclass that paints NOTHING — the parent HeroDecay paints the
// rings underneath. The slider is here purely to capture mouse drag /
// scroll-wheel input and forward it to the APVTS attachment.
class InvisibleRotarySlider : public juce::Slider
{
public:
    InvisibleRotarySlider() : juce::Slider (RotaryHorizontalVerticalDrag, NoTextBox) {}
    void paint (juce::Graphics&) override {}
};

// =====================================================================
// HeroDecay — oversized DECAY visualisation: concentric rings whose
// count + radius reflect the decay value, value text below, name above.
// Replaces the standard rotary knob for the single most-used control.
// Style cue: Valhalla VintageVerb's hero-Decay visualisation.
// =====================================================================
class HeroDecay : public juce::Component
{
public:
    HeroDecay();

    void init (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramID, const juce::String& tooltip);

    void paint (juce::Graphics& g) override;
    void resized() override;

    InvisibleRotarySlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    // Owns the double-click MouseListener that opens the ValueEditor popup.
    // Kept as juce::MouseListener so the header doesn't need DuskLookAndFeel.h.
    std::unique_ptr<juce::MouseListener> valueEditorTrigger;

    // Display name override — defaults to "DECAY" but engines that hijack
    // the decay parameter (e.g. NonLinear → "LENGTH") can swap the label
    // by calling setName() and triggering a repaint.
    juce::String displayName { "DECAY" };
    void setDisplayName (const juce::String& name)
    {
        if (displayName != name)
        {
            displayName = name;
            repaint();
        }
    }
};

// =====================================================================
// EngineGlyph — tiny topology icon next to the Algorithm dropdown.
// Renders a stylised diagram of the active late-tank engine so the user
// sees what they're picking, not just a name.
// =====================================================================
class EngineGlyph : public juce::Component
{
public:
    EngineGlyph() { setInterceptsMouseClicks (false, false); }

    void setEngine (EngineType e)
    {
        if (engine_ != e) { engine_ = e; repaint(); }
    }

    void paint (juce::Graphics& g) override;

private:
    EngineType engine_ = EngineType::Dattorro;
};

// =====================================================================
// TailMeter — slim live histogram of recent output level, rolling
// right-to-left. Ring-buffer of post-output peak (dB) samples, painted
// as a filled accent-colour curve. Cheap (no DSP), visually shows what
// the reverb is doing.
// =====================================================================
class TailMeter : public juce::Component
{
public:
    TailMeter() { setInterceptsMouseClicks (false, false); }

    // Push one frame from the editor's timer (typically 15 Hz).
    void pushFrame (float outputLevelDb);

    void paint (juce::Graphics& g) override;

private:
    static constexpr int kNumFrames = 200;
    std::array<float, kNumFrames> history_ {};   // [0] = oldest, [last] = newest
};

class DuskVerbEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit DuskVerbEditor (DuskVerbProcessor&);
    ~DuskVerbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showSupportersPanel();
    void hideSupportersPanel();

    // Repaints the entire UI in the new engine accent. Called from the
    // algorithm dropdown's onChange and once during construction.
    void applyEngineAccent (EngineType engine);

    DuskVerbProcessor& processorRef;
    DuskVerbLookAndFeel lnf_;
    ScalableEditorHelper scaler_;

    // Algorithm dropdown (= engine selector). Mirrors the algorithm APVTS choice.
    juce::ComboBox algorithmBox_;
    EngineGlyph    engineGlyph_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> algorithmAttachment_;

    // Live output-tail histogram, painted under the header dropdowns.
    TailMeter tailMeter_;

    // Preset browser
    juce::ComboBox presetBox_;
    juce::TextButton prevPresetButton_;
    juce::TextButton nextPresetButton_;
    void loadPreset (int index);
    void refreshPresetList();
    void stepFactoryPreset (int delta);

    // User preset management
    std::unique_ptr<UserPresetManager> userPresetManager_;
    juce::TextButton savePresetButton_;
    juce::TextButton deletePresetButton_;
    void saveUserPreset();
    void loadUserPreset (const juce::String& name);
    void deleteUserPreset (const juce::String& name);
    void updateDeleteButtonVisibility();

    // 16 main knobs (matches the 21-param spec minus algorithm/bus_mode/predelay_sync/freeze/bypass).
    KnobWithLabel preDelay_;
    HeroDecay     decay_;
    KnobWithLabel size_;
    KnobWithLabel modDepth_;
    KnobWithLabel modRate_;
    KnobWithLabel damping_;
    KnobWithLabel bassMult_;
    KnobWithLabel midMult_;            // NEW: 3-band mid multiplier
    KnobWithLabel crossover_;
    KnobWithLabel highCrossover_;      // NEW: 3-band high crossover
    KnobWithLabel saturation_;         // NEW: drive softClip
    KnobWithLabel diffusion_;
    KnobWithLabel erLevel_;
    KnobWithLabel erSize_;
    KnobWithLabel mix_;
    KnobWithLabel loCut_;
    KnobWithLabel hiCut_;
    KnobWithLabel monoBelow_;
    KnobWithLabel width_;
    KnobWithLabel gainTrim_;

    juce::ToggleButton freezeButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment_;

    juce::ToggleButton busModeButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> busModeAttachment_;

    juce::ComboBox predelaySyncBox_;
    juce::Label    predelaySyncLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> predelaySyncAttachment_;

    LEDMeter inputMeter_  { LEDMeter::Vertical };
    LEDMeter outputMeter_ { LEDMeter::Vertical };

    std::unique_ptr<SupportersOverlay> supportersOverlay_;
    juce::Rectangle<int> titleClickArea_;

    juce::TooltipWindow tooltipWindow_ { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskVerbEditor)
};

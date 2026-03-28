#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MultiSynth.h"
#include "MultiSynthLookAndFeel.h"
#include "../shared/DuskLookAndFeel.h"
#include "../shared/LEDMeter.h"
#include "../shared/SupportersOverlay.h"
#include "../shared/UserPresetManager.h"
#include "../shared/ScalableEditorHelper.h"

//==============================================================================
// Oscilloscope: reads from processor's scope ring buffer
class WaveformDisplay : public juce::Component
{
public:
    void setAccentColor(juce::Colour c) { accent = c; repaint(); }
    void setBackgroundColor(juce::Colour c) { bg = c; }
    void updateBuffer(const float* data, int size);
    void paint(juce::Graphics& g) override;

private:
    juce::Colour accent { 0xFF6070DD };
    juce::Colour bg { 0xFF1A1C2E };
    std::array<float, 512> samples {};
    int numSamples = 0;
};

//==============================================================================
// Filter frequency response mini-display
class FilterResponseDisplay : public juce::Component
{
public:
    void setParameters(float cutoffHz, float resonance, float sampleRate)
    {
        cutoff = cutoffHz;
        res = resonance;
        sr = sampleRate;
        repaint();
    }
    void setAccentColor(juce::Colour c) { accent = c; }
    void setBackgroundColor(juce::Colour c) { bg = c; }
    void paint(juce::Graphics& g) override;
private:
    float cutoff = 8000.0f, res = 0.3f, sr = 44100.0f;
    juce::Colour accent { 0xFF6070DD }, bg { 0xFF1A1C2E };
};

//==============================================================================
// Mod Matrix Overlay (popup panel)
class ModMatrixOverlay : public juce::Component
{
public:
    ModMatrixOverlay();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void setAccentColor(juce::Colour c) { accent = c; }
    std::function<void()> onDismiss;

    // Slots (set up externally by the editor)
    juce::ComboBox srcBoxes[8], dstBoxes[8];
    DuskSlider amtSliders[8];

private:
    juce::Colour accent { 0xFF6070DD };
};

//==============================================================================
class MultiSynthEditor : public juce::AudioProcessorEditor,
                          public juce::Timer
{
public:
    explicit MultiSynthEditor(MultiSynthProcessor&);
    ~MultiSynthEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    MultiSynthProcessor& processor;
    ScalableEditorHelper scaleHelper;

    // Mode-specific LookAndFeels
    CosmosLookAndFeel cosmosLAF;
    OracleLookAndFeel oracleLAF;
    MonoLookAndFeel monoLAF;
    ModularLookAndFeel modularLAF;
    MultiSynthLookAndFeelBase* currentLAF = nullptr;
    MultiSynthDSP::SynthMode lastMode = MultiSynthDSP::SynthMode::Cosmos;

    void applyCurrentLookAndFeel();
    juce::Colour getModeColor() const { return currentLAF ? currentLAF->colors.accent : juce::Colour(0xff4a9eff); }

    // === Layout Constants (1x scale, multiply by sf) ===
    static constexpr int kDefaultWidth  = 1000;
    static constexpr int kDefaultHeight = 700;
    static constexpr int kMargin        = 8;
    static constexpr int kSectionGap    = 6;
    static constexpr int kSectionPad    = 8;
    static constexpr int kSectionTitleH = 20;
    static constexpr int kKnobSize      = 70;
    static constexpr int kSmallKnob     = 55;
    static constexpr int kLabelH        = 18;
    static constexpr int kKnobSpacing   = 8;
    static constexpr int kComboH        = 22;
    static constexpr int kToggleH       = 22;
    static constexpr int kTopBarH       = 38;
    static constexpr int kMeterW        = 24;

    int scaled(int v) const { return scaleHelper.scaled(v); }

    // === Top bar ===
    juce::ComboBox modeSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeSelectorAttachment;
    juce::ComboBox presetBox;
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton deletePresetButton { "Del" };
    juce::ComboBox oversamplingBox;
    juce::TextButton modMatrixButton { "MOD" };

    // Preset management
    std::unique_ptr<UserPresetManager> userPresetManager;
    void refreshPresetList();
    void saveUserPreset();
    void loadUserPreset(const juce::String& name);
    void deleteUserPreset();
    int factoryPresetCount = 0;

    // === Oscillators ===
    juce::ComboBox osc1WaveBox, osc2WaveBox, osc3WaveBox, subWaveBox;
    DuskSlider osc1LevelSlider, osc1DetuneSlider, osc1PWSlider;
    DuskSlider osc2LevelSlider, osc2DetuneSlider, osc2SemiSlider;
    DuskSlider osc3LevelSlider, subLevelSlider, noiseLevelSlider;
    DuskSlider crossModSlider, ringModSlider, fmAmountSlider;
    juce::ToggleButton hardSyncButton;

    // === Filter ===
    DuskSlider filterCutoffSlider, filterResSlider, filterHPSlider, filterEnvAmtSlider;

    // === Envelopes ===
    DuskSlider ampASlider, ampDSlider, ampSSlider, ampRSlider;
    DuskSlider filtASlider, filtDSlider, filtSSlider, filtRSlider;
    juce::ComboBox ampCurveBox, filtCurveBox;

    // === LFOs ===
    DuskSlider lfo1RateSlider, lfo1FadeSlider, lfo2RateSlider, lfo2FadeSlider;
    juce::ComboBox lfo1ShapeBox, lfo2ShapeBox;
    juce::ToggleButton lfo1SyncButton, lfo2SyncButton;

    // === Character / Unison ===
    DuskSlider portaSlider, analogSlider, vintageSlider, velSensSlider;
    juce::ComboBox velCurveBox;
    juce::ToggleButton legatoButton;
    juce::ComboBox glideModeBox;
    DuskSlider unisonVoicesSlider, unisonDetuneSlider, unisonSpreadSlider;

    // === Cosmos-specific ===
    juce::ComboBox cosmosChorusBox; // Off, I, II, I+II

    // === Oracle poly-mod ===
    DuskSlider pmFEnvOscASlider, pmFEnvFiltSlider, pmOscBOscASlider, pmOscBPWMSlider;

    // === Arpeggiator ===
    juce::ToggleButton arpOnButton, arpLatchButton;
    juce::ComboBox arpModeBox, arpRateBox, arpVelModeBox;
    DuskSlider arpOctaveSlider, arpGateSlider, arpSwingSlider;

    // === Effects ===
    juce::ToggleButton driveOnButton, chorusOnButton, delayOnButton, reverbOnButton;
    juce::ComboBox driveTypeBox;
    DuskSlider driveAmtSlider, driveMixSlider;
    DuskSlider chorusRateSlider, chorusDepthSlider, chorusMixSlider;
    juce::ToggleButton delaySyncButton, delayPPButton, delayTapeButton;
    DuskSlider delayTimeSlider, delayFBSlider, delayMixSlider;
    juce::ComboBox delayDivBox;
    DuskSlider reverbSizeSlider, reverbDecaySlider, reverbDampSlider, reverbMixSlider, reverbPDSlider;

    // === Master ===
    DuskSlider masterTuneSlider, masterVolSlider, masterPanSlider, stereoWidthSlider;
    LEDMeter outputMeterL, outputMeterR;

    // === Oscilloscope ===
    WaveformDisplay waveformDisplay;

    // === Filter Response ===
    FilterResponseDisplay filterResponseDisplay;

    // === Mod Matrix Overlay ===
    ModMatrixOverlay modMatrixOverlay;

    // === Supporters ===
    SupportersOverlay supportersOverlay;

    // === APVTS Attachments ===
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    void setupSlider(DuskSlider& slider, const juce::String& paramId);
    void setupKnob(DuskSlider& slider, juce::Label& label, const juce::String& paramId, const juce::String& name);
    void setupComboBox(juce::ComboBox& box, const juce::String& paramId);
    void setupToggle(juce::ToggleButton& button, const juce::String& paramId, const juce::String& text);
    void updateModeVisibility();

    // Per-mode layout (called from resized)
    void layoutCosmos();   // Jupiter-8: vertical faders, orange header
    void layoutOracle();   // Prophet-5: rotary knobs, wood cheeks
    void layoutMono();     // SH-2: mix of knobs and faders, metal panel
    void layoutModular();  // ARP 2600: knobs + faders, patch points

    // Per-mode painting (called from paint)
    void paintCosmos(juce::Graphics& g);
    void paintOracle(juce::Graphics& g);
    void paintMono(juce::Graphics& g);
    void paintModular(juce::Graphics& g);

    // Helpers for per-mode layouts
    void setAllSlidersToKnobs();
    void setSliderAsFader(DuskSlider& s);
    void layoutSharedLowerStrip(); // arp, scope, meters — shared across modes

    // Knob labels (one per slider, attached above via attachToComponent)
    juce::Label osc1LevelLbl, osc1DetuneLbl, osc1PWLbl;
    juce::Label osc2LevelLbl, osc2DetuneLbl, osc2SemiLbl;
    juce::Label osc3LevelLbl, subLevelLbl, noiseLevelLbl;
    juce::Label crossModLbl, ringModLbl, fmAmountLbl;
    juce::Label filterCutoffLbl, filterResLbl, filterHPLbl, filterEnvAmtLbl;
    juce::Label ampALbl, ampDLbl, ampSLbl, ampRLbl;
    juce::Label filtALbl, filtDLbl, filtSLbl, filtRLbl;
    juce::Label lfo1RateLbl, lfo1FadeLbl, lfo2RateLbl, lfo2FadeLbl;
    juce::Label portaLbl, analogLbl, vintageLbl, velSensLbl;
    juce::Label unisonVoicesLbl, unisonDetuneLbl, unisonSpreadLbl;
    juce::Label arpOctaveLbl, arpGateLbl, arpSwingLbl;
    juce::Label driveAmtLbl, driveMixLbl;
    juce::Label chorusRateLbl, chorusDepthLbl, chorusMixLbl;
    juce::Label delayTimeLbl, delayFBLbl, delayMixLbl;
    juce::Label reverbSizeLbl, reverbDecayLbl, reverbDampLbl, reverbMixLbl, reverbPDLbl;
    juce::Label masterTuneLbl, masterVolLbl, masterPanLbl, stereoWidthLbl;
    juce::Label pmFEnvOscALbl, pmFEnvFiltLbl, pmOscBOscALbl, pmOscBPWMLbl;

    // Section bounds cache (set in resized, used in paint)
    struct SectionBounds {
        juce::Rectangle<int> oscillators, filter, envelopes, scopeArea, metersArea;
        juce::Rectangle<int> lfo, character, arp;
        juce::Rectangle<int> drive, chorus, delay, reverb;
    } sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiSynthEditor)
};

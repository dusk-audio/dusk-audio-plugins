#pragma once

#include "PluginProcessor.h"
#include "ui/DuskAmpLookAndFeel.h"
#include "ui/CabBrowser.h"
#include "ui/NAMBrowser.h"
#include "LEDMeter.h"
#include "ScalableEditorHelper.h"
#include "UserPresetManager.h"
#include "SupportersOverlay.h"

// Reusable knob+label (same pattern as DuskVerb)
struct KnobWithLabel
{
    juce::Slider slider;
    juce::Label  nameLabel;
    juce::Label  valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    void init (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramID, const juce::String& displayName,
               const juce::String& suffix, const juce::String& tooltip = {});

    void setDimmed (bool dimmed)
    {
        float alpha = dimmed ? 0.4f : 1.0f;
        slider.setAlpha (alpha);
        nameLabel.setAlpha (alpha);
        valueLabel.setAlpha (alpha);
    }
};

// 2-segment mode selector (DSP / NAM)
class AmpModeSelector : public juce::Component
{
public:
    AmpModeSelector (juce::RangedAudioParameter& param);

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::RangedAudioParameter& param_;
    juce::ParameterAttachment attachment_;
    int currentIndex_ = 0;
    juce::StringArray labels_ { "DSP", "NAM" };
    std::vector<juce::Rectangle<int>> segmentBounds_;
};

// Small vertical indicator showing power supply B+ voltage (sag)
class SagIndicator : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setSagLevel (float level01) { sagLevel_ = level01; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        // Background track
        g.setColour (juce::Colour (0xff0d0d0d));
        g.fillRoundedRectangle (bounds, 3.0f);

        // Fill from bottom: full height = 1.0 (no sag), partial = sagging
        float normalized = juce::jlimit (0.0f, 1.0f, (sagLevel_ - 0.6f) / 0.4f);
        float fillH = bounds.getHeight() * normalized;
        auto fillBounds = bounds.withTop (bounds.getBottom() - fillH);

        // Color gradient: green (full) -> amber (moderate) -> red (heavy sag)
        juce::Colour fillColour;
        if (normalized > 0.7f)
            fillColour = juce::Colour (0xff4ade80);  // green
        else if (normalized > 0.4f)
            fillColour = juce::Colour (0xfffde047);  // amber
        else
            fillColour = juce::Colour (0xfff87171);  // red

        g.setColour (fillColour.withAlpha (0.9f));
        g.fillRoundedRectangle (fillBounds, 2.0f);

        // Subtle glow
        g.setColour (fillColour.withAlpha (0.2f));
        g.fillRoundedRectangle (fillBounds.expanded (1.0f), 3.0f);

        // Border
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (bounds, 3.0f, 0.5f);
    }

private:
    float sagLevel_ = 1.0f;
};

// IR waveform thumbnail display
class IRWaveformDisplay : public juce::Component
{
public:
    void setThumbnail (const std::array<float, 128>& data, bool ready)
    {
        if (ready)
            thumbnail_ = data;
        ready_ = ready;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        // Background
        g.setColour (juce::Colour (0xff0d0d0d));
        g.fillRoundedRectangle (bounds, 3.0f);

        if (!ready_)
        {
            g.setColour (juce::Colour (0xff555555));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("No IR", bounds, juce::Justification::centred);
            return;
        }

        // Draw waveform
        juce::Path path;
        float w = bounds.getWidth();
        float h = bounds.getHeight();
        float x0 = bounds.getX();
        float y0 = bounds.getY();

        path.startNewSubPath (x0, y0 + h);
        for (int i = 0; i < 128; ++i)
        {
            float x = x0 + (static_cast<float>(i) / 127.0f) * w;
            float y = y0 + h - thumbnail_[static_cast<size_t>(i)] * h * 0.9f;
            path.lineTo (x, y);
        }
        path.lineTo (x0 + w, y0 + h);
        path.closeSubPath();

        // Fill
        g.setColour (juce::Colour (0xff4a9ed6).withAlpha (0.3f));
        g.fillPath (path);

        // Stroke
        g.setColour (juce::Colour (0xff4a9ed6).withAlpha (0.7f));
        g.strokePath (path, juce::PathStrokeType (1.0f));

        // Border
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (bounds, 3.0f, 0.5f);
    }

private:
    std::array<float, 128> thumbnail_ {};
    bool ready_ = false;
};

class DuskAmpEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit DuskAmpEditor (DuskAmpProcessor&);
    ~DuskAmpEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showSupportersPanel();
    void hideSupportersPanel();
    DuskAmpProcessor& processorRef;
    DuskAmpLookAndFeel lnf_;
    ScalableEditorHelper scaler_;

    // Mode selector
    std::unique_ptr<AmpModeSelector> modeSelector_;

    // Preset browser
    juce::ComboBox presetBox_;
    std::unique_ptr<UserPresetManager> userPresetManager_;
    juce::TextButton savePresetButton_;
    juce::TextButton deletePresetButton_;
    void saveUserPreset();
    void loadUserPreset (const juce::String& name);
    void deleteUserPreset (const juce::String& name);
    void refreshPresetList();
    void updateDeleteButtonVisibility();

    // -- INPUT section --
    KnobWithLabel inputGain_;
    KnobWithLabel gateThreshold_;
    KnobWithLabel gateRelease_;

    // -- AMP section --
    KnobWithLabel drive_;
    SagIndicator sagIndicator_;
    juce::Label sagLabel_;
    // Amp model selector (Round / Chime / Punch)
    juce::ComboBox ampModelBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampModelAttachment_;

    // -- TONE section --
    KnobWithLabel bass_;
    KnobWithLabel mid_;
    KnobWithLabel treble_;

    // -- POWER AMP section --
    juce::ToggleButton powerAmpEnabled_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAmpEnabledAttachment_;
    KnobWithLabel presence_;
    KnobWithLabel resonance_;

    // -- CABINET section --
    juce::ToggleButton cabEnabled_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabEnabledAttachment_;
    KnobWithLabel cabMix_;
    KnobWithLabel cabHiCut_;
    KnobWithLabel cabLoCut_;
    juce::ToggleButton cabAutoGain_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabAutoGainAttachment_;
    CabBrowser cabBrowser_;
    IRWaveformDisplay irWaveform_;

    // -- EFFECTS section --
    juce::ToggleButton delayEnabled_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> delayEnabledAttachment_;
    KnobWithLabel delayTime_;
    KnobWithLabel delayFeedback_;
    KnobWithLabel delayMix_;
    juce::ToggleButton reverbEnabled_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverbEnabledAttachment_;
    KnobWithLabel reverbMix_;
    KnobWithLabel reverbDecay_;

    // -- NAM browser + levels --
    NAMBrowser namBrowser_;
    KnobWithLabel namInputLevel_;
    KnobWithLabel namOutputLevel_;

    // -- OUTPUT --
    KnobWithLabel outputLevel_;

    // Oversampling selector
    juce::ComboBox oversamplingBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment_;

    // Level meters
    LEDMeter inputMeter_  { LEDMeter::Vertical };
    LEDMeter outputMeter_ { LEDMeter::Vertical };

    // Supporters
    std::unique_ptr<SupportersOverlay> supportersOverlay_;
    juce::Rectangle<int> titleClickArea_;

    // Stored group box bounds (set in resized, drawn in paint)
    juce::Rectangle<int> inputGroupBounds_, outputGroupBounds_;
    juce::Rectangle<int> centerTopBounds_, centerMidBounds_, centerBotBounds_;
    juce::Rectangle<int> cabGroupBounds_, fxGroupBounds_;
    bool layoutIsNamMode_ = false;
    int cachedAmpModel_ = -1;  // tracks model changes for UI theme updates

    // Tooltip
    juce::TooltipWindow tooltipWindow_ { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskAmpEditor)
};

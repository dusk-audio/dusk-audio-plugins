/*
  ==============================================================================

    Convolution Reverb - IR Waveform Display
    Waveform visualization with envelope overlay and EQ curve view
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "EnvelopeProcessor.h"

// Display mode enum
enum class DisplayMode { IRWaveform, EQCurve };

class IRWaveformDisplay : public juce::Component,
                          private juce::Timer
{
public:
    IRWaveformDisplay();
    ~IRWaveformDisplay() override;

    // Display mode toggle
    void setDisplayMode(DisplayMode mode);
    DisplayMode getDisplayMode() const { return displayMode; }

    // Callback for mode changes
    std::function<void(DisplayMode)> onDisplayModeChanged;

    // Set the IR waveform data
    void setIRWaveform(const juce::AudioBuffer<float>& ir, double sampleRate);
    void clearWaveform();

    // Update envelope visualization parameters
    void setEnvelopeParameters(float attack, float decay, float length);

    // Set IR offset (0-1)
    void setIROffset(float offset);

    // Set filter envelope parameters for visualization
    void setFilterEnvelope(bool enabled, float initFreq, float endFreq, float attack);

    // Set whether the IR is reversed
    void setReversed(bool isReversed);

    // Playback position indicator (0.0 to 1.0)
    void setPlaybackPosition(float position);

    // Set colors
    void setWaveformColour(juce::Colour colour) { waveformColour = colour; repaint(); }
    void setEnvelopeColour(juce::Colour colour) { envelopeColour = colour; repaint(); }
    void setBackgroundColour(juce::Colour colour) { backgroundColour = colour; repaint(); }
    void setGridColour(juce::Colour colour) { gridColour = colour; repaint(); }

    // EQ curve parameters (for EQ view mode)
    void setEQParameters(float hpfFreq, float lpfFreq,
                         float lowGain, float loMidGain, float hiMidGain, float highGain);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void rebuildWaveformPath();
    void rebuildEnvelopePath();
    void drawTimeGrid(juce::Graphics& g);
    void drawEQCurve(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawModeToggle(juce::Graphics& g);
    float calculateEQResponse(float frequencyHz);

    // Display mode
    DisplayMode displayMode = DisplayMode::IRWaveform;
    juce::Rectangle<int> irToggleBounds;
    juce::Rectangle<int> eqToggleBounds;

    // IR data
    juce::AudioBuffer<float> irBuffer;
    double irSampleRate = 44100.0;

    // Paths for drawing
    juce::Path waveformPath;
    juce::Path envelopePath;

    // Envelope parameters
    float attackParam = 0.0f;
    float decayParam = 1.0f;
    float lengthParam = 1.0f;
    float irOffsetParam = 0.0f;
    bool reversed = false;

    // Filter envelope visualization
    bool filterEnvEnabled = false;
    float filterEnvInitFreq = 20000.0f;
    float filterEnvEndFreq = 2000.0f;
    float filterEnvAttack = 0.3f;
    juce::Path filterEnvPath;

    // Playback position
    float playbackPosition = 0.0f;

    // State
    bool needsRepaint = true;
    bool hasWaveform = false;

    // Colors
    juce::Colour waveformColour{0xff5588cc};
    juce::Colour envelopeColour{0xffcc8855};
    juce::Colour gridColour{0xff3a3a3a};
    juce::Colour backgroundColour{0xff1a1a1a};
    juce::Colour positionColour{0xffff8888};
    juce::Colour textColour{0xff909090};
    juce::Colour irOffsetColour{0xff88ff88};     // Green for IR offset
    juce::Colour filterEnvColour{0xffaa66ff};    // Purple for filter envelope
    juce::Colour accentColour{0xff4a9eff};       // Accent blue for EQ curve

    // EQ parameters for EQ curve view (fixed frequencies for this reverb)
    float eqHpfFreq = 20.0f;
    float eqLpfFreq = 20000.0f;
    float eqLowGain = 0.0f;      // 100 Hz
    float eqLoMidGain = 0.0f;    // 600 Hz
    float eqHiMidGain = 0.0f;    // 3000 Hz
    float eqHighGain = 0.0f;     // 8000 Hz

    // Fixed EQ band frequencies
    static constexpr float lowFreq = 100.0f;
    static constexpr float loMidFreq = 600.0f;
    static constexpr float hiMidFreq = 3000.0f;
    static constexpr float highFreq = 8000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRWaveformDisplay)
};

#pragma once

#include <JuceHeader.h>
#include <array>
#include "EQBand.h"

//==============================================================================
/**
    FFT Spectrum Analyzer Component for Multi-Q

    Features:
    - Real-time spectrum display with configurable resolution
    - Peak and RMS display modes
    - Logarithmic frequency scale (20 Hz - 20 kHz)
    - Adjustable decay rate
    - Pre/Post EQ display option
*/
class FFTAnalyzer : public juce::Component
{
public:
    FFTAnalyzer();
    ~FFTAnalyzer() override = default;

    void paint(juce::Graphics& g) override;

    // Pass mouse events through to parent (EQGraphicDisplay handles interaction)
    bool hitTest(int /*x*/, int /*y*/) override { return false; }

    // Update analyzer with new magnitude data
    void updateMagnitudes(const std::array<float, 2048>& magnitudes);

    // Set display parameters
    void setDisplayRange(float minDB, float maxDB);
    void setFrequencyRange(float minHz, float maxHz);
    void setFillColor(juce::Colour color) { fillColor = color; }
    void setLineColor(juce::Colour color) { lineColor = color; }
    void setEnabled(bool enabled) { analyzerEnabled = enabled; repaint(); }
    void setShowPeakHold(bool show) { showPeakHold = show; }

    // Get frequency at x position (for interaction)
    float getFrequencyAtX(float x) const;

    // Get x position for frequency
    float getXForFrequency(float freq) const;

    // Get y position for dB value
    float getYForDB(float dB) const;

    // Get dB value at y position
    float getDBAtY(float y) const;

private:
    std::array<float, 2048> currentMagnitudes{};
    std::array<float, 2048> peakHoldMagnitudes{};

    float minDisplayDB = -60.0f;
    float maxDisplayDB = 12.0f;
    float minFrequency = 20.0f;
    float maxFrequency = 20000.0f;

    juce::Colour fillColor = juce::Colour(0x40888888);
    juce::Colour lineColor = juce::Colour(0xFFAAAAAA);

    bool analyzerEnabled = true;
    bool showPeakHold = false;

    // Smooth the magnitude path for display
    juce::Path createMagnitudePath() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFTAnalyzer)
};

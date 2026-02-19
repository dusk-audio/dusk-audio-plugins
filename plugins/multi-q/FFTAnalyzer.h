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
    - Spectrum smoothing (temporal and spatial)
*/
class FFTAnalyzer : public juce::Component
{
public:
    // Spectrum smoothing modes (Professional)
    enum class SmoothingMode
    {
        Off = 0,      // Raw FFT data, no smoothing
        Light,        // Subtle smoothing, fast response
        Medium,       // Balanced smoothing (default)
        Heavy         // Maximum smoothing, very smooth appearance
    };

    FFTAnalyzer();
    ~FFTAnalyzer() override = default;

    void paint(juce::Graphics& g) override;

    // Pass mouse events through to parent (EQGraphicDisplay handles interaction)
    bool hitTest(int /*x*/, int /*y*/) override { return false; }

    // Update analyzer with new magnitude data
    void updateMagnitudes(const std::array<float, 2048>& magnitudes);

    // Update pre-EQ magnitude data (for dual spectrum overlay)
    void updatePreMagnitudes(const std::array<float, 2048>& magnitudes);

    // Set display parameters
    void setDisplayRange(float minDB, float maxDB);
    void setFrequencyRange(float minHz, float maxHz);
    void setFillColor(juce::Colour color) { fillColor = color; }
    void setLineColor(juce::Colour color) { lineColor = color; }
    void setEnabled(bool enabled) { analyzerEnabled = enabled; repaint(); }
    void setShowPeakHold(bool show) { showPeakHold = show; }

    // Pre-EQ spectrum overlay
    void setShowPreSpectrum(bool show) { showPreSpectrum = show; repaint(); }
    bool isPreSpectrumVisible() const { return showPreSpectrum; }
    void setPreFillColor(juce::Colour color) { preFillColor = color; }
    void setPreLineColor(juce::Colour color) { preLineColor = color; }

    // Smoothing control
    void setSmoothingMode(SmoothingMode mode) { smoothingMode = mode; }
    SmoothingMode getSmoothingMode() const { return smoothingMode; }

    // Spectrum freeze (captures current spectrum as reference)
    void toggleFreeze()
    {
        if (spectrumFrozen)
        {
            // Unfreeze - clear frozen data
            spectrumFrozen = false;
        }
        else
        {
            // Freeze - capture current smoothed magnitudes
            frozenMagnitudes = smoothedMagnitudes;
            spectrumFrozen = true;
        }
        repaint();
    }

    bool isFrozen() const { return spectrumFrozen; }

    void clearFrozen()
    {
        spectrumFrozen = false;
        std::fill(frozenMagnitudes.begin(), frozenMagnitudes.end(), -100.0f);
        repaint();
    }

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
    std::array<float, 2048> smoothedMagnitudes{};  // Temporally smoothed values
    std::array<float, 2048> peakHoldMagnitudes{};
    std::array<float, 2048> frozenMagnitudes{};    // Frozen spectrum for reference

    float minDisplayDB = -60.0f;
    float maxDisplayDB = 12.0f;
    float minFrequency = 20.0f;
    float maxFrequency = 20000.0f;

    juce::Colour fillColor = juce::Colour(0x40888888);
    juce::Colour lineColor = juce::Colour(0xFFAAAAAA);

    // Pre-EQ spectrum overlay
    std::array<float, 2048> preSmoothedMagnitudes{};
    bool showPreSpectrum = false;
    juce::Colour preFillColor = juce::Colour(0x20997755);   // Warm muted orange
    juce::Colour preLineColor = juce::Colour(0x40aa8855);   // Warm muted line

    bool analyzerEnabled = true;
    bool showPeakHold = false;
    bool spectrumFrozen = false;  // True when spectrum is frozen for reference

    // Smoothing settings
    SmoothingMode smoothingMode = SmoothingMode::Medium;

    // Get smoothing coefficients based on mode
    float getTemporalSmoothingCoeff() const;
    int getSpatialSmoothingWidth() const;

    // Apply spatial smoothing (averaging neighboring bins)
    void applySpatialSmoothing(std::array<float, 2048>& magnitudes) const;

    // Smooth the magnitude path for display
    juce::Path createMagnitudePath() const;
    juce::Path createMagnitudePath(const std::array<float, 2048>& mags) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFTAnalyzer)
};

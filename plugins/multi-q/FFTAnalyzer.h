#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "EQBand.h"

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
    // Spectrum smoothing modes
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
    void setFillColor(juce::Colour color) { fillColorARGB.store(color.getARGB(), std::memory_order_relaxed); }
    void setLineColor(juce::Colour color) { lineColorARGB.store(color.getARGB(), std::memory_order_relaxed); }
    void setEnabled(bool enabled) { analyzerEnabled.store(enabled, std::memory_order_relaxed); repaint(); }
    void setShowPeakHold(bool show) { showPeakHold.store(show, std::memory_order_relaxed); repaint(); }

    // Pre-EQ spectrum overlay
    void setShowPreSpectrum(bool show) { showPreSpectrum.store(show, std::memory_order_relaxed); repaint(); }
    bool isPreSpectrumVisible() const { return showPreSpectrum.load(std::memory_order_relaxed); }
    void setPreFillColor(juce::Colour color) { preFillColorARGB.store(color.getARGB(), std::memory_order_relaxed); }
    void setPreLineColor(juce::Colour color) { preLineColorARGB.store(color.getARGB(), std::memory_order_relaxed); }

    // Smoothing control
    void setSmoothingMode(SmoothingMode mode) { smoothingMode.store(mode, std::memory_order_relaxed); }
    SmoothingMode getSmoothingMode() const { return smoothingMode.load(std::memory_order_relaxed); }

    // Spectrum freeze (captures current spectrum as reference)
    void toggleFreeze();

    bool isFrozen() const { return spectrumFrozen.load(std::memory_order_relaxed); }

    void clearFrozen()
    {
        {
            juce::SpinLock::ScopedLockType lock(magnitudeLock);
            spectrumFrozen.store(false, std::memory_order_relaxed);
            std::fill(frozenMagnitudes.begin(), frozenMagnitudes.end(), -100.0f);
        }
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
    juce::SpinLock magnitudeLock;  // Protects magnitude arrays (audio→GUI)
    std::array<float, 2048> currentMagnitudes{};
    std::array<float, 2048> smoothedMagnitudes{};  // Temporally smoothed values
    std::array<float, 2048> peakHoldMagnitudes{};
    std::array<float, 2048> frozenMagnitudes{};    // Frozen spectrum for reference

    float minDisplayDB = -60.0f;
    float maxDisplayDB = 12.0f;
    float minFrequency = 20.0f;
    float maxFrequency = 20000.0f;

    std::atomic<uint32_t> fillColorARGB{0x40888888};
    std::atomic<uint32_t> lineColorARGB{0xFFAAAAAA};

    // Pre-EQ spectrum overlay
    std::array<float, 2048> preSmoothedMagnitudes{};
    std::atomic<bool> showPreSpectrum{false};
    std::atomic<uint32_t> preFillColorARGB{0x20997755};   // Warm muted orange
    std::atomic<uint32_t> preLineColorARGB{0x40aa8855};   // Warm muted line

    std::atomic<bool> analyzerEnabled{true};
    std::atomic<bool> showPeakHold{false};
    std::atomic<bool> spectrumFrozen{false};  // True when spectrum is frozen for reference

    // Peak hold decay: dB/sec falloff rate
    float peakDecayRateDbPerSec = 20.0f;
    std::atomic<double> lastPeakDecayTime{0.0};  // seconds (from juce::Time::getMillisecondCounterHiRes)

    // Smoothing settings
    std::atomic<SmoothingMode> smoothingMode{SmoothingMode::Medium};

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

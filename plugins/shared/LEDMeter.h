#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
 * Professional LED-style level meter component
 * Used for input/output level metering in audio plugins
 *
 * Features:
 * - Color-coded LEDs (green/yellow/orange/red)
 * - Vertical or horizontal orientation
 * - Glow effects and highlights for realistic LED appearance
 * - -60dB to +6dB range
 * - VU-style ballistics (smooth attack/release)
 * - Peak hold indicator with configurable hold time
 * - Stereo mode: displays L/R channels side-by-side
 */
class LEDMeter : public juce::Component
{
public:
    enum Orientation { Vertical, Horizontal };

    explicit LEDMeter(Orientation orient = Vertical);

    /** Set the current level in dB (-60 to +6 dB) - applies VU ballistics
     *  For mono mode or when stereo levels are identical */
    void setLevel(float newLevel);

    /** Set stereo levels in dB (-60 to +6 dB) - applies VU ballistics
     *  Only works when stereo mode is enabled */
    void setStereoLevels(float leftLevel, float rightLevel);

    /** Enable/disable stereo mode (shows L and R as separate bars) */
    void setStereoMode(bool enabled) { stereoMode = enabled; repaint(); }

    /** Check if stereo mode is enabled */
    bool isStereoMode() const { return stereoMode; }

    /** Set the sample rate for accurate ballistics timing */
    void setSampleRate(double sampleRate);

    /** Set the UI refresh rate (how often setLevel is called per second) */
    void setRefreshRate(float rateHz);

    /** Enable/disable peak hold indicator */
    void setPeakHoldEnabled(bool enabled) { peakHoldEnabled = enabled; }

    /** Set peak hold time in seconds (default 1.5s) */
    void setPeakHoldTime(float seconds) { peakHoldTimeSeconds = seconds; }

    /** Paint the LED meter */
    void paint(juce::Graphics& g) override;

private:
    Orientation orientation;
    bool stereoMode = false;          // When true, shows L/R side-by-side

    // Mono/combined levels
    float currentLevel = -60.0f;      // Raw input level
    float displayLevel = -60.0f;      // Smoothed display level (with ballistics)

    // Stereo levels (L/R)
    float currentLevelL = -60.0f;     // Raw left level
    float currentLevelR = -60.0f;     // Raw right level
    float displayLevelL = -60.0f;     // Smoothed left display level
    float displayLevelR = -60.0f;     // Smoothed right display level

    int numLEDs = 12;

    // VU Ballistics parameters
    // Standard VU: ~300ms to reach 99% of target (integration time)
    // We use attack/release coefficients based on refresh rate
    float attackCoeff = 0.0f;   // How fast meter rises
    float releaseCoeff = 0.0f;  // How fast meter falls
    float refreshRateHz = 30.0f;  // UI refresh rate

    // Peak hold parameters (mono)
    bool peakHoldEnabled = true;        // Peak hold on by default
    float peakHoldTimeSeconds = 1.5f;   // How long to hold peak (1.5 seconds)
    float peakLevel = -60.0f;           // Current peak hold level
    int peakHoldCounter = 0;            // Counts down from hold time
    int peakHoldSamples = 0;            // Number of UI frames to hold peak

    // Peak hold parameters (stereo L/R)
    float peakLevelL = -60.0f;
    float peakLevelR = -60.0f;
    int peakHoldCounterL = 0;
    int peakHoldCounterR = 0;

    void updateBallisticsCoefficients();

    /** Apply ballistics to a single channel
     *  Returns the new display level */
    float applyBallistics(float currentLevel, float displayLevel);

    /** Update peak hold for a channel
     *  Returns the new peak level and updates the counter */
    void updatePeakHold(float currentLevel, float displayLevel, float& peakLevel, int& peakHoldCounter);

    /** Get the color for a specific LED based on its position */
    juce::Colour getLEDColor(int ledIndex, int totalLEDs);

    /** Paint a single column of LEDs (used for both mono and stereo modes) */
    void paintVerticalColumn(juce::Graphics& g, juce::Rectangle<float> bounds,
                             float displayLevel, float peakLevel);

    /** Paint a single row of LEDs (horizontal orientation) */
    void paintHorizontalRow(juce::Graphics& g, juce::Rectangle<float> bounds,
                            float displayLevel, float peakLevel);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LEDMeter)
};

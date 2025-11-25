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
 */
class LEDMeter : public juce::Component
{
public:
    enum Orientation { Vertical, Horizontal };

    explicit LEDMeter(Orientation orient = Vertical);

    /** Set the current level in dB (-60 to +6 dB) */
    void setLevel(float newLevel);

    /** Paint the LED meter */
    void paint(juce::Graphics& g) override;

private:
    Orientation orientation;
    float currentLevel = -60.0f;
    int numLEDs = 12;

    /** Get the color for a specific LED based on its position */
    juce::Colour getLEDColor(int ledIndex, int totalLEDs);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LEDMeter)
};

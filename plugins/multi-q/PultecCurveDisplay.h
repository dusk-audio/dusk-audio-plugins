#pragma once

#include <JuceHeader.h>
#include "MultiQ.h"

//==============================================================================
/**
 * Pultec EQ Curve Display Component
 *
 * Displays frequency response graph for Pultec (Tube) mode showing:
 * - LF Boost and Atten curves (showing the famous "Pultec trick")
 * - HF Boost curve with bandwidth visualization
 * - HF Atten shelf curve
 * - Combined frequency response with vintage cream/gold styling
 * - Vintage-style grid with tube-era aesthetic
 */
class PultecCurveDisplay : public juce::Component,
                           private juce::Timer
{
public:
    explicit PultecCurveDisplay(MultiQ& processor);
    ~PultecCurveDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    MultiQ& audioProcessor;

    // Dark professional color scheme (matching Vintage Tube EQ style)
    static constexpr uint32_t lfBoostColor = 0xff60a0c0;    // Blue (low boost)
    static constexpr uint32_t lfAttenColor = 0xff4080a0;    // Darker blue (low cut)
    static constexpr uint32_t hfBoostColor = 0xff80c0e0;    // Light blue (high boost)
    static constexpr uint32_t hfAttenColor = 0xff5090b0;    // Muted blue (high cut)
    static constexpr uint32_t combinedColor = 0xffe0e0e0;   // Light gray/white
    static constexpr uint32_t gridColor = 0xff404040;       // Dark gray grid
    static constexpr uint32_t backgroundColor = 0xff2a2a2a; // Dark gray background

    // Frequency range
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;
    static constexpr float minDB = -25.0f;
    static constexpr float maxDB = 25.0f;

    // Cached parameter values for change detection
    struct CachedParams {
        // LF Section
        float lfBoostGain = 0.0f;
        float lfBoostFreq = 60.0f;
        float lfAttenGain = 0.0f;

        // HF Boost Section
        float hfBoostGain = 0.0f;
        float hfBoostFreq = 8000.0f;
        float hfBoostBandwidth = 0.5f;

        // HF Atten Section
        float hfAttenGain = 0.0f;
        float hfAttenFreq = 10000.0f;

        // Global
        float tubeDrive = 0.3f;
    } cachedParams;

    bool needsRepaint = true;

    // Helper functions
    float freqToX(float freq, const juce::Rectangle<float>& area) const;
    float xToFreq(float x, const juce::Rectangle<float>& area) const;
    float dbToY(float db, const juce::Rectangle<float>& area) const;

    void drawVintageGrid(juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawBandCurve(juce::Graphics& g, const juce::Rectangle<float>& area,
                       juce::Colour color, std::function<float(float)> getMagnitude);
    void drawCombinedCurve(juce::Graphics& g, const juce::Rectangle<float>& area);

    // Filter response calculations (matching Pultec characteristics)
    float calculateLFBoostResponse(float freq) const;
    float calculateLFAttenResponse(float freq) const;
    float calculateHFBoostResponse(float freq) const;
    float calculateHFAttenResponse(float freq) const;
    float calculateCombinedResponse(float freq) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PultecCurveDisplay)
};

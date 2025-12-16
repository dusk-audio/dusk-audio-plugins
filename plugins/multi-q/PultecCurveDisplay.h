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

    // Vintage Pultec color scheme
    static constexpr uint32_t lfBoostColor = 0xffc49a6c;    // Warm brown (low boost)
    static constexpr uint32_t lfAttenColor = 0xff8c6444;    // Darker brown (low cut)
    static constexpr uint32_t hfBoostColor = 0xffcaa864;    // Gold (high boost)
    static constexpr uint32_t hfAttenColor = 0xff7a6a5a;    // Muted gold (high cut)
    static constexpr uint32_t combinedColor = 0xfff0e8d8;   // Cream/ivory
    static constexpr uint32_t gridColor = 0xff3a3530;       // Warm dark grid
    static constexpr uint32_t backgroundColor = 0xff201c18; // Dark brown background

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

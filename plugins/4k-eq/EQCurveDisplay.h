#pragma once

#include <JuceHeader.h>
#include "FourKEQ.h"

//==============================================================================
/**
 * EQ Curve Display Component
 *
 * Displays frequency response graph showing:
 * - Individual band curves in their respective colors
 * - Combined frequency response as white/cream line
 * - Grid lines at standard frequencies
 */
class EQCurveDisplay : public juce::Component,
                       private juce::Timer
{
public:
    explicit EQCurveDisplay(FourKEQ& processor);
    ~EQCurveDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    FourKEQ& audioProcessor;

    // Color scheme for bands
    static constexpr uint32_t bandLFColor = 0xffc44444;    // Red
    static constexpr uint32_t bandLMFColor = 0xffc47a44;   // Orange
    static constexpr uint32_t bandHMFColor = 0xff5c9a5c;   // Green
    static constexpr uint32_t bandHFColor = 0xff4a7a9a;    // Blue
    static constexpr uint32_t combinedColor = 0xffe8e0d0;  // Cream/white
    static constexpr uint32_t gridColor = 0xff3a3a3a;      // Subtle grid
    static constexpr uint32_t backgroundColor = 0xff1a1a1a; // Dark background

    // Frequency range
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;
    static constexpr float minDB = -25.0f;
    static constexpr float maxDB = 25.0f;

    // Cached parameter values for change detection
    struct CachedParams {
        float hpfFreq = 20.0f;
        bool hpfEnabled = false;
        float lpfFreq = 20000.0f;
        bool lpfEnabled = false;
        float lfGain = 0.0f, lfFreq = 100.0f;
        float lmGain = 0.0f, lmFreq = 600.0f, lmQ = 0.7f;
        float hmGain = 0.0f, hmFreq = 2000.0f, hmQ = 0.7f;
        float hfGain = 0.0f, hfFreq = 8000.0f;
        bool lfBell = false, hfBell = false;
        bool isBlack = false;
    } cachedParams;

    bool needsRepaint = true;

    // Helper functions
    float freqToX(float freq, const juce::Rectangle<float>& area) const;
    float xToFreq(float x, const juce::Rectangle<float>& area) const;
    float dbToY(float db, const juce::Rectangle<float>& area) const;

    void drawGrid(juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawBandCurve(juce::Graphics& g, const juce::Rectangle<float>& area,
                       juce::Colour color, std::function<float(float)> getMagnitude);
    void drawCombinedCurve(juce::Graphics& g, const juce::Rectangle<float>& area);

    // Filter response calculations
    float calculateHPFResponse(float freq) const;
    float calculateLPFResponse(float freq) const;
    float calculateLFResponse(float freq) const;
    float calculateLMFResponse(float freq) const;
    float calculateHMFResponse(float freq) const;
    float calculateHFResponse(float freq) const;
    float calculateCombinedResponse(float freq) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQCurveDisplay)
};

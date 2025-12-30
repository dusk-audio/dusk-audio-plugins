#pragma once

#include <JuceHeader.h>
#include "MultiQ.h"
#include "FFTAnalyzer.h"

//==============================================================================
/**
 * British EQ Curve Display Component
 *
 * Displays frequency response graph for British (4K-EQ style) mode showing:
 * - Individual band curves in their respective colors
 * - Combined frequency response as white/cream line
 * - Grid lines at standard frequencies
 * - FFT analyzer overlay (when enabled)
 */
class BritishEQCurveDisplay : public juce::Component,
                               private juce::Timer
{
public:
    explicit BritishEQCurveDisplay(MultiQ& processor);
    ~BritishEQCurveDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    // Show/hide FFT analyzer
    void setAnalyzerVisible(bool visible);

private:
    MultiQ& audioProcessor;

    // FFT Analyzer component (child component, drawn behind EQ curves)
    std::unique_ptr<FFTAnalyzer> analyzer;

    // Color scheme for bands (matching 4K-EQ)
    static constexpr uint32_t bandLFColor = 0xffc44444;    // Red
    static constexpr uint32_t bandLMFColor = 0xffc47a44;   // Orange
    static constexpr uint32_t bandHMFColor = 0xff5c9a5c;   // Green
    static constexpr uint32_t bandHFColor = 0xff4a7a9a;    // Blue
    static constexpr uint32_t filterColor = 0xffb8860b;    // Brown/orange for HPF/LPF
    static constexpr uint32_t combinedColor = 0xffe8e0d0;  // Cream/white
    static constexpr uint32_t gridColor = 0xff3a3a3a;      // Subtle grid
    static constexpr uint32_t backgroundColor = 0xff1a1a1a; // Dark background

    // Frequency range
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;
    static constexpr float minDB = -25.0f;
    static constexpr float maxDB = 25.0f;

    // Graph area margins
    static constexpr float graphLeftMargin = 30.0f;    // Space for dB labels
    static constexpr float graphBottomMargin = 18.0f;  // Space for frequency labels
    static constexpr float graphTopMargin = 6.0f;
    static constexpr float graphRightMargin = 6.0f;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BritishEQCurveDisplay)
};

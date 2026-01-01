#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../shared/LEDMeter.h"

//==============================================================================
/**
    Meter Panel Component

    Displays:
    - Stereo correlation meter
    - True peak meters (L/R)
    - LUFS display (Momentary, Short-term, Integrated, LRA)
    - Output level meters
*/
class MeterPanel : public juce::Component
{
public:
    MeterPanel();
    ~MeterPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
    // Update meter values
    void setCorrelation(float correlation);
    void setTruePeakL(float dbTP);
    void setTruePeakR(float dbTP);
    void setClipping(bool clipping);

    void setMomentaryLUFS(float lufs);
    void setShortTermLUFS(float lufs);
    void setIntegratedLUFS(float lufs);
    void setLoudnessRange(float lra);

    void setOutputLevelL(float db);
    void setOutputLevelR(float db);
    void setRmsLevel(float db);

private:
    void drawCorrelationMeter(juce::Graphics& g, juce::Rectangle<int> area);
    void drawTruePeakMeter(juce::Graphics& g, juce::Rectangle<int> area);
    void drawLUFSMeter(juce::Graphics& g, juce::Rectangle<int> area);

    juce::String formatLUFS(float lufs) const;
    juce::String formatDB(float db) const;

    //==========================================================================
    // Values
    float correlation = 1.0f;
    float truePeakL = -100.0f;
    float truePeakR = -100.0f;
    bool clipping = false;

    float momentaryLUFS = -100.0f;
    float shortTermLUFS = -100.0f;
    float integratedLUFS = -100.0f;
    float loudnessRange = 0.0f;

    float outputLevelL = -100.0f;
    float outputLevelR = -100.0f;
    float rmsLevel = -100.0f;

    // Panel areas
    juce::Rectangle<int> correlationArea;
    juce::Rectangle<int> truePeakArea;
    juce::Rectangle<int> lufsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterPanel)
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Compact meter strip showing correlation, true peak, and LUFS readouts.
*/
class MeterPanel : public juce::Component
{
public:
    MeterPanel() = default;
    ~MeterPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
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
    juce::String formatLUFS(float lufs) const;
    juce::String formatDB(float db) const;

    float correlation_ = 1.0f;
    float truePeakL_ = -100.0f;
    float truePeakR_ = -100.0f;
    bool clipping_ = false;

    float momentaryLUFS_ = -100.0f;
    float shortTermLUFS_ = -100.0f;
    float integratedLUFS_ = -100.0f;
    float loudnessRange_ = 0.0f;

    juce::Rectangle<int> correlationArea_;
    juce::Rectangle<int> truePeakArea_;
    juce::Rectangle<int> lufsArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterPanel)
};

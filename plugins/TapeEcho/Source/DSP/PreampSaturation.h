#pragma once

#include <JuceHeader.h>
#include <cmath>

class PreampSaturation
{
public:
    PreampSaturation() = default;
    ~PreampSaturation() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void setInputGain(float gain);
    void setSaturationAmount(float amount);
    void setCharacter(float character); // 0 = clean, 1 = warm/vintage

    float processSample(float input);

private:
    float inputGain = 1.0f;
    float saturationAmount = 0.0f;
    float character = 0.5f;

    // DC blocking filter
    float dcBlockerX1 = 0.0f;
    float dcBlockerY1 = 0.0f;
    const float dcBlockerCoeff = 0.995f;

    // Oversampling for better saturation quality
    juce::dsp::Oversampling<float> oversampler{1, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR};

    float sampleRate = 44100.0f;

    // Different saturation curves
    float processTanhSaturation(float input);
    float processAsymmetricSaturation(float input);
    float processVintageSaturation(float input);

    float processDCBlocker(float input);
};
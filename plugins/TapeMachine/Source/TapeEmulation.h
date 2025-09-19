#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

class TapeEmulation
{
public:
    TapeEmulation();
    ~TapeEmulation() = default;

    enum TapeMachine
    {
        StuderA800 = 0,
        AmpexATR102,
        Blend
    };

    enum TapeSpeed
    {
        Speed_7_5_IPS = 0,
        Speed_15_IPS,
        Speed_30_IPS
    };

    enum TapeType
    {
        Ampex456 = 0,
        GP9,
        BASF911
    };

    void prepare(double sampleRate);
    void reset();

    float processSample(float input, TapeMachine machine, TapeSpeed speed, TapeType type);

private:
    double currentSampleRate = 44100.0;

    struct MachineCharacteristics
    {
        float lowFreqBoost;
        float highFreqRoll;
        float saturationCurve;
        float compressionRatio;
        float harmonicProfile;
    };

    struct TapeCharacteristics
    {
        float hysteresis;
        float coercivity;
        float retentivity;
        float saturationLevel;
        float noiseFloor;
    };

    juce::dsp::IIR::Filter<float> preEmphasisFilter;
    juce::dsp::IIR::Filter<float> deEmphasisFilter;
    juce::dsp::IIR::Filter<float> headBumpFilter;
    juce::dsp::IIR::Filter<float> tapeResponseFilter;

    float previousInput = 0.0f;
    float previousOutput = 0.0f;
    float hysteresisState = 0.0f;

    std::array<float, 4> delayLine = {0.0f, 0.0f, 0.0f, 0.0f};
    int delayIndex = 0;

    MachineCharacteristics getMachineCharacteristics(TapeMachine machine);
    TapeCharacteristics getTapeCharacteristics(TapeType type);
    void updateFilters(TapeSpeed speed, TapeMachine machine);

    float applyHysteresis(float input, float hysteresisAmount);
    float applyCrossoverDistortion(float input, float amount);
    float applyMagneticSaturation(float input, float saturationLevel, float coercivity);

    static constexpr float denormalPrevention = 1e-8f;
};
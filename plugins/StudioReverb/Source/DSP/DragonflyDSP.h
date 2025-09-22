#pragma once

#include <JuceHeader.h>
#include "freeverb/earlyref.hpp"
#include "freeverb/zrev2.hpp"
#include "freeverb/progenitor2.hpp"
#include "freeverb/nrev.hpp"
#include "freeverb/nrevb.hpp"
#include "freeverb/strev.hpp"
#include "freeverb/biquad.hpp"
#include "freeverb/utils.hpp"

enum class ReverbType
{
    Hall = 0,
    Room,
    Plate,
    Early
};

class DragonflyDSP
{
public:
    DragonflyDSP();
    ~DragonflyDSP();

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();
    void processBlock(juce::AudioBuffer<float>& buffer);

    void setReverbType(ReverbType type);
    void setDryLevel(float value);
    void setWetLevel(float value);
    void setSize(float value);
    void setPreDelay(float value);
    void setDamping(float value);
    void setLowCut(float value);
    void setHighCut(float value);
    void setDecay(float value);

private:
    double currentSampleRate = 44100.0;
    ReverbType currentType = ReverbType::Hall;

    // Parameters
    float dryLevel = 0.8f;
    float wetLevel = 0.2f;
    float roomSize = 0.5f;
    float preDelay = 0.0f;
    float damping = 0.5f;
    float lowCut = 50.0f;
    float highCut = 10000.0f;
    float decay = 1.0f;

    // Early reflections (shared by all types)
    std::unique_ptr<fv3::earlyref_f> early;

    // Late reverb models
    std::unique_ptr<fv3::zrev2_f> hallLate;
    std::unique_ptr<fv3::progenitor2_f> roomLate;
    std::unique_ptr<fv3::nrev_f> plateLate;

    // Input filters
    std::unique_ptr<fv3::iir_1st_f> inputHPF_L, inputHPF_R;
    std::unique_ptr<fv3::iir_1st_f> inputLPF_L, inputLPF_R;

    // Processing buffers
    static constexpr int BUFFER_SIZE = 256;
    std::vector<float> earlyOutL, earlyOutR;
    std::vector<float> lateInL, lateInR;
    std::vector<float> lateOutL, lateOutR;

    void updateParameters();
    void processHall(float* inputL, float* inputR, float* outputL, float* outputR, int numSamples);
    void processRoom(float* inputL, float* inputR, float* outputL, float* outputR, int numSamples);
    void processPlate(float* inputL, float* inputR, float* outputL, float* outputR, int numSamples);
    void processEarly(float* inputL, float* inputR, float* outputL, float* outputR, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DragonflyDSP)
};
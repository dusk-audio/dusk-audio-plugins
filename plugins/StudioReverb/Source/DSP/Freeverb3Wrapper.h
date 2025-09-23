#pragma once

#include <JuceHeader.h>
#include <memory>

// Freeverb3 includes
#include "freeverb/earlyref.hpp"
#include "freeverb/revmodel.hpp"
#include "freeverb/zrev.hpp"
#include "freeverb/progenitor.hpp"

class Freeverb3Wrapper
{
public:
    enum class ReverbType
    {
        EarlyReflections = 0,
        Room,
        Plate,
        Hall
    };

    Freeverb3Wrapper();
    ~Freeverb3Wrapper();

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();
    void processBlock(juce::AudioBuffer<float>& buffer);

    // Reverb type
    void setReverbType(ReverbType type);
    ReverbType getReverbType() const { return currentType; }

    // Common parameters
    void setRoomSize(float value);
    void setDamping(float value);
    void setPreDelay(float ms);
    void setDecayTime(float seconds);
    void setDiffusion(float value);
    void setModulation(float value);
    void setWetLevel(float value);
    void setDryLevel(float value);
    void setWidth(float value);

private:
    ReverbType currentType = ReverbType::Hall;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // Freeverb3 reverb instances
    std::unique_ptr<fv3::earlyref_f> earlyReflections;
    std::unique_ptr<fv3::revmodel_f> roomReverb;
    std::unique_ptr<fv3::progenitor_f> plateReverb;
    std::unique_ptr<fv3::zrev_f> hallReverb;

    // Common parameters
    float roomSize = 0.5f;
    float damping = 0.5f;
    float preDelay = 0.0f;
    float decayTime = 2.0f;
    float diffusion = 0.5f;
    float modulation = 0.0f;
    float wetLevel = 0.3f;
    float dryLevel = 0.7f;
    float width = 1.0f;

    // Processing buffers
    std::vector<float> leftBuffer;
    std::vector<float> rightBuffer;

    void updateParameters();
    void initializeReverbs();
};
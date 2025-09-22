#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>

class SimpleFreeverb
{
public:
    SimpleFreeverb();
    ~SimpleFreeverb() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();
    void processBlock(juce::AudioBuffer<float>& buffer);

    void setRoomSize(float value) { roomSize = value * 0.28f + 0.7f; updateDamping(); }
    void setDamping(float value) { damp = value * 0.4f; updateDamping(); }
    void setWetLevel(float value) { wet = value * 3.0f; }
    void setDryLevel(float value) { dry = value * 2.0f; }
    void setWidth(float value) { width = value; }

private:
    static constexpr int numCombs = 8;
    static constexpr int numAllpasses = 4;
    static constexpr float fixedGain = 0.015f;

    // Tuning values (at 44100Hz)
    static constexpr std::array<int, numCombs> combTuning = {
        1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617
    };

    static constexpr std::array<int, numAllpasses> allpassTuning = {
        556, 441, 341, 225
    };

    struct Comb {
        std::vector<float> buffer;
        int bufferSize = 0;
        float feedback = 0.0f;
        float filterstore = 0.0f;
        float damp1 = 0.0f;
        float damp2 = 0.0f;
        int bufidx = 0;

        void setSize(int size) {
            bufferSize = size;
            buffer.resize(size);
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            bufidx = 0;
        }

        void setFeedback(float val) { feedback = val; }
        void setDamp(float val) {
            damp1 = val;
            damp2 = 1.0f - val;
        }

        float process(float input) {
            float output = buffer[bufidx];
            filterstore = (output * damp2) + (filterstore * damp1);
            buffer[bufidx] = input + (filterstore * feedback);
            if (++bufidx >= bufferSize) bufidx = 0;
            return output;
        }

        void clear() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            filterstore = 0.0f;
        }
    };

    struct Allpass {
        std::vector<float> buffer;
        int bufferSize = 0;
        float feedback = 0.5f;
        int bufidx = 0;

        void setSize(int size) {
            bufferSize = size;
            buffer.resize(size);
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            bufidx = 0;
        }

        float process(float input) {
            float bufout = buffer[bufidx];
            float output = -input + bufout;
            buffer[bufidx] = input + (bufout * feedback);
            if (++bufidx >= bufferSize) bufidx = 0;
            return output;
        }

        void clear() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
    };

    std::array<Comb, numCombs> combL, combR;
    std::array<Allpass, numAllpasses> allpassL, allpassR;

    float roomSize = 0.5f;
    float damp = 0.5f;
    float wet = 1.0f;
    float dry = 0.0f;
    float width = 1.0f;
    double currentSampleRate = 44100.0;

    void updateDamping();
};
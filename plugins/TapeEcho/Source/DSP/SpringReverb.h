#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>

class SpringReverb
{
public:
    SpringReverb();
    ~SpringReverb() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void setDecayTime(float seconds);
    void setDamping(float amount);
    void setPreDelay(float ms);
    void setDiffusion(float amount);
    void setModulation(float depth, float rate);

    float processSample(float input, int channel);

private:
    static constexpr int NUM_DELAY_LINES = 4;
    static constexpr int NUM_ALLPASS = 6;

    struct DelayLine
    {
        std::vector<float> buffer;
        int writePosition = 0;
        int size = 0;
        float feedback = 0.0f;
        float damping = 0.0f;
        float lastOutput = 0.0f;

        void init(int sampleSize)
        {
            size = sampleSize;
            buffer.resize(size);
            reset();
        }

        void reset()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writePosition = 0;
            lastOutput = 0.0f;
        }

        float process(float input)
        {
            float output = buffer[writePosition];

            // Apply damping (simple lowpass)
            lastOutput = output * (1.0f - damping) + lastOutput * damping;

            // Write input + feedback to buffer
            buffer[writePosition] = input + lastOutput * feedback;

            writePosition++;
            if (writePosition >= size) writePosition = 0;

            return output;
        }
    };

    struct AllPassFilter
    {
        std::vector<float> buffer;
        int writePosition = 0;
        int size = 0;
        float gain = 0.5f;

        void init(int sampleSize)
        {
            size = sampleSize;
            buffer.resize(size);
            reset();
        }

        void reset()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writePosition = 0;
        }

        float process(float input)
        {
            float bufferedSample = buffer[writePosition];
            float output = -input + bufferedSample;
            buffer[writePosition] = input + bufferedSample * gain;

            writePosition++;
            if (writePosition >= size) writePosition = 0;

            return output;
        }
    };

    // Pre-delay
    std::vector<float> preDelayBufferL;
    std::vector<float> preDelayBufferR;
    int preDelayWritePos = 0;
    int preDelaySize = 0;
    float preDelayMs = 0.0f;

    // Parallel delay lines (comb filters)
    std::array<DelayLine, NUM_DELAY_LINES> delayLinesL;
    std::array<DelayLine, NUM_DELAY_LINES> delayLinesR;

    // Series allpass filters for diffusion
    std::array<AllPassFilter, NUM_ALLPASS> allpassL;
    std::array<AllPassFilter, NUM_ALLPASS> allpassR;

    // Modulation
    float modulationDepth = 0.0f;
    float modulationRate = 1.0f;
    float lfoPhase = 0.0f;

    // Parameters
    float decayTime = 2.0f;
    float damping = 0.5f;
    float diffusion = 0.7f;
    float sampleRate = 44100.0f;

    // Spring-specific character
    float springTension = 0.9f;
    float springDamping = 0.3f;
    juce::IIRFilter characterFilterL;
    juce::IIRFilter characterFilterR;

    void updateDelayTimes();
    float processSpringCharacter(float input, int channel);
};
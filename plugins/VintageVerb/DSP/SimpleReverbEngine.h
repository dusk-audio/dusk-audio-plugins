#pragma once
#include <JuceHeader.h>
#include <vector>
#include <array>

class SimpleReverbEngine
{
public:
    SimpleReverbEngine();
    ~SimpleReverbEngine() = default;

    void prepare(double sampleRate, int blockSize);
    void reset();
    void process(juce::AudioBuffer<float>& buffer);

    // Simple parameters
    void setRoomSize(float size) { roomSize = juce::jlimit(0.0f, 1.0f, size); updateParameters(); }
    void setDamping(float damp) { damping = juce::jlimit(0.0f, 1.0f, damp); updateParameters(); }
    void setWidth(float w) { width = juce::jlimit(0.0f, 1.0f, w); }
    void setMix(float m) { mix = juce::jlimit(0.0f, 1.0f, m); }

private:
    // Simple comb filter for reverb
    class CombFilter
    {
    public:
        CombFilter() = default;

        void setSize(int size)
        {
            if (size != bufferSize)
            {
                bufferSize = size;
                buffer.resize(size);
                reset();
            }
        }

        void reset()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            bufferIndex = 0;
            filterStore = 0.0f;
        }

        float process(float input, float feedback, float damp)
        {
            float output = buffer[bufferIndex];
            filterStore = (output * (1.0f - damp)) + (filterStore * damp);
            buffer[bufferIndex] = input + (filterStore * feedback);

            bufferIndex++;
            if (bufferIndex >= bufferSize)
                bufferIndex = 0;

            return output;
        }

    private:
        std::vector<float> buffer;
        int bufferSize = 0;
        int bufferIndex = 0;
        float filterStore = 0.0f;
    };

    // Simple allpass filter
    class AllpassFilter
    {
    public:
        AllpassFilter() = default;

        void setSize(int size)
        {
            if (size != bufferSize)
            {
                bufferSize = size;
                buffer.resize(size);
                reset();
            }
        }

        void reset()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            bufferIndex = 0;
        }

        float process(float input)
        {
            float bufferedValue = buffer[bufferIndex];
            float output = -input + bufferedValue;
            buffer[bufferIndex] = input + (bufferedValue * 0.5f);

            bufferIndex++;
            if (bufferIndex >= bufferSize)
                bufferIndex = 0;

            return output;
        }

    private:
        std::vector<float> buffer;
        int bufferSize = 0;
        int bufferIndex = 0;
    };

    void updateParameters();

    // 8 comb filters and 4 allpass filters per channel (Freeverb style)
    static constexpr int numCombs = 8;
    static constexpr int numAllpasses = 4;

    std::array<CombFilter, numCombs> combFiltersL;
    std::array<CombFilter, numCombs> combFiltersR;
    std::array<AllpassFilter, numAllpasses> allpassFiltersL;
    std::array<AllpassFilter, numAllpasses> allpassFiltersR;

    // Tuning values (in samples at 44100 Hz)
    const std::array<int, numCombs> combTunings = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    const std::array<int, numAllpasses> allpassTunings = {556, 441, 341, 225};
    const int stereoSpread = 23;

    float roomSize = 0.5f;
    float damping = 0.5f;
    float width = 1.0f;
    float mix = 0.5f;
    float feedback = 0.0f;
    float damp1 = 0.0f;
    float damp2 = 0.0f;

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleReverbEngine)
};
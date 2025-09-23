/*
  ==============================================================================

    DragonflyReverb.h
    Direct implementation of Dragonfly reverb algorithms using simplified Freeverb3

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <cmath>

class DragonflyReverb
{
public:
    enum class ReverbType {
        Room = 0,      // Progenitor2 algorithm
        Hall,          // Zrev2 algorithm
        Plate,         // Simplified plate algorithm
        EarlyReflections  // Early reflections only
    };

    DragonflyReverb();
    ~DragonflyReverb() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer);
    void reset();

    // Main controls
    void setReverbType(ReverbType type) { currentType = type; }
    ReverbType getReverbType() const { return currentType; }

    // Parameters
    void setDryWetMix(float mix) { wetMix = mix; dryMix = 1.0f - mix; }
    void setPreDelay(float ms);
    void setSize(float size);
    void setDecayTime(float seconds);
    void setDamping(float amount);
    void setDiffusion(float amount);
    void setWidth(float stereoWidth) { width = stereoWidth; }

    // Tone controls
    void setLowCut(float freq);
    void setHighCut(float freq);
    void setLowMultiplier(float mult) { lowMult = mult; }
    void setHighMultiplier(float mult) { highMult = mult; }

    // Mix controls
    void setEarlyMix(float mix) { earlyMix = mix; }
    void setLateMix(float mix) { lateMix = mix; }

    // Modulation
    void setModulationRate(float hz) { modRate = hz; }
    void setModulationDepth(float depth) { modDepth = depth; }

private:
    double sampleRate = 44100.0;
    ReverbType currentType = ReverbType::Hall;

    // Mix parameters
    float dryMix = 0.5f;
    float wetMix = 0.5f;
    float earlyMix = 0.5f;
    float lateMix = 0.5f;
    float width = 1.0f;

    // Tone parameters
    float lowMult = 1.0f;
    float highMult = 0.8f;
    float modRate = 0.5f;
    float modDepth = 0.0f;

    //==============================================================================
    // Simple delay line for pre-delay and reflections
    class DelayLine {
    public:
        void setMaxSize(int maxSamples);
        float read(float delaySamples) const;
        void write(float sample);
        void clear();

    private:
        std::vector<float> buffer;
        int writePos = 0;
        int size = 0;
    };

    //==============================================================================
    // Allpass filter for diffusion (simplified from Freeverb3)
    class AllpassFilter {
    public:
        void setSize(int samples);
        void setFeedback(float g);
        float process(float input);
        void clear();

    private:
        std::vector<float> buffer;
        int bufferSize = 0;
        int writePos = 0;
        float feedback = 0.5f;
    };

    //==============================================================================
    // Comb filter for reverb tail (simplified from Freeverb3)
    class CombFilter {
    public:
        void setSize(int samples);
        void setFeedback(float g);
        void setDamping(float d);
        float process(float input);
        void clear();

    private:
        std::vector<float> buffer;
        int bufferSize = 0;
        int writePos = 0;
        float feedback = 0.84f;
        float damping = 0.5f;
        float filterStore = 0.0f;
    };

    //==============================================================================
    // Early Reflections (simplified from Freeverb3 earlyref)
    class EarlyReflections {
    public:
        void prepare(double sampleRate);
        void setRoomSize(float size);
        void process(const float* inputL, const float* inputR,
                    float* outputL, float* outputR, int numSamples);
        void clear();

    private:
        static constexpr int numTaps = 18;
        std::array<DelayLine, 2> delays;
        std::array<float, numTaps> tapTimesMs = {
            // Based on typical room reflections
            5.0f, 7.0f, 11.0f, 13.0f, 17.0f, 19.0f, 23.0f, 29.0f, 31.0f,
            37.0f, 41.0f, 43.0f, 47.0f, 53.0f, 59.0f, 61.0f, 67.0f, 71.0f
        };
        std::array<float, numTaps> tapGains;
        float roomSize = 1.0f;
        double sampleRate = 44100.0;
    };

    //==============================================================================
    // Progenitor2-style Room Reverb (simplified from Freeverb3)
    class RoomReverb {
    public:
        void prepare(double sampleRate);
        void setDecayTime(float seconds);
        void setDamping(float damp);
        void setSize(float size);
        void process(const float* inputL, const float* inputR,
                    float* outputL, float* outputR, int numSamples);
        void clear();

    private:
        // 8 parallel comb filters per channel
        static constexpr int numCombs = 8;
        std::array<CombFilter, numCombs> combsL;
        std::array<CombFilter, numCombs> combsR;

        // 4 series allpass filters per channel
        static constexpr int numAllpasses = 4;
        std::array<AllpassFilter, numAllpasses> allpassesL;
        std::array<AllpassFilter, numAllpasses> allpassesR;

        // Tuning based on Freeverb/Progenitor2
        const std::array<float, numCombs> combTuningsMs = {
            25.31f, 26.94f, 28.96f, 30.75f, 32.24f, 33.81f, 35.31f, 36.67f
        };
        const std::array<float, numAllpasses> allpassTuningsMs = {
            12.61f, 10.00f, 7.73f, 5.10f
        };

        float decayFeedback = 0.84f;
        float damping = 0.5f;
        double sampleRate = 44100.0;
    };

    //==============================================================================
    // Zrev2-style Hall Reverb (simplified from Freeverb3)
    class HallReverb {
    public:
        void prepare(double sampleRate);
        void setDecayTime(float seconds);
        void setDamping(float damp);
        void setDiffusion(float diff);
        void setSize(float size);
        void process(const float* inputL, const float* inputR,
                    float* outputL, float* outputR, int numSamples);
        void clear();

    private:
        // More complex structure for halls
        static constexpr int numCombs = 12;
        std::array<CombFilter, numCombs> combsL;
        std::array<CombFilter, numCombs> combsR;

        static constexpr int numAllpasses = 5;
        std::array<AllpassFilter, numAllpasses> allpassesL;
        std::array<AllpassFilter, numAllpasses> allpassesR;

        // Input diffusion network
        std::array<AllpassFilter, 4> inputDiffusion;

        // Tuning for larger hall spaces
        const std::array<float, numCombs> combTuningsMs = {
            36.0f, 38.7f, 41.1f, 43.7f, 46.5f, 48.9f,
            51.5f, 53.9f, 56.3f, 58.7f, 61.0f, 63.5f
        };
        const std::array<float, numAllpasses> allpassTuningsMs = {
            14.1f, 11.3f, 8.73f, 6.28f, 4.10f
        };

        float decayFeedback = 0.91f;
        float damping = 0.3f;
        float diffusion = 0.8f;
        double sampleRate = 44100.0;
    };

    //==============================================================================
    // Simplified Plate Reverb
    class PlateReverb {
    public:
        void prepare(double sampleRate);
        void setDecayTime(float seconds);
        void setDamping(float damp);
        void process(const float* inputL, const float* inputR,
                    float* outputL, float* outputR, int numSamples);
        void clear();

    private:
        // Lattice structure typical of plates
        static constexpr int numDelays = 6;
        std::array<DelayLine, numDelays> delaysL;
        std::array<DelayLine, numDelays> delaysR;

        std::array<AllpassFilter, 4> diffusionL;
        std::array<AllpassFilter, 4> diffusionR;

        // Plate-specific timings (metallic character)
        const std::array<float, numDelays> delayTimesMs = {
            12.73f, 19.31f, 26.83f, 32.69f, 39.97f, 45.71f
        };

        float decay = 0.85f;
        float damping = 0.4f;
        double sampleRate = 44100.0;

        // Lowpass filter state for damping
        std::array<float, numDelays> lpStatesL;
        std::array<float, numDelays> lpStatesR;
    };

    //==============================================================================
    // Processing components
    EarlyReflections earlyReflections;
    RoomReverb roomReverb;
    HallReverb hallReverb;
    PlateReverb plateReverb;

    // Pre-delay
    DelayLine preDelayL, preDelayR;
    float preDelayTime = 0.0f;

    // Filters
    juce::dsp::StateVariableTPTFilter<float> inputHighpass;
    juce::dsp::StateVariableTPTFilter<float> inputLowpass;
    juce::dsp::StateVariableTPTFilter<float> outputHighpass;
    juce::dsp::StateVariableTPTFilter<float> outputLowpass;

    // Temporary buffers
    std::vector<float> tempBufferL;
    std::vector<float> tempBufferR;
    std::vector<float> earlyBufferL;
    std::vector<float> earlyBufferR;
    std::vector<float> lateBufferL;
    std::vector<float> lateBufferR;

    void processRoom(juce::AudioBuffer<float>& buffer);
    void processHall(juce::AudioBuffer<float>& buffer);
    void processPlate(juce::AudioBuffer<float>& buffer);
    void processEarlyOnly(juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DragonflyReverb)
};
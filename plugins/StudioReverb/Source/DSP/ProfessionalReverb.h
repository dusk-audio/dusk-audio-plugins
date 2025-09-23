/*
  ==============================================================================

    ProfessionalReverb.h
    High-quality reverb processor with algorithms inspired by Lexicon, Eventide,
    and the best of Freeverb3/Dragonfly

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <cmath>
#include <random>

class ProfessionalReverb
{
public:
    enum class ReverbType {
        EarlyReflections = 0,
        Room,
        Hall,
        Plate
    };

    ProfessionalReverb();
    ~ProfessionalReverb() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer);
    void reset();

    // Main controls
    void setReverbType(ReverbType type) { currentType = type; }
    ReverbType getReverbType() const { return currentType; }

    // Common parameters for all types
    void setDryWetMix(float mix) { wetMix = mix; dryMix = 1.0f - mix; }
    void setPreDelay(float ms);
    void setSize(float size);
    void setDecayTime(float seconds);
    void setDamping(float amount);
    void setDiffusion(float amount);
    void setWidth(float stereoWidth);

    // Early/Late mix (not used for plate)
    void setEarlyMix(float mix) { earlyMix = mix; }
    void setLateMix(float mix) { lateMix = mix; }

    // Tone controls
    void setLowCut(float freq);
    void setHighCut(float freq);
    void setLowMultiplier(float mult) { lowMult = mult; }
    void setHighMultiplier(float mult) { highMult = mult; }

    // Modulation
    void setModulationRate(float hz);
    void setModulationDepth(float depth);

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

    //==============================================================================
    // High-quality delay line with cubic interpolation
    class DelayLine {
    public:
        void setMaxSize(int maxSamples);
        void setDelay(float delaySamples);
        float read(float delaySamples) const;
        void write(float sample);
        void clear();

        int size = 0;  // Made public for access in AllpassFilter

    private:
        std::vector<float> buffer;
        int writePos = 0;

        float cubicInterpolate(float y0, float y1, float y2, float y3, float x) const;
    };

    //==============================================================================
    // High-quality allpass filter with modulation capability
    class AllpassFilter {
    public:
        void setSize(int samples);
        void setFeedback(float g);
        float process(float input);
        float processModulated(float input, float modulation);
        void clear();

    private:
        DelayLine delay;
        float feedback = 0.5f;
    };

    //==============================================================================
    // Comb filter with damping
    class CombFilter {
    public:
        void setSize(int samples);
        void setFeedback(float g);
        void setDamping(float d);
        float process(float input);
        void clear();

    private:
        DelayLine delay;
        float feedback = 0.5f;
        float damping = 0.5f;
        float filterStore = 0.0f;
    };

    //==============================================================================
    // Early Reflections Generator (high quality)
    class EarlyReflections {
    public:
        void prepare(double sampleRate);
        void setRoomSize(float size);
        void setDiffusion(float diff);
        void process(const float* inputL, const float* inputR,
                     float* outputL, float* outputR, int numSamples);
        void clear();

    private:
        static constexpr int numReflections = 24;  // More reflections for higher quality
        std::array<DelayLine, 2> delays;
        std::array<float, numReflections> tapTimesL;
        std::array<float, numReflections> tapTimesR;
        std::array<float, numReflections> tapGainsL;
        std::array<float, numReflections> tapGainsR;
        std::array<AllpassFilter, 4> diffusers;
    };

    //==============================================================================
    // FDN (Feedback Delay Network) Hall Reverb - Highest quality
    class FDNHallReverb {
    public:
        void prepare(double sampleRate);
        void setDecayTime(float seconds);
        void setDiffusion(float diff);
        void setModulation(float rate, float depth);
        void setDamping(float damp);
        void setSize(float size);
        void process(const float* inputL, const float* inputR,
                     float* outputL, float* outputR, int numSamples);
        void clear();

    private:
        static constexpr int numDelays = 16;  // 16-delay FDN for rich sound
        std::array<DelayLine, numDelays> delayLines;
        std::array<float, numDelays> delayTimes;
        std::array<float, numDelays> feedbackGains;
        std::array<std::array<float, numDelays>, numDelays> feedbackMatrix;
        std::array<AllpassFilter, 8> inputDiffusion;
        std::array<AllpassFilter, 4> outputDiffusion;

        // Modulation LFOs
        float lfo1Phase = 0.0f;
        float lfo2Phase = 0.0f;
        float modRate = 0.5f;
        float modDepth = 0.0f;

        // Damping filters
        std::array<float, numDelays> lowpassStates;
        std::array<float, numDelays> highpassStates;
        float dampingFreq = 8000.0f;

        void generateHadamardMatrix();
    };

    //==============================================================================
    // Enhanced Room Reverb with proper diffusion
    class RoomReverb {
    public:
        void prepare(double sampleRate);
        void setDecayTime(float seconds);
        void setDiffusion(float diff);
        void setDamping(float damp);
        void setSize(float size);
        void process(const float* inputL, const float* inputR,
                     float* outputL, float* outputR, int numSamples);
        void clear();

    private:
        // Parallel comb filters
        static constexpr int numCombs = 8;
        std::array<CombFilter, numCombs> combsL;
        std::array<CombFilter, numCombs> combsR;

        // Series allpass filters
        static constexpr int numAllpasses = 4;
        std::array<AllpassFilter, numAllpasses> allpassesL;
        std::array<AllpassFilter, numAllpasses> allpassesR;

        // Room-specific delay times (in samples at 44.1kHz)
        const std::array<int, numCombs> combTunings = {
            1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617
        };
        const std::array<int, numAllpasses> allpassTunings = {
            556, 441, 341, 225
        };
    };

    //==============================================================================
    // Dattorro Plate Reverb - The gold standard plate algorithm
    class DattorroPlate {
    public:
        void prepare(double sampleRate);
        void setDecayTime(float seconds);
        void setDiffusion(float diff);
        void setDamping(float damp);
        void setModulation(float rate, float depth);
        void process(const float* inputL, const float* inputR,
                     float* outputL, float* outputR, int numSamples);
        void clear();

    private:
        // Input diffusion
        std::array<AllpassFilter, 4> inputDiffusionL;
        std::array<AllpassFilter, 4> inputDiffusionR;

        // Tank structure (the heart of the plate)
        struct Tank {
            AllpassFilter allpass1;
            DelayLine delay1;
            AllpassFilter allpass2;
            DelayLine delay2;
            float lpState = 0.0f;
            float hpState = 0.0f;
        };

        Tank tankL, tankR;

        // Modulation
        float lfoPhase = 0.0f;
        float modRate = 1.0f;
        float modDepth = 0.0f;

        // Tank parameters
        float decay = 0.5f;
        float damping = 0.5f;
        float tankFeedback = 0.7f;

        // Output tap positions for plate character
        std::array<float, 7> outputTapsL;
        std::array<float, 7> outputTapsR;
    };

    //==============================================================================
    // Processing components
    EarlyReflections earlyReflections;
    FDNHallReverb hallReverb;
    RoomReverb roomReverb;
    DattorroPlate plateReverb;

    // Pre-delay
    DelayLine preDelayL, preDelayR;
    float preDelayTime = 0.0f;

    // Input/Output filters
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

    void processEarlyReflections(juce::AudioBuffer<float>& buffer);
    void processRoom(juce::AudioBuffer<float>& buffer);
    void processHall(juce::AudioBuffer<float>& buffer);
    void processPlate(juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProfessionalReverb)
};
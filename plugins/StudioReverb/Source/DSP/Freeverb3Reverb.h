/*
  ==============================================================================

    Freeverb3Reverb.h
    Direct port of Dragonfly's Freeverb3 implementation
    Based on Freeverb3 by Teru Kamogashira

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

// Freeverb3-style reverb implementation matching Dragonfly
class Freeverb3Reverb
{
public:
    enum class ReverbType {
        Room = 0,           // Progenitor2 algorithm
        Hall,               // Zrev2 algorithm
        Plate,              // Strev/custom plate
        EarlyReflections    // Early reflections only
    };

    Freeverb3Reverb();
    ~Freeverb3Reverb() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer);
    void reset();

    // Main controls matching Dragonfly
    void setReverbType(ReverbType type) { currentType = type; }
    ReverbType getReverbType() const { return currentType; }

    // Core parameters (matching Dragonfly)
    void setDryLevel(float level) { dryLevel = level; }
    void setEarlyLevel(float level) { earlyLevel = level; }
    void setLateLevel(float level) { lateLevel = level; }

    void setSize(float meters);        // 10-60 meters
    void setWidth(float percent);      // 50-150%
    void setPreDelay(float ms);        // 0-100ms
    void setDiffuse(float percent);    // 0-100%

    void setDecay(float seconds);      // 0.1-10 seconds
    void setLowCut(float freq);        // High-pass frequency
    void setHighCut(float freq);       // Low-pass frequency
    void setLowCrossover(float freq);  // Low frequency crossover
    void setHighCrossover(float freq); // High frequency crossover
    void setLowMult(float mult);       // Low frequency multiplier
    void setHighMult(float mult);      // High frequency multiplier

    void setModAmount(float amount);   // Modulation depth
    void setModSpeed(float speed);     // Modulation rate

private:
    double sampleRate = 44100.0;
    ReverbType currentType = ReverbType::Hall;

    // Levels
    float dryLevel = 0.7f;
    float earlyLevel = 0.3f;
    float lateLevel = 0.5f;
    float earlyLateSend = 0.2f;

    // Core parameters
    float roomSize = 30.0f;    // meters
    float width = 1.0f;         // stereo width
    float preDelayMs = 0.0f;
    float diffusion = 0.8f;
    float decay = 2.0f;         // seconds

    // Modulation
    float modAmount = 0.0f;
    float modSpeed = 0.5f;
    float modPhase = 0.0f;

    //==============================================================================
    // Simplified Freeverb3 earlyref implementation
    class EarlyReflections {
    public:
        void prepare(double sampleRate);
        void reset();
        void setRoomSize(float meters);
        void setDiffusion(float diff);
        void setWidth(float w);
        void setLRDelay(float delay);
        void setLRCrossApFreq(float freq, float stages);
        void setDiffusionApFreq(float freq, float stages);
        void loadPresetReflection(int preset);
        void process(const float* inputL, const float* inputR,
                    float* outputL, float* outputR, int numSamples);

    private:
        // Delay lines for reflections
        static constexpr int maxDelayMs = 100;
        std::vector<float> delayLineL;
        std::vector<float> delayLineR;
        int writePos = 0;

        // Tap structure for early reflections
        static constexpr int numTaps = 24;
        struct Tap {
            float delayMs;
            float gainL;
            float gainR;
        };
        std::array<Tap, numTaps> taps;

        // Diffusion allpasses
        class AllpassFilter {
        public:
            void setSize(int samples);
            void setFeedback(float g);
            float process(float input);
            void clear();
        private:
            std::vector<float> buffer;
            int writePos = 0;
            float feedback = 0.5f;
        };

        std::array<AllpassFilter, 4> diffusionL;
        std::array<AllpassFilter, 4> diffusionR;

        // Parameters
        float roomSizeFactor = 1.0f;
        float diffusionAmount = 0.7f;
        float stereoWidth = 1.0f;
        float lrDelay = 0.3f;
        double sampleRate = 44100.0;

        void initializePreset1Taps();
    };

    //==============================================================================
    // Simplified Freeverb3 zrev2 (FDN Hall) implementation
    class Zrev2 {
    public:
        void prepare(double sampleRate);
        void reset();
        void setrt60(float seconds);           // Decay time
        void setidiffusion1(float diff);       // Input diffusion
        void setodiffusion1(float diff);       // Output diffusion
        void setwidth(float w);                // Stereo width
        void setRSFactor(float factor);        // Room size factor
        void setDamping(float damp);           // HF damping
        void setModulation(float depth, float speed);
        void process(const float* inputL, const float* inputR,
                    float* outputL, float* outputR, int numSamples);

    private:
        // FDN delay lines (16 for high quality)
        static constexpr int numDelays = 16;
        class ModulatedDelayLine {
        public:
            void setMaxSize(int samples);
            void setDelay(float samples);
            float read(float modulation = 0.0f) const;
            void write(float sample);
            void clear();
        private:
            std::vector<float> buffer;
            int writePos = 0;
            float delayTime = 0;
            int maxSize = 0;
        };

        std::array<ModulatedDelayLine, numDelays> delayLines;
        std::array<float, numDelays> delayTimes;
        std::array<float, numDelays> feedbackGains;

        // Hadamard feedback matrix for FDN
        std::array<std::array<float, numDelays>, numDelays> feedbackMatrix;

        // Input diffusion (4 allpass stages)
        std::array<AllpassFilter, 4> inputDiffusionL;
        std::array<AllpassFilter, 4> inputDiffusionR;

        // Output diffusion (2 allpass stages)
        std::array<AllpassFilter, 2> outputDiffusionL;
        std::array<AllpassFilter, 2> outputDiffusionR;

        // Damping filters
        std::array<float, numDelays> dampingStates;
        float dampingCoeff = 0.3f;

        // Modulation
        float modDepth = 0.0f;
        float modRate = 0.5f;
        float lfoPhase = 0.0f;

        // Parameters
        float rt60 = 2.0f;
        float roomSizeFactor = 1.0f;
        float stereoWidth = 1.0f;
        double sampleRate = 44100.0;

        void generateHadamardMatrix();
        void updateDelayTimes();

        // Allpass helper class
        class AllpassFilter {
        public:
            void setSize(int samples);
            void setFeedback(float g);
            float process(float input);
            void clear();
        private:
            std::vector<float> buffer;
            int writePos = 0;
            float feedback = 0.5f;
        };
    };

    //==============================================================================
    // Simplified Freeverb3 progenitor2 (Room) implementation
    class Progenitor2 {
    public:
        void prepare(double sampleRate);
        void reset();
        void setrt60(float seconds);
        void setidiffusion1(float diff);
        void setodiffusion1(float diff);
        void setwidth(float w);
        void setRSFactor(float factor);
        void setDamping(float damp);
        void process(const float* inputL, const float* inputR,
                    float* outputL, float* outputR, int numSamples);

    private:
        // Classic Freeverb-style structure
        static constexpr int numCombs = 8;
        static constexpr int numAllpasses = 4;

        class CombFilter {
        public:
            void setSize(int samples);
            void setFeedback(float g);
            void setDamping(float d);
            float process(float input);
            void clear();
        private:
            std::vector<float> buffer;
            int writePos = 0;
            float feedback = 0.84f;
            float damping = 0.5f;
            float filterStore = 0.0f;
        };

        class AllpassFilter {
        public:
            void setSize(int samples);
            void setFeedback(float g);
            float process(float input);
            void clear();
        private:
            std::vector<float> buffer;
            int writePos = 0;
            float feedback = 0.5f;
        };

        // Parallel comb filters
        std::array<CombFilter, numCombs> combsL;
        std::array<CombFilter, numCombs> combsR;

        // Series allpass filters
        std::array<AllpassFilter, numAllpasses> allpassesL;
        std::array<AllpassFilter, numAllpasses> allpassesR;

        // Input diffusion
        std::array<AllpassFilter, 2> inputDiffusion;

        // Room-specific tunings (in ms, matching Dragonfly Room)
        const std::array<float, numCombs> combTuningsMs = {
            29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 51.3f, 53.9f, 56.1f
        };
        const std::array<float, numAllpasses> allpassTuningsMs = {
            14.1f, 11.3f, 8.73f, 5.87f
        };

        // Parameters
        float rt60 = 1.0f;
        float roomSizeFactor = 1.0f;
        float damping = 0.5f;
        float stereoWidth = 1.0f;
        double sampleRate = 44100.0;

        void updateParameters();
    };

    //==============================================================================
    // Plate reverb (similar to strev or custom plate)
    class PlateReverb {
    public:
        void prepare(double sampleRate);
        void reset();
        void setDecay(float seconds);
        void setDamping(float damp);
        void setBandwidth(float bw);
        void setDiffusion(float diff);
        void process(const float* inputL, const float* inputR,
                    float* outputL, float* outputR, int numSamples);

    private:
        // Dattorro-style plate structure
        class DelayLine {
        public:
            void setSize(int samples);
            float read(int delaySamples) const;
            void write(float sample);
            void clear();
        private:
            std::vector<float> buffer;
            int writePos = 0;
        };

        class AllpassFilter {
        public:
            void setSize(int samples);
            void setFeedback(float g);
            float process(float input);
            void clear();
        private:
            std::vector<float> buffer;
            int writePos = 0;
            float feedback = 0.5f;
        };

        // Input diffusion network
        std::array<AllpassFilter, 4> inputDiffusionL;
        std::array<AllpassFilter, 4> inputDiffusionR;

        // Plate tank (lattice structure)
        struct Tank {
            AllpassFilter allpass1;
            DelayLine delay1;
            AllpassFilter allpass2;
            DelayLine delay2;
            float lpState = 0.0f;  // Damping filter state
        };

        Tank tankL, tankR;

        // Output taps for plate character
        std::array<int, 7> outputTapsL;
        std::array<int, 7> outputTapsR;

        // Parameters
        float decay = 0.5f;
        float damping = 0.5f;
        float bandwidth = 0.9f;
        float diffusion = 0.7f;
        double sampleRate = 44100.0;

        void initializePlate();
    };

    //==============================================================================
    // Processing components
    EarlyReflections earlyReflections;
    Zrev2 hallReverb;
    Progenitor2 roomReverb;
    PlateReverb plateReverb;

    // Pre-delay
    std::vector<float> preDelayBufferL;
    std::vector<float> preDelayBufferR;
    int preDelayWritePos = 0;
    int preDelaySamples = 0;

    // Filters (matching Dragonfly's filter structure)
    juce::dsp::StateVariableTPTFilter<float> inputHighpass;
    juce::dsp::StateVariableTPTFilter<float> inputLowpass;
    juce::dsp::StateVariableTPTFilter<float> outputHighpass;
    juce::dsp::StateVariableTPTFilter<float> outputLowpass;

    // Crossover filters for frequency-dependent processing
    juce::dsp::LinkwitzRileyFilter<float> lowCrossover;
    juce::dsp::LinkwitzRileyFilter<float> highCrossover;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Freeverb3Reverb)
};
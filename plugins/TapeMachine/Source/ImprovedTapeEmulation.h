#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <random>

class ImprovedTapeEmulation
{
public:
    ImprovedTapeEmulation();
    ~ImprovedTapeEmulation() = default;

    enum TapeMachine
    {
        Swiss800 = 0,      // Swiss-style precision tape machine
        Classic102,        // Classic American tape machine
        Blend             // Hybrid blend of both
    };

    enum TapeSpeed
    {
        Speed_7_5_IPS = 0,
        Speed_15_IPS,
        Speed_30_IPS
    };

    enum TapeType
    {
        Type456 = 0,      // Classic high-output formulation
        TypeGP9,          // Grand Prix 9 formulation
        Type911,          // German precision formulation
        Type250           // Professional studio formulation
    };

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    // Main processing with all parameters
    float processSample(float input,
                       TapeMachine machine,
                       TapeSpeed speed,
                       TapeType type,
                       float biasAmount,      // 0-1 (affects harmonic content)
                       float saturationDepth, // 0-1 (tape compression)
                       float wowFlutterAmount, // 0-1 (pitch modulation)
                       bool noiseEnabled = false,  // Noise on/off
                       float noiseAmount = 0.0f    // 0-1 (noise level)
                       );

    // Metering
    float getInputLevel() const { return inputLevel.load(); }
    float getOutputLevel() const { return outputLevel.load(); }
    float getGainReduction() const { return gainReduction.load(); }

private:
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // Machine-specific characteristics
    struct MachineCharacteristics
    {
        // Frequency response
        float headBumpFreq;      // Center frequency of head bump
        float headBumpGain;      // Gain at head bump frequency
        float headBumpQ;         // Q factor of head bump

        // High frequency response
        float hfRolloffFreq;     // -3dB point for HF rolloff
        float hfRolloffSlope;    // dB/octave beyond rolloff

        // Saturation characteristics
        float saturationKnee;    // Soft knee point (0.6-0.9)
        float saturationHarmonics[5]; // Harmonic profile (2nd-6th)

        // Dynamic response
        float compressionRatio;  // Subtle compression (0.05-0.2)
        float compressionAttack; // ms
        float compressionRelease;// ms

        // Phase response
        float phaseShift;        // Subtle phase rotation

        // Crosstalk
        float crosstalkAmount;   // L/R bleed (-60 to -40 dB)
    };

    // Tape formulation characteristics
    struct TapeCharacteristics
    {
        // Magnetic properties
        float coercivity;        // Magnetic field strength needed
        float retentivity;       // How well tape holds magnetization
        float saturationPoint;   // Maximum flux level

        // Distortion characteristics
        float hysteresisAmount;  // Non-linearity amount
        float hysteresisAsymmetry; // Asymmetric distortion

        // Noise characteristics
        float noiseFloor;        // Base noise level (-70 to -60 dB)
        float modulationNoise;   // Noise modulated by signal

        // Frequency response mod
        float lfEmphasis;        // Low frequency emphasis
        float hfLoss;            // High frequency loss factor
    };

    // Speed-dependent parameters
    struct SpeedCharacteristics
    {
        float headBumpMultiplier;  // How speed affects head bump
        float hfExtension;         // HF response improvement with speed
        float noiseReduction;      // Noise improvement with speed
        float flutterRate;         // Typical flutter frequency
        float wowRate;             // Typical wow frequency
    };

    // DSP Components

    // Pre/Post emphasis (NAB/CCIR curves)
    juce::dsp::IIR::Filter<float> preEmphasisFilter1;
    juce::dsp::IIR::Filter<float> preEmphasisFilter2;
    juce::dsp::IIR::Filter<float> deEmphasisFilter1;
    juce::dsp::IIR::Filter<float> deEmphasisFilter2;

    // Head bump modeling (resonant peak)
    juce::dsp::IIR::Filter<float> headBumpFilter;

    // HF loss modeling
    juce::dsp::IIR::Filter<float> hfLossFilter1;
    juce::dsp::IIR::Filter<float> hfLossFilter2;

    // Record/Playback head gap loss
    juce::dsp::IIR::Filter<float> gapLossFilter;

    // Bias-induced HF boost
    juce::dsp::IIR::Filter<float> biasFilter;

    // DC blocking filter to prevent subsonic rumble
    juce::dsp::IIR::Filter<float> dcBlocker;

    // Hysteresis modeling
    struct HysteresisProcessor
    {
        float state = 0.0f;
        float previousInput = 0.0f;
        float previousOutput = 0.0f;

        float process(float input, float amount, float asymmetry, float saturation);
    };
    HysteresisProcessor hysteresisProc;

    // Saturation/Compression
    struct TapeSaturator
    {
        float envelope = 0.0f;
        float attackCoeff = 0.0f;
        float releaseCoeff = 0.0f;

        void updateCoefficients(float attackMs, float releaseMs, double sampleRate);
        float process(float input, float threshold, float ratio, float makeup);
    };
    TapeSaturator saturator;

    // Wow & Flutter
    struct WowFlutterProcessor
    {
        std::vector<float> delayBuffer;  // Dynamic size based on sample rate
        int writeIndex = 0;
        double wowPhase = 0.0;     // Use double for better precision
        double flutterPhase = 0.0;  // Use double for better precision
        float randomPhase = 0.0f;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

        void prepare(double sampleRate)
        {
            // Size for up to 50ms of delay at current sample rate
            size_t bufferSize = static_cast<size_t>(sampleRate * 0.05);
            delayBuffer.resize(bufferSize, 0.0f);
            writeIndex = 0;
        }

        float process(float input, float wowAmount, float flutterAmount,
                     float wowRate, float flutterRate, double sampleRate);
    };
    WowFlutterProcessor wowFlutter;

    // Tape noise generator
    struct NoiseGenerator
    {
        std::mt19937 rng{std::random_device{}()};
        std::normal_distribution<float> gaussian{0.0f, 1.0f};
        juce::dsp::IIR::Filter<float> pinkingFilter;

        float generateNoise(float noiseFloor, float modulationAmount, float signal);
    };
    NoiseGenerator noiseGen;

    // Crosstalk simulation (for stereo)
    float crosstalkBuffer = 0.0f;

    // Metering
    std::atomic<float> inputLevel{0.0f};
    std::atomic<float> outputLevel{0.0f};
    std::atomic<float> gainReduction{0.0f};

    // Filter update tracking (instance variables instead of statics)
    TapeMachine m_lastMachine = static_cast<TapeMachine>(-1);
    TapeSpeed m_lastSpeed = static_cast<TapeSpeed>(-1);
    TapeType m_lastType = static_cast<TapeType>(-1);
    float m_lastBias = -1.0f;

    // Helper functions
    MachineCharacteristics getMachineCharacteristics(TapeMachine machine);
    TapeCharacteristics getTapeCharacteristics(TapeType type);
    SpeedCharacteristics getSpeedCharacteristics(TapeSpeed speed);
    void updateFilters(TapeMachine machine, TapeSpeed speed, TapeType type, float biasAmount);

    // Soft clipping function
    static float softClip(float input, float threshold);

    // Harmonic generation
    float generateHarmonics(float input, const float* harmonicProfile, int numHarmonics);

    static constexpr float denormalPrevention = 1e-8f;
};
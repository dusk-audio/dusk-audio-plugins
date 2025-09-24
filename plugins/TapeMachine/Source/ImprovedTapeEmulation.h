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
        BASF911,
        Scotch250
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
                       float wowFlutterAmount // 0-1 (pitch modulation)
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
        std::array<float, 4096> delayBuffer{};
        int writeIndex = 0;
        float wowPhase = 0.0f;
        float flutterPhase = 0.0f;
        float randomPhase = 0.0f;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

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
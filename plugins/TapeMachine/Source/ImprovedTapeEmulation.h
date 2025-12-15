#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <random>

// Wow & Flutter processor - can be shared between channels for stereo coherence
class WowFlutterProcessor
{
public:
    std::vector<float> delayBuffer;  // Dynamic size based on sample rate
    int writeIndex = 0;
    double wowPhase = 0.0;     // Use double for better precision
    double flutterPhase = 0.0;  // Use double for better precision
    float randomPhase = 0.0f;
    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

    void prepare(double sampleRate)
    {
        // Validate sampleRate to prevent overflow
        if (sampleRate <= 0.0 || sampleRate > 1000000.0)
            sampleRate = 44100.0;  // Use safe default

        // Size for up to 50ms of delay at current sample rate
        // Check for potential overflow before casting
        constexpr double maxSafeRate = static_cast<double>(SIZE_MAX) / 0.05;
        if (sampleRate > maxSafeRate)
            sampleRate = 192000.0;  // Clamp to reasonable maximum

        // Perform calculation with overflow protection
        double bufferSizeDouble = sampleRate * 0.05;
        size_t bufferSize = (bufferSizeDouble > static_cast<double>(SIZE_MAX))
                            ? SIZE_MAX
                            : static_cast<size_t>(bufferSizeDouble);

        // Ensure minimum size
        if (bufferSize < 64)
            bufferSize = 64;

        delayBuffer.resize(bufferSize, 0.0f);
        writeIndex = 0;
    }

    // Process and return modulation amount (in samples)
    float calculateModulation(float wowAmount, float flutterAmount,
                             float wowRate, float flutterRate, double sampleRate)
    {
        // Protect against invalid sample rate
        if (sampleRate <= 0.0)
            sampleRate = 44100.0;

        // Calculate modulation
        float wowMod = static_cast<float>(std::sin(wowPhase)) * wowAmount * 10.0f;  // ±10 samples max
        float flutterMod = static_cast<float>(std::sin(flutterPhase)) * flutterAmount * 2.0f;  // ±2 samples max
        float randomMod = dist(rng) * flutterAmount * 0.5f;  // Random component

        // Update phases with double precision
        double safeSampleRate = std::max(1.0, sampleRate);
        double wowIncrement = 2.0 * juce::MathConstants<double>::pi * wowRate / safeSampleRate;
        double flutterIncrement = 2.0 * juce::MathConstants<double>::pi * flutterRate / safeSampleRate;

        wowPhase += wowIncrement;
        if (wowPhase > 2.0 * juce::MathConstants<double>::pi)
            wowPhase -= 2.0 * juce::MathConstants<double>::pi;

        flutterPhase += flutterIncrement;
        if (flutterPhase > 2.0 * juce::MathConstants<double>::pi)
            flutterPhase -= 2.0 * juce::MathConstants<double>::pi;

        return wowMod + flutterMod + randomMod;
    }

    // Process sample with given modulation
    float processSample(float input, float modulationSamples)
    {
        if (delayBuffer.empty())
            return input;

        // Write to delay buffer with bounds checking
        if (writeIndex < 0 || writeIndex >= static_cast<int>(delayBuffer.size()))
            writeIndex = 0;

        delayBuffer[writeIndex] = input;

        // Calculate total delay with bounds limiting
        float totalDelay = 20.0f + modulationSamples;  // Base delay + modulation
        int bufferSize = static_cast<int>(delayBuffer.size());
        if (bufferSize <= 0)
            return input;

        // Ensure delay is within valid range
        totalDelay = std::max(1.0f, std::min(totalDelay, static_cast<float>(bufferSize - 1)));

        // Fractional delay interpolation
        int delaySamples = static_cast<int>(totalDelay);
        float fraction = totalDelay - delaySamples;

        int readIndex1 = (writeIndex - delaySamples + bufferSize) % bufferSize;
        int readIndex2 = (readIndex1 - 1 + bufferSize) % bufferSize;

        // Additional bounds checking
        readIndex1 = std::max(0, std::min(readIndex1, bufferSize - 1));
        readIndex2 = std::max(0, std::min(readIndex2, bufferSize - 1));

        float sample1 = delayBuffer[readIndex1];
        float sample2 = delayBuffer[readIndex2];

        // Linear interpolation
        float output = sample1 * (1.0f - fraction) + sample2 * fraction;

        // Update write index with bounds checking
        writeIndex = (writeIndex + 1) % std::max(1, bufferSize);

        return output;
    }
};

// Transformer saturation model - authentic input/output stage coloration
class TransformerSaturation
{
public:
    void prepare(double sampleRate);
    float process(float input, float driveAmount, bool isOutputStage);
    void reset();

private:
    // DC blocking for transformer coupling
    float dcState = 0.0f;
    float dcBlockCoeff = 0.9995f;

    // Transformer hysteresis state
    float hystState = 0.0f;
    float prevInput = 0.0f;

    // LF resonance from core saturation
    float lfResonanceState = 0.0f;
};

// Playback head frequency response - distinct from tape response
class PlaybackHeadResponse
{
public:
    void prepare(double sampleRate);
    float process(float input, float gapWidth, float speed);
    void reset();

private:
    // Head gap loss filter (comb filter approximation)
    std::array<float, 64> gapDelayLine{};
    int gapDelayIndex = 0;

    // Head resonance (mechanical + electrical)
    float resonanceState1 = 0.0f;
    float resonanceState2 = 0.0f;

    double currentSampleRate = 44100.0;
};

// Record head bias oscillator effects
class BiasOscillator
{
public:
    void prepare(double sampleRate);
    float process(float input, float biasFreq, float biasAmount);
    void reset();

private:
    double phase = 0.0;
    double sampleRate = 44100.0;

    // Intermodulation products filter
    float imState = 0.0f;
};

// Capstan/motor flutter - separate from tape wow/flutter
class MotorFlutter
{
public:
    void prepare(double sampleRate);
    float calculateFlutter(float motorQuality);  // Returns pitch modulation
    void reset();

private:
    double phase1 = 0.0;  // Primary motor frequency
    double phase2 = 0.0;  // Secondary bearing frequency
    double phase3 = 0.0;  // Capstan eccentricity
    double sampleRate = 44100.0;

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter{-1.0f, 1.0f};
};

class ImprovedTapeEmulation
{
public:
    ImprovedTapeEmulation();
    ~ImprovedTapeEmulation() = default;

    enum TapeMachine
    {
        Swiss800 = 0,      // Studer A800 - Swiss precision tape machine
        Classic102,        // Ampex ATR-102 - Classic American tape machine
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
                       float noiseAmount = 0.0f,   // 0-1 (noise level)
                       float* sharedWowFlutterMod = nullptr,  // Shared modulation for stereo coherence
                       float calibrationLevel = 0.0f  // 0/3/6/9 dB - affects headroom/saturation point
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

    // Per-channel delay line for wow/flutter (uses shared modulation)
    WowFlutterProcessor perChannelWowFlutter;

    // Tape noise generator
    struct NoiseGenerator
    {
        std::mt19937 rng{std::random_device{}()};
        std::normal_distribution<float> gaussian{0.0f, 1.0f};
        juce::dsp::IIR::Filter<float> pinkingFilter;

        float generateNoise(float noiseFloor, float modulationAmount, float signal);
    };
    NoiseGenerator noiseGen;

    // NEW: Enhanced DSP components for UAD-quality emulation
    TransformerSaturation inputTransformer;
    TransformerSaturation outputTransformer;
    PlaybackHeadResponse playbackHead;
    BiasOscillator biasOsc;
    MotorFlutter motorFlutter;

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
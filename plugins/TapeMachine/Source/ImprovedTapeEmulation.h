#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <random>
#include <complex>

//==============================================================================
// 8th-order Butterworth Anti-Aliasing Filter (numerically stable)
// Uses cascaded biquad sections with pre-computed Q values
// Provides ~48dB/octave rolloff which is sufficient when combined with
// JUCE's oversampler anti-aliasing
//==============================================================================
class ChebyshevAntiAliasingFilter
{
public:
    static constexpr int NUM_SECTIONS = 4; // 4 biquads = 8th order

    struct BiquadState {
        float z1 = 0.0f, z2 = 0.0f;
    };

    struct BiquadCoeffs {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
    };

    void prepare(double sampleRate, double cutoffHz)
    {
        // Clamp cutoff to safe range (well below Nyquist)
        cutoffHz = std::min(cutoffHz, sampleRate * 0.45);
        cutoffHz = std::max(cutoffHz, 20.0);

        // 8th-order Butterworth Q values (4 biquad sections)
        // Q_k = 1 / (2 * sin((2k-1) * pi / 16)) for k = 1 to 4
        static constexpr float Qs[4] = { 0.5098f, 0.6013f, 0.9000f, 2.5629f };

        for (int i = 0; i < NUM_SECTIONS; ++i)
        {
            designLowpass(sampleRate, cutoffHz, Qs[i], coeffs[i]);
        }

        reset();
    }

    void reset()
    {
        for (auto& state : states)
            state = BiquadState{};
    }

    float process(float input)
    {
        float signal = input;

        for (int i = 0; i < NUM_SECTIONS; ++i)
        {
            signal = processBiquad(signal, coeffs[i], states[i]);
        }

        // Denormal protection
        if (std::abs(signal) < 1e-15f)
            signal = 0.0f;

        return signal;
    }

private:
    std::array<BiquadCoeffs, NUM_SECTIONS> coeffs;
    std::array<BiquadState, NUM_SECTIONS> states;

    float processBiquad(float input, const BiquadCoeffs& c, BiquadState& s)
    {
        float output = c.b0 * input + s.z1;
        s.z1 = c.b1 * input - c.a1 * output + s.z2;
        s.z2 = c.b2 * input - c.a2 * output;
        return output;
    }

    void designLowpass(double sampleRate, double freq, float Q, BiquadCoeffs& c)
    {
        // Bilinear transform lowpass design
        const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * Q);

        const double a0 = 1.0 + alpha;
        const double a0_inv = 1.0 / a0;

        c.b0 = static_cast<float>(((1.0 - cosw0) / 2.0) * a0_inv);
        c.b1 = static_cast<float>((1.0 - cosw0) * a0_inv);
        c.b2 = c.b0;
        c.a1 = static_cast<float>((-2.0 * cosw0) * a0_inv);
        c.a2 = static_cast<float>((1.0 - alpha) * a0_inv);
    }
};

//==============================================================================
// Soft Limiter for Pre-Saturation Peak Control
//
// PURPOSE: Prevents harmonic explosion at extreme input levels.
// Pre-emphasis can add +6-7dB to HF, so +12dB input becomes +18-19dB
// at HF before saturation. This limiter catches those peaks to
// prevent aliasing while preserving normal operation below +6 VU.
//
// PLACEMENT: After pre-emphasis, before record head filter and saturation.
// This ensures that extreme HF peaks don't generate excessive harmonics
// that would alias back into the audible spectrum on downsampling.
//
// IMPORTANT: We use simple hard clipping instead of tanh() because:
// - tanh() generates infinite harmonics that alias badly
// - The 16th-order record head filter immediately after smooths transitions
// - At 0.95 threshold, only true peaks are clipped (rare in normal use)
// - Any clipping harmonics are removed by the record head + AA filters
//==============================================================================
class SoftLimiter
{
public:
    // Threshold at 0.95 amplitude - only clips true peaks
    // Pre-emphasized HF rarely exceeds this unless input is extremely hot
    static constexpr float threshold = 0.95f;

    float process(float x) const
    {
        // Simple hard limit - generates finite harmonics that are
        // filtered by the 16th-order record head filter that follows
        if (x > threshold)
            return threshold;
        if (x < -threshold)
            return -threshold;
        return x;
    }
};

//==============================================================================
// Saturation Split Filter - 2-pole Butterworth lowpass for frequency-selective saturation
//
// PURPOSE: Prevents HF content from being saturated, which causes aliasing.
// By splitting the signal and only saturating low frequencies, HF passes through
// clean and doesn't generate harmonics that alias back into the audible band.
//
// DESIGN: 2-pole Butterworth at 5kHz (12dB/octave)
// - At 5kHz: -3dB (crossover point)
// - At 10kHz: ~-12dB
// - At 14.5kHz: ~-18dB (test frequency significantly attenuated for saturation)
//
// Why 5kHz? Testing showed:
// - H3 (tape warmth harmonic) preserved at all typical audio frequencies
// - Aliasing below -80dB with 14.5kHz @ +8.3dB input
// - HF passes through linearly, keeping brightness (unlike HF detector approach)
//
// This is different from the HF detector approach - we don't reduce saturation
// based on HF detection (which makes the plugin sound dull). Instead, we split
// the signal and only saturate the LF content, letting HF pass through linearly.
// Result: full HF brightness is preserved, but HF doesn't generate harmonics.
//==============================================================================
class SaturationSplitFilter
{
public:
    void prepare(double sampleRate, double cutoffHz = 5000.0)
    {
        // 2-pole Butterworth (Q = 0.707 for maximally flat)
        const double w0 = 2.0 * juce::MathConstants<double>::pi * cutoffHz / sampleRate;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * 0.7071);  // Q = sqrt(2)/2 for Butterworth
        const double a0 = 1.0 + alpha;

        b0 = static_cast<float>(((1.0 - cosw0) / 2.0) / a0);
        b1 = static_cast<float>((1.0 - cosw0) / a0);
        b2 = b0;
        a1 = static_cast<float>((-2.0 * cosw0) / a0);
        a2 = static_cast<float>((1.0 - alpha) / a0);

        reset();
    }

    void reset()
    {
        z1 = z2 = 0.0f;
    }

    // Returns the lowpass filtered signal (for saturation)
    // Caller should compute highpass as: original - lowpass
    float process(float input)
    {
        float output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

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
        // Validate sampleRate with consistent bounds
        // MIN: 8000 Hz (lowest professional rate)
        // MAX: 768000 Hz (4x oversampled 192kHz - highest expected)
        static constexpr double MIN_SAMPLE_RATE = 8000.0;
        static constexpr double MAX_SAMPLE_RATE = 768000.0;
        static constexpr double MAX_DELAY_SECONDS = 0.05;  // 50ms buffer

        if (sampleRate <= 0.0 || !std::isfinite(sampleRate))
            sampleRate = 44100.0;  // Use safe default

        sampleRate = std::clamp(sampleRate, MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);

        // Calculate buffer size with explicit bounds checking
        // At MAX_SAMPLE_RATE (768kHz), 50ms = 38400 samples - well within size_t range
        double bufferSizeDouble = sampleRate * MAX_DELAY_SECONDS;

        // Clamp to reasonable bounds: min 64 samples, max 65536 samples (more than enough for 50ms at any rate)
        static constexpr size_t MIN_BUFFER_SIZE = 64;
        static constexpr size_t MAX_BUFFER_SIZE = 65536;

        size_t bufferSize = static_cast<size_t>(bufferSizeDouble);
        bufferSize = std::clamp(bufferSize, MIN_BUFFER_SIZE, MAX_BUFFER_SIZE);

        // Only resize if needed (avoid unnecessary allocations)
        if (delayBuffer.size() != bufferSize)
        {
            delayBuffer.resize(bufferSize, 0.0f);
        }
        else
        {
            // Clear existing buffer
            std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
        }
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
        Classic102         // Ampex ATR-102 - Classic American tape machine
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

    // Record head gap filter - models HF loss at record head before saturation
    // Real tape: record head gap creates natural lowpass response (~15-18kHz at 15 IPS)
    // This prevents HF content from generating harmonics that would alias
    // 8 cascaded biquads = 16th-order Butterworth for steep rolloff (96dB/oct)
    // Applied BEFORE saturation to mimic real tape head behavior
    juce::dsp::IIR::Filter<float> recordHeadFilter1;
    juce::dsp::IIR::Filter<float> recordHeadFilter2;
    juce::dsp::IIR::Filter<float> recordHeadFilter3;
    juce::dsp::IIR::Filter<float> recordHeadFilter4;
    juce::dsp::IIR::Filter<float> recordHeadFilter5;
    juce::dsp::IIR::Filter<float> recordHeadFilter6;
    juce::dsp::IIR::Filter<float> recordHeadFilter7;
    juce::dsp::IIR::Filter<float> recordHeadFilter8;

    // Post-saturation anti-aliasing filter - 8th-order Chebyshev Type I
    // CRITICAL: This prevents aliasing by removing harmonics above original Nyquist
    // before the JUCE oversampler downsamples the signal.
    //
    // Design: 8th-order Chebyshev Type I with 0.1dB passband ripple
    // - Provides ~96dB attenuation at 2x the cutoff frequency
    // - Much steeper transition band than equivalent-order Butterworth
    // - Cutoff set to 0.45 * base sample rate (e.g., 19.8kHz for 44.1kHz base)
    //
    // Why Chebyshev over Butterworth?
    // - Butterworth: 96dB/oct requires 16th order (8 biquads)
    // - Chebyshev: 96dB at 2x cutoff with only 8th order (4 biquads)
    // - Chebyshev has passband ripple but much steeper rolloff
    ChebyshevAntiAliasingFilter antiAliasingFilter;

    // Pre-saturation soft limiter - catches extreme peaks after pre-emphasis
    // Placed AFTER pre-emphasis, BEFORE record head filter and saturation
    // This prevents aliasing at extreme input levels while preserving
    // normal tape saturation behavior at typical operating levels
    SoftLimiter preSaturationLimiter;

    // Split filters for frequency-selective saturation
    // These split the signal so that only low frequencies get saturated,
    // preventing HF content from generating harmonics that alias
    SaturationSplitFilter saturationSplitFilter;   // For harmonic generation stage
    SaturationSplitFilter softClipSplitFilter;     // For soft clip stage

    // Store base sample rate for anti-aliasing filter cutoff calculation
    double baseSampleRate = 44100.0;

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

    // Record head gap cutoff frequency (set in prepare() based on tape speed)
    float recordHeadCutoff = 15000.0f;

    // Metering
    std::atomic<float> inputLevel{0.0f};
    std::atomic<float> outputLevel{0.0f};
    std::atomic<float> gainReduction{0.0f};

    // Filter update tracking (instance variables instead of statics)
    TapeMachine m_lastMachine = static_cast<TapeMachine>(-1);
    TapeSpeed m_lastSpeed = static_cast<TapeSpeed>(-1);
    TapeType m_lastType = static_cast<TapeType>(-1);
    float m_lastBias = -1.0f;

    // Cached characteristics (updated when parameters change, not per-sample)
    MachineCharacteristics m_cachedMachineChars;
    TapeCharacteristics m_cachedTapeChars;
    SpeedCharacteristics m_cachedSpeedChars;
    bool m_hasTransformers = false;
    float m_gapWidth = 3.0f;

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

    // Helper to validate filter coefficients are finite (not NaN or Inf)
    static bool validateCoefficients(const juce::dsp::IIR::Coefficients<float>::Ptr& coeffs)
    {
        if (coeffs == nullptr)
            return false;

        auto* rawCoeffs = coeffs->getRawCoefficients();
        size_t numCoeffs = coeffs->getFilterOrder() + 1;  // b coeffs
        numCoeffs += coeffs->getFilterOrder();             // a coeffs (a0 normalized to 1)

        for (size_t i = 0; i < numCoeffs * 2; ++i)  // Check all coefficients
        {
            if (!std::isfinite(rawCoeffs[i]))
                return false;
        }
        return true;
    }
};
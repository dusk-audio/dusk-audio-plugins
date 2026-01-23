#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <random>
#include <complex>

//==============================================================================
// 8th-order Chebyshev Type I Anti-Aliasing Filter (0.5dB passband ripple)
// Uses cascaded biquad sections with poles from analog prototype via bilinear transform
// Provides ~64dB stopband rejection at 1.7× cutoff (~26dB more than Butterworth)
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
        cutoffHz = std::min(cutoffHz, sampleRate * 0.45);
        cutoffHz = std::max(cutoffHz, 20.0);

        // 8th-order Chebyshev Type I with 0.5dB passband ripple
        // Poles computed from analog prototype, then bilinear-transformed
        constexpr int N = 8;
        constexpr double rippleDb = 0.5;

        const double epsilon = std::sqrt(std::pow(10.0, rippleDb / 10.0) - 1.0);
        const double a = std::asinh(1.0 / epsilon) / N;
        const double sinhA = std::sinh(a);
        const double coshA = std::cosh(a);

        // Bilinear transform constant and pre-warped cutoff
        const double C = 2.0 * sampleRate;
        const double Wa = C * std::tan(juce::MathConstants<double>::pi * cutoffHz / sampleRate);

        for (int k = 0; k < NUM_SECTIONS; ++k)
        {
            // Analog prototype pole: θ_k = (2k+1)π/(2N)
            double theta = (2.0 * k + 1.0) * juce::MathConstants<double>::pi / (2.0 * N);
            double sigma = -sinhA * std::sin(theta);
            double omega = coshA * std::cos(theta);

            // Bilinear transform coefficients
            double poleMagSq = sigma * sigma + omega * omega;
            double A = Wa * Wa * poleMagSq;
            double B = 2.0 * (-sigma) * Wa * C;
            double a0 = C * C + B + A;
            double a0_inv = 1.0 / a0;

            coeffs[k].b0 = static_cast<float>(A * a0_inv);
            coeffs[k].b1 = static_cast<float>(2.0 * A * a0_inv);
            coeffs[k].b2 = coeffs[k].b0;
            coeffs[k].a1 = static_cast<float>(2.0 * (A - C * C) * a0_inv);
            coeffs[k].a2 = static_cast<float>((C * C - B + A) * a0_inv);
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

//==============================================================================
// 3-Band Splitter for frequency-dependent tape saturation
// Uses cascaded first-order TPT filters (Linkwitz-Riley 2nd-order, 12dB/oct)
// for proper crossover behavior with -6dB at crossover frequencies.
// Bands: Bass (<200Hz), Mid (200Hz-5kHz), Treble (>5kHz)
// Note: bass + mid + treble = input (algebraic perfect reconstruction).
// Slight crossover coloration is acceptable for tape saturation purposes.
//==============================================================================
class ThreeBandSplitter
{
public:
    void prepare(double sampleRate)
    {
        lr200.prepare(sampleRate, 200.0f);
        lr5000.prepare(sampleRate, 5000.0f);
    }

    void reset()
    {
        lr200.reset();
        lr5000.reset();
    }

    // Split signal into 3 bands with algebraic perfect reconstruction
    // bass + mid + treble = input (exactly, at all frequencies)
    void split(float input, float& bass, float& mid, float& treble)
    {
        float lp200Out = lr200.process(input);
        float lp5000Out = lr5000.process(input);

        bass = lp200Out;
        mid = lp5000Out - lp200Out;
        treble = input - lp5000Out;
    }

private:
    // Linkwitz-Riley 2nd-order lowpass: two cascaded first-order TPT sections
    // Provides 12dB/oct slope and -6dB at crossover frequency
    struct LR2Filter
    {
        struct OnePoleLP
        {
            float state = 0.0f;
            float coeff = 0.0f;

            void prepare(double sampleRate, float cutoffHz)
            {
                float g = std::tan(juce::MathConstants<float>::pi * cutoffHz
                                  / static_cast<float>(sampleRate));
                coeff = g / (1.0f + g);
                state = 0.0f;
            }

            void reset() { state = 0.0f; }

            float process(float input)
            {
                float v = (input - state) * coeff;
                float output = v + state;
                state = output + v;
                return output;
            }
        };

        OnePoleLP stage1;
        OnePoleLP stage2;

        void prepare(double sampleRate, float cutoffHz)
        {
            stage1.prepare(sampleRate, cutoffHz);
            stage2.prepare(sampleRate, cutoffHz);
        }

        void reset()
        {
            stage1.reset();
            stage2.reset();
        }

        float process(float input)
        {
            float s1 = stage1.process(input);
            return stage2.process(s1);
        }
    };

    LR2Filter lr200;
    LR2Filter lr5000;
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

    // Smoothed random modulation (avoids per-sample noise from raw RNG)
    float randomTarget = 0.0f;
    float randomCurrent = 0.0f;
    int randomUpdateCounter = 0;
    static constexpr int RANDOM_UPDATE_INTERVAL = 64; // Base update interval (at 1x rate)

    // Rate compensation: ensures identical behavior regardless of oversampling factor
    int oversamplingFactor = 1;
    float smoothingAlpha = 0.01f;  // Calculated in prepare() for ~70Hz cutoff

    void prepare(double sampleRate, int osFactor = 1)
    {
        // Store oversampling factor for rate compensation
        oversamplingFactor = std::max(1, osFactor);

        // Calculate smoothing alpha for ~70Hz cutoff regardless of sample rate
        // One-pole: alpha = 1 - exp(-2*pi*fc/fs)
        // This ensures the random modulation bandwidth is always ~70Hz
        smoothingAlpha = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 70.0f
                                          / static_cast<float>(sampleRate));

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

    // Process and return modulation amount (in samples at current rate)
    float calculateModulation(float wowAmount, float flutterAmount,
                             float wowRate, float flutterRate, double sampleRate)
    {
        // Protect against invalid sample rate
        if (sampleRate <= 0.0)
            sampleRate = 44100.0;

        // Scale modulation depths by oversampling factor to maintain constant TIME deviation
        // At 4x: ±10 samples at 176.4kHz = same time as ±10 samples at 44.1kHz only if scaled by 4x
        float osScale = static_cast<float>(oversamplingFactor);

        float wowMod = static_cast<float>(std::sin(wowPhase)) * wowAmount * 10.0f * osScale;
        float flutterMod = static_cast<float>(std::sin(flutterPhase)) * flutterAmount * 2.0f * osScale;

        // Smoothed random component: update target at time-based rate
        // Scale interval by oversampling factor so updates occur at same temporal rate
        int scaledInterval = RANDOM_UPDATE_INTERVAL * oversamplingFactor;
        if (++randomUpdateCounter >= scaledInterval)
        {
            randomUpdateCounter = 0;
            randomTarget = dist(rng);
        }
        // Rate-compensated one-pole smoothing (~70Hz cutoff regardless of sample rate)
        randomCurrent += (randomTarget - randomCurrent) * smoothingAlpha;
        float randomMod = randomCurrent * flutterAmount * 0.5f * osScale;

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

        // Scale base delay by oversampling factor to maintain constant time offset
        // At 1x: 20 samples = 454μs; at 4x: 80 samples = 454μs (same time)
        float baseDelay = 20.0f * static_cast<float>(oversamplingFactor);
        float totalDelay = baseDelay + modulationSamples;  // Base delay + modulation
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
    float hystDecay = 0.995f;       // Rate-compensated (calculated in prepare)
    float prevInput = 0.0f;

    // LF resonance from core saturation
    float lfResonanceState = 0.0f;
    float lfResonanceCoeff = 0.002f; // Rate-compensated (calculated in prepare)
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

    // Rate-compensated resonance coefficient (~740Hz cutoff regardless of sample rate)
    float resonanceCoeff = 0.1f;

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
    void prepare(double sampleRate, int osFactor = 1);
    float calculateFlutter(float motorQuality);  // Returns pitch modulation
    void reset();

private:
    double phase1 = 0.0;  // Primary motor frequency
    double phase2 = 0.0;  // Secondary bearing frequency
    double phase3 = 0.0;  // Capstan eccentricity
    double sampleRate = 44100.0;
    int oversamplingFactor = 1;  // Rate compensation for consistent noise power

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

    // EQ Standard - affects pre-emphasis/de-emphasis curves
    enum EQStandard
    {
        NAB = 0,          // American standard (60Hz hum region)
        CCIR,             // European/IEC standard (50Hz hum region)
        AES               // AES standard (typically 30 IPS only)
    };

    // Signal Path - determines processing stages
    enum SignalPath
    {
        Repro = 0,        // Full tape processing (record + playback)
        Sync,             // Record head playback (slightly different EQ)
        Input,            // Electronics only (no tape saturation)
        Thru              // Complete bypass
    };

    void prepare(double sampleRate, int samplesPerBlock, int oversamplingFactor = 1);
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
                       float calibrationLevel = 0.0f,  // 0/3/6/9 dB - affects headroom/saturation point
                       EQStandard eqStandard = NAB,    // NAB/CCIR/AES pre-emphasis curves
                       SignalPath signalPath = Repro   // Processing path selection
                       );

    // Metering
    float getInputLevel() const { return inputLevel.load(); }
    float getOutputLevel() const { return outputLevel.load(); }
    float getGainReduction() const { return gainReduction.load(); }

private:
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int currentOversamplingFactor = 1;  // Stored for AA filter bypass at 1x

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
    // Post-saturation filters use double precision to avoid quantization noise
    // at low normalized frequencies when oversampling (e.g., 60Hz at 176.4kHz).
    // Float32 biquad poles near unit circle amplify roundoff error by ~700x.
    juce::dsp::IIR::Filter<double> deEmphasisFilter1;
    juce::dsp::IIR::Filter<double> deEmphasisFilter2;

    // Head bump modeling (resonant peak) - double for low-freq precision
    juce::dsp::IIR::Filter<double> headBumpFilter;

    // HF loss modeling - double for consistency with post-saturation chain
    juce::dsp::IIR::Filter<double> hfLossFilter1;
    juce::dsp::IIR::Filter<double> hfLossFilter2;

    // Record/Playback head gap loss - double for post-saturation chain
    juce::dsp::IIR::Filter<double> gapLossFilter;

    // Bias-induced HF boost
    juce::dsp::IIR::Filter<float> biasFilter;

    // DC blocking filter to prevent subsonic rumble - double for 25Hz at high rates
    juce::dsp::IIR::Filter<double> dcBlocker;

    // Record head gap filter - models HF loss at record head before saturation
    // Real tape: record head gap creates natural lowpass response (~15-18kHz at 15 IPS)
    // This prevents HF content from generating harmonics that would alias
    // 2 cascaded biquads = 4th-order Butterworth for 24dB/oct rolloff
    // (Post-saturation AA filter + JUCE decimation filter handle remaining aliasing)
    // Applied BEFORE saturation to mimic real tape head behavior
    juce::dsp::IIR::Filter<float> recordHeadFilter1;
    juce::dsp::IIR::Filter<float> recordHeadFilter2;

    // Post-saturation anti-aliasing filter - 8th-order Chebyshev Type I
    // CRITICAL: This prevents aliasing by removing harmonics above original Nyquist
    // before the JUCE oversampler downsamples the signal.
    //
    // Design: 8th-order Chebyshev Type I with 0.5dB passband ripple
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

    // 3-band splitter for frequency-dependent tape saturation
    // Replaces binary 5kHz split with physically-accurate per-band drive
    ThreeBandSplitter threeBandSplitter;

    // Split filters for the two soft-clip stages (separate instances to avoid shared state)
    SaturationSplitFilter softClipSplitFilter1;   // After 3-band saturation
    SaturationSplitFilter softClipSplitFilter2;   // Before AA filter

    // Store base sample rate for anti-aliasing filter cutoff calculation
    double baseSampleRate = 44100.0;

    // Lookup-table-based tape saturation curve
    // Pre-computed tanh-based transfer function with machine-specific asymmetry
    // Produces natural harmonic spectrum that rises smoothly with drive level
    struct TapeSaturationTable
    {
        static constexpr int TABLE_SIZE = 4096;
        static constexpr float TABLE_RANGE = 4.0f;  // Input: [-2, +2]

        std::array<float, TABLE_SIZE> table{};
        float currentAsymmetry = 0.0f;
        bool needsRegeneration = true;

        // Generate lookup table for given machine type and asymmetry
        void generate(bool isStuder, float asymmetry);

        // Process sample through table with drive-controlled nonlinearity
        float process(float input, float drive) const;
    };
    TapeSaturationTable saturationTable;

    // Hysteresis-modulated drive: adjusts saturation based on signal history
    // Rising signal: less drive (cleaner transients)
    // Falling signal: more drive (warmer sustain)
    struct HysteresisDriveModulator
    {
        float previousSample = 0.0f;
        float magneticState = 0.0f;
        float smoothedOffset = 0.0f;
        float smoothingCoeff = 0.995f;

        // Rate-compensated coefficients (calculated in prepare)
        float magneticDecay = 0.9999f;    // Per-sample decay for magneticState
        float trackingCoeff = 0.1f;       // Per-sample tracking rate

        void prepare(double sampleRate, int osFactor = 1);
        void reset();

        // Returns drive multiplier: 1.0 ± 0.05
        float computeDriveMultiplier(float currentSample, float saturationDepth,
                                     float coercivity, float asymmetry);
    };
    HysteresisDriveModulator hysteresisMod;

    // Table regeneration tracking (initialized to valid defaults;
    // needsRegeneration=true ensures first call always generates the table)
    TapeMachine m_lastTableMachine = Swiss800;
    float m_lastTableBias = 0.0f;

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
    EQStandard m_lastEqStandard = static_cast<EQStandard>(-1);
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
    void updateFilters(TapeMachine machine, TapeSpeed speed, TapeType type, float biasAmount,
                      EQStandard eqStandard = NAB);

    // Soft clipping function
    static float softClip(float input, float threshold);

    // Harmonic generation
    float generateHarmonics(float input, const float* harmonicProfile, int numHarmonics);

    // Band drive ratios for frequency-dependent saturation
    struct BandDriveRatios
    {
        float bass;     // Drive multiplier for <200Hz
        float mid;      // Drive multiplier for 200Hz-5kHz (always 1.0)
        float treble;   // Drive multiplier for >5kHz
    };

    BandDriveRatios getBandDriveRatios(TapeMachine machine) const
    {
        if (machine == Swiss800)
            return { 0.55f, 1.0f, 0.20f };  // Studer: precise, less LF/HF saturation
        else
            return { 0.65f, 1.0f, 0.30f };  // Ampex: more musical LF, slightly more HF
    }

    // Compute drive from saturation depth with exponential mapping
    // Calibrated for real tape THD: ~0.3% Studer / ~0.5% Ampex at 0VU
    // The gentle exponential accounts for signal level also increasing with input gain
    float computeDrive(float saturationDepth, float tapeFormulationScale) const
    {
        if (saturationDepth < 0.001f) return 0.0f;
        return 0.62f * std::exp(1.8f * saturationDepth) * tapeFormulationScale;
    }

    static constexpr float denormalPrevention = 1e-8f;

    // Helper to validate filter coefficients are finite (not NaN or Inf)
    static bool validateCoefficients(const juce::dsp::IIR::Coefficients<float>::Ptr& coeffs)
    {
        if (coeffs == nullptr) return false;
        auto* rawCoeffs = coeffs->getRawCoefficients();
        if (rawCoeffs == nullptr) return false;
        size_t numCoeffs = (coeffs->getFilterOrder() + 1) + coeffs->getFilterOrder();
        for (size_t i = 0; i < numCoeffs; ++i)
            if (!std::isfinite(rawCoeffs[i])) return false;
        return true;
    }

    static bool validateCoefficients(const juce::dsp::IIR::Coefficients<double>::Ptr& coeffs)
    {
        if (coeffs == nullptr) return false;
        auto* rawCoeffs = coeffs->getRawCoefficients();
        if (rawCoeffs == nullptr) return false;
        size_t numCoeffs = (coeffs->getFilterOrder() + 1) + coeffs->getFilterOrder();
        for (size_t i = 0; i < numCoeffs; ++i)
            if (!std::isfinite(rawCoeffs[i])) return false;
        return true;
    }
};
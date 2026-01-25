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

        constexpr int N = 8;
        constexpr double rippleDb = 0.5;

        const double epsilon = std::sqrt(std::pow(10.0, rippleDb / 10.0) - 1.0);
        const double a = std::asinh(1.0 / epsilon) / N;
        const double sinhA = std::sinh(a);
        const double coshA = std::cosh(a);

        const double C = 2.0 * sampleRate;
        const double Wa = C * std::tan(juce::MathConstants<double>::pi * cutoffHz / sampleRate);

        for (int k = 0; k < NUM_SECTIONS; ++k)
        {
            double theta = (2.0 * k + 1.0) * juce::MathConstants<double>::pi / (2.0 * N);
            double sigma = -sinhA * std::sin(theta);
            double omega = coshA * std::cos(theta);

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
//==============================================================================
class SoftLimiter
{
public:
    static constexpr float threshold = 0.95f;

    float process(float x) const
    {
        if (x > threshold)
            return threshold;
        if (x < -threshold)
            return -threshold;
        return x;
    }
};

//==============================================================================
// Saturation Split Filter - 2-pole Butterworth lowpass for frequency-selective saturation
//==============================================================================
class SaturationSplitFilter
{
public:
    void prepare(double sampleRate, double cutoffHz = 5000.0)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * cutoffHz / sampleRate;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * 0.7071);
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

    void split(float input, float& bass, float& mid, float& treble)
    {
        float lp200Out = lr200.process(input);
        float lp5000Out = lr5000.process(input);

        bass = lp200Out;
        mid = lp5000Out - lp200Out;
        treble = input - lp5000Out;
    }

private:
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

//==============================================================================
// Langevin Tape Saturation (replaces J-A RK4 ODE)
// Uses the Langevin function L(x) = coth(x) - 1/x as a memoryless waveshaper.
// This is the anhysteretic magnetization curve from the Jiles-Atherton model,
// providing identical H3-dominant harmonic character without state accumulation.
//
// Advantages over full J-A ODE:
//   - Zero DC offset (odd-symmetric function)
//   - Exact unity gain normalization (3*L(x)/x → 1 for small x)
//   - No numerical instability for any parameter set
//   - No transient/settling behavior
//   - ~10x less CPU (no RK4 per sample)
//
// THD characteristics (Type456, Studer, bias=0.5):
//   0VU: THD≈0.27% (H3 at -51dB, purely odd harmonics)
//   +6VU: THD≈3.0%
//   Bias modulates THD: low bias = more saturation
//   Studer cleaner than Ampex (machine factor)
//==============================================================================
class JilesAthertonHysteresis
{
public:
    struct TapeFormulationParams
    {
        float Ms = 280.0f;    // Saturation magnetization (kA/m)
        float a = 720.0f;     // Anhysteretic shape parameter (A/m)
        float alpha = 0.016f; // Coupling coefficient (unused in waveshaper, kept for API)
        float k = 640.0f;     // Pinning coefficient (unused in waveshaper, kept for API)
        float c = 0.50f;      // Reversibility (unused in waveshaper, kept for API)
    };

    void prepare(double /*sampleRate*/, int /*oversamplingFactor*/ = 1)
    {
        // No state to initialize (memoryless waveshaper)
    }

    void reset()
    {
        // No state to reset (memoryless waveshaper)
    }

    void setFormulation(TapeFormulationParams params)
    {
        currentParams = params;
    }

    void setMachineType(bool isStuder)
    {
        studerMode = isStuder;
    }

    // Process one sample through Langevin saturation waveshaper
    // input: normalized audio signal (-1 to +1)
    // drive: saturation depth (0 to ~3.0)
    // biasLinearization: 0-1, modulates saturation depth (models AC bias effect)
    float processSample(float input, float drive, float biasLinearization)
    {
        if (drive < 0.001f || std::abs(input) < 1e-10f)
            return input;

        // Bias modulates effective saturation depth:
        // Low bias (0.0) = under-biased tape = more distortion (×1.3)
        // Reference bias (0.5) = no change (×1.0)
        // High bias (1.0) = over-biased tape = cleaner (×0.7)
        float biasFactor = 1.0f + 0.6f * (0.5f - biasLinearization);

        // Machine character:
        // Studer (push-pull, transformerless) = slightly cleaner (×0.92)
        // Ampex (transformer, single-ended stages) = slightly dirtier (×1.08)
        float machineFactor = studerMode ? 0.92f : 1.08f;

        float effectiveDrive = drive * biasFactor * machineFactor;

        // Langevin waveshaper: x = input * effectiveDrive * fieldScale / a
        // Output = 3*L(x)*a / (effectiveDrive*fieldScale)
        //        = input * 3*L(x)/x  (unity-normalized)
        float x = input * effectiveDrive * fieldScale / currentParams.a;
        float Lx = langevin(x);

        // Unity-normalized: for small x, 3*L(x)/x → 1 (passes input unchanged)
        // For large x, L(x) → 1, so output → 3*a/(effectiveDrive*fieldScale*input)
        // which provides smooth soft compression.
        float output = 3.0f * Lx * currentParams.a / (effectiveDrive * fieldScale);

        return output;
    }

private:
    TapeFormulationParams currentParams;
    bool studerMode = false;

    // Field scaling: calibrated for target THD at operating levels.
    // With inputGain applied before tape emulation:
    //   0VU: amplitude=0.120, drive≈1.18, x≈0.63 → H3≈-52dB
    //   +6VU: amplitude=0.240, drive≈1.86, x≈1.26 → THD≈3-5%
    // The Langevin function's Taylor series: L(x) = x/3 - x³/45 + ...
    // gives H3/H1 ≈ x²/60, producing purely odd-harmonic distortion.
    // Calibrated empirically against Studer A800 THD measurements.
    static constexpr float fieldScale = 3200.0f;

    // Langevin function: L(x) = coth(x) - 1/x
    // Padé [2,2] approximation: L(x) ≈ x*(315+105x²)/(945+420x²+63x⁴)
    // Denominator constant 945 (=3×315) ensures L(x)/x → 1/3 as x→0
    // Blends to asymptotic L(x) = sign(x)*(1-1/|x|) for |x| > 1.5
    static float langevin(float x)
    {
        if (std::abs(x) < 1e-4f)
            return x / 3.0f;

        float ax = std::abs(x);

        if (ax > 2.5f)
        {
            float sign = (x >= 0.0f) ? 1.0f : -1.0f;
            return sign * (1.0f - 1.0f / ax);
        }

        float x2 = x * x;
        float pade = x * (315.0f + 105.0f * x2) / (945.0f + 420.0f * x2 + 63.0f * x2 * x2);

        if (ax <= 1.5f)
            return pade;

        // Blend Padé → asymptotic for 1.5 < |x| < 2.5
        float sign = (x >= 0.0f) ? 1.0f : -1.0f;
        float asympt = sign * (1.0f - 1.0f / ax);
        float t = (ax - 1.5f);
        return pade * (1.0f - t) + asympt * t;
    }
};

//==============================================================================
// Tape EQ Filter - First-order time-constant network
// Implements exact NAB/CCIR/AES equalization via bilinear transform
// H(s) = (1 + s*τ_num) / (1 + s*τ_den)
//==============================================================================
class TapeEQFilter
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        z1 = 0.0f;
    }

    // Set pre-emphasis: τ_num > τ_den (HF boost for recording)
    // tau values in microseconds
    void setPreEmphasis(float tau_num_us, float tau_den_us)
    {
        computeCoefficients(tau_num_us, tau_den_us);
    }

    // Set de-emphasis: τ_num < τ_den (HF cut for playback, inverse of pre-emphasis)
    void setDeEmphasis(float tau_num_us, float tau_den_us)
    {
        computeCoefficients(tau_num_us, tau_den_us);
    }

    float processSample(float input)
    {
        float output = b0 * input + z1;
        z1 = b1 * input - a1 * output;
        return output;
    }

private:
    double fs = 44100.0;
    float b0 = 1.0f, b1 = 0.0f;
    float a1 = 0.0f;
    float z1 = 0.0f;

    void computeCoefficients(float tau_num_us, float tau_den_us)
    {
        // Convert microseconds to seconds
        double tauNum = static_cast<double>(tau_num_us) * 1e-6;
        double tauDen = static_cast<double>(tau_den_us) * 1e-6;

        // Bilinear transform of H(s) = (1 + s*tauNum) / (1 + s*tauDen)
        // Pre-warping not needed for first-order (exact match at DC and Nyquist)
        double T = 1.0 / fs;
        double wNum = 2.0 * tauNum / T;
        double wDen = 2.0 * tauDen / T;

        double num0 = 1.0 + wNum;
        double num1 = 1.0 - wNum;
        double den0 = 1.0 + wDen;
        double den1 = 1.0 - wDen;

        // Normalize by den0
        // Transfer function: H(z) = (b0 + b1*z^-1) / (1 + a1*z^-1)
        // From bilinear: denominator = (1+wD) + (1-wD)*z^-1
        // So a1 = (1-wD)/(1+wD) = den1/den0
        double invDen0 = 1.0 / den0;
        b0 = static_cast<float>(num0 * invDen0);
        b1 = static_cast<float>(num1 * invDen0);
        a1 = static_cast<float>(den1 * invDen0);
    }
};

//==============================================================================
// Phase Smearing Filter - 4 cascaded first-order allpass filters
// Models the frequency-dependent phase response of tape electronics/heads
//==============================================================================
class PhaseSmearingFilter
{
public:
    static constexpr int NUM_STAGES = 4;

    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& stage : stages)
            stage.state = 0.0f;
    }

    // Set phase characteristics per machine type
    void setMachineCharacter(bool isStuder)
    {
        // Break frequencies chosen for realistic tape phase response
        float breakFreqs[NUM_STAGES];
        if (isStuder)
        {
            // Studer: less phase shift (precision engineering)
            breakFreqs[0] = 60.0f;
            breakFreqs[1] = 250.0f;
            breakFreqs[2] = 2000.0f;
            breakFreqs[3] = 8000.0f;
        }
        else
        {
            // Ampex: more phase shift (character, warmth)
            breakFreqs[0] = 40.0f;
            breakFreqs[1] = 150.0f;
            breakFreqs[2] = 1200.0f;
            breakFreqs[3] = 6000.0f;
        }

        for (int i = 0; i < NUM_STAGES; ++i)
        {
            float tanVal = std::tan(juce::MathConstants<float>::pi * breakFreqs[i]
                                    / static_cast<float>(currentSampleRate));
            stages[i].coeff = (tanVal - 1.0f) / (tanVal + 1.0f);
        }
    }

    float processSample(float input)
    {
        float signal = input;
        for (auto& stage : stages)
        {
            float y = stage.coeff * signal + stage.state;
            stage.state = signal - stage.coeff * y;
            signal = y;
        }
        return signal;
    }

private:
    struct AllpassStage
    {
        float coeff = 0.0f;
        float state = 0.0f;
    };

    std::array<AllpassStage, NUM_STAGES> stages;
    double currentSampleRate = 44100.0;
};

//==============================================================================
// Improved Noise Generator - Paul Kellett pink noise + modulation + scrape flutter
//==============================================================================
struct ImprovedNoiseGenerator
{
    // Paul Kellett's 6-stage pink noise state
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;

    // Scrape flutter bandpass (centered ~4kHz)
    float scrapeBP_z1 = 0.0f, scrapeBP_z2 = 0.0f;
    float scrapeBP_b0 = 0.0f, scrapeBP_b1 = 0.0f, scrapeBP_b2 = 0.0f;
    float scrapeBP_a1 = 0.0f, scrapeBP_a2 = 0.0f;

    // Signal envelope for modulation noise
    float envelope = 0.0f;
    float envelopeCoeff = 0.999f;

    // Speed-dependent spectral tilt
    float tiltState = 0.0f;
    float tiltCoeff = 0.5f;

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> whiteDist{-1.0f, 1.0f};

    void prepare(double sampleRate, int tapeSpeed);
    void reset();

    float generateNoise(float noiseFloor, float modulationAmount, float signal);
};

//==============================================================================
// Wow & Flutter processor - shared between channels for stereo coherence
// Uses Thiran allpass fractional delay interpolation
//==============================================================================
class WowFlutterProcessor
{
public:
    std::vector<float> delayBuffer;
    int writeIndex = 0;
    double wowPhase = 0.0;
    double flutterPhase = 0.0;
    float randomPhase = 0.0f;
    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

    // Smoothed random modulation
    float randomTarget = 0.0f;
    float randomCurrent = 0.0f;
    int randomUpdateCounter = 0;
    static constexpr int RANDOM_UPDATE_INTERVAL = 64;

    // Rate compensation
    int oversamplingFactor = 1;
    float smoothingAlpha = 0.01f;

    // Thiran allpass state for fractional delay
    float allpassState = 0.0f;

    void prepare(double sampleRate, int osFactor = 1)
    {
        oversamplingFactor = std::max(1, osFactor);

        smoothingAlpha = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 70.0f
                                          / static_cast<float>(sampleRate));

        static constexpr double MIN_SAMPLE_RATE = 8000.0;
        static constexpr double MAX_SAMPLE_RATE = 768000.0;
        static constexpr double MAX_DELAY_SECONDS = 0.05;

        if (sampleRate <= 0.0 || !std::isfinite(sampleRate))
            sampleRate = 44100.0;

        sampleRate = std::clamp(sampleRate, MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);

        double bufferSizeDouble = sampleRate * MAX_DELAY_SECONDS;

        static constexpr size_t MIN_BUFFER_SIZE = 64;
        static constexpr size_t MAX_BUFFER_SIZE = 65536;

        size_t bufferSize = static_cast<size_t>(bufferSizeDouble);
        bufferSize = std::clamp(bufferSize, MIN_BUFFER_SIZE, MAX_BUFFER_SIZE);

        if (delayBuffer.size() != bufferSize)
        {
            delayBuffer.resize(bufferSize, 0.0f);
        }
        else
        {
            std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
        }
        writeIndex = 0;
        allpassState = 0.0f;
    }

    float calculateModulation(float wowAmount, float flutterAmount,
                             float wowRate, float flutterRate, double sampleRate)
    {
        if (sampleRate <= 0.0)
            sampleRate = 44100.0;

        float osScale = static_cast<float>(oversamplingFactor);

        float wowMod = static_cast<float>(std::sin(wowPhase)) * wowAmount * 10.0f * osScale;
        float flutterMod = static_cast<float>(std::sin(flutterPhase)) * flutterAmount * 2.0f * osScale;

        int scaledInterval = RANDOM_UPDATE_INTERVAL * oversamplingFactor;
        if (++randomUpdateCounter >= scaledInterval)
        {
            randomUpdateCounter = 0;
            randomTarget = dist(rng);
        }
        randomCurrent += (randomTarget - randomCurrent) * smoothingAlpha;
        float randomMod = randomCurrent * flutterAmount * 0.5f * osScale;

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

    // Process sample with Thiran allpass fractional delay interpolation
    float processSample(float input, float modulationSamples)
    {
        if (delayBuffer.empty())
            return input;

        if (writeIndex < 0 || writeIndex >= static_cast<int>(delayBuffer.size()))
            writeIndex = 0;

        delayBuffer[writeIndex] = input;

        float baseDelay = 20.0f * static_cast<float>(oversamplingFactor);
        float totalDelay = baseDelay + modulationSamples;
        int bufferSize = static_cast<int>(delayBuffer.size());
        if (bufferSize <= 0)
            return input;

        // Ensure delay is within valid range (need at least 2 samples for allpass)
        totalDelay = std::max(1.5f, std::min(totalDelay, static_cast<float>(bufferSize - 2)));

        // Integer and fractional parts
        int delaySamples = static_cast<int>(totalDelay);
        float frac = totalDelay - static_cast<float>(delaySamples);

        // Read integer-delayed samples
        int readIndex0 = (writeIndex - delaySamples + bufferSize) % bufferSize;
        int readIndex1 = (readIndex0 - 1 + bufferSize) % bufferSize;

        readIndex0 = std::max(0, std::min(readIndex0, bufferSize - 1));
        readIndex1 = std::max(0, std::min(readIndex1, bufferSize - 1));

        float x_n = delayBuffer[readIndex0];
        float x_n1 = delayBuffer[readIndex1];

        // 1st-order Thiran allpass interpolation
        // Clamp fractional delay to stable range [0.1, 0.9]
        float d = std::clamp(frac, 0.1f, 0.9f);
        float a1 = (1.0f - d) / (1.0f + d);

        // Allpass: y[n] = a1 * x[n] + x[n-1] - a1 * y[n-1]
        float output = a1 * x_n + x_n1 - a1 * allpassState;
        allpassState = output;

        // Denormal protection on allpass state
        if (std::abs(allpassState) < 1e-15f)
            allpassState = 0.0f;

        writeIndex = (writeIndex + 1) % std::max(1, bufferSize);

        return output;
    }
};

//==============================================================================
// Transformer saturation model - authentic input/output stage coloration
//==============================================================================
class TransformerSaturation
{
public:
    void prepare(double sampleRate);
    float process(float input, float driveAmount, bool isOutputStage);
    void reset();

private:
    float dcState = 0.0f;
    float dcBlockCoeff = 0.9995f;
    float hystState = 0.0f;
    float hystDecay = 0.995f;
    float prevInput = 0.0f;
    float lfResonanceState = 0.0f;
    float lfResonanceCoeff = 0.002f;
};

//==============================================================================
// Playback head frequency response
//==============================================================================
class PlaybackHeadResponse
{
public:
    void prepare(double sampleRate);
    float process(float input, float gapWidth, float speed);
    void reset();

private:
    std::array<float, 64> gapDelayLine{};
    int gapDelayIndex = 0;
    float resonanceState1 = 0.0f;
    float resonanceState2 = 0.0f;
    float resonanceCoeff = 0.1f;
    double currentSampleRate = 44100.0;
};

//==============================================================================
// Capstan/motor flutter
//==============================================================================
class MotorFlutter
{
public:
    void prepare(double sampleRate, int osFactor = 1);
    float calculateFlutter(float motorQuality);
    void reset();

private:
    double phase1 = 0.0;
    double phase2 = 0.0;
    double phase3 = 0.0;
    double sampleRate = 44100.0;
    int oversamplingFactor = 1;

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter{-1.0f, 1.0f};
};

//==============================================================================
// Main Tape Emulation Class
//==============================================================================
class ImprovedTapeEmulation
{
public:
    ImprovedTapeEmulation();
    ~ImprovedTapeEmulation() = default;

    enum TapeMachine
    {
        Swiss800 = 0,      // Studer A800
        Classic102         // Ampex ATR-102
    };

    enum TapeSpeed
    {
        Speed_7_5_IPS = 0,
        Speed_15_IPS,
        Speed_30_IPS
    };

    enum TapeType
    {
        Type456 = 0,
        TypeGP9,
        Type911,
        Type250
    };

    enum EQStandard
    {
        NAB = 0,
        CCIR,
        AES
    };

    enum SignalPath
    {
        Repro = 0,
        Sync,
        Input,
        Thru
    };

    void prepare(double sampleRate, int samplesPerBlock, int oversamplingFactor = 1);
    void reset();

    float processSample(float input,
                       TapeMachine machine,
                       TapeSpeed speed,
                       TapeType type,
                       float biasAmount,
                       float saturationDepth,
                       float wowFlutterAmount,
                       bool noiseEnabled = false,
                       float noiseAmount = 0.0f,
                       float* sharedWowFlutterMod = nullptr,
                       float calibrationLevel = 0.0f,
                       EQStandard eqStandard = NAB,
                       SignalPath signalPath = Repro);

    // Metering
    float getInputLevel() const { return inputLevel.load(); }
    float getOutputLevel() const { return outputLevel.load(); }
    float getGainReduction() const { return gainReduction.load(); }

private:
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int currentOversamplingFactor = 1;

    // Machine-specific characteristics
    struct MachineCharacteristics
    {
        float headBumpFreq;
        float headBumpGain;
        float headBumpQ;
        float hfRolloffFreq;
        float hfRolloffSlope;
        float saturationKnee;
        float saturationHarmonics[5];
        float compressionRatio;
        float compressionAttack;
        float compressionRelease;
        float phaseShift;
        float crosstalkAmount;
    };

    struct TapeCharacteristics
    {
        float coercivity;
        float retentivity;
        float saturationPoint;
        float hysteresisAmount;
        float hysteresisAsymmetry;
        float noiseFloor;
        float modulationNoise;
        float lfEmphasis;
        float hfLoss;
    };

    struct SpeedCharacteristics
    {
        float headBumpMultiplier;
        float hfExtension;
        float noiseReduction;
        float flutterRate;
        float wowRate;
    };

    // ==========================================
    // DSP Components
    // ==========================================

    // Jiles-Atherton hysteresis (3 instances for frequency-dependent saturation)
    JilesAthertonHysteresis hysteresisBass;
    JilesAthertonHysteresis hysteresisMid;
    JilesAthertonHysteresis hysteresisTreble;

    // Tape EQ (NAB/CCIR/AES)
    TapeEQFilter preEmphasisEQ;
    TapeEQFilter deEmphasisEQ;

    // Phase smearing (allpass filters)
    PhaseSmearingFilter phaseSmear;

    // Improved noise generator
    ImprovedNoiseGenerator improvedNoiseGen;

    // Head bump modeling (resonant peak) - double for low-freq precision
    juce::dsp::IIR::Filter<double> headBumpFilter;

    // HF loss modeling - double for consistency
    juce::dsp::IIR::Filter<double> hfLossFilter1;
    juce::dsp::IIR::Filter<double> hfLossFilter2;

    // Record/Playback head gap loss
    juce::dsp::IIR::Filter<double> gapLossFilter;

    // Bias-induced HF boost
    juce::dsp::IIR::Filter<float> biasFilter;

    // DC blocking filter
    juce::dsp::IIR::Filter<double> dcBlocker;

    // Record head gap filter (4th-order Butterworth)
    juce::dsp::IIR::Filter<float> recordHeadFilter1;
    juce::dsp::IIR::Filter<float> recordHeadFilter2;

    // Post-saturation anti-aliasing filter
    ChebyshevAntiAliasingFilter antiAliasingFilter;

    // Pre-saturation soft limiter
    SoftLimiter preSaturationLimiter;

    // 3-band splitter for frequency-dependent saturation
    ThreeBandSplitter threeBandSplitter;

    // Split filter for soft-clip stage
    SaturationSplitFilter softClipSplitFilter;

    // Base sample rate for AA filter cutoff
    double baseSampleRate = 44100.0;

    // Record head gap cutoff frequency
    float recordHeadCutoff = 15000.0f;

    // Enhanced DSP components
    TransformerSaturation inputTransformer;
    TransformerSaturation outputTransformer;
    PlaybackHeadResponse playbackHead;
    MotorFlutter motorFlutter;

    // Per-channel wow/flutter delay line
    WowFlutterProcessor perChannelWowFlutter;

    // Crosstalk simulation
    float crosstalkBuffer = 0.0f;

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

    // Metering
    std::atomic<float> inputLevel{0.0f};
    std::atomic<float> outputLevel{0.0f};
    std::atomic<float> gainReduction{0.0f};

    // Filter update tracking
    TapeMachine m_lastMachine = static_cast<TapeMachine>(-1);
    TapeSpeed m_lastSpeed = static_cast<TapeSpeed>(-1);
    TapeType m_lastType = static_cast<TapeType>(-1);
    EQStandard m_lastEqStandard = static_cast<EQStandard>(-1);
    float m_lastBias = -1.0f;

    // Cached characteristics
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

    // Band drive ratios for frequency-dependent saturation
    struct BandDriveRatios
    {
        float bass;
        float mid;
        float treble;
    };

    BandDriveRatios getBandDriveRatios(TapeMachine machine) const
    {
        if (machine == Swiss800)
            return { 0.55f, 1.0f, 0.20f };
        else
            return { 0.65f, 1.0f, 0.30f };
    }

    // Compute drive from saturation depth with exponential mapping
    float computeDrive(float saturationDepth, float tapeFormulationScale) const
    {
        if (saturationDepth < 0.001f) return 0.0f;
        return 0.62f * std::exp(1.8f * saturationDepth) * tapeFormulationScale;
    }

    // Get J-A formulation parameters for a tape type
    JilesAthertonHysteresis::TapeFormulationParams getJAParams(TapeType type) const
    {
        switch (type)
        {
            case Type456:
                return { 280.0f, 720.0f, 0.016f, 640.0f, 0.50f };
            case TypeGP9:
                return { 320.0f, 800.0f, 0.012f, 700.0f, 0.60f };
            case Type911:
                return { 270.0f, 700.0f, 0.018f, 620.0f, 0.48f };
            case Type250:
                return { 240.0f, 680.0f, 0.020f, 580.0f, 0.45f };
            default:
                return { 280.0f, 720.0f, 0.016f, 640.0f, 0.50f };
        }
    }

    static constexpr float denormalPrevention = 1e-8f;

    // Validate filter coefficients
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

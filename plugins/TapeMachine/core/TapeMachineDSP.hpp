// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// TapeMachineDSP.hpp — framework-free (no-JUCE) port of the TapeMachine
// tape-emulation DSP core, for the DPF build. This is a TRANSLATION of
// Source/ImprovedTapeEmulation.{h,cpp} + the driving logic in
// Source/PluginProcessor.cpp (prepareToPlay / processBlock / updateFilters).
// Every formula, constant, coefficient and per-sample stage order is preserved
// from the JUCE original. Substitutions and judgment calls are recorded in
// core/PORT_NOTES.md.

#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include <algorithm>

#include "DuskFilters.hpp"     // Biquad (float) + designers, OnePole etc.
#include "DuskSVF.hpp"         // DuskSVF (TPT SVF)
#include "DuskSmoothed.hpp"    // SmoothedValue (exp one-pole)
#include "DuskOversampler.hpp" // Oversampler
#include "DuskDenormals.hpp"   // ScopedFlushDenormals

namespace duskaudio
{

//==============================================================================
// Constants and helpers replacing juce::MathConstants / juce::Decibels.
//==============================================================================
constexpr double kPiD    = 3.14159265358979323846;
constexpr float  kPiF    = 3.14159265358979323846f;
constexpr double kTwoPiD = 6.28318530717958647692;
constexpr float  kTwoPiF = 6.28318530717958647692f;

// juce::Decibels::decibelsToGain (default minus-infinity dB = -100).
inline float  dbToGain (float db)  noexcept { return db > -100.0f ? std::pow (10.0f, db * 0.05f) : 0.0f; }
inline double dbToGainD(double db) noexcept { return db > -100.0  ? std::pow (10.0,  db * 0.05)  : 0.0;  }

//==============================================================================
// DBiquad — DOUBLE-precision transposed-direct-form-II biquad.
//
// The JUCE original uses juce::dsp::IIR::Filter<double> for headBump / hfLoss1 /
// hfLoss2 / gapLoss / dcBlocker ("double for low-freq precision"), processing
// with static_cast<double>(signal). The shared duskaudio::Biquad is float-only,
// so this is a double copy of the exact same designer formulas the shared
// Biquad uses (which the shared header states match juce::dsp::IIR). Kept in
// double so the low-frequency high-Q head-bump stays stable at 4x rates.
//==============================================================================
struct DBiquadCoeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };

class DBiquad
{
public:
    void setCoeffs (const DBiquadCoeffs& k) noexcept { c = k; }
    void reset() noexcept { z1 = z2 = 0.0; }

    double process (double x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    // juce::dsp::IIR makePeakFilter (RBJ peaking EQ, alpha = sin(w0)/(2Q)).
    static DBiquadCoeffs peak (double fs, double freq, double gainDb, double Q) noexcept
    {
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = kTwoPiD * freq / fs;
        const double cw    = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * Q);
        const double b0 = 1.0 + alpha * A;
        const double b1 = -2.0 * cw;
        const double b2 = 1.0 - alpha * A;
        const double a0 = 1.0 + alpha / A;
        const double a1 = -2.0 * cw;
        const double a2 = 1.0 - alpha / A;
        const double inv = 1.0 / a0;
        return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
    }

    // juce::dsp::IIR makeLowPass(fs, freq, Q).
    static DBiquadCoeffs lowPass (double fs, double freq, double Q) noexcept
    {
        const double n   = 1.0 / std::tan (kPiD * freq / fs);
        const double nSq = n * n;
        const double invQ = 1.0 / Q;
        const double c1  = 1.0 / (1.0 + invQ * n + nSq);
        return { c1, c1 * 2.0, c1, c1 * 2.0 * (1.0 - nSq), c1 * (1.0 - invQ * n + nSq) };
    }

    // juce::dsp::IIR makeHighPass(fs, freq, Q).
    static DBiquadCoeffs highPass (double fs, double freq, double Q) noexcept
    {
        const double n   = std::tan (kPiD * freq / fs);
        const double nSq = n * n;
        const double invQ = 1.0 / Q;
        const double c1  = 1.0 / (1.0 + invQ * n + nSq);
        return { c1, c1 * -2.0, c1, c1 * 2.0 * (nSq - 1.0), c1 * (1.0 - invQ * n + nSq) };
    }

    // juce::dsp::IIR makeHighShelf / makeLowShelf (RBJ, alpha = sin(w0)/(2Q)).
    static DBiquadCoeffs shelf (double fs, double freq, double gainDb, double Q, bool high) noexcept
    {
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = kTwoPiD * freq / fs;
        const double cw    = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * Q);
        const double sqA2a = 2.0 * std::sqrt (A) * alpha;
        double b0, b1, b2, a0, a1, a2;
        if (high)
        {
            b0 =  A * ((A + 1.0) + (A - 1.0) * cw + sqA2a);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
            b2 =  A * ((A + 1.0) + (A - 1.0) * cw - sqA2a);
            a0 =      (A + 1.0) - (A - 1.0) * cw + sqA2a;
            a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cw);
            a2 =      (A + 1.0) - (A - 1.0) * cw - sqA2a;
        }
        else
        {
            b0 =  A * ((A + 1.0) - (A - 1.0) * cw + sqA2a);
            b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
            b2 =  A * ((A + 1.0) - (A - 1.0) * cw - sqA2a);
            a0 =      (A + 1.0) + (A - 1.0) * cw + sqA2a;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
            a2 =      (A + 1.0) + (A - 1.0) * cw - sqA2a;
        }
        const double inv = 1.0 / a0;
        return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
    }

private:
    DBiquadCoeffs c;
    double z1 = 0.0, z2 = 0.0;
};

//==============================================================================
// 8th-order Chebyshev Type I Anti-Aliasing Filter (verbatim port).
//==============================================================================
class ChebyshevAntiAliasingFilter
{
public:
    static constexpr int NUM_SECTIONS = 4; // 4 biquads = 8th order

    struct BiquadState { float z1 = 0.0f, z2 = 0.0f; };
    struct BiquadCoeffs { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f; float a1 = 0.0f, a2 = 0.0f; };

    void prepare (double sampleRate, double cutoffHz)
    {
        cutoffHz = std::min (cutoffHz, sampleRate * 0.45);
        cutoffHz = std::max (cutoffHz, 20.0);

        constexpr int N = 8;
        constexpr double rippleDb = 0.5;

        const double epsilon = std::sqrt (std::pow (10.0, rippleDb / 10.0) - 1.0);
        const double a = std::asinh (1.0 / epsilon) / N;
        const double sinhA = std::sinh (a);
        const double coshA = std::cosh (a);

        const double C = 2.0 * sampleRate;
        const double Wa = C * std::tan (kPiD * cutoffHz / sampleRate);

        for (int k = 0; k < NUM_SECTIONS; ++k)
        {
            double theta = (2.0 * k + 1.0) * kPiD / (2.0 * N);
            double sigma = -sinhA * std::sin (theta);
            double omega = coshA * std::cos (theta);

            double poleMagSq = sigma * sigma + omega * omega;
            double A = Wa * Wa * poleMagSq;
            double B = 2.0 * (-sigma) * Wa * C;
            double a0 = C * C + B + A;
            double a0_inv = 1.0 / a0;

            coeffs[k].b0 = static_cast<float> (A * a0_inv);
            coeffs[k].b1 = static_cast<float> (2.0 * A * a0_inv);
            coeffs[k].b2 = coeffs[k].b0;
            coeffs[k].a1 = static_cast<float> (2.0 * (A - C * C) * a0_inv);
            coeffs[k].a2 = static_cast<float> ((C * C - B + A) * a0_inv);
        }

        reset();
    }

    void reset() { for (auto& state : states) state = BiquadState{}; }

    float process (float input)
    {
        float signal = input;
        for (int i = 0; i < NUM_SECTIONS; ++i)
            signal = processBiquad (signal, coeffs[i], states[i]);
        if (std::abs (signal) < 1e-15f) signal = 0.0f;
        return signal;
    }

private:
    std::array<BiquadCoeffs, NUM_SECTIONS> coeffs;
    std::array<BiquadState, NUM_SECTIONS> states;

    float processBiquad (float input, const BiquadCoeffs& c, BiquadState& s)
    {
        float output = c.b0 * input + s.z1;
        s.z1 = c.b1 * input - c.a1 * output + s.z2;
        s.z2 = c.b2 * input - c.a2 * output;
        return output;
    }
};

//==============================================================================
// Soft Limiter (verbatim).
//==============================================================================
class SoftLimiter
{
public:
    static constexpr float threshold = 0.95f;
    float process (float x) const
    {
        if (x > threshold)  return threshold;
        if (x < -threshold) return -threshold;
        return x;
    }
};

//==============================================================================
// Saturation Split Filter — 2-pole Butterworth lowpass (verbatim).
//==============================================================================
class SaturationSplitFilter
{
public:
    void prepare (double sampleRate, double cutoffHz = 5000.0)
    {
        const double w0 = 2.0 * kPiD * cutoffHz / sampleRate;
        const double cosw0 = std::cos (w0);
        const double sinw0 = std::sin (w0);
        const double alpha = sinw0 / (2.0 * 0.7071);
        const double a0 = 1.0 + alpha;

        b0 = static_cast<float> (((1.0 - cosw0) / 2.0) / a0);
        b1 = static_cast<float> ((1.0 - cosw0) / a0);
        b2 = b0;
        a1 = static_cast<float> ((-2.0 * cosw0) / a0);
        a2 = static_cast<float> ((1.0 - alpha) / a0);
        reset();
    }

    void reset() { z1 = z2 = 0.0f; }

    float process (float input)
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
// 3-Band Splitter (verbatim).
//==============================================================================
class ThreeBandSplitter
{
public:
    void prepare (double sampleRate)
    {
        lr200.prepare (sampleRate, 200.0f);
        lr5000.prepare (sampleRate, 5000.0f);
    }
    void reset() { lr200.reset(); lr5000.reset(); }

    void split (float input, float& bass, float& mid, float& treble)
    {
        float lp200Out  = lr200.process (input);
        float lp5000Out = lr5000.process (input);
        bass   = lp200Out;
        mid    = lp5000Out - lp200Out;
        treble = input - lp5000Out;
    }

private:
    struct LR2Filter
    {
        struct OnePoleLP
        {
            float state = 0.0f, coeff = 0.0f;
            void prepare (double sampleRate, float cutoffHz)
            {
                float g = std::tan (kPiF * cutoffHz / static_cast<float> (sampleRate));
                coeff = g / (1.0f + g);
                state = 0.0f;
            }
            void reset() { state = 0.0f; }
            float process (float input)
            {
                float v = (input - state) * coeff;
                float output = v + state;
                state = output + v;
                return output;
            }
        };
        OnePoleLP stage1, stage2;
        void prepare (double sampleRate, float cutoffHz) { stage1.prepare (sampleRate, cutoffHz); stage2.prepare (sampleRate, cutoffHz); }
        void reset() { stage1.reset(); stage2.reset(); }
        float process (float input) { return stage2.process (stage1.process (input)); }
    };

    LR2Filter lr200, lr5000;
};

//==============================================================================
// Jiles-Atherton Tape Hysteresis (verbatim RK4 J-A model).
//==============================================================================
class JilesAthertonHysteresis
{
public:
    struct TapeFormulationParams
    {
        float Ms = 280.0f;    float a = 720.0f;   float alpha = 0.016f;
        float k = 640.0f;     float c = 0.50f;
    };

    void prepare (double /*sampleRate*/, int /*oversamplingFactor*/ = 1) { prevM = 0.0f; prevH = 0.0f; }
    void reset() { prevM = 0.0f; prevH = 0.0f; }
    void setFormulation (TapeFormulationParams params) { currentParams = params; }
    void setMachineType (bool isPrecision) { precisionMode = isPrecision; }

    float processSample (float input, float drive, float biasLinearization)
    {
        if (drive < 0.001f) { prevM = 0.0f; prevH = 0.0f; return input; }

        const float biasFactor = 1.0f + 0.6f * (0.5f - biasLinearization);
        const float machineFactor = precisionMode ? 0.92f : 1.08f;
        const float effectiveDrive = drive * biasFactor * machineFactor;

        const float Ms    = currentParams.Ms;
        const float a     = currentParams.a;
        const float alpha = currentParams.alpha;
        const float k     = currentParams.k;
        const float c     = currentParams.c;

        const float H  = input * effectiveDrive * fieldScale;
        const float dH = H - prevH;
        const float delta = (dH >= 0.0f) ? 1.0f : -1.0f;

        auto dMdH = [&] (float Hx, float Mx) -> float
        {
            const float He     = Hx + alpha * Mx;
            const float q      = He / a;
            const float Man    = Ms * langevin (q);
            const float dManHe = (Ms / a) * langevinDeriv (q);
            const float Mdiff  = Man - Mx;
            const bool  sameDir = (delta > 0.0f) ? (Mdiff > 0.0f) : (Mdiff < 0.0f);
            const float deltaM = sameDir ? 1.0f : 0.0f;

            float kap = (1.0f - c) * delta * k - alpha * Mdiff;
            if (std::abs (kap) < 1e-4f) kap = (kap < 0.0f ? -1e-4f : 1e-4f);
            const float irr = deltaM * Mdiff / kap;

            const float num = (1.0f - c) * irr + c * dManHe;
            float den = 1.0f - alpha * c * dManHe;
            if (std::abs (den) < 1e-4f) den = (den < 0.0f ? -1e-4f : 1e-4f);
            return num / den;
        };

        const float k1 = dMdH (prevH,             prevM);
        const float k2 = dMdH (prevH + 0.5f * dH, prevM + 0.5f * dH * k1);
        const float k3 = dMdH (prevH + 0.5f * dH, prevM + 0.5f * dH * k2);
        const float k4 = dMdH (prevH + dH,        prevM + dH * k3);
        float M = prevM + (dH / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);

        if (! std::isfinite (M)) { prevM = 0.0f; prevH = H; return input; }
        const float Mlim = 1.5f * Ms;
        M = (M > Mlim) ? Mlim : (M < -Mlim ? -Mlim : M);

        prevM = M;
        prevH = H;

        const float loopOut   = M * 3.0f * a / (Ms * effectiveDrive * fieldScale);
        const float anhystOut = 3.0f * langevin (H / a) * a / (effectiveDrive * fieldScale);
        const float loopWeight = (1.0f - biasLinearization) * 0.5f;
        const float output = anhystOut + loopWeight * (loopOut - anhystOut);
        return std::isfinite (output) ? output : input;
    }

private:
    TapeFormulationParams currentParams;
    bool precisionMode = false;
    float prevM = 0.0f, prevH = 0.0f;
    static constexpr float fieldScale = 3200.0f;

public:
    static float langevin (float x)
    {
        if (std::abs (x) < 1e-4f) return x / 3.0f;
        float ax = std::abs (x);
        if (ax > 2.5f)
        {
            float sign = (x >= 0.0f) ? 1.0f : -1.0f;
            return sign * (1.0f - 1.0f / ax);
        }
        float x2 = x * x;
        float pade = x * (315.0f + 105.0f * x2) / (945.0f + 420.0f * x2 + 63.0f * x2 * x2);
        if (ax <= 1.5f) return pade;
        float sign = (x >= 0.0f) ? 1.0f : -1.0f;
        float asympt = sign * (1.0f - 1.0f / ax);
        float t = (ax - 1.5f);
        return pade * (1.0f - t) + asympt * t;
    }

    static float langevinDeriv (float x)
    {
        const float ax = std::abs (x);
        if (ax < 1.0e-4f) return 1.0f / 3.0f;
        const float x2 = x * x;
        const float N  = 315.0f * x + 105.0f * x * x2;
        const float D  = 945.0f + 420.0f * x2 + 63.0f * x2 * x2;
        const float Np = 315.0f + 315.0f * x2;
        const float Dp = 840.0f * x + 252.0f * x * x2;
        const float padeP = (Np * D - N * Dp) / (D * D);
        if (ax <= 1.5f) return padeP;
        const float asymptP = 1.0f / x2;
        if (ax >= 2.5f) return asymptP;
        const float s      = (x >= 0.0f) ? 1.0f : -1.0f;
        const float pade   = N / D;
        const float asympt = s - 1.0f / x;
        const float t      = ax - 1.5f;
        return padeP * (1.0f - t) + asymptP * t + s * (asympt - pade);
    }
};

//==============================================================================
// Tape EQ Filter — first-order time-constant network (verbatim).
//==============================================================================
class TapeEQFilter
{
public:
    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset() { z1 = 0.0f; }
    void setPreEmphasis (float tau_num_us, float tau_den_us) { computeCoefficients (tau_num_us, tau_den_us); }
    void setDeEmphasis  (float tau_num_us, float tau_den_us) { computeCoefficients (tau_num_us, tau_den_us); }

    float processSample (float input)
    {
        float output = b0 * input + z1;
        z1 = b1 * input - a1 * output;
        return output;
    }

private:
    double fs = 44100.0;
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f, z1 = 0.0f;

    void computeCoefficients (float tau_num_us, float tau_den_us)
    {
        double tauNum = static_cast<double> (tau_num_us) * 1e-6;
        double tauDen = static_cast<double> (tau_den_us) * 1e-6;
        double T = 1.0 / fs;
        double wNum = 2.0 * tauNum / T;
        double wDen = 2.0 * tauDen / T;
        double num0 = 1.0 + wNum;
        double num1 = 1.0 - wNum;
        double den0 = 1.0 + wDen;
        double den1 = 1.0 - wDen;
        double invDen0 = 1.0 / den0;
        b0 = static_cast<float> (num0 * invDen0);
        b1 = static_cast<float> (num1 * invDen0);
        a1 = static_cast<float> (den1 * invDen0);
    }
};

//==============================================================================
// Phase Smearing Filter — 4 cascaded first-order allpass (verbatim).
//==============================================================================
class PhaseSmearingFilter
{
public:
    static constexpr int NUM_STAGES = 4;

    void prepare (double sampleRate) { currentSampleRate = sampleRate; reset(); }
    void reset() { for (auto& stage : stages) stage.state = 0.0f; }

    void setMachineCharacter (bool isPrecision)
    {
        float breakFreqs[NUM_STAGES];
        if (isPrecision) { breakFreqs[0] = 60.0f;  breakFreqs[1] = 250.0f; breakFreqs[2] = 2000.0f; breakFreqs[3] = 8000.0f; }
        else             { breakFreqs[0] = 40.0f;  breakFreqs[1] = 150.0f; breakFreqs[2] = 1200.0f; breakFreqs[3] = 6000.0f; }

        for (int i = 0; i < NUM_STAGES; ++i)
        {
            float tanVal = std::tan (kPiF * breakFreqs[i] / static_cast<float> (currentSampleRate));
            stages[i].coeff = (tanVal - 1.0f) / (tanVal + 1.0f);
        }
    }

    float processSample (float input)
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
    struct AllpassStage { float coeff = 0.0f, state = 0.0f; };
    std::array<AllpassStage, NUM_STAGES> stages;
    double currentSampleRate = 44100.0;
};

//==============================================================================
// Improved Noise Generator (verbatim; RNG made deterministic — see PORT_NOTES).
//==============================================================================
struct ImprovedNoiseGenerator
{
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;

    float scrapeBP_z1 = 0.0f, scrapeBP_z2 = 0.0f;
    float scrapeBP_b0 = 0.0f, scrapeBP_b1 = 0.0f, scrapeBP_b2 = 0.0f;
    float scrapeBP_a1 = 0.0f, scrapeBP_a2 = 0.0f;

    float envelope = 0.0f;
    float envelopeCoeff = 0.999f;
    float tiltState = 0.0f;
    float tiltCoeff = 0.5f;

    std::mt19937 rng;                                        // deterministic (fixed seed)
    std::uniform_real_distribution<float> whiteDist{ -1.0f, 1.0f };

    void setSeed (std::uint32_t s) { rng.seed (s); }

    void prepare (double sampleRate, int tapeSpeed)
    {
        float tiltFreq = (tapeSpeed == 0) ? 800.0f : (tapeSpeed == 1) ? 1500.0f : 3000.0f;
        tiltCoeff = 1.0f - std::exp (-2.0f * kPiF * tiltFreq / static_cast<float> (sampleRate));
        envelopeCoeff = 1.0f - std::exp (-2.0f * kPiF * 100.0f / static_cast<float> (sampleRate));

        float fc = 4000.0f, Q = 2.0f;
        float w0 = 2.0f * kPiF * fc / static_cast<float> (sampleRate);
        float cosw0 = std::cos (w0), sinw0 = std::sin (w0);
        float alpha = sinw0 / (2.0f * Q);
        float a0 = 1.0f + alpha;

        scrapeBP_b0 = (alpha) / a0;
        scrapeBP_b1 = 0.0f;
        scrapeBP_b2 = (-alpha) / a0;
        scrapeBP_a1 = (-2.0f * cosw0) / a0;
        scrapeBP_a2 = (1.0f - alpha) / a0;
        reset();
    }

    void reset()
    {
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f;
        scrapeBP_z1 = scrapeBP_z2 = 0.0f;
        envelope = 0.0f;
        tiltState = 0.0f;
    }

    float generateNoise (float noiseFloor, float modulationAmount, float signal)
    {
        float white = whiteDist (rng);
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;

        float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.11f;
        b6 = white * 0.115926f;

        tiltState += (pink - tiltState) * tiltCoeff;
        float tiltedNoise = pink - tiltState * 0.5f;

        float absSignal = std::abs (signal);
        envelope += (absSignal - envelope) * envelopeCoeff;
        float modNoise = tiltedNoise * (1.0f + envelope * modulationAmount * 8.0f);

        float scrapeWhite = whiteDist (rng);
        float scrapeOut = scrapeBP_b0 * scrapeWhite + scrapeBP_z1;
        scrapeBP_z1 = scrapeBP_b1 * scrapeWhite - scrapeBP_a1 * scrapeOut + scrapeBP_z2;
        scrapeBP_z2 = scrapeBP_b2 * scrapeWhite - scrapeBP_a2 * scrapeOut;

        float totalNoise = modNoise * noiseFloor + scrapeOut * noiseFloor * 0.15f;
        return totalNoise;
    }
};

//==============================================================================
// Wow & Flutter processor — shared between channels for stereo coherence.
// Verbatim Thiran-allpass fractional-delay port. The delay buffer is sized for
// the MAX oversampling rate once, so an oversampling-factor change never
// reallocates on the audio thread (see PORT_NOTES). RNG is deterministic.
//==============================================================================
class WowFlutterProcessor
{
public:
    std::vector<float> delayBuffer;
    int writeIndex = 0;
    double wowPhase = 0.0;
    double flutterPhase = 0.0;
    float randomPhase = 0.0f;
    std::mt19937 rng;                                       // deterministic (fixed seed)
    std::uniform_real_distribution<float> dist{ -1.0f, 1.0f };

    float randomTarget = 0.0f;
    float randomCurrent = 0.0f;
    int randomUpdateCounter = 0;
    static constexpr int RANDOM_UPDATE_INTERVAL = 64;

    int oversamplingFactor = 1;
    float smoothingAlpha = 0.01f;
    float allpassState = 0.0f;

    void setSeed (std::uint32_t s) { rng.seed (s); }

    // sampleRate  = ACTIVE oversampled rate (drives coeffs / phase timing).
    // osFactor    = ACTIVE oversampling factor.
    // maxSampleRate = highest oversampled rate the plugin can run at; the buffer
    //   is sized from THIS so factor changes never reallocate (only re-zero).
    void prepare (double sampleRate, int osFactor, double maxSampleRate)
    {
        oversamplingFactor = std::max (1, osFactor);

        smoothingAlpha = 1.0f - std::exp (-2.0f * kPiF * 70.0f / static_cast<float> (sampleRate));

        static constexpr double MIN_SAMPLE_RATE = 8000.0;
        static constexpr double MAX_SAMPLE_RATE = 768000.0;
        static constexpr double MAX_DELAY_SECONDS = 0.05;

        if (maxSampleRate <= 0.0 || !std::isfinite (maxSampleRate))
            maxSampleRate = 44100.0;

        maxSampleRate = std::clamp (maxSampleRate, MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);

        double bufferSizeDouble = maxSampleRate * MAX_DELAY_SECONDS;

        static constexpr size_t MIN_BUFFER_SIZE = 64;
        static constexpr size_t MAX_BUFFER_SIZE = 65536;

        size_t bufferSize = static_cast<size_t> (bufferSizeDouble);
        bufferSize = std::clamp (bufferSize, MIN_BUFFER_SIZE, MAX_BUFFER_SIZE);

        if (delayBuffer.size() != bufferSize)
            delayBuffer.assign (bufferSize, 0.0f);          // (re)allocate — setup thread only
        else
            std::fill (delayBuffer.begin(), delayBuffer.end(), 0.0f);

        writeIndex = 0;
        allpassState = 0.0f;
    }

    float calculateModulation (float wowAmount, float flutterAmount,
                               float wowRate, float flutterRate, double sampleRate)
    {
        if (sampleRate <= 0.0) sampleRate = 44100.0;

        float osScale = static_cast<float> (oversamplingFactor);

        float wowMod = static_cast<float> (std::sin (wowPhase)) * wowAmount * 10.0f * osScale;
        float flutterMod = static_cast<float> (std::sin (flutterPhase)) * flutterAmount * 2.0f * osScale;

        int scaledInterval = RANDOM_UPDATE_INTERVAL * oversamplingFactor;
        if (++randomUpdateCounter >= scaledInterval)
        {
            randomUpdateCounter = 0;
            randomTarget = dist (rng);
        }
        randomCurrent += (randomTarget - randomCurrent) * smoothingAlpha;
        float randomMod = randomCurrent * flutterAmount * 0.5f * osScale;

        double safeSampleRate = std::max (1.0, sampleRate);
        double wowIncrement = 2.0 * kPiD * wowRate / safeSampleRate;
        double flutterIncrement = 2.0 * kPiD * flutterRate / safeSampleRate;

        wowPhase += wowIncrement;
        if (wowPhase > 2.0 * kPiD) wowPhase -= 2.0 * kPiD;

        flutterPhase += flutterIncrement;
        if (flutterPhase > 2.0 * kPiD) flutterPhase -= 2.0 * kPiD;

        return wowMod + flutterMod + randomMod;
    }

    float processSample (float input, float modulationSamples)
    {
        if (delayBuffer.empty()) return input;
        if (writeIndex < 0 || writeIndex >= static_cast<int> (delayBuffer.size())) writeIndex = 0;

        delayBuffer[writeIndex] = input;

        float baseDelay = 20.0f * static_cast<float> (oversamplingFactor);
        float totalDelay = baseDelay + modulationSamples;
        int bufferSize = static_cast<int> (delayBuffer.size());
        if (bufferSize <= 0) return input;

        totalDelay = std::max (1.5f, std::min (totalDelay, static_cast<float> (bufferSize - 2)));

        int delaySamples = static_cast<int> (totalDelay);
        float frac = totalDelay - static_cast<float> (delaySamples);

        int readIndex0 = (writeIndex - delaySamples + bufferSize) % bufferSize;
        int readIndex1 = (readIndex0 - 1 + bufferSize) % bufferSize;

        readIndex0 = std::max (0, std::min (readIndex0, bufferSize - 1));
        readIndex1 = std::max (0, std::min (readIndex1, bufferSize - 1));

        float x_n  = delayBuffer[readIndex0];
        float x_n1 = delayBuffer[readIndex1];

        float d = std::clamp (frac, 0.1f, 0.9f);
        float a1 = (1.0f - d) / (1.0f + d);

        float output = a1 * x_n + x_n1 - a1 * allpassState;
        allpassState = output;

        if (std::abs (allpassState) < 1e-15f) allpassState = 0.0f;

        writeIndex = (writeIndex + 1) % std::max (1, bufferSize);
        return output;
    }
};

//==============================================================================
// Transformer saturation model (verbatim).
//==============================================================================
class TransformerSaturation
{
public:
    void prepare (double sampleRate)
    {
        dcBlockCoeff = 1.0f - (20.0f * kPiF / static_cast<float> (sampleRate));
        lfResonanceCoeff = 1.0f - std::exp (-2.0f * kPiF * 50.0f / static_cast<float> (sampleRate));
        float targetDecayRate = 220.5f;
        hystDecay = 1.0f - (targetDecayRate / static_cast<float> (sampleRate));
        hystDecay = std::clamp (hystDecay, 0.95f, 0.9999f);
        reset();
    }

    void reset() { dcState = 0.0f; hystState = 0.0f; prevInput = 0.0f; lfResonanceState = 0.0f; }

    float process (float input, float driveAmount, bool isOutputStage)
    {
        float signal = input;

        float dcBlocked = signal - dcState;
        dcState = signal * (1.0f - dcBlockCoeff) + dcState * dcBlockCoeff;
        signal = dcBlocked;

        float asymmetryCoeff = 0.80f * driveAmount;
        if (asymmetryCoeff > 0.0001f)
            signal = signal * (1.0f + asymmetryCoeff * signal);

        float absSignal = std::abs (signal);
        float saturationThreshold = isOutputStage ? 0.92f : 0.95f;
        if (absSignal > saturationThreshold)
        {
            float excess = absSignal - saturationThreshold;
            float headroom = 1.0f - saturationThreshold;
            float limited = saturationThreshold + headroom * (1.0f - std::exp (-excess * 2.0f / headroom));
            signal = (signal >= 0.0f ? 1.0f : -1.0f) * limited;
        }

        if (isOutputStage && driveAmount > 0.01f)
        {
            float resonanceQ = 0.15f * driveAmount;
            lfResonanceState += (signal - lfResonanceState) * lfResonanceCoeff;
            signal += lfResonanceState * resonanceQ;
        }

        float hystAmount = isOutputStage ? 0.005f : 0.002f;
        hystAmount *= driveAmount;
        float hystDelta = signal - prevInput;
        hystState = hystState * hystDecay + hystDelta * hystAmount;
        signal += hystState;
        prevInput = signal;

        return signal;
    }

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
// Playback head frequency response (verbatim).
//==============================================================================
class PlaybackHeadResponse
{
public:
    void prepare (double sampleRate)
    {
        currentSampleRate = sampleRate;
        constexpr float targetCutoff = 740.0f;
        resonanceCoeff = 1.0f - std::exp (-2.0f * kPiF * targetCutoff / static_cast<float> (sampleRate));
        reset();
    }

    void reset()
    {
        std::fill (gapDelayLine.begin(), gapDelayLine.end(), 0.0f);
        gapDelayIndex = 0;
        resonanceState1 = 0.0f;
        resonanceState2 = 0.0f;
    }

    float process (float input, float gapWidth, float speed)
    {
        float speedCmPerSec = speed == 0 ? 19.05f : (speed == 1 ? 38.1f : 76.2f);
        float gapMicrons = gapWidth;

        float delayMs = (gapMicrons * 0.0001f) / speedCmPerSec * 1000.0f;
        float delaySamples = delayMs * 0.001f * static_cast<float> (currentSampleRate);
        delaySamples = std::min (delaySamples, static_cast<float> (gapDelayLine.size() - 1));

        gapDelayLine[static_cast<size_t> (gapDelayIndex)] = input;

        int readIndex = (gapDelayIndex - static_cast<int> (delaySamples) + static_cast<int> (gapDelayLine.size())) % static_cast<int> (gapDelayLine.size());
        float delayedSignal = gapDelayLine[static_cast<size_t> (readIndex)];

        gapDelayIndex = (gapDelayIndex + 1) % static_cast<int> (gapDelayLine.size());

        float gapEffect = input * 0.98f + delayedSignal * 0.02f;

        resonanceState1 += (gapEffect - resonanceState1) * resonanceCoeff;
        resonanceState2 += (resonanceState1 - resonanceState2) * resonanceCoeff;

        float resonanceBoost = (resonanceState1 - resonanceState2) * 0.15f;
        return gapEffect + resonanceBoost;
    }

private:
    std::array<float, 64> gapDelayLine{};
    int gapDelayIndex = 0;
    float resonanceState1 = 0.0f;
    float resonanceState2 = 0.0f;
    float resonanceCoeff = 0.1f;
    double currentSampleRate = 44100.0;
};

//==============================================================================
// Fast sine approximation used by MotorFlutter (verbatim).
//==============================================================================
inline float fastSin (float x)
{
    if (! std::isfinite (x)) return 0.0f;
    constexpr float pi = 3.14159265f;
    constexpr float twoPi = 6.28318530f;
    x = std::fmod (x, twoPi);
    if (x > pi) x -= twoPi;
    else if (x < -pi) x += twoPi;
    constexpr float B = 4.0f / pi;
    constexpr float C = -4.0f / (pi * pi);
    return B * x + C * x * std::abs (x);
}

//==============================================================================
// Capstan/motor flutter (verbatim; deterministic RNG).
//==============================================================================
class MotorFlutter
{
public:
    void setSeed (std::uint32_t s) { rng.seed (s); }

    void prepare (double sr, int osFactor = 1)
    {
        sampleRate = sr;
        oversamplingFactor = std::max (1, osFactor);
        reset();
    }

    void reset() { phase1 = 0.0; phase2 = 0.0; phase3 = 0.0; }

    float calculateFlutter (float motorQuality)
    {
        if (motorQuality < 0.001f) return 0.0f;

        constexpr float twoPiF = 6.28318530f;
        float inc1 = twoPiF * 50.0f / static_cast<float> (sampleRate);
        float inc2 = twoPiF * 15.0f / static_cast<float> (sampleRate);
        float inc3 = twoPiF * 3.0f / static_cast<float> (sampleRate);

        phase1 += inc1; phase2 += inc2; phase3 += inc3;
        if (phase1 > twoPiF) phase1 -= twoPiF;
        if (phase2 > twoPiF) phase2 -= twoPiF;
        if (phase3 > twoPiF) phase3 -= twoPiF;

        float osScale = static_cast<float> (oversamplingFactor);
        float baseFlutter = motorQuality * 0.0004f * osScale;

        float motorComponent = fastSin (static_cast<float> (phase1)) * baseFlutter * 0.3f;
        float bearingComponent = fastSin (static_cast<float> (phase2)) * baseFlutter * 0.5f;
        float eccentricityComponent = fastSin (static_cast<float> (phase3)) * baseFlutter * 0.2f;

        float randomComponent = jitter (rng) * baseFlutter * 0.1f / std::sqrt (osScale);

        return motorComponent + bearingComponent + eccentricityComponent + randomComponent;
    }

private:
    double phase1 = 0.0, phase2 = 0.0, phase3 = 0.0;
    double sampleRate = 44100.0;
    int oversamplingFactor = 1;
    std::mt19937 rng;                                       // deterministic (fixed seed)
    std::uniform_real_distribution<float> jitter{ -1.0f, 1.0f };
};

//==============================================================================
// TapeCore — per-channel tape emulation. Direct port of ImprovedTapeEmulation
// (Source/ImprovedTapeEmulation.{h,cpp}). One instance per channel. All filter
// coefficient recompute (updateFilters) is plain float/double math and is
// RT-safe, so — unlike the JUCE original, which allocated ref-counted
// Coefficients on the audio thread — the internal dirty-check is kept as-is.
//==============================================================================
class TapeCore
{
public:
    enum TapeMachine { Swiss800 = 0, Classic102 };
    enum TapeSpeed   { Speed_7_5_IPS = 0, Speed_15_IPS, Speed_30_IPS };
    enum TapeType    { Type456 = 0, TypeGP9, Type911, Type250 };
    enum EQStandard  { NAB = 0, CCIR, AES };
    enum SignalPath  { Repro = 0, Sync, Input, Thru };

    TapeCore() { reset(); }

    // seedBase distinguishes L/R RNG streams so noise/flutter are decorrelated.
    void setSeeds (std::uint32_t seedBase)
    {
        improvedNoiseGen.setSeed (seedBase + 1u);
        perChannelWowFlutter.setSeed (seedBase + 2u);
        motorFlutter.setSeed (seedBase + 3u);
    }

    // osRate = oversampled sample rate; osFactor = oversampling factor;
    // maxOsRate = highest oversampled rate (wow/flutter buffer sizing).
    void prepare (double osRate, int osFactor, double maxOsRate)
    {
        if (osRate <= 0.0) osRate = 44100.0;
        if (osFactor < 1) osFactor = 1;

        currentSampleRate = osRate;
        currentOversamplingFactor = osFactor;
        baseSampleRate = osRate / static_cast<double> (osFactor);

        double antiAliasingCutoff = baseSampleRate * 0.45;
        antiAliasingFilter.prepare (osRate, antiAliasingCutoff);

        threeBandSplitter.prepare (osRate);

        hysteresisBass.prepare (osRate, osFactor);
        hysteresisMid.prepare (osRate, osFactor);
        hysteresisTreble.prepare (osRate, osFactor);

        preEmphasisEQ.prepare (osRate);
        deEmphasisEQ.prepare (osRate);
        phaseSmear.prepare (osRate);
        improvedNoiseGen.prepare (osRate, 1);
        softClipSplitFilter.prepare (osRate, 5000.0);

        perChannelWowFlutter.prepare (osRate, osFactor, maxOsRate);

        inputTransformer.prepare (osRate);
        outputTransformer.prepare (osRate);
        playbackHead.prepare (osRate);
        motorFlutter.prepare (osRate, osFactor);

        reset();

        auto nyquist = osRate * 0.5;
        auto safeMaxFreq = nyquist * 0.9;
        auto safeFreq = [safeMaxFreq] (float freq) { return std::min (freq, static_cast<float> (safeMaxFreq)); };

        headBumpFilter.setCoeffs (DBiquad::peak (osRate, 60.0, 3.0, 1.5));
        hfLossFilter1.setCoeffs  (DBiquad::lowPass (osRate, static_cast<double> (safeFreq (16000.0f)), 0.707));
        hfLossFilter2.setCoeffs  (DBiquad::shelf (osRate, static_cast<double> (safeFreq (10000.0f)), -2.0, 0.5, true));
        gapLossFilter.setCoeffs  (DBiquad::shelf (osRate, static_cast<double> (safeFreq (12000.0f)), -1.5, 0.707, true));
        biasFilter.setCoeffs     (Biquad::shelf (osRate, safeFreq (8000.0f), 2.0f, 0.707f, true));
        dcBlocker.setCoeffs      (DBiquad::highPass (osRate, 25.0, 0.707));

        recordHeadCutoff = std::min (20000.0f, static_cast<float> (safeMaxFreq));
        recordHeadFilter1.setCoeffs (Biquad::lowPass (osRate, recordHeadCutoff, 1.3066f));
        recordHeadFilter2.setCoeffs (Biquad::lowPass (osRate, recordHeadCutoff, 0.5412f));

        preEmphasisEQ.setPreEmphasis (125.0f, 50.0f);
        deEmphasisEQ.setDeEmphasis   (50.0f, 125.0f);
        phaseSmear.setMachineCharacter (true);
        saturator.updateCoefficients (0.1f, 10.0f, osRate);

        // Force a full updateFilters() on the next processSample so the machine-
        // specific coefficients replace the neutral defaults above (also after an
        // oversampling-factor change). See PORT_NOTES: the JUCE original left
        // m_last* stale here, leaving neutral coeffs after an OS switch.
        m_lastMachine = static_cast<TapeMachine> (-1);
        m_lastSpeed   = static_cast<TapeSpeed> (-1);
        m_lastType    = static_cast<TapeType> (-1);
        m_lastEqStandard = static_cast<EQStandard> (-1);
        m_lastBias    = -1.0f;
    }

    void reset()
    {
        headBumpFilter.reset(); hfLossFilter1.reset(); hfLossFilter2.reset();
        gapLossFilter.reset(); biasFilter.reset(); dcBlocker.reset();
        recordHeadFilter1.reset(); recordHeadFilter2.reset();
        antiAliasingFilter.reset();
        threeBandSplitter.reset(); softClipSplitFilter.reset();
        hysteresisBass.reset(); hysteresisMid.reset(); hysteresisTreble.reset();
        preEmphasisEQ.reset(); deEmphasisEQ.reset(); phaseSmear.reset();
        improvedNoiseGen.reset();
        saturator.envelope = 0.0f;

        if (! perChannelWowFlutter.delayBuffer.empty())
            std::fill (perChannelWowFlutter.delayBuffer.begin(), perChannelWowFlutter.delayBuffer.end(), 0.0f);
        perChannelWowFlutter.writeIndex = 0;
        perChannelWowFlutter.allpassState = 0.0f;

        inputTransformer.reset(); outputTransformer.reset();
        playbackHead.reset(); motorFlutter.reset();
        crosstalkBuffer = 0.0f;
    }

    float processSample (float input,
                         TapeMachine machine, TapeSpeed speed, TapeType type,
                         float biasAmount, float saturationDepth, float wowFlutterAmount,
                         bool noiseEnabled, float noiseAmount,
                         float* sharedWowFlutterMod, float calibrationLevel,
                         EQStandard eqStandard, SignalPath signalPath)
    {
        if (signalPath == Thru) return input;
        if (std::abs (input) < denormalPrevention) return 0.0f;

        if (machine != m_lastMachine || speed != m_lastSpeed || type != m_lastType ||
            std::abs (biasAmount - m_lastBias) > 0.01f || eqStandard != m_lastEqStandard)
        {
            updateFilters (machine, speed, type, biasAmount, eqStandard);
            m_lastMachine = machine; m_lastSpeed = speed; m_lastType = type;
            m_lastBias = biasAmount; m_lastEqStandard = eqStandard;

            m_cachedMachineChars = getMachineCharacteristics (machine);
            m_cachedTapeChars = getTapeCharacteristics (type);
            m_cachedSpeedChars = getSpeedCharacteristics (speed);
            m_hasTransformers = (machine == Classic102);
            m_gapWidth = (machine == Swiss800) ? 2.5f : 3.5f;
        }

        const auto& tapeChars = m_cachedTapeChars;
        const auto& speedChars = m_cachedSpeedChars;

        const bool processTape = (signalPath == Repro || signalPath == Sync);
        const float playbackGapWidth = (signalPath == Sync) ? m_gapWidth * 2.0f : m_gapWidth;

        float calibrationGain = dbToGain (calibrationLevel);
        float signal = input * 0.95f / calibrationGain;

        float transformerDrive = m_hasTransformers ? saturationDepth * 0.3f : 0.0f;
        if (m_hasTransformers)
            signal = inputTransformer.process (signal, transformerDrive, false);

        signal = preEmphasisEQ.processSample (signal);

        if (processTape)
        {
            if (biasAmount > 0.0f)
                signal = biasFilter.process (signal);

            signal = preSaturationLimiter.process (signal);

            if (currentOversamplingFactor > 1)
            {
                signal = recordHeadFilter1.process (signal);
                signal = recordHeadFilter2.process (signal);
            }

            float tapeFormScale = 2.0f * (1.0f - tapeChars.saturationPoint) + 0.6f;
            float drive = computeDrive (saturationDepth, tapeFormScale);

            if (drive > 0.001f)
            {
                float bass, mid, treble;
                threeBandSplitter.split (signal, bass, mid, treble);
                auto ratios = getBandDriveRatios (machine);
                float biasLin = biasAmount;
                float bassSat   = hysteresisBass.processSample (bass, drive * ratios.bass, biasLin);
                float midSat    = hysteresisMid.processSample (mid, drive * ratios.mid, biasLin);
                float trebleSat = hysteresisTreble.processSample (treble, drive * ratios.treble, biasLin);
                signal = bassSat + midSat + trebleSat;
            }

            {
                float lowFreq = softClipSplitFilter.process (signal);
                float highFreq = signal - lowFreq;
                lowFreq = softClip (lowFreq, 0.95f);
                signal = lowFreq + highFreq;
            }

            signal = static_cast<float> (gapLossFilter.process (static_cast<double> (signal)));

            if (wowFlutterAmount > 0.0f)
            {
                float motorQuality = (machine == Swiss800) ? 0.2f : 0.6f;
                float motorFlutterMod = motorFlutter.calculateFlutter (motorQuality * wowFlutterAmount);

                if (sharedWowFlutterMod != nullptr)
                {
                    float totalModulation = *sharedWowFlutterMod + motorFlutterMod * 5.0f;
                    signal = perChannelWowFlutter.processSample (signal, totalModulation);
                }
                else
                {
                    float modulation = perChannelWowFlutter.calculateModulation (
                        wowFlutterAmount * 0.7f, wowFlutterAmount * 0.3f,
                        speedChars.wowRate, speedChars.flutterRate, currentSampleRate);
                    float totalModulation = modulation + motorFlutterMod * 5.0f;
                    signal = perChannelWowFlutter.processSample (signal, totalModulation);
                }
            }

            signal = static_cast<float> (headBumpFilter.process (static_cast<double> (signal)));

            signal = static_cast<float> (hfLossFilter1.process (static_cast<double> (signal)));
            signal = static_cast<float> (hfLossFilter2.process (static_cast<double> (signal)));

            if (signalPath == Sync)
                signal = static_cast<float> (hfLossFilter1.process (static_cast<double> (signal)));

            signal = playbackHead.process (signal, playbackGapWidth, static_cast<float> (speed));
        }

        signal = deEmphasisEQ.processSample (signal);
        signal = phaseSmear.processSample (signal);

        if (m_hasTransformers)
            signal = outputTransformer.process (signal, transformerDrive * 0.5f, true);

        if (processTape && noiseEnabled && noiseAmount > 0.001f)
        {
            float noiseLevel = dbToGain (tapeChars.noiseFloor) * speedChars.noiseReduction * noiseAmount;
            float noise = improvedNoiseGen.generateNoise (noiseLevel, tapeChars.modulationNoise, signal);
            signal += noise;
        }

        signal = static_cast<float> (dcBlocker.process (static_cast<double> (signal)));

        if (currentOversamplingFactor > 1)
            signal = antiAliasingFilter.process (signal);

        if (std::abs (signal) < denormalPrevention) signal = 0.0f;
        return signal;
    }

private:
    double currentSampleRate = 44100.0;
    int currentOversamplingFactor = 1;
    double baseSampleRate = 44100.0;
    float recordHeadCutoff = 15000.0f;

    struct MachineCharacteristics
    {
        float headBumpFreq, headBumpGain, headBumpQ;
        float hfRolloffFreq, hfRolloffSlope;
        float saturationKnee, saturationHarmonics[5];
        float compressionRatio, compressionAttack, compressionRelease;
        float phaseShift, crosstalkAmount;
    };
    struct TapeCharacteristics
    {
        float coercivity, retentivity, saturationPoint;
        float hysteresisAmount, hysteresisAsymmetry;
        float noiseFloor, modulationNoise, lfEmphasis, hfLoss;
    };
    struct SpeedCharacteristics
    {
        float headBumpMultiplier, hfExtension, noiseReduction, flutterRate, wowRate;
    };

    JilesAthertonHysteresis hysteresisBass, hysteresisMid, hysteresisTreble;
    TapeEQFilter preEmphasisEQ, deEmphasisEQ;
    PhaseSmearingFilter phaseSmear;
    ImprovedNoiseGenerator improvedNoiseGen;

    DBiquad headBumpFilter;                 // juce IIR<double> in source
    DBiquad hfLossFilter1, hfLossFilter2;   // juce IIR<double>
    DBiquad gapLossFilter;                  // juce IIR<double>
    Biquad  biasFilter;                     // juce IIR<float>
    DBiquad dcBlocker;                       // juce IIR<double>
    Biquad  recordHeadFilter1, recordHeadFilter2; // juce IIR<float>

    ChebyshevAntiAliasingFilter antiAliasingFilter;
    SoftLimiter preSaturationLimiter;
    ThreeBandSplitter threeBandSplitter;
    SaturationSplitFilter softClipSplitFilter;

    TransformerSaturation inputTransformer, outputTransformer;
    PlaybackHeadResponse playbackHead;
    MotorFlutter motorFlutter;
    WowFlutterProcessor perChannelWowFlutter;

    float crosstalkBuffer = 0.0f;

    // TapeSaturator: DEAD CODE in the source (process() is never called in the
    // chain). Kept only so updateFilters()/prepare() stay structurally faithful;
    // updateCoefficients() has no audible effect. See PORT_NOTES rule 10.
    struct TapeSaturator
    {
        float envelope = 0.0f, attackCoeff = 0.0f, releaseCoeff = 0.0f;
        void updateCoefficients (float attackMs, float releaseMs, double sampleRate)
        {
            if (sampleRate <= 0.0) sampleRate = 44100.0;
            attackMs = std::max (0.001f, attackMs);
            releaseMs = std::max (0.001f, releaseMs);
            attackCoeff = std::exp (-1.0f / (attackMs * 0.001f * static_cast<float> (sampleRate)));
            releaseCoeff = std::exp (-1.0f / (releaseMs * 0.001f * static_cast<float> (sampleRate)));
        }
    };
    TapeSaturator saturator;

    TapeMachine m_lastMachine = static_cast<TapeMachine> (-1);
    TapeSpeed   m_lastSpeed = static_cast<TapeSpeed> (-1);
    TapeType    m_lastType = static_cast<TapeType> (-1);
    EQStandard  m_lastEqStandard = static_cast<EQStandard> (-1);
    float m_lastBias = -1.0f;

    MachineCharacteristics m_cachedMachineChars{};
    TapeCharacteristics m_cachedTapeChars{};
    SpeedCharacteristics m_cachedSpeedChars{};
    bool m_hasTransformers = false;
    float m_gapWidth = 3.0f;

    static constexpr float denormalPrevention = 1e-8f;

    struct BandDriveRatios { float bass, mid, treble; };
    BandDriveRatios getBandDriveRatios (TapeMachine machine) const
    {
        if (machine == Swiss800) return { 0.55f, 1.0f, 0.20f };
        return { 0.65f, 1.0f, 0.30f };
    }

    float computeDrive (float saturationDepth, float tapeFormulationScale) const
    {
        if (saturationDepth < 0.001f) return 0.0f;
        return 0.62f * std::exp (1.8f * saturationDepth) * tapeFormulationScale;
    }

    JilesAthertonHysteresis::TapeFormulationParams getJAParams (TapeType type) const
    {
        switch (type)
        {
            case Type456: return { 280.0f, 720.0f, 0.016f, 640.0f, 0.50f };
            case TypeGP9: return { 320.0f, 800.0f, 0.012f, 700.0f, 0.60f };
            case Type911: return { 270.0f, 700.0f, 0.018f, 620.0f, 0.48f };
            case Type250: return { 240.0f, 680.0f, 0.020f, 580.0f, 0.45f };
            default:      return { 280.0f, 720.0f, 0.016f, 640.0f, 0.50f };
        }
    }

    static float softClip (float input, float threshold)
    {
        float absInput = std::abs (input);
        if (absInput < threshold) return input;
        float sign = (input >= 0.0f) ? 1.0f : -1.0f;
        float excess = absInput - threshold;
        float headroom = 1.0f - threshold;
        float normalized = excess / (headroom + 0.001f);
        float smoothed = normalized / (1.0f + normalized);
        float clipped = threshold + headroom * smoothed;
        return clipped * sign;
    }

    MachineCharacteristics getMachineCharacteristics (TapeMachine machine)
    {
        MachineCharacteristics chars{};
        switch (machine)
        {
            case Swiss800:
                chars.headBumpFreq = 48.0f; chars.headBumpGain = 3.0f; chars.headBumpQ = 1.0f;
                chars.hfRolloffFreq = 22000.0f; chars.hfRolloffSlope = -12.0f;
                chars.saturationKnee = 0.92f;
                chars.saturationHarmonics[0] = 0.003f; chars.saturationHarmonics[1] = 0.030f;
                chars.saturationHarmonics[2] = 0.001f; chars.saturationHarmonics[3] = 0.005f;
                chars.saturationHarmonics[4] = 0.0005f;
                chars.compressionRatio = 0.03f; chars.compressionAttack = 0.08f; chars.compressionRelease = 40.0f;
                chars.phaseShift = 0.015f; chars.crosstalkAmount = -70.0f;
                break;
            case Classic102:
                chars.headBumpFreq = 62.0f; chars.headBumpGain = 4.5f; chars.headBumpQ = 1.4f;
                chars.hfRolloffFreq = 18000.0f; chars.hfRolloffSlope = -18.0f;
                chars.saturationKnee = 0.85f;
                chars.saturationHarmonics[0] = 0.008f; chars.saturationHarmonics[1] = 0.032f;
                chars.saturationHarmonics[2] = 0.003f; chars.saturationHarmonics[3] = 0.004f;
                chars.saturationHarmonics[4] = 0.002f;
                chars.compressionRatio = 0.05f; chars.compressionAttack = 0.15f; chars.compressionRelease = 80.0f;
                chars.phaseShift = 0.04f; chars.crosstalkAmount = -55.0f;
                break;
        }
        return chars;
    }

    TapeCharacteristics getTapeCharacteristics (TapeType type)
    {
        TapeCharacteristics chars{};
        switch (type)
        {
            case Type456:
                chars.coercivity = 0.78f; chars.retentivity = 0.82f; chars.saturationPoint = 0.88f;
                chars.hysteresisAmount = 0.12f; chars.hysteresisAsymmetry = 0.02f;
                chars.noiseFloor = -60.0f; chars.modulationNoise = 0.025f; chars.lfEmphasis = 1.12f; chars.hfLoss = 0.92f;
                break;
            case TypeGP9:
                chars.coercivity = 0.92f; chars.retentivity = 0.95f; chars.saturationPoint = 0.96f;
                chars.hysteresisAmount = 0.06f; chars.hysteresisAsymmetry = 0.01f;
                chars.noiseFloor = -64.0f; chars.modulationNoise = 0.015f; chars.lfEmphasis = 1.05f; chars.hfLoss = 0.96f;
                break;
            case Type911:
                chars.coercivity = 0.82f; chars.retentivity = 0.86f; chars.saturationPoint = 0.85f;
                chars.hysteresisAmount = 0.14f; chars.hysteresisAsymmetry = 0.025f;
                chars.noiseFloor = -58.0f; chars.modulationNoise = 0.028f; chars.lfEmphasis = 1.15f; chars.hfLoss = 0.90f;
                break;
            case Type250:
                chars.coercivity = 0.70f; chars.retentivity = 0.75f; chars.saturationPoint = 0.80f;
                chars.hysteresisAmount = 0.18f; chars.hysteresisAsymmetry = 0.035f;
                chars.noiseFloor = -55.0f; chars.modulationNoise = 0.035f; chars.lfEmphasis = 1.18f; chars.hfLoss = 0.87f;
                break;
            default:
                chars = getTapeCharacteristics (Type456);
                break;
        }
        return chars;
    }

    SpeedCharacteristics getSpeedCharacteristics (TapeSpeed speed)
    {
        SpeedCharacteristics chars{};
        switch (speed)
        {
            case Speed_7_5_IPS:
                chars.headBumpMultiplier = 1.5f; chars.hfExtension = 0.7f; chars.noiseReduction = 1.0f;
                chars.flutterRate = 3.5f; chars.wowRate = 0.33f;
                break;
            case Speed_15_IPS:
                chars.headBumpMultiplier = 1.0f; chars.hfExtension = 1.0f; chars.noiseReduction = 0.7f;
                chars.flutterRate = 5.0f; chars.wowRate = 0.5f;
                break;
            case Speed_30_IPS:
                chars.headBumpMultiplier = 0.7f; chars.hfExtension = 1.3f; chars.noiseReduction = 0.5f;
                chars.flutterRate = 7.0f; chars.wowRate = 0.8f;
                break;
            default:
                chars = getSpeedCharacteristics (Speed_15_IPS);
                break;
        }
        return chars;
    }

    void updateFilters (TapeMachine machine, TapeSpeed speed, TapeType type,
                        float biasAmount, EQStandard eqStandard)
    {
        auto machineChars = getMachineCharacteristics (machine);
        auto tapeChars = getTapeCharacteristics (type);
        auto speedChars = getSpeedCharacteristics (speed);

        float preEQ_tauNum = 125.0f;
        float preEQ_tauDen = 50.0f;

        switch (eqStandard)
        {
            case NAB:
                switch (speed)
                {
                    case Speed_7_5_IPS: preEQ_tauNum = 225.0f; preEQ_tauDen = 90.0f; break;
                    case Speed_15_IPS:  preEQ_tauNum = 125.0f; preEQ_tauDen = 50.0f; break;
                    case Speed_30_IPS:  preEQ_tauNum = 44.0f;  preEQ_tauDen = 17.5f; break;
                }
                break;
            case CCIR:
                switch (speed)
                {
                    case Speed_7_5_IPS: preEQ_tauNum = 175.0f; preEQ_tauDen = 70.0f; break;
                    case Speed_15_IPS:  preEQ_tauNum = 88.0f;  preEQ_tauDen = 35.0f; break;
                    case Speed_30_IPS:  preEQ_tauNum = 36.0f;  preEQ_tauDen = 17.5f; break;
                }
                break;
            case AES:
                preEQ_tauNum = 35.0f; preEQ_tauDen = 17.5f;
                break;
        }

        preEmphasisEQ.setPreEmphasis (preEQ_tauNum, preEQ_tauDen);
        deEmphasisEQ.setDeEmphasis   (preEQ_tauDen, preEQ_tauNum);

        auto jaParams = getJAParams (type);
        bool isPrecision = (machine == Swiss800);

        hysteresisBass.setFormulation (jaParams);   hysteresisBass.setMachineType (isPrecision);
        hysteresisMid.setFormulation (jaParams);    hysteresisMid.setMachineType (isPrecision);
        hysteresisTreble.setFormulation (jaParams); hysteresisTreble.setMachineType (isPrecision);

        phaseSmear.setMachineCharacter (isPrecision);

        improvedNoiseGen.prepare (currentSampleRate, static_cast<int> (speed));

        float headBumpFreq = machineChars.headBumpFreq;
        float headBumpGain = machineChars.headBumpGain * speedChars.headBumpMultiplier;
        float headBumpQ = machineChars.headBumpQ;

        switch (speed)
        {
            case Speed_7_5_IPS:
                headBumpFreq = machineChars.headBumpFreq * 0.65f;
                headBumpGain *= 1.4f; headBumpQ *= 1.3f;
                break;
            case Speed_15_IPS:
                break;
            case Speed_30_IPS:
                headBumpFreq = machineChars.headBumpFreq * 1.5f;
                headBumpGain *= 0.7f; headBumpQ *= 0.8f;
                break;
        }

        headBumpGain *= tapeChars.lfEmphasis * 0.8f;

        headBumpFreq = std::clamp (headBumpFreq, 30.0f, 120.0f);
        headBumpQ    = std::clamp (headBumpQ, 0.7f, 2.0f);
        headBumpGain = std::clamp (headBumpGain, 1.5f, 5.0f);

        headBumpFilter.setCoeffs (DBiquad::peak (currentSampleRate,
            static_cast<double> (headBumpFreq), static_cast<double> (headBumpGain),
            static_cast<double> (headBumpQ)));

        float maxFilterFreq = static_cast<float> (currentSampleRate * 0.45);
        float hfCutoff = machineChars.hfRolloffFreq * speedChars.hfExtension * tapeChars.hfLoss;
        hfCutoff = std::min (hfCutoff, maxFilterFreq);
        hfLossFilter1.setCoeffs (DBiquad::lowPass (currentSampleRate, static_cast<double> (hfCutoff), 0.707));

        float hfShelfFreq = std::min (hfCutoff * 0.6f, maxFilterFreq);
        hfLossFilter2.setCoeffs (DBiquad::shelf (currentSampleRate, static_cast<double> (hfShelfFreq),
            static_cast<double> (-2.0f * tapeChars.hfLoss), 0.5, true));

        float gapLossFreq = speed == Speed_7_5_IPS ? 8000.0f : (speed == Speed_30_IPS ? 15000.0f : 12000.0f);
        float gapLossAmount = speed == Speed_7_5_IPS ? -3.0f : (speed == Speed_30_IPS ? -0.5f : -1.5f);
        gapLossFilter.setCoeffs (DBiquad::shelf (currentSampleRate, static_cast<double> (gapLossFreq),
            static_cast<double> (gapLossAmount), 0.707, true));

        float biasFreq = 6000.0f + (biasAmount * 4000.0f);
        biasFilter.setCoeffs (Biquad::shelf (currentSampleRate, biasFreq, biasAmount * 3.0f, 0.707f, true));

        saturator.updateCoefficients (machineChars.compressionAttack,
                                      machineChars.compressionRelease, currentSampleRate);
    }
};

//==============================================================================
// TapeMachineDSP — framework-free equivalent of the JUCE PluginProcessor's
// audio path. Public API is fixed by the DPF shell contract. Parameters are
// stored in std::atomic (memory_order_relaxed) and are safe to set from any
// thread; coefficient recompute happens at the top of processBlock().
//==============================================================================
class TapeMachineDSP
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, int maxBlockSize);   // may allocate; setup thread only
    void reset();
    void processBlock (const float* const* inputs, float* const* outputs, int nCh, int nSamples) noexcept;
    int  latencySamples() const noexcept;

    void setTapeMachine (int idx)  noexcept { pMachine.store (clampI (idx, 0, 1), std::memory_order_relaxed); }
    void setTapeSpeed   (int idx)  noexcept { pSpeed.store (clampI (idx, 0, 2), std::memory_order_relaxed); }
    void setTapeType    (int idx)  noexcept { pType.store (clampI (idx, 0, 3), std::memory_order_relaxed); }
    void setSignalPath  (int idx)  noexcept { pSignalPath.store (clampI (idx, 0, 3), std::memory_order_relaxed); }
    void setEqStandard  (int idx)  noexcept { pEqStandard.store (clampI (idx, 0, 2), std::memory_order_relaxed); }
    void setInputGainDb (float db) noexcept { pInputGainDb.store (db, std::memory_order_relaxed); }
    void setSaturation  (float pct)noexcept { pSaturation.store (pct, std::memory_order_relaxed); }  // DEAD (see PORT_NOTES)
    void setBias        (float pct)noexcept { pBias.store (pct, std::memory_order_relaxed); }
    void setCalibration (int idx)  noexcept { pCalibration.store (clampI (idx, 0, 3), std::memory_order_relaxed); }
    void setAutoCal     (bool b)   noexcept { pAutoCal.store (b, std::memory_order_relaxed); }
    void setHighpassHz  (float hz) noexcept { pHighpassHz.store (hz, std::memory_order_relaxed); }
    void setLowpassHz   (float hz) noexcept { pLowpassHz.store (hz, std::memory_order_relaxed); }
    void setNoiseAmount (float pct)noexcept { pNoiseAmount.store (pct, std::memory_order_relaxed); }
    void setNoiseEnabled(bool b)   noexcept { pNoiseEnabled.store (b, std::memory_order_relaxed); } // DEAD gate (see PORT_NOTES)
    void setWow         (float pct)noexcept { pWow.store (pct, std::memory_order_relaxed); }
    void setFlutter     (float pct)noexcept { pFlutter.store (pct, std::memory_order_relaxed); }
    void setOutputGainDb(float db) noexcept { pOutputGainDb.store (db, std::memory_order_relaxed); }
    void setAutoComp    (bool b)   noexcept { pAutoComp.store (b, std::memory_order_relaxed); }
    void setOversampling(int idx)  noexcept { pOversampling.store (clampI (idx, 0, 2), std::memory_order_relaxed); }
    void setBypass      (bool b)   noexcept { pBypass.store (b, std::memory_order_relaxed); }

    float getVuL() const noexcept { return vuL.load (std::memory_order_relaxed); }
    float getVuR() const noexcept { return vuR.load (std::memory_order_relaxed); }

private:
    static int clampI (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static int factorFromChoice (int c) noexcept { return c == 0 ? 1 : (c == 1 ? 2 : 4); }

    void applyFactor (int newFactor);   // reconfigure internal DSP for an OS-factor change

    // --- parameters (atomics) ---
    std::atomic<int>   pMachine{0}, pSpeed{1}, pType{0}, pSignalPath{0}, pEqStandard{0};
    std::atomic<int>   pCalibration{0}, pOversampling{2};
    std::atomic<float> pInputGainDb{0.0f}, pSaturation{4.0f}, pBias{50.0f};
    std::atomic<float> pHighpassHz{20.0f}, pLowpassHz{20000.0f};
    std::atomic<float> pNoiseAmount{0.0f}, pWow{7.0f}, pFlutter{3.0f}, pOutputGainDb{0.0f};
    std::atomic<bool>  pAutoCal{true}, pNoiseEnabled{false}, pAutoComp{true}, pBypass{false};

    // --- config ---
    double baseSampleRate = 44100.0;
    int    maxBlock = 512;
    double maxOsRate = 176400.0;
    int    currentFactor = 4;
    double currentOsRate = 176400.0;

    // --- per-channel DSP ---
    TapeCore   coreL, coreR;
    WowFlutterProcessor sharedWowFlutter;
    Oversampler osL, osR;
    DuskSVF hpL, hpR, lpL, lpR;

    // --- smoothers ---
    SmoothedValue inGain;                   // base-rate ramp (shared by L/R)
    SmoothedValue outGain;                  // OS-rate ramp (shared by L/R)
    SmoothedValue smSat, smWow, smFlutter, smNoise;

    bool bypassLowpass = true;

    // --- dirty tracking ---
    float lastHpFreq = -1.0f, lastLpFreq = -1.0f;
    int   lastFactor = -1;

    // --- scratch (allocated in prepare) ---
    std::vector<float> inGainArr;           // base-rate  [maxBlock]
    std::vector<float> satArr, wowFlutArr, noiseArr, sharedModArr, outGainArr; // OS-rate [maxBlock*4]

    // --- metering ---
    std::atomic<float> vuL{0.0f}, vuR{0.0f};
    float vuStateL = 0.0f, vuStateR = 0.0f;
    float vuReleaseCoeff = 0.0f;
};

} // namespace duskaudio

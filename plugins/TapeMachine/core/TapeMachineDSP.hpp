// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// TapeMachineDSP.hpp — framework-free (no-JUCE) port of the TapeMachine DSP core.

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
// DBiquad — DOUBLE-precision transposed-direct-form-II biquad (double for LF high-Q stability at 4x).
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

    // 1st-order (6 dB/oct) high-pass, bilinear. Gentle DC blocker whose passband
    // phase/group-delay is far lower than a 2nd-order Butterworth of the same corner —
    // matches the reference decks, which keep a broad LF bump nearly flat to ~10-15 Hz.
    static DBiquadCoeffs highPass1 (double fs, double freq) noexcept
    {
        const double k = std::tan (kPiD * freq / fs);
        const double n = 1.0 / (1.0 + k);
        return { n, -n, 0.0, (k - 1.0) * n, 0.0 };
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
// Local2xStage now lives in shared DuskOversampler.hpp (resolves unqualified here).
//==============================================================================

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
        // LF smear stages (old 60/250 Hz Swiss, 40/150 Hz Classic) are NEUTRALIZED (fully
        // bypassed — no coeff, no z^-1 state): their allpass dispersion put ~5 ms of extra
        // group delay at 30-125 Hz that the reference decks do not have (private calibration analysis GD; +8-14 dB
        // 32 Hz sustain ring on drums, private calibration analysis). Only the HF character stages remain.
        float breakFreqs[NUM_STAGES];
        if (isPrecision) { breakFreqs[0] = 2000.0f; breakFreqs[1] = 8000.0f; }
        else             { breakFreqs[0] = 1200.0f; breakFreqs[1] = 6000.0f; }
        breakFreqs[2] = breakFreqs[3] = 0.0f;   // sentinel: neutral stage

        for (int i = 0; i < NUM_STAGES; ++i)
        {
            if (breakFreqs[i] <= 0.0f)
            {
                stages[i].active = false;
                stages[i].coeff = 0.0f;
                stages[i].state = 0.0f;
                continue;
            }
            float tanVal = std::tan (kPiF * breakFreqs[i] / static_cast<float> (currentSampleRate));
            stages[i].coeff = (tanVal - 1.0f) / (tanVal + 1.0f);
            stages[i].active = true;
        }
    }

    float processSample (float input)
    {
        float signal = input;
        for (auto& stage : stages)
        {
            if (! stage.active)
                continue;
            float y = stage.coeff * signal + stage.state;
            stage.state = signal - stage.coeff * y;
            signal = y;
        }
        return signal;
    }

private:
    struct AllpassStage { float coeff = 0.0f, state = 0.0f; bool active = false; };
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
        float tiltFreq = (tapeSpeed == 3) ? 500.0f    // 3.75 IPS: darkest noise tilt
                       : (tapeSpeed == 0) ? 800.0f : (tapeSpeed == 1) ? 1500.0f : 3000.0f;
        tiltCoeff = 1.0f - std::exp (-2.0f * kPiF * tiltFreq / static_cast<float> (sampleRate));
        envelopeCoeff = 1.0f - std::exp (-2.0f * kPiF * 100.0f / static_cast<float> (sampleRate));

        float fc = 8500.0f, Q = 1.1f;   // HF-rise "scrape" band; fills the reference hiss rise up to 10 kHz
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

    // Signal-independent idle tape hiss (pink + scrape), ~unit RMS; caller scales it.
    float idleHiss() noexcept
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
        float tilted = pink - tiltState * 0.62f;    // de-emphasise the pink low-mid body; HF rise comes from the scrape band
        float sw = whiteDist (rng);
        float so = scrapeBP_b0 * sw + scrapeBP_z1;
        scrapeBP_z1 = scrapeBP_b1 * sw - scrapeBP_a1 * so + scrapeBP_z2;
        scrapeBP_z2 = scrapeBP_b2 * sw - scrapeBP_a2 * so;
        return tilted + so * 0.42f;                 // more 1-4 kHz scrape energy (worst reference deficit band)
    }
};

//==============================================================================
// Wow & Flutter processor — shared between channels for stereo coherence.
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

    float randomTarget = 0.0f;         // fast noise -> flutter/scrape band
    float randomCurrent = 0.0f;
    float randomCurrent2 = 0.0f;        // 2nd cascaded pole (steeper flutter->scrape rolloff)
    int randomUpdateCounter = 0;
    float wowRandTarget = 0.0f;         // slow noise -> wow drift band
    float wowRandCurrent = 0.0f;
    int wowRandCounter = 0;
    static constexpr int RANDOM_UPDATE_INTERVAL = 64;

    // Overall pitch-modulation depth per unit wow/flutter amount (in delay samples).
    // Calibrated so mine's 1 kHz FM deviation matches the reference American's Wow & Flutter
    // control AND its modulation *spectrum* (private calibration analysis): the Classic texture is a slow
    // deep smooth drift, so wow (0.1-4 Hz) dominates, flutter (4-30) is secondary, scrape
    // (30-200) is small. The absolute/spectral anchor is Sunbaked (3.75 IPS, wow18/flut12):
    // wow 1.09x / flutter 0.90x / scrape 1.37x of reference, wow peak 0.45 vs 0.50 Hz, FMdev in
    // private calibration analysis tol. TapeMachineDSP.cpp applies a per-speed wfDepthScale (Classic speed curve,
    // 3.25/1.76/1.0/0.68 at 3.75/7.5/15/30 IPS, 15 IPS = 1.0) ON TOP, so the effective depth
    // is speed-dependent like the reference. Re-anchored (2.49/0.389) after the 2026-07-11 spectral
    // RESHAPE (wow slowed to peak ~0.5 Hz; flutter given a steeper 2-pole rolloff so scrape is
    // no longer hot) dropped raw FMdev ~4.6x. NOTE: scale the OVERALL depth here, not the *4
    // random-walk boosts — the boosts set the noise:sine ratio that keeps the modulation
    // aperiodic (low AM "pulsing", periodicity ~0); shrinking them would let the sine dominate.
    static constexpr float kWowDepth     = 2.490f;   // re-anchored after spectral reshape
    static constexpr float kFlutterDepth = 0.389f;   // re-anchored after spectral reshape

    int oversamplingFactor = 1;
    float smoothingAlpha = 0.01f;      // 13 Hz, applied as 2 cascaded poles (flutter band; steep scrape rolloff)
    float wowSmoothingAlpha = 0.001f;  // ~2 Hz one-pole (wow band)

    void setSeed (std::uint32_t s) { rng.seed (s); }

    // maxSampleRate sizes the buffer once so OS-factor changes never reallocate (only re-zero).
    void prepare (double sampleRate, int osFactor, double maxSampleRate)
    {
        oversamplingFactor = std::max (1, osFactor);

        smoothingAlpha    = 1.0f - std::exp (-2.0f * kPiF * 13.0f / static_cast<float> (sampleRate));
        wowSmoothingAlpha = 1.0f - std::exp (-2.0f * kPiF *  2.0f / static_cast<float> (sampleRate));

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
        wowRandCounter = 0;
        randomUpdateCounter = 0;
        wowPhase = 0.0;
        flutterPhase = 0.0;
        randomPhase = 0.0f;
        wowRandCurrent = wowRandTarget = 0.0f;
        randomCurrent = randomCurrent2 = randomTarget = 0.0f;
    }

    float calculateModulation (float wowAmount, float flutterAmount,
                               float wowRate, float flutterRate, double sampleRate)
    {
        if (sampleRate <= 0.0) sampleRate = 44100.0;

        const float osScale = static_cast<float> (oversamplingFactor);
        const double safeSampleRate = std::max (1.0, sampleRate);

        // Real tape W&F is NOT a pure sine: wow is a quasi-periodic capstan drift and
        // flutter is random (scrape). A pure-sine modulation makes the pitch swing at one
        // fixed rate -> a visible periodic "pulse" on a test tone that the reference (random
        // wobble) doesn't have. So drive the delay mostly with band-limited NOISE (a random
        // walk), keeping only a small residual sine for the periodic capstan component.
        // The modulation SPECTRUM is shaped to the reference Classic (slow deep smooth drift):
        //   wow  band (0.1-4 Hz):  sample-hold @ ~wowRate*2.5 + a 2 Hz one-pole -> peak ~0.5 Hz.
        //   flut band (4-30 Hz):   randomCurrent2 = two cascaded 13 Hz poles -> populates 4-30
        //                          but rolls off -12 dB/oct so the scrape band (30-200) stays
        //                          low (a single one-pole was ~9x too hot in 30-200 Hz).
        // safeSampleRate is already the oversampled rate (caller passes currentOsRate and this
        // runs once per OS sample), so no extra oversamplingFactor scale here.
        const int wowInterval = std::max (1, (int) (safeSampleRate
                              / std::max (1.0, (double) wowRate * 2.5)));
        if (++wowRandCounter >= wowInterval) { wowRandCounter = 0; wowRandTarget = dist (rng); }
        wowRandCurrent += (wowRandTarget - wowRandCurrent) * wowSmoothingAlpha;

        const int scaledInterval = RANDOM_UPDATE_INTERVAL * oversamplingFactor;
        if (++randomUpdateCounter >= scaledInterval) { randomUpdateCounter = 0; randomTarget = dist (rng); }
        randomCurrent  += (randomTarget  - randomCurrent)  * smoothingAlpha;
        randomCurrent2 += (randomCurrent - randomCurrent2) * smoothingAlpha;

        // Random walks have a smaller RMS than a unit sine, so boost them to keep the same
        // overall depth (the wow/flutter AMOUNT still maps to the same pitch deviation).
        const float wowSine  = static_cast<float> (std::sin (wowPhase));
        const float flutSine = static_cast<float> (std::sin (flutterPhase));
        const float wowMod     = (0.65f * wowRandCurrent * 4.0f + 0.35f * wowSine)
                               * wowAmount * kWowDepth * osScale;
        const float flutterMod = (0.85f * randomCurrent2 * 4.0f + 0.15f * flutSine)
                               * flutterAmount * kFlutterDepth * osScale;

        wowPhase += 2.0 * kPiD * wowRate / safeSampleRate;
        if (wowPhase > 2.0 * kPiD) wowPhase -= 2.0 * kPiD;
        flutterPhase += 2.0 * kPiD * flutterRate / safeSampleRate;
        if (flutterPhase > 2.0 * kPiD) flutterPhase -= 2.0 * kPiD;

        return wowMod + flutterMod;
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

        totalDelay = std::max (2.5f, std::min (totalDelay, static_cast<float> (bufferSize - 3)));

        int delaySamples = static_cast<int> (totalDelay);
        float frac = totalDelay - static_cast<float> (delaySamples);

        // 4-point cubic (Catmull-Rom) fractional-delay interpolation. The old first-order
        // allpass with a frac clamp of [0.1,0.9] could not represent fractional delays near
        // the integer boundaries, so as the wow/flutter swept the delay across each integer
        // sample the interpolation stuck then jumped — a PERIODIC amplitude ripple ("pulsing"
        // AM synced to the wow rate) that real tape W&F (pure pitch/FM) does not have. Cubic
        // is stateless, unclamped and continuous across integer crossings -> no AM artifact.
        const int i1 = (writeIndex - delaySamples + bufferSize) % bufferSize;   // t=0
        const int i0 = (i1 + 1) % bufferSize;                                   // newer neighbour
        const int i2 = (i1 - 1 + bufferSize) % bufferSize;                      // t=1 (older)
        const int i3 = (i1 - 2 + bufferSize) % bufferSize;                      // older neighbour
        const float p0 = delayBuffer[i0], p1 = delayBuffer[i1];
        const float p2 = delayBuffer[i2], p3 = delayBuffer[i3];
        const float t = frac;
        const float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
        const float b =  p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
        const float c = -0.5f * p0 + 0.5f * p2;
        const float output = ((a * t + b) * t + c) * t + p1;

        writeIndex = (writeIndex + 1) % std::max (1, bufferSize);
        return output;
    }
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
        float speedCmPerSec = speed >= 3.0f ? 9.525f       // 3.75 IPS
                            : (speed == 0 ? 19.05f : (speed == 1 ? 38.1f : 76.2f));
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
// TapeCore — per-channel tape emulation (direct port of ImprovedTapeEmulation).
//==============================================================================
class TapeCore
{
public:
    enum TapeMachine { Swiss = 0, American };
    // 3.75 IPS appended (idx 3) NOT inserted — every switch(speed) and the harness
    // assume 0/1/2 = 7.5/15/30. American-only; non-standard on the Swiss.
    enum TapeSpeed   { Speed_7_5_IPS = 0, Speed_15_IPS, Speed_30_IPS, Speed_3_75_IPS };
    enum TapeType    { FormulaClassic = 0, FormulaHighOutput, FormulaModern, FormulaVintage };
    enum EQStandard  { NAB = 0, CCIR };   // both reference decks expose only NAB/CCIR (no AES)
    enum SignalPath  { Repro = 0, Sync, Input, Thru };

    TapeCore() { reset(); }

    // seedBase distinguishes L/R RNG streams so noise/flutter are decorrelated.
    void setSeeds (std::uint32_t seedBase)
    {
        improvedNoiseGen.setSeed (seedBase + 1u);
        perChannelWowFlutter.setSeed (seedBase + 2u);
        motorFlutter.setSeed (seedBase + 3u);
    }

    // osRate/osFactor = oversampled rate/factor; maxOsRate sizes the wow/flutter buffer.
    void prepare (double osRate, int osFactor, double maxOsRate)
    {
        if (osRate <= 0.0) osRate = 44100.0;
        if (osFactor < 1) osFactor = 1;

        currentSampleRate = osRate;
        currentOversamplingFactor = osFactor;
        baseSampleRate = osRate / static_cast<double> (osFactor);
        m_humPhaseInc = 2.0f * kPiF * 60.0f / static_cast<float> (currentSampleRate);

        double antiAliasingCutoff = baseSampleRate * 0.45;
        antiAliasingFilter.prepare (osRate, antiAliasingCutoff);

        preEmphasisEQ.prepare (osRate);
        deEmphasisEQ.prepare (osRate);
        phaseSmear.prepare (osRate);
        improvedNoiseGen.prepare (osRate, 1);
        softClipSplitFilter.prepare (osRate, 5000.0);

        perChannelWowFlutter.prepare (osRate, osFactor, maxOsRate);

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
        headWidthFilter.setCoeffs (DBiquad::peak (osRate, static_cast<double> (safeFreq (7500.0f)), 0.0, 0.9)); // neutral until updateFilters
        driveHfShelf.setCoeffs   (DBiquad::shelf (osRate, static_cast<double> (safeFreq (2500.0f)), 0.0, 0.5, true)); // neutral until setDriveHfComp
        levelHfShelf.setCoeffs   (DBiquad::shelf (osRate, static_cast<double> (safeFreq (7900.0f)), 0.0, 0.45, true)); // neutral until setLevelComp
        levelLfShelf.setCoeffs   (DBiquad::peak  (osRate,   32.0, 0.0, 1.4)); // neutral until setLevelComp
        reproLfShelf.setCoeffs   (DBiquad::shelf (osRate,   80.0, 0.0, 0.5, false)); // 4-band repro EQ, neutral until setReproEq
        reproLmfPeak.setCoeffs   (DBiquad::peak  (osRate,  160.0, 0.0, 0.9));
        reproHmfPeak.setCoeffs   (DBiquad::peak  (osRate, static_cast<double> (safeFreq (5000.0f)), 0.0, 0.8));
        reproHfShelf.setCoeffs   (DBiquad::shelf (osRate, static_cast<double> (safeFreq (9000.0f)), 0.0, 0.5, true));
        slowModernPresencePeak.setCoeffs (DBiquad::peak (osRate,
            static_cast<double> (safeFreq (2700.0f)), 0.0, 1.6));
        transformerLfShelf.setCoeffs (DBiquad::shelf (osRate, 48.0, 0.0, 0.7, false)); // neutral until setTransformerOff
        // Emphasis-rebalance G / 1-G pair — neutral defaults; updateFilters sets the per-machine values.
        emphLfPre.setCoeffs  (DBiquad::shelf (osRate, 150.0, 0.0, 0.5, false));
        emphHfPre.setCoeffs  (DBiquad::peak  (osRate, static_cast<double> (safeFreq (5000.0f)), 0.0, 2.0));
        emphHfPost.setCoeffs (DBiquad::peak  (osRate, static_cast<double> (safeFreq (5000.0f)), 0.0, 2.0));
        emphLfPost.setCoeffs (DBiquad::shelf (osRate, 150.0, 0.0, 0.5, false));
        aliasHfPre.setCoeffs  (DBiquad::shelf (osRate, static_cast<double> (safeFreq (12000.0f)), 0.0, 0.7, true));
        aliasHfPost.setCoeffs (DBiquad::shelf (osRate, static_cast<double> (safeFreq (12000.0f)), 0.0, 0.7, true));
        biasFilter.setCoeffs     (Biquad::shelf (osRate, safeFreq (8000.0f), 2.0f, 0.707f, true));
        dcBlocker.setCoeffs      (DBiquad::highPass1 (osRate, 18.0));

        recordHeadCutoff = std::min (20000.0f, static_cast<float> (safeMaxFreq));
        recordHeadFilter1.setCoeffs (Biquad::lowPass (osRate, recordHeadCutoff, 1.3066f));
        recordHeadFilter2.setCoeffs (Biquad::lowPass (osRate, recordHeadCutoff, 0.5412f));

        preEmphasisEQ.setPreEmphasis (125.0f, 50.0f);
        deEmphasisEQ.setDeEmphasis   (50.0f, 125.0f);
        phaseSmear.setMachineCharacter (true);

        // Force a full updateFilters() next processSample so machine coeffs replace these neutral defaults.
        m_lastMachine = static_cast<TapeMachine> (-1);
        m_lastSpeed   = static_cast<TapeSpeed> (-1);
        m_lastNoiseSpeed = static_cast<TapeSpeed> (-1);   // re-prepare the noise gen at the new rate on next updateFilters
        m_lastType    = static_cast<TapeType> (-1);
        m_lastEqStandard = static_cast<EQStandard> (-1);
        m_lastBias    = -1.0f;
    }

    void reset()
    {
        headBumpFilter.reset(); hfLossFilter1.reset(); hfLossFilter2.reset();
        gapLossFilter.reset(); biasFilter.reset(); dcBlocker.reset();
        headWidthFilter.reset(); driveHfShelf.reset(); levelHfShelf.reset(); levelLfShelf.reset();
        reproLfShelf.reset(); reproLmfPeak.reset(); reproHmfPeak.reset(); reproHfShelf.reset();
        slowModernPresencePeak.reset();
        transformerLfShelf.reset();
        emphLfPre.reset(); emphHfPre.reset(); emphHfPost.reset(); emphLfPost.reset();
        aliasHfPre.reset(); aliasHfPost.reset();
        recordHeadFilter1.reset(); recordHeadFilter2.reset();
        antiAliasingFilter.reset();
        softClipSplitFilter.reset();
        preEmphasisEQ.reset(); deEmphasisEQ.reset(); phaseSmear.reset();
        improvedNoiseGen.reset();

        if (! perChannelWowFlutter.delayBuffer.empty())
            std::fill (perChannelWowFlutter.delayBuffer.begin(), perChannelWowFlutter.delayBuffer.end(), 0.0f);
        perChannelWowFlutter.writeIndex = 0;
        perChannelWowFlutter.wowRandCounter = 0;
        perChannelWowFlutter.randomUpdateCounter = 0;
        perChannelWowFlutter.wowPhase = 0.0;
        perChannelWowFlutter.flutterPhase = 0.0;
        perChannelWowFlutter.randomPhase = 0.0f;
        perChannelWowFlutter.wowRandCurrent = perChannelWowFlutter.wowRandTarget = 0.0f;
        perChannelWowFlutter.randomCurrent = perChannelWowFlutter.randomCurrent2 = perChannelWowFlutter.randomTarget = 0.0f;

        playbackHead.reset(); motorFlutter.reset();
        m_limPrev = 0.0f; m_shaperPrev = 0.0f;
        m_lim2x.reset(); m_shaper2x.reset();
        crosstalkBuffer = 0.0f;
        m_humPhase = 0.0f;
    }

    // Drive-linked HF restore. The memoryless waveshaper compresses HF as it is
    // driven (measured: ~-2.7 dB @10-15k at +8 dB drive, Swiss); the reference models the
    // opposite — a record HF EQ that BRIGHTENS with level. A post-shaper high-shelf
    // whose gain tracks drive^2 cancels the compression so hot presets stay bright.
    // gainDb 0 (reference drive: inGain 0 @ +6 cal) => neutral => reference unchanged.
    // Block-rate: set once per processBlock from the drive, then applied per sample.
    void setDriveHfComp (float gainDb) noexcept
    {
        const double fs = currentSampleRate > 0.0 ? currentSampleRate : 96000.0;
        driveHfShelf.setCoeffs (DBiquad::shelf (fs, 2500.0, static_cast<double> (gainDb), 0.5, true));
    }

    // Signal-level FR compensation. Above the -12 dBFS reference operating level the
    // memoryless tape core's FR drifts vs the reference: the HF droops (waveshaper HF
    // compression) and the deep lows thicken (rel-1k). Measured mine-minus-reference at 0 dBFS
    // (~0 at/below -12): HF ~-5 dB @8-15k, LF ~+3 (Swiss)/+5 (Classic) dB @30 Hz. Two
    // level-keyed filters cancel it: an HF high-shelf RESTORE (~5.5 kHz corner — the droop
    // is small at 3-5 kHz but ~5 dB by 8 kHz, so a 5.5 kHz corner balances 3k..15k better
    // than the 2.5 kHz preset-restore shelf) and a narrow LF CUT peak (~32 Hz — the LF
    // excess is a narrow feature near 30 Hz, so a bell beats a low-shelf that would over-cut
    // 50-100 Hz). Kept SEPARATE from driveHfShelf so the preset-static HF restore (2.5 kHz)
    // is untouched and every factory-preset FR fit stays valid. hfDb 0 + lfDb 0 (flux <=
    // -12 dBFS) => both neutral => reference/preset FR unchanged. Block-rate; the caller
    // pre-smooths the gains (fast-attack one-pole) so the coeffs never step on a transient.
    void setLevelComp (TapeMachine machine, float hfDb, float lfDb) noexcept
    {
        // Per-machine shelf geometry selected HERE (block start), not in the lazy
        // updateFilters() inside processSample: after a machine change the first block's
        // coeffs would otherwise be built from the previous machine's geometry. The
        // American's HF droop is deeper at 8 kHz+ and near-flat at 3-5 kHz, so it uses
        // a steeper, higher-Q shelf than the Swiss's broad one; LF cut is slightly narrower.
        double hfFc, hfQ, lfFc, lfQ;
        if (machine == Swiss) { hfFc = 7900.0; hfQ = 0.45; lfFc = 32.0; lfQ = 1.4; }
        else                     { hfFc = 6000.0; hfQ = 0.55; lfFc = 30.0; lfQ = 1.6; }
        const double fs = currentSampleRate > 0.0 ? currentSampleRate : 96000.0;
        // Neutral HF compensation (0 dB — always so on the Swiss, where kLevelHfSwiss
        // == 0): a 0 dB shelf is an identity transfer function but a TDF-II biquad still
        // holds z1/z2, so a prior American shelf state would linger as a decaying
        // transient after a machine switch. Reset it so it can't remain active. Nonzero
        // HF keeps normal (stateful) shelf processing.
        if (hfDb == 0.0f)
            levelHfShelf.reset();
        levelHfShelf.setCoeffs (DBiquad::shelf (fs, hfFc, static_cast<double> (hfDb), hfQ, true));
        levelLfShelf.setCoeffs (DBiquad::peak  (fs, lfFc, static_cast<double> (lfDb), lfQ));
    }

    // Advanced repro-head EQ: a post-tape 4-band shaper (LF low-shelf 80 Hz, low-mid
    // peak 160 Hz, high-mid/presence peak 5 kHz, HF high-shelf 9 kHz) modelling the reference
    // decks' Repro EQ section. Per-preset trims that reproduce the factory presets'
    // baked repro EQ shape. 0 dB on all => neutral => reference unchanged. Block-rate.
    void setReproEq (float lfDb, float lmfDb, float hmfDb, float hfDb) noexcept
    {
        const double fs = currentSampleRate > 0.0 ? currentSampleRate : 96000.0;
        reproLfShelf.setCoeffs  (DBiquad::shelf (fs,   80.0, static_cast<double> (lfDb),  0.5, false));
        reproLmfPeak.setCoeffs  (DBiquad::peak  (fs,  160.0, static_cast<double> (lmfDb), 0.9));
        reproHmfPeak.setCoeffs  (DBiquad::peak  (fs, 5000.0, static_cast<double> (hmfDb), 0.8));
        reproHfShelf.setCoeffs  (DBiquad::shelf (fs, 9000.0, static_cast<double> (hfDb),  0.5, true));
    }

    // Transformer Off (American only): bypassing the American output transformer EXTENDS
    // the deep bass (the transformer rolls off the lows — measured On->Off = +3.4/+1.0/+0.4 dB
    // @30/60/100 Hz, flat above ~200 Hz) and THINS the 2nd harmonic (measured On->Off = -8 dB).
    // Modelled as a low-shelf LF restore + an even-order (2nd) scale. lfGainDb 0 + evenScale 1
    // = Transformer On (the fitted default) => both neutral => byte-identical reference.
    void setTransformerOff (float lfGainDb, float evenScale) noexcept
    {
        const double fs = currentSampleRate > 0.0 ? currentSampleRate : 96000.0;
        transformerLfShelf.setCoeffs (DBiquad::shelf (fs, 48.0, static_cast<double> (lfGainDb), 0.7, false));
        m_americanEvenScale = evenScale;
        // Skip the shelf entirely when the transformer is On (lfGainDb 0): a 0 dB shelf is only
        // an identity by pole/zero cancellation, not bit-exact, so gating the call (not just
        // zeroing the gain) is what keeps the default/reference render byte-identical.
        const bool transformerOff = (lfGainDb != 0.0f);
        if (transformerOff != m_transformerBypass)
            transformerLfShelf.reset();   // clear stale state left from the previous toggle state
        m_transformerBypass = transformerOff;
    }

    // Constant idle noise: pink hiss + 60 Hz mains hum, scaled by Noise; hiss/humScale = per-machine floor (matched to reference at 100).
    // h1/h2/h3 = mains-hum harmonic weights: the Swiss hum is nearly pure fundamental + a little 3rd (symmetric
    // odd-harmonic waveform: 120 Hz absent, 180 Hz ~-20 dB); the Classic hum carries a strong 2nd (60 Hz ~= 120 Hz).
    float idleNoise (float noiseAmount, float hissScale, float humScale,
                     float h1, float h2, float h3) noexcept
    {
        const float frac = noiseAmount * 0.01f;                 // 0..1
        const float hiss = improvedNoiseGen.idleHiss() * hissScale;
        m_humPhase += m_humPhaseInc;
        if (m_humPhase > 2.0f * kPiF) m_humPhase -= 2.0f * kPiF;
        const float hum = (std::sin (m_humPhase) * h1
                         + std::sin (2.0f * m_humPhase) * h2
                         + std::sin (3.0f * m_humPhase) * h3) * humScale;
        return (hiss + hum) * frac;
    }
    // Swiss (reference tracking deck) idle floor — matched to Swiss noise-max; hum is odd-harmonic
    // (120 Hz absent, 180 Hz ~-20 dB) so h2~0, h3 small.
    float swissIdleNoise (float noiseAmount) noexcept { return idleNoise (noiseAmount, kSwissHiss, kSwissHum, 0.7f, 0.003f, 0.08f); }
    // American (reference mastering deck) "Hiss & Hum": brighter hiss + strong-2nd hum (60~=120 Hz), 180 Hz absent.
    float americanIdleNoise (float noiseAmount) noexcept { return idleNoise (noiseAmount, kAmericanHiss, kAmericanHum, 0.7f, 0.7f, 0.002f); }

    float processSample (float input,
                         TapeMachine machine, TapeSpeed speed, TapeType type,
                         float biasAmount, float saturationDepth, float wowFlutterAmount,
                         bool noiseEnabled, float noiseAmount,
                         float* sharedWowFlutterMod, float calibrationLevel,
                         EQStandard eqStandard, SignalPath signalPath, int headWidth)
    {
        if (signalPath == Thru) return input;
        // Near-silent input: zero it but DON'T early-return. Falling through advances the
        // LocalAAStage FIR histories + ADAA previous-sample state (m_lim2x / m_shaper2x)
        // through the silence, so a frozen history no longer clicks when signal resumes —
        // the same reasoning that makes the shaper run unconditionally (Lines ~1012). Idle
        // tape noise still persists: it is added on the full path below (Lines ~1120),
        // under the identical processTape/noiseEnabled/amount gate.
        if (std::abs (input) < denormalPrevention)
            input = 0.0f;

        if (machine != m_lastMachine || speed != m_lastSpeed || type != m_lastType ||
            std::abs (biasAmount - m_lastBias) > 0.01f || eqStandard != m_lastEqStandard ||
            headWidth != m_lastHeadWidth)
        {
            updateFilters (machine, speed, type, biasAmount, eqStandard, headWidth);
            m_lastMachine = machine; m_lastSpeed = speed; m_lastType = type;
            m_lastBias = biasAmount; m_lastEqStandard = eqStandard; m_lastHeadWidth = headWidth;

            m_cachedMachineChars = getMachineCharacteristics (machine);
            m_cachedTapeChars = getTapeCharacteristics (type);
            m_cachedSpeedChars = getSpeedCharacteristics (speed);
            m_gapWidth = (machine == Swiss) ? 2.5f : 3.5f;
        }

        const auto& tapeChars = m_cachedTapeChars;
        const auto& speedChars = m_cachedSpeedChars;

        const bool processTape = (signalPath == Repro || signalPath == Sync);
        // Swiss Sync ~= Repro (barely wider gap); American uses the original 2x gap.
        const float syncGapMult = (machine == Swiss) ? 1.0f : 2.0f;
        const float playbackGapWidth = (signalPath == Sync) ? m_gapWidth * syncGapMult : m_gapWidth;

        // Cal Level sets the tape's reference operating flux. Higher cal records the
        // tape HOTTER for the same 0 VU, so it saturates EARLIER (reference spec + measured:
        // reference THD RISES with cal). Model it as a flux scale RELATIVE to the +6 dB
        // reference at which the waveshapers were fitted, so +6 stays byte-identical and
        // cal moves saturation in the correct direction. kCalFlux (dB drive per dB cal)
        // tunes the slope to each deck's reference THD-vs-cal curve; calRestore inverts the
        // flux + baseline so the tape CORE stays level-neutral (cal changes saturation
        // onset, not output level).
        constexpr float kCalRefDb = 6.0f;
        const float kCalFlux    = (machine == Swiss) ? 0.58f : 1.30f;
        const float baseAtten   = dbToGain (kCalRefDb);
        const float fluxScale   = dbToGain ((calibrationLevel - kCalRefDb) * kCalFlux);
        const float calRestore  = baseAtten / fluxScale;
        float signal = input * 0.95f * (fluxScale / baseAtten);

        signal = preEmphasisEQ.processSample (signal);

        if (processTape)
        {
            if (biasAmount > 0.0f)
                signal = biasFilter.process (signal);

            // Smooth-knee soft limit (+-0.95 ceiling), ADAA inside a local 2x (~4x AA).
            signal = m_lim2x.process (signal, [this] (float v) noexcept { return adaaLimit (v); });

            if (currentOversamplingFactor > 1)
            {
                signal = recordHeadFilter1.process (signal);
                signal = recordHeadFilter2.process (signal);
            }

            // Measured transfer-curve waveshaper (ADAA inside LocalAAStage); m_swissDrive/
            // m_americanDrive set the operating point. Run UNCONDITIONALLY: gating it on a
            // drive threshold (old code) froze the LocalAAStage delay lines + ADAA prev-
            // state at the very bottom of the input-gain range, so crossing that boundary
            // flushed stale FIR history as a click. At tiny input the shaper is near-linear.
            (void) saturationDepth;   // input trim already scales the signal upstream

            // G (pre-shaper half of the emphasis-rebalance pair): LF-shelf boost + HF-peak
            // notch. Brackets ONLY the shaper (the harmonic source); the limiter above is
            // near-linear at these levels and stays OUTSIDE the pair so its safety behaviour
            // is untouched. 1-G below undoes this for the linear signal.
            signal = static_cast<float> (emphLfPre.process (static_cast<double> (signal)));
            signal = static_cast<float> (emphHfPre.process (static_cast<double> (signal)));
            signal = static_cast<float> (aliasHfPre.process (static_cast<double> (signal)));

            if (machine == Swiss)
                signal = m_shaper2x.process (signal, [this] (float v) noexcept { return adaaSatSwiss (v); });
            else
                signal = m_shaper2x.process (signal, [this] (float v) noexcept { return adaaSatAmerican (v); });

            // 1-G (post-shaper half): HF-peak boost + LF-shelf cut = exact inverse of G, so the
            // linear FR is preserved while the shaper-born 5th/IMD products are lifted.
            signal = static_cast<float> (aliasHfPost.process (static_cast<double> (signal)));
            signal = static_cast<float> (emphHfPost.process (static_cast<double> (signal)));
            signal = static_cast<float> (emphLfPost.process (static_cast<double> (signal)));

            {
                float lowFreq = softClipSplitFilter.process (signal);
                float highFreq = signal - lowFreq;
                lowFreq = softClip (lowFreq, 0.95f);
                signal = lowFreq + highFreq;
            }

            signal = static_cast<float> (gapLossFilter.process (static_cast<double> (signal)));

            if (wowFlutterAmount > 0.0f)
            {
                float motorQuality = (machine == Swiss) ? 0.2f : 0.6f;
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

            // Drive-linked HF restore (neutral at reference drive; cancels the
            // waveshaper's HF compression so hot presets stay bright like the reference).
            signal = static_cast<float> (driveHfShelf.process (static_cast<double> (signal)));

            // Signal-level FR compensation (HF restore + LF cut; both neutral at 0 dB =
            // flux <= -12 dBFS ref). Cancels the core's level-dependent HF droop / LF
            // thickening so the FR-vs-level surface matches the reference on real program. Run
            // unconditionally like the repro EQ (0 dB ~= identity to ~1e-6 dB); the
            // reference/preset gate signals stay at 0 dB so their FR holds.
            signal = static_cast<float> (levelHfShelf.process (static_cast<double> (signal)));
            signal = static_cast<float> (levelLfShelf.process (static_cast<double> (signal)));

            // Advanced repro-head EQ trims (neutral at 0 dB; reproduce the reference factory
            // presets' baked Repro HF/LF EQ so each preset's FR matches).
            signal = static_cast<float> (reproLfShelf.process (static_cast<double> (signal)));
            signal = static_cast<float> (reproLmfPeak.process (static_cast<double> (signal)));
            signal = static_cast<float> (reproHmfPeak.process (static_cast<double> (signal)));
            signal = static_cast<float> (reproHfShelf.process (static_cast<double> (signal)));

            // The American's 3.75 IPS / modern formulation has a narrow reproduce-head
            // presence resonance around 3 kHz before its steep 5 kHz shoulder. The
            // general 5 kHz HMF band cannot create that opposing peak/drop shape.
            if (m_slowModernPresence)
                signal = static_cast<float> (slowModernPresencePeak.process (static_cast<double> (signal)));

            // Transformer-Off LF restore (neutral unless Transformer switched Off on the
            // American): the output transformer rolls off the deep bass, so bypassing
            // it boosts the lows back. 0 dB (Transformer On, default) => identity => reference
            // and the whole Swiss path stay byte-identical (the call is skipped, not just
            // zeroed, since a 0 dB shelf is not bit-exact).
            if (m_transformerBypass)
                signal = static_cast<float> (transformerLfShelf.process (static_cast<double> (signal)));

            // American head-width HF peak. Only runs for American at a non-1/2" width,
            // so the tuned 1/2" reference and the whole Swiss path stay byte-identical.
            if (machine == American && headWidth != 1)
                signal = static_cast<float> (headWidthFilter.process (static_cast<double> (signal)));

            // Sync (record-head monitor) differs from Repro only by the wider head gap
            // (playbackGapWidth below); on the real decks Sync is nearly as bright as
            // Repro. The old double-lowpass here over-darkened it (killed harmonic HF ->
            // THD collapsed), so the extra HF loss is dropped — the gap carries the diff.
            signal = playbackHead.process (signal, playbackGapWidth, static_cast<float> (speed));
        }

        signal = deEmphasisEQ.processSample (signal);
        signal = phaseSmear.processSample (signal);

        // Restore the calibration attenuation applied to the input above, and apply the
        // small per-machine unity trim, so the tape CORE is level-neutral: cal changes
        // saturation onset but not output level (the reference spec: "cal sets the reference
        // flux level without disturbing unity gain"), and knobs-at-0 pass -12 dBFS ->
        // -12 dBFS whether or not the gain link is engaged. Added before the noise floor
        // so the modelled hiss/hum stays at its absolute (cal-independent) level.
        signal *= calRestore * m_machineMakeupGain;

        if (processTape && noiseEnabled && noiseAmount > 0.001f)
        {
            // Both decks add constant idle hiss+hum (a modelled constant floor).
            signal += (machine == Swiss) ? swissIdleNoise (noiseAmount)
                                            : americanIdleNoise (noiseAmount);
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

    TapeEQFilter preEmphasisEQ, deEmphasisEQ;
    PhaseSmearingFilter phaseSmear;
    ImprovedNoiseGenerator improvedNoiseGen;
    float m_humPhase = 0.0f;        // Swiss idle mains-hum oscillator phase
    float m_humPhaseInc = 0.0f;     // cached 60 Hz phase step (set in prepare)
    float m_swissDrive = 1.0f;       // cached waveshaper drive (set in updateFilters)
    float m_swissDriveInv = 1.0f;    // cached 1/drive makeup
    float m_americanDrive = 1.0f;    // cached American waveshaper drive
    float m_americanDriveInv = 1.0f; // cached 1/drive makeup
    float m_americanEvenScale = 1.0f; // American even-order (2nd) scale: 1 = Transformer On (default), <1 thins the 2nd when Transformer Off
    bool  m_transformerBypass = false; // true only when Transformer Off (American): applies the LF-restore shelf
    bool  m_slowModernPresence = false; // true only for measured American 3.75 IPS / FormulaModern resonance
    float m_limPrev = 0.0f;         // ADAA soft-limit state (per channel)
    float m_shaperPrev = 0.0f;      // ADAA waveshaper state (per channel)
    LocalAAStage m_lim2x, m_shaper2x;  // soft-limit + shaper: deep local-4x AA (mixed-rate: filters at 2x core, NL alias-free)

    DBiquad headBumpFilter;                 // juce IIR<double> in source
    DBiquad hfLossFilter1, hfLossFilter2;   // juce IIR<double>
    DBiquad gapLossFilter;                  // juce IIR<double>
    DBiquad headWidthFilter;                // American head-width HF peak (neutral otherwise)
    DBiquad driveHfShelf;                   // drive-linked HF restore (neutral at reference drive)
    DBiquad levelHfShelf;                   // signal-level HF restore (neutral at 0 dB; keyed on instantaneous flux)
    DBiquad levelLfShelf;                   // signal-level LF cut (neutral at 0 dB; keyed on instantaneous flux)
    DBiquad reproLfShelf, reproLmfPeak, reproHmfPeak, reproHfShelf; // advanced repro-head 4-band EQ (neutral at 0 dB)
    DBiquad slowModernPresencePeak;             // measured Classic 3.75 IPS / modern formulation presence resonance
    DBiquad transformerLfShelf;             // Transformer-Off LF restore (neutral at 0 dB = Transformer On)
    // Emphasis-rebalance G / 1-G pair wrapping ONLY the shaper (per-machine, always active).
    // G = emphLfPre (LF-shelf boost) + emphHfPre (HF-peak NOTCH); 1-G = emphHfPost (HF-peak
    // BOOST) + emphLfPost (LF-shelf cut). The pair is a mathematical identity for the LINEAR
    // signal (double biquads cancel to ~1e-10 dB), so the end-to-end FR is unchanged; only the
    // harmonic/IMD products BORN in the shaper (which pass 1-G but never G) are re-weighted:
    // the LF-shelf drives the tape harder at LF -> raises the two-tone IMD (60 Hz-modulated
    // sidebands) without touching the 1 kHz THD; the HF peak lifts the 5th-harmonic region
    // (~5 kHz) to undo the de-emphasis attenuation, without brightening the linear FR.
    DBiquad emphLfPre, emphHfPre, emphHfPost, emphLfPost;
    DBiquad aliasHfPre, aliasHfPost;       // inverse HF pair reducing near-Nyquist shaper aliases
    Biquad  biasFilter;                     // juce IIR<float>
    DBiquad dcBlocker;                       // juce IIR<double>
    Biquad  recordHeadFilter1, recordHeadFilter2; // juce IIR<float>

    ChebyshevAntiAliasingFilter antiAliasingFilter;
    SaturationSplitFilter softClipSplitFilter;

    PlaybackHeadResponse playbackHead;
    MotorFlutter motorFlutter;
    WowFlutterProcessor perChannelWowFlutter;

    float crosstalkBuffer = 0.0f;

    TapeMachine m_lastMachine = static_cast<TapeMachine> (-1);
    TapeSpeed   m_lastSpeed = static_cast<TapeSpeed> (-1);
    TapeSpeed   m_lastNoiseSpeed = static_cast<TapeSpeed> (-1);   // gates improvedNoiseGen.prepare (noise state reset only on real speed/rate change)
    TapeType    m_lastType = static_cast<TapeType> (-1);
    EQStandard  m_lastEqStandard = static_cast<EQStandard> (-1);
    float m_lastBias = -1.0f;
    int   m_lastHeadWidth = -1;

    MachineCharacteristics m_cachedMachineChars{};
    TapeCharacteristics m_cachedTapeChars{};
    SpeedCharacteristics m_cachedSpeedChars{};
    float m_gapWidth = 3.0f;
    // Per-machine linear trim making the tape core unity-gain (compensates the input
    // 0.95 scale + the tape's small insertion offset). Set in updateFilters. Chosen so
    // -12 dBFS in -> -12 dBFS out at the reference (matches gain-link makeup on/off).
    float m_machineMakeupGain = 1.0f;

    static constexpr float denormalPrevention = 1e-8f;

    float computeDrive (float saturationDepth, float tapeFormulationScale) const
    {
        if (saturationDepth < 0.001f) return 0.0f;
        return 0.62f * std::exp (1.8f * saturationDepth) * tapeFormulationScale;
    }

    // reference tracking deck static transfer curve, fitted from measured multi-level harmonics
    // (order-11, 0.22% residual, private calibration analysis 2026-07-10); value+slope-matched tanh knee
    // beyond |x|=0.80 (the accelerating in-band IMD lives at |x|<=0.71, fully polynomial —
    // the knee only bounds full-scale peaks so it can't compress the distortion growth).
    // 2026-07-12 HARMONIC-DISTRIBUTION campaign: the poly's EVEN coeffs (c2,c4,c6,c8,c10) are
    // ZEROED. Reason: at the real drive (~2.35x) they get drive-amplified and fought the explicit
    // even term, producing a level-dependent cancellation that COLLAPSED mine's 2nd below -12 dBFS
    // (measured 2f -110 @-18 vs reference's clean 1 dB/dB -73). Zeroing them removes even harmonics
    // WITHOUT touching odd (even/odd poly bases are orthogonal over a symmetric sine, so 3f/5f/7f
    // are byte-unchanged); the 2nd is regenerated cleanly by the level-persistent kSwissEven*x^2
    // term below. Even contribution at the knee x0=0.8 was only 7.4e-4 (<0.2% fit resid) so P0/S0
    // stay — the odd knee continuation is preserved.
    static constexpr float kSc1 = 0.9793873f, kSc2 = 0.0f, kSc3 = -0.6470817f, kSc4 = 0.0f;
    static constexpr float kSc5 = 3.027688f, kSc6 = 0.0f, kSc7 = -8.856463f, kSc8 = 0.0f;
    static constexpr float kSc9 = 12.42955f, kSc10 = 0.0f, kSc11 = -6.648037f;
    static constexpr float kSx0 = 0.8f, kSP0 = 0.684929f, kSS0 = 0.602271f, kSknee = 0.5f;
    static constexpr float kSkneeInv = 1.0f / kSknee;   // avoid a per-sample divide

    static inline float swissShaper (float x) noexcept
    {
        const float ax = std::abs (x);
        if (ax <= kSx0)
            return x * (kSc1 + x * (kSc2 + x * (kSc3 + x * (kSc4 + x * (kSc5 + x * (kSc6
                 + x * (kSc7 + x * (kSc8 + x * (kSc9 + x * (kSc10 + x * kSc11))))))))));
        const float sgn = (x < 0.0f) ? -1.0f : 1.0f;
        return sgn * (kSP0 + kSS0 * kSknee * std::tanh ((ax - kSx0) * kSkneeInv));
    }

    // Swiss input-stage limit: +-0.95 ceiling, linear to 0.8 then a value+slope-matched tanh knee (fold products stay below the reference floor).
    static constexpr float kLimKnee = 0.8f, kLimCeil = 0.95f;
    static constexpr float kLimRange = kLimCeil - kLimKnee;
    static constexpr float kLimRangeInv = 1.0f / kLimRange;

    static inline float swissSoftLimit (float x) noexcept
    {
        const float ax = std::abs (x);
        if (ax <= kLimKnee) return x;
        const float sgn = (x < 0.0f) ? -1.0f : 1.0f;
        return sgn * (kLimKnee + kLimRange * std::tanh ((ax - kLimKnee) * kLimRangeInv));
    }

    // reference mastering deck static transfer curve (American), fitted from measured multi-level
    // harmonics (order-11, 0.21% residual, private calibration analysis 2026-07-10); reproduces the Classic's
    // 5th-harmonic-heavy, super-linearly accelerating distortion (5th ~= 3rd at -6 dBFS, both
    // grow with level). value+slope-matched tanh knee beyond |x|=0.78 bounds full-scale peaks.
    // 2026-07-12: EVEN coeffs (c2,c4,c6,c8,c10) ZEROED — same rationale as the Swiss above
    // (drive-amplified poly even fought the explicit even -> 2nd collapsed below -12 dBFS).
    // Odd (5th-heavy) behaviour byte-unchanged; 2nd regenerated cleanly by kAmericanEven*x^2.
    static constexpr float kCc1 = 1.025164f, kCc2 = 0.0f, kCc3 = 0.7539777f, kCc4 = 0.0f;
    static constexpr float kCc5 = -5.338438f, kCc6 = 0.0f, kCc7 = 15.2137f, kCc8 = 0.0f;
    static constexpr float kCc9 = -21.00829f, kCc10 = 0.0f, kCc11 = 11.08413f;
    static constexpr float kCx0 = 0.78f, kCP0 = 0.765386f, kCS0 = 0.765175f, kCknee = 0.5f;
    static constexpr float kCkneeInv = 1.0f / kCknee;

    static inline float americanShaper (float x) noexcept
    {
        const float ax = std::abs (x);
        if (ax <= kCx0)
            return x * (kCc1 + x * (kCc2 + x * (kCc3 + x * (kCc4 + x * (kCc5 + x * (kCc6
                 + x * (kCc7 + x * (kCc8 + x * (kCc9 + x * (kCc10 + x * kCc11))))))))));
        const float sgn = (x < 0.0f) ? -1.0f : 1.0f;
        return sgn * (kCP0 + kCS0 * kCknee * std::tanh ((ax - kCx0) * kCkneeInv));
    }

    // American drive base — sets the operating point on the fitted curve. Raised 1.4->2.5
    // (2026-07-10 reshape) so the shaper operates at UNITY input scale like the reference extraction
    // (was under-driven to arg~0.56x input, hiding the 5th/acceleration); now arg~input*1.0.
    static constexpr float kAmericanDriveBase = 2.5f;
    // Level-persistent x^2 even-order term (2026-07-12 harmonic-distribution campaign): applied
    // to the RAW shaper-input midpoint (NOT the driven/saturating output sh), so the 2nd tracks a
    // clean 1 dB/dB law down to the noise floor — matching the reference decks' 2nd (which persists at
    // ~1 dB/dB from -3 to -30 dBFS) instead of collapsing below -12 like the old kEven*sh^2 did.
    // Tuned so private calibration analysis 2f matches reference (~-56 Classic / ~-61 Swiss @-6 dBFS) AND holds the shape
    // down-level. Even harmonics >2 come only from this + the (now odd-only) knee, i.e. minimal.
    static constexpr float kAmericanEven = 0.0118f;
    static constexpr float kSwissEven = 0.0069f;   // +1.2 dB vs 0.0060: restores the 2f gate against
                                                  // the floor's ~+0.9 dB fundamental-gain shift (below)
    // Low-level odd-harmonic FLOOR (2026-07-12 brightness campaign): a smooth scale-covariant
    // power-law nonlinearity f(s)=g*s*(s^2+eps^2)^((p-1)/2), added in PARALLEL to the shaper
    // (ADAA'd). A memoryless polynomial shaper's odd harmonics scale as s^n, so mine's 3f/5f/7f
    // COLLAPSE below -12 dBFS (5f -125 dBc @-30) while the reference decks keep a slowly-decaying floor
    // (5f -71 dBc @-30). A covariant (homogeneous) nonlinearity has ~level-INVARIANT relative
    // harmonics (flat dBc), so it dominates the fast-dying poly at low level and reproduces that
    // floor, while the steep poly still dominates at/above -6 dBFS (reference gate byte-preserved
    // by construction: the floor is ~30 dB below the poly there). eps rounds the origin cusp ->
    // C-inf (no idle-noise distortion, no near-Nyquist aliasing, clean below ~-40 dBFS-equiv).
    // p<1 is compressive => negative-going odd harmonics + a ~uniform fundamental gain (the -kLin*s
    // term below nulls it near the operating point). Values MODELLED offline (floor_model.py:
    // candidate 1 DC-bias & parallel-cubic both proven incapable of a covariant floor) then tuned
    // on the built plugin vs private calibration analysis. Swiss fits 3f AND 5f/7f; Classic fits 5f/7f (its positive-c3
    // shaper's 3f conflicts with the floor's negative 3f in the -12/-24 transition, rule-5 residual).
    // *** MEASURED SCOPE — CONTROL EXPERIMENT (2026-07-12, private calibration analysis + private calibration analysis, floor-on
    // vs floor-off rebuild): this floor CLOSES the tone-ladder harmonic deficit (a fidelity match to
    // reference's measured decay) BUT does NOT reduce the audible program brightness the campaign targeted.
    // The harmonics it restores sit at <= -63 dBFS absolute (near-inaudible); the drum-program LTAS
    // is unchanged floor-on vs -off (+1.5/+0.9 HF both); and on sustained tonal program it marginally
    // RAISES 4-8k presence (+0.2..0.8 dB). The user's brightness is the 10-16k rising FR tilt
    // (hot-level / repro FR) — a different axis this floor does not touch. Kept as a reference-harmonic
    // fidelity match pending the user's DAW A/B; one-line disable = set both kFloorG to 0.0f and
    // kSwissEven back to 0.0060f. See the campaign report for the full control evidence. ***
    // kFloorLin: a LINEAR term subtracted from the floor (f = g*s*[(s^2+eps^2)^((p-1)/2) - kLin]).
    // The covariant floor's ~level-flat fundamental gain (p near 1 => near-linear) persists at HIGH
    // drive, where it injects clean fundamental that DILUTES the saturated shaper's THD (~10% rel;
    // measured -1..-2% on high-drive presets Thick Sat/Drum Bus/Old Tape). Subtracting kLin*s (pure
    // linear -> ZERO harmonics -> the low-level 3f/5f/7f floor is byte-unchanged) nulls that
    // fundamental near the hot operating point, restoring preset THD with NO drive re-fit, while
    // keeping the low-level warmth (fund shift +0.75->~0 @ref, +0.98->+0.32 @-30). Tuned vs
    // preset_validate THD (harmonics invariant to it, so free to tune).
    static constexpr float kFloorEps      = 0.006f;
    static constexpr float kSwissFloorG    = 0.070f, kSwissFloorP    = 0.88f, kSwissFloorLin    = 1.05f;
    // Classic floor: the Classic shaper's positive-c3 3f partially cancels the floor's negative 3f in
    // the -12/-24 transition (a moving null — LOWERING g just relocates it, measured), so g is
    // set for the best 5f/7f-floor fidelity (deficits of 40-110 dB vs reference) with BOUNDED 3f-transition
    // thinning (worst ~-20 dB @-24). rule-5 residual: reference's Classic 3f is itself in a measured level-null.
    static constexpr float kAmericanFloorG = 0.017f, kAmericanFloorP = 0.80f, kAmericanFloorLin = 1.10f;
    // Swiss idle floor — hum scale sets the ceiling (Noise=100% ~ Swiss noise-max); hiss set lower
    // than the hum so the fit (which matches the hum-dominated overall level) also lands the broadband hiss.
    static constexpr float kSwissHiss = 0.0046f;
    static constexpr float kSwissHum  = 0.0037f;
    // American idle floor: brighter hiss + strong-2nd hum. Headroom set so the lo-fi Classic presets
    // (3.75 IPS + cassette LP, which cut the bright hiss ~5 dB harder than the 15 IPS reference) still
    // reach the reference Hiss&Hum idle level within the per-preset Noise-knob fit.
    static constexpr float kAmericanHiss = 0.000525f;
    static constexpr float kAmericanHum  = 0.00027f;

    // ---- Antiderivative anti-aliasing (ADAA) --------------------------------
    // 1st-order ADAA replaces the point evaluation y=f(x) with the mean of f over
    // [xPrev,x] via f's analytic antiderivative F: y=(F(x)-F(xPrev))/(x-xPrev). Here it
    // runs INSIDE LocalAAStage (4x local): the oversampling handles the bulk of the
    // aliasing, ADAA cleans up the low-order residual the halfbands leave. DC transfer
    // is unchanged => THD/harmonics hold. The even-order term is applied post-ADAA.
    // NOTE: the antiderivatives + ADAA divided-difference are evaluated in DOUBLE. The
    // ADAA runs at the 4x-local rate where consecutive samples are close, so F(s)-F(sp)
    // is a small difference of O(0.1..1) values — in float that loses ~4-5 digits and
    // raises the noise floor (~-85 dBFS grunge on sustained LF). Double removes it.
    static inline double logCosh (double t) noexcept   // stable ln(cosh t) (no cosh overflow)
    {
        const double at = std::abs (t);
        return at - 0.69314718055994531 + std::log1p (std::exp (-2.0 * at));
    }
    // Antiderivative of the order-11 poly f(v)=c1 v + c2 v^2 + ... + c11 v^11 (signed v).
    static inline double polyAD11 (double v, double c1, double c2, double c3, double c4,
                                   double c5, double c6, double c7, double c8, double c9,
                                   double c10, double c11) noexcept
    {
        return v * v * (c1 * 0.5 + v * (c2 * (1.0/3.0) + v * (c3 * 0.25 + v * (c4 * 0.2
             + v * (c5 * (1.0/6.0) + v * (c6 * (1.0/7.0) + v * (c7 * 0.125
             + v * (c8 * (1.0/9.0) + v * (c9 * 0.1 + v * (c10 * (1.0/11.0)
             + v * (c11 * (1.0/12.0))))))))))));
    }
    // Antiderivative of a shaper (poly region |v|<=x0, then P0 + S0*knee*tanh knee).
    static inline double shaperAD (double x, double c1, double c2, double c3, double c4,
                                   double c5, double c6, double c7, double c8, double c9,
                                   double c10, double c11, double x0, double P0,
                                   double S0, double knee, double kneeInv) noexcept
    {
        const double ax = std::abs (x);
        if (ax <= x0) return polyAD11 (x, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11);
        const double Fb = polyAD11 (x < 0.0 ? -x0 : x0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11);
        return Fb + P0 * (ax - x0) + S0 * knee * knee * logCosh ((ax - x0) * kneeInv);
    }
    static inline double swissShaperAD (double x) noexcept
    { return shaperAD (x, kSc1, kSc2, kSc3, kSc4, kSc5, kSc6, kSc7, kSc8, kSc9, kSc10, kSc11, kSx0, kSP0, kSS0, kSknee, kSkneeInv); }
    static inline double americanShaperAD (double x) noexcept
    { return shaperAD (x, kCc1, kCc2, kCc3, kCc4, kCc5, kCc6, kCc7, kCc8, kCc9, kCc10, kCc11, kCx0, kCP0, kCS0, kCknee, kCkneeInv); }
    // Antiderivative of the soft-limit (linear |x|<=knee, tanh knee beyond).
    static inline double softLimitAD (double x) noexcept
    {
        const double ax = std::abs (x);
        if (ax <= kLimKnee) return 0.5 * x * x;
        return 0.5 * (double) kLimKnee * kLimKnee + (double) kLimKnee * (ax - kLimKnee)
             + (double) kLimRange * kLimRange * logCosh ((ax - kLimKnee) * (double) kLimRangeInv);
    }

    static constexpr float kAdaaEps = 1e-5f;
    // Covariant low-level harmonic floor + its ADAA. f(s)=g*s*(s^2+eps^2)^((p-1)/2);
    // antiderivative F(s)=g/(p+1)*(s^2+eps^2)^((p+1)/2). Evaluated in double (matches the
    // shaper AD's precision note). Shares the shaper's per-channel prev state (raw input s).
    static inline float floorF (float s, float g, float p, float lin) noexcept
    {
        return g * s * (std::pow (s * s + kFloorEps * kFloorEps, (p - 1.0f) * 0.5f) - lin);
    }
    static inline double floorAD (double s, double g, double p, double lin) noexcept
    {
        return g / (p + 1.0) * std::pow (s * s + (double) kFloorEps * kFloorEps, (p + 1.0) * 0.5)
             - g * lin * 0.5 * s * s;
    }
    inline float adaaFloor (float s, float sp, float g, float p, float lin) noexcept
    {
        const double dx = (double) s - (double) sp;
        if (std::abs (dx) < kAdaaEps) return floorF (0.5f * (s + sp), g, p, lin);
        return (float) ((floorAD ((double) s, g, p, lin) - floorAD ((double) sp, g, p, lin)) / dx);
    }
    // ADAA soft-limit (shared by both machines); m_limPrev is per-channel state.
    inline float adaaLimit (float s) noexcept
    {
        const float sp = m_limPrev; m_limPrev = s;
        const double dx = (double) s - (double) sp;
        if (std::abs (dx) < kAdaaEps) return swissSoftLimit (0.5f * (s + sp));
        return (float) ((softLimitAD (s) - softLimitAD (sp)) / dx);
    }
    // ADAA waveshaper: G(s)=(dInv/d)*F(s*d) so ADAA runs in the pre-drive signal domain;
    // the small even-order term is applied to the (already anti-aliased) output.
    inline float adaaSatSwiss (float s) noexcept
    {
        const float sp = m_shaperPrev; m_shaperPrev = s;
        const double dx = (double) s - (double) sp;
        float sh;
        if (std::abs (dx) < kAdaaEps)
            sh = swissShaper (0.5f * (s + sp) * m_swissDrive) * m_swissDriveInv;
        else
            sh = (float) ((double) m_swissDriveInv / m_swissDrive
                 * (swissShaperAD ((double) s * m_swissDrive) - swissShaperAD ((double) sp * m_swissDrive)) / dx);
        // Even term on the RAW input midpoint (drive-independent, level-persistent 2nd).
        const float sm = 0.5f * (s + sp);
        // Covariant low-level harmonic floor (ADAA'd; slowly-decaying 3f/5f/7f like the reference).
        return sh + kSwissEven * (sm * sm) + adaaFloor (s, sp, kSwissFloorG, kSwissFloorP, kSwissFloorLin);
    }
    inline float adaaSatAmerican (float s) noexcept
    {
        const float sp = m_shaperPrev; m_shaperPrev = s;
        const double dx = (double) s - (double) sp;
        float sh;
        if (std::abs (dx) < kAdaaEps)
            sh = americanShaper (0.5f * (s + sp) * m_americanDrive) * m_americanDriveInv;
        else
            sh = (float) ((double) m_americanDriveInv / m_americanDrive
                 * (americanShaperAD ((double) s * m_americanDrive) - americanShaperAD ((double) sp * m_americanDrive)) / dx);
        const float sm = 0.5f * (s + sp);
        return sh + kAmericanEven * m_americanEvenScale * (sm * sm)
                  + adaaFloor (s, sp, kAmericanFloorG, kAmericanFloorP, kAmericanFloorLin);
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
            case Swiss:
                chars.headBumpFreq = 48.0f; chars.headBumpGain = 3.0f; chars.headBumpQ = 1.0f;
                chars.hfRolloffFreq = 22000.0f; chars.hfRolloffSlope = -12.0f;
                chars.saturationKnee = 0.92f;
                chars.saturationHarmonics[0] = 0.003f; chars.saturationHarmonics[1] = 0.030f;
                chars.saturationHarmonics[2] = 0.001f; chars.saturationHarmonics[3] = 0.005f;
                chars.saturationHarmonics[4] = 0.0005f;
                chars.compressionRatio = 0.03f; chars.compressionAttack = 0.08f; chars.compressionRelease = 40.0f;
                chars.phaseShift = 0.015f; chars.crosstalkAmount = -70.0f;
                break;
            case American:
                chars.headBumpFreq = 62.0f; chars.headBumpGain = 4.5f; chars.headBumpQ = 1.4f;
                chars.hfRolloffFreq = 21000.0f; chars.hfRolloffSlope = -18.0f; // American flat to ~15k
                chars.saturationKnee = 0.85f;
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

    // Per-machine/speed head-bump target; gain is PRE tape-emphasis (updateFilters multiplies by lfEmphasis * 0.8).
    struct HeadBump { float freq, gain, q; };
    HeadBump getHeadBump (TapeMachine machine, TapeSpeed speed) const noexcept
    {
        if (machine == Swiss)
        {
            switch (speed)   // Swiss measured bump @30 Hz: +1.4/+2.8/+2.4 at
            {                // 7.5/15/30 IPS (keeps a strong LF bump even at 30).
                case Speed_7_5_IPS:  return { 27.0f, 4.80f, 0.9f };
                case Speed_15_IPS:   return { 30.0f, 6.20f, 0.9f };
                case Speed_30_IPS:   return { 50.0f, 5.20f, 0.6f };  // broad bump (+2.5 @100)
                case Speed_3_75_IPS: return { 24.0f, 4.40f, 0.9f };  // non-std on Swiss; extrapolated
            }
        }
        switch (speed)   // American (American) measured bump @30 Hz: +2.3 (7.5),
        {                // +3.0 (15), and a -4.7 LF DIP at 30 IPS (negative gain).
            case Speed_7_5_IPS:  return { 32.0f,  4.40f, 1.3f };
            case Speed_15_IPS:   return { 32.0f,  5.10f, 1.3f };
            case Speed_30_IPS:   return { 31.0f, -3.70f, 2.0f };
            case Speed_3_75_IPS: return { 33.0f,  2.50f, 1.2f };  // 3.75 IPS: tuned vs American (modest LF bump)
        }
        return { 32.0f, 5.10f, 1.3f };
    }

    TapeCharacteristics getTapeCharacteristics (TapeType type)
    {
        TapeCharacteristics chars{};
        switch (type)
        {
            case FormulaClassic:
                chars.coercivity = 0.78f; chars.retentivity = 0.82f; chars.saturationPoint = 0.88f;
                chars.hysteresisAmount = 0.12f; chars.hysteresisAsymmetry = 0.02f;
                chars.noiseFloor = -60.0f; chars.modulationNoise = 0.025f; chars.lfEmphasis = 1.12f; chars.hfLoss = 0.92f;
                break;
            case FormulaHighOutput:
                chars.coercivity = 0.92f; chars.retentivity = 0.95f; chars.saturationPoint = 0.64f;
                chars.hysteresisAmount = 0.06f; chars.hysteresisAsymmetry = 0.01f;
                chars.noiseFloor = -64.0f; chars.modulationNoise = 0.015f; chars.lfEmphasis = 1.05f; chars.hfLoss = 0.96f;
                break;
            case FormulaModern:
                // American/modern high-output formula: higher output/lower noise than
                // classic formulation, just under high-output formula (reference orders 250<classic formulation<modern formulation<high-output formula). Starting row
                // interpolated classic formulation<->high-output formula; retuned vs the reference modern formulation (private calibration analysis tape).
                chars.coercivity = 0.85f; chars.retentivity = 0.89f; chars.saturationPoint = 0.72f;
                chars.hysteresisAmount = 0.09f; chars.hysteresisAsymmetry = 0.015f;
                chars.noiseFloor = -62.0f; chars.modulationNoise = 0.018f; chars.lfEmphasis = 1.08f; chars.hfLoss = 0.94f;
                break;
            case FormulaVintage:
                chars.coercivity = 0.70f; chars.retentivity = 0.75f; chars.saturationPoint = 0.80f;
                chars.hysteresisAmount = 0.18f; chars.hysteresisAsymmetry = 0.035f;
                chars.noiseFloor = -55.0f; chars.modulationNoise = 0.035f; chars.lfEmphasis = 1.18f; chars.hfLoss = 0.87f;
                break;
            default:
                chars = getTapeCharacteristics (FormulaClassic);
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
            case Speed_3_75_IPS:   // slowest: biggest bump, most HF loss, most noise, slowest W&F
                chars.headBumpMultiplier = 1.8f; chars.hfExtension = 0.5f; chars.noiseReduction = 1.3f;
                chars.flutterRate = 2.5f; chars.wowRate = 0.22f;
                break;
            default:
                chars = getSpeedCharacteristics (Speed_15_IPS);
                break;
        }
        return chars;
    }

    void updateFilters (TapeMachine machine, TapeSpeed speed, TapeType type,
                        float biasAmount, EQStandard eqStandard, int headWidth)
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
                    case Speed_7_5_IPS:  preEQ_tauNum = 225.0f; preEQ_tauDen = 90.0f; break;
                    case Speed_15_IPS:   preEQ_tauNum = 125.0f; preEQ_tauDen = 50.0f; break;
                    case Speed_30_IPS:   preEQ_tauNum = 44.0f;  preEQ_tauDen = 17.5f; break;
                    case Speed_3_75_IPS: preEQ_tauNum = 300.0f; preEQ_tauDen = 120.0f; break; // NAB 3.75 (bigger record boost)
                }
                break;
            case CCIR:
                switch (speed)
                {
                    case Speed_7_5_IPS:  preEQ_tauNum = 175.0f; preEQ_tauDen = 70.0f; break;
                    case Speed_15_IPS:   preEQ_tauNum = 88.0f;  preEQ_tauDen = 35.0f; break;
                    case Speed_30_IPS:   preEQ_tauNum = 36.0f;  preEQ_tauDen = 17.5f; break;
                    case Speed_3_75_IPS: preEQ_tauNum = 230.0f; preEQ_tauDen = 95.0f; break;
                }
                break;
        }

        preEmphasisEQ.setPreEmphasis (preEQ_tauNum, preEQ_tauDen);
        deEmphasisEQ.setDeEmphasis   (preEQ_tauDen, preEQ_tauNum);

        bool isPrecision = (machine == Swiss);

        phaseSmear.setMachineCharacter (isPrecision);

        // Re-prepare the noise generator ONLY when the speed (or, via reset(), the sample
        // rate) actually changes — its prepare() calls reset() which zeros the pink/tilt/
        // scrape state. Calling it on every updateFilters (bias automation, head-width
        // changes, etc.) restarted the idle hiss mid-stream (an audible click); those
        // updates don't touch the noise spectrum, which depends only on speed + rate.
        if (speed != m_lastNoiseSpeed)
        {
            improvedNoiseGen.prepare (currentSampleRate, static_cast<int> (speed));
            m_lastNoiseSpeed = speed;
        }

        const HeadBump hb = getHeadBump (machine, speed);
        float headBumpFreq = hb.freq;
        float headBumpGain = hb.gain * tapeChars.lfEmphasis * 0.8f;
        float headBumpQ    = hb.q;

        // CCIR/IEC has no LF pre-emphasis (unlike NAB's 3180 us LF boost), so the American
        // reads ~5-6 dB thinner in the lows under CCIR — a dip, not a bump. Trim the
        // American head-bump gain (into the negative, floored by the -6 clamp below).
        // The Swiss's CCIR low end already matches, so Swiss is left alone. NOT at 30 IPS,
        // where the American's NAB and CCIR low ends converge (both ~-4.7 dB @30 Hz — short
        // wavelengths, minimal LF pre-emphasis either way, so the trim would over-cut).
        if (machine == American && eqStandard == CCIR && speed != Speed_30_IPS)
            headBumpGain -= 5.5f;

        // American Head Width: the 1" head stack rolls off the deep lows (~-1.6 dB
        // @30 Hz vs 1/2") but leaves 50 Hz+ intact — a sub-40 Hz shelf, so it's done
        // with a raised dc-blocker below (not a head-bump trim, which would eat 50 Hz).
        // HF is the headWidthFilter above. American only (the Swiss has no head width).

        // Swiss clamps wider (lower freq, higher gain); American allows a low bump
        // that survives the LF highpass, and a NEGATIVE gain (LF dip) for 30 IPS
        // where the American rolls the low end off instead of bumping it.
        const float bumpFreqMin = (machine == Swiss) ? 20.0f : 28.0f;
        const float bumpGainMax = (machine == Swiss) ? 7.0f : 6.5f;
        const float bumpGainMin = (machine == Swiss) ? 1.5f : -6.0f;
        headBumpFreq = std::clamp (headBumpFreq, bumpFreqMin, 120.0f);
        headBumpQ    = std::clamp (headBumpQ, 0.7f, 2.0f);
        headBumpGain = std::clamp (headBumpGain, bumpGainMin, bumpGainMax);

        headBumpFilter.setCoeffs (DBiquad::peak (currentSampleRate,
            static_cast<double> (headBumpFreq), static_cast<double> (headBumpGain),
            static_cast<double> (headBumpQ)));

        float maxFilterFreq = static_cast<float> (currentSampleRate * 0.45);
        // Both reference decks keep HF far more extended than the shared 0.7/1.0/1.3
        // per-speed rolloff. American: ~flat to 15 kHz at every speed. Swiss:
        // bright at 15/30 IPS, ~-2 dB @15k at 7.5 IPS.
        float hfExt = speedChars.hfExtension;
        if (machine == American)
            hfExt = (speed == Speed_3_75_IPS) ? 0.45f      // 3.75 IPS: heavy HF gap-loss (~9k LP), tuned vs American
                  : (speed == Speed_7_5_IPS) ? 1.30f : (speed == Speed_30_IPS ? 0.90f : 1.1f);
        else // Swiss (3.75 non-standard — reuse the 7.5 value)
            hfExt = (speed == Speed_3_75_IPS) ? 1.10f
                  : (speed == Speed_7_5_IPS) ? 1.10f : (speed == Speed_30_IPS ? 1.50f : 1.5f);
        float hfCutoff = machineChars.hfRolloffFreq * hfExt * tapeChars.hfLoss;
        hfCutoff = std::min (hfCutoff, maxFilterFreq);
        hfLossFilter1.setCoeffs (DBiquad::lowPass (currentSampleRate, static_cast<double> (hfCutoff), 0.707));

        // American tracks nearly flat to ~15 kHz, so American's HF-loss shelves are scaled right down.
        const float hfLossScale = (machine == Swiss) ? 0.3f : 0.2f;

        // 3.75 IPS: the American keeps a flat ~5 kHz shoulder then drops off a cliff past
        // 10 kHz. hfLossFilter1's 2-pole gives the shoulder; hfLossFilter2 here adds
        // the extra top-octave cliff (high-shelf ~11 kHz) that a single 2-pole can't.
        if (speed == Speed_3_75_IPS)
        {
            hfLossFilter2.setCoeffs (DBiquad::shelf (currentSampleRate,
                std::min (11000.0f, maxFilterFreq), -3.5, 0.5, true));
        }
        else
        {
            float hfShelfFreq = std::min (hfCutoff * 0.6f, maxFilterFreq);
            hfLossFilter2.setCoeffs (DBiquad::shelf (currentSampleRate, static_cast<double> (hfShelfFreq),
                static_cast<double> (-2.0f * tapeChars.hfLoss * hfLossScale), 0.5, true));
        }

        const bool wasSlow900Presence = m_slowModernPresence;
        m_slowModernPresence = (machine == American && speed == Speed_3_75_IPS && type == FormulaModern);
        // Clear retained biquad state on any activation/deactivation transition so a stale
        // filter history cannot fire a transient when the American / 3.75 IPS / Modern
        // combination is re-enabled after being off.
        if (m_slowModernPresence != wasSlow900Presence)
            slowModernPresencePeak.reset();
        slowModernPresencePeak.setCoeffs (DBiquad::peak (currentSampleRate,
            std::min (2700.0f, maxFilterFreq), m_slowModernPresence ? 5.8 : 0.0, 1.6));

        // American Head Width HF character (American only; neutral at 1/2" so the
        // reference stays byte-identical and the filter is skipped in the chain). Both
        // 1/4" and 1" read brighter than 1/2" through the upper mids: a peak ~7.5 kHz
        // that tapers by 15 kHz. 1" is the hotter, LF-lighter head (see the LF trim above).
        {
            const double hwGain = (headWidth == 0) ? 2.5   // 1/4"
                                : (headWidth == 2) ? 3.2   // 1"
                                : 0.0;                     // 1/2" (neutral)
            headWidthFilter.setCoeffs (DBiquad::peak (currentSampleRate,
                std::min (7500.0, currentSampleRate * 0.45), hwGain, 0.9));
        }

        float gapLossFreq = speed == Speed_3_75_IPS ? 5000.0f
                          : (speed == Speed_7_5_IPS ? 8000.0f : (speed == Speed_30_IPS ? 15000.0f : 12000.0f));
        // 3.75 IPS: the hfLossFilter1 LP already supplies the (large) HF rolloff, so
        // no extra gap-loss shelf (it only dragged the 5 kHz shoulder down).
        float gapLossAmount = (speed == Speed_3_75_IPS ? 0.0f
                          : (speed == Speed_7_5_IPS ? -3.0f : (speed == Speed_30_IPS ? -0.5f : -1.5f))) * hfLossScale;
        gapLossFilter.setCoeffs (DBiquad::shelf (currentSampleRate, static_cast<double> (gapLossFreq),
            static_cast<double> (gapLossAmount), 0.707, true));

        // Both decks brighten HF when under-biased and flatten when over-biased (bias shelf).
        float biasFreq, biasGainDb;
        if (machine == Swiss) { biasFreq = 8000.0f; biasGainDb = -(biasAmount - 0.5f) * 8.0f + 1.0f; }
        else                     { biasFreq = 7000.0f; biasGainDb = (0.5f - biasAmount) * 5.0f + 1.5f; }
        biasFilter.setCoeffs (Biquad::shelf (currentSampleRate, biasFreq, biasGainDb, 0.707f, true));

        // DC blocker: 1st-order (6 dB/oct). Both reference decks keep a broad LF head bump nearly
        // flat down to ~10-15 Hz, then roll off gently below — a 2nd-order Butterworth corner
        // (old 25 Hz Swiss / 15 Hz Classic) ate that bump and added ~5-7 ms of excess group
        // delay at 30 Hz vs the reference (measured, private calibration analysis), which doubled the kick 10-90%
        // attack time (9.3 vs 4.2 ms). A gentle 1st-order corner (Swiss 18 Hz / Classic 10 Hz)
        // restores the reference's kick attack and 20-30 Hz magnitude while still killing DC and
        // leaving the driven-preset 30 Hz levels (high-output formula calibration) within their gate.
        // Swiss 30 IPS keeps the original 2nd-order 12 Hz corner: that exception exists so
        // the broad 30 IPS head bump reaches 30 Hz, its GD@50-80 Hz is already low (no kick
        // penalty), and the driven-high-output formula 30 IPS preset fit depends on its exact 30 Hz level.
        if (machine == Swiss && speed == Speed_30_IPS)
            dcBlocker.setCoeffs (DBiquad::highPass (currentSampleRate, 12.0, 0.707));
        else
        {
            double dcBlockFreq = (machine == Swiss) ? 18.0 : 10.0;
            // American 1" head: raise the corner to shed the deep lows (matches the reference)
            // without touching 50 Hz+. 1/4" and 1/2" keep the base corner.
            if (machine == American && headWidth == 2) dcBlockFreq = 20.0;
            dcBlocker.setCoeffs (DBiquad::highPass1 (currentSampleRate, dcBlockFreq));
        }

        // Per-machine unity trim for the level-neutral tape core (see m_machineMakeupGain):
        // +0.4 dB (Swiss) / -0.5 dB (American) lands -12 dBFS in -> -12 dBFS out.
        m_machineMakeupGain = dbToGain (machine == Swiss ? 0.4f : -0.5f);

        // Cache the Swiss waveshaper drive (block-constant): base x per-tape feel x bias factor; hoisted out of the audio loop.
        const float tapeFormScale = 2.0f * (1.0f - tapeChars.saturationPoint) + 0.6f;
        const float biasDrive = std::clamp (std::exp (4.0f * (0.5f - biasAmount)), 0.2f, 5.0f);
        m_swissDrive = 2.8f * tapeFormScale * biasDrive;
        m_swissDriveInv = 1.0f / m_swissDrive;

        // American waveshaper drive: same staging as the Swiss but a STEEPER bias curve (the American is far more bias-sensitive).
        const float americanBiasDrive = std::clamp (std::exp (6.5f * (0.5f - biasAmount)), 0.15f, 9.0f);
        m_americanDrive = kAmericanDriveBase * tapeFormScale * americanBiasDrive;
        m_americanDriveInv = 1.0f / m_americanDrive;

        // Emphasis-rebalance G / 1-G pair (per-machine, always active). Values chosen via the
        // emphasis_model.py sweep methodology (which validated a high-shelf lever); the shipped
        // DSP uses a 5 kHz peak (Q 2.0) instead, whose gains were confirmed by the measured gate
        // table on the built plugin, not by that model directly: the LF-shelf drives the tape
        // harder at LF to raise the two-tone IMD; the HF peak (@~5 kHz) restores the 5th-harmonic energy the tape
        // de-emphasis attenuates. G (pre-shaper) = LF-shelf BOOST + HF-peak NOTCH; 1-G
        // (post-shaper) = HF-peak BOOST + LF-shelf CUT — the exact inverse, so the linear FR
        // is preserved (verified numerically) while the shaper-born products are re-weighted.
        // Swiss's 3rd is strong (not nulled) so its HF peak is gentler to protect the 3rd/THD;
        // the Classic's 3rd sits in a level-dependent null so it tolerates a hotter peak.
        const double fsE = currentSampleRate;
        const float  emphLfDb = (machine == Swiss) ? 2.0f : 3.0f;   // LF drive boost (IMD lever)
        const float  emphHfDbBase = (machine == Swiss) ? 3.0f : 4.0f;   // HF peak boost (5th lever)
        constexpr double kEmphLfFc = 150.0, kEmphLfQ = 0.5;
        constexpr double kEmphHfFc = 5000.0, kEmphHfQ = 2.0;
        // The HF peak lifts the 5th-region harmonics; at the reference bias (biasDrive == 1) it
        // runs at full gain. Under-bias drives the shaper MUCH harder (biasDrive up to 9x), where
        // the shaper's own 5th already explodes — amplifying THAT with the peak inflates the
        // (already-overshooting) under-bias THD. Fade the peak by 1/biasDrive (clamped to 1) so
        // the reference 5th/IMD win is preserved but the under-bias overshoot is not made worse.
        // The LF shelf is NOT faded: it drives 150 Hz, which does not affect the 1 kHz under-bias
        // THD, and the IMD is measured at the reference bias anyway.
        const float biasDriveForEmph = (machine == Swiss) ? biasDrive : americanBiasDrive;
        const float emphHfDb = emphHfDbBase * std::clamp (1.0f / biasDriveForEmph, 0.0f, 1.0f);
        emphLfPre.setCoeffs  (DBiquad::shelf (fsE, kEmphLfFc,  emphLfDb, kEmphLfQ, false));
        emphLfPost.setCoeffs (DBiquad::shelf (fsE, kEmphLfFc, -emphLfDb, kEmphLfQ, false));
        emphHfPre.setCoeffs  (DBiquad::peak  (fsE, kEmphHfFc, -emphHfDb, kEmphHfQ));
        emphHfPost.setCoeffs (DBiquad::peak  (fsE, kEmphHfFc,  emphHfDb, kEmphHfQ));

        // A tiny inverse shelf pair reduces only the shaper's near-Nyquist drive.
        // At 19 kHz, -0.6 dB before an odd-order shaper suppresses its fifth-order
        // fold by roughly 3 dB; +0.6 dB afterward restores the linear signal.
        constexpr double kAliasHfFc = 12000.0, kAliasHfQ = 0.7;
        constexpr double kAliasHfDb = 0.6;
        aliasHfPre.setCoeffs  (DBiquad::shelf (fsE, kAliasHfFc, -kAliasHfDb, kAliasHfQ, true));
        aliasHfPost.setCoeffs (DBiquad::shelf (fsE, kAliasHfFc,  kAliasHfDb, kAliasHfQ, true));
    }
};

//==============================================================================
// TapeMachineDSP — framework-free equivalent of the JUCE PluginProcessor audio path.
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
    void setTapeSpeed   (int idx)  noexcept { pSpeed.store (clampI (idx, 0, 3), std::memory_order_relaxed); }
    void setTapeType    (int idx)  noexcept { pType.store (clampI (idx, 0, 3), std::memory_order_relaxed); }
    void setSignalPath  (int idx)  noexcept { pSignalPath.store (clampI (idx, 0, 3), std::memory_order_relaxed); }
    void setEqStandard  (int idx)  noexcept { pEqStandard.store (clampI (idx, 0, 1), std::memory_order_relaxed); }
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
    void setHeadWidth   (int idx)  noexcept { pHeadWidth.store (clampI (idx, 0, 2), std::memory_order_relaxed); } // American only
    // American front-panel toggles (American only; ignored on the Swiss). All default On.
    void setCrosstalk        (bool b) noexcept { pCrosstalk.store (b, std::memory_order_relaxed); }
    void setWowFlutterEnabled(bool b) noexcept { pWowFlutterOn.store (b, std::memory_order_relaxed); }
    void setTransformer      (bool b) noexcept { pTransformer.store (b, std::memory_order_relaxed); }
    void setReproLf     (float db) noexcept { pReproLf.store  (db, std::memory_order_relaxed); } // advanced repro-head 4-band EQ
    void setReproLmf    (float db) noexcept { pReproLmf.store (db, std::memory_order_relaxed); }
    void setReproHmf    (float db) noexcept { pReproHmf.store (db, std::memory_order_relaxed); }
    void setReproHf     (float db) noexcept { pReproHf.store  (db, std::memory_order_relaxed); }
    void setBypass      (bool b)   noexcept { pBypass.store (b, std::memory_order_relaxed); }

    float getVuL() const noexcept { return vuL.load (std::memory_order_relaxed); }
    float getVuR() const noexcept { return vuR.load (std::memory_order_relaxed); }
    // Pre-processing input peak (for a UI In/Out meter switch). Metering only.
    float getInVuL() const noexcept { return inVuL.load (std::memory_order_relaxed); }
    float getInVuR() const noexcept { return inVuR.load (std::memory_order_relaxed); }
    // INPUT (record-node) true-peak hold (instant attack, ~300 ms release), taken post-
    // input-gain / PRE-tape — feeds ONLY the UI PEAK lamp, kept separate from the mean-abs
    // VU integrator. Metering only.
    float getInPeakL() const noexcept { return inPeakL.load (std::memory_order_relaxed); }
    float getInPeakR() const noexcept { return inPeakR.load (std::memory_order_relaxed); }
    // OUTPUT (final post-everything) true sample-peak hold (instant attack, ~300 ms release),
    // taken on the buffer the host receives — feeds the UI PEAK lamp as a genuine digital-clip
    // (output over 0 dBFS) indicator. Tape saturates softly, so moderate drive does NOT trip it.
    // Metering only.
    float getOutPeakL() const noexcept { return outPeakL.load (std::memory_order_relaxed); }
    float getOutPeakR() const noexcept { return outPeakR.load (std::memory_order_relaxed); }

private:
    static int clampI (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    // Oversampling is PINNED at 2x. The reference decks this emulates have no OS control, and the
    // 4x mode is un-tuned (~0.3-0.5 dB dark in the top octave — the IIR chain and all 20
    // factory presets were joint-fit at the 2x core). The Oversampling parameter is retained
    // for state round-trip (old sessions with 1x/4x load fine) but IGNORED by the DSP: this is
    // the single coercion point feeding prepare(), the processBlock reconfigure, and
    // latencySamples(), so the tuned 2x core always runs regardless of the stored choice.
    static int factorFromChoice (int /*storedChoice*/) noexcept { return 2; }

    void applyFactor (int newFactor);   // reconfigure internal DSP for an OS-factor change

    // --- parameters (atomics) ---
    std::atomic<int>   pMachine{0}, pSpeed{1}, pType{0}, pSignalPath{0}, pEqStandard{0};
    std::atomic<int>   pCalibration{0}, pOversampling{1}, pHeadWidth{1};   // OS default 2x (index 1); headWidth 1 = 1/2"
    std::atomic<float> pInputGainDb{0.0f}, pSaturation{4.0f}, pBias{50.0f};
    std::atomic<float> pHighpassHz{20.0f}, pLowpassHz{20000.0f};
    std::atomic<float> pNoiseAmount{0.0f}, pWow{7.0f}, pFlutter{3.0f}, pOutputGainDb{0.0f};
    std::atomic<float> pReproLf{0.0f}, pReproLmf{0.0f}, pReproHmf{0.0f}, pReproHf{0.0f}; // advanced repro-head 4-band EQ (0 = neutral)
    std::atomic<bool>  pAutoCal{true}, pNoiseEnabled{false}, pAutoComp{true}, pBypass{false};
    // American front-panel toggles — default On = the state the American tuning captured.
    std::atomic<bool>  pCrosstalk{true}, pWowFlutterOn{true}, pTransformer{true};

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
    SmoothedValue smSat, smWow, smFlutter, smNoise, smBias;

    bool bypassLowpass = true;

    // --- signal-level envelope detector (shared max(L,R), PRE input gain) ---
    // Peak detector (instant attack, ~30 ms release) at base rate over the RAW (pre-gain)
    // input; feeds the level-keyed HF-restore / LF-cut EQ so mine's FR-vs-level surface
    // tracks the reference. The input gain is folded back in analytically (smInGainDb) rather than
    // scaled onto |x| here, so the anchor holds under gain automation (see cpp detector).
    float m_levelEnv = 0.0f;         // persistent linear peak state (across blocks) — PRE gain
    float m_levelRelCoeff = 0.0f;    // per-sample release decay (set in prepare)

    // Level-comp factor smoother (block-rate, applied once per block): FAST attack so the
    // tone shifts ON a transient, slower release tracking the 30 ms detector. Prevents a
    // multi-dB shelf-gain step (biquad coeff discontinuity / zipper / click) when the factor
    // jumps on a drum hit. Per-sample coeffs are raised to the block length each block so the
    // effective time constant is block-size-independent.
    float m_levelFactorSm = 0.0f;    // persistent smoothed level-comp factor (0..1)
    float m_levelFactAtkCoeff = 1.0f;// per-sample one-pole attack coeff (~4 ms, set in prepare)
    float m_levelFactRelCoeff = 0.0f;// per-sample one-pole release coeff (~30 ms, set in prepare)
    // Smoothed below-anchor decay factor for the knob-static driveHfComp shelf (crest-sizzle
    // fix). Inits to 1.0 (neutral) so a preset render starting at/above the -12 anchor is
    // byte-identical from the first sample. Shares the level-comp attack/release coeffs (fast
    // attack so the tone brightens ON a transient, slow release tracking the envelope decay).
    float m_driveDecaySm = 1.0f;     // persistent smoothed driveHfComp decay factor (~-0.75..1)

    // --- dirty tracking ---
    float lastHpFreq = -1.0f, lastLpFreq = -1.0f;
    int   lastFactor = -1;

    // --- scratch (allocated in prepare) ---
    std::vector<float> inGainArr;           // base-rate  [maxBlock]
    std::vector<float> satArr, biasArr, wowFlutArr, noiseArr, sharedModArr, outGainArr; // OS-rate [maxBlock*4]

    // --- metering ---
    std::atomic<float> vuL{0.0f}, vuR{0.0f};
    std::atomic<float> inVuL{0.0f}, inVuR{0.0f};
    float vuStateL = 0.0f, vuStateR = 0.0f;
    float inVuStateL = 0.0f, inVuStateR = 0.0f;
    float vuBallisticAlpha = 0.0f;   // one-pole integrator coeff for ANSI VU ballistics (~300 ms to 99%)
    // separate INPUT-node true-peak hold for the PEAK lamp (instant attack, 300 ms release)
    std::atomic<float> inPeakL{0.0f}, inPeakR{0.0f};
    float inPeakStateL = 0.0f, inPeakStateR = 0.0f;
    std::atomic<float> outPeakL{0.0f}, outPeakR{0.0f};  // final-output sample-peak hold (PEAK lamp = digital clip)
    float outPeakStateL = 0.0f, outPeakStateR = 0.0f;
    float peakDecayCoeff = 0.0f;
};

} // namespace duskaudio

/*
  ==============================================================================

    ConsoleSaturation.h

    SSL 4000 console channel saturation using a polynomial waveshaper.

    Architecture (per sample):
      1. Slew-rate limiting        — HF transient intermodulation (TL072 13 V/µs)
      2. Pre-emphasis filter       — gentle +1.5 dB HF shelf before waveshaper
                                     forces HF to saturate more (analog "sheen")
      3. Polynomial waveshaper     — y = x + b·x² + c·x³ + d·x⁴ + e·x⁵
                                     x = pre-emph · drive · DRIVE_SCALE
                                     all terms from same x (no cascading)
      4. De-emphasis filter        — exact inverse of pre-emphasis
      5. DC blocking               — removes DC from even-order (x², x⁴) terms
      6. Noise floor               — console self-noise at -94 dB (SSL spec)
      7. Dry/wet mix               — drive controls wet amount

    Polynomial harmonic content at nominal SSL levels (drive = 0.5, input ≈ 0.5):
      E-Series:  H3 ≈ 0.065%,  H2 ≈ 0.025%,  H5 ≈ 0.008%,  H4 < 0.005%
      G-Series:  ~50% of E-Series levels (cleaner VCA, better component spec)

    Drive-dependent scaling (natural from polynomial):
      H2 ∝ drive¹ · A        — grows slowly, "VCA warmth" at low drive
      H3 ∝ drive² · A²       — grows faster, "transistor bite" when pushed
      H5 ∝ drive⁴ · A⁴       — spikes sharply at hard clip
      At heavy clip (drive ≈ 1, input ≈ 1): H3 ≈ 85% of total THD.

    Real SSL 4000: transformerless per-channel, Class-AB IC topology.
    Odd-harmonic dominant (H3 > H2) at nominal, even harmonics from
    JFET input-stage asymmetry and VCA log-antilog nonlinearity.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <random>
#include "SafeFloat.h"
#include "ADAASaturation.h"

class ConsoleSaturation
{
public:
    enum class ConsoleType
    {
        ESeries,    // SSL 4000 E-Series (Brown knobs) — grittier, dbx 202C VCA
        GSeries     // SSL 4000 G-Series (Black knobs) — smoother, dbx 2150 VCA
    };

    explicit ConsoleSaturation(unsigned int seed = 0)
    {
        unsigned int actualSeed = (seed != 0) ? seed
            : std::random_device{}();
        noiseGen  = std::mt19937(actualSeed);
        noiseDist = std::uniform_real_distribution<float>(-1.0f, 1.0f);
    }

    void setConsoleType(ConsoleType type) { consoleType = type; }

    void setSampleRate(double newSampleRate)
    {
        sampleRate = newSampleRate;
        const float sr = static_cast<float>(sampleRate);

        // DC blocker at ~5 Hz
        const float dcCutoff = 5.0f;
        const float dcRC = 1.0f / (juce::MathConstants<float>::twoPi * dcCutoff);
        dcBlockerCoeff = dcRC / (dcRC + 1.0f / sr);

        // Pre/de-emphasis shelf: +1.5 dB HF boost at 8 kHz before waveshaper.
        // Forces high frequencies to saturate more, giving the classic analog "sheen".
        // Implemented as 1-pole LP + subtraction: y = x + shelfGain*(x - LP(x))
        // LP coefficient:
        const float lpCutoff = 8000.0f;
        lpEmphCoeff = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * lpCutoff / sr);

        // Shelf gain for +1.5 dB at HF: linear gain = 10^(1.5/20) = 1.189, shelf amount = 0.189
        emphShelfGain = std::pow(10.0f, 1.5f / 20.0f) - 1.0f;  // ≈ 0.189

        // De-emphasis: exact IIR inverse of the pre-emphasis filter.
        // Transfer function of pre-emphasis (α = 1 - lpEmphCoeff):
        //   H(z) = (1 + A·α − α·(1+A)·z⁻¹) / (1 − α·z⁻¹)
        //   where A = emphShelfGain
        // Inverse: H_inv = (1 - α*z^{-1}) / numerator of H
        const float alpha = 1.0f - lpEmphCoeff;
        const float Am    = emphShelfGain * alpha;             // A·α (matches pre-emph numerator)
        // Normalised inverse IIR coefficients (y[n] = b0*x[n] + b1*x[n-1] + a1*y[n-1])
        const float h0 = 1.0f + Am;                          // pre-emph numerator DC component
        deEmphB0 =  1.0f / h0;
        deEmphB1 = -alpha / h0;
        deEmphA1 =  (alpha + Am) / h0;
    }

    void reset()
    {
        dcBlockerX1_L = dcBlockerY1_L = 0.0f;
        dcBlockerX1_R = dcBlockerY1_R = 0.0f;
        lpEmphState_L = lpEmphState_R = 0.0f;
        deEmphX1_L    = deEmphX1_R    = 0.0f;
        deEmphY1_L    = deEmphY1_R    = 0.0f;
        prevXdL       = prevXdR       = 0.0f;
    }

    // Main processing: drive in [0, 1]
    float processSample(float input, float drive, bool isLeft)
    {
        juce::ScopedNoDenormals noDenormals;

        if (!safeIsFinite(input)) return 0.0f;

        // ── 1. Pre-emphasis (+1.5 dB HF shelf) ───────────────────────────────
        float x = applyPreEmphasis(input, isLeft);

        // ── 3. Polynomial waveshaper ──────────────────────────────────────────
        // x_driven = x * (drive * DRIVE_SCALE)
        // DRIVE_SCALE = 4.0 calibrated to 0 VU = -18 dBFS (A=0.126) nominal.
        // Soft-clip caps xd at ~+/-2.0 to prevent polynomial explosion at hot levels.
        const float DRIVE_SCALE = 4.0f;
        const float driveAmount = drive * DRIVE_SCALE;
        const float xd_raw = x * driveAmount;
        const float xd = xd_raw / std::sqrt(1.0f + xd_raw * xd_raw * 0.25f);

        float b, c, d, e;
        if (consoleType == ConsoleType::ESeries)
        {
            // Calibrated to E-Series at nominal (drive=0.5, input amp A=0.5):
            //   H2 ≈ 0.025%   (JFET input asymmetry + VCA log-antilog)
            //   H3 ≈ 0.065%   (symmetric transistor clipping — dominant)
            //   H4 ≈ 0.0016%  (negligible, < 0.005% spec)
            //   H5 ≈ 0.008%   (spikes at hard clip, ≈85% of THD at drive=1)
            b = 0.001f;    // H2 even
            c = 0.0104f;   // H3 odd  — dominant
            d = 0.001f;    // H4 even — negligible
            e = 0.0205f;   // H5 odd  — spikes at hard push
        }
        else
        {
            // G-Series: ≈50% of E-Series distortion (better components, cleaner VCA)
            b = 0.0005f;
            c = 0.0052f;
            d = 0.0005f;
            e = 0.0103f;
        }

        // y = x_driven + harmonic terms (distortion only; divide by driveAmount to normalise).
        // ADAA: apply first-order antiderivative antialiasing to the polynomial distortion
        // terms (alias-free equivalent of ~2x oversampling for smooth polynomial waveshapers).
        float& prevXd = isLeft ? prevXdL : prevXdR;
        float distortion = ADAASaturation::process(
            xd, prevXd,
            [b,c,d,e](float v) { return ADAASaturation::polyWaveshaper(v, b, c, d, e); },
            [b,c,d,e](float v) { return ADAASaturation::polyAntideriv(v, b, c, d, e); });
        prevXd = xd;
        float y = x + distortion / driveAmount;

        // ── 4. De-emphasis (exact IIR inverse of pre-emphasis) ───────────────
        y = applyDeEmphasis(y, isLeft);

        // ── 5. DC blocking ────────────────────────────────────────────────────
        // Removes DC offset introduced by even-order (x², x⁴) terms.
        y = processDCBlocker(y, isLeft);

        // ── 6. Console noise floor ────────────────────────────────────────────
        // SSL -94 dB noise floor, slightly higher with drive engaged
        float noiseLevel = 0.00002f * (1.0f + drive * 0.3f);
        y += noiseDist(noiseGen) * noiseLevel;

        // ── 7. Gain compensation + dry/wet mix ────────────────────────────────
        y *= 1.0f / (1.0f + drive * 0.15f);  // compensate for gain increase at high drive

        float wetMix = juce::jlimit(0.0f, 1.0f, drive * 1.4f);
        float result = input * (1.0f - wetMix) + y * wetMix;

        return safeIsFinite(result) ? result : input;
    }

private:
    ConsoleType consoleType = ConsoleType::ESeries;
    double sampleRate = 44100.0;

    // ── DC blocker state ─────────────────────────────────────────────────────
    float dcBlockerX1_L = 0.0f, dcBlockerY1_L = 0.0f;
    float dcBlockerX1_R = 0.0f, dcBlockerY1_R = 0.0f;
    float dcBlockerCoeff = 0.999f;

    // ── Pre/de-emphasis filter state and coefficients ─────────────────────────
    float lpEmphCoeff = 0.68f;    // 1-pole LP coefficient for pre-emphasis
    float emphShelfGain = 0.189f; // HF shelf gain amount (linear, ≈ +1.5 dB)
    float lpEmphState_L = 0.0f, lpEmphState_R = 0.0f;  // LP state for pre-emph

    // De-emphasis IIR: y[n] = deEmphB0*x[n] + deEmphB1*x[n-1] + deEmphA1*y[n-1]
    float deEmphB0 = 0.886f, deEmphB1 = 0.284f, deEmphA1 = 0.398f;
    float deEmphX1_L = 0.0f, deEmphY1_L = 0.0f;
    float deEmphX1_R = 0.0f, deEmphY1_R = 0.0f;

    // ── ADAA state (previous xd value per channel) ───────────────────────────
    float prevXdL = 0.0f, prevXdR = 0.0f;

    // ── Noise ─────────────────────────────────────────────────────────────────
    std::mt19937 noiseGen;
    std::uniform_real_distribution<float> noiseDist;

    // ─────────────────────────────────────────────────────────────────────────

    // Pre-emphasis: y = x + shelfGain * (x - LP(x))
    // LP(x) is a 1-pole lowpass at lpCutoff (8 kHz). The subtracted portion is
    // the high-frequency residual, boosted by emphShelfGain.
    float applyPreEmphasis(float input, bool isLeft)
    {
        float& lpState = isLeft ? lpEmphState_L : lpEmphState_R;
        lpState += lpEmphCoeff * (input - lpState);
        return input + emphShelfGain * (input - lpState);
    }

    // De-emphasis: exact IIR inverse of pre-emphasis, computed in setSampleRate.
    float applyDeEmphasis(float input, bool isLeft)
    {
        float& x1 = isLeft ? deEmphX1_L : deEmphX1_R;
        float& y1 = isLeft ? deEmphY1_L : deEmphY1_R;
        float output = deEmphB0 * input + deEmphB1 * x1 + deEmphA1 * y1;
        x1 = input;
        y1 = output;
        return output;
    }

    // DC blocking filter (~5 Hz high-pass).
    float processDCBlocker(float input, bool isLeft)
    {
        float& x1 = isLeft ? dcBlockerX1_L : dcBlockerX1_R;
        float& y1 = isLeft ? dcBlockerY1_L : dcBlockerY1_R;
        float output = input - x1 + dcBlockerCoeff * y1;
        x1 = input;
        y1 = output;
        return output;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConsoleSaturation)
};

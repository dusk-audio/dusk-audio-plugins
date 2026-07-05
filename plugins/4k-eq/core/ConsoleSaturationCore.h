// ConsoleSaturationCore.h — framework-free port of Multi-Q's British-mode
// ConsoleSaturation (the up-to-date console saturator, replacing the older
// piecewise Jiles-Atherton model the standalone 4K EQ shipped with).
//
// Polynomial waveshaper with first-order ADAA, per-sample:
//   1. pre-emphasis  (+1.5 dB HF shelf @ 8 kHz — HF saturates more, "sheen")
//   2. drive + soft-clip cap, then y = x + (b·x²+c·x³+d·x⁴+e·x⁵) via ADAA
//        E-series (Brown): H2-dominant, warm/gritty
//        G-series (Black): H3-dominant, ~60% THD, smoother
//   3. de-emphasis   (exact IIR inverse of pre-emphasis)
//   4. DC block, console noise floor, gain-comp + drive-scaled dry/wet mix
//
// De-JUCE of plugins/multi-q/ConsoleSaturation.h: math byte-identical; the
// three JUCE touch-points (twoPi, jlimit, ScopedNoDenormals) are swapped and
// the noise seed is FIXED so offline renders reproduce (the ±5% instance
// variation of the old model is gone in this design). safeIsFinite -> std::isfinite.

#pragma once

#include <cmath>
#include <random>
#include "../../shared-dpf/dsp/DuskFilters.hpp"  // kDuskTwoPi
#include "../../shared-dpf/dsp/DuskADAA.hpp"

namespace duskaudio
{

class ConsoleSaturationCore
{
public:
    enum class ConsoleType { ESeries, GSeries };

    ConsoleSaturationCore()
    {
        noiseGen  = std::mt19937(0x4E4F4953u); // "NOIS" — fixed for reproducible renders
        noiseDist = std::uniform_real_distribution<float>(-1.0f, 1.0f);
    }

    void setConsoleType(ConsoleType type) { consoleType = type; }

    void setSampleRate(double newSampleRate)
    {
        sampleRate = newSampleRate;
        const float sr = static_cast<float>(sampleRate);

        const float dcCutoff = 5.0f;
        const float dcRC = 1.0f / (kDuskTwoPi * dcCutoff);
        dcBlockerCoeff = dcRC / (dcRC + 1.0f / sr);

        const float lpCutoff = 8000.0f;
        lpEmphCoeff = 1.0f - std::exp(-kDuskTwoPi * lpCutoff / sr);
        emphShelfGain = std::pow(10.0f, 1.5f / 20.0f) - 1.0f; // +1.5 dB shelf amount

        const float alpha = 1.0f - lpEmphCoeff;
        const float Am    = emphShelfGain * alpha;
        const float h0    = 1.0f + Am;
        deEmphB0 =  1.0f / h0;
        deEmphB1 = -alpha / h0;
        deEmphA1 =  (alpha + Am) / h0;
    }

    void reset() { resetChannel(true); resetChannel(false); }

    void resetChannel(bool isLeft)
    {
        if (isLeft) { dcBlockerX1_L = dcBlockerY1_L = 0.0f; lpEmphState_L = 0.0f; deEmphX1_L = deEmphY1_L = 0.0f; prevXdL = 0.0f; }
        else        { dcBlockerX1_R = dcBlockerY1_R = 0.0f; lpEmphState_R = 0.0f; deEmphX1_R = deEmphY1_R = 0.0f; prevXdR = 0.0f; }
    }

    // drive in [0, 1].
    float processSample(float input, float drive, bool isLeft)
    {
        if (!std::isfinite(input)) return 0.0f;

        float x = applyPreEmphasis(input, isLeft);

        const float DRIVE_SCALE = 4.0f;
        const float driveAmount = drive * DRIVE_SCALE;
        const float xd_raw = x * driveAmount;
        const float xd = xd_raw / std::sqrt(1.0f + xd_raw * xd_raw * 0.25f);

        float b, c, d, e;
        if (consoleType == ConsoleType::ESeries) { b = 0.012f; c = 0.004f; d = 0.001f;  e = 0.001f; }
        else                                     { b = 0.003f; c = 0.008f; d = 0.0005f; e = 0.001f; }

        float& prevXd = isLeft ? prevXdL : prevXdR;
        const float distortion = adaa::process(
            xd, prevXd,
            [b, c, d, e](float v) { return adaa::polyWaveshaper(v, b, c, d, e); },
            [b, c, d, e](float v) { return adaa::polyAntideriv(v, b, c, d, e); });
        prevXd = xd;
        float y = x + (driveAmount > 1e-9f ? distortion / driveAmount : 0.0f);

        y = applyDeEmphasis(y, isLeft);
        y = processDCBlocker(y, isLeft);

        const float noiseLevel = 0.00002f * (1.0f + drive * 0.3f);
        y += noiseDist(noiseGen) * noiseLevel;

        y *= 1.0f / (1.0f + drive * 0.15f);
        const float wetMix = clampf(drive * 1.4f, 0.0f, 1.0f);
        const float result = input * (1.0f - wetMix) + y * wetMix;

        if (!std::isfinite(result)) { resetChannel(isLeft); return 0.0f; }
        return result;
    }

private:
    static float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    ConsoleType consoleType = ConsoleType::ESeries;
    double sampleRate = 44100.0;

    float dcBlockerX1_L = 0.0f, dcBlockerY1_L = 0.0f;
    float dcBlockerX1_R = 0.0f, dcBlockerY1_R = 0.0f;
    float dcBlockerCoeff = 0.999f;

    float lpEmphCoeff = 0.68f;
    float emphShelfGain = 0.189f;
    float lpEmphState_L = 0.0f, lpEmphState_R = 0.0f;

    float deEmphB0 = 0.886f, deEmphB1 = 0.284f, deEmphA1 = 0.398f;
    float deEmphX1_L = 0.0f, deEmphY1_L = 0.0f;
    float deEmphX1_R = 0.0f, deEmphY1_R = 0.0f;

    float prevXdL = 0.0f, prevXdR = 0.0f;

    std::mt19937 noiseGen;
    std::uniform_real_distribution<float> noiseDist;

    float applyPreEmphasis(float input, bool isLeft)
    {
        float& lpState = isLeft ? lpEmphState_L : lpEmphState_R;
        lpState += lpEmphCoeff * (input - lpState);
        return input + emphShelfGain * (input - lpState);
    }

    float applyDeEmphasis(float input, bool isLeft)
    {
        float& x1 = isLeft ? deEmphX1_L : deEmphX1_R;
        float& y1 = isLeft ? deEmphY1_L : deEmphY1_R;
        const float output = deEmphB0 * input + deEmphB1 * x1 + deEmphA1 * y1;
        x1 = input; y1 = output;
        return output;
    }

    float processDCBlocker(float input, bool isLeft)
    {
        float& x1 = isLeft ? dcBlockerX1_L : dcBlockerX1_R;
        float& y1 = isLeft ? dcBlockerY1_L : dcBlockerY1_R;
        const float output = input - x1 + dcBlockerCoeff * y1;
        x1 = input; y1 = output;
        return output;
    }

    ConsoleSaturationCore(const ConsoleSaturationCore&) = delete;
    ConsoleSaturationCore& operator=(const ConsoleSaturationCore&) = delete;
};

} // namespace duskaudio

// ConsoleSaturationCore.h — framework-free port of ConsoleSaturation.h.
//
// British console harmonic emulation (two voicings, E-Series warm/2nd-harmonic
// and G-Series clean/3rd-harmonic). This is a straight de-JUCE of the JUCE
// plugins/4k-eq/ConsoleSaturation.h: the saturation math is byte-for-byte the
// same; only three JUCE touch-points are replaced:
//   juce::MathConstants<float>::twoPi -> duskaudio::kDuskTwoPi
//   juce::jlimit(lo,hi,x)             -> clampf(x,lo,hi)
//   juce::ScopedNoDenormals           -> removed (the DSP core sets FTZ/DAZ
//                                        once per block via ScopedFlushDenormals)
// The per-instance component tolerances use a FIXED seed here (JUCE used
// std::random_device) so offline A/B renders are reproducible; the ±5% analog
// variation is preserved, it is just deterministic per build.

#pragma once

#include <cmath>
#include <random>
#include "../../shared-dpf/dsp/DuskFilters.hpp" // kDuskTwoPi

namespace duskaudio
{

class ConsoleSaturationCore
{
public:
    enum class ConsoleType { ESeries, GSeries };

    ConsoleSaturationCore()
    {
        // Deterministic per-instance tolerance draw (fixed seed => reproducible).
        std::mt19937 gen(0x4B455121u); // "KEQ!"
        std::uniform_real_distribution<float> dist(-0.05f, 0.05f);
        transformerTolerance       = 1.0f + dist(gen);
        opAmpTolerance             = 1.0f + dist(gen);
        outputTransformerTolerance = 1.0f + dist(gen);
        noiseGen  = std::mt19937(0x4E4F4953u); // "NOIS"
        noiseDist = std::uniform_real_distribution<float>(-1.0f, 1.0f);
    }

    void setConsoleType(ConsoleType type) { consoleType = type; }

    void setSampleRate(double newSampleRate)
    {
        sampleRate = newSampleRate;
        const float cutoffFreq = 5.0f;
        const float RC = 1.0f / (kDuskTwoPi * cutoffFreq);
        dcBlockerCoeff = RC / (RC + 1.0f / static_cast<float>(sampleRate));
    }

    void reset()
    {
        dcBlockerX1_L = dcBlockerY1_L = 0.0f;
        dcBlockerX1_R = dcBlockerY1_R = 0.0f;
        lastSample_L = lastSample_R = 0.0f;
        highFreqEstimate_L = highFreqEstimate_R = 0.0f;
    }

    float processSample(float input, float drive, bool isLeftChannel)
    {
        if (drive < 0.001f)
            return input;

        float limited = input;
        float absInput = std::abs(input);
        if (absInput > 0.95f)
        {
            float excess = absInput - 0.95f;
            float compressed = 0.95f + std::tanh(excess * 3.0f) * 0.05f;
            limited = (input > 0.0f) ? compressed : -compressed;
        }

        float highFreqContent = estimateHighFrequencyContent(limited, isLeftChannel);
        float hfReduction = highFreqContent * (0.25f + drive * 0.35f);
        float effectiveDrive = drive * (1.0f - hfReduction);

        float transformed = processInputTransformer(limited, effectiveDrive * transformerTolerance);
        float opAmpOut = processOpAmpStage(transformed, effectiveDrive * opAmpTolerance);

        float output = (consoleType == ConsoleType::ESeries)
            ? processOutputTransformer(opAmpOut, drive * 0.7f * outputTransformerTolerance)
            : opAmpOut;

        float noiseLevel = 0.00003162f * (1.0f + drive * 0.5f); // -90dB base
        output += noiseDist(noiseGen) * noiseLevel;

        output = processDCBlocker(output, isLeftChannel);

        float wetMix = clampf(drive * 1.4f, 0.0f, 1.0f);
        return input * (1.0f - wetMix) + output * wetMix;
    }

private:
    static float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    ConsoleType consoleType = ConsoleType::ESeries;
    double sampleRate = 44100.0;

    float dcBlockerX1_L = 0.0f, dcBlockerY1_L = 0.0f;
    float dcBlockerX1_R = 0.0f, dcBlockerY1_R = 0.0f;
    float dcBlockerCoeff = 0.999f;

    float lastSample_L = 0.0f, lastSample_R = 0.0f;
    float highFreqEstimate_L = 0.0f, highFreqEstimate_R = 0.0f;
    float highFreqScale = 3.0f;

    float transformerTolerance = 1.0f;
    float opAmpTolerance = 1.0f;
    float outputTransformerTolerance = 1.0f;

    std::mt19937 noiseGen;
    std::uniform_real_distribution<float> noiseDist;

    float estimateHighFrequencyContent(float input, bool isLeftChannel)
    {
        float& lastSample = isLeftChannel ? lastSample_L : lastSample_R;
        float& estimate = isLeftChannel ? highFreqEstimate_L : highFreqEstimate_R;
        float difference = std::abs(input - lastSample);
        lastSample = input;
        const float smoothing = 0.95f;
        estimate = estimate * smoothing + difference * (1.0f - smoothing);
        return clampf(estimate * highFreqScale, 0.0f, 1.0f);
    }

    float processInputTransformer(float input, float drive)
    {
        const float transformerDrive = 1.0f + drive * 7.0f;
        float driven = input * transformerDrive;
        float abs_x = std::abs(driven);

        float saturated;
        if (abs_x < 0.9f)
            saturated = driven;
        else if (abs_x < 1.5f)
        {
            float excess = abs_x - 0.9f;
            float compressed = 0.9f + excess * (1.0f - excess * 0.15f);
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }
        else
        {
            float excess = abs_x - 1.5f;
            float compressed = 1.5f + std::tanh(excess * 1.5f) * 0.3f;
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }

        float threshold = (consoleType == ConsoleType::ESeries) ? 0.6f : 0.05f;
        if (abs_x > threshold)
        {
            float saturationAmount = (abs_x - threshold) / (1.2f - threshold);
            saturationAmount = clampf(saturationAmount, 0.0f, 1.0f);
            if (consoleType == ConsoleType::ESeries)
                saturated += saturated * saturated * (0.12f * saturationAmount);
            else
            {
                saturated += saturated * saturated * (0.025f * saturationAmount);
                saturated += saturated * saturated * saturated * (0.050f * saturationAmount);
            }
        }
        return saturated / transformerDrive;
    }

    float processOpAmpStage(float input, float drive)
    {
        const float opAmpDrive = 1.0f + drive * 9.0f;
        float driven = input * opAmpDrive;
        float output;

        if (driven > 0.0f)
        {
            if (driven < 1.0f)
                output = driven;
            else if (driven < 1.8f)
            {
                float excess = driven - 1.0f;
                output = 1.0f + excess * (1.0f - excess * 0.2f);
            }
            else
            {
                float clipHardness = (consoleType == ConsoleType::ESeries) ? 1.5f : 2.0f;
                output = 1.5f + std::tanh((driven - 1.8f) * clipHardness) * 0.3f;
            }
        }
        else
        {
            if (driven > -1.0f)
                output = driven;
            else if (driven > -1.9f)
            {
                float excess = -driven - 1.0f;
                output = -1.0f - excess * (1.0f - excess * 0.18f);
            }
            else
            {
                float clipHardness = (consoleType == ConsoleType::ESeries) ? 1.5f : 2.0f;
                output = -1.55f + std::tanh((driven + 1.9f) * clipHardness) * 0.3f;
            }
        }

        float threshold = (consoleType == ConsoleType::ESeries) ? 0.6f : 0.05f;
        if (std::abs(driven) > threshold)
        {
            float saturationAmount = (std::abs(driven) - threshold) / (1.5f - threshold);
            saturationAmount = clampf(saturationAmount, 0.0f, 1.0f);
            if (consoleType == ConsoleType::ESeries)
                output += output * output * std::copysign(0.10f * saturationAmount, output);
            else
            {
                output += output * output * std::copysign(0.022f * saturationAmount, output);
                output += output * output * output * (0.040f * saturationAmount);
            }
        }
        return output / opAmpDrive;
    }

    float processOutputTransformer(float input, float drive)
    {
        const float transformerDrive = 1.0f + drive * 2.0f;
        float driven = input * transformerDrive;
        float abs_x = std::abs(driven);

        float saturated;
        if (abs_x < 0.5f)
            saturated = driven;
        else if (abs_x < 0.9f)
        {
            float excess = abs_x - 0.5f;
            float compressed = 0.5f + excess * (1.0f - excess * 0.25f);
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }
        else
        {
            float excess = abs_x - 0.9f;
            float compressed = 0.9f + std::tanh(excess * 1.5f) * 0.15f;
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }
        saturated += saturated * std::abs(saturated) * 0.05f;
        return saturated / transformerDrive;
    }

    float processDCBlocker(float input, bool isLeftChannel)
    {
        float& x1 = isLeftChannel ? dcBlockerX1_L : dcBlockerX1_R;
        float& y1 = isLeftChannel ? dcBlockerY1_L : dcBlockerY1_R;
        float output = input - x1 + dcBlockerCoeff * y1;
        x1 = input;
        y1 = output;
        return output;
    }

    ConsoleSaturationCore(const ConsoleSaturationCore&) = delete;
    ConsoleSaturationCore& operator=(const ConsoleSaturationCore&) = delete;
};

} // namespace duskaudio

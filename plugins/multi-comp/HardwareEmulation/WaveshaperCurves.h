// WaveshaperCurves.h — Lookup table waveshapers for hardware saturation curves

#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace HardwareEmulation {

class WaveshaperCurves
{
public:
    static constexpr int TABLE_SIZE = 4096;
    static constexpr float TABLE_RANGE = 4.0f;  // Input range: -2 to +2

    enum class CurveType
    {
        Opto_Tube,      // Asymmetric tube saturation
        FET,       // FET transistor clipping
        Classic_VCA,        // Clean VCA saturation
        Console_Bus,    // Console bus character
        Transformer,    // Generic transformer saturation
        Linear          // Bypass (no saturation)
    };

    WaveshaperCurves()
    {
        initialize();
    }

    void initialize()
    {
        initializeOptoCurve();
        initializeFETCurve();
        initializeVCACurve();
        initializeConsoleCurve();
        initializeTransformerCurve();
        initializeLinearCurve();
    }

    // Process a single sample through the waveshaper
    // Input should be normalized (-2 to +2 range for full curve access)
    float process(float input, CurveType curve) const
    {
        // Map input to table index
        float normalized = (input + TABLE_RANGE / 2.0f) / TABLE_RANGE;
        normalized = std::clamp(normalized, 0.0f, 0.9999f);

        float indexFloat = normalized * (TABLE_SIZE - 1);
        int index0 = static_cast<int>(indexFloat);
        int index1 = std::min(index0 + 1, TABLE_SIZE - 1);
        float frac = indexFloat - static_cast<float>(index0);

        const auto& table = getTable(curve);
        return table[index0] * (1.0f - frac) + table[index1] * frac;
    }

    // Process with drive amount (0 = bypass, 1 = full saturation)
    float processWithDrive(float input, CurveType curve, float drive) const
    {
        drive = std::clamp(drive, 0.0f, 1.0f);
        if (drive <= 0.0f)
            return input;

        float saturated = process(input, curve);
        return input + (saturated - input) * drive;
    }
    // Get raw table for direct access (advanced use)
    const std::array<float, TABLE_SIZE>& getTable(CurveType curve) const
    {
        switch (curve)
        {
            case CurveType::Opto_Tube:   return optoCurve;
            case CurveType::FET:    return fetCurve;
            case CurveType::Classic_VCA:     return vcaCurve;
            case CurveType::Console_Bus: return consoleCurve;
            case CurveType::Transformer: return transformerCurve;
            case CurveType::Linear:
            default:                     return linearCurve;
        }
    }

private:
    std::array<float, TABLE_SIZE> optoCurve;
    std::array<float, TABLE_SIZE> fetCurve;
    std::array<float, TABLE_SIZE> vcaCurve;
    std::array<float, TABLE_SIZE> consoleCurve;
    std::array<float, TABLE_SIZE> transformerCurve;
    std::array<float, TABLE_SIZE> linearCurve;

    // Convert table index to input value (-2 to +2)
    static float indexToInput(int index)
    {
        return (static_cast<float>(index) / (TABLE_SIZE - 1)) * TABLE_RANGE - TABLE_RANGE / 2.0f;
    }

    //--------------------------------------------------------------------------
    // Opto tube — asymmetric, 2nd harmonic dominant
    void initializeOptoCurve()
    {
        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);

            if (x >= 0.0f)
            {
                float softClip = x / (1.0f + x * 0.12f);
                float harmonic2 = softClip * softClip * 0.025f;
                optoCurve[i] = softClip - harmonic2;
            }
            else
            {
                float absX = std::abs(x);
                float hardClip = -absX / (1.0f + absX * 0.08f);
                optoCurve[i] = hardClip;
            }
        }
    }

    //--------------------------------------------------------------------------
    // FET — symmetric, odd harmonics (3rd + 5th)
    void initializeFETCurve()
    {
        constexpr float threshold = 1.0f;
        constexpr float h3Coeff = 0.18f;
        constexpr float h5Coeff = 0.04f;
        constexpr float shapedAtThreshold = threshold + (threshold * threshold * threshold) * h3Coeff
                                          + (threshold * threshold * threshold * threshold * threshold) * h5Coeff;

        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);
            float absX = std::abs(x);
            float sign = (x >= 0.0f) ? 1.0f : -1.0f;

            float x3 = x * x * x;
            float x5 = x3 * x * x;
            float shaped = x + x3 * h3Coeff + x5 * h5Coeff;

            if (absX > threshold)
            {
                float excess = absX - threshold;
                float limit = shapedAtThreshold + std::tanh(excess * 1.5f) * 0.15f;
                shaped = sign * limit;
            }

            fetCurve[i] = shaped;
        }
    }

    //--------------------------------------------------------------------------
    // VCA — nearly linear, gentle limiting at extremes
    void initializeVCACurve()
    {
        constexpr float threshold = 1.5f;
        constexpr float h3Coeff = 0.018f;
        constexpr float shapedAtThreshold = threshold + (threshold * threshold * threshold) * h3Coeff;

        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);
            float absX = std::abs(x);
            float sign = (x >= 0.0f) ? 1.0f : -1.0f;

            if (absX < threshold)
            {
                vcaCurve[i] = x + x * x * x * h3Coeff;
            }
            else
            {
                float excess = absX - threshold;
                float sat = shapedAtThreshold + std::tanh(excess * 0.3f) * 0.14f;
                vcaCurve[i] = sign * sat;
            }
        }
    }

    //--------------------------------------------------------------------------
    // Console bus — asymmetric thresholds for punch
    void initializeConsoleCurve()
    {
        constexpr float thresholdPos = 0.92f;
        constexpr float thresholdNeg = 0.88f;
        constexpr float h3Coeff = 0.02f;
        constexpr float shapedAtThresholdPos = thresholdPos + (thresholdPos * thresholdPos * thresholdPos) * h3Coeff;
        constexpr float shapedAtThresholdNeg = thresholdNeg + (thresholdNeg * thresholdNeg * thresholdNeg) * h3Coeff;

        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);
            float absX = std::abs(x);
            float sign = (x >= 0.0f) ? 1.0f : -1.0f;

            float threshold = (x >= 0.0f) ? thresholdPos : thresholdNeg;
            float shapedAtThreshold = (x >= 0.0f) ? shapedAtThresholdPos : shapedAtThresholdNeg;

            if (absX < threshold)
            {
                consoleCurve[i] = x + x * x * x * h3Coeff;
            }
            else
            {
                float excess = absX - threshold;
                float sat = shapedAtThreshold + std::tanh(excess * 3.5f) * 0.18f;
                consoleCurve[i] = sign * sat;
            }
        }
    }

    //--------------------------------------------------------------------------
    // Transformer — progressive compression, 2nd harmonic emphasis
    void initializeTransformerCurve()
    {
        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);
            float absX = std::abs(x);
            float sign = (x >= 0.0f) ? 1.0f : -1.0f;

            if (absX < 0.7f)
            {
                // x * |x| gives asymmetric transfer → 2nd harmonic
                float harmonic2 = x * absX * 0.05f;
                transformerCurve[i] = x + harmonic2;
            }
            else if (absX < 1.2f)
            {
                float excess = absX - 0.7f;
                float compressed = 0.7f + excess * (1.0f - excess * 0.25f);
                float harmonic2 = (sign * compressed) * compressed * 0.08f;
                transformerCurve[i] = sign * compressed + harmonic2;
            }
            else
            {
                float excess = absX - 1.2f;
                float hard = 1.05f + std::tanh(excess * 1.5f) * 0.15f;
                transformerCurve[i] = sign * hard;
            }
        }
    }

    //--------------------------------------------------------------------------
    // Linear (bypass)
    void initializeLinearCurve()
    {
        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            linearCurve[i] = indexToInput(i);
        }
    }
};

// First call initializes lookup tables — call from prepareToPlay, not the audio thread.
inline WaveshaperCurves& getWaveshaperCurves()
{
    static WaveshaperCurves instance;
    return instance;
}

} // namespace HardwareEmulation

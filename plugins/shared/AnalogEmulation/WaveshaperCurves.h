/*
  ==============================================================================

    WaveshaperCurves.h
    Lookup table-based waveshapers for hardware-accurate saturation

    Pre-computed curves based on measured hardware transfer functions:
    - LA-2A tube saturation (asymmetric, 2nd harmonic dominant)
    - 1176 FET saturation (symmetric, odd harmonics)
    - DBX VCA saturation (nearly linear)
    - SSL Bus saturation (punchy, slight asymmetry)
    - Generic transformer saturation

    This is the shared library version - all plugins should use this.

  ==============================================================================
*/

#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace AnalogEmulation {

class WaveshaperCurves
{
public:
    static constexpr int TABLE_SIZE = 4096;
    static constexpr float TABLE_RANGE = 4.0f;  // Input range: -2 to +2

    enum class CurveType
    {
        LA2A_Tube,      // Asymmetric tube saturation
        FET_1176,       // FET transistor clipping
        DBX_VCA,        // Clean VCA saturation
        SSL_Bus,        // SSL console character
        Transformer,    // Generic transformer saturation
        Tape,           // Tape saturation (similar to LA-2A but smoother)
        Triode,         // Generic triode tube saturation
        Pentode,        // Pentode tube saturation (more aggressive)
        Linear          // Bypass (no saturation)
    };

    WaveshaperCurves()
    {
        initialize();
    }

    void initialize()
    {
        initializeLA2ACurve();
        initializeFETCurve();
        initializeVCACurve();
        initializeSSLCurve();
        initializeTransformerCurve();
        initializeTapeCurve();
        initializeTriodeCurve();
        initializePentodeCurve();
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
            case CurveType::LA2A_Tube:   return la2aCurve;
            case CurveType::FET_1176:    return fetCurve;
            case CurveType::DBX_VCA:     return vcaCurve;
            case CurveType::SSL_Bus:     return sslCurve;
            case CurveType::Transformer: return transformerCurve;
            case CurveType::Tape:        return tapeCurve;
            case CurveType::Triode:      return triodeCurve;
            case CurveType::Pentode:     return pentodeCurve;
            case CurveType::Linear:
            default:                     return linearCurve;
        }
    }

private:
    std::array<float, TABLE_SIZE> la2aCurve;
    std::array<float, TABLE_SIZE> fetCurve;
    std::array<float, TABLE_SIZE> vcaCurve;
    std::array<float, TABLE_SIZE> sslCurve;
    std::array<float, TABLE_SIZE> transformerCurve;
    std::array<float, TABLE_SIZE> tapeCurve;
    std::array<float, TABLE_SIZE> triodeCurve;
    std::array<float, TABLE_SIZE> pentodeCurve;
    std::array<float, TABLE_SIZE> linearCurve;

    // Convert table index to input value (-2 to +2)
    static float indexToInput(int index)
    {
        return (static_cast<float>(index) / (TABLE_SIZE - 1)) * TABLE_RANGE - TABLE_RANGE / 2.0f;
    }

    //--------------------------------------------------------------------------
    // LA-2A tube saturation
    // Characteristics: Asymmetric, 2nd harmonic dominant, soft compression
    // Based on 12AX7 triode transfer curve measurements
    // Target: ~0.25-0.5% THD at +10dBm, 2nd harmonic dominant
    void initializeLA2ACurve()
    {
        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);

            if (x >= 0.0f)
            {
                // Positive half: Softer saturation (grid current region)
                float softClip = x / (1.0f + x * 0.12f);
                float harmonic2 = softClip * softClip * 0.025f;
                la2aCurve[i] = softClip - harmonic2;
            }
            else
            {
                // Negative half: Slightly harder clipping (cutoff region)
                float absX = std::abs(x);
                float hardClip = -absX / (1.0f + absX * 0.08f);
                la2aCurve[i] = hardClip;
            }
        }
    }

    //--------------------------------------------------------------------------
    // 1176 FET saturation
    // Characteristics: More symmetric, odd harmonics, sharp knee
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
            float harmonic3 = x3 * h3Coeff;
            float harmonic5 = x5 * h5Coeff;

            float shaped = x + harmonic3 + harmonic5;

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
    // DBX 160 VCA saturation
    // Characteristics: Very clean, nearly linear, gentle limiting only at extremes
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
                float harmonic3 = x * x * x * h3Coeff;
                vcaCurve[i] = x + harmonic3;
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
    // SSL Bus saturation
    // Characteristics: Punchy, console character, slight asymmetry for "punch"
    void initializeSSLCurve()
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
                float subtle = x + x * x * x * h3Coeff;
                sslCurve[i] = subtle;
            }
            else
            {
                float excess = absX - threshold;
                float sat = shapedAtThreshold + std::tanh(excess * 3.5f) * 0.18f;
                sslCurve[i] = sign * sat;
            }
        }
    }

    //--------------------------------------------------------------------------
    // Generic transformer saturation
    // Characteristics: Progressive compression, 2nd harmonic emphasis
    void initializeTransformerCurve()
    {
        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);
            float absX = std::abs(x);
            float sign = (x >= 0.0f) ? 1.0f : -1.0f;

            if (absX < 0.7f)
            {
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
    // Tape saturation
    // Characteristics: Smooth, warm, subtle hysteresis-like behavior
    void initializeTapeCurve()
    {
        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);
            float absX = std::abs(x);
            float sign = (x >= 0.0f) ? 1.0f : -1.0f;

            // Tape has very smooth saturation with gradual onset
            // Slightly asymmetric for warmth
            if (x >= 0.0f)
            {
                // Positive: softer compression (recording head behavior)
                float softClip = x / (1.0f + x * 0.15f);
                float harmonic2 = softClip * softClip * 0.02f;
                tapeCurve[i] = softClip + harmonic2;
            }
            else
            {
                // Negative: slightly harder (playback head)
                float softClip = x / (1.0f + absX * 0.12f);
                tapeCurve[i] = softClip;
            }
        }
    }

    //--------------------------------------------------------------------------
    // Triode tube saturation
    // Characteristics: Classic tube warmth, asymmetric, 2nd harmonic dominant
    void initializeTriodeCurve()
    {
        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);

            if (x >= 0.0f)
            {
                // Positive grid: grid current causes soft compression
                float normalized = x / (1.0f + x * 0.4f);
                triodeCurve[i] = normalized * (1.0f - normalized * 0.12f);
            }
            else
            {
                float absX = std::abs(x);
                if (absX < 0.8f)
                {
                    triodeCurve[i] = x;
                }
                else if (absX < 1.5f)
                {
                    float excess = absX - 0.8f;
                    float compressed = 0.8f + excess * (1.0f - excess * 0.5f);
                    triodeCurve[i] = -compressed;
                }
                else
                {
                    float excess = absX - 1.5f;
                    float clipped = 1.15f + std::tanh(excess * 2.0f) * 0.2f;
                    triodeCurve[i] = -clipped;
                }
            }
        }
    }

    //--------------------------------------------------------------------------
    // Pentode tube saturation
    // Characteristics: More aggressive, odd harmonics, sharper knee
    void initializePentodeCurve()
    {
        for (int i = 0; i < TABLE_SIZE; ++i)
        {
            float x = indexToInput(i);
            float absX = std::abs(x);
            float sign = (x >= 0.0f) ? 1.0f : -1.0f;

            // Pentodes have sharper cutoff and more odd harmonics
            if (absX < 0.6f)
            {
                // Linear region with subtle 3rd harmonic
                float h3 = x * x * x * 0.03f;
                pentodeCurve[i] = x + h3;
            }
            else if (absX < 1.0f)
            {
                // Transition region
                float excess = absX - 0.6f;
                float compressed = 0.6f + excess * (1.0f - excess * 0.4f);
                float h3 = (sign * compressed) * compressed * compressed * 0.05f;
                pentodeCurve[i] = sign * compressed + h3;
            }
            else
            {
                // Hard limiting (screen grid saturation)
                float excess = absX - 1.0f;
                float hard = 0.92f + std::tanh(excess * 3.0f) * 0.15f;
                pentodeCurve[i] = sign * hard;
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

//==============================================================================
// Singleton accessor for shared waveshaper instance
//
// WARNING: First call initializes lookup tables (~144KB for 9 tables).
// To avoid blocking an audio/RT thread, call this function once during plugin
// initialization (e.g., in prepareToPlay or constructor) before any RT processing.
//
// Example: auto& curves = AnalogEmulation::getWaveshaperCurves(); // Force init
inline WaveshaperCurves& getWaveshaperCurves()
{
    static WaveshaperCurves instance;
    return instance;
}

} // namespace AnalogEmulation

#pragma once

#include <cmath>
#include <algorithm>

//==============================================================================
/**
    K-System Meter (Bob Katz)

    Provides K-12, K-14, and K-20 metering scales.
    - K-12: Broadcast/web (-12 dBFS = 0 VU, 12 dB headroom)
    - K-14: Pop/rock music (-14 dBFS = 0 VU, 14 dB headroom)
    - K-20: Classical/film (-20 dBFS = 0 VU, 20 dB headroom)

    Uses RMS with 300ms VU-style integration.
*/
class KSystemMeter
{
public:
    enum class Type
    {
        K12 = 0,
        K14,
        K20
    };

    KSystemMeter() = default;

    void prepare(double sampleRate)
    {
        this->sampleRate = sampleRate;

        // VU-standard 300ms integration
        // Time constant for exponential averaging
        float integrationTimeSec = 0.3f;
        float samplesForIntegration = static_cast<float>(sampleRate) * integrationTimeSec;
        decayCoeff = 1.0f - (1.0f / samplesForIntegration);

        reset();
    }

    void reset()
    {
        rmsAccumulatorL = 0.0f;
        rmsAccumulatorR = 0.0f;
        peakHoldL = 0.0f;
        peakHoldR = 0.0f;
    }

    void setType(Type type) { currentType = type; }
    Type getType() const { return currentType; }

    //==========================================================================
    // Process audio block
    void process(const float* left, const float* right, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float L = left[i];
            float R = right[i];

            // Exponential RMS averaging
            rmsAccumulatorL = rmsAccumulatorL * decayCoeff + (L * L) * (1.0f - decayCoeff);
            rmsAccumulatorR = rmsAccumulatorR * decayCoeff + (R * R) * (1.0f - decayCoeff);
        }

        // Update peak hold
        float currentRmsL = std::sqrt(rmsAccumulatorL);
        float currentRmsR = std::sqrt(rmsAccumulatorR);

        if (currentRmsL > peakHoldL) peakHoldL = currentRmsL;
        if (currentRmsR > peakHoldR) peakHoldR = currentRmsR;
    }

    //==========================================================================
    // Get K-system level (with offset applied)
    float getKLevelL() const { return linearToKLevel(std::sqrt(rmsAccumulatorL)); }
    float getKLevelR() const { return linearToKLevel(std::sqrt(rmsAccumulatorR)); }

    // Get average (mono) K level
    float getKLevelMono() const
    {
        float monoRms = std::sqrt((rmsAccumulatorL + rmsAccumulatorR) * 0.5f);
        return linearToKLevel(monoRms);
    }

    // Get raw RMS in dB (without K offset)
    float getRmsDbL() const { return linearToDb(std::sqrt(rmsAccumulatorL)); }
    float getRmsDbR() const { return linearToDb(std::sqrt(rmsAccumulatorR)); }

    // Get peak hold in K-system level
    float getPeakHoldL() const { return linearToKLevel(peakHoldL); }
    float getPeakHoldR() const { return linearToKLevel(peakHoldR); }

    // Reset peak hold
    void resetPeakHold()
    {
        peakHoldL = 0.0f;
        peakHoldR = 0.0f;
    }

    //==========================================================================
    // Get reference level offset for current K-type
    float getReferenceLevel() const
    {
        switch (currentType)
        {
            case Type::K12: return -12.0f;
            case Type::K14: return -14.0f;
            case Type::K20: return -20.0f;
        }
        return -14.0f;
    }

    // Get headroom for current K-type
    float getHeadroom() const
    {
        switch (currentType)
        {
            case Type::K12: return 12.0f;
            case Type::K14: return 14.0f;
            case Type::K20: return 20.0f;
        }
        return 14.0f;
    }

    static const char* getTypeName(Type type)
    {
        switch (type)
        {
            case Type::K12: return "K-12";
            case Type::K14: return "K-14";
            case Type::K20: return "K-20";
        }
        return "K-14";
    }

private:
    float linearToDb(float linear) const
    {
        if (linear < 1e-10f) return -100.0f;
        return 20.0f * std::log10(linear);
    }

    float linearToKLevel(float linear) const
    {
        // K-level = RMS_dB + reference_offset
        // This makes the reference level appear at 0 VU on the meter
        return linearToDb(linear) - getReferenceLevel();
    }

    double sampleRate = 44100.0;
    Type currentType = Type::K14;

    float rmsAccumulatorL = 0.0f;
    float rmsAccumulatorR = 0.0f;
    float peakHoldL = 0.0f;
    float peakHoldR = 0.0f;

    float decayCoeff = 0.999f;
};

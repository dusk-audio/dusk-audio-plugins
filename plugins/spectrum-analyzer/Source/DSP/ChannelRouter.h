#pragma once

#include <cmath>

//==============================================================================
/**
    Channel routing for spectrum analysis.
    Supports Stereo, Mono, Mid, and Side modes.
*/
class ChannelRouter
{
public:
    enum class Mode
    {
        Stereo = 0,
        Mono,
        Mid,
        Side
    };

    ChannelRouter() = default;

    void setMode(Mode newMode) { mode = newMode; }
    Mode getMode() const { return mode; }

    //==========================================================================
    // Process audio and output the channels to analyze
    void process(const float* inputL, const float* inputR,
                 float* outputL, float* outputR, int numSamples)
    {
        switch (mode)
        {
            case Mode::Stereo:
                // Pass through
                for (int i = 0; i < numSamples; ++i)
                {
                    outputL[i] = inputL[i];
                    outputR[i] = inputR[i];
                }
                break;

            case Mode::Mono:
                // Sum to mono
                for (int i = 0; i < numSamples; ++i)
                {
                    float mono = (inputL[i] + inputR[i]) * 0.5f;
                    outputL[i] = mono;
                    outputR[i] = mono;
                }
                break;

            case Mode::Mid:
                // Mid = (L + R) / sqrt(2)
                for (int i = 0; i < numSamples; ++i)
                {
                    float mid = (inputL[i] + inputR[i]) * kSqrt2Inv;
                    outputL[i] = mid;
                    outputR[i] = mid;
                }
                break;

            case Mode::Side:
                // Side = (L - R) / sqrt(2)
                for (int i = 0; i < numSamples; ++i)
                {
                    float side = (inputL[i] - inputR[i]) * kSqrt2Inv;
                    outputL[i] = side;
                    outputR[i] = side;
                }
                break;
        }
    }

    //==========================================================================
    // Get mode name for display
    static const char* getModeNameShort(Mode m)
    {
        switch (m)
        {
            case Mode::Stereo: return "ST";
            case Mode::Mono:   return "M";
            case Mode::Mid:    return "Mid";
            case Mode::Side:   return "Side";
        }
        return "ST";
    }

    static const char* getModeName(Mode m)
    {
        switch (m)
        {
            case Mode::Stereo: return "Stereo";
            case Mode::Mono:   return "Mono";
            case Mode::Mid:    return "Mid";
            case Mode::Side:   return "Side";
        }
        return "Stereo";
    }

private:
    Mode mode = Mode::Stereo;
    static constexpr float kSqrt2Inv = 0.7071067811865476f;  // 1/sqrt(2)
};

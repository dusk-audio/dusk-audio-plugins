#pragma once

#include <cmath>
#include <algorithm>

class ToneStack
{
public:
    enum class Type { American = 0, British = 1, AC = 2 };

    void prepare (double sampleRate);
    void reset();
    void setType (Type type);
    void setBass (float value01);
    void setMid (float value01);
    void setTreble (float value01);
    void process (float* buffer, int numSamples);

private:
    Type currentType_ = Type::British;
    double sampleRate_ = 44100.0;
    float bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;

    // Transposed Direct Form II biquad
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float x)
        {
            float out = b0 * x + z1;
            z1 = b1 * x - a1 * out + z2;
            z2 = b2 * x - a2 * out;
            return out;
        }

        void reset() { z1 = z2 = 0.0f; }
    };

    Biquad bassFilter_, midFilter_, trebleFilter_;
    bool coeffsDirty_ = true;

    void recomputeCoefficients();
    void computeLowShelf (Biquad& bq, float freq, float gainDB, float Q);
    void computePeaking (Biquad& bq, float freq, float gainDB, float Q);
    void computeHighShelf (Biquad& bq, float freq, float gainDB, float Q);
};

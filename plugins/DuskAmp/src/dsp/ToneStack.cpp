#include "ToneStack.h"

static constexpr float kPi = 3.14159265358979323846f;

void ToneStack::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    recomputeCoefficients();
    reset();
}

void ToneStack::reset()
{
    bassFilter_.reset();
    midFilter_.reset();
    trebleFilter_.reset();
}

void ToneStack::setType (Type type)
{
    if (type == currentType_) return;
    currentType_ = type;
    coeffsDirty_ = true;
}

void ToneStack::setBass (float value01)
{
    bass_ = std::clamp (value01, 0.0f, 1.0f);
    coeffsDirty_ = true;
}

void ToneStack::setMid (float value01)
{
    mid_ = std::clamp (value01, 0.0f, 1.0f);
    coeffsDirty_ = true;
}

void ToneStack::setTreble (float value01)
{
    treble_ = std::clamp (value01, 0.0f, 1.0f);
    coeffsDirty_ = true;
}

void ToneStack::process (float* buffer, int numSamples)
{
    if (coeffsDirty_)
    {
        recomputeCoefficients();
        coeffsDirty_ = false;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];
        sample = bassFilter_.process (sample);
        sample = midFilter_.process (sample);
        sample = trebleFilter_.process (sample);
        buffer[i] = sample;
    }
}

void ToneStack::recomputeCoefficients()
{
    // Map 0-1 knob values to dB gain range: 0 -> -15dB, 0.5 -> 0dB, 1 -> +15dB
    float bassDB   = (bass_   - 0.5f) * 30.0f;
    float midDB    = (mid_    - 0.5f) * 30.0f;
    float trebleDB = (treble_ - 0.5f) * 30.0f;

    float bassFreq, midFreq, trebleFreq, midQ;

    switch (currentType_)
    {
        case Type::American:
            bassFreq   = 100.0f;
            midFreq    = 450.0f;
            midQ       = 0.7f;
            trebleFreq = 3500.0f;
            break;

        case Type::British:
            bassFreq   = 120.0f;
            midFreq    = 700.0f;
            midQ       = 0.9f;
            trebleFreq = 3000.0f;
            break;

        case Type::AC:
            bassFreq   = 150.0f;
            midFreq    = 1000.0f;
            midQ       = 1.2f;
            trebleFreq = 4000.0f;
            break;
    }

    computeLowShelf (bassFilter_, bassFreq, bassDB, 0.707f);
    computePeaking (midFilter_, midFreq, midDB, midQ);
    computeHighShelf (trebleFilter_, trebleFreq, trebleDB, 0.707f);
}

// Audio EQ Cookbook (Robert Bristow-Johnson) formulas

void ToneStack::computeLowShelf (Biquad& bq, float freq, float gainDB, float Q)
{
    float nyquist = static_cast<float> (sampleRate_) * 0.49f;
    freq = std::min (freq, nyquist);

    float A  = std::pow (10.0f, gainDB / 40.0f);
    float w0 = 2.0f * kPi * freq / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);
    float sqrtA = std::sqrt (A);

    float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
    if (std::abs (a0) < 1e-7f) { bq.b0 = 1.0f; bq.b1 = bq.b2 = bq.a1 = bq.a2 = 0.0f; return; }

    bq.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha)) / a0;
    bq.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
    bq.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha)) / a0;
    bq.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
    bq.a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha) / a0;
}

void ToneStack::computePeaking (Biquad& bq, float freq, float gainDB, float Q)
{
    float nyquist = static_cast<float> (sampleRate_) * 0.49f;
    freq = std::min (freq, nyquist);

    float A  = std::pow (10.0f, gainDB / 40.0f);
    float w0 = 2.0f * kPi * freq / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);

    float a0 = 1.0f + alpha / A;
    if (std::abs (a0) < 1e-7f) { bq.b0 = 1.0f; bq.b1 = bq.b2 = bq.a1 = bq.a2 = 0.0f; return; }

    bq.b0 = (1.0f + alpha * A) / a0;
    bq.b1 = (-2.0f * cosw0) / a0;
    bq.b2 = (1.0f - alpha * A) / a0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha / A) / a0;
}

void ToneStack::computeHighShelf (Biquad& bq, float freq, float gainDB, float Q)
{
    float nyquist = static_cast<float> (sampleRate_) * 0.49f;
    freq = std::min (freq, nyquist);

    float A  = std::pow (10.0f, gainDB / 40.0f);
    float w0 = 2.0f * kPi * freq / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);
    float sqrtA = std::sqrt (A);

    float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
    if (std::abs (a0) < 1e-7f) { bq.b0 = 1.0f; bq.b1 = bq.b2 = bq.a1 = bq.a2 = 0.0f; return; }

    bq.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha)) / a0;
    bq.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
    bq.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha)) / a0;
    bq.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
    bq.a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha) / a0;
}

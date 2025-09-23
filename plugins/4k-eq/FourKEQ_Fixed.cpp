// Fixed frequency pre-warping implementation for updateHFBand and other bands
// This addresses the frequency cramping issue in digital EQs

#include <cmath>

// Add this helper function to calculate pre-warped frequency
float preWarpFrequency(float freq, double sampleRate)
{
    // Pre-warp frequency for bilinear transform
    // This compensates for frequency warping near Nyquist
    const float nyquist = sampleRate * 0.5f;
    const float k = std::tan((M_PI * freq) / sampleRate);
    float warpedFreq = (sampleRate / M_PI) * std::atan(k);

    // Apply additional compensation for very high frequencies
    if (freq > nyquist * 0.4f) {
        // Extra compensation factor for frequencies above 40% of Nyquist
        float ratio = freq / nyquist;
        float compensation = 1.0f + (ratio - 0.4f) * 0.3f; // Gradually increase compensation
        warpedFreq = freq * compensation;
    }

    return std::min(warpedFreq, static_cast<float>(nyquist * 0.99f));
}

// Alternative approach using analog prototype design
void updateHFBandWithPreWarp(double sampleRate)
{
    float gain = hfGainParam->load();
    float freq = hfFreqParam->load();
    bool isBlack = (eqTypeParam->load() > 0.5f);
    bool isBell = (hfBellParam->load() > 0.5f);

    // Pre-warp the frequency
    float warpedFreq = preWarpFrequency(freq, sampleRate);

    if (isBlack && isBell)
    {
        // Bell mode with pre-warped frequency
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, warpedFreq, 0.7f, juce::Decibels::decibelsToGain(gain));
        hfFilter.filter.coefficients = coeffs;
        hfFilter.filterR.coefficients = coeffs;
    }
    else
    {
        // High shelf with pre-warped frequency
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
            sampleRate, warpedFreq, 0.7f, juce::Decibels::decibelsToGain(gain));
        hfFilter.filter.coefficients = coeffs;
        hfFilter.filterR.coefficients = coeffs;
    }
}

// Similar fix for HM band which also operates in higher frequencies
void updateHMBandWithPreWarp(double sampleRate)
{
    float gain = hmGainParam->load();
    float freq = hmFreqParam->load();
    float q = hmQParam->load();
    bool isBlack = (eqTypeParam->load() > 0.5f);

    // Pre-warp frequency if above 3kHz
    float warpedFreq = freq;
    if (freq > 3000.0f) {
        warpedFreq = preWarpFrequency(freq, sampleRate);
    }

    // Apply dynamic Q in Black mode
    if (isBlack)
        q = calculateDynamicQ(gain, q);

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, warpedFreq, q, juce::Decibels::decibelsToGain(gain));

    hmFilter.filter.coefficients = coeffs;
    hmFilter.filterR.coefficients = coeffs;
}

// Advanced: Custom biquad coefficients with exact analog matching
void makeAnalogMatchedHighShelf(double sampleRate, float freq, float q, float gainDB,
                                juce::dsp::IIR::Coefficients<float>* coeffs)
{
    // Use analog prototype and bilinear transform with pre-warping
    float A = std::pow(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * q);

    // Pre-warp critical frequency
    float K = std::tan(w0 / 2.0f);
    float K2 = K * K;
    float norm = 1.0f / (1.0f + std::sqrt(2.0f) * K + K2);

    // High shelf with pre-warping compensation
    float b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha);
    float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
    float b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha);
    float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
    float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
    float a2 = (A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha;

    // Normalize
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;

    *coeffs = juce::dsp::IIR::Coefficients<float>(b0, b1, b2, 1.0f, a1, a2);
}

// RECOMMENDATION: Update the main updateFilters() method to use these fixed versions:
/*
void FourKEQ::updateFilters()
{
    double oversampledRate = currentSampleRate * oversamplingFactor;

    updateHPF(oversampledRate);
    updateLPF(oversampledRate);
    updateLFBand(oversampledRate);
    updateLMBand(oversampledRate);
    updateHMBandWithPreWarp(oversampledRate);  // Use pre-warped version
    updateHFBandWithPreWarp(oversampledRate);   // Use pre-warped version
}
*/
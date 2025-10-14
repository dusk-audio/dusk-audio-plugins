#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <JuceHeader.h>

// Test program to verify FFT processing with pure sine wave
int main()
{
    std::cout << "FFT Spectrum Analyzer Test\n";
    std::cout << "===========================\n\n";

    // Test parameters
    const int fftOrder = 12;
    const int fftSize = 1 << fftOrder;  // 4096
    const double sampleRate = 48000.0;
    const float testFreq = 1000.0f;      // 1 kHz test tone
    const float amplitude = 0.794328f;   // -18 dBFS (10^(-18/20))

    std::cout << "FFT Size: " << fftSize << "\n";
    std::cout << "Sample Rate: " << sampleRate << " Hz\n";
    std::cout << "Test Frequency: " << testFreq << " Hz\n";
    std::cout << "Test Amplitude: " << amplitude << " (-18 dBFS)\n";
    std::cout << "Frequency Resolution: " << (sampleRate / fftSize) << " Hz/bin\n\n";

    // Create FFT and window
    juce::dsp::FFT forwardFFT(fftOrder);
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);

    // Generate test signal: pure 1 kHz sine wave
    std::vector<float> signal(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        signal[i] = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * testFreq * t);
    }

    // Prepare FFT data (need 2x size for complex data)
    std::vector<float> fftData(fftSize * 2, 0.0f);
    std::copy(signal.begin(), signal.end(), fftData.begin());

    // Apply Hann window
    window.multiplyWithWindowingTable(fftData.data(), fftSize);

    // Perform FFT
    forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

    // Calculate scaling factors
    // For Hann window: coherent gain = 0.5, so scale by 2.0 to compensate
    // For FFT normalization: scale by 2.0/N (factor of 2 because we only look at positive frequencies)
    const float scalingFactor = 2.0f / static_cast<float>(fftSize) * 2.0f;  // (2/N) * (1/coherent_gain)

    std::cout << "Scaling factor: " << scalingFactor << "\n\n";

    // Find the peak and display results around test frequency
    int expectedBin = static_cast<int>(testFreq * fftSize / sampleRate + 0.5f);

    std::cout << "Expected bin for " << testFreq << " Hz: " << expectedBin << "\n";
    std::cout << "Expected frequency in bin: " << (expectedBin * sampleRate / fftSize) << " Hz\n\n";

    // Find actual peak
    float maxMagnitude = 0.0f;
    int maxBin = 0;

    for (int i = 0; i < fftSize / 2; ++i)
    {
        float magnitude = fftData[i] * scalingFactor;
        if (magnitude > maxMagnitude)
        {
            maxMagnitude = magnitude;
            maxBin = i;
        }
    }

    float peakDB = 20.0f * std::log10f(maxMagnitude);
    float peakFreq = maxBin * sampleRate / fftSize;

    std::cout << "Peak found at bin " << maxBin << " (" << peakFreq << " Hz)\n";
    std::cout << "Peak magnitude: " << maxMagnitude << " (" << peakDB << " dBFS)\n\n";

    // Display spectrum around the peak (±10 bins)
    std::cout << "Spectrum around peak:\n";
    std::cout << "Bin\tFreq(Hz)\tMagnitude\tdB\n";
    std::cout << "---\t--------\t---------\t--\n";

    for (int i = std::max(0, maxBin - 10); i <= std::min(fftSize / 2 - 1, maxBin + 10); ++i)
    {
        float magnitude = fftData[i] * scalingFactor;
        float freq = i * sampleRate / fftSize;
        float db = magnitude > 1e-10f ? 20.0f * std::log10f(magnitude) : -200.0f;

        std::cout << i << "\t" << freq << "\t\t" << magnitude;
        if (db > -100.0f)
            std::cout << "\t\t" << db << " dB";
        else
            std::cout << "\t\t<-100 dB";

        if (i == maxBin)
            std::cout << " <-- PEAK";
        std::cout << "\n";
    }

    // Calculate noise floor (average of bins far from peak)
    float noiseSum = 0.0f;
    int noiseCount = 0;

    for (int i = 100; i < 200; ++i)  // Sample bins far from 1kHz
    {
        float magnitude = fftData[i] * scalingFactor;
        noiseSum += magnitude;
        noiseCount++;
    }

    float noiseFloor = noiseSum / noiseCount;
    float noiseFloorDB = 20.0f * std::log10f(noiseFloor);
    float snr = peakDB - noiseFloorDB;

    std::cout << "\nNoise floor: " << noiseFloor << " (" << noiseFloorDB << " dBFS)\n";
    std::cout << "Signal-to-Noise Ratio: " << snr << " dB\n";

    // Check if results are correct
    std::cout << "\n=== VERIFICATION ===\n";
    bool freqCorrect = std::abs(peakFreq - testFreq) < 10.0f;
    bool ampCorrect = std::abs(peakDB - (-18.0f)) < 1.0f;
    bool noiseGood = snr > 80.0f;

    std::cout << "Frequency accuracy: " << (freqCorrect ? "PASS" : "FAIL") << "\n";
    std::cout << "Amplitude accuracy: " << (ampCorrect ? "PASS" : "FAIL") << " (expected -18 dB, got " << peakDB << " dB)\n";
    std::cout << "Noise floor: " << (noiseGood ? "PASS" : "FAIL") << " (SNR = " << snr << " dB)\n";

    if (freqCorrect && ampCorrect && noiseGood)
    {
        std::cout << "\n✓ All tests PASSED!\n";
        return 0;
    }
    else
    {
        std::cout << "\n✗ Some tests FAILED\n";
        return 1;
    }
}

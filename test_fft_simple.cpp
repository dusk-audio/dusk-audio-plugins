#include <iostream>
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>

// Simple FFT implementation for testing
class SimpleFFT
{
public:
    static void fft(std::vector<std::complex<float>>& data)
    {
        const size_t n = data.size();
        if (n <= 1) return;

        // Divide
        std::vector<std::complex<float>> even(n / 2);
        std::vector<std::complex<float>> odd(n / 2);

        for (size_t i = 0; i < n / 2; ++i)
        {
            even[i] = data[i * 2];
            odd[i] = data[i * 2 + 1];
        }

        // Conquer
        fft(even);
        fft(odd);

        // Combine
        for (size_t k = 0; k < n / 2; ++k)
        {
            float angle = -2.0f * M_PI * k / n;
            std::complex<float> t = std::polar(1.0f, angle) * odd[k];
            data[k] = even[k] + t;
            data[k + n / 2] = even[k] - t;
        }
    }
};

// Hann window
void applyHannWindow(std::vector<float>& data)
{
    const size_t n = data.size();
    for (size_t i = 0; i < n; ++i)
    {
        float multiplier = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (n - 1)));
        data[i] *= multiplier;
    }
}

int main()
{
    std::cout << "FFT Spectrum Analyzer Test\n";
    std::cout << "===========================\n\n";

    // Test parameters
    const int fftSize = 4096;
    const double sampleRate = 48000.0;
    const float testFreq = 1000.0f;      // 1 kHz test tone
    const float amplitude = 0.794328f;   // -18 dBFS (10^(-18/20))

    std::cout << "FFT Size: " << fftSize << "\n";
    std::cout << "Sample Rate: " << sampleRate << " Hz\n";
    std::cout << "Test Frequency: " << testFreq << " Hz\n";
    std::cout << "Test Amplitude: " << amplitude << " (-18 dBFS)\n";
    std::cout << "Frequency Resolution: " << (sampleRate / fftSize) << " Hz/bin\n\n";

    // Generate test signal: pure 1 kHz sine wave
    std::vector<float> signal(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        signal[i] = amplitude * std::sin(2.0f * M_PI * testFreq * t);
    }

    // Apply Hann window
    applyHannWindow(signal);

    // Calculate Hann window coherent gain
    float hannCoherentGain = 0.5f;  // For Hann window

    // Prepare complex FFT data
    std::vector<std::complex<float>> fftData(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        fftData[i] = std::complex<float>(signal[i], 0.0f);
    }

    // Perform FFT
    SimpleFFT::fft(fftData);

    // Calculate magnitudes with proper scaling
    // For JUCE performFrequencyOnlyForwardTransform:
    // - Returns magnitudes directly (not power)
    // - NO built-in normalization (raw FFT output)
    // - We need to scale by: (2/N) / coherent_gain
    //   - 2/N: Standard FFT normalization (2 for one-sided spectrum, N for FFT size)
    //   - 1/coherent_gain: Compensate for window amplitude loss (0.5 for Hann)
    const float scalingFactor = (2.0f / static_cast<float>(fftSize)) / hannCoherentGain;

    std::vector<float> magnitudes(fftSize / 2);
    for (int i = 0; i < fftSize / 2; ++i)
    {
        magnitudes[i] = std::abs(fftData[i]) * scalingFactor;
    }

    std::cout << "Scaling factor: " << scalingFactor << "\n\n";

    // Find the peak
    int expectedBin = static_cast<int>(testFreq * fftSize / sampleRate + 0.5f);
    std::cout << "Expected bin for " << testFreq << " Hz: " << expectedBin << "\n";
    std::cout << "Expected frequency in bin: " << (expectedBin * sampleRate / fftSize) << " Hz\n\n";

    float maxMagnitude = 0.0f;
    int maxBin = 0;

    for (int i = 0; i < fftSize / 2; ++i)
    {
        if (magnitudes[i] > maxMagnitude)
        {
            maxMagnitude = magnitudes[i];
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
        float magnitude = magnitudes[i];
        float freq = i * sampleRate / fftSize;
        float db = magnitude > 1e-10f ? 20.0f * std::log10f(magnitude) : -200.0f;

        printf("%d\t%.1f\t\t%.6f", i, freq, magnitude);
        if (db > -100.0f)
            printf("\t%.1f dB", db);
        else
            printf("\t<-100 dB");

        if (i == maxBin)
            printf(" <-- PEAK");
        printf("\n");
    }

    // Calculate noise floor (average of bins far from peak)
    float noiseSum = 0.0f;
    int noiseCount = 0;

    for (int i = 100; i < 200; ++i)
    {
        noiseSum += magnitudes[i];
        noiseCount++;
    }

    float noiseFloor = noiseSum / noiseCount;
    float noiseFloorDB = noiseFloor > 1e-10f ? 20.0f * std::log10f(noiseFloor) : -200.0f;
    float snr = peakDB - noiseFloorDB;

    std::cout << "\nNoise floor: " << noiseFloor << " (" << noiseFloorDB << " dBFS)\n";
    std::cout << "Signal-to-Noise Ratio: " << snr << " dB\n";

    // Check if results are correct
    std::cout << "\n=== VERIFICATION ===\n";
    bool freqCorrect = std::abs(peakFreq - testFreq) < 15.0f;  // Within one bin
    bool ampCorrect = std::abs(peakDB - (-18.0f)) < 1.5f;      // Within 1.5 dB
    bool noiseGood = snr > 60.0f;                               // SNR > 60 dB

    std::cout << "Frequency accuracy: " << (freqCorrect ? "PASS" : "FAIL")
              << " (expected " << testFreq << " Hz, got " << peakFreq << " Hz)\n";
    std::cout << "Amplitude accuracy: " << (ampCorrect ? "PASS" : "FAIL")
              << " (expected -18.0 dB, got " << peakDB << " dB)\n";
    std::cout << "Noise floor: " << (noiseGood ? "PASS" : "FAIL")
              << " (SNR = " << snr << " dB)\n";

    if (freqCorrect && ampCorrect && noiseGood)
    {
        std::cout << "\nAll tests PASSED!\n";
        return 0;
    }
    else
    {
        std::cout << "\nSome tests FAILED\n";
        return 1;
    }
}

#include "FFTProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
    // juce::Decibels::gainToDecibels(gain, -100): 20*log10(gain) clamped to the
    // floor, with non-positive gain mapping straight to the floor.
    inline float gainToDecibels(float gain, float minusInfinityDb)
    {
        return gain > 0.0f
             ? std::max(minusInfinityDb, 20.0f * std::log10(gain))
             : minusInfinityDb;
    }
}

//==============================================================================
FFTProcessor::FFTProcessor()
{
    // Ring capacities are fixed at 16384 (member initialisers), matching the
    // JUCE AbstractFifo sizes.
}

void FFTProcessor::prepare(double sr, int /*maxBlockSize*/)
{
    sampleRate = sr;

    // Initialize FFT with current resolution
    fftPrepared = false;           // force a rebuild even at the same size
    updateFFTSize(currentResolution);

    // Update peak hold samples based on refresh rate (30 Hz assumed)
    peakHoldSamples = static_cast<int>(peakHoldTime * 30.0f);

    reset();
}

void FFTProcessor::reset()
{
    // Clear FIFOs
    fifoL.clear();
    fifoR.clear();

    // Clear magnitudes
    displayMagnitudes.fill(-100.0f);
    peakHoldMagnitudes.fill(-100.0f);
    smoothedMagnitudes.fill(-100.0f);
    peakHoldCounters.fill(0);

    dataReady.store(false);
}

//==============================================================================
void FFTProcessor::pushSamples(const float* left, const float* right, int numSamples)
{
    fifoL.write(left, numSamples);
    fifoR.write(right, numSamples);
}

//==============================================================================
void FFTProcessor::processFFT()
{
    // Check if we have enough samples
    if (fifoL.numReady() < currentFFTSize || fifoR.numReady() < currentFFTSize)
        return;

    if (!fifoL.read(fftInputL.data(), currentFFTSize))
        return;
    if (!fifoR.read(fftInputR.data(), currentFFTSize))
        return;

    // Sum to mono for spectrum display (or could do L/R separately), window,
    // and load the complex FFT input. The window table already carries JUCE's
    // normalise=true scaling.
    for (int i = 0; i < currentFFTSize; ++i)
    {
        const float mono = (fftInputL[(size_t)i] + fftInputR[(size_t)i]) * 0.5f;
        fftRe[(size_t)i] = mono * hannWindow[(size_t)i];
        fftIm[(size_t)i] = 0.0f;
    }

    // Perform FFT and take bin magnitudes (what JUCE's
    // performFrequencyOnlyForwardTransform leaves in the buffer's first half).
    fft.forward(fftRe.data(), fftIm.data());

    const int numFFTBins = currentFFTSize / 2;
    for (int k = 0; k < numFFTBins; ++k)
        fftRe[(size_t)k] = std::sqrt(fftRe[(size_t)k] * fftRe[(size_t)k]
                                   + fftIm[(size_t)k] * fftIm[(size_t)k]);

    // Map FFT bins to logarithmic display bins
    float binFreqWidth = static_cast<float>(sampleRate) / static_cast<float>(currentFFTSize);

    float logMinFreq = std::log10(minFreq);
    float logMaxFreq = std::log10(maxFreq);
    float logRange = logMaxFreq - logMinFreq;

    // Decay per frame (at 30 Hz refresh)
    float decayPerFrame = decayRate / 30.0f;

    for (int displayBin = 0; displayBin < DISPLAY_BINS; ++displayBin)
    {
        // Map display bin to frequency (logarithmic)
        float normalizedPos = static_cast<float>(displayBin) / static_cast<float>(DISPLAY_BINS - 1);
        float logFreq = logMinFreq + normalizedPos * logRange;
        float freq = std::pow(10.0f, logFreq);

        // Find corresponding FFT bin
        float fftBinFloat = freq / binFreqWidth;
        int fftBin = static_cast<int>(fftBinFloat);
        fftBin = std::clamp(fftBin, 0, numFFTBins - 1);

        // Get magnitude
        float magnitude = fftRe[static_cast<size_t>(fftBin)];

        // Normalize and convert to dB
        float dB = gainToDecibels(
            magnitude * 2.0f / static_cast<float>(currentFFTSize), -100.0f);

        // Apply slope compensation
        if (std::abs(slopeDbPerOctave) > 0.01f)
        {
            float octavesFromRef = std::log2(freq / 1000.0f);  // Reference at 1kHz
            dB += octavesFromRef * slopeDbPerOctave;
        }

        // Apply smoothing
        float smoothed;
        if (smoothingFactor > 0.01f)
        {
            float coeff = smoothingFactor * 0.95f;  // Scale to useful range
            smoothed = smoothedMagnitudes[displayBin] * coeff + dB * (1.0f - coeff);
            smoothedMagnitudes[displayBin] = smoothed;
        }
        else
        {
            smoothed = dB;
            smoothedMagnitudes[displayBin] = dB;
        }

        displayMagnitudes[displayBin] = smoothed;

        // Update peak hold
        if (peakHoldEnabled)
        {
            if (smoothed > peakHoldMagnitudes[displayBin])
            {
                peakHoldMagnitudes[displayBin] = smoothed;
                peakHoldCounters[displayBin] = peakHoldSamples;
            }
            else
            {
                if (peakHoldCounters[displayBin] > 0)
                {
                    peakHoldCounters[displayBin]--;
                }
                else
                {
                    peakHoldMagnitudes[displayBin] -= decayPerFrame;
                    if (peakHoldMagnitudes[displayBin] < smoothed)
                        peakHoldMagnitudes[displayBin] = smoothed;
                }
            }
        }
    }

    dataReady.store(true);
}

//==============================================================================
void FFTProcessor::updateFFTSize(Resolution resolution)
{
    int order = static_cast<int>(resolution);
    int newSize = 1 << order;

    if (newSize != currentFFTSize || !fftPrepared)
    {
        currentFFTSize = newSize;
        currentResolution = resolution;

        fft.prepare(currentFFTSize);
        fftPrepared = true;

        // Hann window with JUCE's normalise=true behaviour: raw coefficients
        // 0.5 - 0.5*cos(2*pi*i/(N-1)), then every coefficient scaled by
        // N / sum(coefficients) so the window's DC amplitude is one. Without
        // that factor (~2.0 for Hann) every reading would sit ~6 dB low
        // against the JUCE build.
        hannWindow.assign((size_t)currentFFTSize, 0.0f);
        constexpr double kPi = 3.14159265358979323846;
        double sum = 0.0;
        for (int i = 0; i < currentFFTSize; ++i)
        {
            const double c = 0.5 - 0.5 * std::cos(2.0 * kPi * (double)i
                                                  / (double)(currentFFTSize - 1));
            hannWindow[(size_t)i] = (float)c;
            sum += c;
        }
        const float factor = (float)((double)currentFFTSize / sum);
        for (auto& c : hannWindow)
            c *= factor;

        fftInputL.assign(static_cast<size_t>(currentFFTSize), 0.0f);
        fftInputR.assign(static_cast<size_t>(currentFFTSize), 0.0f);
        fftRe.assign(static_cast<size_t>(currentFFTSize), 0.0f);
        fftIm.assign(static_cast<size_t>(currentFFTSize), 0.0f);
    }
}

void FFTProcessor::setResolution(Resolution res)
{
    updateFFTSize(res);
}

void FFTProcessor::setSmoothing(float smoothing)
{
    smoothingFactor = std::clamp(smoothing, 0.0f, 1.0f);
}

void FFTProcessor::setSlope(float dbPerOctave)
{
    slopeDbPerOctave = std::clamp(dbPerOctave, -4.5f, 4.5f);
}

void FFTProcessor::setDecayRate(float dbPerSecond)
{
    decayRate = std::clamp(dbPerSecond, 3.0f, 60.0f);
}

void FFTProcessor::setPeakHoldEnabled(bool enabled)
{
    peakHoldEnabled = enabled;
    if (!enabled)
        peakHoldMagnitudes.fill(-100.0f);
}

void FFTProcessor::setPeakHoldTime(float seconds)
{
    peakHoldTime = std::clamp(seconds, 0.5f, 10.0f);
    peakHoldSamples = static_cast<int>(peakHoldTime * 30.0f);
}

//==============================================================================
float FFTProcessor::getFrequencyForBin(int bin)
{
    float normalizedPos = static_cast<float>(bin) / static_cast<float>(DISPLAY_BINS - 1);
    float logMinF = std::log10(minFreq);
    float logMaxF = std::log10(maxFreq);
    float logFreq = logMinF + normalizedPos * (logMaxF - logMinF);
    return std::pow(10.0f, logFreq);
}

int FFTProcessor::getBinForFrequency(float freq)
{
    freq = std::clamp(freq, minFreq, maxFreq);
    float logMinF = std::log10(minFreq);
    float logMaxF = std::log10(maxFreq);
    float logFreq = std::log10(freq);
    float normalizedPos = (logFreq - logMinF) / (logMaxF - logMinF);
    return static_cast<int>(normalizedPos * static_cast<float>(DISPLAY_BINS - 1));
}

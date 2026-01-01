#include "FFTProcessor.h"

//==============================================================================
FFTProcessor::FFTProcessor()
{
    audioBufferL.resize(16384, 0.0f);
    audioBufferR.resize(16384, 0.0f);
}

void FFTProcessor::prepare(double sr, int /*maxBlockSize*/)
{
    sampleRate = sr;

    // Initialize FFT with current resolution
    updateFFTSize(currentResolution);

    // Update peak hold samples based on refresh rate (30 Hz assumed)
    peakHoldSamples = static_cast<int>(peakHoldTime * 30.0f);

    reset();
}

void FFTProcessor::reset()
{
    // Clear FIFOs
    fifoL.reset();
    fifoR.reset();

    std::fill(audioBufferL.begin(), audioBufferL.end(), 0.0f);
    std::fill(audioBufferR.begin(), audioBufferR.end(), 0.0f);

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
    // Push left channel
    {
        int start1, size1, start2, size2;
        fifoL.prepareToWrite(numSamples, start1, size1, start2, size2);

        if (size1 > 0)
            std::copy(left, left + size1, audioBufferL.begin() + start1);
        if (size2 > 0)
            std::copy(left + size1, left + numSamples, audioBufferL.begin() + start2);

        fifoL.finishedWrite(size1 + size2);
    }

    // Push right channel
    {
        int start1, size1, start2, size2;
        fifoR.prepareToWrite(numSamples, start1, size1, start2, size2);

        if (size1 > 0)
            std::copy(right, right + size1, audioBufferR.begin() + start1);
        if (size2 > 0)
            std::copy(right + size1, right + numSamples, audioBufferR.begin() + start2);

        fifoR.finishedWrite(size1 + size2);
    }
}

//==============================================================================
void FFTProcessor::processFFT()
{
    // Check if we have enough samples
    if (fifoL.getNumReady() < currentFFTSize || fifoR.getNumReady() < currentFFTSize)
        return;

    // Read left channel from FIFO
    {
        int start1, size1, start2, size2;
        fifoL.prepareToRead(currentFFTSize, start1, size1, start2, size2);

        std::copy(audioBufferL.begin() + start1,
                  audioBufferL.begin() + start1 + size1,
                  fftInputL.begin());
        if (size2 > 0)
        {
            std::copy(audioBufferL.begin() + start2,
                      audioBufferL.begin() + start2 + size2,
                      fftInputL.begin() + size1);
        }
        fifoL.finishedRead(size1 + size2);
    }

    // Read right channel from FIFO
    {
        int start1, size1, start2, size2;
        fifoR.prepareToRead(currentFFTSize, start1, size1, start2, size2);

        std::copy(audioBufferR.begin() + start1,
                  audioBufferR.begin() + start1 + size1,
                  fftInputR.begin());
        if (size2 > 0)
        {
            std::copy(audioBufferR.begin() + start2,
                      audioBufferR.begin() + start2 + size2,
                      fftInputR.begin() + size1);
        }
        fifoR.finishedRead(size1 + size2);
    }

    // Sum to mono for spectrum display (or could do L/R separately)
    for (int i = 0; i < currentFFTSize; ++i)
        fftWorkBuffer[i] = (fftInputL[i] + fftInputR[i]) * 0.5f;

    // Apply window
    window->multiplyWithWindowingTable(fftWorkBuffer.data(), static_cast<size_t>(currentFFTSize));

    // Perform FFT (frequency-only for efficiency)
    fft->performFrequencyOnlyForwardTransform(fftWorkBuffer.data());

    // Map FFT bins to logarithmic display bins
    int numFFTBins = currentFFTSize / 2;
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
        fftBin = juce::jlimit(0, numFFTBins - 1, fftBin);

        // Get magnitude
        float magnitude = fftWorkBuffer[static_cast<size_t>(fftBin)];

        // Normalize and convert to dB
        float dB = juce::Decibels::gainToDecibels(
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

    if (newSize != currentFFTSize || !fft)
    {
        currentFFTSize = newSize;
        currentResolution = resolution;

        fft = std::make_unique<juce::dsp::FFT>(order);
        window = std::make_unique<juce::dsp::WindowingFunction<float>>(
            static_cast<size_t>(currentFFTSize),
            juce::dsp::WindowingFunction<float>::hann);

        fftInputL.resize(static_cast<size_t>(currentFFTSize * 2), 0.0f);
        fftInputR.resize(static_cast<size_t>(currentFFTSize * 2), 0.0f);
        fftWorkBuffer.resize(static_cast<size_t>(currentFFTSize * 2), 0.0f);
    }
}

void FFTProcessor::setResolution(Resolution res)
{
    updateFFTSize(res);
}

void FFTProcessor::setSmoothing(float smoothing)
{
    smoothingFactor = juce::jlimit(0.0f, 1.0f, smoothing);
}

void FFTProcessor::setSlope(float dbPerOctave)
{
    slopeDbPerOctave = juce::jlimit(-4.5f, 4.5f, dbPerOctave);
}

void FFTProcessor::setDecayRate(float dbPerSecond)
{
    decayRate = juce::jlimit(3.0f, 60.0f, dbPerSecond);
}

void FFTProcessor::setPeakHoldEnabled(bool enabled)
{
    peakHoldEnabled = enabled;
    if (!enabled)
        peakHoldMagnitudes.fill(-100.0f);
}

void FFTProcessor::setPeakHoldTime(float seconds)
{
    peakHoldTime = juce::jlimit(0.5f, 10.0f, seconds);
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
    freq = juce::jlimit(minFreq, maxFreq, freq);
    float logMinF = std::log10(minFreq);
    float logMaxF = std::log10(maxFreq);
    float logFreq = std::log10(freq);
    float normalizedPos = (logFreq - logMinF) / (logMaxF - logMinF);
    return static_cast<int>(normalizedPos * static_cast<float>(DISPLAY_BINS - 1));
}

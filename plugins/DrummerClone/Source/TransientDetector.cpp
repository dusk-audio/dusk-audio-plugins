#include "TransientDetector.h"
#include <cmath>

TransientDetector::TransientDetector()
{
    // Initialize with default sample rate
    prepare(44100.0);
}

void TransientDetector::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;

    // Calculate window sizes based on sample rate
    rmsWindowSamples = static_cast<int>(sampleRate * RMS_WINDOW_MS / 1000.0);
    debounceSamples = static_cast<int>(sampleRate * DEBOUNCE_MS / 1000.0);

    // Prepare ring buffer (2 seconds)
    audioRingBuffer.resize(static_cast<size_t>(sampleRate * BUFFER_SECONDS), 0.0f);
    ringBufferWritePos = 0;

    // Set up high-pass filter at 100Hz
    updateHighPassFilter();

    // Prepare filter
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 4096;
    spec.numChannels = 1;
    highPassFilter.prepare(spec);

    // Clear RMS buffer
    rmsBuffer.clear();

    // Reset state
    reset();
}

void TransientDetector::updateHighPassFilter()
{
    // Create 100Hz high-pass filter coefficients (2nd order)
    highPassCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 100.0f, 0.707f);
    highPassFilter.coefficients = highPassCoeffs;
}

void TransientDetector::reset()
{
    highPassFilter.reset();
    rmsBuffer.clear();
    currentRMS = 0.0f;
    previousRMS = 0.0f;
    samplesSinceLastOnset = debounceSamples; // Allow immediate detection
    recentTransientCount = 0;
    recentOnsets.clear();
    std::fill(audioRingBuffer.begin(), audioRingBuffer.end(), 0.0f);
    ringBufferWritePos = 0;
}

void TransientDetector::setSensitivity(float newSensitivity)
{
    sensitivity = juce::jlimit(0.1f, 0.8f, newSensitivity);

    // Adjust threshold based on sensitivity
    // Lower sensitivity = higher threshold = fewer detections
    threshold = 0.2f - (sensitivity * 0.15f);  // Range: 0.08 to 0.185

    // Adjust rise threshold
    thresholdRiseDB = 6.0f - (sensitivity * 5.0f);  // Range: 1dB to 5.5dB
}

std::vector<double> TransientDetector::process(const juce::AudioBuffer<float>& buffer)
{
    std::vector<double> detectedOnsets;

    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return detectedOnsets;

    const float* inputData = buffer.getReadPointer(0);
    const int numSamples = buffer.getNumSamples();

    // Create a temporary buffer for filtered audio
    juce::AudioBuffer<float> filteredBuffer(1, numSamples);
    filteredBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);

    // Apply high-pass filter
    juce::dsp::AudioBlock<float> block(filteredBuffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    highPassFilter.process(context);

    const float* filteredData = filteredBuffer.getReadPointer(0);

    // Process sample by sample for onset detection
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = filteredData[i];

        // Add to RMS buffer
        rmsBuffer.push_back(sample * sample);
        if (rmsBuffer.size() > static_cast<size_t>(rmsWindowSamples))
            rmsBuffer.pop_front();

        // Calculate current RMS
        if (!rmsBuffer.empty())
        {
            float sum = 0.0f;
            for (float sq : rmsBuffer)
                sum += sq;
            currentRMS = std::sqrt(sum / static_cast<float>(rmsBuffer.size()));
        }

        // Check for onset
        samplesSinceLastOnset++;

        if (samplesSinceLastOnset >= debounceSamples && isOnset(currentRMS, previousRMS))
        {
            // Calculate time in seconds
            double onsetTime = static_cast<double>(i) / sampleRate;
            detectedOnsets.push_back(onsetTime);
            samplesSinceLastOnset = 0;
        }

        previousRMS = currentRMS;
    }

    // Add input to ring buffer
    addToRingBuffer(inputData, numSamples);

    // Update recent transient tracking
    recentOnsets.insert(recentOnsets.end(), detectedOnsets.begin(), detectedOnsets.end());

    // Remove onsets older than 1 second
    double currentTime = static_cast<double>(numSamples) / sampleRate;
    recentOnsets.erase(
        std::remove_if(recentOnsets.begin(), recentOnsets.end(),
            [currentTime](double t) { return (currentTime - t) > 1.0; }),
        recentOnsets.end());

    recentTransientCount = static_cast<int>(recentOnsets.size());

    return detectedOnsets;
}

bool TransientDetector::isOnset(float currentEnergy, float previousEnergy)
{
    // Must be above absolute threshold
    if (currentEnergy < threshold)
        return false;

    // Must have significant rise
    if (previousEnergy < 0.0001f)
        previousEnergy = 0.0001f;  // Prevent division by zero

    float riseDB = 20.0f * std::log10(currentEnergy / previousEnergy);

    return riseDB > thresholdRiseDB;
}

float TransientDetector::calculateRMS(const float* samples, int numSamples)
{
    if (numSamples == 0)
        return 0.0f;

    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        sum += samples[i] * samples[i];
    }

    return std::sqrt(sum / static_cast<float>(numSamples));
}

void TransientDetector::addToRingBuffer(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        audioRingBuffer[ringBufferWritePos] = samples[i];
        ringBufferWritePos = (ringBufferWritePos + 1) % static_cast<int>(audioRingBuffer.size());
    }
}
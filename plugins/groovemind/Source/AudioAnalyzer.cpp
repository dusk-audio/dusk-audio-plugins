/*
  ==============================================================================

    AudioAnalyzer.cpp
    Audio analysis implementation for Follow Mode

  ==============================================================================
*/

#include "AudioAnalyzer.h"

//==============================================================================
AudioAnalyzer::AudioAnalyzer()
{
    reset();
}

//==============================================================================
void AudioAnalyzer::prepare(double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = maxBlockSize;

    // Energy follower coefficients
    // Fast attack (~5ms), slower release (~100ms)
    float attackTimeMs = 5.0f;
    float releaseTimeMs = 100.0f;

    energyAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * attackTimeMs * 0.001f));
    energyReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * releaseTimeMs * 0.001f));

    // Compute sample-rate-dependent window sizes
    // Onset window: ~2 seconds for measuring onset density
    onsetWindowSamples = static_cast<int>(sampleRate * 2.0);

    // Minimum fill interval: ~4 seconds between automatic fills
    minFillIntervalSamples = static_cast<int>(sampleRate * 4.0);

    // Update filter coefficients for spectral analysis
    updateFilterCoefficients();

    reset();
}

//==============================================================================
void AudioAnalyzer::reset()
{
    currentAnalysis = AudioAnalysisResult();

    energyEnvelope = 0.0f;
    peakEnergy = 0.001f;  // Avoid division by zero
    std::fill(std::begin(energyHistory), std::end(energyHistory), 0.0f);
    energyHistoryIndex = 0;

    prevEnergy = 0.0f;
    onsetCount = 0;
    samplesSinceReset = 0;

    lowpassState = 0.0f;
    bandpassLowState = 0.0f;
    bandpassHighState = 0.0f;
    highpassState = 0.0f;

    lowBandEnergy = 0.0f;
    midBandEnergy = 0.0f;
    highBandEnergy = 0.0f;
    prevLowEnergy = 0.0f;
    prevMidEnergy = 0.0f;
    prevHighEnergy = 0.0f;

    std::fill(std::begin(spectralFluxHistory), std::end(spectralFluxHistory), 0.0f);
    spectralFluxIndex = 0;
    avgSpectralFlux = 0.0f;
    samplesSinceLastFill = minFillIntervalSamples; // Allow fill immediately

    lastBeatPosition = 0.0;
    std::fill(std::begin(beatEnergies), std::end(beatEnergies), 0.0f);
    currentBeatIndex = 0;
}

//==============================================================================
void AudioAnalyzer::setSensitivity(float newSensitivity)
{
    sensitivity = juce::jlimit(0.0f, 1.0f, newSensitivity);
    onsetThreshold = 0.15f * (1.0f - sensitivity * 0.7f);
}

void AudioAnalyzer::setEnergySmoothing(float timeMs)
{
    float releaseTimeMs = juce::jlimit(20.0f, 500.0f, timeMs);
    energyReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * releaseTimeMs * 0.001f));
}

void AudioAnalyzer::setFillSensitivity(float newSensitivity)
{
    fillSensitivity = juce::jlimit(0.0f, 1.0f, newSensitivity);
}

//==============================================================================
void AudioAnalyzer::updateFilterCoefficients()
{
    float twoPiOverSr = static_cast<float>(2.0 * juce::MathConstants<double>::pi / sampleRate);

    lowCutoff = 1.0f - std::exp(-200.0f * twoPiOverSr);    // ~200 Hz
    midCutoff = 1.0f - std::exp(-2000.0f * twoPiOverSr);   // ~2000 Hz
    highCutoff = 1.0f - std::exp(-6000.0f * twoPiOverSr);  // ~6000 Hz
}

//==============================================================================
void AudioAnalyzer::processBlock(const float* leftChannel, const float* rightChannel,
                                  int numSamples, double hostBpm, double hostPositionBeats)
{
    // Sum to mono
    std::vector<float> mono(numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        mono[i] = (leftChannel[i] + rightChannel[i]) * 0.5f;
    }

    // Run all analysis
    updateEnergyFollower(mono.data(), numSamples);
    detectOnsets(mono.data(), numSamples);
    analyzeSpectrum(mono.data(), numSamples);
    detectSectionChanges();
    trackBeats(hostPositionBeats);

    // Update sample counters
    samplesSinceReset += numSamples;
    samplesSinceLastFill += numSamples;

    // Calculate confidence based on signal level and time
    float signalPresence = currentAnalysis.smoothedEnergy > 0.01f ? 1.0f : 0.0f;
    float timeConfidence = juce::jmin(1.0f, static_cast<float>(samplesSinceReset) / static_cast<float>(sampleRate * 2.0));
    currentAnalysis.confidence = signalPresence * timeConfidence;

    // Is input active?
    currentAnalysis.isActive = currentAnalysis.smoothedEnergy > 0.01f;
}

//==============================================================================
void AudioAnalyzer::updateEnergyFollower(const float* monoData, int numSamples)
{
    // Calculate block RMS energy
    float blockEnergy = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        blockEnergy += monoData[i] * monoData[i];
    }
    blockEnergy = std::sqrt(blockEnergy / static_cast<float>(numSamples));

    // Update envelope with attack/release
    if (blockEnergy > energyEnvelope)
        energyEnvelope += energyAttackCoeff * (blockEnergy - energyEnvelope);
    else
        energyEnvelope += energyReleaseCoeff * (blockEnergy - energyEnvelope);

    // Track peak for normalization (slow decay)
    if (energyEnvelope > peakEnergy)
        peakEnergy = energyEnvelope;
    else
        peakEnergy *= 0.9999f;  // Very slow decay

    peakEnergy = juce::jmax(peakEnergy, 0.001f);  // Minimum to avoid div by zero

    // Normalized energy (0-1)
    currentAnalysis.energy = juce::jlimit(0.0f, 1.0f, energyEnvelope / peakEnergy);

    // Store in history for smoothing
    energyHistory[energyHistoryIndex] = currentAnalysis.energy;
    energyHistoryIndex = (energyHistoryIndex + 1) % 64;

    // Calculate smoothed energy (average over history)
    float sum = 0.0f;
    for (int i = 0; i < 64; ++i)
        sum += energyHistory[i];
    currentAnalysis.smoothedEnergy = sum / 64.0f;
}

//==============================================================================
void AudioAnalyzer::detectOnsets(const float* monoData, int numSamples)
{
    // Simple onset detection based on energy rise
    float blockEnergy = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        blockEnergy += std::abs(monoData[i]);
    }
    blockEnergy /= static_cast<float>(numSamples);

    // Detect onset if energy rises significantly
    float energyDelta = blockEnergy - prevEnergy;
    if (energyDelta > onsetThreshold && blockEnergy > 0.02f)
    {
        onsetCount++;
    }

    prevEnergy = prevEnergy * 0.9f + blockEnergy * 0.1f;  // Smooth for next comparison

    // Calculate onset density over window
    // Reset counter periodically
    if (samplesSinceReset > onsetWindowSamples)
    {
        // Onsets per second, normalized to 0-1 range
        // Assume 0-8 onsets/sec maps to 0-1 complexity
        float onsetsPerSec = static_cast<float>(onsetCount) * static_cast<float>(sampleRate) /
                             static_cast<float>(onsetWindowSamples);
        currentAnalysis.onsetDensity = juce::jlimit(0.0f, 1.0f, onsetsPerSec / 8.0f);

        // Reset for next window (but keep some history for continuity)
        onsetCount = onsetCount / 4;  // Carry over 25%
        samplesSinceReset = 0;
    }
}

//==============================================================================
void AudioAnalyzer::analyzeSpectrum(const float* monoData, int numSamples)
{
    // Process through 3-band filter bank
    float lowSum = 0.0f, midSum = 0.0f, highSum = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = monoData[i];

        // Low band (< 200 Hz)
        float low = applyLowpass(sample, lowpassState, lowCutoff);
        lowSum += std::abs(low);

        // Mid band (200 Hz - 2000 Hz)
        float mid = applyHighpass(sample, bandpassLowState, lowCutoff);
        mid = applyLowpass(mid, bandpassHighState, midCutoff);
        midSum += std::abs(mid);

        // High band (> 2000 Hz)
        float high = applyHighpass(sample, highpassState, midCutoff);
        highSum += std::abs(high);
    }

    // Update band energies with smoothing
    float smoothing = 0.1f;
    lowBandEnergy = lowBandEnergy * (1.0f - smoothing) + (lowSum / numSamples) * smoothing;
    midBandEnergy = midBandEnergy * (1.0f - smoothing) + (midSum / numSamples) * smoothing;
    highBandEnergy = highBandEnergy * (1.0f - smoothing) + (highSum / numSamples) * smoothing;

    // Calculate ratios
    float totalEnergy = lowBandEnergy + midBandEnergy + highBandEnergy;
    if (totalEnergy > 0.0001f)
    {
        currentAnalysis.lowEnergyRatio = lowBandEnergy / totalEnergy;
        currentAnalysis.midEnergyRatio = midBandEnergy / totalEnergy;
        currentAnalysis.highEnergyRatio = highBandEnergy / totalEnergy;
    }

    // Calculate spectral flux (change in spectrum)
    float flux = std::abs(lowBandEnergy - prevLowEnergy) +
                 std::abs(midBandEnergy - prevMidEnergy) +
                 std::abs(highBandEnergy - prevHighEnergy);

    // Normalize flux
    flux = juce::jlimit(0.0f, 1.0f, flux * 10.0f);
    currentAnalysis.spectralFlux = flux;

    // Store for next frame
    prevLowEnergy = lowBandEnergy;
    prevMidEnergy = midBandEnergy;
    prevHighEnergy = highBandEnergy;
}

//==============================================================================
void AudioAnalyzer::detectSectionChanges()
{
    // Store spectral flux in history
    spectralFluxHistory[spectralFluxIndex] = currentAnalysis.spectralFlux;
    spectralFluxIndex = (spectralFluxIndex + 1) % 32;

    // Calculate average spectral flux
    float sum = 0.0f;
    for (int i = 0; i < 32; ++i)
        sum += spectralFluxHistory[i];
    avgSpectralFlux = sum / 32.0f;

    // Detect section change: spectral flux significantly above average
    // AND we haven't triggered a fill recently
    float fluxThreshold = avgSpectralFlux * (2.5f - fillSensitivity * 1.5f);
    fluxThreshold = juce::jmax(fluxThreshold, 0.1f);

    currentAnalysis.sectionChangeDetected = false;
    currentAnalysis.suggestFill = false;

    if (currentAnalysis.spectralFlux > fluxThreshold &&
        samplesSinceLastFill > minFillIntervalSamples)
    {
        // Check for significant energy or spectrum change
        // This catches chord changes, key changes, dynamics changes
        if (currentAnalysis.spectralFlux > 0.3f ||
            std::abs(currentAnalysis.energy - currentAnalysis.smoothedEnergy) > 0.2f)
        {
            currentAnalysis.sectionChangeDetected = true;
            currentAnalysis.suggestFill = true;
            samplesSinceLastFill = 0;
        }
    }
}

//==============================================================================
void AudioAnalyzer::trackBeats(double hostPositionBeats)
{
    // Detect beat boundaries
    int currentBeat = static_cast<int>(std::fmod(hostPositionBeats, 4.0));

    if (currentBeat != currentBeatIndex && hostPositionBeats > lastBeatPosition)
    {
        // New beat - store energy
        beatEnergies[currentBeatIndex] = currentAnalysis.energy;
        currentBeatIndex = currentBeat;

        // Calculate downbeat strength (beat 1 vs others)
        float beat1Energy = beatEnergies[0];
        float otherBeatsAvg = (beatEnergies[1] + beatEnergies[2] + beatEnergies[3]) / 3.0f;

        // Downbeat is stronger if beat 1 has more energy
        if (otherBeatsAvg > 0.01f)
        {
            currentAnalysis.downbeatStrength = juce::jlimit(0.0f, 1.0f,
                (beat1Energy / otherBeatsAvg) - 0.5f);
        }
    }

    lastBeatPosition = hostPositionBeats;
}

/*
  ==============================================================================

    TransientDetector.cpp
    Transient detection implementation

  ==============================================================================
*/

#include "TransientDetector.h"

//==============================================================================
TransientDetector::TransientDetector()
{
}

//==============================================================================
void TransientDetector::prepare(double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate;

    // Calculate envelope follower coefficients
    // Fast attack (~1ms), slower release (~50ms)
    float attackTimeMs = 1.0f;
    float releaseTimeMs = 50.0f;

    attackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * attackTimeMs * 0.001f));
    releaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * releaseTimeMs * 0.001f));

    // Update hold time in samples
    holdTimeSamples = static_cast<int>(holdTimeMs * 0.001f * sampleRate);

    // Update filter coefficients
    updateFilterCoefficients();

    reset();
}

//==============================================================================
void TransientDetector::reset()
{
    envelope = 0.0f;
    previousEnvelope = 0.0f;
    envelopeDelta = 0.0f;
    inTransient = false;
    holdCounter = 0;
    totalSamplesProcessed = 0;

    bandEnergies.fill(0.0f);
    prevBandEnergies.fill(0.0f);

    lowpassState = 0.0f;
    bandpassLowState = 0.0f;
    bandpassHighState = 0.0f;
    highpassState = 0.0f;
}

//==============================================================================
void TransientDetector::updateFilterCoefficients()
{
    // Simple one-pole filter coefficients
    // coeff = 1 - exp(-2 * pi * fc / fs)

    float twoPiOverSr = static_cast<float>(2.0 * juce::MathConstants<double>::pi / sampleRate);

    lowCutoff = 1.0f - std::exp(-200.0f * twoPiOverSr);    // ~200 Hz
    midCutoff = 1.0f - std::exp(-2000.0f * twoPiOverSr);   // ~2000 Hz
    highCutoff = 1.0f - std::exp(-8000.0f * twoPiOverSr);  // ~8000 Hz
}

//==============================================================================
void TransientDetector::setSensitivity(float newSensitivity)
{
    sensitivity = juce::jlimit(0.0f, 1.0f, newSensitivity);
}

void TransientDetector::setThreshold(float newThreshold)
{
    threshold = juce::jlimit(0.0f, 1.0f, newThreshold);
}

void TransientDetector::setHoldTime(float newHoldTimeMs)
{
    holdTimeMs = juce::jlimit(10.0f, 200.0f, newHoldTimeMs);
    holdTimeSamples = static_cast<int>(holdTimeMs * 0.001f * sampleRate);
}

//==============================================================================
std::vector<TransientEvent> TransientDetector::processStereo(const float* leftChannel,
                                                               const float* rightChannel,
                                                               int numSamples)
{
    // Sum to mono
    std::vector<float> mono(numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        mono[i] = (leftChannel[i] + rightChannel[i]) * 0.5f;
    }

    return process(mono.data(), numSamples);
}

//==============================================================================
std::vector<TransientEvent> TransientDetector::process(const float* audioData, int numSamples)
{
    std::vector<TransientEvent> detectedTransients;

    // Adjust threshold based on sensitivity
    // Higher sensitivity = lower effective threshold
    float effectiveThreshold = threshold * (1.0f - sensitivity * 0.8f);

    // Minimum delta for transient detection (envelope rise rate)
    // Higher sensitivity = lower delta needed
    float minDelta = 0.01f * (1.0f - sensitivity * 0.9f);

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = audioData[i];
        float absSample = std::abs(sample);

        // Update envelope (asymmetric attack/release)
        if (absSample > envelope)
            envelope = envelope + attackCoeff * (absSample - envelope);
        else
            envelope = envelope + releaseCoeff * (absSample - envelope);

        // Calculate envelope delta (rate of rise)
        envelopeDelta = envelope - previousEnvelope;

        // Update band energies using simple filters
        float lowBand = applyLowpass(sample, lowpassState, lowCutoff);
        float midBand = applyHighpass(sample, bandpassLowState, lowCutoff) -
                        applyHighpass(sample, bandpassHighState, midCutoff);
        float highBand = applyHighpass(sample, highpassState, highCutoff);

        // Accumulate band energies (RMS-like)
        bandEnergies[0] = bandEnergies[0] * 0.99f + std::abs(lowBand) * 0.01f;
        bandEnergies[1] = bandEnergies[1] * 0.99f + std::abs(midBand) * 0.01f;
        bandEnergies[2] = bandEnergies[2] * 0.99f + std::abs(highBand) * 0.01f;

        // Decrement hold counter
        if (holdCounter > 0)
            holdCounter--;

        // Detect transient: envelope rising fast and above threshold
        if (!inTransient && holdCounter == 0)
        {
            if (envelopeDelta > minDelta && envelope > effectiveThreshold)
            {
                // Transient detected!
                inTransient = true;
                holdCounter = holdTimeSamples;

                TransientEvent event;
                event.timeInSamples = static_cast<double>(totalSamplesProcessed + i);
                event.strength = juce::jlimit(0.0f, 1.0f, envelope);
                event.lowEnergy = bandEnergies[0];
                event.midEnergy = bandEnergies[1];
                event.highEnergy = bandEnergies[2];
                event.instrumentCategory = classifyInstrument(
                    bandEnergies[0], bandEnergies[1], bandEnergies[2]);

                detectedTransients.push_back(event);
            }
        }

        // Reset transient flag when envelope starts falling
        if (inTransient && envelopeDelta < 0)
        {
            inTransient = false;
        }

        previousEnvelope = envelope;

        // Store previous band energies for spectral flux (future enhancement)
        prevBandEnergies = bandEnergies;
    }

    totalSamplesProcessed += numSamples;

    return detectedTransients;
}

//==============================================================================
int TransientDetector::classifyInstrument(float lowEnergy, float midEnergy, float highEnergy) const
{
    // Simple heuristic classification based on frequency content
    // This is a basic version - could be enhanced with ML

    float totalEnergy = lowEnergy + midEnergy + highEnergy;
    if (totalEnergy < 0.0001f)
        return 5;  // Other (too quiet to classify)

    // Normalize
    float lowRatio = lowEnergy / totalEnergy;
    float midRatio = midEnergy / totalEnergy;
    float highRatio = highEnergy / totalEnergy;

    // Classification heuristics:
    // - Kick: Dominant low frequencies (>60% low)
    // - Snare: Strong mid with some high (mid>30%, high>20%)
    // - Hi-hat: Dominant high frequencies (>50% high)
    // - Tom: Low-mid dominant, less high than snare
    // - Cymbal: High with sustain (similar to hihat but detected differently in context)

    if (lowRatio > 0.6f)
    {
        return 0;  // Kick
    }
    else if (highRatio > 0.5f)
    {
        return 2;  // Hi-hat
    }
    else if (midRatio > 0.3f && highRatio > 0.2f)
    {
        return 1;  // Snare
    }
    else if (lowRatio > 0.3f && midRatio > 0.3f)
    {
        return 3;  // Tom
    }
    else if (highRatio > 0.4f)
    {
        return 4;  // Cymbal
    }

    return 5;  // Other
}

/*
  ==============================================================================

    TempoTracker.cpp
    Tempo tracking implementation

  ==============================================================================
*/

#include "TempoTracker.h"

//==============================================================================
TempoTracker::TempoTracker()
{
    beatPeriodSamples = sampleRate * 60.0 / estimatedBpm;
}

//==============================================================================
void TempoTracker::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    beatPeriodSamples = sampleRate * 60.0 / estimatedBpm;
    reset();
}

//==============================================================================
void TempoTracker::reset()
{
    transientTimes.clear();
    transientStrengths.clear();
    recentIOIs.clear();
    tempoHistogram.fill(0.0f);

    estimatedBpm = hasTempoHint ? tempoHint : 120.0;
    confidence = 0.0f;
    lastBeatTime = 0.0;
    beatPeriodSamples = sampleRate * 60.0 / estimatedBpm;

    stableFrameCount = 0;
    previousBpm = estimatedBpm;
}

//==============================================================================
void TempoTracker::setTempoRange(double newMinBpm, double newMaxBpm)
{
    minBpm = juce::jlimit(30.0, 300.0, newMinBpm);
    maxBpm = juce::jlimit(30.0, 300.0, newMaxBpm);
    if (maxBpm < minBpm)
        std::swap(minBpm, maxBpm);
}

void TempoTracker::setAdaptationRate(float rate)
{
    adaptationRate = juce::jlimit(0.0f, 1.0f, rate);
}

void TempoTracker::setTempoHint(double bpm)
{
    tempoHint = juce::jlimit(minBpm, maxBpm, bpm);
    hasTempoHint = true;

    // Use hint to bootstrap estimate
    if (confidence < 0.3f)
    {
        estimatedBpm = tempoHint;
        beatPeriodSamples = sampleRate * 60.0 / estimatedBpm;
    }
}

void TempoTracker::clearTempoHint()
{
    hasTempoHint = false;
}

//==============================================================================
void TempoTracker::addTransient(double timeInSamples, float strength, int /*instrumentCategory*/)
{
    // Store transient
    transientTimes.push_back(timeInSamples);
    transientStrengths.push_back(strength);

    // Keep only recent transients
    while (transientTimes.size() > MAX_TRANSIENTS)
    {
        transientTimes.pop_front();
        transientStrengths.pop_front();
    }

    // Calculate IOI with previous transient
    if (transientTimes.size() >= 2)
    {
        double ioi = transientTimes.back() - transientTimes[transientTimes.size() - 2];

        // Convert to ms
        double ioiMs = ioi * 1000.0 / sampleRate;

        // Only consider IOIs in valid tempo range
        if (ioiMs >= HISTOGRAM_MIN_PERIOD_MS && ioiMs <= HISTOGRAM_MAX_PERIOD_MS)
        {
            recentIOIs.push_back(ioi);

            // Add to histogram (weighted by transient strength)
            int bin = ioiToHistogramBin(ioi);
            if (bin >= 0 && bin < HISTOGRAM_SIZE)
            {
                tempoHistogram[bin] += strength;

                // Also add to half and double period bins (for robustness)
                int halfBin = ioiToHistogramBin(ioi / 2.0);
                int doubleBin = ioiToHistogramBin(ioi * 2.0);

                if (halfBin >= 0 && halfBin < HISTOGRAM_SIZE)
                    tempoHistogram[halfBin] += strength * 0.3f;
                if (doubleBin >= 0 && doubleBin < HISTOGRAM_SIZE)
                    tempoHistogram[doubleBin] += strength * 0.3f;
            }
        }

        // Keep IOI list bounded
        while (recentIOIs.size() > MAX_IOIS)
        {
            recentIOIs.erase(recentIOIs.begin());
        }
    }

    // Decay histogram over time
    for (auto& bin : tempoHistogram)
    {
        bin *= 0.98f;
    }

    // Update tempo estimate
    updateTempoEstimate();

    // Update last beat time (quantize to nearest beat)
    if (confidence > 0.3f)
    {
        double beatsSinceStart = timeInSamples / beatPeriodSamples;
        double nearestBeat = std::round(beatsSinceStart);
        lastBeatTime = nearestBeat * beatPeriodSamples;
    }
}

//==============================================================================
int TempoTracker::ioiToHistogramBin(double ioiSamples) const
{
    double ioiMs = ioiSamples * 1000.0 / sampleRate;

    if (ioiMs < HISTOGRAM_MIN_PERIOD_MS || ioiMs > HISTOGRAM_MAX_PERIOD_MS)
        return -1;

    // Logarithmic mapping for better resolution at common tempos
    double logMin = std::log(HISTOGRAM_MIN_PERIOD_MS);
    double logMax = std::log(HISTOGRAM_MAX_PERIOD_MS);
    double logIoi = std::log(ioiMs);

    double normalized = (logIoi - logMin) / (logMax - logMin);
    return static_cast<int>(normalized * (HISTOGRAM_SIZE - 1));
}

//==============================================================================
double TempoTracker::findDominantPeriod() const
{
    // Find peak in histogram
    int peakBin = 0;
    float peakValue = 0.0f;

    for (int i = 0; i < HISTOGRAM_SIZE; ++i)
    {
        if (tempoHistogram[i] > peakValue)
        {
            peakValue = tempoHistogram[i];
            peakBin = i;
        }
    }

    if (peakValue < 0.1f)
        return 0.0;  // No clear peak

    // Convert bin back to period
    double logMin = std::log(HISTOGRAM_MIN_PERIOD_MS);
    double logMax = std::log(HISTOGRAM_MAX_PERIOD_MS);
    double normalized = static_cast<double>(peakBin) / (HISTOGRAM_SIZE - 1);
    double logPeriod = logMin + normalized * (logMax - logMin);

    return std::exp(logPeriod);
}

//==============================================================================
double TempoTracker::correctOctaveError(double rawBpm) const
{
    // If we have a tempo hint, use it to resolve octave ambiguity
    if (hasTempoHint)
    {
        double ratio = rawBpm / tempoHint;

        // Check if raw BPM is roughly double or half of hint
        if (ratio > 1.8 && ratio < 2.2)
            return rawBpm / 2.0;
        if (ratio > 0.45 && ratio < 0.55)
            return rawBpm * 2.0;
    }

    // Prefer tempos in the 80-160 BPM range (most common)
    double adjustedBpm = rawBpm;

    while (adjustedBpm > 160.0 && adjustedBpm / 2.0 >= minBpm)
        adjustedBpm /= 2.0;

    while (adjustedBpm < 80.0 && adjustedBpm * 2.0 <= maxBpm)
        adjustedBpm *= 2.0;

    return adjustedBpm;
}

//==============================================================================
void TempoTracker::updateTempoEstimate()
{
    if (recentIOIs.size() < 4)
    {
        confidence = 0.0f;
        return;
    }

    // Find dominant period from histogram
    double dominantPeriodMs = findDominantPeriod();

    if (dominantPeriodMs < HISTOGRAM_MIN_PERIOD_MS)
    {
        confidence = 0.0f;
        return;
    }

    // Convert to BPM
    double rawBpm = periodMsToBpm(dominantPeriodMs);

    // Correct octave errors
    double correctedBpm = correctOctaveError(rawBpm);

    // Clamp to valid range
    correctedBpm = juce::jlimit(minBpm, maxBpm, correctedBpm);

    // Calculate confidence based on histogram peak strength and consistency
    float peakStrength = 0.0f;
    float totalStrength = 0.0f;

    for (const auto& bin : tempoHistogram)
    {
        if (bin > peakStrength)
            peakStrength = bin;
        totalStrength += bin;
    }

    confidence = (totalStrength > 0.01f) ? (peakStrength / totalStrength) : 0.0f;
    confidence = juce::jlimit(0.0f, 1.0f, confidence * 3.0f);  // Scale up

    // Smooth tempo changes
    if (confidence > 0.3f)
    {
        double smoothingFactor = adaptationRate * confidence;
        estimatedBpm = estimatedBpm + smoothingFactor * (correctedBpm - estimatedBpm);
        beatPeriodSamples = sampleRate * 60.0 / estimatedBpm;
    }

    // Track stability
    if (std::abs(estimatedBpm - previousBpm) < 2.0)
    {
        stableFrameCount++;
    }
    else
    {
        stableFrameCount = 0;
    }

    previousBpm = estimatedBpm;
}

//==============================================================================
TempoEstimate TempoTracker::getTempoEstimate() const
{
    TempoEstimate result;
    result.bpm = estimatedBpm;
    result.confidence = confidence;
    result.isStable = (stableFrameCount >= STABLE_THRESHOLD);
    return result;
}

//==============================================================================
float TempoTracker::getBeatPhase(double currentTimeInSamples) const
{
    if (beatPeriodSamples <= 0)
        return 0.0f;

    double timeSinceLastBeat = currentTimeInSamples - lastBeatTime;
    double phase = std::fmod(timeSinceLastBeat, beatPeriodSamples) / beatPeriodSamples;

    if (phase < 0)
        phase += 1.0;

    return static_cast<float>(phase);
}

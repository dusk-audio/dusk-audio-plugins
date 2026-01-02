/*
  ==============================================================================

    GrooveExtractor.cpp
    Groove extraction implementation

  ==============================================================================
*/

#include "GrooveExtractor.h"

//==============================================================================
// GrooveExtractor Implementation
//==============================================================================

GrooveExtractor::GrooveExtractor()
{
    extractedGroove.reset();
}

//==============================================================================
void GrooveExtractor::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    reset();
}

//==============================================================================
void GrooveExtractor::reset()
{
    recentHits.clear();
    accumulatedTimingOffsets.fill(0.0f);
    accumulatedVelocities.fill(0.0f);
    hitCounts.fill(0);
    extractedGroove.reset();
    barsAnalyzed = 0;
    lastBarStartTime = 0.0;
}

//==============================================================================
void GrooveExtractor::setAnalysisWindowBars(int bars)
{
    analysisWindowBars = juce::jlimit(1, 16, bars);
}

void GrooveExtractor::setAdaptationRate(float rate)
{
    adaptationRate = juce::jlimit(0.0f, 1.0f, rate);
}

//==============================================================================
void GrooveExtractor::notifyBarStart(double timeInSamples)
{
    // Calculate bar length from previous bar
    if (lastBarStartTime > 0.0)
    {
        barLengthSamples = timeInSamples - lastBarStartTime;
        barsAnalyzed++;

        // Update extracted groove periodically
        if (barsAnalyzed % 2 == 0)  // Every 2 bars
        {
            updateExtractedGroove();
        }
    }

    lastBarStartTime = timeInSamples;
}

//==============================================================================
int GrooveExtractor::quantizeToSlot(double timeInSamples, double barStartTime, double bpm) const
{
    if (bpm <= 0)
        return 0;

    // Calculate bar length in samples
    double beatsPerSecond = bpm / 60.0;
    double samplesPerBeat = sampleRate / beatsPerSecond;
    double samplesPerBar = samplesPerBeat * 4.0;  // Assuming 4/4 time

    // Position within bar (0 to 1)
    double positionInBar = (timeInSamples - barStartTime) / samplesPerBar;
    positionInBar = std::fmod(positionInBar, 1.0);
    if (positionInBar < 0)
        positionInBar += 1.0;

    // Convert to slot (0 to 15)
    int slot = static_cast<int>(std::round(positionInBar * ExtractedGroove::SLOTS_PER_BAR));
    slot = slot % ExtractedGroove::SLOTS_PER_BAR;

    return slot;
}

//==============================================================================
float GrooveExtractor::calculateTimingOffset(double timeInSamples, int slot,
                                               double barStartTime, double bpm) const
{
    if (bpm <= 0)
        return 0.0f;

    // Calculate exact grid position for this slot
    double beatsPerSecond = bpm / 60.0;
    double samplesPerBeat = sampleRate / beatsPerSecond;
    double samplesPerBar = samplesPerBeat * 4.0;
    double samplesPerSlot = samplesPerBar / ExtractedGroove::SLOTS_PER_BAR;

    double gridPosition = barStartTime + slot * samplesPerSlot;

    // Calculate offset in samples
    double offsetSamples = timeInSamples - gridPosition;

    // Wrap around for positions near bar boundaries
    if (offsetSamples > samplesPerBar / 2)
        offsetSamples -= samplesPerBar;
    if (offsetSamples < -samplesPerBar / 2)
        offsetSamples += samplesPerBar;

    // Convert to milliseconds
    float offsetMs = static_cast<float>(offsetSamples * 1000.0 / sampleRate);

    // Clamp to reasonable range
    return juce::jlimit(-50.0f, 50.0f, offsetMs);
}

//==============================================================================
void GrooveExtractor::addTransient(const TransientEvent& event, double currentBpm,
                                    double barStartTimeInSamples)
{
    if (currentBpm <= 0)
        return;

    // Update bar tracking if provided
    if (barStartTimeInSamples > 0)
    {
        lastBarStartTime = barStartTimeInSamples;
    }

    // Calculate bar length
    double beatsPerSecond = currentBpm / 60.0;
    double samplesPerBeat = sampleRate / beatsPerSecond;
    barLengthSamples = samplesPerBeat * 4.0;

    // Quantize to slot
    int slot = quantizeToSlot(event.timeInSamples, lastBarStartTime, currentBpm);

    // Calculate timing offset
    float timingOffsetMs = calculateTimingOffset(event.timeInSamples, slot,
                                                   lastBarStartTime, currentBpm);

    // Create groove hit
    GrooveHit hit;
    hit.slotIndex = slot;
    hit.timingOffsetMs = timingOffsetMs;
    hit.normalizedStrength = event.strength;
    hit.instrumentCategory = event.instrumentCategory;

    // Add to history
    recentHits.push_back(hit);
    while (recentHits.size() > MAX_HITS)
    {
        recentHits.pop_front();
    }

    // Accumulate for averaging
    accumulatedTimingOffsets[slot] += timingOffsetMs;
    accumulatedVelocities[slot] += event.strength;
    hitCounts[slot]++;
}

//==============================================================================
void GrooveExtractor::updateExtractedGroove()
{
    // Calculate averages for each slot
    int slotsWithData = 0;

    for (int i = 0; i < ExtractedGroove::SLOTS_PER_BAR; ++i)
    {
        if (hitCounts[i] > 0)
        {
            // Smooth update with existing values
            float newTiming = accumulatedTimingOffsets[i] / static_cast<float>(hitCounts[i]);
            float newVelocity = accumulatedVelocities[i] / static_cast<float>(hitCounts[i]);

            // Apply adaptation rate for smooth transitions
            extractedGroove.timingOffsets[i] = extractedGroove.timingOffsets[i] * (1.0f - adaptationRate)
                                                + newTiming * adaptationRate;

            // Velocity multiplier: normalize around mean velocity
            float velocityMultiplier = newVelocity * 2.0f;  // Scale 0-0.5 to 0-1, then to 0.5-1.5
            velocityMultiplier = juce::jlimit(0.5f, 1.5f, velocityMultiplier);
            extractedGroove.velocityMultipliers[i] = extractedGroove.velocityMultipliers[i] * (1.0f - adaptationRate)
                                                      + velocityMultiplier * adaptationRate;

            // Confidence based on hit count
            extractedGroove.confidence[i] = juce::jlimit(0.0f, 1.0f,
                static_cast<float>(hitCounts[i]) / static_cast<float>(analysisWindowBars * 2));

            slotsWithData++;
        }
        else
        {
            // Decay confidence for slots with no recent data
            extractedGroove.confidence[i] *= 0.9f;
        }
    }

    // Overall confidence based on how many slots have data
    extractedGroove.overallConfidence = static_cast<float>(slotsWithData) /
                                         static_cast<float>(ExtractedGroove::SLOTS_PER_BAR);

    // Groove is valid if we have enough data
    extractedGroove.isValid = (barsAnalyzed >= analysisWindowBars / 2) &&
                               (extractedGroove.overallConfidence > 0.3f);

    // Decay accumulated data for next window
    for (int i = 0; i < ExtractedGroove::SLOTS_PER_BAR; ++i)
    {
        accumulatedTimingOffsets[i] *= 0.7f;
        accumulatedVelocities[i] *= 0.7f;
        hitCounts[i] = static_cast<int>(hitCounts[i] * 0.7f);
    }
}

//==============================================================================
float GrooveExtractor::getAnalysisProgress() const
{
    return juce::jlimit(0.0f, 1.0f,
        static_cast<float>(barsAnalyzed) / static_cast<float>(analysisWindowBars));
}

//==============================================================================
// FollowModeController Implementation
//==============================================================================

FollowModeController::FollowModeController()
{
}

//==============================================================================
void FollowModeController::prepare(double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    transientDetector.prepare(sampleRate, maxBlockSize);
    tempoTracker.prepare(sampleRate);
    grooveExtractor.prepare(sampleRate);
}

//==============================================================================
void FollowModeController::reset()
{
    transientDetector.reset();
    tempoTracker.reset();
    grooveExtractor.reset();
    lastTransients.clear();
    currentBpm = 120.0;
    currentBarStart = 0.0;
    lastPositionBeats = 0.0;
}

//==============================================================================
void FollowModeController::setSensitivity(float sensitivity)
{
    transientDetector.setSensitivity(sensitivity);
}

void FollowModeController::setTempoSource(int source)
{
    tempoSource = juce::jlimit(0, 1, source);
}

//==============================================================================
void FollowModeController::updateBarTracking(double hostPositionBeats, double hostBpm)
{
    // Detect bar boundaries
    double currentBar = std::floor(hostPositionBeats / 4.0);
    double lastBar = std::floor(lastPositionBeats / 4.0);

    if (currentBar != lastBar || lastPositionBeats < 0)
    {
        // New bar started
        double barStartBeats = currentBar * 4.0;
        double beatsPerSecond = hostBpm / 60.0;
        double samplesPerBeat = sampleRate / beatsPerSecond;

        // Estimate bar start time in samples (relative calculation)
        double beatsIntoBar = hostPositionBeats - barStartBeats;
        double samplesIntoBar = beatsIntoBar * samplesPerBeat;
        // Note: We don't have absolute sample time, so this is approximate
        currentBarStart -= samplesIntoBar;

        grooveExtractor.notifyBarStart(currentBarStart);
    }

    lastPositionBeats = hostPositionBeats;
}

//==============================================================================
void FollowModeController::processAudio(const float* leftChannel, const float* rightChannel,
                                          int numSamples, double hostBpm, double hostPositionBeats)
{
    if (!followEnabled)
    {
        lastTransients.clear();
        return;
    }

    // Update bar tracking
    updateBarTracking(hostPositionBeats, hostBpm);

    // Determine which tempo to use
    if (tempoSource == 0)  // Auto
    {
        auto tempoEst = tempoTracker.getTempoEstimate();
        if (tempoEst.confidence > 0.5f && tempoEst.isStable)
        {
            currentBpm = tempoEst.bpm;
        }
        else
        {
            // Fall back to host tempo
            currentBpm = hostBpm;
            tempoTracker.setTempoHint(hostBpm);
        }
    }
    else  // Host
    {
        currentBpm = hostBpm;
        tempoTracker.setTempoHint(hostBpm);
    }

    // Detect transients
    lastTransients = transientDetector.processStereo(leftChannel, rightChannel, numSamples);

    // Process each transient
    for (const auto& transient : lastTransients)
    {
        // Add to tempo tracker
        tempoTracker.addTransient(transient.timeInSamples, transient.strength,
                                   transient.instrumentCategory);

        // Add to groove extractor
        grooveExtractor.addTransient(transient, currentBpm, currentBarStart);
    }

    // Update bar start position for next block
    currentBarStart += numSamples;
}

//==============================================================================
float FollowModeController::applyGroove(double beatPosition, float originalOffsetMs) const
{
    if (!followEnabled || grooveAmount < 0.01f)
        return originalOffsetMs;

    const auto& groove = grooveExtractor.getExtractedGroove();
    if (!groove.isValid)
        return originalOffsetMs;

    // Get position within bar (0-1)
    double positionInBar = std::fmod(beatPosition, 4.0) / 4.0;
    if (positionInBar < 0)
        positionInBar += 1.0;

    // Map to slot
    int slot = static_cast<int>(positionInBar * ExtractedGroove::SLOTS_PER_BAR);
    slot = juce::jlimit(0, ExtractedGroove::SLOTS_PER_BAR - 1, slot);

    // Apply extracted groove timing offset (weighted by confidence and amount)
    float grooveOffset = groove.timingOffsets[slot];
    float confidence = groove.confidence[slot];

    return originalOffsetMs + grooveOffset * grooveAmount * confidence;
}

//==============================================================================
float FollowModeController::applyGrooveVelocity(double beatPosition, float originalVelocity) const
{
    if (!followEnabled || grooveAmount < 0.01f)
        return originalVelocity;

    const auto& groove = grooveExtractor.getExtractedGroove();
    if (!groove.isValid)
        return originalVelocity;

    // Get position within bar (0-1)
    double positionInBar = std::fmod(beatPosition, 4.0) / 4.0;
    if (positionInBar < 0)
        positionInBar += 1.0;

    // Map to slot
    int slot = static_cast<int>(positionInBar * ExtractedGroove::SLOTS_PER_BAR);
    slot = juce::jlimit(0, ExtractedGroove::SLOTS_PER_BAR - 1, slot);

    // Apply extracted groove velocity multiplier (weighted by confidence and amount)
    float multiplier = groove.velocityMultipliers[slot];
    float confidence = groove.confidence[slot];

    // Interpolate between 1.0 (no change) and the extracted multiplier
    float effectiveMultiplier = 1.0f + (multiplier - 1.0f) * grooveAmount * confidence;

    return originalVelocity * effectiveMultiplier;
}

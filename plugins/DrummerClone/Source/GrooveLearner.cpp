#include "GrooveLearner.h"
#include <cmath>
#include <algorithm>
#include <numeric>

GrooveLearner::GrooveLearner()
{
    grooveBuffers[0].reset();
    grooveBuffers[1].reset();
    interOnsetIntervals.reserve(MAX_IOI_HISTORY);
}

juce::String GrooveLearner::getDetectedGenreString() const
{
    switch (detectedGenre.load(std::memory_order_relaxed))
    {
        case DetectedGenre::Rock:       return "Rock";
        case DetectedGenre::HipHop:     return "HipHop";
        case DetectedGenre::RnB:        return "R&B";
        case DetectedGenre::Electronic: return "Electronic";
        case DetectedGenre::Trap:       return "Trap";
        case DetectedGenre::Jazz:       return "Jazz";
        case DetectedGenre::Funk:       return "Funk";
        case DetectedGenre::Songwriter: return "Songwriter";
        case DetectedGenre::Latin:      return "Latin";
        default:                        return "Unknown";
    }
}

TempoDriftInfo GrooveLearner::getTempoDrift() const
{
    // Return cached drift info (updated during processing)
    return cachedTempoDrift;
}

void GrooveLearner::prepare(double newSampleRate, double bpm)
{
    // Validate BPM is within reasonable range (20-300 BPM)
    if (bpm < 20.0 || bpm > 300.0)
    {
        DBG("GrooveLearner::prepare: Invalid BPM " << bpm << " (must be 20.0-300.0)");
        jassertfalse;
        return;
    }

    const juce::SpinLock::ScopedLockType lock(processLock);
    sampleRate = newSampleRate;
    currentBPM = bpm;
    grooveGenerator.prepare(sampleRate);

    // Pre-allocate transient storage to avoid audio thread allocations
    // Reserve based on autoLockBars: 16 sixteenths * bars * 4 hits per position
    int lockBars = autoLockBars.load(std::memory_order_relaxed);
    size_t reserveSize = static_cast<size_t>(16 * lockBars * 4);
    reserveSize = std::max(reserveSize, DEFAULT_TRANSIENT_RESERVE);
    allTransients.clear();
    allTransients.reserve(reserveSize);

    updateGrooveTemplate();
}

void GrooveLearner::setBPM(double newBPM)
{
    // Validate BPM is within reasonable range (20-300 BPM)
    if (newBPM < 20.0 || newBPM > 300.0)
    {
        DBG("GrooveLearner::setBPM: Invalid BPM " << newBPM << " (must be 20.0-300.0)");
        jassertfalse;
        return;
    }

    const juce::SpinLock::ScopedLockType lock(processLock);
    currentBPM = newBPM;
}

void GrooveLearner::setTimeSignature(int numerator, int denominator)
{
    // Validate numerator must be positive
    if (numerator <= 0)
    {
        DBG("GrooveLearner::setTimeSignature: Invalid numerator " << numerator);
        jassertfalse;
        return;
    }

    // Validate denominator must be a positive power of two (1, 2, 4, 8, 16)
    bool isPowerOfTwo = (denominator > 0) && ((denominator & (denominator - 1)) == 0) && (denominator <= 16);
    if (!isPowerOfTwo)
    {
        DBG("GrooveLearner::setTimeSignature: Invalid denominator " << denominator << " (must be 1, 2, 4, 8, or 16)");
        jassertfalse;
        return;
    }

    const juce::SpinLock::ScopedLockType lock(processLock);
    timeSignatureNumerator = numerator;
    timeSignatureDenominator = denominator;
    // Bar length in quarter notes: 4/4 = 4, 3/4 = 3, 6/8 = 3
    barLengthInQuarters = (4.0 * static_cast<double>(numerator)) / static_cast<double>(denominator);
}

void GrooveLearner::startLearning()
{
    const juce::SpinLock::ScopedLockType lock(processLock);

    // Always clear previous data when starting a new learning session
    // This ensures no stale data remains from previous sessions
    State expected = currentState.load(std::memory_order_acquire);
    if (expected != State::Learning)
    {
        // Clear internal state (reset() without lock since we already hold it)
        // Pre-allocate to avoid audio thread allocations during learning
        int lockBars = autoLockBars.load(std::memory_order_relaxed);
        size_t reserveSize = static_cast<size_t>(16 * lockBars * 4);
        reserveSize = std::max(reserveSize, DEFAULT_TRANSIENT_RESERVE);
        allTransients.clear();
        allTransients.reserve(reserveSize);

        lastBarNumber = -1;
        hitCounts.fill(0);
        avgDeviations.fill(0.0f);

        // Reset atomic counters
        barsAnalyzed.store(0, std::memory_order_relaxed);
        totalHits.store(0, std::memory_order_relaxed);

        // Reset groove buffers
        grooveBuffers[0].reset();
        grooveBuffers[1].reset();
    }
    currentState.store(State::Learning, std::memory_order_release);
}

void GrooveLearner::lockGroove()
{
    const juce::SpinLock::ScopedLockType lock(processLock);

    if (currentState.load(std::memory_order_acquire) == State::Learning && isGrooveReady())
    {
        analyzeTransients();
        currentState.store(State::Locked, std::memory_order_release);
    }
}

void GrooveLearner::reset()
{
    const juce::SpinLock::ScopedLockType lock(processLock);

    currentState.store(State::Idle, std::memory_order_release);
    allTransients.clear();
    barsAnalyzed.store(0, std::memory_order_relaxed);
    lastBarNumber = -1;
    totalHits.store(0, std::memory_order_relaxed);
    audioHits.store(0, std::memory_order_relaxed);
    midiHits.store(0, std::memory_order_relaxed);
    hitCounts.fill(0);
    avgDeviations.fill(0.0f);
    avgVelocities.fill(0.0f);
    velocityCounts.fill(0);
    grooveBuffers[0].reset();
    grooveBuffers[1].reset();

    // Phase 3: Reset new tracking data
    interOnsetIntervals.clear();
    lastOnsetPPQ = -1.0;
    cachedTempoDrift = TempoDriftInfo();
    kickBeatHits.fill(0);
    snareBeatHits.fill(0);
    accumulatedSwing = 0.0f;
    swingSamples = 0;
    hasHalfTimeSnare = false;
    hasFourOnFloor = false;
    sixteenthNoteHits = 0;
    detectedGenre.store(DetectedGenre::Unknown, std::memory_order_relaxed);
}

void GrooveLearner::processOnsets(const std::vector<double>& onsets, double ppqPosition, int numSamples)
{
    juce::ignoreUnused(numSamples);

    if (currentState.load(std::memory_order_acquire) != State::Learning)
        return;

    const juce::SpinLock::ScopedLockType lock(processLock);

    // Double-check state after acquiring lock
    if (currentState.load(std::memory_order_relaxed) != State::Learning)
        return;

    // Calculate PPQ per sample for offset calculation
    double secondsPerBeat = 60.0 / currentBPM;
    double ppqPerSecond = 1.0 / secondsPerBeat;

    // Always track bar boundaries, even if no onsets detected
    int currentBar = getBarNumber(ppqPosition);
    if (currentBar != lastBarNumber)
    {
        if (lastBarNumber >= 0)
        {
            int bars = barsAnalyzed.fetch_add(1, std::memory_order_relaxed) + 1;

            // Update groove template periodically
            if (bars % 2 == 0)
            {
                updateGrooveTemplate();
                analyzeGenre();  // Phase 3: Update genre detection
                updateTempoDrift();  // Phase 3: Update tempo drift
            }

            // Auto-lock check - lock if we have enough data OR if we've waited too long
            int lockBars = autoLockBars.load(std::memory_order_relaxed);
            if (autoLockEnabled.load(std::memory_order_relaxed) && bars >= lockBars)
            {
                if (isGrooveReady())
                {
                    // We have enough transient data - lock with learned groove
                    analyzeTransients();
                    analyzeGenre();  // Final genre detection
                    currentState.store(State::Locked, std::memory_order_release);
                    return;
                }
                else if (bars >= lockBars * 4)
                {
                    // Fallback: if we've waited 4x the target bars with no valid groove,
                    // lock anyway with a neutral/default groove
                    DBG("GrooveLearner: No transients detected after " << bars << " bars, locking with default groove");
                    currentState.store(State::Locked, std::memory_order_release);
                    return;
                }
            }
        }
        lastBarNumber = currentBar;
    }

    // Process any detected onsets
    if (onsets.empty())
        return;

    for (double onsetTimeSeconds : onsets)
    {
        // Convert onset time to PPQ position
        double onsetPPQ = ppqPosition + (onsetTimeSeconds * ppqPerSecond);
        processTransientInternal(onsetPPQ, TransientSource::Audio, 100, -1);
        audioHits.fetch_add(1, std::memory_order_relaxed);
    }
}

void GrooveLearner::processMidiOnsets(const std::vector<std::tuple<double, int, int>>& midiOnsets, double ppqPosition)
{
    if (currentState.load(std::memory_order_acquire) != State::Learning)
        return;

    const juce::SpinLock::ScopedLockType lock(processLock);

    // Double-check state after acquiring lock
    if (currentState.load(std::memory_order_relaxed) != State::Learning)
        return;

    // Track bar boundaries (same as audio path)
    int currentBar = getBarNumber(ppqPosition);
    if (currentBar != lastBarNumber)
    {
        if (lastBarNumber >= 0)
        {
            int bars = barsAnalyzed.fetch_add(1, std::memory_order_relaxed) + 1;

            if (bars % 2 == 0)
            {
                updateGrooveTemplate();
                analyzeGenre();
                updateTempoDrift();
            }

            int lockBars = autoLockBars.load(std::memory_order_relaxed);
            if (autoLockEnabled.load(std::memory_order_relaxed) && bars >= lockBars)
            {
                if (isGrooveReady())
                {
                    analyzeTransients();
                    analyzeGenre();
                    currentState.store(State::Locked, std::memory_order_release);
                    return;
                }
                else if (bars >= lockBars * 4)
                {
                    DBG("GrooveLearner: No MIDI transients detected after " << bars << " bars, locking with default groove");
                    currentState.store(State::Locked, std::memory_order_release);
                    return;
                }
            }
        }
        lastBarNumber = currentBar;
    }

    // Process MIDI onsets
    for (const auto& [midiPPQ, velocity, note] : midiOnsets)
    {
        processTransientInternal(midiPPQ, TransientSource::Midi, velocity, note);
        midiHits.fetch_add(1, std::memory_order_relaxed);
    }
}

void GrooveLearner::processTransientInternal(double onsetPPQ, TransientSource source, int velocity, int midiNote)
{
    // Must be called with processLock held
    double secondsPerBeat = 60.0 / currentBPM;

    // Get bar and position info
    int barNum = getBarNumber(onsetPPQ);
    double ppqInBar = getPPQPositionInBar(onsetPPQ);
    int sixteenthPos = getSixteenthPosition(ppqInBar);

    // Calculate beat position (0.0 - 1.0 within each beat)
    double beatPosition = std::fmod(ppqInBar, 1.0);

    // Store the transient with source info
    TransientEvent event;
    event.ppqPosition = onsetPPQ;
    event.beatPosition = beatPosition;
    event.barNumber = barNum;
    event.sixteenthPosition = sixteenthPos;
    event.source = source;
    event.velocity = velocity;
    event.midiNote = midiNote;

    allTransients.push_back(event);

    // Phase 3: Track inter-onset intervals for tempo drift detection
    if (lastOnsetPPQ >= 0.0)
    {
        double ioi = onsetPPQ - lastOnsetPPQ;
        if (ioi > 0.0 && ioi < 4.0)  // Reasonable IOI range (< 4 beats)
        {
            if (interOnsetIntervals.size() >= MAX_IOI_HISTORY)
            {
                interOnsetIntervals.erase(interOnsetIntervals.begin());
            }
            interOnsetIntervals.push_back(ioi);
        }
    }
    lastOnsetPPQ = onsetPPQ;

    // Update hit counts
    if (sixteenthPos >= 0 && sixteenthPos < 16)
    {
        hitCounts[static_cast<size_t>(sixteenthPos)]++;
        totalHits.fetch_add(1, std::memory_order_relaxed);

        // Calculate deviation from grid (in ms)
        double gridPPQ = std::floor(ppqInBar * 4.0) / 4.0;  // Nearest 16th
        double deviationPPQ = ppqInBar - gridPPQ;
        double deviationMs = (deviationPPQ * secondsPerBeat) * 1000.0;

        // Running average of deviation
        int count = hitCounts[static_cast<size_t>(sixteenthPos)];
        avgDeviations[static_cast<size_t>(sixteenthPos)] =
            avgDeviations[static_cast<size_t>(sixteenthPos)] * (count - 1) / count +
            static_cast<float>(deviationMs) / count;

        // Phase 3: Track velocity for dynamics analysis
        if (velocity > 0)
        {
            velocityCounts[static_cast<size_t>(sixteenthPos)]++;
            int velCount = velocityCounts[static_cast<size_t>(sixteenthPos)];
            avgVelocities[static_cast<size_t>(sixteenthPos)] =
                avgVelocities[static_cast<size_t>(sixteenthPos)] * (velCount - 1) / velCount +
                static_cast<float>(velocity) / velCount;
        }

        // Phase 3: Track pattern characteristics for genre detection
        // Note: This is a simplified heuristic - real detection would use ML
        int beatNumber = sixteenthPos / 4;  // 0, 1, 2, 3 for beats 1-4

        // Track hits on pure 16th positions (odd sixteenths)
        if (sixteenthPos % 2 == 1)
        {
            sixteenthNoteHits++;
        }

        // Accumulate swing from timing deviations on upbeats
        if (sixteenthPos % 2 == 1)  // Upbeat 16ths
        {
            accumulatedSwing += static_cast<float>(deviationMs);
            swingSamples++;
        }

        // For MIDI, we can analyze note numbers for kick/snare detection
        if (source == TransientSource::Midi && midiNote >= 0)
        {
            // Common drum note ranges (GM standard)
            bool isKickRange = (midiNote >= 35 && midiNote <= 36);
            bool isSnareRange = (midiNote >= 38 && midiNote <= 40);

            if (isKickRange && beatNumber >= 0 && beatNumber < 4)
            {
                kickBeatHits[static_cast<size_t>(beatNumber)]++;
            }
            if (isSnareRange && beatNumber >= 0 && beatNumber < 4)
            {
                snareBeatHits[static_cast<size_t>(beatNumber)]++;
            }
        }
    }
}

GrooveTemplate GrooveLearner::getGrooveTemplate() const
{
    // Lock-free read from the active buffer
    int activeBuffer = activeGrooveBuffer.load(std::memory_order_acquire);
    return grooveBuffers[activeBuffer];
}

float GrooveLearner::getLearningProgress() const
{
    State state = currentState.load(std::memory_order_acquire);

    if (state == State::Locked)
        return 1.0f;

    if (state == State::Idle)
        return 0.0f;

    // Guard against division by zero
    int lockBars = autoLockBars.load(std::memory_order_relaxed);
    if (lockBars <= 0)
        return 1.0f;

    // Progress based on bars analyzed vs auto-lock target
    int bars = barsAnalyzed.load(std::memory_order_relaxed);
    return std::min(1.0f, static_cast<float>(bars) / static_cast<float>(lockBars));
}

float GrooveLearner::getConfidence() const
{
    int hits = totalHits.load(std::memory_order_relaxed);

    if (hits < MIN_HITS_FOR_VALID_GROOVE)
        return 0.0f;

    // Phase 3: Multi-factor confidence scoring
    // 1. Number of bars analyzed (more data = more confidence)
    // 2. Number of hits (more hits = better statistical significance)
    // 3. Pattern consistency (how repeatable is the pattern)
    // 4. Tempo stability (stable tempo = reliable groove)
    // 5. Multi-source agreement (if both MIDI and audio agree)

    int bars = barsAnalyzed.load(std::memory_order_relaxed);
    float barConfidence = std::min(1.0f, static_cast<float>(bars) / 4.0f);
    float hitConfidence = std::min(1.0f, static_cast<float>(hits) / 32.0f);

    // Pattern consistency from timing deviations
    float patternConfidence = calculatePatternConsistency();

    // Tempo stability factor
    float tempoConfidence = cachedTempoDrift.stability;

    // Multi-source bonus: if we have both MIDI and audio hits, that's more reliable
    float multiSourceBonus = 0.0f;
    int audio = audioHits.load(std::memory_order_relaxed);
    int midi = midiHits.load(std::memory_order_relaxed);
    if (audio > 4 && midi > 4)
    {
        multiSourceBonus = 0.1f;  // Bonus for multi-source agreement
    }

    // Weighted combination
    float confidence = (barConfidence * 0.25f +
                       hitConfidence * 0.25f +
                       patternConfidence * 0.25f +
                       tempoConfidence * 0.15f +
                       multiSourceBonus);

    return std::min(1.0f, confidence + 0.1f);  // Small baseline boost
}

float GrooveLearner::calculatePatternConsistency() const
{
    // Calculate how consistent the pattern is across all positions
    // Low variance in hit counts and deviations = high consistency

    int maxHits = *std::max_element(hitCounts.begin(), hitCounts.end());
    if (maxHits == 0)
        return 0.5f;

    // Calculate coefficient of variation for hit counts
    float sum = 0.0f;
    int nonZeroCount = 0;
    for (int count : hitCounts)
    {
        if (count > 0)
        {
            sum += static_cast<float>(count);
            nonZeroCount++;
        }
    }

    if (nonZeroCount == 0)
        return 0.5f;

    float mean = sum / nonZeroCount;
    float variance = 0.0f;
    for (int count : hitCounts)
    {
        if (count > 0)
        {
            float diff = static_cast<float>(count) - mean;
            variance += diff * diff;
        }
    }
    variance /= nonZeroCount;
    float stdDev = std::sqrt(variance);
    float cv = (mean > 0) ? (stdDev / mean) : 1.0f;

    // Lower CV = more consistent pattern
    return std::max(0.0f, 1.0f - cv);
}

bool GrooveLearner::isGrooveReady() const
{
    return totalHits.load(std::memory_order_relaxed) >= MIN_HITS_FOR_VALID_GROOVE &&
           barsAnalyzed.load(std::memory_order_relaxed) >= MIN_BARS_FOR_CONFIDENCE;
}

void GrooveLearner::analyzeTransients()
{
    // Must be called with processLock held
    if (allTransients.empty())
        return;

    updateGrooveTemplate();
}

void GrooveLearner::updateGrooveTemplate()
{
    // Must be called with processLock held
    int hits = totalHits.load(std::memory_order_relaxed);
    if (hits < MIN_HITS_FOR_VALID_GROOVE)
        return;

    // Work on the inactive buffer
    int activeBuffer = activeGrooveBuffer.load(std::memory_order_relaxed);
    int inactiveBuffer = 1 - activeBuffer;
    GrooveTemplate& groove = grooveBuffers[inactiveBuffer];

    // Calculate swing from even/odd 16th note timing
    groove.swing16 = calculateSwingFromHits();

    // Calculate 8th note swing
    float eighthSwing = 0.0f;
    int eighthPairs = 0;
    for (int i = 0; i < 16; i += 4)
    {
        if (hitCounts[static_cast<size_t>(i)] > 0 && hitCounts[static_cast<size_t>(i + 2)] > 0)
        {
            float deviation = avgDeviations[static_cast<size_t>(i + 2)];
            eighthSwing += deviation;
            eighthPairs++;
        }
    }
    if (eighthPairs > 0)
    {
        // Convert ms deviation to swing amount (rough: 30ms late = full triplet swing)
        groove.swing8 = std::clamp(eighthSwing / eighthPairs / 30.0f, 0.0f, 0.5f);
    }

    // Calculate micro-timing offsets
    for (int i = 0; i < 16; ++i)
    {
        int pos32a = i * 2;
        int pos32b = i * 2 + 1;

        if (pos32a < 32)
            groove.microOffset[static_cast<size_t>(pos32a)] = avgDeviations[static_cast<size_t>(i)];
        if (pos32b < 32)
            groove.microOffset[static_cast<size_t>(pos32b)] = avgDeviations[static_cast<size_t>(i)];
    }

    // Calculate accent pattern from hit density
    for (int i = 0; i < 16; ++i)
    {
        if (hits > 0)
        {
            // Normalize hit counts to accent values
            float maxHits = static_cast<float>(*std::max_element(hitCounts.begin(), hitCounts.end()));
            if (maxHits > 0)
            {
                groove.accentPattern[static_cast<size_t>(i)] =
                    0.3f + 0.7f * (static_cast<float>(hitCounts[static_cast<size_t>(i)]) / maxHits);
            }
        }
    }

    // Calculate energy from hit density
    int bars = barsAnalyzed.load(std::memory_order_relaxed);
    int sixteenthsPerBar = timeSignatureNumerator * 4;
    float avgHitsPerBar = static_cast<float>(hits) / std::max(1, bars);
    groove.energy = std::clamp(avgHitsPerBar / static_cast<float>(sixteenthsPerBar), 0.0f, 1.0f);

    // Density: what fraction of 16th positions are typically hit
    int activePositions = 0;
    for (int count : hitCounts)
    {
        if (count > 0) activePositions++;
    }
    groove.density = static_cast<float>(activePositions) / 16.0f;

    // Syncopation: ratio of offbeat hits to on-beat hits
    int onBeatHits = hitCounts[0] + hitCounts[4] + hitCounts[8] + hitCounts[12];
    int offBeatHits = hits - onBeatHits;
    if (hits > 0)
    {
        groove.syncopation = static_cast<float>(offBeatHits) / static_cast<float>(hits);
    }

    // Determine primary division (8th vs 16th based patterns)
    int sixteenthHits = 0;
    for (int i = 1; i < 16; i += 2)  // Odd positions = pure 16ths
    {
        sixteenthHits += hitCounts[static_cast<size_t>(i)];
    }
    groove.primaryDivision = (sixteenthHits > hits / 4) ? 16 : 8;

    groove.noteCount = hits;

    // Publish the updated groove by swapping buffers
    publishGrooveTemplate();
}

void GrooveLearner::publishGrooveTemplate()
{
    // Must be called with processLock held
    // Atomically swap the active buffer index
    int activeBuffer = activeGrooveBuffer.load(std::memory_order_relaxed);
    activeGrooveBuffer.store(1 - activeBuffer, std::memory_order_release);
}

double GrooveLearner::getPPQPositionInBar(double ppq) const
{
    // Must be called with processLock held
    if (barLengthInQuarters <= 0.0)
        return 0.0;
    return std::fmod(ppq, barLengthInQuarters);
}

int GrooveLearner::getBarNumber(double ppq) const
{
    // Must be called with processLock held
    if (barLengthInQuarters <= 0.0)
        return 0;
    return static_cast<int>(std::floor(ppq / barLengthInQuarters));
}

int GrooveLearner::getSixteenthPosition(double ppqInBar) const
{
    // Must be called with processLock held
    double sixteenthsPerQuarter = 4.0;
    int pos = static_cast<int>(std::floor(ppqInBar * sixteenthsPerQuarter));

    int maxSixteenths = static_cast<int>(barLengthInQuarters * 4.0);
    return std::clamp(pos, 0, maxSixteenths - 1) % 16;
}

float GrooveLearner::calculateSwingFromHits() const
{
    // Must be called with processLock held
    float totalDeviation = 0.0f;
    int count = 0;

    for (int i = 1; i < 16; i += 2)
    {
        if (hitCounts[static_cast<size_t>(i)] > 0)
        {
            totalDeviation += avgDeviations[static_cast<size_t>(i)];
            count++;
        }
    }

    if (count == 0)
        return 0.0f;

    float avgDev = totalDeviation / static_cast<float>(count);

    double msPerSixteenth = (60000.0 / currentBPM) / 4.0;
    float maxSwingMs = static_cast<float>(msPerSixteenth) * 0.33f;

    return std::clamp(avgDev / maxSwingMs * 0.5f, 0.0f, 0.5f);
}

void GrooveLearner::calculateMicroTimingFromHits()
{
    // Must be called with processLock held
    for (int i = 0; i < 16; ++i)
    {
        int pos32a = i * 2;
        int pos32b = i * 2 + 1;

        if (pos32a < 32)
            grooveBuffers[1 - activeGrooveBuffer.load(std::memory_order_relaxed)].microOffset[static_cast<size_t>(pos32a)] = avgDeviations[static_cast<size_t>(i)];
        if (pos32b < 32)
            grooveBuffers[1 - activeGrooveBuffer.load(std::memory_order_relaxed)].microOffset[static_cast<size_t>(pos32b)] = avgDeviations[static_cast<size_t>(i)];
    }
}

//==============================================================================
// Phase 3: Genre Detection

void GrooveLearner::analyzeGenre()
{
    // Must be called with processLock held
    // Analyze accumulated pattern characteristics to detect genre

    int hits = totalHits.load(std::memory_order_relaxed);
    if (hits < MIN_HITS_FOR_VALID_GROOVE * 2)
    {
        // Not enough data yet
        return;
    }

    // Calculate swing amount
    float avgSwing = (swingSamples > 0) ? (accumulatedSwing / swingSamples) : 0.0f;
    bool hasSwing = std::abs(avgSwing) > 5.0f;  // > 5ms average deviation = swing
    bool hasHeavySwing = std::abs(avgSwing) > 15.0f;  // > 15ms = heavy swing (jazz/shuffle)

    // Analyze kick pattern
    int kickTotal = kickBeatHits[0] + kickBeatHits[1] + kickBeatHits[2] + kickBeatHits[3];
    hasFourOnFloor = (kickTotal > 0) &&
                     (kickBeatHits[0] > kickTotal / 6) &&
                     (kickBeatHits[1] > kickTotal / 6) &&
                     (kickBeatHits[2] > kickTotal / 6) &&
                     (kickBeatHits[3] > kickTotal / 6);

    // Analyze snare pattern
    int snareTotal = snareBeatHits[0] + snareBeatHits[1] + snareBeatHits[2] + snareBeatHits[3];
    bool hasBackbeat = (snareTotal > 0) &&
                       (snareBeatHits[1] > snareTotal / 4) &&
                       (snareBeatHits[3] > snareTotal / 4);
    hasHalfTimeSnare = (snareTotal > 0) &&
                       (snareBeatHits[2] > snareTotal / 2) &&
                       (snareBeatHits[1] < snareTotal / 6) &&
                       (snareBeatHits[3] < snareTotal / 6);

    // 16th note density
    float sixteenthDensity = (hits > 0) ? (static_cast<float>(sixteenthNoteHits) / hits) : 0.0f;
    bool has16thGroove = sixteenthDensity > 0.3f;

    // Determine genre based on characteristics
    DetectedGenre genre = DetectedGenre::Unknown;

    if (hasHalfTimeSnare)
    {
        // Half-time snare = Trap
        genre = DetectedGenre::Trap;
    }
    else if (hasFourOnFloor && !hasSwing)
    {
        // Four on floor without swing = Electronic
        genre = DetectedGenre::Electronic;
    }
    else if (hasHeavySwing && hasBackbeat)
    {
        // Heavy swing with backbeat = Jazz or Funk
        if (has16thGroove)
            genre = DetectedGenre::Funk;
        else
            genre = DetectedGenre::Jazz;
    }
    else if (hasSwing && has16thGroove)
    {
        // Moderate swing with 16ths = R&B or HipHop
        if (sixteenthDensity > 0.4f)
            genre = DetectedGenre::RnB;
        else
            genre = DetectedGenre::HipHop;
    }
    else if (hasBackbeat && !hasSwing)
    {
        // Straight backbeat = Rock
        genre = DetectedGenre::Rock;
    }
    else if (!has16thGroove && hits < 20)
    {
        // Simple, sparse pattern = Songwriter
        genre = DetectedGenre::Songwriter;
    }
    else if (has16thGroove && hasSwing)
    {
        // 16th groove with swing = Funk
        genre = DetectedGenre::Funk;
    }

    detectedGenre.store(genre, std::memory_order_relaxed);
}

//==============================================================================
// Phase 3: Tempo Drift Detection

void GrooveLearner::updateTempoDrift()
{
    // Must be called with processLock held
    // Analyze inter-onset intervals to detect tempo drift

    if (interOnsetIntervals.size() < 8)
    {
        // Not enough data for tempo analysis
        cachedTempoDrift.stability = 1.0f;
        return;
    }

    // Calculate mean IOI
    double sum = std::accumulate(interOnsetIntervals.begin(), interOnsetIntervals.end(), 0.0);
    double meanIOI = sum / interOnsetIntervals.size();

    // Calculate variance
    double variance = 0.0;
    for (double ioi : interOnsetIntervals)
    {
        double diff = ioi - meanIOI;
        variance += diff * diff;
    }
    variance /= interOnsetIntervals.size();
    double stdDev = std::sqrt(variance);

    // Calculate coefficient of variation (lower = more stable)
    double cv = (meanIOI > 0) ? (stdDev / meanIOI) : 1.0;

    // Stability is inverse of CV, clamped to 0-1
    cachedTempoDrift.stability = static_cast<float>(std::max(0.0, 1.0 - cv * 4.0));

    // Calculate average tempo from IOI (assuming IOIs represent 8th notes or 16ths)
    // This is an approximation - real tempo comes from the DAW
    if (meanIOI > 0)
    {
        // Assume quarter note = 1.0 PPQ, so IOI in quarter notes
        double beatsPerMinute = 60.0 / (meanIOI * (60.0 / currentBPM));
        cachedTempoDrift.avgTempo = static_cast<float>(beatsPerMinute);
    }

    cachedTempoDrift.tempoVariance = static_cast<float>(variance);

    // Detect rushing/dragging by looking at trend in IOIs
    if (interOnsetIntervals.size() >= 16)
    {
        // Compare first half to second half
        size_t half = interOnsetIntervals.size() / 2;
        double firstHalf = 0.0, secondHalf = 0.0;

        for (size_t i = 0; i < half; ++i)
            firstHalf += interOnsetIntervals[i];
        for (size_t i = half; i < interOnsetIntervals.size(); ++i)
            secondHalf += interOnsetIntervals[i];

        firstHalf /= half;
        secondHalf /= (interOnsetIntervals.size() - half);

        // If second half IOIs are shorter, player is rushing
        // If second half IOIs are longer, player is dragging
        double driftRatio = (firstHalf > 0) ? (secondHalf / firstHalf) : 1.0;

        cachedTempoDrift.driftPercentage = static_cast<float>((1.0 - driftRatio) * 100.0);
        cachedTempoDrift.isRushing = (driftRatio < 0.97);   // > 3% faster
        cachedTempoDrift.isDragging = (driftRatio > 1.03);  // > 3% slower
    }
}

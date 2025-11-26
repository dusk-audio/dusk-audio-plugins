#include "DrummerEngine.h"
#include <cmath>

DrummerEngine::DrummerEngine(juce::AudioProcessorValueTreeState& params)
    : parameters(params)
{
    random.setSeedRandomly();

    // Load default drummer profile
    currentProfile = drummerDNA.getProfile(0);
    variationEngine.prepare(static_cast<uint32_t>(random.nextInt()));
}

void DrummerEngine::prepare(double sr, int blockSize)
{
    sampleRate = sr;
    samplesPerBlock = blockSize;

    // Reset variation engine with sample-rate-based seed for variety
    variationEngine.prepare(static_cast<uint32_t>(sr));
}

void DrummerEngine::reset()
{
    random.setSeedRandomly();
    variationEngine.reset();
    barsSinceLastFill = 0;
}

void DrummerEngine::setDrummer(int index)
{
    currentDrummer = juce::jlimit(0, drummerDNA.getNumDrummers() - 1, index);
    currentProfile = drummerDNA.getProfile(currentDrummer);

    // Reset variation engine with drummer-specific seed for unique patterns
    variationEngine.prepare(static_cast<uint32_t>(currentDrummer * 12345));
}

juce::MidiBuffer DrummerEngine::generateRegion(int bars,
                                               double bpm,
                                               int styleIndex,
                                               const GrooveTemplate& groove,
                                               float complexity,
                                               float loudness,
                                               float swingOverride)
{
    juce::MidiBuffer buffer;

    if (bars <= 0 || bpm <= 0.0)
        return buffer;

    // Get style hints
    juce::String styleName = styleNames[juce::jlimit(0, styleNames.size() - 1, styleIndex)];
    DrumMapping::StyleHints hints = DrumMapping::getStyleHints(styleName);

    // Apply drummer personality to style hints
    hints.ghostNoteProb *= currentProfile.ghostNotes * 2.0f;  // Scale by drummer's ghost note preference
    hints.syncopation *= (1.0f - currentProfile.simplicity);   // Complex drummers syncopate more

    // Apply swing - use drummer's default if no override
    GrooveTemplate effectiveGroove = groove;
    float effectiveSwing = (swingOverride > 0.0f) ? swingOverride :
                           (currentProfile.swingDefault * 100.0f + currentProfile.grooveBias * 50.0f);
    if (effectiveSwing > 0.0f)
    {
        effectiveGroove.swing16 = effectiveSwing / 200.0f;  // 0-100 -> 0-0.5
        effectiveGroove.swing8 = effectiveSwing / 250.0f;   // Slightly less for 8ths
    }

    // Apply drummer's laid-back feel to micro-timing
    if (std::abs(currentProfile.laidBack) > 0.01f)
    {
        float laidBackMs = currentProfile.laidBack * 20.0f;  // -20ms to +20ms
        for (int i = 0; i < 32; ++i)
            effectiveGroove.microOffset[static_cast<size_t>(i)] += laidBackMs;
    }

    // Get energy variation from Perlin noise for natural drift
    float energyVar = variationEngine.getEnergyVariation(static_cast<double>(barsSinceLastFill));
    float effectiveLoudness = loudness * energyVar;

    // Apply drummer's aggression to velocity range
    effectiveLoudness *= (0.7f + currentProfile.aggression * 0.6f);

    // Generate each element
    generateKickPattern(buffer, bars, bpm, hints, effectiveGroove, complexity, effectiveLoudness);
    generateSnarePattern(buffer, bars, bpm, hints, effectiveGroove, complexity, effectiveLoudness);
    generateHiHatPattern(buffer, bars, bpm, hints, effectiveGroove, complexity, effectiveLoudness);

    // Add cymbals based on complexity and drummer preferences
    float cymbalThreshold = 3.0f * (1.0f - currentProfile.crashHappiness);  // Crash-happy drummers add cymbals earlier
    if (complexity > cymbalThreshold)
    {
        // Use ride vs hi-hat based on drummer preference
        hints.useRide = variationEngine.nextRandom() < currentProfile.ridePreference;
        generateCymbals(buffer, bars, bpm, hints, effectiveGroove, complexity, effectiveLoudness);
    }

    // Add ghost notes based on complexity and drummer preference
    float ghostThreshold = 5.0f * (1.0f - currentProfile.ghostNotes);  // Ghost-loving drummers add ghosts earlier
    if (complexity > ghostThreshold && hints.ghostNoteProb > 0.0f)
    {
        generateGhostNotes(buffer, bars, bpm, hints, effectiveGroove, complexity);
    }

    // Check if we should trigger a fill
    barsSinceLastFill++;
    float fillProb = variationEngine.getFillProbability(barsSinceLastFill, currentProfile.fillHunger);
    float variationProb = variationEngine.getVariationProbability(barsSinceLastFill);

    if (variationEngine.nextRandom() < fillProb * variationProb)
    {
        // Generate a fill at the end of this bar
        int fillBeats = (variationEngine.nextRandom() < 0.7f) ? 1 : 2;  // Usually 1-beat fills
        float fillIntensity = 0.5f + currentProfile.aggression * 0.5f;

        // Use toms based on drummer's tom preference
        int startTick = (bars - 1) * ticksPerBar(bpm) + (4 - fillBeats) * PPQ;
        juce::MidiBuffer fillBuffer = generateFill(fillBeats, bpm, fillIntensity * currentProfile.tomLove, startTick);
        buffer.addEvents(fillBuffer, 0, -1, 0);

        barsSinceLastFill = 0;
    }

    return buffer;
}

void DrummerEngine::generateKickPattern(juce::MidiBuffer& buffer, int bars, double bpm,
                                        const DrumMapping::StyleHints& hints,
                                        const GrooveTemplate& groove,
                                        float complexity, float loudness)
{
    int kickNote = DrumMapping::getNoteForElement(DrumMapping::KICK);
    int barTicks = ticksPerBar(bpm);

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        // Basic pattern: kick on beats 1 and 3
        for (int beat = 0; beat < 4; ++beat)
        {
            int tick = barOffset + (beat * PPQ);

            // Always hit beats 1 and 3
            if (beat == 0 || beat == 2)
            {
                int vel = calculateVelocity(110, loudness, groove, tick);
                tick = applyMicroTiming(tick, groove, bpm);
                tick = applyHumanization(tick, 5);
                addNote(buffer, kickNote, vel, tick, PPQ / 4);
            }
            // Add variations on beats 2 and 4 based on complexity
            else if (complexity > 5.0f && shouldTrigger(hints.syncopation * 0.3f))
            {
                int vel = calculateVelocity(90, loudness, groove, tick);
                tick = applyMicroTiming(tick, groove, bpm);
                tick = applyHumanization(tick, 8);
                addNote(buffer, kickNote, vel, tick, PPQ / 4);
            }
        }

        // Add syncopated kicks based on complexity
        if (complexity > 3.0f)
        {
            // 16th note positions for syncopation
            std::vector<int> syncopationPositions = {3, 7, 11, 15};  // Upbeats

            for (int pos : syncopationPositions)
            {
                if (shouldTrigger(getComplexityProbability(complexity, hints.syncopation * 0.2f)))
                {
                    int tick = barOffset + (pos * ticksPerSixteenth());
                    int vel = calculateVelocity(85, loudness, groove, tick);
                    tick = applySwing(tick, groove.swing16, 16);
                    tick = applyMicroTiming(tick, groove, bpm);
                    tick = applyHumanization(tick, 10);
                    addNote(buffer, kickNote, vel, tick, PPQ / 4);
                }
            }
        }
    }
}

void DrummerEngine::generateSnarePattern(juce::MidiBuffer& buffer, int bars, double bpm,
                                         const DrumMapping::StyleHints& hints,
                                         const GrooveTemplate& groove,
                                         float complexity, float loudness)
{
    int snareNote = DrumMapping::getNoteForElement(DrumMapping::SNARE);
    int barTicks = ticksPerBar(bpm);

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        // Basic backbeat: snare on beats 2 and 4
        for (int beat = 0; beat < 4; ++beat)
        {
            int tick = barOffset + (beat * PPQ);

            if (beat == 1 || beat == 3)
            {
                int vel = calculateVelocity(100, loudness, groove, tick);
                tick = applyMicroTiming(tick, groove, bpm);
                tick = applyHumanization(tick, 5);
                addNote(buffer, snareNote, vel, tick, PPQ / 4);
            }
        }

        // Add snare variations at higher complexity
        if (complexity > 6.0f)
        {
            // Possible positions for additional snare hits
            std::vector<int> variationPositions = {4, 12};  // Beat 1.5 and 3.5

            for (int pos : variationPositions)
            {
                if (shouldTrigger(getComplexityProbability(complexity, 0.15f)))
                {
                    int tick = barOffset + (pos * ticksPerSixteenth());
                    int vel = calculateVelocity(70, loudness, groove, tick);
                    tick = applySwing(tick, groove.swing16, 16);
                    tick = applyHumanization(tick, 10);
                    addNote(buffer, snareNote, vel, tick, PPQ / 4);
                }
            }
        }
    }
}

void DrummerEngine::generateHiHatPattern(juce::MidiBuffer& buffer, int bars, double bpm,
                                         const DrumMapping::StyleHints& hints,
                                         const GrooveTemplate& groove,
                                         float complexity, float loudness)
{
    int closedHat = DrumMapping::getNoteForElement(DrumMapping::HI_HAT_CLOSED);
    int openHat = DrumMapping::getNoteForElement(DrumMapping::HI_HAT_OPEN);
    int barTicks = ticksPerBar(bpm);

    // Determine subdivision based on style and groove
    int division = (hints.primaryDivision == 8 || groove.primaryDivision == 8) ? 8 : 16;
    int ticksPerDiv = (division == 8) ? ticksPerEighth() : ticksPerSixteenth();
    int hitsPerBar = (division == 8) ? 8 : 16;

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        for (int hit = 0; hit < hitsPerBar; ++hit)
        {
            int tick = barOffset + (hit * ticksPerDiv);

            // Determine if this should be open hat
            bool isOpen = hints.openHats && shouldTrigger(0.1f) && (hit % 4 == 3);  // Open on upbeats

            // Calculate velocity with accent pattern
            int accentPos = hit % 16;
            float accent = groove.accentPattern[accentPos];
            int baseVel = isOpen ? 90 : 80;
            baseVel = static_cast<int>(baseVel * accent);

            int vel = calculateVelocity(baseVel, loudness, groove, tick, 8);

            // Apply swing for upbeats
            if (hit % 2 == 1)
            {
                tick = applySwing(tick, (division == 16) ? groove.swing16 : groove.swing8, division);
            }

            tick = applyMicroTiming(tick, groove, bpm);
            tick = applyHumanization(tick, 4);

            // Skip some hits at lower complexity
            if (complexity < 4.0f && division == 16 && hit % 2 == 1)
            {
                if (!shouldTrigger(complexity / 5.0f))
                    continue;
            }

            addNote(buffer, isOpen ? openHat : closedHat, vel, tick, ticksPerDiv / 2);
        }
    }
}

void DrummerEngine::generateCymbals(juce::MidiBuffer& buffer, int bars, double bpm,
                                    const DrumMapping::StyleHints& hints,
                                    const GrooveTemplate& groove,
                                    float complexity, float loudness)
{
    int crashNote = DrumMapping::getNoteForElement(DrumMapping::CRASH_1);
    int rideNote = DrumMapping::getNoteForElement(DrumMapping::RIDE);
    int barTicks = ticksPerBar(bpm);

    // Crash at beginning of pattern (with probability)
    if (shouldTrigger(0.3f))
    {
        int vel = calculateVelocity(110, loudness, groove, 0);
        addNote(buffer, crashNote, vel, 0, PPQ);
    }

    // Use ride instead of hi-hat if style suggests it
    if (hints.useRide && complexity > 4.0f)
    {
        for (int bar = 0; bar < bars; ++bar)
        {
            int barOffset = bar * barTicks;

            // Ride pattern on quarter notes or 8ths
            for (int beat = 0; beat < 4; ++beat)
            {
                int tick = barOffset + (beat * PPQ);
                int vel = calculateVelocity(85, loudness, groove, tick);
                tick = applyHumanization(tick, 5);
                addNote(buffer, rideNote, vel, tick, PPQ / 2);

                // Add 8th note ride hits
                if (complexity > 6.0f)
                {
                    tick = barOffset + (beat * PPQ) + ticksPerEighth();
                    vel = calculateVelocity(70, loudness, groove, tick);
                    tick = applySwing(tick, groove.swing8, 8);
                    tick = applyHumanization(tick, 8);
                    addNote(buffer, rideNote, vel, tick, PPQ / 4);
                }
            }
        }
    }
}

void DrummerEngine::generateGhostNotes(juce::MidiBuffer& buffer, int bars, double bpm,
                                       const DrumMapping::StyleHints& hints,
                                       const GrooveTemplate& groove,
                                       float complexity)
{
    int snareNote = DrumMapping::getNoteForElement(DrumMapping::SNARE);
    int barTicks = ticksPerBar(bpm);
    float ghostProb = hints.ghostNoteProb * (complexity / 10.0f);

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        // Ghost notes on 16th note positions (avoiding main snare hits on 2 and 4)
        std::vector<int> ghostPositions = {1, 3, 5, 7, 9, 11, 13, 15};

        for (int pos : ghostPositions)
        {
            // Skip positions too close to main snare hits (4, 8, 12, 16 -> beats 1,2,3,4)
            if (pos == 3 || pos == 7 || pos == 11 || pos == 15)
                continue;

            if (shouldTrigger(ghostProb))
            {
                int tick = barOffset + (pos * ticksPerSixteenth());

                // Ghost notes are quiet
                int vel = 30 + random.nextInt(20);  // 30-50 range

                tick = applySwing(tick, groove.swing16, 16);
                tick = applyMicroTiming(tick, groove, bpm);
                tick = applyHumanization(tick, 15);

                addNote(buffer, snareNote, vel, tick, ticksPerSixteenth() / 2);
            }
        }
    }
}

juce::MidiBuffer DrummerEngine::generateFill(int beats, double bpm, float intensity, int startTick)
{
    juce::MidiBuffer buffer;
    juce::ignoreUnused(bpm);

    int snareNote = DrumMapping::getNoteForElement(DrumMapping::SNARE);
    int tomLow = DrumMapping::getNoteForElement(DrumMapping::TOM_LOW);
    int tomMid = DrumMapping::getNoteForElement(DrumMapping::TOM_MID);
    int tomHigh = DrumMapping::getNoteForElement(DrumMapping::TOM_HIGH);
    int tomFloor = DrumMapping::getNoteForElement(DrumMapping::TOM_FLOOR);
    int crashNote = DrumMapping::getNoteForElement(DrumMapping::CRASH_1);
    int kickNote = DrumMapping::getNoteForElement(DrumMapping::KICK);

    int fillTicks = beats * PPQ;
    int division = (intensity > 0.7f) ? 16 : 8;
    int ticksPerDiv = (division == 16) ? ticksPerSixteenth() : ticksPerEighth();
    int numHits = fillTicks / ticksPerDiv;

    // Create drum set based on drummer's tom preference
    std::vector<int> drums;
    if (currentProfile.tomLove > 0.5f)
    {
        // Tom-heavy fills
        drums = {tomHigh, tomMid, tomLow, tomFloor, snareNote};
    }
    else if (currentProfile.tomLove > 0.2f)
    {
        // Mixed fills
        drums = {snareNote, tomHigh, snareNote, tomMid, tomLow};
    }
    else
    {
        // Snare-focused fills
        drums = {snareNote, snareNote, tomMid, snareNote};
    }

    int drumIndex = 0;

    // Choose fill pattern type based on variation engine
    int fillType = static_cast<int>(variationEngine.nextRandom() * 4.0f);

    for (int i = 0; i < numHits; ++i)
    {
        int tick = startTick + (i * ticksPerDiv);

        // Velocity builds through the fill
        float progress = static_cast<float>(i) / static_cast<float>(numHits);

        // Apply drummer's velocity range
        int baseVel = currentProfile.velocityFloor +
                      static_cast<int>(progress * static_cast<float>(currentProfile.velocityCeiling - currentProfile.velocityFloor) * intensity);
        int vel = juce::jlimit(1, 127, baseVel + random.nextInt(10) - 5);

        int note;
        switch (fillType)
        {
            case 0:  // Descending tom pattern
                note = drums[static_cast<size_t>(drumIndex % static_cast<int>(drums.size()))];
                if (variationEngine.nextRandom() < (0.4f + progress * 0.3f))
                    drumIndex++;
                break;

            case 1:  // Alternating snare/tom
                note = (i % 2 == 0) ? snareNote : drums[static_cast<size_t>(drumIndex % static_cast<int>(drums.size()))];
                if (i % 2 == 1)
                    drumIndex++;
                break;

            case 2:  // Single stroke roll on snare building to toms
                if (progress < 0.6f)
                    note = snareNote;
                else
                    note = drums[static_cast<size_t>(drumIndex++ % static_cast<int>(drums.size()))];
                break;

            default:  // Random pattern
                note = drums[static_cast<size_t>(random.nextInt(static_cast<int>(drums.size())))];
                break;
        }

        // Add kick on downbeats for aggressive drummers
        if (currentProfile.aggression > 0.6f && i % 4 == 0)
        {
            addNote(buffer, kickNote, vel - 10, tick, ticksPerDiv / 2);
        }

        tick = applyHumanization(tick, 8);
        addNote(buffer, note, vel, tick, ticksPerDiv / 2);
    }

    // Crash at end of fill based on drummer's crash happiness
    if (variationEngine.nextRandom() < (0.3f + currentProfile.crashHappiness * 0.7f))
    {
        int crashTick = startTick + fillTicks;
        int crashVel = currentProfile.velocityFloor +
                       static_cast<int>(static_cast<float>(currentProfile.velocityCeiling - currentProfile.velocityFloor) * 0.9f);
        addNote(buffer, crashNote, crashVel, crashTick, PPQ);

        // Add kick with crash for aggressive drummers
        if (currentProfile.aggression > 0.5f)
        {
            addNote(buffer, kickNote, crashVel - 10, crashTick, PPQ / 2);
        }
    }

    return buffer;
}

int DrummerEngine::applySwing(int tick, float swing, int division)
{
    if (swing <= 0.0f)
        return tick;

    int divisionTicks = (division == 16) ? ticksPerSixteenth() : ticksPerEighth();

    // Find position within the division pair
    int pairTicks = divisionTicks * 2;
    int posInPair = tick % pairTicks;

    // Only swing the upbeat (second note of the pair)
    if (posInPair >= divisionTicks)
    {
        // Calculate swing offset
        int swingOffset = static_cast<int>(divisionTicks * swing);
        return tick + swingOffset;
    }

    return tick;
}

int DrummerEngine::applyMicroTiming(int tick, const GrooveTemplate& groove, double bpm)
{
    if (!groove.isValid())
        return tick;

    // Get position in 32nd notes
    int thirtySecondTicks = PPQ / 8;
    int position = (tick / thirtySecondTicks) % 32;

    // Apply micro-offset (convert ms to ticks)
    float offsetMs = groove.microOffset[position];
    double ticksPerMs = (PPQ * bpm) / 60000.0;
    int offsetTicks = static_cast<int>(offsetMs * ticksPerMs);

    return tick + offsetTicks;
}

int DrummerEngine::applyHumanization(int tick, int maxJitterTicks)
{
    int jitter = random.nextInt(maxJitterTicks * 2 + 1) - maxJitterTicks;
    return std::max(0, tick + jitter);
}

int DrummerEngine::calculateVelocity(int baseVelocity, float loudness, const GrooveTemplate& groove,
                                     int tickPosition, int jitterRange)
{
    // Apply loudness scaling (0-100 -> 0.5-1.5 multiplier)
    float loudnessMultiplier = 0.5f + (loudness / 100.0f);

    // Apply groove energy
    float energyMultiplier = 0.7f + (groove.energy * 0.6f);

    // Apply accent pattern
    int sixteenthPos = (tickPosition / ticksPerSixteenth()) % 16;
    float accent = groove.accentPattern[sixteenthPos];

    // Calculate final velocity
    float vel = baseVelocity * loudnessMultiplier * energyMultiplier * accent;

    // Add random variation
    vel += random.nextInt(jitterRange * 2 + 1) - jitterRange;

    return juce::jlimit(1, 127, static_cast<int>(vel));
}

bool DrummerEngine::shouldTrigger(float probability)
{
    return random.nextFloat() < probability;
}

float DrummerEngine::getComplexityProbability(float complexity, float baseProb)
{
    // Scale probability by complexity (1-10)
    float complexityFactor = (complexity - 1.0f) / 9.0f;  // 0.0 to 1.0
    return baseProb * complexityFactor;
}

void DrummerEngine::addNote(juce::MidiBuffer& buffer, int pitch, int velocity, int startTick, int durationTicks)
{
    // Convert ticks to sample position (simplified - actual implementation would need proper sync)
    // For now, we'll use ticks directly as the sample position placeholder
    // The processor will need to convert these based on actual playback position

    auto noteOn = juce::MidiMessage::noteOn(10, pitch, static_cast<juce::uint8>(velocity));
    noteOn.setTimeStamp(startTick);
    buffer.addEvent(noteOn, startTick % samplesPerBlock);

    auto noteOff = juce::MidiMessage::noteOff(10, pitch);
    noteOff.setTimeStamp(startTick + durationTicks);
    buffer.addEvent(noteOff, (startTick + durationTicks) % samplesPerBlock);
}
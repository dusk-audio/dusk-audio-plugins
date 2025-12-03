#include "DrummerEngine.h"
#include <cmath>

DrummerEngine::DrummerEngine(juce::AudioProcessorValueTreeState& params)
    : parameters(params)
{
    random.setSeedRandomly();

    // Load default drummer profile
    currentProfile = drummerDNA.getProfile(0);
    variationEngine.prepare(static_cast<uint32_t>(random.nextInt()));

    // Get parameter pointer for usePatternLibrary setting
    // Parameter is added in PluginProcessor - if not found, default to enabled
    if (auto* param = parameters.getRawParameterValue("usePatternLibrary"))
        usePatternLibraryParam = param;

    // Pattern library is lazily initialized on first use when parameter is enabled
    // This avoids unnecessary construction overhead when disabled
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

bool DrummerEngine::isElementEnabled(DrumMapping::DrumElement element) const
{
    using DE = DrumMapping::DrumElement;

    switch (element)
    {
        case DE::KICK:
            return kitMask.kick;

        case DE::SNARE:
        case DE::SIDE_STICK:
        case DE::BRUSH_SWIRL:
        case DE::BRUSH_SWEEP:
        case DE::BRUSH_TAP:
        case DE::BRUSH_SLAP:
            return kitMask.snare;

        case DE::HI_HAT_CLOSED:
        case DE::HI_HAT_OPEN:
        case DE::HI_HAT_PEDAL:
            return kitMask.hihat;

        case DE::TOM_HIGH:
        case DE::TOM_MID:
        case DE::TOM_LOW:
        case DE::TOM_FLOOR:
            return kitMask.toms;

        case DE::CRASH_1:
        case DE::CRASH_2:
        case DE::RIDE:
        case DE::RIDE_BELL:
            return kitMask.cymbals;

        case DE::TAMBOURINE:
        case DE::COWBELL:
        case DE::CLAP:
        case DE::SHAKER:
            return kitMask.percussion;

        default:
            return true;
    }
}

void DrummerEngine::initPatternLibraryIfNeeded()
{
    // Already initialized or failed - nothing to do
    if (patternLibraryInitialized || patternLibraryFailed)
        return;

    // Check if parameter says to use pattern library
    // If parameter not found (null), default to enabling pattern library
    bool shouldUse = true;
    if (usePatternLibraryParam != nullptr)
        shouldUse = usePatternLibraryParam->load() > 0.5f;

    if (!shouldUse)
    {
        usePatternLibrary = false;
        return;
    }

    // Lazy initialization - create and load pattern library
    try
    {
        patternLibrary = std::make_unique<PatternLibrary>();
        patternVariator = std::make_unique<PatternVariator>();

        patternLibrary->loadBuiltInPatterns();

        int numPatterns = patternLibrary->getNumPatterns();
        if (numPatterns > 0)
        {
            DBG("DrummerEngine: Loaded " << numPatterns << " built-in patterns");
            usePatternLibrary = true;
            patternLibraryInitialized = true;
        }
        else
        {
            DBG("DrummerEngine: Pattern library loaded but empty, falling back to algorithmic generation");
            patternLibrary.reset();
            patternVariator.reset();
            usePatternLibrary = false;
            patternLibraryFailed = true;
        }
    }
    catch (const std::exception& e)
    {
        DBG("DrummerEngine: Failed to initialize pattern library: " << e.what());
        patternLibrary.reset();
        patternVariator.reset();
        usePatternLibrary = false;
        patternLibraryFailed = true;
    }
    catch (...)
    {
        DBG("DrummerEngine: Failed to initialize pattern library (unknown error)");
        patternLibrary.reset();
        patternVariator.reset();
        usePatternLibrary = false;
        patternLibraryFailed = true;
    }
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
                                               float swingOverride,
                                               DrumSection section,
                                               HumanizeSettings humanize,
                                               FillSettings fill)
{
    juce::MidiBuffer buffer;
    juce::ignoreUnused(styleIndex);  // Style now comes from drummer's profile

    if (bars <= 0 || bpm <= 0.0)
        return buffer;

    // Get style from drummer profile
    juce::String style = currentProfile.style;

    // Lazy-initialize pattern library if needed
    initPatternLibraryIfNeeded();

    // Try pattern library first (Phase 2 improvement)
    if (usePatternLibrary && patternLibrary && patternLibrary->getNumPatterns() > 0)
    {
        buffer = generateFromPatternLibrary(bars, bpm, style, groove,
                                            complexity, loudness, section, humanize);

        // Handle fills with pattern library
        barsSinceLastFill++;
        bool triggerFill = fill.manualTrigger;

        if (!triggerFill)
        {
            float baseFillProb = fill.frequency / 100.0f;
            float fillProb = variationEngine.getFillProbability(barsSinceLastFill, currentProfile.fillHunger);
            float variationProb = variationEngine.getVariationProbability(barsSinceLastFill);
            float combinedProb = baseFillProb * fillProb * variationProb;

            if (section == DrumSection::PreChorus || section == DrumSection::Bridge)
                combinedProb *= 1.5f;

            if (variationEngine.nextRandom() < combinedProb)
                triggerFill = true;
        }

        if (triggerFill)
        {
            int numBeats = beatsPerBar();
            int actualFillBeats = juce::jmin(fill.lengthBeats, numBeats);
            int startTick = (bars - 1) * ticksPerBar(bpm) + (numBeats - actualFillBeats) * ticksPerBeat();

            juce::MidiBuffer fillBuffer = generateFillFromLibrary(
                actualFillBeats, bpm, fill.intensity / 100.0f, style, startTick);
            buffer.addEvents(fillBuffer, 0, -1, 0);
            barsSinceLastFill = 0;
        }

        return buffer;
    }

    // Fall back to algorithmic generation if no patterns available

    // Cache humanization settings for use in generation methods
    currentHumanize = humanize;

    // Apply section-based modifiers
    float sectionDensity = getSectionDensityMultiplier(section);
    float sectionLoudness = getSectionLoudnessMultiplier(section);

    // Adjust complexity and loudness based on section
    float effectiveComplexity = complexity * sectionDensity;
    float effectiveLoudnessBase = loudness * sectionLoudness;

    // Get style hints from DRUMMER'S style (each drummer has their own style)
    // This makes each drummer play in their genre's style
    juce::String styleName = currentProfile.style;
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

    // Apply drummer's laid-back feel to micro-timing, combined with push/drag from humanization
    float laidBackMs = currentProfile.laidBack * 20.0f;  // -20ms to +20ms from drummer personality
    laidBackMs += humanize.pushDrag * 0.4f;  // Add -20ms to +20ms from push/drag control
    if (std::abs(laidBackMs) > 0.1f)
    {
        for (int i = 0; i < 32; ++i)
            effectiveGroove.microOffset[static_cast<size_t>(i)] += laidBackMs;
    }

    // Apply groove depth from humanization - scales how much the groove template affects timing
    float grooveDepthScale = humanize.grooveDepth / 100.0f;
    for (int i = 0; i < 32; ++i)
        effectiveGroove.microOffset[static_cast<size_t>(i)] *= grooveDepthScale;

    // Get energy variation from Perlin noise for natural drift
    float energyVar = variationEngine.getEnergyVariation(static_cast<double>(barsSinceLastFill));
    float effectiveLoudness = effectiveLoudnessBase * energyVar;

    // Apply drummer's aggression to velocity range
    effectiveLoudness *= (0.7f + currentProfile.aggression * 0.6f);

    // Generate each element
    generateKickPattern(buffer, bars, bpm, hints, effectiveGroove, effectiveComplexity, effectiveLoudness);
    generateSnarePattern(buffer, bars, bpm, hints, effectiveGroove, effectiveComplexity, effectiveLoudness);
    generateHiHatPattern(buffer, bars, bpm, hints, effectiveGroove, effectiveComplexity, effectiveLoudness);

    // Add crash at start of sections that need emphasis
    if (shouldAddCrashForSection(section))
    {
        int crashNote = getNoteForElement(DrumMapping::CRASH_1);
        int kickNote = getNoteForElement(DrumMapping::KICK);
        int vel = applyVelocityHumanization(static_cast<int>(110.0f * (effectiveLoudness / 100.0f)), humanize);
        addNote(buffer, crashNote, vel, 0, PPQ);
        int kickVel = std::clamp(vel - 10, 1, 127);
        addNote(buffer, kickNote, kickVel, 0, PPQ / 2);  // Kick with crash
    }

    // Add cymbals based on complexity and drummer preferences
    float cymbalThreshold = 3.0f * (1.0f - currentProfile.crashHappiness);  // Crash-happy drummers add cymbals earlier
    if (effectiveComplexity > cymbalThreshold)
    {
        // Use ride vs hi-hat based on drummer preference
        hints.useRide = variationEngine.nextRandom() < currentProfile.ridePreference;
        generateCymbals(buffer, bars, bpm, hints, effectiveGroove, effectiveComplexity, effectiveLoudness);
    }

    // Add ghost notes based on complexity and drummer preference
    float ghostThreshold = 5.0f * (1.0f - currentProfile.ghostNotes);  // Ghost-loving drummers add ghosts earlier
    if (effectiveComplexity > ghostThreshold && hints.ghostNoteProb > 0.0f)
    {
        generateGhostNotes(buffer, bars, bpm, hints, effectiveGroove, effectiveComplexity);
    }

    // Add percussion layer (shaker, tambourine, clap) based on complexity
    // Percussion kicks in at moderate complexity for groove enhancement
    float percThreshold = 4.0f;  // Start adding percussion at complexity 4+
    if (effectiveComplexity > percThreshold)
    {
        generatePercussionPattern(buffer, bars, bpm, hints, effectiveGroove, effectiveComplexity, effectiveLoudness);
    }

    // Handle fill generation
    barsSinceLastFill++;
    bool triggerFill = false;
    int fillBeats = fill.lengthBeats;
    float fillIntensity = fill.intensity / 100.0f;

    // Manual trigger overrides automatic fill
    if (fill.manualTrigger)
    {
        triggerFill = true;
    }
    else
    {
        // Calculate fill probability based on fill frequency setting and drummer personality
        float baseFillProb = fill.frequency / 100.0f;  // User's fill frequency
        float fillProb = variationEngine.getFillProbability(barsSinceLastFill, currentProfile.fillHunger);
        float variationProb = variationEngine.getVariationProbability(barsSinceLastFill);

        // Combine user setting with drummer personality
        float combinedProb = baseFillProb * fillProb * variationProb;

        // Increase fill probability at section transitions (end of Verse, Pre-Chorus, etc.)
        if (section == DrumSection::PreChorus || section == DrumSection::Bridge)
            combinedProb *= 1.5f;

        if (variationEngine.nextRandom() < combinedProb)
        {
            triggerFill = true;
        }
    }

    if (triggerFill)
    {
        // Apply drummer personality to fill intensity
        float effectiveFillIntensity = fillIntensity * (0.5f + currentProfile.aggression * 0.5f);

        // Clamp fill beats to available beats in bar
        int numBeats = beatsPerBar();
        int actualFillBeats = juce::jmin(fillBeats, numBeats);

        // Calculate start tick - fill starts at (numBeats - fillBeats) beats from bar start
        int startTick = (bars - 1) * ticksPerBar(bpm) + (numBeats - actualFillBeats) * ticksPerBeat();
        juce::MidiBuffer fillBuffer = generateFill(actualFillBeats, bpm, effectiveFillIntensity * currentProfile.tomLove, startTick);
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
    int kickNote = getNoteForElement(DrumMapping::KICK);
    int barTicks = ticksPerBar(bpm);
    int numBeats = beatsPerBar();
    int numSixteenths = sixteenthsPerBar();

    // Loudness scaling for kick (kicks should be prominent)
    float loudnessScale = 0.7f + (loudness / 100.0f) * 0.5f;

    // Apply style-specific push/pull to groove
    float stylePushPull = hints.pushPull;

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        // FOUR ON FLOOR (Electronic/House)
        if (hints.fourOnFloor)
        {
            // Kick on every beat - the foundation of house music
            for (int beat = 0; beat < numBeats; ++beat)
            {
                int tick = barOffset + (beat * ticksPerBeat());
                // Beat 1 slightly accented
                int baseVel = (beat == 0) ? 118 : 110;
                int vel = static_cast<int>(baseVel * loudnessScale);
                vel = applyVelocityHumanization(vel, currentHumanize);
                vel = juce::jlimit(80, 127, vel);
                tick = applyMicroTiming(tick, groove, bpm);
                tick = applyAdvancedHumanization(tick, currentHumanize, bpm);
                addNote(buffer, kickNote, vel, tick, PPQ / 4);
            }
        }
        else
        {
            // Standard kick pattern: beat 1 and beat 3
            for (int beat = 0; beat < numBeats; ++beat)
            {
                int tick = barOffset + (beat * ticksPerBeat());
                bool isMainKickBeat = (beat == 0) || (beat == 2 && numBeats >= 4);

                if (isMainKickBeat)
                {
                    int baseVel = (beat == 0) ? 115 : 105;
                    int vel = static_cast<int>(baseVel * loudnessScale);
                    vel = applyVelocityHumanization(vel, currentHumanize);
                    vel = juce::jlimit(60, 127, vel);
                    tick = applyMicroTiming(tick, groove, bpm);
                    tick = applyAdvancedHumanization(tick, currentHumanize, bpm);
                    addNote(buffer, kickNote, vel, tick, PPQ / 4);
                }
            }
        }

        // SYNCOPATED KICKS based on style
        if (hints.kickOnAnd || hints.kickSyncopation > 0.0f)
        {
            // "And of 4" - common in rock, creates drive going into next bar
            if (hints.kickOnAnd && numBeats >= 4 && shouldTrigger(0.5f + complexity * 0.05f))
            {
                int tick = barOffset + (15 * ticksPerSixteenth());  // Position 15 = "and of 4"
                int vel = static_cast<int>(95.0f * loudnessScale);
                vel = applyVelocityHumanization(vel, currentHumanize);
                vel = juce::jlimit(55, 110, vel);
                tick = applySwing(tick, groove.swing16 + hints.swingAmount, 16);
                tick = applyMicroTiming(tick, groove, bpm);
                addNote(buffer, kickNote, vel, tick, PPQ / 4);
            }

            // Additional syncopation based on style
            if (hints.kickSyncopation > 0.1f && complexity > 3.0f)
            {
                // Hip-hop/R&B style syncopated kicks
                // Common positions: "and of 2" (pos 6), "a of 1" (pos 3), "e of 3" (pos 9)
                std::vector<std::pair<int, float>> syncopatedPositions = {
                    {6, 0.4f},   // "and of 2" - very common
                    {3, 0.25f},  // "a of 1" - before beat 2
                    {10, 0.3f},  // "and of 3" - leading to beat 4
                };

                for (const auto& [pos, baseProb] : syncopatedPositions)
                {
                    if (pos < numSixteenths && shouldTrigger(baseProb * hints.kickSyncopation * (complexity / 10.0f)))
                    {
                        int tick = barOffset + (pos * ticksPerSixteenth());
                        int vel = static_cast<int>(85.0f * loudnessScale);
                        vel = applyVelocityHumanization(vel, currentHumanize);
                        vel = juce::jlimit(50, 100, vel);
                        tick = applySwing(tick, groove.swing16 + hints.swingAmount, 16);
                        tick = applyMicroTiming(tick, groove, bpm);
                        addNote(buffer, kickNote, vel, tick, PPQ / 4);
                    }
                }
            }
        }

        // TRAP-STYLE 808 kicks - sparse but heavy
        if (hints.halfTimeSnare && complexity > 2.0f)
        {
            // Trap kicks are often on beat 1 and random syncopated positions
            // Add occasional kick on "and of 1" for that trap bounce
            if (shouldTrigger(0.3f))
            {
                int tick = barOffset + (2 * ticksPerSixteenth());  // "and of 1"
                int vel = static_cast<int>(100.0f * loudnessScale);
                vel = juce::jlimit(70, 115, vel);
                addNote(buffer, kickNote, vel, tick, PPQ / 2);  // Longer 808 kick
            }
        }
    }
}

void DrummerEngine::generateSnarePattern(juce::MidiBuffer& buffer, int bars, double bpm,
                                         const DrumMapping::StyleHints& hints,
                                         const GrooveTemplate& groove,
                                         float complexity, float loudness)
{
    int snareNote = getNoteForElement(DrumMapping::SNARE);
    int rimNote = getNoteForElement(DrumMapping::SIDE_STICK);
    int barTicks = ticksPerBar(bpm);
    int numBeats = beatsPerBar();
    int numSixteenths = sixteenthsPerBar();

    // Loudness scaling for snare
    float loudnessScale = 0.7f + (loudness / 100.0f) * 0.5f;

    // Use rim click for certain styles (ballad, bossa)
    int mainSnareNote = hints.rimClickInstead ? rimNote : snareNote;

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        // HALF-TIME SNARE (Trap style) - snare only on beat 3
        if (hints.halfTimeSnare)
        {
            if (numBeats >= 4)
            {
                int tick = barOffset + (2 * ticksPerBeat());  // Beat 3
                int vel = static_cast<int>(115.0f * loudnessScale);
                vel = applyVelocityHumanization(vel, currentHumanize);
                vel = juce::jlimit(85, 127, vel);
                tick = applyMicroTiming(tick, groove, bpm);
                tick = applyAdvancedHumanization(tick, currentHumanize, bpm);
                addNote(buffer, mainSnareNote, vel, tick, PPQ / 4);
            }
        }
        else
        {
            // STANDARD BACKBEAT - snare on beats 2 and 4
            for (int beat = 0; beat < numBeats; ++beat)
            {
                int tick = barOffset + (beat * ticksPerBeat());
                bool isBackbeat = (beat == 1) || (beat == 3 && numBeats >= 4);

                if (isBackbeat)
                {
                    // Beat 4 slightly louder for emphasis going into next bar
                    int baseVel = (beat == 3) ? 112 : 108;
                    int vel = static_cast<int>(baseVel * loudnessScale);
                    vel = applyVelocityHumanization(vel, currentHumanize);
                    vel = juce::jlimit(70, 127, vel);
                    tick = applyMicroTiming(tick, groove, bpm);
                    tick = applyAdvancedHumanization(tick, currentHumanize, bpm);
                    addNote(buffer, mainSnareNote, vel, tick, PPQ / 4);
                }
            }
        }

        // GHOST NOTES - the secret sauce for groove!
        // Different genres use ghost notes differently
        if (hints.ghostNoteProb > 0.0f && complexity > 3.0f)
        {
            // Ghost note positions and probabilities by style
            // Hip-hop/R&B: heavy use of ghosts on the "e" and "a" (positions 1,3,5,7,9,11,13,15)
            // Rock: lighter use, mainly before backbeats
            // Trap: no ghost notes

            std::vector<std::pair<int, float>> ghostPositions;

            if (hints.ghostNoteProb >= 0.3f)
            {
                // Heavy ghost notes (Hip-hop, R&B, Neo-soul)
                // Classic "chick-a" pattern before backbeats
                ghostPositions = {
                    {3, 0.8f},   // "a" of 1 - right before beat 2 (most important!)
                    {7, 0.6f},   // "a" of 2 - after beat 2
                    {11, 0.8f},  // "a" of 3 - right before beat 4 (most important!)
                    {15, 0.5f},  // "a" of 4 - turnaround
                    {1, 0.4f},   // "e" of 1
                    {5, 0.3f},   // "e" of 2
                    {9, 0.4f},   // "e" of 3
                    {13, 0.3f},  // "e" of 4
                };
            }
            else if (hints.ghostNoteProb >= 0.15f)
            {
                // Medium ghost notes (Rock, Alternative)
                ghostPositions = {
                    {3, 0.6f},   // "a" of 1 - before beat 2
                    {11, 0.6f},  // "a" of 3 - before beat 4
                    {7, 0.3f},   // "a" of 2 - occasional
                };
            }
            else
            {
                // Light ghost notes (Songwriter, Ballad)
                ghostPositions = {
                    {3, 0.4f},   // Just before beat 2
                    {11, 0.4f},  // Just before beat 4
                };
            }

            for (const auto& [pos, baseProb] : ghostPositions)
            {
                if (pos < numSixteenths && shouldTrigger(baseProb * hints.ghostNoteProb * (complexity / 7.0f)))
                {
                    int tick = barOffset + (pos * ticksPerSixteenth());
                    // Ghost notes: 25-50 velocity (very quiet!)
                    int vel = 25 + random.nextInt(25);
                    vel = static_cast<int>(vel * loudnessScale);
                    vel = juce::jlimit(20, 55, vel);
                    tick = applySwing(tick, groove.swing16 + hints.swingAmount, 16);
                    tick = applyMicroTiming(tick, groove, bpm);
                    tick = applyAdvancedHumanization(tick, currentHumanize, bpm);
                    addNote(buffer, snareNote, vel, tick, PPQ / 8);  // Short duration
                }
            }
        }

        // SNARE VARIATIONS at high complexity
        if (complexity > 7.0f && !hints.halfTimeSnare)
        {
            // Occasional snare accent on upbeats for drive
            if (numBeats >= 4 && shouldTrigger(0.15f))
            {
                int tick = barOffset + (14 * ticksPerSixteenth());  // "and" of 4
                int vel = static_cast<int>(80.0f * loudnessScale);
                vel = applyVelocityHumanization(vel, currentHumanize);
                vel = juce::jlimit(60, 95, vel);
                tick = applySwing(tick, groove.swing16 + hints.swingAmount, 16);
                tick = applyMicroTiming(tick, groove, bpm);
                addNote(buffer, snareNote, vel, tick, PPQ / 4);
            }
        }
    }
}

void DrummerEngine::generateHiHatPattern(juce::MidiBuffer& buffer, int bars, double bpm,
                                         const DrumMapping::StyleHints& hints,
                                         const GrooveTemplate& groove,
                                         float complexity, float loudness)
{
    int closedHat = getNoteForElement(DrumMapping::HI_HAT_CLOSED);
    int openHat = getNoteForElement(DrumMapping::HI_HAT_OPEN);
    int barTicks = ticksPerBar(bpm);
    int numSixteenths = sixteenthsPerBar();

    // Loudness scaling
    float loudnessScale = 0.6f + (loudness / 100.0f) * 0.5f;

    // TRAP ROLLING HI-HATS
    if (hints.rollingHats)
    {
        generateTrapHiHats(buffer, bars, bpm, loudnessScale, complexity);
        return;
    }

    // Determine division from style hints
    int division = hints.hatDivision;
    if (division != 8 && division != 16 && division != 32)
        division = 8;  // Default to 8ths

    // At low complexity, simplify to 8ths
    if (complexity < 3.0f && division > 8)
        division = 8;

    int ticksPerDiv;
    int hitsPerBar;

    if (division == 32)
    {
        ticksPerDiv = PPQ / 8;  // 32nd notes
        hitsPerBar = numSixteenths * 2;
    }
    else if (division == 16)
    {
        ticksPerDiv = ticksPerSixteenth();
        hitsPerBar = numSixteenths;
    }
    else
    {
        ticksPerDiv = ticksPerEighth();
        hitsPerBar = numSixteenths / 2;
    }

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        for (int hit = 0; hit < hitsPerBar; ++hit)
        {
            int tick = barOffset + (hit * ticksPerDiv);

            // Determine open hat probability based on style
            bool isUpbeat;
            if (division == 8)
                isUpbeat = (hit % 2 == 1);  // Upbeats in 8th pattern
            else if (division == 16)
                isUpbeat = (hit % 2 == 1);  // Every other 16th
            else
                isUpbeat = (hit % 4 == 2);  // Every other 16th in 32nd pattern

            // Open hats - Electronic/House style has open on every upbeat 8th
            bool isOpen = false;
            if (hints.hatOpenProb > 0.0f)
            {
                if (hints.hatAccentUpbeats && division == 16 && (hit % 4 == 2))
                {
                    // House style: open hat on the "and" of each beat
                    isOpen = shouldTrigger(hints.hatOpenProb);
                }
                else if (isUpbeat)
                {
                    isOpen = shouldTrigger(hints.hatOpenProb * 0.5f);
                }
            }

            // Velocity pattern based on division and style
            int baseVel;
            if (hints.hatAccentUpbeats)
            {
                // Disco/Funk/House: accent the upbeats!
                if (division == 8)
                    baseVel = (hit % 2 == 1) ? 100 : 70;  // Upbeats louder
                else if (division == 16)
                    baseVel = ((hit % 4) == 2) ? 95 : ((hit % 4) == 0) ? 75 : 60;
                else
                    baseVel = 65;
            }
            else
            {
                // Standard: downbeats accented
                if (division == 8)
                {
                    baseVel = (hit % 2 == 0) ? 95 : 75;
                }
                else if (division == 16)
                {
                    if (hit % 4 == 0)
                        baseVel = 95;   // Downbeat
                    else if (hit % 4 == 2)
                        baseVel = 80;   // Offbeat 8th
                    else
                        baseVel = 65;   // 16th ghost
                }
                else
                {
                    // 32nd notes - very consistent, slightly lower
                    baseVel = (hit % 2 == 0) ? 75 : 65;
                }
            }

            // Open hats louder
            if (isOpen)
                baseVel = std::min(baseVel + 20, 110);

            int vel = static_cast<int>(baseVel * loudnessScale);
            vel = applyVelocityHumanization(vel, currentHumanize);
            vel = juce::jlimit(35, 115, vel);

            // Apply swing
            float swingAmt = (division == 8) ? groove.swing8 : groove.swing16;
            swingAmt += hints.swingAmount;
            if (hit % 2 == 1)
            {
                tick = applySwing(tick, swingAmt, division);
            }

            tick = applyMicroTiming(tick, groove, bpm);
            tick = applyAdvancedHumanization(tick, currentHumanize, bpm);

            // Sparseness at lower complexity (don't skip main beats though)
            if (complexity < 5.0f && division == 16)
            {
                bool isMain8th = (hit % 2 == 0);
                if (!isMain8th && !shouldTrigger(0.4f + complexity * 0.12f))
                    continue;
            }

            addNote(buffer, isOpen ? openHat : closedHat, vel, tick, ticksPerDiv / 2);
        }
    }
}

// Trap-style rolling hi-hats with triplet patterns
void DrummerEngine::generateTrapHiHats(juce::MidiBuffer& buffer, int bars, double bpm,
                                        float loudnessScale, float complexity)
{
    int closedHat = getNoteForElement(DrumMapping::HI_HAT_CLOSED);
    int barTicks = ticksPerBar(bpm);

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        // Trap hi-hats: mix of 8ths, 16ths, and 32nd rolls
        // Basic pattern: 8th notes with occasional 16th/32nd bursts

        for (int beat = 0; beat < 4; ++beat)
        {
            int beatOffset = barOffset + (beat * PPQ);

            // Decide what pattern for this beat
            float rollChance = 0.2f + complexity * 0.08f;  // More rolls at higher complexity
            bool doRoll = shouldTrigger(rollChance);
            bool doTriplet = !doRoll && shouldTrigger(0.15f);

            if (doRoll)
            {
                // 32nd note roll for this beat (8 hits)
                for (int i = 0; i < 8; ++i)
                {
                    int tick = beatOffset + (i * PPQ / 8);
                    // Velocity ramp - often crescendo or decrescendo
                    int vel = 60 + (shouldTrigger(0.5f) ? i * 5 : (7 - i) * 5);
                    vel = static_cast<int>(vel * loudnessScale);
                    vel = juce::jlimit(40, 100, vel);
                    addNote(buffer, closedHat, vel, tick, PPQ / 16);
                }
            }
            else if (doTriplet)
            {
                // Triplet pattern (3 notes in space of 2)
                for (int i = 0; i < 3; ++i)
                {
                    int tick = beatOffset + (i * PPQ / 3);
                    int vel = static_cast<int>((75 - i * 10) * loudnessScale);
                    vel = juce::jlimit(45, 90, vel);
                    addNote(buffer, closedHat, vel, tick, PPQ / 6);
                }
            }
            else
            {
                // Standard 16th pattern for this beat (4 hits)
                for (int i = 0; i < 4; ++i)
                {
                    int tick = beatOffset + (i * PPQ / 4);
                    int vel;
                    if (i == 0)
                        vel = 85;  // Downbeat
                    else if (i == 2)
                        vel = 75;  // Offbeat
                    else
                        vel = 60;  // Ghost 16ths
                    vel = static_cast<int>(vel * loudnessScale);
                    vel = juce::jlimit(40, 95, vel);
                    addNote(buffer, closedHat, vel, tick, PPQ / 8);
                }
            }
        }
    }
}

void DrummerEngine::generateCymbals(juce::MidiBuffer& buffer, int bars, double bpm,
                                    const DrumMapping::StyleHints& hints,
                                    const GrooveTemplate& groove,
                                    float complexity, float loudness)
{
    int crashNote = getNoteForElement(DrumMapping::CRASH_1);
    int rideNote = getNoteForElement(DrumMapping::RIDE);
    int barTicks = ticksPerBar(bpm);
    int numBeats = beatsPerBar();

    // Crash at beginning of pattern (with probability)
    if (shouldTrigger(0.3f))
    {
        int vel = calculateVelocity(110, loudness, groove, 0);
        vel = applyVelocityHumanization(vel, currentHumanize);
        addNote(buffer, crashNote, vel, 0, PPQ);
    }

    // Use ride instead of hi-hat if style suggests it
    if (hints.useRide && complexity > 4.0f)
    {
        for (int bar = 0; bar < bars; ++bar)
        {
            int barOffset = bar * barTicks;

            // Ride pattern on quarter notes or 8ths
            for (int beat = 0; beat < numBeats; ++beat)
            {
                int tick = barOffset + (beat * ticksPerBeat());
                int vel = calculateVelocity(85, loudness, groove, tick);
                vel = applyVelocityHumanization(vel, currentHumanize);
                tick = applyAdvancedHumanization(tick, currentHumanize, bpm);
                addNote(buffer, rideNote, vel, tick, PPQ / 2);

                // Add 8th note ride hits
                if (complexity > 6.0f)
                {
                    tick = barOffset + (beat * ticksPerBeat()) + ticksPerEighth();
                    vel = calculateVelocity(70, loudness, groove, tick);
                    vel = applyVelocityHumanization(vel, currentHumanize);
                    tick = applySwing(tick, groove.swing8, 8);
                    tick = applyAdvancedHumanization(tick, currentHumanize, bpm);
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
    int snareNote = getNoteForElement(DrumMapping::SNARE);
    int barTicks = ticksPerBar(bpm);
    float ghostProb = hints.ghostNoteProb * (complexity / 10.0f);

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        // Ghost notes on 16th note positions (avoiding main snare hits)
        // Dynamic based on time signature
        int numSixteenths = sixteenthsPerBar();

        for (int pos = 0; pos < numSixteenths; ++pos)
        {
            // Skip positions on the beat (main snare/kick hits)
            // Every 4th sixteenth is a beat in x/4 time
            if (pos % 4 == 0)
                continue;

            // Also skip the position just before beats (too close to main hits)
            if ((pos + 1) % 4 == 0)
                continue;

            if (shouldTrigger(ghostProb))
            {
                int tick = barOffset + (pos * ticksPerSixteenth());

                // Ghost notes are quiet
                int vel = 30 + random.nextInt(20);  // 30-50 range
                vel = applyVelocityHumanization(vel, currentHumanize);

                tick = applySwing(tick, groove.swing16, 16);
                tick = applyMicroTiming(tick, groove, bpm);
                tick = applyAdvancedHumanization(tick, currentHumanize, bpm);

                addNote(buffer, snareNote, vel, tick, ticksPerSixteenth() / 2);
            }
        }
    }
}

void DrummerEngine::generatePercussionPattern(juce::MidiBuffer& buffer, int bars, double bpm,
                                               const DrumMapping::StyleHints& hints,
                                               const GrooveTemplate& groove,
                                               float complexity, float loudness)
{
    // Percussion is style-dependent:
    // - Rock/Alternative: Tambourine on 8ths or quarters
    // - HipHop/R&B: Shaker on 16ths, occasional tambourine
    // - Electronic/Trap: Shaker patterns, clap layering
    // - Songwriter: Light tambourine or nothing

    if (!kitMask.percussion)
        return;  // Percussion disabled

    // Note: Using addNoteFiltered with DrumElement directly instead of pre-fetched notes
    // This ensures proper kit mask filtering is applied

    int barTicks = ticksPerBar(bpm);
    float loudnessScale = loudness / 100.0f;
    int numSixteenths = sixteenthsPerBar();

    // Determine percussion type based on drummer's style
    juce::String style = currentProfile.style;
    bool useShaker = (style == "HipHop" || style == "R&B" || style == "Electronic" || style == "Trap");
    bool useTambourine = (style == "Rock" || style == "Alternative" || style == "Songwriter");
    bool useCowbell = (style == "Rock" && complexity > 6.0f);  // Cowbell for complex rock
    bool useClap = (style == "Electronic" || style == "Trap" || style == "HipHop");

    // Probability scales with complexity
    float percProb = 0.3f + (complexity / 10.0f) * 0.4f;  // 30-70% base probability

    for (int bar = 0; bar < bars; ++bar)
    {
        int barOffset = bar * barTicks;

        // Shaker pattern: 16th notes with accents
        if (useShaker && shouldTrigger(percProb))
        {
            for (int pos = 0; pos < numSixteenths; ++pos)
            {
                // Skip some positions for groove
                float skipProb = (pos % 4 == 0) ? 0.1f : 0.3f;  // Less skipping on beats
                if (shouldTrigger(skipProb))
                    continue;

                int tick = barOffset + (pos * ticksPerSixteenth());

                // Accent pattern: louder on beats, softer on off-beats
                bool isAccent = (pos % 4 == 0) || (pos % 4 == 2);  // Quarter and eighth positions
                int baseVel = isAccent ? 55 : 35;
                baseVel = static_cast<int>(baseVel * loudnessScale);
                int vel = applyVelocityHumanization(baseVel, currentHumanize);

                tick = applySwing(tick, groove.swing16, 16);
                tick = applyAdvancedHumanization(tick, currentHumanize, bpm);

                addNoteFiltered(buffer, DrumMapping::SHAKER, vel, tick, ticksPerSixteenth() / 2);
            }
        }

        // Tambourine pattern: 8th notes
        if (useTambourine && shouldTrigger(percProb * 0.7f))
        {
            int numEighths = numSixteenths / 2;
            for (int pos = 0; pos < numEighths; ++pos)
            {
                // Skip some positions for variety
                if (shouldTrigger(0.2f))
                    continue;

                int tick = barOffset + (pos * ticksPerEighth());

                // Accent on upbeats for drive
                bool isUpbeat = (pos % 2 == 1);
                int baseVel = isUpbeat ? 65 : 50;
                baseVel = static_cast<int>(baseVel * loudnessScale);
                int vel = applyVelocityHumanization(baseVel, currentHumanize);

                tick = applySwing(tick, groove.swing8, 8);
                tick = applyAdvancedHumanization(tick, currentHumanize, bpm);

                addNoteFiltered(buffer, DrumMapping::TAMBOURINE, vel, tick, ticksPerEighth() / 2);
            }
        }

        // Cowbell: quarters or halves (rock/disco)
        if (useCowbell && shouldTrigger(percProb * 0.5f))
        {
            int numBeats = beatsPerBar();
            for (int beat = 0; beat < numBeats; ++beat)
            {
                // Only on certain beats
                if (beat % 2 != 0)
                    continue;

                int tick = barOffset + (beat * ticksPerBeat());
                int baseVel = static_cast<int>(60 * loudnessScale);
                int vel = applyVelocityHumanization(baseVel, currentHumanize);

                tick = applyAdvancedHumanization(tick, currentHumanize, bpm);

                addNoteFiltered(buffer, DrumMapping::COWBELL, vel, tick, ticksPerBeat() / 2);
            }
        }

        // Clap layering: with snare hits (electronic/trap)
        if (useClap && shouldTrigger(percProb * 0.6f))
        {
            // Claps typically on 2 and 4 (or just 3 for trap half-time)
            int numBeats = beatsPerBar();
            for (int beat = 0; beat < numBeats; ++beat)
            {
                bool shouldClap = hints.halfTimeSnare ? (beat == 2) : (beat == 1 || beat == 3);
                if (!shouldClap)
                    continue;

                int tick = barOffset + (beat * ticksPerBeat());
                int baseVel = static_cast<int>(70 * loudnessScale);
                int vel = applyVelocityHumanization(baseVel, currentHumanize);

                // Slight offset from snare for layering effect
                tick += random.nextInt(10) - 5;
                tick = applyAdvancedHumanization(tick, currentHumanize, bpm);

                addNoteFiltered(buffer, DrumMapping::CLAP, vel, tick, ticksPerBeat() / 4);
            }
        }
    }
}

juce::MidiBuffer DrummerEngine::generateFill(int beats, double bpm, float intensity, int startTick)
{
    juce::MidiBuffer buffer;
    juce::ignoreUnused(bpm);

    int snareNote = getNoteForElement(DrumMapping::SNARE);
    int tomLow = getNoteForElement(DrumMapping::TOM_LOW);
    int tomMid = getNoteForElement(DrumMapping::TOM_MID);
    int tomHigh = getNoteForElement(DrumMapping::TOM_HIGH);
    int tomFloor = getNoteForElement(DrumMapping::TOM_FLOOR);
    int crashNote = getNoteForElement(DrumMapping::CRASH_1);
    int kickNote = getNoteForElement(DrumMapping::KICK);

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

        // Apply humanization
        vel = applyVelocityHumanization(vel, currentHumanize);
        int humanizedTick = applyAdvancedHumanization(tick, currentHumanize, 120.0);  // Use approx BPM for fills

        // Add kick on downbeats for aggressive drummers
        if (currentProfile.aggression > 0.6f && i % 4 == 0)
        {
            int kickVel = applyVelocityHumanization(vel - 10, currentHumanize);
            addNote(buffer, kickNote, kickVel, humanizedTick, ticksPerDiv / 2);
        }

        addNote(buffer, note, vel, humanizedTick, ticksPerDiv / 2);
    }

    // Crash at end of fill based on drummer's crash happiness
    if (variationEngine.nextRandom() < (0.3f + currentProfile.crashHappiness * 0.7f))
    {
        int crashTick = startTick + fillTicks;
        int crashVel = currentProfile.velocityFloor +
                       static_cast<int>(static_cast<float>(currentProfile.velocityCeiling - currentProfile.velocityFloor) * 0.9f);
        crashVel = applyVelocityHumanization(crashVel, currentHumanize);
        addNote(buffer, crashNote, crashVel, crashTick, PPQ);

        // Add kick with crash for aggressive drummers
        if (currentProfile.aggression > 0.5f)
        {
            int kickVel = applyVelocityHumanization(crashVel - 10, currentHumanize);
            addNote(buffer, kickNote, kickVel, crashTick, PPQ / 2);
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
    // Use drummer's velocity range from their profile
    int velFloor = currentProfile.velocityFloor;
    int velCeiling = currentProfile.velocityCeiling;
    int velRange = velCeiling - velFloor;

    // Apply loudness scaling (0-100 maps to floor-ceiling range)
    // Low loudness = closer to floor, high loudness = closer to ceiling
    float loudnessNorm = loudness / 100.0f;

    // Apply drummer's aggression to velocity curve
    // High aggression = steeper curve, more dynamic
    // Low aggression = flatter curve, more compressed dynamics
    float aggression = currentProfile.aggression;
    float curvedLoudness = std::pow(loudnessNorm, 1.0f + (1.0f - aggression));

    // Calculate base velocity within drummer's range
    float velInRange = velFloor + (curvedLoudness * velRange * (baseVelocity / 127.0f));

    // Apply groove energy (scaled by aggression)
    float energyMultiplier = 1.0f + (groove.energy - 0.5f) * aggression * 0.4f;
    velInRange *= energyMultiplier;

    // Apply accent pattern with drummer personality
    // Aggressive drummers have stronger accents
    int sixteenthPos = (tickPosition / ticksPerSixteenth()) % 16;
    float accent = groove.accentPattern[sixteenthPos];
    float accentStrength = 0.7f + aggression * 0.6f;  // 0.7-1.3 range
    float accentMod = 1.0f + (accent - 1.0f) * accentStrength;
    velInRange *= accentMod;

    // Add random variation (less for laid-back drummers)
    float variationScale = 1.0f - std::abs(currentProfile.laidBack) * 0.5f;
    int jitter = static_cast<int>(random.nextInt(jitterRange * 2 + 1) - jitterRange) * variationScale;
    velInRange += jitter;

    return juce::jlimit(1, 127, static_cast<int>(velInRange));
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
    // Store tick position in the timestamp - the processor will convert to sample positions
    // based on the current playhead position and tempo
    // Using tick value directly as sample position for storage (processor does conversion)

    auto noteOn = juce::MidiMessage::noteOn(10, pitch, static_cast<juce::uint8>(velocity));
    noteOn.setTimeStamp(static_cast<double>(startTick));
    buffer.addEvent(noteOn, startTick);  // Sample position = tick for now, converted by processor

    auto noteOff = juce::MidiMessage::noteOff(10, pitch);
    noteOff.setTimeStamp(static_cast<double>(startTick + durationTicks));
    buffer.addEvent(noteOff, startTick + durationTicks);
}

void DrummerEngine::addNoteFiltered(juce::MidiBuffer& buffer, DrumMapping::DrumElement element, int velocity, int startTick, int durationTicks)
{
    // Skip if this element is disabled by kit mask
    if (!isElementEnabled(element))
        return;

    int pitch = getNoteForElement(element);
    addNote(buffer, pitch, velocity, startTick, durationTicks);
}

//==============================================================================
// Section-based modifiers

float DrummerEngine::getSectionDensityMultiplier(DrumSection section) const
{
    // Returns a multiplier for pattern complexity based on section type
    switch (section)
    {
        case DrumSection::Intro:      return 0.5f;   // Sparse intro
        case DrumSection::Verse:      return 0.8f;   // Standard verse
        case DrumSection::PreChorus:  return 1.0f;   // Building energy
        case DrumSection::Chorus:     return 1.2f;   // Full energy
        case DrumSection::Bridge:     return 0.7f;   // Pull back a bit
        case DrumSection::Breakdown:  return 0.4f;   // Minimal
        case DrumSection::Outro:      return 0.6f;   // Winding down
        default:                      return 1.0f;
    }
}

float DrummerEngine::getSectionLoudnessMultiplier(DrumSection section) const
{
    // Returns a multiplier for loudness based on section type
    switch (section)
    {
        case DrumSection::Intro:      return 0.7f;   // Quieter intro
        case DrumSection::Verse:      return 0.85f;  // Medium verse
        case DrumSection::PreChorus:  return 0.95f;  // Building
        case DrumSection::Chorus:     return 1.1f;   // Loud chorus
        case DrumSection::Bridge:     return 0.8f;   // Pull back
        case DrumSection::Breakdown:  return 0.6f;   // Quiet breakdown
        case DrumSection::Outro:      return 0.75f;  // Fading out
        default:                      return 1.0f;
    }
}

bool DrummerEngine::shouldAddCrashForSection(DrumSection section)
{
    // Crash cymbal at the start of certain sections
    switch (section)
    {
        case DrumSection::Chorus:     return true;   // Always crash on chorus
        case DrumSection::Bridge:     return variationEngine.nextRandom() < 0.7f;  // Usually crash on bridge
        case DrumSection::Outro:      return variationEngine.nextRandom() < 0.5f;  // Sometimes on outro
        case DrumSection::Intro:      return false;
        case DrumSection::Verse:      return false;
        case DrumSection::PreChorus:  return false;
        case DrumSection::Breakdown:  return false;
    }
    return false;
}

//==============================================================================
// Humanization helpers

int DrummerEngine::applyAdvancedHumanization(int tick, const HumanizeSettings& humanize, double bpm)
{
    // Calculate timing variation in ticks
    // 100% timing variation = up to ±30ms of random variation
    float maxVariationMs = (humanize.timingVariation / 100.0f) * 30.0f;

    // Convert ms to ticks
    double ticksPerMs = (PPQ * bpm) / 60000.0;
    int maxVariationTicks = static_cast<int>(maxVariationMs * ticksPerMs);

    if (maxVariationTicks <= 0)
        return tick;

    // Generate random variation
    int variation = random.nextInt(maxVariationTicks * 2 + 1) - maxVariationTicks;

    return std::max(0, tick + variation);
}

int DrummerEngine::applyVelocityHumanization(int baseVel, const HumanizeSettings& humanize)
{
    // Calculate velocity variation
    // 100% velocity variation = up to ±20 velocity units of random variation
    float maxVariation = (humanize.velocityVariation / 100.0f) * 20.0f;
    int maxVariationInt = static_cast<int>(maxVariation);

    if (maxVariationInt <= 0)
        return juce::jlimit(1, 127, baseVel);

    // Generate random variation
    int variation = random.nextInt(maxVariationInt * 2 + 1) - maxVariationInt;

    return juce::jlimit(1, 127, baseVel + variation);
}

//==============================================================================
// Step sequencer pattern generation

juce::MidiBuffer DrummerEngine::generateFromStepSequencer(
    const std::array<std::array<std::pair<bool, float>, STEP_SEQUENCER_STEPS>, STEP_SEQUENCER_LANES>& pattern,
    double bpm,
    HumanizeSettings humanize)
{
    juce::MidiBuffer buffer;

    if (bpm <= 0.0)
        return buffer;

    // Cache humanization settings
    currentHumanize = humanize;

    // Map step sequencer lanes to MIDI notes (order must match StepSeqLane enum)
    // Use configurable note map instead of static defaults
    static_assert(STEP_SEQUENCER_LANES == 8, "STEP_SEQUENCER_LANES must be 8 for laneToNote array");
    const std::array<int, STEP_SEQUENCER_LANES> laneToNote = {{
        getNoteForElement(DrumMapping::KICK),           // StepSeqLane::SEQ_KICK
        getNoteForElement(DrumMapping::SNARE),          // StepSeqLane::SEQ_SNARE
        getNoteForElement(DrumMapping::HI_HAT_CLOSED),  // StepSeqLane::SEQ_CLOSED_HIHAT
        getNoteForElement(DrumMapping::HI_HAT_OPEN),    // StepSeqLane::SEQ_OPEN_HIHAT
        getNoteForElement(DrumMapping::CLAP),           // StepSeqLane::SEQ_CLAP
        getNoteForElement(DrumMapping::TOM_HIGH),       // StepSeqLane::SEQ_TOM1
        getNoteForElement(DrumMapping::TOM_MID),        // StepSeqLane::SEQ_TOM2
        getNoteForElement(DrumMapping::CRASH_1)         // StepSeqLane::SEQ_CRASH
    }};

    // STEP_SEQUENCER_STEPS steps = 1 bar of 16th notes
    int ticksPerStep = ticksPerSixteenth();

    // Iterate through each lane and step
    for (int lane = 0; lane < STEP_SEQUENCER_LANES; ++lane)
    {
        int note = laneToNote[lane];

        for (int step = 0; step < STEP_SEQUENCER_STEPS; ++step)
        {
            const auto& [active, velocity] = pattern[static_cast<size_t>(lane)][static_cast<size_t>(step)];

            if (active)
            {
                int tick = step * ticksPerStep;

                // Calculate velocity (0.0-1.0 -> 1-127)
                int vel = static_cast<int>(velocity * 127.0f);
                vel = juce::jlimit(1, 127, vel);

                // Apply humanization
                vel = applyVelocityHumanization(vel, humanize);
                tick = applyAdvancedHumanization(tick, humanize, bpm);

                // Add the note
                addNote(buffer, note, vel, tick, ticksPerStep / 2);
            }
        }
    }

    return buffer;
}

//==============================================================================
// Pattern Library-based generation (Phase 2)

juce::MidiBuffer DrummerEngine::generateFromPatternLibrary(int bars, double bpm,
                                                            const juce::String& style,
                                                            const GrooveTemplate& groove,
                                                            float complexity, float loudness,
                                                            DrumSection section,
                                                            HumanizeSettings humanize)
{
    juce::MidiBuffer buffer;

    // Calculate target characteristics from parameters
    float targetEnergy = loudness / 100.0f;
    float targetDensity = complexity / 10.0f;

    // Apply section modifiers
    float sectionDensity = getSectionDensityMultiplier(section);
    float sectionLoudness = getSectionLoudnessMultiplier(section);
    targetEnergy *= sectionLoudness;
    targetDensity *= sectionDensity;

    // Clamp to valid range
    targetEnergy = juce::jlimit(0.0f, 1.0f, targetEnergy);
    targetDensity = juce::jlimit(0.0f, 1.0f, targetDensity);

    // Select best matching pattern
    int patternIdx = patternLibrary->selectBestPattern(style, targetEnergy, targetDensity, true);

    if (patternIdx < 0)
    {
        // No pattern found - fall back to any pattern
        patternIdx = patternLibrary->selectBestPattern("", targetEnergy, targetDensity, false);
    }

    if (patternIdx < 0)
    {
        // Still nothing - return empty buffer (will fall back to algorithmic)
        return buffer;
    }

    // Get the pattern and make a working copy
    PatternPhrase pattern = patternLibrary->getPattern(patternIdx);

    // Apply drummer personality modifications
    float energyScale = 0.7f + (currentProfile.aggression * 0.6f);
    patternVariator->scaleEnergy(pattern, energyScale * (loudness / 75.0f));

    // Adjust ghost notes based on drummer and complexity
    float targetGhostDensity = currentProfile.ghostNotes * (complexity / 10.0f);
    patternVariator->adjustGhostNotes(pattern, targetGhostDensity);

    // Apply swing if drummer prefers it or groove has swing
    float swingAmount = std::max(groove.swing16, currentProfile.swingDefault);
    if (swingAmount > 0.0f)
    {
        patternVariator->applySwing(pattern, swingAmount, 16);
    }

    // Apply humanization with per-instrument characteristics
    patternVariator->humanize(pattern, humanize.timingVariation, humanize.velocityVariation, bpm);

    // Generate MIDI for each bar
    // Generate MIDI for each bar
    int barTicks = ticksPerBar(bpm);

    for (int bar = 0; bar < bars; ++bar)
    {
        int tickOffset = bar * barTicks;

        // Apply slight variation to each repetition
        PatternPhrase barPattern = pattern;
        if (bar > 0)
        {
            // Small variations for repeated bars
            patternVariator->applyVelocityVariation(barPattern, 0.05f, true);
            patternVariator->applyTimingVariation(barPattern, 2.0f, bpm, true);
            patternVariator->applySubstitutions(barPattern, 0.03f);
        }

        // Convert pattern to MIDI
        juce::MidiBuffer barBuffer = patternToMidi(barPattern, bpm, groove, humanize, tickOffset);
        buffer.addEvents(barBuffer, 0, -1, 0);
    }

    // Add crash at start of certain sections
    if (shouldAddCrashForSection(section))
    {
        int crashNote = getNoteForElement(DrumMapping::CRASH_1);
        int kickNote = getNoteForElement(DrumMapping::KICK);
        int vel = static_cast<int>(110.0f * targetEnergy);
        vel = applyVelocityHumanization(vel, humanize);
        addNote(buffer, crashNote, vel, 0, PPQ);
        addNote(buffer, kickNote, std::clamp(vel - 10, 1, 127), 0, PPQ / 2);
    }

    return buffer;
}

juce::MidiBuffer DrummerEngine::generateFillFromLibrary(int beats, double bpm,
                                                         float intensity,
                                                         const juce::String& style,
                                                         int startTick)
{
    juce::MidiBuffer buffer;

    // Phase 4: Use context-aware fill selection
    // Determine fill context based on current state
    FillContext context = FillContext::Standard;
    float nextSectionEnergy = intensity;  // Default to current intensity

    // Determine context based on bars since last fill and section
    if (barsSinceLastFill >= 7)
    {
        // Long time since fill - likely transitioning to new section
        context = FillContext::SectionEnd;
        nextSectionEnergy = intensity * 1.1f;  // Slight energy increase expected
    }
    else if (barsSinceLastFill >= 3 && intensity > 0.7f)
    {
        // High intensity after several bars - likely a build up
        context = FillContext::BuildUp;
        nextSectionEnergy = intensity * 1.2f;
    }
    else if (intensity < 0.3f)
    {
        // Low intensity - breakdown or sparse section
        context = FillContext::Breakdown;
    }

    // Select a fill from the library using context-aware selection
    int fillIdx = patternLibrary->selectContextualFill(style, beats, intensity, context, nextSectionEnergy);

    if (fillIdx < 0)
    {
        // Fall back to algorithmic fill
        return generateFill(beats, bpm, intensity * currentProfile.tomLove, startTick);
    }

    PatternPhrase fill = patternLibrary->getPattern(fillIdx);

    // Phase 4: Generate and add leading tones for smooth transition
    if (context == FillContext::BuildUp || context == FillContext::SectionEnd)
    {
        std::vector<DrumHit> leadingTones = patternLibrary->generateLeadingTones(fill, 1, bpm);

        // Add leading tones at their negative tick positions relative to fill start
        for (const auto& hit : leadingTones)
        {
            int absoluteTick = startTick + hit.tick;  // hit.tick is negative
            if (absoluteTick >= 0)
            {
                int pitch = getNoteForElement(hit.element);
                int vel = applyVelocityHumanization(hit.velocity, currentHumanize);
                addNote(buffer, pitch, vel, absoluteTick, hit.duration);
            }
        }
    }

    // Scale fill energy based on intensity and drummer aggression
    float energyScale = intensity * (0.7f + currentProfile.aggression * 0.6f);
    patternVariator->scaleEnergy(fill, energyScale);

    // Light humanization on fills (keep them tight)
    patternVariator->humanize(fill, 10.0f, 15.0f, bpm);

    // Convert to MIDI at the correct position
    HumanizeSettings fillHumanize;
    fillHumanize.timingVariation = 10.0f;
    fillHumanize.velocityVariation = 15.0f;

    GrooveTemplate emptyGroove;

    // Scale fill to requested length
    int requestedTicks = beats * PPQ;
    int fillTicks = fill.bars * PPQ * fill.timeSigNum * 4 / fill.timeSigDenom;

    // If fill is longer than requested, trim from beginning
    int tickOffset = startTick;
    if (fillTicks > requestedTicks)
    {
        // Start from later in the fill
        int trimTicks = fillTicks - requestedTicks;
        for (auto& hit : fill.hits)
        {
            hit.tick -= trimTicks;
        }
        // Remove hits before tick 0
        fill.hits.erase(
            std::remove_if(fill.hits.begin(), fill.hits.end(),
                           [](const DrumHit& h) { return h.tick < 0; }),
            fill.hits.end());
    }

    buffer.addEvents(patternToMidi(fill, bpm, emptyGroove, fillHumanize, tickOffset), 0, -1, 0);

    // Phase 4: Context-aware crash handling
    // Add crash at end of fill based on context and drummer personality
    float crashProbability = 0.3f + currentProfile.crashHappiness * 0.5f;

    // Increase crash probability for certain contexts
    if (context == FillContext::BuildUp || context == FillContext::TensionRelease)
        crashProbability = 0.9f;
    else if (context == FillContext::SectionEnd)
        crashProbability = 0.75f;
    else if (context == FillContext::Breakdown)
        crashProbability = 0.1f;  // Rare crash in breakdown

    if (variationEngine.nextRandom() < crashProbability)
    {
        int crashTick = startTick + requestedTicks;
        int crashNote = getNoteForElement(DrumMapping::CRASH_1);
        int kickNote = getNoteForElement(DrumMapping::KICK);
        int crashVel = static_cast<int>(110.0f * intensity);

        // Phase 4: Bigger crash for tension release
        if (context == FillContext::TensionRelease)
            crashVel = std::min(127, crashVel + 15);

        addNote(buffer, crashNote, crashVel, crashTick, PPQ);

        if (currentProfile.aggression > 0.5f || context == FillContext::BuildUp)
        {
            addNote(buffer, kickNote, std::max(1, crashVel - 10), crashTick, PPQ / 2);
        }
    }

    return buffer;
}

juce::MidiBuffer DrummerEngine::patternToMidi(const PatternPhrase& pattern,
                                               double bpm,
                                               const GrooveTemplate& groove,
                                               const HumanizeSettings& humanize,
                                               int tickOffset)
{
    juce::MidiBuffer buffer;

    for (const auto& hit : pattern.hits)
    {
        // Skip if this element is disabled by kit mask
        if (!isElementEnabled(hit.element))
            continue;

        int pitch = getNoteForElement(hit.element);
        int velocity = hit.velocity;
        int tick = hit.tick + tickOffset;
        int duration = hit.duration;

        // Apply groove micro-timing
        if (groove.isValid())
        {
            tick = applyMicroTiming(tick, groove, bpm);
        }

        // Apply push/drag from humanization
        if (std::abs(humanize.pushDrag) > 0.1f)
        {
            double ticksPerMs = (PPQ * bpm) / 60000.0;
            int pushDragTicks = static_cast<int>(humanize.pushDrag * 0.4 * ticksPerMs);
            tick += pushDragTicks;
        }

        // Apply drummer's laid-back feel
        if (std::abs(currentProfile.laidBack) > 0.01f)
        {
            double ticksPerMs = (PPQ * bpm) / 60000.0;
            int laidBackTicks = static_cast<int>(currentProfile.laidBack * 20.0 * ticksPerMs);
            tick += laidBackTicks;
        }

        // Ensure tick is non-negative
        tick = std::max(0, tick);

        addNote(buffer, pitch, velocity, tick, duration);
    }

    return buffer;
}
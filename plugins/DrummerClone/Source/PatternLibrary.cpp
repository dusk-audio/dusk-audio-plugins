#include "PatternLibrary.h"
#include <algorithm>
#include <cmath>

//==============================================================================
// PatternPhrase implementation
//==============================================================================

void PatternPhrase::calculateCharacteristics(int ppq)
{
    if (hits.empty())
    {
        energy = 0.0f;
        density = 0.0f;
        syncopation = 0.0f;
        ghostNoteDensity = 0.0f;
        return;
    }

    int totalTicks = bars * ppq * timeSigNum * 4 / timeSigDenom;
    int numSixteenths = bars * timeSigNum * 4;

    // Energy: average velocity normalized
    float totalVel = 0.0f;
    for (const auto& hit : hits)
        totalVel += static_cast<float>(hit.velocity);
    energy = (totalVel / static_cast<float>(hits.size())) / 127.0f;

    // Density: hits per 16th note position
    density = static_cast<float>(hits.size()) / static_cast<float>(numSixteenths);
    density = std::min(1.0f, density);  // Cap at 1.0

    // Syncopation: ratio of offbeat hits
    int onBeatHits = 0;
    int offBeatHits = 0;
    int ticksPer16th = ppq / 4;
    if (ticksPer16th == 0)
    {
        syncopation = 0.0f;
        return;
    }

    for (const auto& hit : hits)
    {
        int pos16th = (hit.tick / ticksPer16th) % 4;
        if (pos16th == 0)
            onBeatHits++;
        else
            offBeatHits++;
    }
    if (onBeatHits + offBeatHits > 0)
        syncopation = static_cast<float>(offBeatHits) / static_cast<float>(onBeatHits + offBeatHits);

    // Ghost note density: count low-velocity snare hits
    int ghostCount = 0;
    int snareCount = 0;
    for (const auto& hit : hits)
    {
        if (hit.element == DrumMapping::SNARE)
        {
            snareCount++;
            if (hit.velocity < 60)
                ghostCount++;
        }
    }

    if (snareCount > 0)
        ghostNoteDensity = static_cast<float>(ghostCount) / static_cast<float>(snareCount);
}

//==============================================================================
// PatternLibrary implementation
//==============================================================================

PatternLibrary::PatternLibrary()
{
    std::random_device rd;
    rng.seed(rd());
    recentlyUsed.fill(-1);

    // Initialize empty pattern
    emptyPattern.id = "empty";
    emptyPattern.bars = 1;
}

int PatternLibrary::loadFromDirectory(const juce::File& directory)
{
    if (!directory.exists() || !directory.isDirectory())
        return 0;

    int loaded = 0;

    // Load JSON patterns
    auto jsonFiles = directory.findChildFiles(juce::File::findFiles, true, "*.json");
    for (const auto& file : jsonFiles)
    {
        if (loadPatternJSON(file))
            loaded++;
    }

    // Load MIDI patterns
    auto midiFiles = directory.findChildFiles(juce::File::findFiles, true, "*.mid");
    for (const auto& file : midiFiles)
    {
        // Try to infer style from path
        juce::String style = "Unknown";
        juce::String path = file.getFullPathName().toLowerCase();

        if (path.contains("rock")) style = "Rock";
        else if (path.contains("hiphop") || path.contains("hip-hop")) style = "HipHop";
        else if (path.contains("rnb") || path.contains("r&b")) style = "R&B";
        else if (path.contains("electronic") || path.contains("edm")) style = "Electronic";
        else if (path.contains("trap")) style = "Trap";
        else if (path.contains("alternative") || path.contains("indie")) style = "Alternative";
        else if (path.contains("songwriter") || path.contains("acoustic")) style = "Songwriter";

        loaded += loadFromMIDI(file, style);
    }

    return loaded;
}

int PatternLibrary::loadFromBinaryData(const void* data, int size)
{
    juce::String jsonStr = juce::String::createStringFromData(data, size);
    auto json = juce::JSON::parse(jsonStr);

    if (!json.isArray())
        return 0;

    int loaded = 0;
    auto* arr = json.getArray();

    for (const auto& item : *arr)
    {
        PatternPhrase pattern = parsePatternJSON(item);
        if (pattern.isValid())
        {
            patterns.push_back(pattern);
            loaded++;
        }
    }

    return loaded;
}

bool PatternLibrary::loadPatternJSON(const juce::File& file)
{
    auto json = juce::JSON::parse(file);
    if (json.isVoid())
        return false;

    // Handle both single pattern and array of patterns
    if (json.isArray())
    {
        auto* arr = json.getArray();
        for (const auto& item : *arr)
        {
            PatternPhrase pattern = parsePatternJSON(item);
            if (pattern.isValid())
                patterns.push_back(pattern);
        }
        return true;
    }
    else if (json.isObject())
    {
        PatternPhrase pattern = parsePatternJSON(json);
        if (pattern.isValid())
        {
            patterns.push_back(pattern);
            return true;
        }
    }

    return false;
}

PatternPhrase PatternLibrary::parsePatternJSON(const juce::var& json)
{
    PatternPhrase pattern;

    if (!json.isObject())
        return pattern;

    auto* obj = json.getDynamicObject();
    if (!obj)
        return pattern;

    pattern.id = obj->getProperty("id").toString();
    pattern.style = obj->getProperty("style").toString();
    pattern.category = obj->getProperty("category").toString();
    pattern.tags = obj->getProperty("tags").toString();
    pattern.bars = static_cast<int>(obj->getProperty("bars"));
    pattern.timeSigNum = obj->hasProperty("timeSigNum") ? static_cast<int>(obj->getProperty("timeSigNum")) : 4;
    pattern.timeSigDenom = obj->hasProperty("timeSigDenom") ? static_cast<int>(obj->getProperty("timeSigDenom")) : 4;
    pattern.energy = static_cast<float>(obj->getProperty("energy"));
    pattern.density = static_cast<float>(obj->getProperty("density"));
    pattern.syncopation = static_cast<float>(obj->getProperty("syncopation"));
    pattern.ghostNoteDensity = static_cast<float>(obj->getProperty("ghostNoteDensity"));
    pattern.swing = static_cast<float>(obj->getProperty("swing"));
    pattern.source = obj->getProperty("source").toString();
    pattern.author = obj->getProperty("author").toString();

    // Parse hits
    auto hitsVar = obj->getProperty("hits");
    if (hitsVar.isArray())
    {
        auto* hitsArr = hitsVar.getArray();
        for (const auto& hitVar : *hitsArr)
        {
            if (hitVar.isObject())
            {
                auto* hitObj = hitVar.getDynamicObject();
                DrumHit hit;
                hit.tick = static_cast<int>(hitObj->getProperty("tick"));

                // Element can be int or string
                auto elemVar = hitObj->getProperty("element");
                if (elemVar.isInt())
                {
                    hit.element = static_cast<DrumMapping::DrumElement>(static_cast<int>(elemVar));
                }
                else
                {
                    // Map string to element
                    juce::String elemStr = elemVar.toString().toLowerCase();
                    if (elemStr == "kick") hit.element = DrumMapping::KICK;
                    else if (elemStr == "snare") hit.element = DrumMapping::SNARE;
                    else if (elemStr == "hihat" || elemStr == "hh") hit.element = DrumMapping::HI_HAT_CLOSED;
                    else if (elemStr == "hihat_open" || elemStr == "hho") hit.element = DrumMapping::HI_HAT_OPEN;
                    else if (elemStr == "crash") hit.element = DrumMapping::CRASH_1;
                    else if (elemStr == "ride") hit.element = DrumMapping::RIDE;
                    else if (elemStr == "tom_high" || elemStr == "tom1") hit.element = DrumMapping::TOM_HIGH;
                    else if (elemStr == "tom_mid" || elemStr == "tom2") hit.element = DrumMapping::TOM_MID;
                    else if (elemStr == "tom_low" || elemStr == "tom3") hit.element = DrumMapping::TOM_LOW;
                    else if (elemStr == "tom_floor" || elemStr == "tom4") hit.element = DrumMapping::TOM_FLOOR;
                    else if (elemStr == "clap") hit.element = DrumMapping::CLAP;
                    else hit.element = DrumMapping::KICK;
                }

                hit.velocity = static_cast<int>(hitObj->getProperty("velocity"));
                hit.duration = hitObj->hasProperty("duration") ?
                               static_cast<int>(hitObj->getProperty("duration")) : 120;

                pattern.hits.push_back(hit);
            }
        }
    }

    // Sort hits by tick
    std::sort(pattern.hits.begin(), pattern.hits.end());

    // Recalculate characteristics if not provided
    if (pattern.energy == 0.0f && !pattern.hits.empty())
        pattern.calculateCharacteristics();

    return pattern;
}

int PatternLibrary::loadFromMIDI(const juce::File& file, const juce::String& style)
{
    juce::FileInputStream stream(file);
    if (!stream.openedOk())
        return 0;

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(stream))
        return 0;

    auto extracted = extractPatternsFromMIDI(midiFile, style);

    for (auto& pattern : extracted)
    {
        pattern.source = file.getFileName();
        patterns.push_back(pattern);
    }

    return static_cast<int>(extracted.size());
}

std::vector<PatternPhrase> PatternLibrary::extractPatternsFromMIDI(const juce::MidiFile& midiFile,
                                                                    const juce::String& style)
{
    std::vector<PatternPhrase> result;

    // Get timing info
    int ppq = midiFile.getTimeFormat();
    if (ppq <= 0) ppq = 960;

    // Scale factor to convert to our PPQ (960)
    double scale = 960.0 / static_cast<double>(ppq);

    // Find drum track (channel 10, or track with drum notes)
    for (int track = 0; track < midiFile.getNumTracks(); ++track)
    {
        const auto* seq = midiFile.getTrack(track);
        if (!seq) continue;

        PatternPhrase pattern;
        pattern.style = style;
        pattern.category = "groove";
        pattern.id = style + "_midi_" + juce::String(patterns.size());

        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            auto event = seq->getEventPointer(i);
            auto msg = event->message;

            // Only note-on events on channel 10 (drum channel)
            if (msg.isNoteOn() && (msg.getChannel() == 10 || msg.getChannel() == 1))
            {
                int pitch = msg.getNoteNumber();
                int velocity = msg.getVelocity();
                int tick = static_cast<int>(event->message.getTimeStamp() * scale);

                // Map GM drum pitch to our element
                DrumMapping::DrumElement element = DrumMapping::KICK;

                if (pitch == 36 || pitch == 35) element = DrumMapping::KICK;
                else if (pitch == 38 || pitch == 40) element = DrumMapping::SNARE;
                else if (pitch == 37) element = DrumMapping::SNARE;  // Side stick
                else if (pitch == 42) element = DrumMapping::HI_HAT_CLOSED;
                else if (pitch == 44) element = DrumMapping::HI_HAT_PEDAL;
                else if (pitch == 46) element = DrumMapping::HI_HAT_OPEN;
                else if (pitch == 49 || pitch == 57) element = DrumMapping::CRASH_1;
                else if (pitch == 51 || pitch == 59) element = DrumMapping::RIDE;
                else if (pitch == 53) element = DrumMapping::RIDE_BELL;
                else if (pitch == 41) element = DrumMapping::TOM_FLOOR;
                else if (pitch == 43) element = DrumMapping::TOM_LOW;
                else if (pitch == 45) element = DrumMapping::TOM_MID;
                else if (pitch == 47 || pitch == 48) element = DrumMapping::TOM_HIGH;
                else if (pitch == 39) element = DrumMapping::CLAP;
                else continue;  // Skip unknown

                DrumHit hit;
                hit.tick = tick;
                hit.element = element;
                hit.velocity = velocity;
                hit.duration = 120;  // Default duration

                pattern.hits.push_back(hit);
            }
        }

        if (!pattern.hits.empty())
        {
            // Calculate bars from max tick
            int maxTick = 0;
            for (const auto& hit : pattern.hits)
                maxTick = std::max(maxTick, hit.tick);

            int ticksPerBar = 960 * 4;  // 4/4
            pattern.bars = (maxTick / ticksPerBar) + 1;

            // Normalize to bar boundaries
            for (auto& hit : pattern.hits)
                hit.tick = hit.tick % (pattern.bars * ticksPerBar);

            std::sort(pattern.hits.begin(), pattern.hits.end());
            pattern.calculateCharacteristics();
            result.push_back(pattern);
        }
    }

    return result;
}

bool PatternLibrary::savePatternJSON(const PatternPhrase& pattern, const juce::File& file)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();

    obj->setProperty("id", pattern.id);
    obj->setProperty("style", pattern.style);
    obj->setProperty("category", pattern.category);
    obj->setProperty("tags", pattern.tags);
    obj->setProperty("bars", pattern.bars);
    obj->setProperty("timeSigNum", pattern.timeSigNum);
    obj->setProperty("timeSigDenom", pattern.timeSigDenom);
    obj->setProperty("energy", pattern.energy);
    obj->setProperty("density", pattern.density);
    obj->setProperty("syncopation", pattern.syncopation);
    obj->setProperty("ghostNoteDensity", pattern.ghostNoteDensity);
    obj->setProperty("swing", pattern.swing);
    obj->setProperty("source", pattern.source);
    obj->setProperty("author", pattern.author);

    juce::Array<juce::var> hitsArr;
    for (const auto& hit : pattern.hits)
    {
        juce::DynamicObject::Ptr hitObj = new juce::DynamicObject();
        hitObj->setProperty("tick", hit.tick);
        hitObj->setProperty("element", static_cast<int>(hit.element));
        hitObj->setProperty("velocity", hit.velocity);
        hitObj->setProperty("duration", hit.duration);
        hitsArr.add(juce::var(hitObj.get()));
    }
    obj->setProperty("hits", hitsArr);

    juce::var json(obj.get());
    return file.replaceWithText(juce::JSON::toString(json, true));
}

std::vector<int> PatternLibrary::findPatterns(const juce::String& style,
                                               const juce::String& category,
                                               float minEnergy,
                                               float maxEnergy,
                                               float minDensity,
                                               float maxDensity) const
{
    std::vector<int> result;

    for (int i = 0; i < static_cast<int>(patterns.size()); ++i)
    {
        const auto& p = patterns[i];

        if (!style.isEmpty() && p.style != style)
            continue;
        if (!category.isEmpty() && p.category != category)
            continue;
        if (p.energy < minEnergy || p.energy > maxEnergy)
            continue;
        if (p.density < minDensity || p.density > maxDensity)
            continue;

        result.push_back(i);
    }

    return result;
}

float PatternLibrary::calculateMatchScore(const PatternPhrase& pattern,
                                           const juce::String& style,
                                           float targetEnergy,
                                           float targetDensity) const
{
    float score = 0.0f;

    // Style match (most important)
    if (pattern.style == style)
        score += 10.0f;
    else
        score += 2.0f;  // Still usable

    // Energy match
    float energyDiff = std::abs(pattern.energy - targetEnergy);
    score += (1.0f - energyDiff) * 5.0f;

    // Density match
    float densityDiff = std::abs(pattern.density - targetDensity);
    score += (1.0f - densityDiff) * 3.0f;

    return score;
}

int PatternLibrary::selectBestPattern(const juce::String& style,
                                       float targetEnergy,
                                       float targetDensity,
                                       bool avoidRecent)
{
    if (patterns.empty())
        return -1;

    // Find candidates
    std::vector<std::pair<int, float>> candidates;  // index, score

    for (int i = 0; i < static_cast<int>(patterns.size()); ++i)
    {
        const auto& p = patterns[i];

        // Skip fills
        if (p.category == "fill")
            continue;

        // Skip recently used if requested
        if (avoidRecent && wasRecentlyUsed(i))
            continue;

        float score = calculateMatchScore(p, style, targetEnergy, targetDensity);
        candidates.push_back({i, score});
    }

    if (candidates.empty())
    {
        // Fall back to any groove pattern
        for (int i = 0; i < static_cast<int>(patterns.size()); ++i)
        {
            if (patterns[i].category != "fill")
            {
                candidates.push_back({i, 1.0f});
            }
        }
    }

    if (candidates.empty())
        return -1;

    // Sort by score (descending)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Pick from top candidates with some randomness
    int topN = std::min(5, static_cast<int>(candidates.size()));
    std::uniform_int_distribution<int> dist(0, topN - 1);
    int selected = candidates[dist(rng)].first;

    markUsed(selected);
    return selected;
}

int PatternLibrary::selectFillPattern(const juce::String& style,
                                       int beats,
                                       float intensity)
{
    std::vector<std::pair<int, float>> candidates;

    for (int i = 0; i < static_cast<int>(patterns.size()); ++i)
    {
        const auto& p = patterns[i];

        if (p.category != "fill")
            continue;

        // Match style
        float score = (p.style == style) ? 5.0f : 1.0f;

        // Match intensity/energy
        float energyDiff = std::abs(p.energy - intensity);
        score += (1.0f - energyDiff) * 3.0f;

        // Prefer matching length (approximate)
        if (p.bars * 4 >= beats)
            score += 2.0f;

        candidates.push_back({i, score});
    }

    if (candidates.empty())
        return -1;

    // Sort and pick randomly from top
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    int topN = std::min(3, static_cast<int>(candidates.size()));
    std::uniform_int_distribution<int> dist(0, topN - 1);

    return candidates[dist(rng)].first;
}

//==============================================================================
// Phase 4: Context-Aware Fill Selection

int PatternLibrary::selectContextualFill(const juce::String& style,
                                          int beats,
                                          float intensity,
                                          FillContext context,
                                          float nextSectionEnergy)
{
    // Adjust intensity based on context
    float effectiveIntensity = intensity;
    juce::String preferredTags;

    switch (context)
    {
        case FillContext::BuildUp:
            // Building tension - use higher intensity, prefer tom-heavy fills
            effectiveIntensity = std::min(1.0f, intensity * 1.3f);
            preferredTags = "buildup,tom,crescendo";
            break;

        case FillContext::TensionRelease:
            // Releasing after buildup - crash-focused, dramatic
            effectiveIntensity = std::max(0.7f, intensity);
            preferredTags = "crash,release,dramatic";
            break;

        case FillContext::SectionStart:
            // Starting a new section - clear and decisive
            effectiveIntensity = nextSectionEnergy;  // Match upcoming section
            preferredTags = "start,accent,clear";
            break;

        case FillContext::SectionEnd:
            // Ending a section - transitional
            // Intensity should bridge current to next section
            effectiveIntensity = (intensity + nextSectionEnergy) / 2.0f;
            preferredTags = "transition,ending";
            break;

        case FillContext::Breakdown:
            // Minimal, sparse fill
            effectiveIntensity = std::min(0.4f, intensity * 0.5f);
            preferredTags = "sparse,minimal,soft";
            break;

        case FillContext::Outro:
            // Ending fill - can be dramatic or fading
            effectiveIntensity = intensity * 0.8f;
            preferredTags = "outro,ending,final";
            break;

        case FillContext::Standard:
        default:
            // No adjustment
            break;
    }

    // Score and select fills with context awareness
    std::vector<std::pair<int, float>> candidates;

    for (int i = 0; i < static_cast<int>(patterns.size()); ++i)
    {
        const auto& p = patterns[i];

        if (p.category != "fill")
            continue;

        float score = 0.0f;

        // Style match
        if (p.style == style)
            score += 5.0f;
        else if (p.style.isEmpty() || p.style == "Any")
            score += 2.0f;

        // Energy match
        float energyDiff = std::abs(p.energy - effectiveIntensity);
        score += (1.0f - energyDiff) * 4.0f;

        // Length match
        if (p.bars * 4 >= beats)
            score += 2.0f;

        // Tag matching for context
        if (!preferredTags.isEmpty())
        {
            juce::StringArray tags;
            tags.addTokens(preferredTags, ",", "");

            for (const auto& tag : tags)
            {
                if (p.tags.containsIgnoreCase(tag.trim()))
                    score += 1.5f;
            }
        }

        // Bonus for high-energy fills in buildup context
        if (context == FillContext::BuildUp && p.energy > 0.7f)
            score += 2.0f;

        // Bonus for crash-heavy fills in release context
        if (context == FillContext::TensionRelease && p.hasElement(DrumMapping::CRASH_1))
            score += 2.0f;

        // Penalty for recently used
        if (wasRecentlyUsed(i))
            score *= 0.5f;

        if (score > 0)
            candidates.push_back({i, score});
    }

    if (candidates.empty())
    {
        // Fall back to standard selection
        return selectFillPattern(style, beats, effectiveIntensity);
    }

    // Sort by score and pick from top candidates
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    int topN = std::min(4, static_cast<int>(candidates.size()));
    std::uniform_int_distribution<int> dist(0, topN - 1);

    int selected = candidates[dist(rng)].first;
    markUsed(selected);

    return selected;
}

std::vector<DrumHit> PatternLibrary::generateLeadingTones(const PatternPhrase& fillPattern,
                                                           int numBeats,
                                                           double bpm)
{
    std::vector<DrumHit> leadingTones;
    juce::ignoreUnused(bpm);

    if (numBeats <= 0 || !fillPattern.isValid())
        return leadingTones;

    constexpr int PPQ = 960;

    // Leading tones create anticipation before the fill
    // Common techniques:
    // 1. Flam on the last beat before fill
    // 2. Crescendo hi-hat rolls
    // 3. Accented snare ghost notes
    // 4. Open hi-hat on the "and" before fill

    // Determine what kind of leading tones based on fill intensity
    bool isIntenseFill = fillPattern.energy > 0.7f;
    bool hasToms = fillPattern.hasElement(DrumMapping::TOM_HIGH) ||
                   fillPattern.hasElement(DrumMapping::TOM_MID) ||
                   fillPattern.hasElement(DrumMapping::TOM_LOW);

    // Position leading tones before tick 0 (fill start)
    int leadStartTick = -numBeats * PPQ;

    if (isIntenseFill)
    {
        // Intense fills: snare flam or roll leading in
        // Add accented snare hits leading to fill
        for (int i = 0; i < numBeats * 2; ++i)
        {
            int tick = leadStartTick + (i * PPQ / 2);  // 8th notes
            int vel = 50 + (i * 10);  // Crescendo
            vel = std::min(85, vel);
            leadingTones.push_back({tick, DrumMapping::SNARE, vel, 60});
        }

        // Add hi-hat accents
        leadingTones.push_back({-PPQ/2, DrumMapping::HI_HAT_OPEN, 90, 120});
    }
    else if (hasToms)
    {
        // Tom fill: single anticipatory snare hit
        leadingTones.push_back({-PPQ/4, DrumMapping::SNARE, 70, 60});  // 16th before
    }
    else
    {
        // Standard fill: ghost notes leading in
        leadingTones.push_back({-PPQ/2, DrumMapping::SNARE, 45, 60});  // Ghost on "and"
        leadingTones.push_back({-PPQ/4, DrumMapping::SNARE, 55, 60});  // Ghost on "a"
    }

    return leadingTones;
}

PatternPhrase PatternLibrary::generateTransition(float fromEnergy,
                                                   float toEnergy,
                                                   int beats)
{
    PatternPhrase transition;
    transition.id = "generated_transition";
    transition.category = "transition";
    transition.bars = 1;
    transition.energy = (fromEnergy + toEnergy) / 2.0f;

    constexpr int PPQ = 960;
    int totalTicks = beats * PPQ;

    if (beats <= 0)
        return transition;

    bool energyUp = toEnergy > fromEnergy;
    bool bigJump = std::abs(toEnergy - fromEnergy) > 0.3f;

    if (bigJump && energyUp)
    {
        // Big energy increase: build with toms and crash
        // Crescendo pattern
        for (int i = 0; i < beats * 4; ++i)  // 16th notes
        {
            int tick = i * (PPQ / 4);
            if (tick >= totalTicks) break;

            float progress = static_cast<float>(i) / (beats * 4);
            int vel = static_cast<int>(60 + progress * 50);

            // Alternate toms going down
            DrumMapping::DrumElement tom = DrumMapping::TOM_HIGH;
            if (i % 4 == 1) tom = DrumMapping::TOM_MID;
            else if (i % 4 == 2) tom = DrumMapping::TOM_LOW;
            else if (i % 4 == 3) tom = DrumMapping::TOM_FLOOR;

            transition.hits.push_back({tick, tom, vel, 60});
        }

        // Crash at end
        transition.hits.push_back({totalTicks - 1, DrumMapping::CRASH_1, 110, 240});
        transition.hits.push_back({totalTicks - 1, DrumMapping::KICK, 100, 120});
    }
    else if (bigJump && !energyUp)
    {
        // Big energy decrease: sparse, fading
        // Just a few hits fading out
        int vel = static_cast<int>(fromEnergy * 100);

        transition.hits.push_back({0, DrumMapping::SNARE, vel, 120});
        transition.hits.push_back({PPQ, DrumMapping::KICK, vel - 20, 120});

        if (beats >= 2)
        {
            transition.hits.push_back({PPQ * 2 - PPQ/4, DrumMapping::SNARE, vel - 30, 60});  // Ghost
        }
    }
    else
    {
        // Moderate transition: simple fill
        int baseVel = static_cast<int>(80 * fromEnergy);

        for (int beat = 0; beat < beats; ++beat)
        {
            int tick = beat * PPQ;
            if (tick >= totalTicks) break;

            // Snare on each beat
            transition.hits.push_back({tick, DrumMapping::SNARE, baseVel, 120});

            // Hi-hat 8ths
            transition.hits.push_back({tick + PPQ/2, DrumMapping::HI_HAT_CLOSED, baseVel - 20, 60});
        }
    }

    transition.calculateCharacteristics();
    return transition;
}
const PatternPhrase& PatternLibrary::getPattern(int index) const
{
    if (index >= 0 && index < static_cast<int>(patterns.size()))
        return patterns[index];
    return emptyPattern;
}

void PatternLibrary::markUsed(int index)
{
    recentlyUsed[historyIndex] = index;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
}

void PatternLibrary::clearHistory()
{
    recentlyUsed.fill(-1);
    historyIndex = 0;
}

bool PatternLibrary::wasRecentlyUsed(int index) const
{
    for (int i = 0; i < HISTORY_SIZE; ++i)
    {
        if (recentlyUsed[i] == index)
            return true;
    }
    return false;
}

bool PatternLibrary::hasStyle(const juce::String& style) const
{
    for (const auto& p : patterns)
    {
        if (p.style == style)
            return true;
    }
    return false;
}

juce::StringArray PatternLibrary::getAvailableStyles() const
{
    juce::StringArray styles;
    for (const auto& p : patterns)
    {
        if (!styles.contains(p.style))
            styles.add(p.style);
    }
    return styles;
}

std::map<juce::String, std::vector<int>> PatternLibrary::getPatternsByStyle() const
{
    std::map<juce::String, std::vector<int>> result;
    for (int i = 0; i < static_cast<int>(patterns.size()); ++i)
    {
        result[patterns[i].style].push_back(i);
    }
    return result;
}

//==============================================================================
// Built-in patterns
//==============================================================================

void PatternLibrary::loadBuiltInPatterns()
{
    createRockPatterns();
    createHipHopPatterns();
    createAlternativePatterns();
    createRnBPatterns();
    createElectronicPatterns();
    createTrapPatterns();
    createSongwriterPatterns();
    createFillPatterns();
}

// Helper to add a hit
static void addHit(PatternPhrase& p, int tick, DrumMapping::DrumElement elem, int vel, int dur = 120)
{
    p.hits.push_back({tick, elem, vel, dur});
}

// PPQ = 960, so:
// Beat = 960 ticks
// 8th = 480 ticks
// 16th = 240 ticks
// 32nd = 120 ticks

void PatternLibrary::createRockPatterns()
{
    // Rock Pattern 1: Basic rock beat
    {
        PatternPhrase p;
        p.id = "rock_basic_1";
        p.style = "Rock";
        p.category = "groove";
        p.tags = "basic,straight";
        p.bars = 1;
        p.energy = 0.7f;
        p.density = 0.5f;

        // Kick on 1 and 3
        addHit(p, 0, DrumMapping::KICK, 110);
        addHit(p, 1920, DrumMapping::KICK, 100);

        // Snare on 2 and 4
        addHit(p, 960, DrumMapping::SNARE, 105);
        addHit(p, 2880, DrumMapping::SNARE, 108);

        // 8th note hi-hats
        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 85 : 70;
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Rock Pattern 2: Driving rock with kick on "and of 4"
    {
        PatternPhrase p;
        p.id = "rock_driving_1";
        p.style = "Rock";
        p.category = "groove";
        p.tags = "driving,syncopated";
        p.bars = 1;
        p.energy = 0.8f;

        addHit(p, 0, DrumMapping::KICK, 115);
        addHit(p, 1920, DrumMapping::KICK, 105);
        addHit(p, 3600, DrumMapping::KICK, 95);  // "and of 4"

        addHit(p, 960, DrumMapping::SNARE, 108);
        addHit(p, 2880, DrumMapping::SNARE, 110);

        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 90 : 75;
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Rock Pattern 3: Heavy rock with crashes
    {
        PatternPhrase p;
        p.id = "rock_heavy_1";
        p.style = "Rock";
        p.category = "groove";
        p.tags = "heavy,loud";
        p.bars = 1;
        p.energy = 0.9f;

        addHit(p, 0, DrumMapping::KICK, 120);
        addHit(p, 0, DrumMapping::CRASH_1, 110);
        addHit(p, 1920, DrumMapping::KICK, 115);

        addHit(p, 960, DrumMapping::SNARE, 115);
        addHit(p, 2880, DrumMapping::SNARE, 118);

        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 95 : 80;
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Rock Pattern 4: Soft verse
    {
        PatternPhrase p;
        p.id = "rock_soft_verse";
        p.style = "Rock";
        p.category = "groove";
        p.tags = "soft,verse";
        p.bars = 1;
        p.energy = 0.5f;

        addHit(p, 0, DrumMapping::KICK, 85);
        addHit(p, 1920, DrumMapping::KICK, 80);

        addHit(p, 960, DrumMapping::SNARE, 80);
        addHit(p, 2880, DrumMapping::SNARE, 85);

        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 70 : 55;
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }
}

void PatternLibrary::createHipHopPatterns()
{
    // Hip-Hop Pattern 1: Classic boom-bap
    {
        PatternPhrase p;
        p.id = "hiphop_boombap_1";
        p.style = "HipHop";
        p.category = "groove";
        p.tags = "boombap,classic,ghost-notes";
        p.bars = 1;
        p.energy = 0.65f;
        p.swing = 0.15f;

        // Kick pattern: 1, "and of 2", 3
        addHit(p, 0, DrumMapping::KICK, 110);
        addHit(p, 1440, DrumMapping::KICK, 95);  // "and of 2"
        addHit(p, 1920, DrumMapping::KICK, 100);

        // Snare on 2 and 4
        addHit(p, 960, DrumMapping::SNARE, 105);
        addHit(p, 2880, DrumMapping::SNARE, 108);

        // Ghost notes - the secret sauce!
        addHit(p, 720, DrumMapping::SNARE, 35);   // "a of 1"
        addHit(p, 2640, DrumMapping::SNARE, 38);  // "a of 3"

        // 16th note hi-hats with swing
        for (int i = 0; i < 16; ++i)
        {
            int vel;
            if (i % 4 == 0) vel = 80;
            else if (i % 4 == 2) vel = 70;
            else vel = 55;
            addHit(p, i * 240, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Hip-Hop Pattern 2: J Dilla-style laid back
    {
        PatternPhrase p;
        p.id = "hiphop_dilla_1";
        p.style = "HipHop";
        p.category = "groove";
        p.tags = "dilla,laid-back,ghost-notes";
        p.bars = 1;
        p.energy = 0.6f;
        p.swing = 0.25f;

        addHit(p, 0, DrumMapping::KICK, 105);
        addHit(p, 1200, DrumMapping::KICK, 90);  // Syncopated
        addHit(p, 2160, DrumMapping::KICK, 95);

        addHit(p, 960, DrumMapping::SNARE, 100);
        addHit(p, 2880, DrumMapping::SNARE, 105);

        // Heavy ghost notes
        addHit(p, 480, DrumMapping::SNARE, 30);
        addHit(p, 720, DrumMapping::SNARE, 35);
        addHit(p, 2400, DrumMapping::SNARE, 32);
        addHit(p, 2640, DrumMapping::SNARE, 38);

        for (int i = 0; i < 16; ++i)
        {
            int vel = 50 + (i % 4 == 0 ? 25 : 0);
            addHit(p, i * 240, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Hip-Hop Pattern 3: Modern bounce
    {
        PatternPhrase p;
        p.id = "hiphop_bounce_1";
        p.style = "HipHop";
        p.category = "groove";
        p.tags = "bounce,modern";
        p.bars = 1;
        p.energy = 0.7f;

        addHit(p, 0, DrumMapping::KICK, 115);
        addHit(p, 720, DrumMapping::KICK, 90);
        addHit(p, 1920, DrumMapping::KICK, 110);
        addHit(p, 2640, DrumMapping::KICK, 85);

        addHit(p, 960, DrumMapping::SNARE, 110);
        addHit(p, 2880, DrumMapping::SNARE, 112);

        addHit(p, 720, DrumMapping::SNARE, 40);
        addHit(p, 2640, DrumMapping::SNARE, 42);

        for (int i = 0; i < 16; ++i)
        {
            int vel = (i % 4 == 0) ? 75 : (i % 2 == 0) ? 65 : 50;
            addHit(p, i * 240, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }
}

void PatternLibrary::createAlternativePatterns()
{
    // Alternative Pattern 1: Indie rock
    {
        PatternPhrase p;
        p.id = "alt_indie_1";
        p.style = "Alternative";
        p.category = "groove";
        p.tags = "indie,dynamic";
        p.bars = 1;
        p.energy = 0.6f;

        addHit(p, 0, DrumMapping::KICK, 100);
        addHit(p, 1680, DrumMapping::KICK, 90);
        addHit(p, 2160, DrumMapping::KICK, 85);

        addHit(p, 960, DrumMapping::SNARE, 95);
        addHit(p, 2880, DrumMapping::SNARE, 100);

        // Ride cymbal pattern
        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 80 : 65;
            addHit(p, i * 480, DrumMapping::RIDE, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Alternative Pattern 2: Post-punk
    {
        PatternPhrase p;
        p.id = "alt_postpunk_1";
        p.style = "Alternative";
        p.category = "groove";
        p.tags = "postpunk,driving";
        p.bars = 1;
        p.energy = 0.7f;

        addHit(p, 0, DrumMapping::KICK, 105);
        addHit(p, 480, DrumMapping::KICK, 90);
        addHit(p, 1920, DrumMapping::KICK, 100);
        addHit(p, 2400, DrumMapping::KICK, 88);

        addHit(p, 960, DrumMapping::SNARE, 100);
        addHit(p, 2880, DrumMapping::SNARE, 105);

        for (int i = 0; i < 16; ++i)
        {
            int vel = (i % 4 == 0) ? 85 : 60;
            addHit(p, i * 240, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }
}

void PatternLibrary::createRnBPatterns()
{
    // R&B Pattern 1: Neo-soul groove
    {
        PatternPhrase p;
        p.id = "rnb_neosoul_1";
        p.style = "R&B";
        p.category = "groove";
        p.tags = "neosoul,smooth,ghost-notes";
        p.bars = 1;
        p.energy = 0.55f;
        p.swing = 0.2f;

        addHit(p, 0, DrumMapping::KICK, 95);
        addHit(p, 1440, DrumMapping::KICK, 85);
        addHit(p, 2160, DrumMapping::KICK, 80);

        addHit(p, 960, DrumMapping::SNARE, 90);
        addHit(p, 2880, DrumMapping::SNARE, 95);

        // Lots of ghost notes
        addHit(p, 240, DrumMapping::SNARE, 28);
        addHit(p, 720, DrumMapping::SNARE, 35);
        addHit(p, 1680, DrumMapping::SNARE, 30);
        addHit(p, 2160, DrumMapping::SNARE, 32);
        addHit(p, 2640, DrumMapping::SNARE, 38);
        addHit(p, 3360, DrumMapping::SNARE, 28);

        for (int i = 0; i < 16; ++i)
        {
            int vel = 45 + (i % 4 == 0 ? 25 : (i % 2 == 0 ? 15 : 0));
            addHit(p, i * 240, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // R&B Pattern 2: Modern R&B
    {
        PatternPhrase p;
        p.id = "rnb_modern_1";
        p.style = "R&B";
        p.category = "groove";
        p.tags = "modern,minimal";
        p.bars = 1;
        p.energy = 0.5f;

        addHit(p, 0, DrumMapping::KICK, 100);
        addHit(p, 1680, DrumMapping::KICK, 88);

        addHit(p, 960, DrumMapping::SNARE, 95);
        addHit(p, 2880, DrumMapping::SNARE, 98);

        addHit(p, 720, DrumMapping::SNARE, 35);
        addHit(p, 2640, DrumMapping::SNARE, 38);

        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 70 : 55;
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }
}

void PatternLibrary::createElectronicPatterns()
{
    // Electronic Pattern 1: Four-on-floor house
    {
        PatternPhrase p;
        p.id = "electronic_house_1";
        p.style = "Electronic";
        p.category = "groove";
        p.tags = "house,four-on-floor";
        p.bars = 1;
        p.energy = 0.75f;

        // Kick on every beat
        for (int i = 0; i < 4; ++i)
        {
            int vel = (i == 0) ? 115 : 110;
            addHit(p, i * 960, DrumMapping::KICK, vel);
        }

        // Clap on 2 and 4
        addHit(p, 960, DrumMapping::CLAP, 105);
        addHit(p, 2880, DrumMapping::CLAP, 108);

        // Open hat on upbeats
        for (int i = 0; i < 4; ++i)
        {
            addHit(p, i * 960 + 480, DrumMapping::HI_HAT_OPEN, 90);
        }

        // Closed hats
        for (int i = 0; i < 8; ++i)
        {
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, 75);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Electronic Pattern 2: Techno
    {
        PatternPhrase p;
        p.id = "electronic_techno_1";
        p.style = "Electronic";
        p.category = "groove";
        p.tags = "techno,driving";
        p.bars = 1;
        p.energy = 0.85f;

        for (int i = 0; i < 4; ++i)
        {
            addHit(p, i * 960, DrumMapping::KICK, 120);
        }

        addHit(p, 960, DrumMapping::CLAP, 100);
        addHit(p, 2880, DrumMapping::CLAP, 105);

        for (int i = 0; i < 16; ++i)
        {
            int vel = (i % 4 == 0) ? 80 : (i % 2 == 0) ? 70 : 55;
            addHit(p, i * 240, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }
}

void PatternLibrary::createTrapPatterns()
{
    // Trap Pattern 1: Basic trap
    {
        PatternPhrase p;
        p.id = "trap_basic_1";
        p.style = "Trap";
        p.category = "groove";
        p.tags = "trap,basic,half-time";
        p.bars = 1;
        p.energy = 0.7f;

        // 808-style kick pattern
        addHit(p, 0, DrumMapping::KICK, 115);
        addHit(p, 720, DrumMapping::KICK, 95);
        addHit(p, 2400, DrumMapping::KICK, 100);

        // Half-time snare on beat 3
        addHit(p, 1920, DrumMapping::SNARE, 110);

        // Rolling hi-hats
        for (int i = 0; i < 32; ++i)
        {
            int vel = 55 + (i % 4 == 0 ? 20 : (i % 2 == 0 ? 10 : 0));
            addHit(p, i * 120, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Trap Pattern 2: Dark trap
    {
        PatternPhrase p;
        p.id = "trap_dark_1";
        p.style = "Trap";
        p.category = "groove";
        p.tags = "trap,dark,aggressive";
        p.bars = 1;
        p.energy = 0.8f;

        addHit(p, 0, DrumMapping::KICK, 120);
        addHit(p, 480, DrumMapping::KICK, 100);
        addHit(p, 2640, DrumMapping::KICK, 110);

        addHit(p, 1920, DrumMapping::SNARE, 115);
        addHit(p, 1920, DrumMapping::CLAP, 100);

        // Triplet hi-hat rolls
        for (int beat = 0; beat < 4; ++beat)
        {
            bool doRoll = (beat == 1 || beat == 3);
            if (doRoll)
            {
                for (int i = 0; i < 6; ++i)
                {
                    int vel = 60 + (i * 5);
                    addHit(p, beat * 960 + i * 160, DrumMapping::HI_HAT_CLOSED, vel);
                }
            }
            else
            {
                for (int i = 0; i < 8; ++i)
                {
                    addHit(p, beat * 960 + i * 120, DrumMapping::HI_HAT_CLOSED, 55);
                }
            }
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }
}

void PatternLibrary::createSongwriterPatterns()
{
    // Songwriter Pattern 1: Simple ballad
    {
        PatternPhrase p;
        p.id = "songwriter_ballad_1";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "ballad,simple,soft";
        p.bars = 1;
        p.energy = 0.4f;

        addHit(p, 0, DrumMapping::KICK, 75);
        addHit(p, 1920, DrumMapping::KICK, 70);

        addHit(p, 960, DrumMapping::SNARE, 70);
        addHit(p, 2880, DrumMapping::SNARE, 75);

        for (int i = 0; i < 4; ++i)
        {
            addHit(p, i * 960, DrumMapping::RIDE, 60);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Songwriter Pattern 2: Acoustic groove
    {
        PatternPhrase p;
        p.id = "songwriter_acoustic_1";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "acoustic,warm";
        p.bars = 1;
        p.energy = 0.5f;

        addHit(p, 0, DrumMapping::KICK, 80);
        addHit(p, 1440, DrumMapping::KICK, 70);
        addHit(p, 1920, DrumMapping::KICK, 75);

        addHit(p, 960, DrumMapping::SNARE, 75);
        addHit(p, 2880, DrumMapping::SNARE, 80);

        addHit(p, 720, DrumMapping::SNARE, 30);
        addHit(p, 2640, DrumMapping::SNARE, 32);

        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 65 : 50;
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // ============ BRUSH KIT PATTERNS ============
    // These patterns use brush articulations for jazz, folk, and acoustic styles

    // Songwriter Pattern 3: Jazz Ballad with Brushes
    {
        PatternPhrase p;
        p.id = "songwriter_brush_ballad";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "brush,ballad,jazz,soft";
        p.bars = 1;
        p.energy = 0.35f;
        p.swing = 0.2f;

        // Soft kick on 1 and 3
        addHit(p, 0, DrumMapping::KICK, 65);
        addHit(p, 1920, DrumMapping::KICK, 60);

        // Brush taps on 2 and 4 (replacing snare backbeat)
        addHit(p, 960, DrumMapping::BRUSH_TAP, 70);
        addHit(p, 2880, DrumMapping::BRUSH_TAP, 75);

        // Continuous brush swirl throughout (represented as hits on beats)
        // Real brushes would be continuous - this simulates the articulation points
        for (int i = 0; i < 4; ++i)
        {
            addHit(p, i * 960, DrumMapping::BRUSH_SWIRL, 50 + (i % 2) * 10);
        }

        // Light ride on quarters
        for (int i = 0; i < 4; ++i)
        {
            addHit(p, i * 960, DrumMapping::RIDE, 55);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Songwriter Pattern 4: Folk Brush Groove
    {
        PatternPhrase p;
        p.id = "songwriter_brush_folk";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "brush,folk,americana,acoustic";
        p.bars = 1;
        p.energy = 0.45f;
        p.swing = 0.1f;

        // Simple kick pattern
        addHit(p, 0, DrumMapping::KICK, 70);
        addHit(p, 1920, DrumMapping::KICK, 65);

        // Brush sweeps on 8ths (left-right motion)
        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 65 : 55;
            addHit(p, i * 480, DrumMapping::BRUSH_SWEEP, vel);
        }

        // Accent taps on 2 and 4
        addHit(p, 960, DrumMapping::BRUSH_TAP, 75);
        addHit(p, 2880, DrumMapping::BRUSH_TAP, 80);

        // Side stick ghost notes before backbeats
        addHit(p, 720, DrumMapping::SIDE_STICK, 35);
        addHit(p, 2640, DrumMapping::SIDE_STICK, 38);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Songwriter Pattern 5: Jazz Swing with Brushes
    {
        PatternPhrase p;
        p.id = "songwriter_brush_swing";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "brush,jazz,swing,ride";
        p.bars = 1;
        p.energy = 0.5f;
        p.swing = 0.33f;  // Triplet swing feel

        // Very sparse kick - just beat 1
        addHit(p, 0, DrumMapping::KICK, 60);
        addHit(p, 2400, DrumMapping::KICK, 55);

        // Brush swirls on triplet feel (simulated with swing)
        addHit(p, 0, DrumMapping::BRUSH_SWIRL, 55);
        addHit(p, 640, DrumMapping::BRUSH_SWIRL, 45);  // Triplet
        addHit(p, 960, DrumMapping::BRUSH_SWIRL, 50);
        addHit(p, 1600, DrumMapping::BRUSH_SWIRL, 45);
        addHit(p, 1920, DrumMapping::BRUSH_SWIRL, 55);
        addHit(p, 2560, DrumMapping::BRUSH_SWIRL, 45);
        addHit(p, 2880, DrumMapping::BRUSH_SWIRL, 50);
        addHit(p, 3520, DrumMapping::BRUSH_SWIRL, 45);

        // Brush taps/accents on 2 and 4
        addHit(p, 960, DrumMapping::BRUSH_TAP, 65);
        addHit(p, 2880, DrumMapping::BRUSH_TAP, 70);

        // Hi-hat foot on 2 and 4
        addHit(p, 960, DrumMapping::HI_HAT_PEDAL, 45);
        addHit(p, 2880, DrumMapping::HI_HAT_PEDAL, 45);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Songwriter Pattern 6: Waltz Brush Pattern (3/4)
    {
        PatternPhrase p;
        p.id = "songwriter_brush_waltz";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "brush,waltz,3/4,ballad";
        p.bars = 1;
        p.timeSigNum = 3;  // 3/4 time
        p.energy = 0.4f;

        // Kick on 1
        addHit(p, 0, DrumMapping::KICK, 70);

        // Brush sweeps - circular pattern emphasized
        addHit(p, 0, DrumMapping::BRUSH_SWIRL, 60);
        addHit(p, 960, DrumMapping::BRUSH_SWEEP, 55);
        addHit(p, 1920, DrumMapping::BRUSH_SWEEP, 55);

        // Light tap on 2 and 3
        addHit(p, 960, DrumMapping::BRUSH_TAP, 55);
        addHit(p, 1920, DrumMapping::BRUSH_TAP, 50);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Songwriter Pattern 7: Intimate Acoustic (very soft)
    {
        PatternPhrase p;
        p.id = "songwriter_brush_intimate";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "brush,intimate,soft,minimal";
        p.bars = 1;
        p.energy = 0.25f;

        // Minimal kick - just beat 1
        addHit(p, 0, DrumMapping::KICK, 55);

        // Very soft brush swirls throughout
        for (int i = 0; i < 4; ++i)
        {
            addHit(p, i * 960, DrumMapping::BRUSH_SWIRL, 40 + (i == 0 ? 10 : 0));
        }

        // Single soft tap on 3 (unconventional placement for intimacy)
        addHit(p, 1920, DrumMapping::BRUSH_TAP, 50);

        // Occasional side stick ghost
        addHit(p, 2640, DrumMapping::SIDE_STICK, 25);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Songwriter Pattern 8: Brush Bossa Nova
    {
        PatternPhrase p;
        p.id = "songwriter_brush_bossa";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "brush,bossa,latin,brazilian";
        p.bars = 1;
        p.energy = 0.4f;
        p.swing = 0.1f;

        // Bossa nova kick pattern
        addHit(p, 0, DrumMapping::KICK, 65);
        addHit(p, 1440, DrumMapping::KICK, 60);
        addHit(p, 2880, DrumMapping::KICK, 62);

        // Brush swirl - continuous circular motion
        for (int i = 0; i < 8; ++i)
        {
            int vel = 45 + ((i % 4 == 0) ? 15 : (i % 2 == 0) ? 5 : 0);
            addHit(p, i * 480, DrumMapping::BRUSH_SWIRL, vel);
        }

        // Side stick on the syncopated bossa rhythm
        addHit(p, 720, DrumMapping::SIDE_STICK, 60);
        addHit(p, 1920, DrumMapping::SIDE_STICK, 65);
        addHit(p, 2400, DrumMapping::SIDE_STICK, 55);
        addHit(p, 3360, DrumMapping::SIDE_STICK, 58);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Songwriter Pattern 9: Gospel/Spiritual Brush Pattern
    {
        PatternPhrase p;
        p.id = "songwriter_brush_gospel";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "brush,gospel,spiritual,soulful";
        p.bars = 1;
        p.energy = 0.55f;
        p.swing = 0.15f;

        // Kick on 1 and anticipation
        addHit(p, 0, DrumMapping::KICK, 75);
        addHit(p, 1680, DrumMapping::KICK, 65);
        addHit(p, 1920, DrumMapping::KICK, 70);

        // Brush slaps on 2 and 4 (more accented)
        addHit(p, 960, DrumMapping::BRUSH_SLAP, 80);
        addHit(p, 2880, DrumMapping::BRUSH_SLAP, 85);

        // Brush sweeps on 8ths
        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 55 : 45;
            addHit(p, i * 480, DrumMapping::BRUSH_SWEEP, vel);
        }

        // Ghost tap before backbeats
        addHit(p, 720, DrumMapping::BRUSH_TAP, 35);
        addHit(p, 2640, DrumMapping::BRUSH_TAP, 38);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Songwriter Pattern 10: Country Brush Train Beat
    {
        PatternPhrase p;
        p.id = "songwriter_brush_train";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "brush,country,train,americana";
        p.bars = 1;
        p.energy = 0.5f;

        // Steady kick on quarters
        for (int i = 0; i < 4; ++i)
        {
            addHit(p, i * 960, DrumMapping::KICK, 65);
        }

        // Alternating brush sweeps creating "chug" sound
        for (int i = 0; i < 16; ++i)
        {
            int vel = (i % 4 == 0) ? 60 : (i % 2 == 0) ? 50 : 40;
            addHit(p, i * 240, DrumMapping::BRUSH_SWEEP, vel);
        }

        // Snare/tap accents on 2 and 4
        addHit(p, 960, DrumMapping::BRUSH_TAP, 70);
        addHit(p, 2880, DrumMapping::BRUSH_TAP, 75);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Songwriter Pattern 11: Brush Fill (for transitions)
    {
        PatternPhrase p;
        p.id = "songwriter_brush_fill";
        p.style = "Songwriter";
        p.category = "fill";
        p.tags = "brush,fill,soft,transition";
        p.bars = 1;
        p.energy = 0.5f;

        // Soft brush roll building
        for (int i = 0; i < 8; ++i)
        {
            int vel = 45 + (i * 5);
            addHit(p, i * 240, DrumMapping::BRUSH_TAP, vel);
        }

        // Brush slap accent at end
        addHit(p, 1920, DrumMapping::BRUSH_SLAP, 80);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }
}

void PatternLibrary::createFillPatterns()
{
    // 1-beat fill: Snare roll
    {
        PatternPhrase p;
        p.id = "fill_1beat_snare";
        p.style = "Rock";
        p.category = "fill";
        p.tags = "short,snare";
        p.bars = 1;
        p.energy = 0.75f;

        for (int i = 0; i < 4; ++i)
        {
            int vel = 85 + (i * 10);
            addHit(p, i * 240, DrumMapping::SNARE, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // 2-beat fill: Descending toms
    {
        PatternPhrase p;
        p.id = "fill_2beat_toms";
        p.style = "Rock";
        p.category = "fill";
        p.tags = "toms,descending";
        p.bars = 1;
        p.energy = 0.8f;

        addHit(p, 0, DrumMapping::SNARE, 100);
        addHit(p, 240, DrumMapping::TOM_HIGH, 95);
        addHit(p, 480, DrumMapping::TOM_MID, 100);
        addHit(p, 720, DrumMapping::TOM_MID, 90);
        addHit(p, 960, DrumMapping::TOM_LOW, 105);
        addHit(p, 1200, DrumMapping::TOM_LOW, 95);
        addHit(p, 1440, DrumMapping::TOM_FLOOR, 110);
        addHit(p, 1680, DrumMapping::TOM_FLOOR, 100);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // 4-beat fill: Build-up
    {
        PatternPhrase p;
        p.id = "fill_4beat_buildup";
        p.style = "Rock";
        p.category = "fill";
        p.tags = "buildup,intense";
        p.bars = 1;
        p.energy = 0.9f;

        // Beat 1: Snare 16ths
        for (int i = 0; i < 4; ++i)
            addHit(p, i * 240, DrumMapping::SNARE, 80 + (i * 5));

        // Beat 2: High tom
        for (int i = 0; i < 4; ++i)
            addHit(p, 960 + i * 240, DrumMapping::TOM_HIGH, 90 + (i * 5));

        // Beat 3: Mid tom
        for (int i = 0; i < 4; ++i)
            addHit(p, 1920 + i * 240, DrumMapping::TOM_MID, 95 + (i * 5));

        // Beat 4: Floor tom
        for (int i = 0; i < 4; ++i)
            addHit(p, 2880 + i * 240, DrumMapping::TOM_FLOOR, 100 + (i * 5));

        // Kick accents
        addHit(p, 0, DrumMapping::KICK, 100);
        addHit(p, 1920, DrumMapping::KICK, 105);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Hip-hop fill
    {
        PatternPhrase p;
        p.id = "fill_hiphop_1";
        p.style = "HipHop";
        p.category = "fill";
        p.tags = "subtle,short";
        p.bars = 1;
        p.energy = 0.6f;

        addHit(p, 0, DrumMapping::SNARE, 90);
        addHit(p, 240, DrumMapping::SNARE, 45);
        addHit(p, 480, DrumMapping::SNARE, 95);
        addHit(p, 720, DrumMapping::SNARE, 50);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Crash ending
    {
        PatternPhrase p;
        p.id = "fill_crash_ending";
        p.style = "Rock";
        p.category = "fill";
        p.tags = "crash,ending";
        p.bars = 1;
        p.energy = 0.95f;

        addHit(p, 0, DrumMapping::CRASH_1, 115);
        addHit(p, 0, DrumMapping::KICK, 120);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // ============ ADDITIONAL PATTERNS FROM CLASSIC BEATS ============

    // Amen Break (classic breakbeat/jungle)
    {
        PatternPhrase p;
        p.id = "fill_amen_break";
        p.style = "HipHop";
        p.category = "groove";
        p.tags = "breakbeat,amen,classic";
        p.bars = 1;
        p.energy = 0.85f;

        // Classic amen break pattern
        addHit(p, 0, DrumMapping::KICK, 110);
        addHit(p, 0, DrumMapping::HI_HAT_CLOSED, 80);
        addHit(p, 240, DrumMapping::HI_HAT_CLOSED, 65);
        addHit(p, 480, DrumMapping::SNARE, 105);
        addHit(p, 480, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 720, DrumMapping::HI_HAT_CLOSED, 60);
        addHit(p, 960, DrumMapping::KICK, 100);
        addHit(p, 960, DrumMapping::HI_HAT_CLOSED, 80);
        addHit(p, 1200, DrumMapping::KICK, 95);
        addHit(p, 1200, DrumMapping::HI_HAT_CLOSED, 65);
        addHit(p, 1440, DrumMapping::SNARE, 110);
        addHit(p, 1440, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 1680, DrumMapping::HI_HAT_CLOSED, 60);
        addHit(p, 1920, DrumMapping::KICK, 105);
        addHit(p, 1920, DrumMapping::HI_HAT_CLOSED, 80);
        addHit(p, 2160, DrumMapping::HI_HAT_CLOSED, 65);
        addHit(p, 2400, DrumMapping::SNARE, 100);
        addHit(p, 2400, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 2640, DrumMapping::HI_HAT_CLOSED, 60);
        addHit(p, 2880, DrumMapping::KICK, 100);
        addHit(p, 2880, DrumMapping::HI_HAT_CLOSED, 80);
        addHit(p, 3120, DrumMapping::SNARE, 95);
        addHit(p, 3120, DrumMapping::HI_HAT_CLOSED, 70);
        addHit(p, 3360, DrumMapping::SNARE, 108);
        addHit(p, 3360, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 3600, DrumMapping::HI_HAT_CLOSED, 60);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Purdie Shuffle (classic funk - Bernard Purdie)
    {
        PatternPhrase p;
        p.id = "funk_purdie_shuffle";
        p.style = "R&B";
        p.category = "groove";
        p.tags = "shuffle,funk,classic,ghost-notes";
        p.bars = 1;
        p.energy = 0.7f;
        p.swing = 0.2f;

        // Triplet-based shuffle feel
        addHit(p, 0, DrumMapping::KICK, 105);
        addHit(p, 0, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 320, DrumMapping::HI_HAT_CLOSED, 55);  // Triplet
        addHit(p, 480, DrumMapping::SNARE, 35);  // Ghost
        addHit(p, 640, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 960, DrumMapping::SNARE, 100);
        addHit(p, 960, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 1280, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 1440, DrumMapping::SNARE, 38);  // Ghost
        addHit(p, 1600, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 1920, DrumMapping::KICK, 100);
        addHit(p, 1920, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 2240, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 2400, DrumMapping::SNARE, 35);
        addHit(p, 2560, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 2880, DrumMapping::SNARE, 105);
        addHit(p, 2880, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 3200, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 3360, DrumMapping::SNARE, 40);
        addHit(p, 3520, DrumMapping::HI_HAT_CLOSED, 75);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Funky Drummer (James Brown / Clyde Stubblefield)
    {
        PatternPhrase p;
        p.id = "funk_funky_drummer";
        p.style = "HipHop";
        p.category = "groove";
        p.tags = "funk,classic,sampled,ghost-notes";
        p.bars = 1;
        p.energy = 0.75f;

        addHit(p, 0, DrumMapping::KICK, 110);
        addHit(p, 0, DrumMapping::HI_HAT_CLOSED, 80);
        addHit(p, 240, DrumMapping::HI_HAT_CLOSED, 60);
        addHit(p, 480, DrumMapping::SNARE, 40);  // Ghost
        addHit(p, 480, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 720, DrumMapping::SNARE, 35);
        addHit(p, 720, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 960, DrumMapping::SNARE, 105);
        addHit(p, 960, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 1200, DrumMapping::HI_HAT_CLOSED, 60);
        addHit(p, 1440, DrumMapping::KICK, 95);
        addHit(p, 1440, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 1680, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 1920, DrumMapping::KICK, 100);
        addHit(p, 1920, DrumMapping::HI_HAT_CLOSED, 80);
        addHit(p, 2160, DrumMapping::SNARE, 38);
        addHit(p, 2160, DrumMapping::HI_HAT_CLOSED, 60);
        addHit(p, 2400, DrumMapping::SNARE, 42);
        addHit(p, 2400, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 2640, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 2880, DrumMapping::SNARE, 108);
        addHit(p, 2880, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 3120, DrumMapping::HI_HAT_CLOSED, 60);
        addHit(p, 3360, DrumMapping::KICK, 90);
        addHit(p, 3360, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 3600, DrumMapping::SNARE, 35);
        addHit(p, 3600, DrumMapping::HI_HAT_CLOSED, 55);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Bossa Nova
    {
        PatternPhrase p;
        p.id = "latin_bossa_nova";
        p.style = "Songwriter";
        p.category = "groove";
        p.tags = "bossa,latin,brazilian,soft";
        p.bars = 1;
        p.energy = 0.4f;

        // Classic bossa nova rim pattern
        addHit(p, 0, DrumMapping::KICK, 75);
        addHit(p, 720, DrumMapping::SNARE, 65);  // Rim click style (lower vel)
        addHit(p, 1440, DrumMapping::KICK, 70);
        addHit(p, 1920, DrumMapping::SNARE, 68);
        addHit(p, 2400, DrumMapping::SNARE, 62);
        addHit(p, 2880, DrumMapping::KICK, 72);
        addHit(p, 3360, DrumMapping::SNARE, 65);

        // Cross-stick pattern
        for (int i = 0; i < 8; ++i)
        {
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, 50 + (i % 2 == 0 ? 10 : 0));
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Jazz Swing (Spang-a-lang)
    {
        PatternPhrase p;
        p.id = "jazz_swing";
        p.style = "Alternative";
        p.category = "groove";
        p.tags = "jazz,swing,ride";
        p.bars = 1;
        p.energy = 0.5f;
        p.swing = 0.33f;  // Triplet swing

        // Ride cymbal pattern (spang-a-lang)
        addHit(p, 0, DrumMapping::RIDE, 85);
        addHit(p, 640, DrumMapping::RIDE, 65);  // Triplet
        addHit(p, 960, DrumMapping::RIDE, 80);
        addHit(p, 1600, DrumMapping::RIDE, 65);
        addHit(p, 1920, DrumMapping::RIDE, 85);
        addHit(p, 2560, DrumMapping::RIDE, 65);
        addHit(p, 2880, DrumMapping::RIDE, 80);
        addHit(p, 3520, DrumMapping::RIDE, 65);

        // Light hi-hat on 2 and 4
        addHit(p, 960, DrumMapping::HI_HAT_PEDAL, 55);
        addHit(p, 2880, DrumMapping::HI_HAT_PEDAL, 55);

        // Sparse kick
        addHit(p, 0, DrumMapping::KICK, 70);
        addHit(p, 2400, DrumMapping::KICK, 65);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Reggae One Drop
    {
        PatternPhrase p;
        p.id = "reggae_one_drop";
        p.style = "Alternative";
        p.category = "groove";
        p.tags = "reggae,one-drop,laid-back";
        p.bars = 1;
        p.energy = 0.55f;

        // Kick and snare together on beat 3 (the "one drop")
        addHit(p, 1920, DrumMapping::KICK, 105);
        addHit(p, 1920, DrumMapping::SNARE, 100);

        // Rim click on 2 and 4
        addHit(p, 960, DrumMapping::SNARE, 60);  // Light rim
        addHit(p, 2880, DrumMapping::SNARE, 62);

        // Hi-hat pattern
        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 75 : 55;
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Disco/Funk 4-on-floor
    {
        PatternPhrase p;
        p.id = "disco_classic";
        p.style = "Electronic";
        p.category = "groove";
        p.tags = "disco,funk,upbeat";
        p.bars = 1;
        p.energy = 0.8f;

        // Four on floor kick
        for (int i = 0; i < 4; ++i)
            addHit(p, i * 960, DrumMapping::KICK, 112);

        // Snare on 2 and 4
        addHit(p, 960, DrumMapping::SNARE, 105);
        addHit(p, 2880, DrumMapping::SNARE, 108);

        // Open hi-hat on upbeats (disco signature)
        for (int i = 0; i < 4; ++i)
            addHit(p, i * 960 + 480, DrumMapping::HI_HAT_OPEN, 95);

        // Closed hi-hats on downbeats
        for (int i = 0; i < 4; ++i)
            addHit(p, i * 960, DrumMapping::HI_HAT_CLOSED, 80);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Motown
    {
        PatternPhrase p;
        p.id = "rnb_motown";
        p.style = "R&B";
        p.category = "groove";
        p.tags = "motown,soul,classic";
        p.bars = 1;
        p.energy = 0.65f;

        addHit(p, 0, DrumMapping::KICK, 100);
        addHit(p, 1920, DrumMapping::KICK, 95);

        addHit(p, 960, DrumMapping::SNARE, 100);
        addHit(p, 2880, DrumMapping::SNARE, 102);

        // Tambourine-style 8ths
        for (int i = 0; i < 8; ++i)
        {
            int vel = (i % 2 == 0) ? 75 : 60;
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, vel);
        }

        // Light ghost notes
        addHit(p, 720, DrumMapping::SNARE, 35);
        addHit(p, 2640, DrumMapping::SNARE, 38);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Half-time shuffle (Rosanna/Toto)
    {
        PatternPhrase p;
        p.id = "rock_halftime_shuffle";
        p.style = "Rock";
        p.category = "groove";
        p.tags = "shuffle,halftime,complex,ghost-notes";
        p.bars = 1;
        p.energy = 0.75f;
        p.swing = 0.15f;

        addHit(p, 0, DrumMapping::KICK, 105);
        addHit(p, 0, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 320, DrumMapping::SNARE, 32);  // Ghost
        addHit(p, 320, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 480, DrumMapping::SNARE, 38);
        addHit(p, 640, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 960, DrumMapping::SNARE, 105);
        addHit(p, 960, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 1280, DrumMapping::SNARE, 35);
        addHit(p, 1280, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 1440, DrumMapping::SNARE, 40);
        addHit(p, 1600, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 1920, DrumMapping::KICK, 100);
        addHit(p, 1920, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 2240, DrumMapping::SNARE, 30);
        addHit(p, 2240, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 2400, DrumMapping::KICK, 95);
        addHit(p, 2560, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 2880, DrumMapping::SNARE, 108);
        addHit(p, 2880, DrumMapping::HI_HAT_CLOSED, 85);
        addHit(p, 3200, DrumMapping::SNARE, 35);
        addHit(p, 3200, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 3360, DrumMapping::SNARE, 42);
        addHit(p, 3520, DrumMapping::HI_HAT_CLOSED, 75);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Metal double-kick
    {
        PatternPhrase p;
        p.id = "metal_double_kick";
        p.style = "Rock";
        p.category = "groove";
        p.tags = "metal,heavy,double-kick";
        p.bars = 1;
        p.energy = 0.95f;

        // 16th note double kick
        for (int i = 0; i < 16; ++i)
            addHit(p, i * 240, DrumMapping::KICK, 110 + (i % 2 == 0 ? 5 : 0));

        // Snare on 2 and 4
        addHit(p, 960, DrumMapping::SNARE, 120);
        addHit(p, 2880, DrumMapping::SNARE, 122);

        // China/Crash pattern
        addHit(p, 0, DrumMapping::CRASH_1, 100);
        addHit(p, 1920, DrumMapping::CRASH_1, 95);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Punk rock (fast)
    {
        PatternPhrase p;
        p.id = "rock_punk";
        p.style = "Rock";
        p.category = "groove";
        p.tags = "punk,fast,energetic";
        p.bars = 1;
        p.energy = 0.9f;

        // Fast 8th note kick/snare
        for (int i = 0; i < 8; ++i)
        {
            if (i % 2 == 0)
                addHit(p, i * 480, DrumMapping::KICK, 115);
            else
                addHit(p, i * 480, DrumMapping::SNARE, 112);
        }

        // 8th note hi-hats
        for (int i = 0; i < 8; ++i)
            addHit(p, i * 480, DrumMapping::HI_HAT_CLOSED, 90);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Lo-fi hip-hop
    {
        PatternPhrase p;
        p.id = "hiphop_lofi";
        p.style = "HipHop";
        p.category = "groove";
        p.tags = "lofi,chill,minimal";
        p.bars = 1;
        p.energy = 0.45f;
        p.swing = 0.18f;

        addHit(p, 0, DrumMapping::KICK, 85);
        addHit(p, 1680, DrumMapping::KICK, 75);

        addHit(p, 960, DrumMapping::SNARE, 80);
        addHit(p, 2880, DrumMapping::SNARE, 82);

        // Sparse hats
        addHit(p, 0, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 960, DrumMapping::HI_HAT_CLOSED, 50);
        addHit(p, 1920, DrumMapping::HI_HAT_CLOSED, 55);
        addHit(p, 2880, DrumMapping::HI_HAT_CLOSED, 50);

        // Subtle ghost
        addHit(p, 720, DrumMapping::SNARE, 28);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Afrobeats
    {
        PatternPhrase p;
        p.id = "afrobeats_1";
        p.style = "R&B";
        p.category = "groove";
        p.tags = "afrobeats,world,rhythmic";
        p.bars = 1;
        p.energy = 0.7f;

        addHit(p, 0, DrumMapping::KICK, 100);
        addHit(p, 720, DrumMapping::KICK, 85);
        addHit(p, 1440, DrumMapping::KICK, 90);
        addHit(p, 1920, DrumMapping::KICK, 95);
        addHit(p, 2640, DrumMapping::KICK, 88);

        addHit(p, 480, DrumMapping::SNARE, 95);
        addHit(p, 1920, DrumMapping::SNARE, 100);
        addHit(p, 3360, DrumMapping::SNARE, 92);

        // Shaker-like hi-hat pattern
        for (int i = 0; i < 16; ++i)
        {
            int vel = 50 + (i % 4 == 0 ? 25 : (i % 2 == 0 ? 12 : 0));
            addHit(p, i * 240, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Dembow (reggaeton)
    {
        PatternPhrase p;
        p.id = "latin_dembow";
        p.style = "Electronic";
        p.category = "groove";
        p.tags = "reggaeton,dembow,latin";
        p.bars = 1;
        p.energy = 0.8f;

        // Classic dembow rhythm
        addHit(p, 0, DrumMapping::KICK, 115);
        addHit(p, 720, DrumMapping::KICK, 100);
        addHit(p, 1920, DrumMapping::KICK, 112);
        addHit(p, 2640, DrumMapping::KICK, 100);

        addHit(p, 480, DrumMapping::SNARE, 105);
        addHit(p, 1440, DrumMapping::SNARE, 108);
        addHit(p, 2400, DrumMapping::SNARE, 105);
        addHit(p, 3360, DrumMapping::SNARE, 108);

        for (int i = 0; i < 16; ++i)
        {
            int vel = (i % 4 == 0) ? 80 : 60;
            addHit(p, i * 240, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // UK Garage
    {
        PatternPhrase p;
        p.id = "electronic_ukgarage";
        p.style = "Electronic";
        p.category = "groove";
        p.tags = "garage,2step,uk";
        p.bars = 1;
        p.energy = 0.75f;
        p.swing = 0.12f;

        // Shuffled kick pattern
        addHit(p, 0, DrumMapping::KICK, 110);
        addHit(p, 720, DrumMapping::KICK, 95);
        addHit(p, 1680, DrumMapping::KICK, 100);
        addHit(p, 2880, DrumMapping::KICK, 105);

        addHit(p, 960, DrumMapping::SNARE, 102);
        addHit(p, 2400, DrumMapping::SNARE, 98);

        for (int i = 0; i < 16; ++i)
        {
            int vel = (i % 4 == 0) ? 75 : (i % 2 == 0) ? 65 : 50;
            addHit(p, i * 240, DrumMapping::HI_HAT_CLOSED, vel);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // ============ ADDITIONAL FILLS ============

    // Syncopated fill
    {
        PatternPhrase p;
        p.id = "fill_syncopated";
        p.style = "Rock";
        p.category = "fill";
        p.tags = "syncopated,complex";
        p.bars = 1;
        p.energy = 0.85f;

        addHit(p, 0, DrumMapping::SNARE, 95);
        addHit(p, 240, DrumMapping::TOM_HIGH, 90);
        addHit(p, 720, DrumMapping::SNARE, 100);
        addHit(p, 960, DrumMapping::TOM_MID, 95);
        addHit(p, 1200, DrumMapping::SNARE, 92);
        addHit(p, 1680, DrumMapping::TOM_LOW, 100);
        addHit(p, 1920, DrumMapping::TOM_FLOOR, 105);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Triplet fill
    {
        PatternPhrase p;
        p.id = "fill_triplet";
        p.style = "R&B";
        p.category = "fill";
        p.tags = "triplet,smooth";
        p.bars = 1;
        p.energy = 0.7f;

        // 8th note triplets
        for (int i = 0; i < 12; ++i)
        {
            int tick = i * 320;  // Triplet spacing
            DrumMapping::DrumElement elem;
            if (i < 4) elem = DrumMapping::SNARE;
            else if (i < 8) elem = DrumMapping::TOM_HIGH;
            else elem = DrumMapping::TOM_MID;
            addHit(p, tick, elem, 85 + (i * 3));
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Linear fill (no simultaneous hits)
    {
        PatternPhrase p;
        p.id = "fill_linear";
        p.style = "HipHop";
        p.category = "fill";
        p.tags = "linear,modern";
        p.bars = 1;
        p.energy = 0.75f;

        addHit(p, 0, DrumMapping::KICK, 100);
        addHit(p, 240, DrumMapping::SNARE, 90);
        addHit(p, 480, DrumMapping::HI_HAT_CLOSED, 75);
        addHit(p, 720, DrumMapping::SNARE, 95);
        addHit(p, 960, DrumMapping::TOM_HIGH, 100);
        addHit(p, 1200, DrumMapping::KICK, 95);
        addHit(p, 1440, DrumMapping::TOM_MID, 98);
        addHit(p, 1680, DrumMapping::SNARE, 105);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Blast beat fill (metal)
    {
        PatternPhrase p;
        p.id = "fill_blast";
        p.style = "Rock";
        p.category = "fill";
        p.tags = "metal,blast,extreme";
        p.bars = 1;
        p.energy = 1.0f;

        // 32nd note alternating kick/snare
        for (int i = 0; i < 16; ++i)
        {
            addHit(p, i * 120, DrumMapping::KICK, 115);
            addHit(p, i * 120 + 60, DrumMapping::SNARE, 112);
        }

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Electronic fill (buildup)
    {
        PatternPhrase p;
        p.id = "fill_electronic_buildup";
        p.style = "Electronic";
        p.category = "fill";
        p.tags = "buildup,edm,riser";
        p.bars = 1;
        p.energy = 0.9f;

        // Snare roll building in velocity
        for (int i = 0; i < 16; ++i)
        {
            int vel = 60 + (i * 4);
            addHit(p, i * 240, DrumMapping::SNARE, vel);
        }

        // Kick accents
        addHit(p, 0, DrumMapping::KICK, 90);
        addHit(p, 960, DrumMapping::KICK, 100);
        addHit(p, 1920, DrumMapping::KICK, 110);
        addHit(p, 2880, DrumMapping::KICK, 120);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }

    // Trap fill (hi-hat roll)
    {
        PatternPhrase p;
        p.id = "fill_trap_hatroll";
        p.style = "Trap";
        p.category = "fill";
        p.tags = "trap,hihat,roll";
        p.bars = 1;
        p.energy = 0.8f;

        // 32nd note hi-hat roll
        for (int i = 0; i < 32; ++i)
        {
            int vel = 55 + (i % 4 == 0 ? 20 : (i % 2 == 0 ? 10 : 0));
            // Crescendo
            vel += (i * 2);
            addHit(p, i * 120, DrumMapping::HI_HAT_CLOSED, std::min(vel, 115));
        }

        // Snare hits
        addHit(p, 1920, DrumMapping::SNARE, 110);

        p.calculateCharacteristics();
        patterns.push_back(p);
    }
}

//==============================================================================
// PatternVariator implementation
//==============================================================================

PatternVariator::PatternVariator()
{
    std::random_device rd;
    rng.seed(rd());
    initInstrumentTimings();
}

void PatternVariator::initInstrumentTimings()
{
    // Kick: slightly ahead of beat, tight timing
    instrumentTimings[DrumMapping::KICK] = {-1.0f, 3.0f, 1.0f};

    // Snare: on the beat, moderate variation
    instrumentTimings[DrumMapping::SNARE] = {0.0f, 5.0f, 1.0f};

    // Hi-hats: slight variation, consistent velocity
    instrumentTimings[DrumMapping::HI_HAT_CLOSED] = {0.0f, 4.0f, 0.9f};
    instrumentTimings[DrumMapping::HI_HAT_OPEN] = {0.0f, 4.0f, 1.0f};

    // Toms: behind the beat slightly
    instrumentTimings[DrumMapping::TOM_HIGH] = {2.0f, 6.0f, 1.0f};
    instrumentTimings[DrumMapping::TOM_MID] = {2.5f, 6.0f, 1.0f};
    instrumentTimings[DrumMapping::TOM_LOW] = {3.0f, 7.0f, 1.0f};
    instrumentTimings[DrumMapping::TOM_FLOOR] = {3.5f, 7.0f, 1.0f};

    // Cymbals: slight push
    instrumentTimings[DrumMapping::CRASH_1] = {-2.0f, 5.0f, 1.0f};
    instrumentTimings[DrumMapping::RIDE] = {0.0f, 3.0f, 0.95f};

    // Clap: on beat
    instrumentTimings[DrumMapping::CLAP] = {0.0f, 4.0f, 1.0f};
}

void PatternVariator::applyVelocityVariation(PatternPhrase& pattern, float amount, bool useGaussian)
{
    if (amount <= 0.0f)
        return;

    float maxVar = amount * 20.0f;  // 100% = ±20 velocity

    for (auto& hit : pattern.hits)
    {
        float variation;
        if (useGaussian)
        {
            variation = gaussianDist(rng) * maxVar * 0.33f;  // 3 sigma = maxVar
        }
        else
        {
            variation = (uniformDist(rng) * 2.0f - 1.0f) * maxVar;
        }

        hit.velocity = std::clamp(hit.velocity + static_cast<int>(variation), 1, 127);
    }
}

void PatternVariator::applyTimingVariation(PatternPhrase& pattern, float amountMs, double bpm, bool useGaussian)
{
    if (amountMs <= 0.0f || bpm <= 0.0)
        return;

    // Convert ms to ticks
    double ticksPerMs = (960.0 * bpm) / 60000.0;

    // Calculate pattern's maximum tick position (pattern length in ticks)
    // Formula: bars * beatsPerBar * ticksPerBeat, where ticksPerBeat = 960 * 4 / timeSigDenom
    int patternMaxTicks = pattern.bars * pattern.timeSigNum * 960 * 4 / pattern.timeSigDenom;
    // Subtract 1 to keep hits strictly within pattern bounds
    patternMaxTicks = std::max(1, patternMaxTicks - 1);

    for (auto& hit : pattern.hits)
    {
        float variationMs;
        if (useGaussian)
        {
            variationMs = gaussianDist(rng) * amountMs * 0.33f;
        }
        else
        {
            variationMs = (uniformDist(rng) * 2.0f - 1.0f) * amountMs;
        }

        int variationTicks = static_cast<int>(variationMs * ticksPerMs);
        hit.tick = std::clamp(hit.tick + variationTicks, 0, patternMaxTicks);
    }

    // Re-sort after timing changes
    std::sort(pattern.hits.begin(), pattern.hits.end());
}

void PatternVariator::applyInstrumentTiming(PatternPhrase& pattern, double bpm)
{
    if (bpm <= 0.0)
        return;

    double ticksPerMs = (960.0 * bpm) / 60000.0;

    // Calculate pattern's maximum tick position (pattern length in ticks)
    int patternMaxTicks = pattern.bars * pattern.timeSigNum * 960 * 4 / pattern.timeSigDenom;
    patternMaxTicks = std::max(1, patternMaxTicks - 1);

    for (auto& hit : pattern.hits)
    {
        auto it = instrumentTimings.find(hit.element);
        if (it != instrumentTimings.end())
        {
            const auto& timing = it->second;

            // Apply mean offset + gaussian variation
            float offsetMs = timing.meanOffset + gaussianDist(rng) * timing.stdDev;
            int offsetTicks = static_cast<int>(offsetMs * ticksPerMs);

            hit.tick = std::clamp(hit.tick + offsetTicks, 0, patternMaxTicks);

            // Apply velocity scale
            hit.velocity = std::clamp(
                static_cast<int>(static_cast<float>(hit.velocity) * timing.velocityScale),
                1, 127);
        }
    }

    std::sort(pattern.hits.begin(), pattern.hits.end());
}

void PatternVariator::applySubstitutions(PatternPhrase& pattern, float probability)
{
    if (probability <= 0.0f)
        return;

    for (auto& hit : pattern.hits)
    {
        if (uniformDist(rng) > probability)
            continue;

        // Substitute based on element type
        switch (hit.element)
        {
            case DrumMapping::HI_HAT_CLOSED:
                // Occasionally open the hi-hat
                if (uniformDist(rng) < 0.3f)
                    hit.element = DrumMapping::HI_HAT_OPEN;
                break;

            case DrumMapping::SNARE:
                // Ghost notes can become slightly louder or quieter
                if (hit.velocity < 50)
                {
                    hit.velocity = std::clamp(
                        hit.velocity + static_cast<int>((uniformDist(rng) - 0.5f) * 20),
                        20, 55);
                }
                break;

            case DrumMapping::CRASH_1:
                // Swap crash types occasionally
                if (uniformDist(rng) < 0.2f)
                    hit.element = DrumMapping::CRASH_2;
                break;

            default:
                break;
        }
    }
}

void PatternVariator::adjustGhostNotes(PatternPhrase& pattern, float targetDensity)
{
    // Count current ghost notes
    int totalSnare = 0;
    int ghostCount = 0;

    for (const auto& hit : pattern.hits)
    {
        if (hit.element == DrumMapping::SNARE)
        {
            totalSnare++;
            if (hit.velocity < 55)
                ghostCount++;
        }
    }

    float currentDensity = (totalSnare > 0) ?
                           static_cast<float>(ghostCount) / static_cast<float>(totalSnare) : 0.0f;

    if (std::abs(currentDensity - targetDensity) < 0.1f)
        return;  // Close enough

    if (targetDensity > currentDensity)
    {
        // Add ghost notes on 16th note positions
        int ticksPerBar = 960 * pattern.timeSigNum * 4 / pattern.timeSigDenom;
        int numPositions = pattern.bars * pattern.timeSigNum * 4;

        for (int i = 0; i < numPositions; ++i)
        {
            int tick = i * 240;  // 16th note positions

            // Skip positions that already have snare hits
            bool hasSnare = false;
            for (const auto& hit : pattern.hits)
            {
                if (hit.element == DrumMapping::SNARE &&
                    std::abs(hit.tick - tick) < 120)
                {
                    hasSnare = true;
                    break;
                }
            }

            if (!hasSnare && uniformDist(rng) < (targetDensity - currentDensity))
            {
                DrumHit ghost;
                ghost.tick = tick;
                ghost.element = DrumMapping::SNARE;
                ghost.velocity = 25 + static_cast<int>(uniformDist(rng) * 25);
                ghost.duration = 60;
                pattern.hits.push_back(ghost);
            }
        }

        std::sort(pattern.hits.begin(), pattern.hits.end());
    }
    else
    {
        // Remove some ghost notes
        pattern.hits.erase(
            std::remove_if(pattern.hits.begin(), pattern.hits.end(),
                [this, targetDensity, currentDensity](const DrumHit& hit) {
                    return hit.element == DrumMapping::SNARE &&
                           hit.velocity < 55 &&
                           uniformDist(rng) < (currentDensity - targetDensity);
                }),
            pattern.hits.end());
    }

    pattern.calculateCharacteristics();
}

void PatternVariator::applySwing(PatternPhrase& pattern, float swing, int division)
{
    if (swing <= 0.0f)
        return;

    int divisionTicks = (division == 8) ? 480 : 240;  // 8th or 16th

    for (auto& hit : pattern.hits)
    {
        int pairTicks = divisionTicks * 2;
        int posInPair = hit.tick % pairTicks;

        // Only swing upbeats
        if (posInPair >= divisionTicks && posInPair < pairTicks)
        {
            int swingOffset = static_cast<int>(divisionTicks * swing);
            hit.tick += swingOffset;
        }
    }

    std::sort(pattern.hits.begin(), pattern.hits.end());
}

void PatternVariator::scaleEnergy(PatternPhrase& pattern, float scale)
{
    if (scale == 1.0f)
        return;

    for (auto& hit : pattern.hits)
    {
        hit.velocity = std::clamp(
            static_cast<int>(static_cast<float>(hit.velocity) * scale),
            1, 127);
    }

    pattern.calculateCharacteristics();
}

void PatternVariator::humanize(PatternPhrase& pattern, float timingVar, float velocityVar, double bpm)
{
    // Apply per-instrument characteristics first
    applyInstrumentTiming(pattern, bpm);

    // Then add random variation
    float timingMs = (timingVar / 100.0f) * 15.0f;  // Max 15ms at 100%
    applyTimingVariation(pattern, timingMs, bpm, true);

    float velAmount = velocityVar / 100.0f;
    applyVelocityVariation(pattern, velAmount, true);

    // Occasional substitutions
    applySubstitutions(pattern, 0.05f);

    pattern.calculateCharacteristics();
}

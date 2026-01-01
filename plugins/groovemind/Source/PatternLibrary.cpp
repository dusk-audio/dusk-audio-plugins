/*
  ==============================================================================

    PatternLibrary.cpp
    Pattern library implementation

  ==============================================================================
*/

#include "PatternLibrary.h"

//==============================================================================
PatternLibrary::PatternLibrary()
{
}

//==============================================================================
bool PatternLibrary::loadFromDirectory(const juce::File& directory)
{
    if (!directory.isDirectory())
        return false;

    // Look for index.json
    auto indexFile = directory.getChildFile("index.json");
    if (!indexFile.existsAsFile())
    {
        DBG("PatternLibrary: No index.json found in " + directory.getFullPathName());
        return false;
    }

    // Parse index
    auto jsonText = indexFile.loadFileAsString();
    auto json = juce::JSON::parse(jsonText);

    if (!json.isObject())
    {
        DBG("PatternLibrary: Failed to parse index.json");
        return false;
    }

    auto patternsArray = json["patterns"];
    if (!patternsArray.isArray())
    {
        DBG("PatternLibrary: No patterns array in index.json");
        return false;
    }

    auto patternsDir = directory.getChildFile("patterns");

    int loadedCount = 0;
    for (int i = 0; i < patternsArray.size(); ++i)
    {
        auto patternJson = patternsArray[i];
        DrumPattern pattern;
        pattern.metadata = parseMetadataJson(patternJson);

        // Load corresponding MIDI file
        auto midiFile = patternsDir.getChildFile(pattern.metadata.id + ".mid");
        if (midiFile.existsAsFile())
        {
            if (loadMidiPattern(midiFile, pattern))
            {
                patternIdIndex[pattern.metadata.id] = patterns.size();
                patterns.push_back(std::move(pattern));
                loadedCount++;
            }
        }
    }

    DBG("PatternLibrary: Loaded " + juce::String(loadedCount) + " patterns");
    return loadedCount > 0;
}

//==============================================================================
bool PatternLibrary::loadFromBinaryData(const void* /*data*/, size_t /*size*/)
{
    // TODO: Implement loading from embedded binary resources
    // This will be used for the final plugin distribution
    return false;
}

//==============================================================================
PatternMetadata PatternLibrary::parseMetadataJson(const juce::var& json)
{
    PatternMetadata meta;

    meta.id = json["id"].toString();
    meta.name = json["name"].toString();
    meta.style = json["style"].toString();
    meta.substyle = json.getProperty("substyle", "").toString();
    meta.type = json["type"].toString();
    meta.section = json.getProperty("section", "any").toString();
    meta.bars = static_cast<int>(json.getProperty("bars", 4));
    meta.energy = static_cast<float>(json.getProperty("energy", 0.5));
    meta.complexity = static_cast<float>(json.getProperty("complexity", 0.5));
    meta.kit = json.getProperty("kit", "acoustic").toString();
    meta.timeSignature = json.getProperty("time_signature", "4/4").toString();

    // Tempo
    auto tempoObj = json["tempo"];
    if (tempoObj.isObject())
    {
        meta.tempoBpm = static_cast<int>(tempoObj.getProperty("bpm", 120));
        meta.tempoRangeMin = static_cast<int>(tempoObj.getProperty("range_min", 80));
        meta.tempoRangeMax = static_cast<int>(tempoObj.getProperty("range_max", 160));
        meta.tempoFeel = tempoObj.getProperty("feel", "medium").toString();
    }

    // Groove
    auto grooveObj = json["groove"];
    if (grooveObj.isObject())
    {
        meta.swing = static_cast<float>(grooveObj.getProperty("swing", 0.0));
        meta.pushPull = static_cast<float>(grooveObj.getProperty("push_pull", 0.0));
        meta.tightness = static_cast<float>(grooveObj.getProperty("tightness", 0.5));
    }

    // Instruments
    auto instObj = json["instruments"];
    if (instObj.isObject())
    {
        meta.hasKick = static_cast<bool>(instObj.getProperty("kick", true));
        meta.hasSnare = static_cast<bool>(instObj.getProperty("snare", true));
        meta.hasHihat = static_cast<bool>(instObj.getProperty("hihat", true));
        meta.hasRide = static_cast<bool>(instObj.getProperty("ride", false));
        meta.hasCrash = static_cast<bool>(instObj.getProperty("crash", false));
        meta.hasToms = static_cast<bool>(instObj.getProperty("toms", false));
    }

    // Articulations
    auto artObj = json["articulations"];
    if (artObj.isObject())
    {
        meta.hasGhostNotes = static_cast<bool>(artObj.getProperty("ghost_notes", false));
        meta.hasBrushSweeps = static_cast<bool>(artObj.getProperty("brush_sweeps", false));
        meta.hasCrossStick = static_cast<bool>(artObj.getProperty("cross_stick", false));
    }

    // Source
    auto srcObj = json["source"];
    if (srcObj.isObject())
    {
        meta.dataset = srcObj.getProperty("dataset", "").toString();
        meta.sourceFile = srcObj.getProperty("file", "").toString();
        meta.drummerId = srcObj.getProperty("drummer_id", "").toString();
    }

    // Tags
    auto tagsArray = json["tags"];
    if (tagsArray.isArray())
    {
        for (int i = 0; i < tagsArray.size(); ++i)
            meta.tags.add(tagsArray[i].toString());
    }

    // ML features
    auto mlObj = json["ml_features"];
    if (mlObj.isObject())
    {
        meta.velocityMean = static_cast<float>(mlObj.getProperty("velocity_mean", 64.0));
        meta.velocityStd = static_cast<float>(mlObj.getProperty("velocity_std", 20.0));
        meta.noteDensity = static_cast<float>(mlObj.getProperty("note_density", 8.0));
    }

    return meta;
}

//==============================================================================
bool PatternLibrary::loadMidiPattern(const juce::File& midiFile, DrumPattern& pattern)
{
    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk())
        return false;

    juce::MidiFile midi;
    if (!midi.readFrom(stream))
        return false;

    // Convert to time-based sequence
    midi.convertTimestampTicksToSeconds();

    // Get the first track with note events
    for (int track = 0; track < midi.getNumTracks(); ++track)
    {
        auto* trackSeq = midi.getTrack(track);
        if (trackSeq != nullptr && trackSeq->getNumEvents() > 0)
        {
            // Copy events
            pattern.midiData = *trackSeq;

            // Calculate length in beats
            double lastTime = 0.0;
            for (int i = 0; i < pattern.midiData.getNumEvents(); ++i)
            {
                auto time = pattern.midiData.getEventTime(i);
                if (time > lastTime)
                    lastTime = time;
            }

            // Convert seconds to beats using metadata tempo
            double beatsPerSecond = pattern.metadata.tempoBpm / 60.0;
            pattern.lengthInBeats = lastTime * beatsPerSecond;

            return true;
        }
    }

    return false;
}

//==============================================================================
float PatternLibrary::scorePattern(const DrumPattern& pattern, const PatternQuery& query) const
{
    float score = 0.0f;
    float totalWeight = 0.0f;

    // Style match (required)
    if (!query.style.isEmpty() && pattern.metadata.style != query.style)
        return -1.0f;  // Disqualify

    // Kit match (required if specified)
    if (!query.kit.isEmpty() && pattern.metadata.kit != query.kit)
        return -1.0f;

    // Type match (required)
    if (!query.type.isEmpty() && pattern.metadata.type != query.type)
        return -1.0f;

    // Brush sweeps requirement
    if (query.requireBrushSweeps && !pattern.metadata.hasBrushSweeps)
        return -1.0f;

    // Energy similarity
    float energyDiff = std::abs(pattern.metadata.energy - query.targetEnergy);
    score += (1.0f - energyDiff) * query.energyWeight;
    totalWeight += query.energyWeight;

    // Complexity similarity
    float complexityDiff = std::abs(pattern.metadata.complexity - query.targetComplexity);
    score += (1.0f - complexityDiff) * query.complexityWeight;
    totalWeight += query.complexityWeight;

    // Tempo compatibility
    if (query.targetTempo >= pattern.metadata.tempoRangeMin &&
        query.targetTempo <= pattern.metadata.tempoRangeMax)
    {
        score += query.tempoWeight;
    }
    else
    {
        // Penalize based on distance from range
        int distance = std::min(
            std::abs(query.targetTempo - pattern.metadata.tempoRangeMin),
            std::abs(query.targetTempo - pattern.metadata.tempoRangeMax)
        );
        float tempoPenalty = juce::jlimit(0.0f, 1.0f, distance / 50.0f);
        score += (1.0f - tempoPenalty) * query.tempoWeight * 0.5f;
    }
    totalWeight += query.tempoWeight;

    // Section bonus
    if (!query.section.isEmpty() && pattern.metadata.section == query.section)
        score += 0.2f;

    // Avoid recently used patterns
    for (const auto& recentId : recentPatternIds)
    {
        if (recentId == pattern.metadata.id)
        {
            score *= 0.3f;  // Heavy penalty for recent use
            break;
        }
    }

    return totalWeight > 0 ? score / totalWeight : 0.0f;
}

//==============================================================================
const DrumPattern* PatternLibrary::selectPattern(const PatternQuery& query) const
{
    auto matches = findMatchingPatterns(query, 5);

    if (matches.empty())
        return nullptr;

    // Add some randomization among top matches
    int index = random.nextInt(std::min(3, static_cast<int>(matches.size())));
    auto* selected = matches[index];

    // Update recent history
    if (selected != nullptr)
    {
        recentPatternIds.insert(recentPatternIds.begin(), selected->metadata.id);
        if (recentPatternIds.size() > maxRecentHistory)
            recentPatternIds.pop_back();
    }

    return selected;
}

//==============================================================================
const DrumPattern* PatternLibrary::selectFill(const PatternQuery& query, int fillLengthBeats) const
{
    PatternQuery fillQuery = query;
    fillQuery.type = "fill";

    auto matches = findMatchingPatterns(fillQuery, 10);

    // Filter by length
    std::vector<const DrumPattern*> lengthMatches;
    for (auto* p : matches)
    {
        // Allow fills within ±1 bar of target
        if (std::abs(p->lengthInBeats - fillLengthBeats) <= 4)
            lengthMatches.push_back(p);
    }

    if (lengthMatches.empty())
        return matches.empty() ? nullptr : matches[0];

    int index = random.nextInt(std::min(3, static_cast<int>(lengthMatches.size())));
    return lengthMatches[index];
}

//==============================================================================
std::vector<const DrumPattern*> PatternLibrary::findMatchingPatterns(
    const PatternQuery& query, int maxResults) const
{
    std::vector<std::pair<float, const DrumPattern*>> scored;

    for (const auto& pattern : patterns)
    {
        float score = scorePattern(pattern, query);
        if (score >= 0)  // Not disqualified
            scored.push_back({score, &pattern});
    }

    // Sort by score descending
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Return top results
    std::vector<const DrumPattern*> results;
    for (int i = 0; i < std::min(maxResults, static_cast<int>(scored.size())); ++i)
        results.push_back(scored[i].second);

    return results;
}

//==============================================================================
const DrumPattern* PatternLibrary::getPatternById(const juce::String& id) const
{
    auto it = patternIdIndex.find(id);
    if (it != patternIdIndex.end())
        return &patterns[it->second];
    return nullptr;
}

//==============================================================================
const DrumPattern* PatternLibrary::getRandomPattern(const juce::String& style,
                                                     const juce::String& type) const
{
    std::vector<const DrumPattern*> matches;
    for (const auto& pattern : patterns)
    {
        if ((style.isEmpty() || pattern.metadata.style == style) &&
            (type.isEmpty() || pattern.metadata.type == type))
        {
            matches.push_back(&pattern);
        }
    }

    if (matches.empty())
        return nullptr;

    return matches[random.nextInt(matches.size())];
}

//==============================================================================
juce::StringArray PatternLibrary::getAvailableStyles() const
{
    juce::StringArray styles;
    for (const auto& pattern : patterns)
    {
        if (!styles.contains(pattern.metadata.style))
            styles.add(pattern.metadata.style);
    }
    styles.sort(false);
    return styles;
}

juce::StringArray PatternLibrary::getAvailableKits() const
{
    juce::StringArray kits;
    for (const auto& pattern : patterns)
    {
        if (!kits.contains(pattern.metadata.kit))
            kits.add(pattern.metadata.kit);
    }
    kits.sort(false);
    return kits;
}

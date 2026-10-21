#include "ChordAnalyzer.h"
#include <utility>

namespace
{
using TextModel = duskaudio::chord::ChordAnalyzerText;
juce::String toJuce(const std::string& text) { return juce::String::fromUTF8(text.c_str()); }
ChordFacts factsOf(const ChordInfo& chord) noexcept
{
    ChordFacts facts;
    facts.rootNote = chord.rootNote;
    facts.bassNote = chord.bassNote;
    facts.quality = chord.quality;
    facts.inversion = chord.inversion;
    facts.slashBass = chord.slashBass;
    facts.isValid = chord.isValid;
    facts.confidence = chord.confidence;
    return facts;
}
}

ChordInfo ChordAnalyzer::analyze(const std::vector<int>& midiNotes)
{
    auto description = TextModel::analyze(midiNotes);
    ChordInfo result;
    result.name = toJuce(description.name);
    result.romanNumeral = toJuce(description.romanNumeral);
    result.function = description.function;
    result.midiNotes = std::move(description.midiNotes);
    result.rootNote = description.rootNote;
    result.bassNote = description.bassNote;
    result.quality = description.quality;
    result.extensions = toJuce(description.extensions);
    result.inversion = description.inversion;
    result.slashBass = description.slashBass;
    result.isValid = description.isValid;
    result.confidence = description.confidence;
    return result;
}

juce::String ChordAnalyzer::getKeyName() const { return toJuce(TextModel::getKeyName()); }
std::vector<ChordSuggestion> ChordAnalyzer::getSuggestions(const ChordInfo& currentChord, SuggestionCategory level) const
{
    const auto descriptions = TextModel::getSuggestions(factsOf(currentChord), level);
    std::vector<ChordSuggestion> out;
    out.reserve(descriptions.size());
    for (const auto& description : descriptions)
    {
        ChordSuggestion suggestion;
        suggestion.romanNumeral = toJuce(description.romanNumeral);
        suggestion.chordName = toJuce(description.chordName);
        suggestion.category = description.category;
        suggestion.reason = toJuce(description.reason);
        suggestion.commonality = description.commonality;
        out.push_back(std::move(suggestion));
    }
    return out;
}

int ChordAnalyzer::encodeLabel(const ChordInfo& chord, bool showInversions) noexcept
{
    return TextModel::encodeLabel(factsOf(chord), chord.midiNotes.data(), static_cast<int>(chord.midiNotes.size()), showInversions);
}
juce::String ChordAnalyzer::decodeLabel(int code) { return toJuce(TextModel::decodeLabel(code)); }
int ChordAnalyzer::labelToCode(const juce::String& text)
{
    return TextModel::labelToCode(text.trim().toStdString());
}
juce::String ChordAnalyzer::noteToName(int note, bool useFlats) { return toJuce(TextModel::noteToName(note, useFlats)); }
juce::String ChordAnalyzer::pitchClassToName(int pitch, bool useFlats) { return toJuce(TextModel::pitchClassToName(pitch, useFlats)); }
int ChordAnalyzer::nameToNote(const juce::String& text)
{
    // Keep the shipping API's Unicode normalization; the shared model parses
    // canonical ASCII spellings without consulting a process-wide locale.
    return TextModel::nameToNote(text.toUpperCase().trim().toStdString());
}
juce::String ChordAnalyzer::qualityToString(ChordQuality quality) { return toJuce(TextModel::qualityToString(quality)); }
juce::String ChordAnalyzer::qualityToSuffix(ChordQuality quality) { return toJuce(TextModel::qualityToSuffix(quality)); }
juce::String ChordAnalyzer::functionToString(HarmonicFunction function) { return toJuce(TextModel::functionToString(function)); }

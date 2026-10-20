// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "ChordAnalyzerText.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace duskaudio::chord
{
enum ParameterId : unsigned
{
    PARAM_KEY_ROOT, PARAM_KEY_MODE, PARAM_SUGGESTION_LEVEL,
    PARAM_SHOW_INVERSIONS, PARAM_RESPECT_SUSTAIN,
    PARAM_DETECTED_ROOT, PARAM_DETECTED_QUALITY, PARAM_DETECTED_BASS,
    PARAM_DETECTED_INVERSION, PARAM_DETECTED_CHORD, PARAM_COUNT
};
inline constexpr unsigned inputParameterCount = PARAM_DETECTED_ROOT;
inline constexpr const char* keyRootLabels[] = {"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
inline constexpr const char* keyModeLabels[] = {"Major", "Minor"};
inline constexpr const char* suggestionLabels[] = {"Basic Only", "Basic + Intermediate", "All (+ Advanced)"};
inline constexpr const char* detectedPitchLabels[] = {"-", "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
inline constexpr const char* detectedQualityLabels[] = {"-", "maj", "m", "dim", "aug", "7", "maj7", "m7", "mMaj7", "dim7", "m7b5", "aug7", "augMaj7", "sus2", "sus4", "7sus4", "add9", "add11", "6", "m6", "maj9", "m9", "9", "maj11", "m11", "11", "maj13", "m13", "13", "5", "7b5", "7#5", "7b9", "7#9"};
inline constexpr const char* detectedInversionLabels[] = {"-", "Root", "1st", "2nd", "3rd", "4th", "5th", "6th", "Slash"};
struct ParameterChoices { const char* const* labels = nullptr; unsigned count = 0; };
template<std::size_t N>
constexpr ParameterChoices choices(const char* const (&labels)[N]) noexcept
{ return {labels, static_cast<unsigned>(N)}; }
inline constexpr ParameterChoices parameterChoices(unsigned index) noexcept
{
    switch (index)
    {
    case PARAM_KEY_ROOT: return choices(keyRootLabels);
    case PARAM_KEY_MODE: return choices(keyModeLabels);
    case PARAM_SUGGESTION_LEVEL: return choices(suggestionLabels);
    case PARAM_DETECTED_ROOT: case PARAM_DETECTED_BASS: return choices(detectedPitchLabels);
    case PARAM_DETECTED_QUALITY: return choices(detectedQualityLabels);
    case PARAM_DETECTED_INVERSION: return choices(detectedInversionLabels);
    default: return {};
    }
}
struct ParameterSpec
{
    const char* symbol;
    const char* name;
    float maximum;
    float initial;
    bool output;
    bool boolean;
};
// Preserve desktop parameter order, symbols, choice indices and label encoding.
inline constexpr std::array<ParameterSpec, PARAM_COUNT> parameterSpecs {{
    {"keyRoot", "Key Root", 11, 0, false, false},
    {"keyMode", "Key Mode", 1, 0, false, false},
    {"suggestionLevel", "Suggestion Level", 2, 2, false, false},
    {"showInversions", "Show Inversions", 1, 1, false, true},
    {"respectSustain", "Respect Sustain", 1, 1, false, true},
    {"detectedRoot", "Detected Root", 12, 0, true, false},
    {"detectedQuality", "Detected Quality", static_cast<float>(ChordQuality::Unknown), 0, true, false},
    {"detectedBass", "Detected Bass", 12, 0, true, false},
    {"detectedInversion", "Detected Inversion", kInversionSlashBass + 1, 0, true, false},
    {"detectedChord", "Detected Chord", ChordAnalyzerText::maxLabelCode, 0, true, false}
}};
static_assert(choices(detectedQualityLabels).count == static_cast<unsigned>(ChordQuality::Unknown) + 1);
static_assert(choices(detectedInversionLabels).count == kInversionSlashBass + 2);
inline float snapParameter(unsigned index, float value) noexcept
{
    return std::round(std::clamp(value, 0.0f, parameterSpecs[index].maximum));
}
} // namespace duskaudio::chord

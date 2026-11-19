#pragma once

#include <juce_core/juce_core.h>

/**
 * Opto Gain knob <-> dB mapping.
 *
 * The "opto_gain" parameter is a 0-100 hardware-style dial, not a dB value:
 * 50 is unity and each unit is 0.8 dB, giving a +/-40 dB span. Writing a raw dB
 * figure into it lands wildly off (5 dB read as knob 5 = -36 dB), so every
 * conversion goes through these helpers rather than open-coding the constants.
 */
namespace MultiComp
{

constexpr float kOptoGainKnobUnity   = 50.0f;   // knob position for 0 dB
constexpr float kOptoGainDbPerUnit   = 0.8f;    // dB per knob unit
constexpr float kOptoGainMaxDb       = 40.0f;   // +/- span at the knob extremes

/** Knob position (0-100) -> gain in dB (-40..+40). */
inline float optoKnobToGainDb (float knob) noexcept
{
    const float clamped = juce::jlimit (0.0f, 100.0f, knob);
    return juce::jlimit (-kOptoGainMaxDb, kOptoGainMaxDb,
                         (clamped - kOptoGainKnobUnity) * kOptoGainDbPerUnit);
}

/** Gain in dB -> knob position (0-100). Inverse of optoKnobToGainDb. */
inline float optoGainDbToKnob (float db) noexcept
{
    const float clamped = juce::jlimit (-kOptoGainMaxDb, kOptoGainMaxDb, db);
    return juce::jlimit (0.0f, 100.0f,
                         kOptoGainKnobUnity + clamped / kOptoGainDbPerUnit);
}

} // namespace MultiComp

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbFormat.hpp — how DuskVerb 2's editor prints a value and names a knob,
// kept out of the ImGui file so DuskVerbPluginLayerTests can hold it to the
// JUCE editor's formatValue() / valueOverride / relabel rules string for string.

#pragma once

#include "../core/DuskVerbParamTable.hpp"
#include "dsp/AlgorithmConfig.h"
#include "../../shared-daf/ui/DuskValueText.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace duskverb::ui
{

// How the on-screen value is written. `unit` in the parameter table already
// separates Hz / dB / ms / s; the unit-less ones need a per-parameter choice,
// and it must match the JUCE editor's suffix table exactly (PluginEditor.cpp
// formatValue) or the two builds print different numbers for one state.
enum class ValueStyle { Percent, Multiplier, Plain, FromUnit };

inline void formatFixed(double value, int decimals, char* out, size_t n) noexcept
{
    if (n == 0) return;
    if (!duskdaf::value_text::finiteDouble(value)) { std::snprintf(out, n, "0"); return; }
    const bool negative = value < 0.0;
    if (negative) value = -value;
    double scale = 1.0;
    for (int i = 0; i < decimals; ++i) scale *= 10.0;
    unsigned long long rounded = static_cast<unsigned long long>(std::floor(value * scale + 0.5));
    unsigned long long whole = decimals > 0 ? rounded / static_cast<unsigned long long>(scale) : rounded;
    unsigned long long fraction = decimals > 0 ? rounded % static_cast<unsigned long long>(scale) : 0;
    int used = std::snprintf(out, n, "%s%llu", negative ? "-" : "", whole);
    if (used < 0 || static_cast<size_t>(used) >= n || decimals == 0) return;
    if (static_cast<size_t>(used + 1 + decimals) >= n) return;
    out[used++] = '.';
    for (int i = decimals - 1; i >= 0; --i)
    {
        const unsigned digit = static_cast<unsigned>(fraction % 10u);
        out[used + i] = static_cast<char>('0' + digit);
        fraction /= 10u;
    }
    out[used + decimals] = '\0';
}

inline ValueStyle valueStyleFor(int p)
{
    switch (p)
    {
        case duskverb::Mix:      case duskverb::Size:      case duskverb::ModDepth:
        case duskverb::Saturation: case duskverb::Diffusion: case duskverb::ErLevel:
        case duskverb::ErSize:   case duskverb::Width:     case duskverb::Duck:
        case duskverb::Character: case duskverb::MonoBelowDepth:
            return ValueStyle::Percent;
        case duskverb::BassMult: case duskverb::MidMult:   case duskverb::Damping:
            return ValueStyle::Multiplier;
        case duskverb::Tone:
            return ValueStyle::Plain;
        default:
            return ValueStyle::FromUnit;
    }
}

// Transcribed from DuskVerbEditor's formatValue(). Decay switches ms/s at
// 1 s, frequencies switch Hz/kHz at 1 kHz and gain two decimals below 100 Hz.
// Per-engine value-text overrides, transcribed from DuskVerbEditor's
// valueOverride lambdas: the Gated engine re-purposes mod_depth / mod_rate /
// diffusion / mid_mult as ATTACK / RELEASE / HOLD / THRESHOLD and the Shimmer
// engine re-purposes mod_depth / mod_rate as PITCH / FEEDBACK, so those knobs
// print the unit the engine actually applies (ms, dB, st, %) rather than the
// raw parameter. The parameter values themselves are untouched.
inline float displayNumber(EngineType engine, int p, float plain) noexcept;
inline bool formatEngineOverride(EngineType e, int p, float v, char* out, size_t n)
{
    if (e == EngineType::NonLinear)
    {
        switch (p)
        {
            case duskverb::ModDepth:   // ATTACK: 1 + depth*49 ms (NonLinearEngine::setModDepth)
                std::snprintf(out, n, "%d ms", (int)std::lround(displayNumber(e, p, v)));
                return true;
            case duskverb::ModRate:    // RELEASE: 5 + (Hz-0.1)/9.9 * 1995 ms
            {
                std::snprintf(out, n, "%d ms", (int)std::lround(displayNumber(e, p, v)));
                return true;
            }
            case duskverb::Diffusion:  // HOLD: diffusion * 500 ms (NonLinearEngine::setTankDiffusion)
                std::snprintf(out, n, "%d ms", (int)std::lround(displayNumber(e, p, v)));
                return true;
            case duskverb::MidMult:    // THRESHOLD: mid_mult 0.1..1.5 -> -60..0 dB
            {
                char number[32]; formatFixed(displayNumber(e, p, v), 1, number, sizeof(number));
                std::snprintf(out, n, "%s dB", number);
                return true;
            }
            default: return false;
        }
    }
    if (e == EngineType::Shimmer)
    {
        switch (p)
        {
            case duskverb::ModDepth:   // PITCH: depth 0..1 -> 0..24 semitones
            {
                const int semis = (int)std::lround(displayNumber(e, p, v));
                std::snprintf(out, n, "%s%d st", semis > 0 ? "+" : "", semis);
                return true;
            }
            case duskverb::ModRate:    // FEEDBACK: 0.1..10 Hz -> 0..95 %
            {
                std::snprintf(out, n, "%d %%", (int)std::lround(displayNumber(e, p, v)));
                return true;
            }
            default: return false;
        }
    }
    return false;
}

// Per-engine knob names, transcribed from DuskVerbEditor::updateEngine...:
// Spring re-labels DEPTH/RATE/DIFFUSION as SPRING LEN/DRIP/CHIRP, Shimmer as
// PITCH/FEEDBACK, the Gated engine as ATTACK/RELEASE/HOLD and MID MULT as
// THRESHOLD. Everything else keeps its universal name.
inline const char* labelFor(EngineType e, int p, const char* universal)
{
    const bool spring = e == EngineType::Spring, shimmer = e == EngineType::Shimmer,
               gated  = e == EngineType::NonLinear;
    switch (p)
    {
        case duskverb::ModDepth:  return spring ? "SPRING LEN" : shimmer ? "PITCH"    : gated ? "ATTACK"  : universal;
        case duskverb::ModRate:   return spring ? "DRIP"       : shimmer ? "FEEDBACK" : gated ? "RELEASE" : universal;
        case duskverb::Diffusion: return gated  ? "HOLD"       : spring  ? "CHIRP"    : universal;
        case duskverb::MidMult:   return gated  ? "THRESHOLD"  : universal;
        default:                  return universal;
    }
}

inline float displayNumber(EngineType engine, int p, float plain) noexcept
{
    if (engine == EngineType::NonLinear)
    {
        if (p == duskverb::ModDepth) return 1.0f + std::clamp(plain, 0.0f, 1.0f) * 49.0f;
        if (p == duskverb::ModRate) return 5.0f + (std::clamp(plain, 0.1f, 10.0f) - 0.1f) / 9.9f * 1995.0f;
        if (p == duskverb::Diffusion) return std::clamp(plain, 0.0f, 1.0f) * 500.0f;
        if (p == duskverb::MidMult) return -60.0f + (std::clamp(plain, 0.1f, 1.5f) - 0.1f) / 1.4f * 60.0f;
    }
    if (engine == EngineType::Shimmer)
    {
        if (p == duskverb::ModDepth) return std::clamp(plain, 0.0f, 1.0f) * 24.0f;
        if (p == duskverb::ModRate) return (std::clamp(plain, 0.1f, 10.0f) - 0.1f) / 9.9f * 95.0f;
    }
    const auto& d = duskverb::paramDesc(p);
    switch (valueStyleFor(p))
    {
        case ValueStyle::Percent: return plain * 100.0f;
        default: break;
    }
    if (std::strcmp(d.unit, "s") == 0)
        return plain < 1.0f ? plain * 1000.0f : plain;
    if (std::strcmp(d.unit, "Hz") == 0)
        return plain >= 1000.0f ? plain / 1000.0f : plain;
    return plain;
}

inline float plainFromDisplayNumber(EngineType engine, int p, float number,
                                   float currentPlain) noexcept
{
    if (engine == EngineType::NonLinear)
    {
        if (p == duskverb::ModDepth) return (number - 1.0f) / 49.0f;
        if (p == duskverb::ModRate) return 0.1f + (number - 5.0f) / 1995.0f * 9.9f;
        if (p == duskverb::Diffusion) return number / 500.0f;
        if (p == duskverb::MidMult) return 0.1f + (number + 60.0f) / 60.0f * 1.4f;
    }
    if (engine == EngineType::Shimmer)
    {
        if (p == duskverb::ModDepth) return number / 24.0f;
        if (p == duskverb::ModRate) return 0.1f + number / 95.0f * 9.9f;
    }
    const auto& d = duskverb::paramDesc(p);
    if (valueStyleFor(p) == ValueStyle::Percent) return number / 100.0f;
    if (std::strcmp(d.unit, "s") == 0)
        return currentPlain < 1.0f ? number / 1000.0f : number;
    if (std::strcmp(d.unit, "Hz") == 0)
        return currentPlain >= 1000.0f ? number * 1000.0f : number;
    return number;
}

inline bool parsePlainValue(EngineType engine, int p, const char* text,
                            float currentPlain, float& plain) noexcept
{
    const char* end = nullptr;
    float number = 0.0f;
    if (!duskdaf::value_text::parseDecimal(text, end, number)) return false;
    const auto noSuffix = [&] { return duskdaf::value_text::finish(end, nullptr); };
    const auto has = [&](const char* a, const char* b = nullptr) {
        return duskdaf::value_text::suffix(end, a, b);
    };
    const bool bare = noSuffix();
    const auto& d = duskverb::paramDesc(p);
    bool accepted = false;
    float converted = number;

    const float milliseconds = has("s") ? number * 1000.0f : number;
    if (engine == EngineType::NonLinear && p == duskverb::ModDepth) { accepted = bare || has("ms") || has("s"); if (accepted) converted = (milliseconds - 1.0f) / 49.0f; }
    else if (engine == EngineType::NonLinear && p == duskverb::ModRate) { accepted = bare || has("ms") || has("s"); if (accepted) converted = (milliseconds - 5.0f) / 1995.0f * 9.9f + 0.1f; }
    else if (engine == EngineType::NonLinear && p == duskverb::Diffusion) { accepted = bare || has("ms") || has("s"); if (accepted) converted = milliseconds / 500.0f; }
    else if (engine == EngineType::NonLinear && p == duskverb::MidMult) { accepted = bare || has("dB"); if (accepted) converted = (number + 60.0f) / 60.0f * 1.4f + 0.1f; }
    else if (engine == EngineType::Shimmer && p == duskverb::ModDepth) { accepted = bare || has("st", "semitones"); if (accepted) converted = number / 24.0f; }
    else if (engine == EngineType::Shimmer && p == duskverb::ModRate) { accepted = bare || has("%"); if (accepted) converted = number / 95.0f * 9.9f + 0.1f; }
    else if (valueStyleFor(p) == ValueStyle::Percent) { accepted = bare || has("%"); if (accepted) converted = number / 100.0f; }
    else if (valueStyleFor(p) == ValueStyle::Multiplier) { accepted = bare || has("x"); }
    else if (valueStyleFor(p) == ValueStyle::Plain) { accepted = bare; }
    else if (std::strcmp(d.unit, "s") == 0)
    {
        const bool adaptiveMs = currentPlain < 1.0f;
        accepted = bare || has("ms") || has("s");
        if (accepted && bare) converted = adaptiveMs ? number / 1000.0f : number;
        else if (accepted && has("ms")) converted = number / 1000.0f;
    }
    else if (std::strcmp(d.unit, "ms") == 0) { accepted = bare || has("ms") || has("s"); converted = milliseconds; }
    else if (std::strcmp(d.unit, "Hz") == 0)
    {
        const bool adaptiveKHz = currentPlain >= 1000.0f;
        accepted = bare || has("Hz") || has("kHz");
        if (accepted && bare) converted = adaptiveKHz ? number * 1000.0f : number;
        else if (accepted && has("kHz")) converted = number * 1000.0f;
    }
    else if (std::strcmp(d.unit, "dB") == 0) { accepted = bare || has("dB"); }
    else if (d.unit[0] == 0) { accepted = bare; }
    if (!accepted || !duskdaf::value_text::finiteFloat(converted)) return false;
    converted = std::clamp(converted, d.min, d.max);
    if (d.interval > 0.0f) converted = duskverb::rangeSnap(d, converted);
    plain = converted;
    return duskdaf::value_text::finiteFloat(plain);
}

inline void formatPlainValue(EngineType engine, int p, float v, char* out, size_t n)
{
    if (formatEngineOverride(engine, p, v, out, n)) return;
    const duskverb::ParamDesc& d = duskverb::paramDesc(p);
    switch (valueStyleFor(p))
    {
        case ValueStyle::Percent: { char number[32]; formatFixed(v * 100.0, 1, number, sizeof(number)); std::snprintf(out, n, "%s%%", number); return; }
        case ValueStyle::Multiplier: { char number[32]; formatFixed(v, 2, number, sizeof(number)); std::snprintf(out, n, "%sx", number); return; }
        case ValueStyle::Plain: { formatFixed(v, 2, out, n); return; }
        case ValueStyle::FromUnit:   break;
    }
    if (std::strcmp(d.unit, "s") == 0)
    {
        if (v < 1.0f) std::snprintf(out, n, "%d ms", (int)std::lround(v * 1000.0f));
        else { char number[32]; formatFixed(v, 2, number, sizeof(number)); std::snprintf(out, n, "%s s", number); }
        return;
    }
    if (std::strcmp(d.unit, "ms") == 0) { std::snprintf(out, n, "%d ms", (int)std::lround(v)); return; }
    if (std::strcmp(d.unit, "Hz") == 0)
    {
        // Choose precision from the rounded display value so host-domain
        // conversion noise cannot alternate between "100 Hz" and "100.00 Hz".
        if (v >= 1000.0f) { char number[32]; formatFixed(v / 1000.0, 2, number, sizeof(number)); std::snprintf(out, n, "%s kHz", number); }
        else if (std::lround(v * 100.0f) < 10000) { char number[32]; formatFixed(v, 2, number, sizeof(number)); std::snprintf(out, n, "%s Hz", number); }
        else                  std::snprintf(out, n, "%d Hz", (int)std::lround(v));
        return;
    }
    if (std::strcmp(d.unit, "dB") == 0) { char number[32]; formatFixed(v, 1, number, sizeof(number)); std::snprintf(out, n, "%s dB", number); return; }
    formatFixed(v, 2, out, n);
}

// The hero read-out prints the number and its unit at two different sizes.
inline void formatDecayParts(float seconds, char* number, size_t nn, char* unit, size_t un)
{
    if (seconds < 1.0f)
    {
        std::snprintf(number, nn, "%d", (int)std::lround(seconds * 1000.0f));
        std::snprintf(unit, un, "ms");
    }
    else
    {
        formatFixed(seconds, 2, number, nn);
        std::snprintf(unit, un, "s");
    }
}
} // namespace duskverb::ui

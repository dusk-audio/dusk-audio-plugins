// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FourKEQUserPresetFile.hpp — the 4K EQ 2 user preset file format
// (~/.config/DuskAudio/FourKEQ2/presets/*.4kpreset), framework-free so the
// tests can read what the editor writes.
//
// key=value lines: format_version 2, then every sound-shaping parameter
// (fkIsPresetParam) under its parameter key. The band and filter frequency
// parameters are positions on the console's printed dial. A file states each
// as the frequency that position plays at the file's own mode, gain and shape
// (frequency_domain=effective_hz, FourKEQDSP::calibratedEqFrequency and
// calibratedFilterFrequency), and reading maps it back onto the dial through
// their inverses. With frequency_domain=control_hz, or no domain line (the
// unversioned files before version 2), a file states the dial positions
// themselves. These are the files 4K EQ 2 1.0.5 reads and writes.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>

#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"

static constexpr int kFourKUserPresetFormatVersion = 2;

// Clamp to range and quantise the discrete parameters exactly the way the
// DSP shell reads them, so the cached value can never disagree with what
// the DSP acts on. The UI's values[] is not display-only: it feeds preset
// identity matching and is what a saved user preset writes to disk.
inline float fkNormalizeParamValue(uint32_t idx, float v) noexcept
{
    v = std::max(kFourKParams[idx].min, std::min(kFourKParams[idx].max, v));
    switch (idx)
    {
    case kHpfEnabled: case kLpfEnabled: case kLfBell: case kHfBell:
    case kEqType: case kBypass: case kMsMode: case kSpectrumPrePost:
    case kAutoGain: case kShowGraph:
        // Folded to an exact 0/1 so the DSP shell's > 0.5f reads can never
        // disagree with this cache about which side a boundary value took.
        return v >= 0.5f ? 1.0f : 0.0f;
    case kOversampling:
        return std::round(v);
    default:
        return v;
    }
}

// The band and filter frequency parameters: dial positions, which a file
// states in its frequency domain.
constexpr bool fkIsFrequencyParam(uint32_t paramId)
{
    switch (paramId)
    {
    case kHpfFreq: case kLpfFreq:
    case kLfFreq: case kLmFreq: case kHmFreq: case kHfFreq:
        return true;
    default:
        return false;
    }
}

// The frequency dial position `dial` of frequency parameter paramId plays,
// by the measured law, at the mode, gain and shape in state.
inline float fkCalibratedFrequency(uint32_t paramId, float dial, const float* state) noexcept
{
    using duskaudio::FourKEQDSP;
    const bool black = state[kEqType] > 0.5f;
    switch (paramId)
    {
    case kHpfFreq:
        return FourKEQDSP::calibratedFilterFrequency(dial, true, black);
    case kLpfFreq:
        return FourKEQDSP::calibratedFilterFrequency(dial, false, black);
    case kLfFreq:
        return FourKEQDSP::calibratedEqFrequency(
            dial, state[kLfGain], FourKEQDSP::Band::LF, black, state[kLfBell] > 0.5f);
    case kLmFreq:
        return FourKEQDSP::calibratedEqFrequency(
            dial, state[kLmGain], FourKEQDSP::Band::LM, black, true);
    case kHmFreq:
        return FourKEQDSP::calibratedEqFrequency(
            dial, state[kHmGain], FourKEQDSP::Band::HM, black, true);
    case kHfFreq:
        return FourKEQDSP::calibratedEqFrequency(
            dial, state[kHfGain], FourKEQDSP::Band::HF, black, state[kHfBell] > 0.5f);
    default:
        return dial;
    }
}

// Inverse of fkCalibratedFrequency: the dial position that plays `target`,
// clamped to the dial's ends. The same core inverse factory presets use.
inline float fkControlForCalibratedFrequency(uint32_t paramId, float target, const float* state) noexcept
{
    using duskaudio::FourKEQDSP;
    const bool black = state[kEqType] > 0.5f;
    switch (paramId)
    {
    case kHpfFreq:
        return FourKEQDSP::controlForCalibratedFilterFrequency(target, true, black);
    case kLpfFreq:
        return FourKEQDSP::controlForCalibratedFilterFrequency(target, false, black);
    case kLfFreq:
        return FourKEQDSP::controlForCalibratedEqFrequency(
            target, state[kLfGain], FourKEQDSP::Band::LF, black, state[kLfBell] > 0.5f);
    case kLmFreq:
        return FourKEQDSP::controlForCalibratedEqFrequency(
            target, state[kLmGain], FourKEQDSP::Band::LM, black, true);
    case kHmFreq:
        return FourKEQDSP::controlForCalibratedEqFrequency(
            target, state[kHmGain], FourKEQDSP::Band::HM, black, true);
    case kHfFreq:
        return FourKEQDSP::controlForCalibratedEqFrequency(
            target, state[kHfGain], FourKEQDSP::Band::HF, black, state[kHfBell] > 0.5f);
    default:
        return target;
    }
}

// Strict field parse, shared by the library scan and the loader so a file
// can never mean two things. atof() reports failure as 0.0 and happily
// yields NaN/inf for "nan"/"1e999", all of which would reach the DSP through
// setP(); require the whole field to be one finite float. Range clamping is
// done after the file's frequency domain is known: effective LPF values can
// legitimately exceed the host parameter's 15.201 kHz end stop.
//
// Locale-independent on purpose, in both directions (the writer imbues the
// same classic locale): plugin hosts do call setlocale(), and a comma-decimal
// locale makes strtod() stop at the '.' in "0.5" — every value in every preset
// file would silently read as its default.
inline bool fkParsePresetNumber(const std::string& line, std::size_t valueStart, float& out)
{
    std::istringstream field(line.substr(valueStart));
    field.imbue(std::locale::classic());
    double d = 0.0;
    field >> d;
    if (field.fail() || !std::isfinite(d)
        || std::abs(d) > (double)std::numeric_limits<float>::max())
        return false;
    char trailing = '\0';
    if (field >> trailing)                   // trailing junk: not a number
        return false;
    out = (float)d;
    return true;
}

// Reads version 2 files and the unversioned files before them. The returned
// array is always in the host-parameter domain (dial positions), so preset
// identity and setP() see one representation.
inline bool fkReadUserPreset(std::istream& in, std::string& name, float (&out)[kParamCount])
{
    for (uint32_t i = 0; i < kParamCount; ++i)
        out[i] = kFourKParams[i].def;
    bool present[kParamCount] = {};
    bool effectiveHz = false;
    bool supportedDomain = true;

    std::string line;
    while (std::getline(in, line))
    {
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string field = line.substr(eq + 1);
        if (key == "name") { name = field; continue; }
        if (key == "frequency_domain")
        {
            effectiveHz = field == "effective_hz";
            supportedDomain = effectiveHz || field == "control_hz";
            continue;
        }
        if (key == "format_version")
        {
            float version = 0.0f;
            if (!fkParsePresetNumber(line, eq + 1, version)
                || version != (float)kFourKUserPresetFormatVersion)
                return false;
            continue;
        }
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fkIsPresetParam(i) && key == kFourKParams[i].key)
            {
                float v = 0.0f;
                if (!fkParsePresetNumber(line, eq + 1, v))
                    return false;
                out[i] = v;
                present[i] = true;
                break;
            }
    }
    if (!supportedDomain)
        return false;

    // Normalise mode/gain/shape first: those values select the inverse
    // frequency law used below, regardless of their order in the file.
    for (uint32_t i = 0; i < kParamCount; ++i)
        if (present[i] && !fkIsFrequencyParam(i))
            out[i] = fkNormalizeParamValue(i, out[i]);
    for (uint32_t i = 0; i < kParamCount; ++i)
        if (present[i] && fkIsFrequencyParam(i))
            out[i] = fkNormalizeParamValue(
                i, effectiveHz ? fkControlForCalibratedFrequency(i, out[i], out)
                               : out[i]);
    return true;
}

// values: every parameter in the host-parameter domain.
inline void fkWriteUserPreset(std::ostream& out, const float* values)
{
    out.imbue(std::locale::classic());
    out << "format_version=" << kFourKUserPresetFormatVersion << '\n';
    out << "frequency_domain=effective_hz\n";
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (uint32_t i = 0; i < kParamCount; ++i)
        if (fkIsPresetParam(i))
            out << kFourKParams[i].key << '='
                << (fkIsFrequencyParam(i) ? fkCalibratedFrequency(i, values[i], values) : values[i])
                << '\n';
}

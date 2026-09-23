// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FourKEQUserPresetFile.hpp — the 4K EQ 2 user preset file format
// (~/.config/DuskAudio/FourKEQ2/presets/*.4kpreset), framework-free so the
// tests can read what the editor writes.
//
// key=value lines. format_version 4 stores each band and filter in Hz under
// lf_hz..hf_hz, hpf_hz and lpf_hz, or, for one following its legacy dial
// (FourKEQBandFrequency.hpp), the dial position under lf_freq..hf_freq, or
// hpf_freq / lpf_freq. Version 2 and the unversioned files before it stored
// every band and filter as a dial position: raw (frequency_domain=control_hz,
// or no domain line), or as the gain-dependent frequency the pre-#288 read-out
// showed (effective_hz). Those load onto the legacy dial they meant, so they
// play what they played. A filter's dial position is stored as its design
// frequency when the domain is effective_hz, as every version since 2 writes
// it.

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

#include "FourKEQBandFrequency.hpp"
#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"

static constexpr int kFourKUserPresetFormatVersion = 4;
static constexpr int kFourKLegacyUserPresetFormatVersion = 2;

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
    case kLegacyDialBands:
    case kLegacyDialFilters:
        return std::round(v);
    default:
        return v;
    }
}

// Strict field parse, shared by the library scan and the loader so a file
// can never mean two things. atof() reports failure as 0.0 and happily
// yields NaN/inf for "nan"/"1e999", all of which would reach the DSP through
// setP(); require the whole field to be one finite float. Range clamping is
// done after the file's frequency domain is known: effective LPF values can
// legitimately exceed the legacy host parameter's 15.201 kHz end stop.
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

// The returned array is in the host-parameter domain, kLegacyDialBands
// included, so preset identity and the loader see one representation.
inline bool fkReadUserPreset(std::istream& in, std::string& name, float (&out)[kParamCount])
{
    using duskaudio::FourKEQDSP;
    for (uint32_t i = 0; i < kParamCount; ++i)
        out[i] = kFourKParams[i].def;
    bool present[kParamCount] = {};
    bool effectiveHz = false;
    bool supportedDomain = true;
    int version = 0;

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
            float v = 0.0f;
            if (!fkParsePresetNumber(line, eq + 1, v)
                || (v != (float)kFourKUserPresetFormatVersion
                    && v != (float)kFourKLegacyUserPresetFormatVersion))
                return false;
            version = (int)v;
            continue;
        }
        for (uint32_t i = 0; i < kParamCount; ++i)
            if ((fkIsPresetParam(i) || fkBandOfLegacyDialParam(i) >= 0 || fkFilterOfLegacyDialParam(i) >= 0)
                && key == kFourKParams[i].key)
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
    const bool legacyFile = version < kFourKUserPresetFormatVersion;

    // Mode, gain and shape first: they select the inverse laws below,
    // whatever order the file lists them in.
    for (uint32_t i = 0; i < kParamCount; ++i)
        if (present[i] && fkFilterOfHzParam(i) < 0 && fkFilterOfLegacyDialParam(i) < 0
            && fkBandOfHzParam(i) < 0 && fkBandOfLegacyDialParam(i) < 0)
            out[i] = fkNormalizeParamValue(i, out[i]);

    const bool black = out[kEqType] > 0.5f;
    uint32_t filterBits = 0;
    for (int f = 0; f < 2; ++f)
    {
        const FourKEQFilterIds& ids = kFourKEQFilters[f];
        if (legacyFile)
            present[ids.hz] = false;
        if (present[ids.hz])
            out[ids.hz] = fkNormalizeParamValue(ids.hz, out[ids.hz]);
        else if (present[ids.legacyDial])
        {
            const float v = out[ids.legacyDial];
            out[ids.legacyDial] = fkNormalizeParamValue(ids.legacyDial, effectiveHz
                ? FourKEQDSP::controlForCalibratedFilterFrequency(v, ids.highPass, black)
                : v);
            filterBits |= 1u << f;
        }
    }
    out[kLegacyDialFilters] = (float)filterBits;

    uint32_t bits = 0;
    for (int b = 0; b < 4; ++b)
    {
        const FourKEQBandIds& ids = kFourKEQBands[b];
        if (legacyFile)
            present[ids.hz] = false;
        if (present[ids.hz])
            out[ids.hz] = fkNormalizeParamValue(ids.hz, out[ids.hz]);
        else if (present[ids.legacyDial])
        {
            float dial = out[ids.legacyDial];
            if (legacyFile && effectiveHz)
                dial = FourKEQDSP::controlForCalibratedEqFrequency(
                    dial, out[ids.gain], ids.band, black, fkBandIsBell(out, b));
            out[ids.legacyDial] = fkNormalizeParamValue(ids.legacyDial, dial);
            bits |= 1u << b;
        }
    }
    out[kLegacyDialBands] = (float)bits;
    return true;
}

// values must carry the plugin's kLegacyDialBands and kLegacyDialFilters.
inline void fkWriteUserPreset(std::ostream& out, const float* values)
{
    using duskaudio::FourKEQDSP;
    out.imbue(std::locale::classic());
    out << "format_version=" << kFourKUserPresetFormatVersion << '\n';
    out << "frequency_domain=effective_hz\n";
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    const bool black = values[kEqType] > 0.5f;
    for (uint32_t i = 0; i < kParamCount; ++i)
    {
        if (!fkIsPresetParam(i))
            continue;
        if (const int b = fkBandOfHzParam(i); b >= 0 && fkBandFollowsLegacyDial(values, b))
        {
            const uint32_t dial = kFourKEQBands[b].legacyDial;
            out << kFourKParams[dial].key << '=' << values[dial] << '\n';
            continue;
        }
        if (const int f = fkFilterOfHzParam(i); f >= 0 && fkFilterFollowsLegacyDial(values, f))
        {
            const FourKEQFilterIds& ids = kFourKEQFilters[f];
            out << kFourKParams[ids.legacyDial].key << '='
                << FourKEQDSP::calibratedFilterFrequency(values[ids.legacyDial], ids.highPass, black) << '\n';
            continue;
        }
        out << kFourKParams[i].key << '=' << values[i] << '\n';
    }
}

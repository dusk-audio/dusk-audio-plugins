// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// SpectrumAnalyzerParams.hpp - the ten saved Spectrum Analyzer controls as one
// framework-free descriptor table (GH #184; docs/DAF_MIGRATION.md, "Frozen
// product contract"). Native parameter metadata, UI controls and state keys
// read this table; nothing else restates an ID, range, step, choice list or
// default.
//
// Every row is the shipping JUCE adapter's declaration
// (Source/PluginProcessor.cpp, createParameterLayout), in its order, and the
// plan's frozen table. tests/test_params_contract.cpp parses both and fails on
// any difference, so neither side can drift silently. All ranges are linear.
//
// peakHold defaults to Off, as the adapter declares it. The standalone
// FFTProcessor starts with peak hold enabled, so a shell must apply this
// default instead of relying on the analyzer's own initial state
// (tests/test_core_capture.cpp pins both facts).
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace duskaudio::spectrum
{
enum ParameterId : unsigned
{
    PARAM_CHANNEL_MODE,
    PARAM_FFT_RESOLUTION,
    PARAM_SMOOTHING,
    PARAM_SLOPE,
    PARAM_DECAY_RATE,
    PARAM_PEAK_HOLD,
    PARAM_PEAK_HOLD_TIME,
    PARAM_DISPLAY_MIN,
    PARAM_DISPLAY_MAX,
    PARAM_K_SYSTEM_TYPE,
    PARAM_COUNT
};

enum class ParameterKind
{
    Choice,     // index into labels (JUCE AudioParameterChoice)
    Toggle,     // 0 = Off, 1 = On (JUCE AudioParameterBool)
    Continuous  // linear range with a step (JUCE AudioParameterFloat)
};

struct ParameterSpec
{
    const char* id;             // state key and native symbol: the JUCE ParameterID (version hint 1)
    const char* name;           // host-visible name
    ParameterKind kind;
    float minimum;
    float maximum;
    float step;
    float defaultValue;
    const char* unit;           // empty for unitless values and indices
    const char* const* labels;  // Choice/Toggle labels in index order; nullptr for Continuous
    unsigned numLabels;
};

inline constexpr const char* channelModeLabels[]   = { "Stereo", "Mono", "Mid", "Side" };
inline constexpr const char* fftResolutionLabels[] = { "2048", "4096", "8192" };
inline constexpr const char* toggleLabels[]        = { "Off", "On" };
inline constexpr const char* kSystemTypeLabels[]   = { "K-12", "K-14", "K-20" };

namespace detail
{
// A choice's range is derived from its labels, so the two cannot disagree.
template <std::size_t N>
constexpr ParameterSpec choice(const char* id, const char* name,
                               const char* const (&labels)[N], unsigned defaultIndex) noexcept
{
    return { id, name, ParameterKind::Choice, 0.0f, static_cast<float>(N - 1), 1.0f,
             static_cast<float>(defaultIndex), "", labels, static_cast<unsigned>(N) };
}

constexpr ParameterSpec toggle(const char* id, const char* name, bool defaultOn) noexcept
{
    return { id, name, ParameterKind::Toggle, 0.0f, 1.0f, 1.0f, defaultOn ? 1.0f : 0.0f,
             "", toggleLabels, 2 };
}

constexpr ParameterSpec continuous(const char* id, const char* name, float minimum, float maximum,
                                   float step, float defaultValue, const char* unit) noexcept
{
    return { id, name, ParameterKind::Continuous, minimum, maximum, step, defaultValue,
             unit, nullptr, 0 };
}
} // namespace detail

inline constexpr std::array<ParameterSpec, PARAM_COUNT> parameterSpecs {{
    detail::choice     ("channelMode",   "Channel Mode",   channelModeLabels, 0),
    detail::choice     ("fftResolution", "FFT Resolution", fftResolutionLabels, 1),
    detail::continuous ("smoothing",     "Smoothing",         0.0f,   1.0f, 0.01f,   0.5f, ""),
    detail::continuous ("slope",         "Slope",            -4.5f,   4.5f, 0.5f,    0.0f, "dB/octave"),
    detail::continuous ("decayRate",     "Decay Rate",        3.0f,  60.0f, 1.0f,   20.0f, "dB/second"),
    detail::toggle     ("peakHold",      "Peak Hold",      false),
    detail::continuous ("peakHoldTime",  "Peak Hold Time",    0.5f,  10.0f, 0.1f,    2.0f, "seconds"),
    detail::continuous ("displayMin",    "Display Min",    -100.0f, -30.0f, 1.0f,  -60.0f, "dB"),
    detail::continuous ("displayMax",    "Display Max",       0.0f,  12.0f, 1.0f,    6.0f, "dB"),
    detail::choice     ("kSystemType",   "K-System Type",  kSystemTypeLabels, 1),
}};

// Index of the control saved under `id`, or PARAM_COUNT when there is none.
constexpr unsigned findParameter(std::string_view id) noexcept
{
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
        if (id == parameterSpecs[i].id)
            return i;
    return PARAM_COUNT;
}

namespace detail
{
constexpr bool rowIsConsistent(const ParameterSpec& p) noexcept
{
    if (!(p.minimum < p.maximum) || !(p.step > 0.0f))
        return false;
    if (p.defaultValue < p.minimum || p.defaultValue > p.maximum)
        return false;
    if (p.kind == ParameterKind::Continuous)
        return p.labels == nullptr && p.numLabels == 0;
    // Discrete rows: indices 0..numLabels-1 in steps of one, integral default.
    return p.labels != nullptr && p.numLabels >= 2 && p.minimum == 0.0f
        && p.maximum == static_cast<float>(p.numLabels - 1) && p.step == 1.0f
        && p.defaultValue == static_cast<float>(static_cast<unsigned>(p.defaultValue))
        && p.unit[0] == '\0';
}

constexpr bool tableIsConsistent() noexcept
{
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
    {
        if (!rowIsConsistent(parameterSpecs[i]) || findParameter(parameterSpecs[i].id) != i)
            return false;   // the second test also rejects a duplicated ID
    }
    return true;
}
} // namespace detail

static_assert(detail::tableIsConsistent(),
              "parameterSpecs: every row needs min < max, step > 0, an in-range default, "
              "discrete rows indexed 0..labels-1 in unit steps, and a unique ID");
} // namespace duskaudio::spectrum

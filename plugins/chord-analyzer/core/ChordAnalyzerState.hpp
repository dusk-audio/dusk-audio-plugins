// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "ChordAnalyzerParams.hpp"
#include <atomic>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace duskaudio::chord
{
using InputValues = std::array<float, inputParameterCount>;

inline bool validInputValues(const InputValues& values) noexcept
{
    for (unsigned i = 0; i < inputParameterCount; ++i)
        if (!std::isfinite(values[i]) || values[i] < 0 || values[i] > parameterSpecs[i].maximum
            || std::trunc(values[i]) != values[i]) return false;
    return true;
}

// Saved data is ASCII integers, independent of the process locale. The complete
// five-key schema is validated in temporaries; neither missing nor extra keys
// can silently change the meaning of an accepted preset.
inline std::string encodeInputState(const InputValues& values)
{
    if (!validInputValues(values)) return {};
    std::string text = "ChordAnalyzer2:1\n";
    for (unsigned i = 0; i < inputParameterCount; ++i)
        text += std::string(parameterSpecs[i].symbol) + "=" + std::to_string(static_cast<int>(values[i])) + "\n";
    return text;
}

inline bool decodeInputState(std::string_view text, InputValues& out) noexcept
{
    constexpr std::string_view header = "ChordAnalyzer2:1\n";
    if (text.substr(0, header.size()) != header) return false;
    text.remove_prefix(header.size());
    InputValues values{};
    std::array<bool, inputParameterCount> seen{};
    while (!text.empty())
    {
        const auto end = text.find('\n');
        if (end == std::string_view::npos) return false;
        const auto line = text.substr(0, end);
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) return false;
        unsigned index = 0;
        while (index < inputParameterCount && line.substr(0, equals) != parameterSpecs[index].symbol) ++index;
        if (index == inputParameterCount || seen[index]) return false;
        const auto token = line.substr(equals + 1);
        int value = 0;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (parsed.ec != std::errc() || parsed.ptr != token.data() + token.size()
            || value < 0 || value > parameterSpecs[index].maximum) return false;
        values[index] = static_cast<float>(value);
        seen[index] = true;
        text.remove_prefix(end + 1);
    }
    for (bool present : seen) if (!present) return false;
    out = values;
    return true;
}

// All inputs are small integral domains: one lock-free word publishes a whole
// recall. Individual automation edits use CAS so an edit cannot overwrite a
// simultaneous change to a different input. Bit widths derive from the table.
class InputState
{
    static constexpr unsigned width(unsigned index) noexcept
    {
        unsigned bits = 0;
        for (unsigned value = static_cast<unsigned>(parameterSpecs[index].maximum); value; value >>= 1) ++bits;
        return bits;
    }
    static constexpr unsigned offset(unsigned index) noexcept
    {
        unsigned shift = 0;
        for (unsigned i = 0; i < index; ++i) shift += width(i);
        return shift;
    }
    static std::uint32_t pack(const InputValues& values) noexcept
    {
        std::uint32_t bits = 0;
        for (unsigned i = 0; i < inputParameterCount; ++i)
            bits |= static_cast<std::uint32_t>(values[i]) << offset(i);
        return bits;
    }
    static InputValues unpack(std::uint32_t bits) noexcept
    {
        InputValues values{};
        for (unsigned i = 0; i < inputParameterCount; ++i)
            values[i] = static_cast<float>((bits >> offset(i)) & ((1u << width(i)) - 1));
        return values;
    }
    std::atomic<std::uint32_t> bits{0};

public:
    static constexpr unsigned requiredBits() noexcept { return offset(inputParameterCount); }
    InputState() noexcept
    {
        InputValues initial{};
        for (unsigned i = 0; i < inputParameterCount; ++i) initial[i] = parameterSpecs[i].initial;
        bits.store(pack(initial), std::memory_order_relaxed);
    }
    InputValues values() const noexcept { return unpack(bits.load(std::memory_order_relaxed)); }
    bool restore(const InputValues& values) noexcept
    {
        if (!validInputValues(values)) return false;
        bits.store(pack(values), std::memory_order_relaxed);
        return true;
    }
    bool set(unsigned index, float value) noexcept
    {
        if (index >= inputParameterCount || !std::isfinite(value)) return false;
        const auto mask = ((1u << width(index)) - 1) << offset(index);
        const auto update = static_cast<std::uint32_t>(snapParameter(index, value)) << offset(index);
        auto old = bits.load(std::memory_order_relaxed);
        while (!bits.compare_exchange_weak(old, (old & ~mask) | update, std::memory_order_relaxed)) {}
        return true;
    }
};
static_assert(InputState::requiredBits() <= 32, "input domains exceed the atomic snapshot word");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "input recalls must not lock audio");
} // namespace duskaudio::chord

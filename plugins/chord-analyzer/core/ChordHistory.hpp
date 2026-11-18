// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "ChordAnalyzerText.hpp"
#include <array>
#include <cstddef>
#include <vector>

namespace duskaudio::chord
{
inline constexpr int maxChordHistorySize = 32;

// Non-real-time, single-owner history. A fixed ring retains the newest 32
// distinct adjacent identities; UI snapshots may allocate when copied.
template<class Chord>
class BasicChordHistory
{
public:
    bool append(const Chord& chord)
    {
        if (!chord.isValid || chord.name == "" || chord.name == "-") return false;
        const auto last = (head + entries.size() - 1) % entries.size();
        if (count != 0 && entries[last] == chord) return false;
        entries[head] = chord;
        head = (head + 1) % entries.size();
        if (count < entries.size()) ++count;
        return true;
    }
    void clear()
    {
        head = count = 0;
        entries = {};
    }
    std::vector<Chord> snapshot() const
    {
        std::vector<Chord> out;
        out.reserve(count);
        const auto start = (head + entries.size() - count) % entries.size();
        for (std::size_t i = 0; i < count; ++i) out.push_back(entries[(start + i) % entries.size()]);
        return out;
    }

private:
    std::array<Chord, maxChordHistorySize> entries{};
    std::size_t head = 0, count = 0;
};
using ChordHistory = BasicChordHistory<ChordDescription>;
} // namespace duskaudio::chord

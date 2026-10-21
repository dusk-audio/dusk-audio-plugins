// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "../../shared-daf/dsp/DuskSeqLock.hpp"
#include <array>
#include <cstdint>

namespace duskaudio::chord
{
// End-of-block notes with the time of that transition (the block's start).
// The elapsed audio clock is separate: Stop must include the final block too.
struct NoteSnapshot
{
    std::array<std::uint64_t, 2> noteWords{};
    double timeSec = 0.0;
};

// Single audio writer, arbitrary non-real-time readers. A failed bounded load
// preserves the caller's previous frame; note words/time never mix publications.
using NoteSnapshotMailbox = duskaudio::SeqLock<NoteSnapshot>;
} // namespace duskaudio::chord

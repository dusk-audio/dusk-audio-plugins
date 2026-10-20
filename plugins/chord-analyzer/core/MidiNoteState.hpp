// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

#include <array>
#include <cstdint>

namespace duskaudio::chord
{
// Audio-thread-owned MIDI 1.0 note/hold state. All 128 pitches on all 16
// channels fit in fixed storage. UI consumers receive a separately published
// copy; they must not read this mutable object concurrently with process().
// Channel-mode semantics: MIDI 1.0 Detailed Specification and RP-015:
// https://midi.org/midi-1-0-detailed-specification
// https://midi.org/response-to-reset-all-controllers
class MidiNoteState
{
public:
    using Words = std::array<std::uint64_t, 2>;

    void reset() noexcept
    {
        channels = {};
        sounding = {};
        respectSustain = true;
    }

    // Disabling sustain releases deferred keys immediately; re-enabling it
    // cannot resurrect them. Continue tracking the physical pedal either way.
    bool setRespectSustain(bool enabled) noexcept
    {
        if (enabled == respectSustain) return false;
        respectSustain = enabled;
        if (!enabled) for (auto& channel : channels) channel.deferred = {};
        return refresh();
    }

    bool process(const std::uint8_t* data, std::uint32_t size) noexcept
    {
        // Hosts supply complete events, not a running-status byte stream. All
        // relevant channel messages are exactly three bytes with 7-bit data.
        if (data == nullptr || size != 3 || data[1] >= 128 || data[2] >= 128) return false;
        const auto kind = data[0] & 0xf0;
        if (kind != 0x80 && kind != 0x90 && kind != 0xb0) return false;
        auto& channel = channels[data[0] & 0x0f];
        if (kind == 0x80 || kind == 0x90)
        {
            const auto word = data[1] / 64;
            const auto bit = std::uint64_t{1} << (data[1] % 64);
            if (kind == 0x90 && data[2] != 0)
            {
                channel.down[word] |= bit;
                channel.deferred[word] &= ~bit;
            }
            else
            {
                const bool wasDown = (channel.down[word] & bit) != 0;
                channel.down[word] &= ~bit;
                if (respectSustain && channel.pedal)
                {
                    if (wasDown) channel.deferred[word] |= bit;
                }
                else channel.deferred[word] &= ~bit;
            }
        }
        else if (data[1] == 64)
        {
            channel.pedal = data[2] >= 64;
            if (!channel.pedal) channel.deferred = {};
        }
        else if (data[1] == 120)
        {
            channel.down = channel.deferred = {}; // All Sound Off bypasses hold
        }
        else if (data[1] == 121)
        {
            channel.pedal = false; // controller reset keeps physically held keys
            channel.deferred = {};
        }
        else if (data[1] >= 123)
        {
            // 123 and the note-off side effect of 124..127. This analyzer does
            // not synthesize voices or adopt a receiver's omni/mono/poly mode.
            for (int word = 0; word < 2; ++word)
            {
                if (respectSustain && channel.pedal) channel.deferred[word] |= channel.down[word];
                else channel.deferred[word] = 0;
                channel.down[word] = 0;
            }
        }
        else return false;
        return refresh();
    }

    Words snapshot() const noexcept { return sounding; }

    int copyNotes(int* out, int capacity) const noexcept
    {
        if (out == nullptr || capacity <= 0) return 0;
        int written = 0;
        for (int note = 0; note < 128 && written < capacity; ++note)
            if ((sounding[note / 64] & (std::uint64_t{1} << (note % 64))) != 0) out[written++] = note;
        return written;
    }

private:
    struct Channel { Words down{}, deferred{}; bool pedal = false; };
    std::array<Channel, 16> channels{};
    Words sounding{};
    bool respectSustain = true;

    bool refresh() noexcept
    {
        Words next{};
        for (const auto& channel : channels)
            for (int word = 0; word < 2; ++word) next[word] |= channel.down[word] | channel.deferred[word];
        const bool changed = next != sounding;
        sounding = next;
        return changed;
    }
};
} // namespace duskaudio::chord

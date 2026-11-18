// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "AllocationProbe.hpp"
#include "../core/MidiNoteState.hpp"
#include <algorithm>
#include <cstdio>

namespace
{
using State = duskaudio::chord::MidiNoteState;
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok && ++failures <= 12) std::fprintf(stderr, "FAIL: %s\n", message);
}

// Independent byte-per-key oracle: Off / physically Down / pedal Held. It
// never uses the implementation's word masks or cached aggregate.
struct Reference
{
    enum Key : uint8_t { Off, Down, Held };
    std::array<std::array<Key,128>,16> keys{};
    std::array<bool,16> pedals{};
    bool respect = true;
    void reset() { keys = {}; pedals = {}; respect = true; }
    void releaseHeld(int channel)
    {
        for (auto& key : keys[channel]) if (key == Held) key = Off;
    }
    void setRespect(bool value)
    {
        respect = value;
        if (!value) for (int channel = 0; channel < 16; ++channel) releaseHeld(channel);
    }
    void process(const uint8_t* data, uint32_t size)
    {
        if (!data || size != 3 || data[1] > 127 || data[2] > 127) return;
        const int channel = data[0] % 16, kind = data[0] / 16;
        if (kind == 8 || kind == 9)
        {
            auto& key = keys[channel][data[1]];
            if (kind == 9 && data[2] > 0) key = Down;
            else if (key != Off) key = respect && pedals[channel] ? Held : Off;
        }
        else if (kind == 11)
        {
            switch (data[1])
            {
                case 64:
                    pedals[channel] = data[2] >= 64;
                    if (!pedals[channel]) releaseHeld(channel);
                    break;
                case 120: keys[channel].fill(Off); break;
                case 121: pedals[channel] = false; releaseHeld(channel); break;
                case 123: case 124: case 125: case 126: case 127:
                    for (auto& key : keys[channel])
                        if (key != Off) key = respect && pedals[channel] ? Held : Off;
                    break;
                default: break;
            }
        }
    }
    std::array<bool,128> sounding() const
    {
        std::array<bool,128> result{};
        for (int note = 0; note < 128; ++note)
            for (const auto& channel : keys) result[note] = result[note] || channel[note] != Off;
        return result;
    }
};

struct Trial
{
    State actual;
    Reference reference;
    std::array<bool,128> previous{};
    int comparisons = 0;
    void compare(int reported)
    {
        const auto expected = reference.sounding();
        const auto words = actual.snapshot();
        bool match = true;
        for (int note = 0; note < 128; ++note)
            match &= ((words[note / 64] >> (note % 64)) & 1u) == unsigned(expected[note]);
        check(match, "both aggregate words match the independent per-channel oracle");
        if (reported >= 0) check(bool(reported) == (expected != previous), "change notification describes aggregate pitches, not channel bookkeeping");
        previous = expected;

        constexpr int capacities[] = {0,1,4,127,128};
        const int capacity = capacities[comparisons++ % 5];
        std::array<int,130> output; output.fill(-901);
        const int count = actual.copyNotes(output.data() + 1, capacity);
        int wanted = 0;
        for (int note = 0; note < 128 && wanted < capacity; ++note)
            if (expected[note]) match &= output[1 + wanted++] == note;
        check(match && count == wanted, "bounded note copy contains exactly the expected ascending prefix");
        bool guards = true;
        for (int i = 0; i < int(output.size()); ++i)
            if (i == 0 || i > count) guards &= output[i] == -901;
        check(guards, "bounded note copy leaves every guard and unused slot untouched");
    }
    void event(uint8_t status, uint8_t first, uint8_t second)
    {
        const uint8_t data[]{status,first,second};
        const bool changed = actual.process(data, 3); reference.process(data, 3); compare(changed);
    }
    void reset() { actual.reset(); reference.reset(); compare(-1); }
    void respect(bool value)
    {
        const bool changed = actual.setRespectSustain(value); reference.setRespect(value); compare(changed);
    }
};
}

int main()
{
    check(ChordAllocationProbe::positiveControl(), "allocation positive control is observed");
    ChordAllocationProbe::watchAllocations = true;
    Trial t;
    check(t.actual.copyNotes(nullptr, 128) == 0, "null output is rejected");
    int sentinel = -901;
    check(t.actual.copyNotes(&sentinel, -1) == 0 && sentinel == -901, "negative capacity cannot write an output");
    t.compare(-1);
    // Every channel and pitch is pressed, overlapped with all other channels,
    // then released. All four 64-bit boundaries are included, without a cap
    // picked to make the convenient sixteen-note case pass.
    for (int channel = 0; channel < 16; ++channel)
        for (int note = 0; note < 128; ++note) t.event(uint8_t(0x90 + channel), uint8_t(note), 100);
    for (int channel = 0; channel < 16; ++channel)
        for (int note = 0; note < 128; ++note) t.event(uint8_t(0x80 + channel), uint8_t(note), 0);
    t.reset();
    for (int channel = 0; channel < 16; ++channel)
    {
        t.event(uint8_t(0xb0 + channel),64,127);
        for (int note = 0; note < 128; ++note)
        {
            t.event(uint8_t(0x90 + channel),uint8_t(note),100);
            t.event(uint8_t(0x90 + channel),uint8_t(note),0); // velocity-zero note-off
        }
    }
    for (int channel = 0; channel < 16; ++channel) t.event(uint8_t(0xb0 + channel),64,0);
    t.respect(false); t.reset();
    t.event(0xb0,64,127); t.event(0x90,60,100); t.event(0x80,60,0);
    check(t.actual.snapshot()[0] != 0, "reset restores sustain policy as well as clearing history");
    t.reset(); t.event(0x90,60,100); t.event(0x80,60,0);
    check(t.actual.snapshot() == State::Words{}, "reset also clears the physical pedal history");

    t.reset(); t.event(0xb0,64,127); t.event(0x80,60,0);
    check(t.actual.snapshot() == State::Words{}, "an orphan note-off under the pedal cannot create a ghost note");
    t.event(0x90,60,100); t.event(0x80,60,0); t.event(0x90,60,100); t.event(0xb0,64,0);
    check(t.actual.snapshot()[0] == (UINT64_C(1) << 60), "retriggering a held note protects the new key press from pedal-up");
    t.event(0x90,60,0);
    check(t.actual.snapshot() == State::Words{}, "velocity-zero note-on releases the retriggered key");

    t.reset(); t.event(0xb0,64,127); t.event(0x90,60,100); t.event(0x90,64,100); t.event(0x80,60,0);
    t.respect(false);
    check(t.actual.snapshot()[0] == 0 && t.actual.snapshot()[1] == 1, "disabling sustain clears deferred keys but preserves physically pressed keys");
    t.respect(true); t.event(0x80,64,0);
    check(t.actual.snapshot()[0] == 0 && t.actual.snapshot()[1] == 1, "re-enable remembers the pedal without resurrecting previously released keys");
    t.event(0xb0,121,0);
    check(t.actual.snapshot() == State::Words{}, "controller reset releases the remaining deferred key");
    t.reset(); t.event(0xb0,64,127); t.event(0x90,60,100); t.event(0x90,64,100); t.event(0x80,60,0); t.event(0xb0,121,0);
    check(t.actual.snapshot()[0] == 0 && t.actual.snapshot()[1] == 1, "controller reset preserves physically pressed keys");

    for (int controller = 123; controller <= 127; ++controller)
    {
        t.reset(); t.event(0x90,60,100); t.event(0x91,64,100); t.event(0xb0,uint8_t(controller),0);
        check(t.actual.snapshot()[0] == 0 && t.actual.snapshot()[1] == 1, "each all-notes-off channel mode leaves other channels untouched");
        t.reset(); t.event(0xb0,64,127); t.event(0x90,60,100); t.event(0xb0,uint8_t(controller),0);
        check(t.actual.snapshot()[0] != 0, "all-notes-off channel modes defer to the active hold pedal");
        t.event(0xb0,64,0);
        check(t.actual.snapshot() == State::Words{}, "pedal-up releases keys deferred by every all-notes-off channel mode");
    }
    t.reset(); t.event(0xb0,64,127); t.event(0x90,60,100); t.event(0xb0,120,0);
    t.event(0x90,64,100); t.event(0x80,64,0);
    check(t.actual.snapshot()[0] == 0 && t.actual.snapshot()[1] == 1, "All Sound Off clears voices without resetting the physical pedal");

    uint32_t random = 0x1832026u;
    constexpr uint8_t controllers[]{64,120,121,123,124,125,126,127,7,122};
    for (int i = 0; i < 100000; ++i)
    {
        random = random * 1664525u + 1013904223u;
        if (i % 4093 == 0) { t.reset(); continue; }
        if (i % 197 == 0) { t.respect((random & 0x10000u) != 0); continue; }
        const int kind = random % 4;
        uint8_t data[]{uint8_t((kind == 0 ? 0x90 : kind == 1 ? 0x80 : kind == 2 ? 0xb0 : 0xd0) + ((random >> 4) & 15)),
                       uint8_t(kind == 2 ? controllers[(random >> 8) % 10] : (random >> 8) & 127),
                       uint8_t((random >> 16) & 127),0};
        uint32_t size = i % 503 == 0 ? uint32_t(i % 5) : 3u;
        if (i % 751 == 0) data[1] = 128;
        const uint8_t* bytes = i % 997 == 0 ? nullptr : data;
        const bool changed = t.actual.process(bytes, size); t.reference.process(bytes, size); t.compare(changed);
    }
    ChordAllocationProbe::watchAllocations = false;
    check(ChordAllocationProbe::allocationCalls == 0, "construction, maximum polyphony, reset and every event/copy allocate nothing");
    std::printf("MIDI note state: %d comparisons, %d checks, %d failures, %zu allocation calls\n",
                t.comparisons, checks, failures, ChordAllocationProbe::allocationCalls);
    return failures == 0 ? 0 : 1;
}

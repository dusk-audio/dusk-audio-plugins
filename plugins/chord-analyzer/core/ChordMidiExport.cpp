// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "ChordMidiExport.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <string>

namespace duskaudio::chord::midi
{
namespace
{
// Inspect exponent bits: invalid input must still be rejected when a consumer
// compiles its adapter with finite-math assumptions.
bool finite(double value) noexcept
{
    std::uint64_t bits;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
void variableLength(Bytes& bytes, unsigned value)
{
    std::uint8_t buffer[4];
    unsigned count = 0;
    buffer[count++] = static_cast<std::uint8_t>(value & 127u);
    while ((value >>= 7) != 0) buffer[count++] = static_cast<std::uint8_t>((value & 127u) | 128u);
    while (count != 0) bytes.push_back(buffer[--count]);
}
void bigEndian32(Bytes& bytes, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
std::string sanitise(std::string name)
{
    for (char& ch : name) if (std::strchr("/\\:*?\"<>|", ch)) ch = ' ';
    const auto first = name.find_first_not_of(" \t\r\n");
    const auto last = name.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : name.substr(first, last - first + 1);
}
std::filesystem::path temporaryFile(const std::string& prefix, const Bytes& bytes)
{
    if (bytes.empty()) return {};
    std::error_code error;
    auto root = std::filesystem::temp_directory_path(error);
    if (error) return {};
    root /= "DuskAudio_ChordAnalyzer";
    std::filesystem::create_directories(root, error);
    if (error) return {};
    std::random_device random;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        char token[33];
        std::snprintf(token, sizeof(token), "%08x%08x%08x%08x", static_cast<unsigned>(random()),
            static_cast<unsigned>(random()), static_cast<unsigned>(random()), static_cast<unsigned>(random()));
        const auto directory = root / token;
        const auto destination = directory / std::filesystem::u8path(prefix + "_" + token + ".mid");
        if (!std::filesystem::create_directory(directory, error))
        {
            if (error) return {};
            continue;
        }
        if (writeFile(destination, bytes)) return destination;
        std::filesystem::remove(destination, error);
        std::filesystem::remove(directory, error);
        return {};
    }
    return {};
}
}

double detail::resolvedTempo(double bpm) noexcept
{ return finite(bpm) && bpm > 0.0 ? bpm : 120.0; }

bool detail::appendNotes(std::vector<NoteEvent>& out, const std::vector<int>& notes,
                         double startTick, double endTick)
{
    if (!finite(startTick) || !finite(endTick) || startTick < 0 || endTick < startTick
        || endTick > static_cast<double>(std::numeric_limits<std::int32_t>::max())) return false;
    for (int note : notes)
    {
        if (note < 0 || note > 127) return false;
        out.push_back({startTick, static_cast<std::uint8_t>(note), true});
        out.push_back({endTick, static_cast<std::uint8_t>(note), false});
    }
    return true;
}

Bytes detail::encode(std::vector<NoteEvent> notes, double tempoBPM)
{
    // Stable ordering uses unrounded timestamps, matching the shipping writer.
    // Round only when emitting deltas; exact half ticks round to even.
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) {return a.tick < b.tick;});
    Bytes track;
    const auto tempo = static_cast<std::uint32_t>(60'000'000.0 / std::max(1.0, resolvedTempo(tempoBPM)));
    track.insert(track.end(), {0, 0xff, 0x51, 3, static_cast<std::uint8_t>(tempo >> 16),
                              static_cast<std::uint8_t>(tempo >> 8), static_cast<std::uint8_t>(tempo)});
    std::uint32_t lastTick = 0;
    std::uint8_t runningStatus = 0xff;
    const auto emit = [&](const NoteEvent& note) {
        const auto tick = static_cast<std::uint32_t>(std::nearbyint(note.tick));
        const auto delta = tick - lastTick;
        if (delta > 0x0fffffffU) return false; // SMF permits at most four VLQ bytes
        variableLength(track, delta);
        lastTick = tick;
        const std::uint8_t status = note.on ? 0x90 : 0x80;
        if (status != runningStatus) track.push_back(status);
        track.push_back(note.pitch);
        track.push_back(note.on ? 100 : 0);
        runningStatus = status;
        return true;
    };
    std::array<bool, 128> active{};
    for (const auto& note : notes)
    {
        // updateMatchedPairs in the old writer inserts a note-off immediately
        // before an overlapping repeat of the same pitch. Retain original offs
        // too, including their order at identical timestamps.
        if (note.on && active[note.pitch] && !emit({note.tick, note.pitch, false})) return {};
        if (!emit(note)) return {};
        active[note.pitch] = note.on;
    }
    track.insert(track.end(), {0, 0xff, 0x2f, 0});
    if (track.size() > std::numeric_limits<std::uint32_t>::max()) return {};
    Bytes bytes {'M','T','h','d', 0,0,0,6, 0,1, 0,1,
        static_cast<std::uint8_t>(ppq >> 8), static_cast<std::uint8_t>(ppq), 'M','T','r','k'};
    bigEndian32(bytes, static_cast<std::uint32_t>(track.size()));
    bytes.insert(bytes.end(), track.begin(), track.end());
    return bytes;
}

bool writeFile(const std::filesystem::path& destination, const Bytes& bytes) noexcept
{
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) return false;
    try
    {
        std::ofstream stream(destination, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) return false;
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        return !stream.fail();
    }
    catch (...) {return false;}
}

std::filesystem::path temporaryChordFile(const ChordDescription& chord) noexcept
{
    try {return temporaryFile("chord_" + sanitise(chord.name), chordBytes(chord));}
    catch (...) {return {};}
}
std::filesystem::path temporarySessionFile(const RecordingSession& session) noexcept
{
    try {return temporaryFile("chord_progression", sessionBytes(session.events, session.tempoBPM));}
    catch (...) {return {};}
}
} // namespace duskaudio::chord::midi

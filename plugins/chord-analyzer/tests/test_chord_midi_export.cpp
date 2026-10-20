// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Frozen shipping-writer byte transcript, plus independent SMF parsing.
#if defined(CHORD_EXPORT_LEGACY_REFERENCE)
 #if defined(CHORD_EXPORT_REFERENCE_HEADER)
  #include CHORD_EXPORT_REFERENCE_HEADER
 #else
  #include "../Source/ChordMidiExport.h"
 #endif
using ExportChord = ChordInfo;
using ExportEvent = RecordedChordEvent;
#else
 #include "../core/ChordMidiExport.hpp"
using ExportChord = duskaudio::chord::ChordDescription;
using ExportEvent = duskaudio::chord::RecordedChordEvent;
#endif
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Bytes = std::vector<std::uint8_t>;
int checks = 0, failures = 0, clips = 0;
bool record = false;
std::uint64_t hash = 14695981039346656037ULL;
std::ofstream capture;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; if (failures < 12) std::fprintf(stderr, "FAIL: %s\n", message); }
}
ExportChord chord(std::initializer_list<int> notes, bool valid = true)
{
    ExportChord out;
    out.isValid = valid; out.name = "C#m7/G#";
    out.midiNotes.assign(notes);
    return out;
}
Bytes exportChord(const ExportChord& c)
{
   #if defined(CHORD_EXPORT_LEGACY_REFERENCE)
    const auto file = ChordMidiExport::writeChordToTempFile(c);
    juce::MemoryBlock bytes;
    if (file.existsAsFile())
    {
        check(file.loadFileAsData(bytes), "legacy chord export reads from disk");
        check(file.deleteFile(), "test removes its own legacy chord clip");
    }
    const auto* data = static_cast<const std::uint8_t*>(bytes.getData());
    return bytes.getSize() ? Bytes(data, data + bytes.getSize()) : Bytes{};
   #else
    return duskaudio::chord::midi::chordBytes(c);
   #endif
}
Bytes exportSession(const std::vector<ExportEvent>& events, double bpm)
{
   #if defined(CHORD_EXPORT_LEGACY_REFERENCE)
    const auto file = ChordMidiExport::writeSessionToTempFile(events, bpm);
    juce::MemoryBlock bytes;
    if (file.existsAsFile())
    {
        check(file.loadFileAsData(bytes), "legacy session export reads from disk");
        check(file.deleteFile(), "test removes its own legacy session clip");
    }
    const auto* data = static_cast<const std::uint8_t*>(bytes.getData());
    return bytes.getSize() ? Bytes(data, data + bytes.getSize()) : Bytes{};
   #else
    return duskaudio::chord::midi::sessionBytes(events, bpm);
   #endif
}
struct Parsed
{
    bool valid = false;
    unsigned ons = 0, offs = 0, tempo = 0, endTick = 0;
    std::vector<std::array<unsigned, 4>> notes; // tick, status, pitch, velocity
};
Parsed parse(const Bytes& bytes)
{
    Parsed p;
    if (bytes.size() < 26 || std::memcmp(bytes.data(), "MThd\0\0\0\6\0\1\0\1\1\xe0MTrk", 18) != 0) return p;
    const unsigned length = (unsigned(bytes[18]) << 24) | (unsigned(bytes[19]) << 16)
                          | (unsigned(bytes[20]) << 8) | unsigned(bytes[21]);
    if (length != bytes.size() - 22) return p;
    unsigned tick = 0, status = 0, tempos = 0;
    std::size_t at = 22;
    const auto var = [&](unsigned& value) {
        value = 0;
        for (int i = 0; i < 4 && at < bytes.size(); ++i)
        {
            const auto b = bytes[at++]; value = (value << 7) | (b & 127u);
            if (b < 128) return true;
        }
        return false;
    };
    while (at < bytes.size())
    {
        unsigned delta;
        if (!var(delta) || at == bytes.size()) return p;
        tick += delta;
        if (bytes[at] >= 128) status = bytes[at++];
        if (status == 0xff)
        {
            if (at == bytes.size()) return p;
            const auto type = bytes[at++]; unsigned count;
            if (!var(count) || count > bytes.size() - at) return p;
            if (type == 0x51)
            {
                if (count != 3 || tick != 0) return p;
                ++tempos;
                p.tempo = (unsigned(bytes[at]) << 16) | (unsigned(bytes[at + 1]) << 8) | bytes[at + 2];
            }
            if (type == 0x2f)
            {
                p.valid = count == 0 && at == bytes.size() && tempos == 1;
                p.endTick = tick;
                return p;
            }
            at += count; status = 0;
        }
        else
        {
            if ((status != 0x90 && status != 0x80) || at + 2 > bytes.size()) return p;
            const unsigned note = bytes[at++], velocity = bytes[at++];
            if (note >= 128 || (status == 0x90 ? velocity != 100 : velocity != 0)) return p;
            if (status == 0x90) ++p.ons; else ++p.offs;
            p.notes.push_back({tick, status, note, velocity});
        }
    }
    return p;
}
void observe(const Bytes& bytes, unsigned ons)
{
    ++clips;
    const auto count = static_cast<std::uint32_t>(bytes.size());
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto b = static_cast<std::uint8_t>(count >> (8 * i));
        hash = (hash ^ b) * 1099511628211ULL;
        if (capture.is_open()) capture.put(static_cast<char>(b));
    }
    for (auto b : bytes) hash = (hash ^ b) * 1099511628211ULL;
    if (capture.is_open() && !bytes.empty())
        capture.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const auto parsed = parse(bytes);
    check(ons == 0 ? bytes.empty() : parsed.valid && parsed.ons == ons && parsed.offs >= ons,
          "independent parser confirms format 1, one 480-PPQ track, tempo, notes, running status and EOT");
}
std::uint32_t rng = 0x18371c0d;
unsigned random(unsigned limit)
{ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng % limit; }
void transcript()
{
    observe(exportChord(chord({})), 0);
    observe(exportChord(chord({60, 64, 67}, false)), 0);
    for (int root = 0; root < 116; ++root)
    {
        const auto c = chord({root, root + 4, root + 7, root + 11});
        const auto bytes = exportChord(c);
        observe(bytes, 4);
        const auto p = parse(bytes);
        check(p.tempo == 500000 && p.endTick == 1920 && p.notes.size() == 8,
              "single chord retains one bar at 120 BPM and velocity 100");
        if (p.notes.size() == 8)
            for (unsigned i = 0; i < 4; ++i)
                check(p.notes[i][0] == 0 && p.notes[i + 4][0] == 1920
                      && p.notes[i][2] == static_cast<unsigned>(c.midiNotes[i]), "chord preserves note order and endpoint timing");
    }
    auto all = chord({});
    for (int n = 0; n < 128; ++n) all.midiNotes.push_back(n);
    observe(exportChord(all), 128);
    observe(exportChord(chord({60, 60, 64, 64, 67})), 5);
    observe(exportSession({}, 120), 0);

    // Exact half ticks and neighbours distinguish the old writer's ties-to-even
    // rounding from truncation or std::round. Overlap probes include repeated
    // pitches, identical-time on/off ordering, and zero duration.
    for (double tick : {0.0, 0.5, std::nextafter(0.5, 0.0), std::nextafter(0.5, 1.0), 1.5, 2.5, 3.5, 127.5, 128.5, 16383.5})
    {
        ExportEvent e; e.chord = chord({60, 64, 67});
        e.startBeat = tick / 480.0; e.durationBeats = 0.5 / 480.0;
        observe(exportSession({e}, 120), 3);
    }
    for (int trial = 0; trial < 2500; ++trial)
    {
        std::vector<ExportEvent> events;
        unsigned ons = 0;
        const auto count = random(10);
        for (unsigned i = 0; i < count; ++i)
        {
            ExportEvent e;
            e.chord = chord({}, random(9) != 0);
            for (unsigned n = random(9); n > 0; --n) e.chord.midiNotes.push_back(static_cast<int>(random(128)));
            e.startBeat = random(5) ? random(12001) / 960.0 : 0.0;
            e.durationBeats = random(5) ? random(3841) / 960.0 : 0.0;
            e.startTimeSec = random(4001) / 1000.0;
            e.durationSec = random(2001) / 1000.0;
            if (e.chord.isValid) ons += static_cast<unsigned>(e.chord.midiNotes.size());
            events.push_back(e);
        }
        const std::array<double, 12> tempos {{-1, 0, 0.25, 1, 2, 60, 60.5, 91.7, 120, 180, 480, 999}};
        observe(exportSession(events, tempos[static_cast<std::size_t>(trial) % tempos.size()]), ons);
    }
    std::vector<ExportEvent> longSession;
    for (int i = 0; i < 2050; ++i)
    {
        ExportEvent e; e.chord = chord({48 + i % 12, 52 + i % 12, 55 + i % 12});
        e.startBeat = i * 0.256; e.durationBeats = 0.256;
        longSession.push_back(e);
    }
    observe(exportSession(longSession, 180), 6150);
}

#if !defined(CHORD_EXPORT_LEGACY_REFERENCE)
Bytes readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
void filesAndGuards()
{
    namespace midi = duskaudio::chord::midi;
    auto c = chord({60, 64, 67});
    c.name = "C/G# : \\?*\"<>|\xC2\xB0";
    const auto path = midi::temporaryChordFile(c);
    check(!path.empty() && std::filesystem::is_regular_file(path), "temporary chord clip exists after export returns");
    if (path.empty()) return;
    check(readFile(path) == midi::chordBytes(c), "temporary chord file contains the exact shared bytes");
    const auto utf8 = path.filename().u8string();
    const std::string filename(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    check(filename.find_first_of("/\\:*?\"<>|") == std::string::npos,
          "temporary chord filename sanitises cross-platform reserved characters");
    check(filename.find("\xC2\xB0") != std::string::npos, "temporary path retains UTF-8 chord text");
    const auto explicitFile = path.parent_path() / "explicit.mid";
    check(midi::writeFile(explicitFile, midi::chordBytes(chord({60, 64, 67, 71, 74}))), "explicit MIDI destination writes");
    check(midi::writeFile(explicitFile, midi::chordBytes(chord({60})))
          && readFile(explicitFile) == midi::chordBytes(chord({60})), "replacing a longer file leaves no trailing bytes");
    check(!midi::writeFile(path.parent_path(), midi::chordBytes(c)), "directory destination reports failure");
    check(!midi::writeFile(path.parent_path() / "missing" / "clip.mid", midi::chordBytes(c)),
          "missing parent reports failure without creating a partial clip");
    const auto empty = path.parent_path() / "empty.mid";
    check(!midi::writeFile(empty, {}) && !std::filesystem::exists(empty), "empty clip creates no destination file");
    check(midi::temporaryChordFile(chord({60}, false)).empty(), "invalid chord produces no temporary file");
    duskaudio::chord::RecordingSession session;
    session.tempoBPM = 180;
    ExportEvent e; e.chord = c; e.startBeat = 2; e.durationBeats = 3;
    session.events.push_back(e);
    const auto sessionFile = midi::temporarySessionFile(session);
    check(!sessionFile.empty() && readFile(sessionFile) == midi::sessionBytes(session.events, 180),
          "session file uses captured session tempo and event beats");
    check(midi::temporarySessionFile({}).empty(), "empty session produces no temporary file");

    std::array<std::filesystem::path, 64> simultaneous;
    std::array<std::thread, 8> writers;
    for (std::size_t i = 0; i < writers.size(); ++i)
        writers[i] = std::thread([&, i] {
            for (std::size_t n = 0; n < 8; ++n) simultaneous[i * 8 + n] = midi::temporaryChordFile(c);
        });
    for (auto& writer : writers) writer.join();
    const std::set<std::filesystem::path> unique(simultaneous.begin(), simultaneous.end());
    check(unique.size() == simultaneous.size(), "concurrent exports reserve different destinations");
    for (const auto& file : simultaneous)
        check(!file.empty() && readFile(file) == midi::chordBytes(c), "concurrent clip is complete and untruncated");

    for (int bad : {-1, 128, std::numeric_limits<int>::max()})
    {
        auto invalid = c; invalid.midiNotes.push_back(bad);
        check(midi::chordBytes(invalid).empty(), "invalid pitch rejects the complete clip");
    }
    for (double bad : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 1e30})
    {
        auto invalid = e; invalid.startBeat = 0; invalid.startTimeSec = bad;
        check(midi::sessionBytes(std::vector<ExportEvent>{invalid}, 120).empty(), "invalid computed timestamp rejects the complete clip");
    }
    auto tooFar = e;
    tooFar.startBeat = 0x10000000U / 480.0;
    check(midi::sessionBytes(std::vector<ExportEvent>{tooFar}, 120).empty(), "unrepresentable SMF delta rejects instead of emitting a five-byte VLQ");
    for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        check(midi::sessionBytes(session.events, bad) == midi::sessionBytes(session.events, 120),
              "nonfinite tempo falls back to 120 without undefined integer conversion");

    std::filesystem::remove(explicitFile);
    for (const auto& file : simultaneous)
        if (!file.empty()) {std::filesystem::remove(file); std::filesystem::remove(file.parent_path());}
    if (!sessionFile.empty()) {std::filesystem::remove(sessionFile); std::filesystem::remove(sessionFile.parent_path());}
    std::filesystem::remove(path); std::filesystem::remove(path.parent_path());
}
#endif
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--record") == 0) record = true;
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc)
        {
            capture.open(argv[++i], std::ios::binary | std::ios::trunc);
            if (!capture.is_open()) return 2;
        }
        else return 2;
    }
    transcript();
   #if !defined(CHORD_EXPORT_LEGACY_REFERENCE)
    filesAndGuards();
   #endif
    std::printf("MIDI export transcript: %d clips, hash=%016llx\n", clips, static_cast<unsigned long long>(hash));
    if (!record) check(clips == 2632 && hash == 0x128308f14cb51432ULL,
                       "complete byte transcript matches the frozen shipping writer");
    if (capture.is_open()) {capture.flush(); check(capture.good(), "captured transcript writes completely");}
    std::printf("MIDI export: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

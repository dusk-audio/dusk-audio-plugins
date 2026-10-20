// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #183: recording state/timing preservation, independent of the plugin framework.
#include <string>
#include <string_view>
#if defined(CHORD_ANALYZER_LEGACY_REFERENCE)
#include "../Source/ChordRecorder.h"
using Model = ChordAnalyzer;
using Recorder = ChordRecorder;
static std::string bytes(const juce::String& s) { return s.toStdString(); }
static void start(Recorder& r, std::int64_t) { r.startRecording(); }
static void name(Recorder& r, std::string_view s) { r.setSessionName(juce::String::fromUTF8(s.data(), int(s.size()))); }
static std::int64_t stamp(const RecordingSession& s) { return s.startTime.toMilliseconds(); }
#else
#include "../core/ChordRecorder.hpp"
using namespace duskaudio::chord;
using Model = ChordAnalyzerText;
using Recorder = ChordRecorder;
static std::string_view bytes(const std::string& s) { return s; }
static void start(Recorder& r, std::int64_t t) { r.startRecording(t); }
static void name(Recorder& r, std::string_view s) { r.setSessionName(std::string(s)); }
static std::int64_t stamp(const RecordingSession& s) { return s.startTime; }
#endif
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
int failures = 0, observations = 0;
void check(bool ok, const char* message)
{
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
struct Hash
{
    std::uint64_t value = UINT64_C(14695981039346656037);
    void word(std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i) { value ^= (v >> (8 * i)) & 255u; value *= UINT64_C(1099511628211); }
    }
    template<class S> void text(const S& raw)
    {
        const auto s = bytes(raw); word(s.size());
        for (char c : s) { value ^= static_cast<unsigned char>(c); value *= UINT64_C(1099511628211); }
    }
    // Sub-nanosecond compiler differences are irrelevant to MIDI export, but
    // chord boundaries, event ordering and all copied metadata remain exact.
    void time(double t) { word(static_cast<std::uint64_t>(std::llround(t * 1.0e9))); }
    template<class C> void chord(const C& c)
    {
        text(c.name); text(c.romanNumeral); text(c.extensions);
        word(static_cast<unsigned>(c.function)); word(static_cast<unsigned>(c.rootNote));
        word(static_cast<unsigned>(c.bassNote)); word(static_cast<unsigned>(c.quality));
        word(static_cast<unsigned>(c.inversion)); word(c.slashBass); word(c.isValid);
        std::uint32_t confidence; std::memcpy(&confidence, &c.confidence, sizeof(confidence)); word(confidence);
        word(c.midiNotes.size()); for (int n : c.midiNotes) word(static_cast<unsigned>(n));
    }
    void observe(const Recorder& r)
    {
        ++observations;
        const auto& s = r.getSession();
        word(r.isRecording()); word(static_cast<unsigned>(r.getEventCount())); time(r.getRecordingDuration());
        text(s.name); word(stamp(s) != 0); time(s.tempoBPM); word(static_cast<unsigned>(s.keyRoot)); word(s.isMinor);
        word(s.events.size());
        for (const auto& e : s.events)
        {
            chord(e.chord); time(e.startTimeSec); time(e.durationSec); time(e.startBeat); time(e.durationBeats);
        }
    }
};
}

int main(int argc, char** argv)
{
    const bool record = argc == 2 && std::strcmp(argv[1], "--record") == 0;
    if (argc > 1 && !record) return 2;
    Model model;
    const auto major = model.analyze({60,64,67});
    const auto bass = model.analyze({64,67,72});
    const auto minor = model.analyze({57,60,64});
    const auto silence = model.analyze({});
    const auto dyad = model.analyze({60,64});
    Hash focused, longSession, randomized;
    Recorder r;
    focused.observe(r);
    r.recordChord(major, 900.0); r.stopRecording(901.0); focused.observe(r);
    check(r.getEventCount() == 0 && !r.isRecording(), "record/stop while inactive cannot create an event");
    start(r, 1234567890000); r.setTempo(180); r.setKey(7, true); name(r, "Caf\xc3\xa9 progression");
    focused.observe(r);
#if !defined(CHORD_ANALYZER_LEGACY_REFERENCE)
    check(stamp(r.getSession()) == 1234567890000, "recording metadata uses the caller's Unix-millisecond clock");
#endif
    r.recordChord(major, 900.0); focused.observe(r);
    r.recordChord(major, 901.0); focused.observe(r);
    check(r.getEventCount() == 0, "repeated identity does not restart or finish the current chord");
    r.recordChord(bass, 901.25); focused.observe(r);
    r.recordChord(silence, 901.5); focused.observe(r);
    r.recordChord(minor, 902.0); focused.observe(r);
    r.stopRecording(902.75); focused.observe(r);
    const auto& events = r.getEvents();
    check(events.size() == 3, "bass changes and silence close the correct events");
    if (events.size() == 3)
    {
        check(events[0].startTimeSec == 0 && events[0].durationSec == 1.25,
              "first recorded chord anchors relative time after a long-running plugin clock");
        check(events[1].startTimeSec == 1.25 && events[1].durationSec == 0.25 && events[1].chord.bassNote == 4,
              "changed bass is a separate recorded identity");
        check(events[2].startTimeSec == 2.0 && events[2].durationSec == 0.75
                  && events[2].startBeat == 6.0 && events[2].durationBeats == 2.25,
              "stop uses the real stop time and recording tempo, preserving intervening silence");
    }
    r.recordChord(major, 905); start(r, 1234567891000); focused.observe(r);
    check(r.getEventCount() == 0 && r.getSession().tempoBPM == 120 && r.getSession().keyRoot == 0,
          "a new recording resets session metadata and completed events");
    for (double duration : {0.049, 0.05, 0.051})
    {
        r.clearSession(); r.recordChord(major, 0); r.stopRecording(duration); focused.observe(r);
        check(r.getEventCount() == (duration < 0.05 ? 0 : 1), "the fifty-millisecond boundary is retained");
        start(r, 1234567892000);
    }
    r.setTempo(137); r.recordChord(major, 500); r.endCurrentChord(0.1); focused.observe(r);
    r.recordChord(minor, 501); r.stopRecording(501.25); focused.observe(r);
    start(r, 1234567893000); r.setTempo(0); r.recordChord(major, 7); r.recordChord(minor, 6); r.stopRecording(7.25);
    focused.observe(r);
    r.clearSession(); focused.observe(r);
    check(!r.isRecording() && r.getEventCount() == 0, "clear preserves stopped status and removes chord history");

    start(r, 1234567894000); r.setTempo(60);
    const std::array chords{major,bass,minor,silence,dyad};
    for (int i = 0; i < 2050; ++i)
    {
        r.recordChord(chords[static_cast<std::size_t>(i % 3)], 2000.0 + 0.125 * i);
        if (i % 128 == 0) longSession.observe(r);
    }
    r.stopRecording(2000.0 + 0.125 * 2050); longSession.observe(r);
    check(r.getEventCount() == 2050 && r.getRecordingDuration() == 256.25,
          "recordings grow past the 1024-event initial reserve without truncation");

    std::uint32_t random = 0x1830c0de;
    double time = 9000;
    for (int i = 0; i < 12000; ++i)
    {
        random = random * 1664525u + 1013904223u;
        time += double((random >> 8) % 2048) / 48000.0;
        if (i % 401 == 0) time = 0; // prepare/restart can rewind the plugin clock
        switch (random % 10)
        {
            case 0: start(r, 1234567895000 + i); break;
            case 1: r.stopRecording(time); break;
            case 2: r.clearSession(); break;
            case 3: r.setTempo(double((random >> 8) % 300) - 1.0); break;
            case 4: r.setKey(int((random >> 8) % 12), (random & 0x10000) != 0); break;
            case 5: name(r, (random & 0x10000) ? "" : "take #2"); break;
            case 6: r.endCurrentChord(r.getRecordingDuration() + 0.1); break;
            default: r.recordChord(chords[(random >> 8) % chords.size()], time); break;
        }
        randomized.observe(r);
    }
    r.stopRecording(time + 1.0); randomized.observe(r);
    std::printf("Chord recording: %d observations; focused=%016llx long=%016llx randomized=%016llx\n",
                observations, static_cast<unsigned long long>(focused.value),
                static_cast<unsigned long long>(longSession.value), static_cast<unsigned long long>(randomized.value));
    // Frozen independently from 18d8e1d2 with normal and shipping fast-math flags.
    if (!record)
    {
        check(focused.value == UINT64_C(0x86df5b399fbf96ab), "focused state, metadata and timing match frozen legacy behavior");
        check(longSession.value == UINT64_C(0x73b588ac8a6e2505), "long recordings match frozen legacy contents");
        check(randomized.value == UINT64_C(0xfcaea66804132226), "all randomized lifecycle transitions match frozen legacy behavior");
    }
    std::printf("Chord recording: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}

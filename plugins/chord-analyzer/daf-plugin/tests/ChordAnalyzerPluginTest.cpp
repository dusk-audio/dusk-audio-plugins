// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "../ChordAnalyzerPlugin.cpp"
#include "src/DafPluginInternal.hpp"
#include "../../tests/AllocationProbe.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>

namespace duskaudio::chord
{
struct ChordAnalyzerRuntimeTestAccess
{
    static std::unique_lock<std::mutex> holdConsumer(ChordAnalyzerRuntime& runtime)
    { return std::unique_lock<std::mutex>(runtime.modelMutex); }
};
}

namespace
{
using namespace DAF_NAMESPACE;
using namespace duskaudio::chord;
int checks = 0, failures = 0;
void check(bool pass, const char* what)
{
    ++checks;
    if (!pass) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); }
}
bool close(double a, double b) { return std::abs(a - b) < 1.0e-8; }
struct Packet
{
    std::uint32_t frame = 0, size = 0;
    std::array<std::uint8_t, 2048> data{};
};
struct Sink
{
    std::array<Packet, 32> packets{};
    unsigned count = 0, attempts = 0, capacity = 32;
    static bool write(void* context, const MidiEvent& event)
    {
        auto& s = *static_cast<Sink*>(context);
        ++s.attempts;
        if (s.count == s.capacity || event.size > 2048) return false;
        auto& out = s.packets[s.count++];
        out.frame = event.frame;
        out.size = event.size;
        const auto* data = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
        std::copy_n(data, event.size, out.data.data());
        return true;
    }
    void clear(unsigned limit = 32) { count = attempts = 0; capacity = limit; }
};
MidiEvent event(std::uint8_t status, std::uint8_t a, std::uint8_t b, unsigned frame = 0)
{
    MidiEvent e{};
    e.frame = frame; e.size = 3;
    e.data[0] = status; e.data[1] = a; e.data[2] = b;
    return e;
}
ChordAnalyzerRuntime& runtime(PluginExporter& host)
{ return static_cast<ChordAnalyzerPlugin*>(host.getInstancePointer())->runtimeForUI(); }

void block(PluginExporter& host, Sink& sink, const MidiEvent* events = nullptr,
           unsigned count = 0, unsigned frames = 4096, bool mono = false)
{
    std::array<float, 4096> left, right, input;
    left.fill(0.25f); right.fill(-0.5f); input.fill(0.75f);
    const float* in[] = {input.data(), mono ? nullptr : input.data()};
    float* out[] = {left.data(), mono ? nullptr : right.data()};
    ChordAllocationProbe::allocationCalls = 0;
    ChordAllocationProbe::watchAllocations = true;
   #if CHORD_ANALYZER_DAF_MIDI_MODE
    host.run(nullptr, nullptr, frames, events, count);
   #else
    host.run(in, out, frames, events, count);
   #endif
    ChordAllocationProbe::watchAllocations = false;
    check(ChordAllocationProbe::allocationCalls == 0, "actual run allocates no C/C++ memory");
   #if !CHORD_ANALYZER_DAF_MIDI_MODE
    check(std::all_of(left.begin(), left.begin() + frames, [](float v) { return v == 0.0f; }),
          "instrument clears every requested left sample");
    if (!mono) check(std::all_of(right.begin(), right.begin() + frames, [](float v) { return v == 0.0f; }),
                     "instrument clears every requested right sample");
    if (frames < left.size()) check(left[frames] == 0.25f, "callback stays within the requested frames");
    if (frames == 0) check(left[0] == 0.25f && right[0] == -0.5f, "empty callback leaves audio untouched");
   #endif
    (void)in; (void)out; (void)sink;
}

void hostText(PluginExporter& host)
{
    struct Exposed : ChordAnalyzerPlugin
    {
        using ChordAnalyzerPlugin::getParameterValueText;
        using ChordAnalyzerPlugin::getParameterValueFromText;
    } direct;
    for (unsigned id = 0; id < PARAM_COUNT; ++id)
        check(host.hasCustomParameterText(id) == (id == PARAM_DETECTED_CHORD), "only the encoded chord output overrides host text");
    char text[ChordAnalyzerText::labelTextCapacity];
    float parsed = -99;
    check(host.getParameterValueText(PARAM_DETECTED_CHORD, 61, text, sizeof(text)) && std::strcmp(text, "C4") == 0,
          "host formats an encoded single note as its label");
    check(host.getParameterValueFromText(PARAM_DETECTED_CHORD, " C7(#11)/E ", parsed)
        && host.getParameterValueText(PARAM_DETECTED_CHORD, parsed, text, sizeof(text)) && std::strcmp(text, "C7(#11)/E") == 0,
          "host parses and formats a complete tension/slash label");
    check(host.getParameterValueText(PARAM_DETECTED_CHORD, 61.25f, text, sizeof(text)) && std::strcmp(text, "C4") == 0
        && host.getParameterValueText(PARAM_DETECTED_CHORD, 61.75f, text, sizeof(text)) && std::strcmp(text, "C#4") == 0
        && host.getParameterValueText(PARAM_DETECTED_CHORD, 60.5f, text, sizeof(text)) && std::strcmp(text, "B3") == 0
        && host.getParameterValueText(PARAM_DETECTED_CHORD, 61.5f, text, sizeof(text)) && std::strcmp(text, "C#4") == 0,
        "continuous-domain probes retain the shipping nearest-even label rounding");
    parsed = -99;
    check(!host.getParameterValueFromText(PARAM_DETECTED_CHORD, "Cgarbage", parsed) && parsed == -99
        && !direct.getParameterValueFromText(PARAM_DETECTED_CHORD, nullptr, parsed) && parsed == -99,
        "failed host parsing preserves the output and handles null text");
    std::array<char, ChordAnalyzerText::labelTextCapacity + 1> longText; longText.fill('C'); longText.back() = '\0';
    check(!host.getParameterValueFromText(PARAM_DETECTED_CHORD, longText.data(), parsed), "host parser rejects overlong input");
    for (float value : {-1.0f, static_cast<float>(ChordAnalyzerText::maxLabelCode + 1),
                        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        check(!host.getParameterValueText(PARAM_DETECTED_CHORD, value, text, sizeof(text)), "invalid host label values reject conversion");
    check(!direct.getParameterValueText(PARAM_DETECTED_CHORD, 61, nullptr, 8)
        && !direct.getParameterValueText(PARAM_DETECTED_CHORD, 61, text, 0), "host text handles missing destinations");
    char small[3] = {'!', '!', '!'};
    check(!host.getParameterValueText(PARAM_DETECTED_CHORD, 61, small, 2) && small[0] == '\0' && small[2] == '!',
          "host text never returns a truncated label or writes past capacity");
    ChordAllocationProbe::allocationCalls = 0;
    ChordAllocationProbe::watchAllocations = true;
    const bool formatted = host.getParameterValueText(PARAM_DETECTED_CHORD, 100000, text, sizeof(text));
    const bool decoded = formatted && host.getParameterValueFromText(PARAM_DETECTED_CHORD, text, parsed);
    ChordAllocationProbe::watchAllocations = false;
    check(decoded && ChordAllocationProbe::allocationCalls == 0, "actual host callbacks allocate no C/C++ memory");
    std::atomic<unsigned> concurrentFailures{0};
    std::array<std::thread, 4> readers;
    for (unsigned worker = 0; worker < readers.size(); ++worker)
        readers[worker] = std::thread([&, worker] {
            char first[ChordAnalyzerText::labelTextCapacity], second[ChordAnalyzerText::labelTextCapacity];
            ChordAllocationProbe::allocationCalls = 0;
            ChordAllocationProbe::watchAllocations = true;
            for (unsigned i = 0; i < 2500; ++i)
            {
                const auto code = static_cast<float>((i * 11773 + worker * 11939) % (ChordAnalyzerText::maxLabelCode + 1));
                float value = -1;
                if (!host.getParameterValueText(PARAM_DETECTED_CHORD, code, first, sizeof(first))
                    || !host.getParameterValueFromText(PARAM_DETECTED_CHORD, first, value)
                    || !host.getParameterValueText(PARAM_DETECTED_CHORD, value, second, sizeof(second))
                    || std::strcmp(first, second) != 0) concurrentFailures.fetch_add(1);
            }
            ChordAllocationProbe::watchAllocations = false;
            if (ChordAllocationProbe::allocationCalls != 0) concurrentFailures.fetch_add(1);
        });
    for (auto& reader : readers) reader.join();
    check(concurrentFailures.load() == 0, "concurrent host text callbacks retain independent scratch buffers without allocation");
}

void metadataAndParameters(PluginExporter& host)
{
    check(host.getParameterCount() == 10, "five retained inputs and five detection outputs");
    const std::array<const char*, 10> ids {{"keyRoot", "keyMode", "suggestionLevel", "showInversions",
        "respectSustain", "detectedRoot", "detectedQuality", "detectedBass", "detectedInversion", "detectedChord"}};
    const std::array<float, 10> maxima {{11, 1, 2, 1, 1, 12, 33, 12, 8, 295340}};
    const std::array<float, 10> defaults {{0, 0, 2, 1, 1, 0, 0, 0, 0, 0}};
    for (unsigned i = 0; i < ids.size(); ++i)
    {
        check(host.getParameterSymbol(i) == ids[i], "parameter symbols and order retained");
        check(host.getParameterRanges(i).max == maxima[i] && host.getParameterRanges(i).def == defaults[i],
              "retained parameter ranges and defaults");
        check(host.isParameterOutput(i) == (i >= 5), "transient outputs are read-only host metadata");
        if (i >= 5)
        {
            host.setParameterValue(i, maxima[i]);
            check(host.getParameterValue(i) == 0.0f, "a host cannot overwrite detection outputs");
        }
    }
    check(!host.isParameterInteger(PARAM_DETECTED_CHORD), "large encoded label avoids indexed AU domain");
    check(host.getParameterEnumValues(PARAM_KEY_ROOT).count == 12
          && host.getParameterEnumValues(PARAM_KEY_ROOT).values[11].label == "B"
          && host.getParameterEnumValues(PARAM_DETECTED_QUALITY).count == 34
          && host.getParameterEnumValues(PARAM_DETECTED_QUALITY).values[33].label == "7#9"
          && host.getParameterEnumValues(PARAM_DETECTED_INVERSION).values[8].label == "Slash",
          "generic host views retain choice labels and appended indices");
    host.setParameterValue(PARAM_KEY_ROOT, 3.8f);
    host.setParameterValue(PARAM_KEY_MODE, 0.8f);
    host.setParameterValue(PARAM_SUGGESTION_LEVEL, -10.0f);
    host.setParameterValue(PARAM_SHOW_INVERSIONS, 0.3f);
    check(host.getParameterValue(PARAM_KEY_ROOT) == 4.0f && host.getParameterValue(PARAM_KEY_MODE) == 1.0f
          && host.getParameterValue(PARAM_SUGGESTION_LEVEL) == 0.0f
          && host.getParameterValue(PARAM_SHOW_INVERSIONS) == 0.0f, "host fractions snap on entry");
    host.setParameterValue(PARAM_KEY_ROOT, std::numeric_limits<float>::quiet_NaN());
    host.setParameterValue(PARAM_KEY_ROOT, std::numeric_limits<float>::infinity());
    check(host.getParameterValue(PARAM_KEY_ROOT) == 4.0f, "nonfinite edits are ignored");
    for (unsigned i = 0; i < 5; ++i) host.setParameterValue(i, defaults[i]);
    runtime(host).flush();
}

void midiAndLifecycle(PluginExporter& host, Sink& sink)
{
    auto& rt = runtime(host);
    std::array<std::uint8_t, 1024> sysex{};
    sysex.fill(1); sysex.front() = 0xf0; sysex.back() = 0xf7;
    std::array<MidiEvent, 6> input {{event(0x90, 60, 100, 3), event(0x90, 64, 100, 7),
        event(0x90, 67, 100, 12), event(0xb0, 1, 30, 15), event(0xe0, 3, 64, 17), {}}};
    input.back().frame = 18; input.back().size = sysex.size(); input.back().dataExt = sysex.data();
    sink.clear();
    block(host, sink, input.data(), input.size(), 0);
    rt.flush();
    check(sink.attempts == 0 && rt.view().notes.empty(), "zero frames process neither MIDI nor note state");
    block(host, sink, input.data(), input.size(), 31);
    rt.flush();
    check(sink.count == input.size(), "all valid short/long messages forwarded");
    for (unsigned i = 0; i < input.size(); ++i)
    {
        const auto* bytes = input[i].size > 4 ? input[i].dataExt : input[i].data;
        check(sink.packets[i].frame == input[i].frame && sink.packets[i].size == input[i].size
              && std::equal(bytes, bytes + input[i].size, sink.packets[i].data.begin()),
              "MIDI bytes, order and frame offsets unchanged");
    }
    auto model = rt.view();
    check(model.notes == std::vector<int>({60, 64, 67}) && model.chord.name == "C"
          && !model.suggestions.empty() && model.history.size() == 1, "complete formatted model shares one note frame");
    check(host.getParameterValue(PARAM_DETECTED_ROOT) == 1 && host.getParameterValue(PARAM_DETECTED_QUALITY) == 1
          && host.getParameterValue(PARAM_DETECTED_BASS) == 1 && host.getParameterValue(PARAM_DETECTED_INVERSION) == 1,
          "all four detected facts reach host outputs");
    check(ChordAnalyzerText::decodeLabel(static_cast<int>(host.getParameterValue(PARAM_DETECTED_CHORD))) == "C",
          "retained encoded chord label reaches host output");

    // A full output buffer must stop forwarding attempts, while the observer
    // still consumes the remaining MIDI and releases held notes correctly.
    const std::array<MidiEvent, 4> off {{event(0xb0, 64, 127), event(0x80, 60, 0),
        event(0x80, 64, 0), event(0x80, 67, 0)}};
    sink.clear(1); block(host, sink, off.data(), off.size(), 64); rt.flush();
    check(sink.count == 1 && sink.attempts == 2 && rt.view().notes.size() == 3,
          "output full stops writes without skipping sustain/note state");
    host.setParameterValue(PARAM_RESPECT_SUSTAIN, 0.0f);
    sink.clear(); block(host, sink, nullptr, 0, 1); rt.flush();
    check(rt.view().notes.empty() && host.getParameterValue(PARAM_DETECTED_CHORD) == 0,
          "sustain policy edit flushes held notes on next block without MIDI");
    host.setParameterValue(PARAM_RESPECT_SUSTAIN, 1.0f);

    auto bad = input;
    bad[0].frame = 64; bad[1].size = 0;
    bad[2].size = 100; bad[2].dataExt = nullptr;
    sink.clear(); block(host, sink, bad.data(), 3, 64); rt.flush();
    check(sink.count == 0 && rt.view().notes.empty(), "invalid payloads/timestamps are safely ignored");

    host.deactivate(); host.activate();
    block(host, sink, input.data(), 3, 64); rt.flush();
    check(rt.view().notes.size() == 3, "fresh activation accepts notes");
    host.deactivate(); host.activate();
    rt.flush();
    check(rt.view().notes.empty() && host.getParameterValue(PARAM_DETECTED_CHORD) == 0,
          "reactivation clears notes, pedals, cached frame and output state");
    host.setSampleRate(96000.0, true); host.setSampleRate(96000.0, true);
    block(host, sink, nullptr, 0, 1); rt.flush();
    check(rt.view().notes.empty(), "repeated preparation stays clear");
    host.setSampleRate(48000.0, true);

   #if !CHORD_ANALYZER_DAF_MIDI_MODE
    host.deactivate(); host.setAudioPortIO(1, 1); host.activate();
    sink.clear(); block(host, sink, input.data(), 3, 17, true); rt.flush();
    check(rt.view().notes.size() == 3, "mono topology retains MIDI analysis");
    host.deactivate(); host.setAudioPortIO(2, 2); host.activate();
   #endif
}

void recordingAndOverflow(PluginExporter& host, Sink& sink)
{
    auto& rt = runtime(host);
    host.deactivate(); host.activate();
    rt.clearHistory(); rt.clearRecording();
    host.setParameterValue(PARAM_KEY_ROOT, 3);
    host.setParameterValue(PARAM_KEY_MODE, 1);
    TimePosition position;
    position.bpmValid = true; position.bbt.beatsPerMinute = 180.0;
    host.setTimePosition(position);
    sink.clear(); block(host, sink); rt.flush();
    rt.startRecording(123456);
    {
        // Deterministically fill all 63 queue cells before any consumer runs.
        auto hold = ChordAnalyzerRuntimeTestAccess::holdConsumer(rt);
        for (int i = 0; i < 64; ++i)
        {
            const int root = 48 + i % 12;
            const std::array<MidiEvent, 4> chord {{event(0xb0, 120, 0), event(0x90, root, 100),
                event(0x90, root + 4, 100), event(0x90, root + 7, 100)}};
            sink.clear(); block(host, sink, chord.data(), chord.size());
        }
    }
    // Stop drains the queued transitions and the timestamped overflow frame.
    rt.stopRecording();
    const auto session = rt.session();
    const double duration = 4096.0 / 48000.0;
    check(session.events.size() == 64, "overflow retains final chord with its start time");
    check(session.startTime == 123456 && session.tempoBPM == 180.0 && session.keyRoot == 3 && session.isMinor,
          "recording captures timestamp, key and host tempo");
    for (unsigned i = 0; i < session.events.size(); ++i)
    {
        const auto& e = session.events[i];
        check(close(e.startTimeSec, i * duration) && close(e.durationSec, duration)
              && close(e.startBeat, i * duration * 3) && close(e.durationBeats, duration * 3),
              "all event clocks and beats remain relative and nonzero");
    }
    auto model = rt.view();
    check(model.history.size() == 32 && !model.recording && model.recordedEvents == 64,
          "newest 32 history and completed recording published together");
    ChordAnalyzerText expected;
    expected.setKey(3, true);
    for (unsigned i = 0; i < model.history.size(); ++i)
    {
        const int root = 48 + (i + 32) % 12;
        const auto chord = expected.analyze({root, root + 4, root + 7});
        check(model.history[i].name == chord.name && model.history[i].midiNotes == chord.midiNotes,
              "history ordering and voicing survive queue overflow");
    }
    rt.clearHistory(); rt.clearRecording();
    check(rt.view().history.empty() && rt.session().events.empty() && rt.view().notes.size() == 3,
          "Clear resets recordings/history without clearing held notes");

    position.bpmValid = position.bbt.valid = false;
    host.setTimePosition(position);
    block(host, sink, nullptr, 0, 64);
    check(rt.view().tempoBPM == 120.0, "missing transport tempo falls back instead of retaining stale BPM");
}

void backgroundWorker(PluginExporter& host, Sink& sink)
{
    host.deactivate(); host.activate();
    host.setParameterValue(PARAM_KEY_ROOT, 0); host.setParameterValue(PARAM_KEY_MODE, 0);
    const std::array<MidiEvent, 3> chord {{event(0x90, 60, 100), event(0x90, 64, 100), event(0x90, 67, 100)}};
    sink.clear(); block(host, sink, chord.data(), chord.size(), 64);
    // No flush(), editor instance or view() call drives analysis here.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (host.getParameterValue(PARAM_DETECTED_ROOT) != 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(host.getParameterValue(PARAM_DETECTED_ROOT) == 1, "analysis continues with editor closed");
}

void activeRecordingReprepare(PluginExporter& host, Sink& sink)
{
    auto& rt = runtime(host);
    host.deactivate(); host.activate();
    rt.clearRecording(); rt.startRecording(789);
    const std::array<MidiEvent, 3> chord {{event(0x90, 60, 100), event(0x90, 64, 100), event(0x90, 67, 100)}};
    sink.clear(); block(host, sink, chord.data(), chord.size()); rt.flush();
    host.setSampleRate(96000, true);
    const auto session = rt.session();
    check(!rt.view().recording && session.events.size() == 1
          && close(session.events[0].durationSec, 4096.0 / 48000.0),
          "lifecycle stop closes the active chord before resetting the audio clock");
    check(rt.view().notes.empty() && host.getParameterValue(PARAM_DETECTED_CHORD) == 0,
          "rate reprepare clears active notes and transient detection");
    host.setSampleRate(48000, true);
}

void concurrentPublication(PluginExporter& host, Sink& sink)
{
    auto& rt = runtime(host);
    host.deactivate(); host.activate();
    rt.clearHistory(); rt.clearRecording(); rt.startRecording(456);
    std::atomic<bool> started{false}, finished{false};
    unsigned reads = 0, mismatches = 0;
    std::thread reader([&] {
        ChordAnalyzerText oracle;
        started.store(true, std::memory_order_release);
        do
        {
            rt.flush();
            const auto snapshot = rt.view();
            const auto expected = oracle.analyze(snapshot.notes);
            if (snapshot.chord.name != expected.name || snapshot.chord.midiNotes != snapshot.notes
                || snapshot.chord.rootNote != expected.rootNote || snapshot.chord.bassNote != expected.bassNote)
                ++mismatches;
            ++reads;
        } while (!finished.load(std::memory_order_acquire));
    });
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = 0; i < 5000; ++i)
    {
        const int root = 48 + i % 12;
        const std::array<MidiEvent, 4> notes {{event(0xb0, 120, 0), event(0x90, root, 100),
            event(0x90, root + 4, 100), event(0x90, root + 7, 100)}};
        sink.clear(); block(host, sink, notes.data(), notes.size());
    }
    finished.store(true, std::memory_order_release);
    reader.join();
    rt.stopRecording();
    check(reads > 0 && mismatches == 0, "concurrent audio/consumer publishes coherent note and formatted chord frames");
    const auto session = rt.session();
    check(!session.events.empty(), "concurrent recording actually produces events");
    double end = 0.0;
    for (const auto& e : session.events)
    {
        check(e.startTimeSec + 1.0e-8 >= end && e.durationSec >= 0.05,
              "overflow fallback never rewinds/replays a consumed recording transition");
        end = e.startTimeSec + e.durationSec;
    }
    check(rt.view().notes == std::vector<int>({55, 59, 62}), "concurrent consumer converges to final audio frame");
    std::printf("Concurrent publication: 5000 audio blocks, %u coherent reads, %zu recorded events\n", reads, session.events.size());
}
}

int main()
{
    d_nextBufferSize = 4096; d_nextSampleRate = 48000.0;
    Sink sink;
    PluginExporter host(&sink, Sink::write, nullptr, nullptr);
    check(ChordAllocationProbe::positiveControl(), "allocation probe observes real allocations");
    metadataAndParameters(host);
    hostText(host);
    host.activate();
    midiAndLifecycle(host, sink);
    recordingAndOverflow(host, sink);
    backgroundWorker(host, sink);
    activeRecordingReprepare(host, sink);
    concurrentPublication(host, sink);
    host.deactivate();
    std::printf("DAF Chord Analyzer %s: %d checks, %d failures\n",
                CHORD_ANALYZER_DAF_MIDI_MODE ? "MIDI" : "instrument", checks, failures);
    return failures == 0 ? 0 : 1;
}

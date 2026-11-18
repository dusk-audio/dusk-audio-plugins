// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Exercise real audio -> analysis queue -> timer -> history/recording -> MIDI export.
#include "AllocationProbe.hpp"
#include "../Source/PluginProcessor.h"
#include "../Source/ChordMidiExport.h"
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0, checks = 0;
bool record = false;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
struct Playhead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm(180.0); return p;
    }
};
class Driver : private juce::Timer
{
public:
    explicit Driver(ChordAnalyzerProcessor& p) : processor(p)
    {
        processor.setPlayHead(&playhead);
        processor.prepareToPlay(48000, frames);
        auto& apvts = processor.getAPVTS();
        auto* key = apvts.getParameter(ChordAnalyzerProcessor::PARAM_KEY_ROOT);
        key->setValueNotifyingHost(key->convertTo0to1(3));
        apvts.getParameter(ChordAnalyzerProcessor::PARAM_KEY_MODE)->setValueNotifyingHost(1);
        model.setKey(3, true);
        send(-1); // sample host tempo before Start; the session reset must not erase it
        processor.startRecording();
        for (int i = 0; i < 64; ++i) send(i % 12);
        startTimer(150);
    }
    ~Driver() override { processor.setPlayHead(nullptr); }
private:
    static constexpr int frames = 4096;
    static constexpr double blockSeconds = double(frames) / 48000.0;
    ChordAnalyzerProcessor& processor;
    Playhead playhead;
    ChordAnalyzer model;
    juce::AudioBuffer<float> audio{2, frames};
    int previous = -1, phase = 0;
    void send(int root)
    {
        juce::MidiBuffer midi;
        if (previous >= 0)
            for (int offset : {0,4,7}) midi.addEvent(juce::MidiMessage::noteOff(1,48+previous+offset), 0);
        if (root >= 0)
            for (int offset : {0,4,7}) midi.addEvent(juce::MidiMessage::noteOn(1,48+root+offset,juce::uint8(100)), 1);
        ChordAllocationProbe::allocationCalls = 0;
        ChordAllocationProbe::watchAllocations = true;
        processor.processBlock(audio, midi);
        ChordAllocationProbe::watchAllocations = false;
        check(ChordAllocationProbe::allocationCalls == 0, "recording/history never allocate on the actual audio callback");
        previous = root;
    }
    bool expected(const ChordInfo& actual, int root)
    {
        const auto wanted = model.analyze({48+root,52+root,55+root});
        return actual.name == wanted.name && actual.romanNumeral == wanted.romanNumeral
            && actual.extensions == wanted.extensions && actual.function == wanted.function
            && actual.rootNote == wanted.rootNote && actual.bassNote == wanted.bassNote
            && actual.quality == wanted.quality && actual.inversion == wanted.inversion
            && actual.slashBass == wanted.slashBass && actual.isValid == wanted.isValid
            && actual.confidence == wanted.confidence && actual.midiNotes == wanted.midiNotes;
    }
    void timerCallback() override
    {
        if (phase++ == 0)
        {
            const auto history = processor.getChordHistory();
            check(history.size() == 32, "the actual processor retains exactly its newest 32 history entries");
            bool correct = history.size() == 32;
            if (correct) for (std::size_t i = 0; i < history.size(); ++i) correct &= expected(history[i], int((i+32)%12));
            check(correct, "the actual history preserves every field in oldest-to-newest order after wrap");
            processor.stopRecording();
            const auto session = processor.getRecordingSession();
            std::printf("Recorded session: %zu events", session.events.size());
            if (!session.events.empty()) std::printf(", last start=%.12f duration=%.12f", session.events.back().startTimeSec, session.events.back().durationSec);
            std::printf("\n");
            check(!processor.isRecording() && processor.getRecordedEventCount() == 64 && session.events.size() == 64,
                  "the analysis queue, overflow fallback and Stop preserve all 64 recorded chords");
            check(session.tempoBPM == 180 && session.keyRoot == 3 && session.isMinor,
                  "Start retains the sampled host tempo and selected key after clearing the old session");
            bool timing = session.events.size() == 64;
            if (timing) for (std::size_t i = 0; i < session.events.size(); ++i)
            {
                const auto& e = session.events[i];
                timing &= expected(e.chord, int(i%12)) && std::abs(e.startTimeSec - i*blockSeconds) < 1e-9
                    && std::abs(e.durationSec-blockSeconds) < 1e-9 && std::abs(e.startBeat-i*blockSeconds*3) < 1e-9
                    && std::abs(e.durationBeats-blockSeconds*3) < 1e-9;
            }
            check(timing, "recorded chords retain sample-clock-relative seconds and beat timing");
            const auto file = ChordMidiExport::writeSessionToTempFile(session.events, session.tempoBPM);
            juce::MidiFile midi;
            bool read = false;
            {
                juce::FileInputStream input(file);
                read = input.openedOk() && midi.readFrom(input);
            }
            check(read && midi.getNumTracks() == 1 && midi.getTimeFormat() == 480,
                  "the actual recorded session exports a readable single-track 480-PPQ MIDI file");
            int on = 0, off = 0, tempo = 0;
            if (read && midi.getNumTracks() == 1)
                for (const auto* event : *midi.getTrack(0))
                {
                    const auto& msg = event->message;
                    if (msg.isNoteOn()) ++on;
                    if (msg.isNoteOff()) ++off;
                    if (msg.isTempoMetaEvent())
                    {
                        ++tempo;
                        check(std::abs(msg.getTempoSecondsPerQuarterNote()-1.0/3.0) < 1e-6,
                              "exported tempo meta-event retains the recording BPM");
                    }
                }
            check(on == 192 && off == 192 && tempo == 1, "export retains every recorded triad note-on/off and one tempo event");
            juce::MemoryBlock bytes;
            const bool loaded = file.loadFileAsData(bytes);
            std::uint64_t hash = UINT64_C(14695981039346656037);
            const auto* data = static_cast<const unsigned char*>(bytes.getData());
            for (std::size_t i = 0; i < bytes.getSize(); ++i) { hash ^= data[i]; hash *= UINT64_C(1099511628211); }
            std::printf("Session MIDI: %zu bytes, hash=%016llx\n", bytes.getSize(), static_cast<unsigned long long>(hash));
            if (!record) check(loaded && hash == UINT64_C(0xcec6e9b5286c45ed), "exported MIDI bytes match the corrected pre-extraction legacy processor");
            check(file.existsAsFile() && file.deleteFile(), "the test removes its exported MIDI file");
            processor.clearChordHistory(); processor.clearRecording();
            check(processor.getChordHistory().empty() && processor.getRecordedEventCount() == 0,
                  "the actual Clear actions remove history and recorded events");
            send(4);
        }
        else
        {
            const auto history = processor.getChordHistory();
            check(history.size() == 1 && expected(history.front(),4), "history starts cleanly after Clear");
            stopTimer(); juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    }
};
}
int main(int argc, char** argv)
{
    record = argc == 2 && std::strcmp(argv[1], "--record") == 0;
    if (argc > 1 && !record) return 2;
    juce::ScopedJuceInitialiser_GUI init;
    check(ChordAllocationProbe::positiveControl(), "allocation probe observes a real allocation");
    ChordAnalyzerProcessor processor;
    Driver driver(processor);
    juce::MessageManager::getInstance()->runDispatchLoop();
    processor.releaseResources();
    std::printf("Chord session integration: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

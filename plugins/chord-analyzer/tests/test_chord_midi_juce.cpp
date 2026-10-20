// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "AllocationProbe.hpp"
#include "../Source/PluginProcessor.h"
#include <array>
#include <cstdio>
#include <vector>

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
struct Fixture
{
    ChordAnalyzerProcessor processor;
    juce::AudioBuffer<float> buffer{processor.isMidiEffect() ? 0 : 2, 512};
    void reset()
    {
        processor.prepareToPlay(48000.0, 512);
        processor.getAPVTS().getParameter(ChordAnalyzerProcessor::PARAM_RESPECT_SUSTAIN)->setValueNotifyingHost(1.0f);
    }
    std::size_t send(juce::MidiBuffer& midi)
    {
        juce::MidiBuffer original(midi);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < 512; ++i) buffer.setSample(ch, i, 0.125f);
        ChordAllocationProbe::allocationCalls = 0;
        ChordAllocationProbe::watchAllocations = true;
        processor.processBlock(buffer, midi);
        ChordAllocationProbe::watchAllocations = false;
        const auto allocations = ChordAllocationProbe::allocationCalls;
        auto expected = original.cbegin();
        bool identical = true;
        for (const auto event : midi)
        {
            if (expected == original.cend()) { identical = false; break; }
            const auto before = *expected++;
            identical &= event.samplePosition == before.samplePosition && event.numBytes == before.numBytes
                && std::memcmp(event.data, before.data, size_t(event.numBytes)) == 0;
        }
        check(identical && expected == original.cend(), "JUCE passes MIDI payloads and sample positions through exactly");
        bool silent = true;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < 512; ++i) silent &= buffer.getSample(ch, i) == 0.0f;
        check(silent, "the instrument keeps silent output while the MIDI effect has no audio buses");
        return allocations;
    }
    void send(std::initializer_list<std::array<uint8_t,3>> messages)
    {
        juce::MidiBuffer midi;
        int time = 0;
        for (const auto& message : messages) midi.addEvent(message.data(), 3, time++);
        send(midi);
    }
    void chord() { send({{0x90,60,100},{0x90,64,100},{0x90,67,100}}); }
    void expect(std::initializer_list<int> notes, const char* message)
    {
        check(processor.getActiveNotes() == std::vector<int>(notes), message);
    }
};
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    check(ChordAllocationProbe::positiveControl(), "C++ allocation positive control is observed");
#if defined(CHORD_TEST_WRAP_MALLOC)
    ChordAllocationProbe::watchAllocations = true;
    void* (*volatile request)(std::size_t) = std::malloc;
    void* p = request(8); std::free(p);
    ChordAllocationProbe::watchAllocations = false;
    check(ChordAllocationProbe::allocationCalls == 1, "direct C allocation positive control is observed");
#endif
    Fixture f; f.reset(); f.chord(); f.expect({60,64,67}, "the real desktop control chord is held");
    const int channels = f.processor.isMidiEffect() ? 0 : 2;
    check(f.processor.getTotalNumInputChannels() == channels && f.processor.getTotalNumOutputChannels() == channels,
          "both shipping audio-bus topologies remain unchanged");
    f.send({{0x91,60,100},{0x80,60,0}});
    f.expect({60,64,67}, "desktop note-off preserves the same pitch on another channel");
    f.send({{0x81,60,0}}); f.expect({64,67}, "desktop last-channel release removes the pitch");
    f.reset(); f.chord();
    f.send({{0xB1,64,127},{0x80,60,0},{0x80,64,0},{0x80,67,0}});
    f.expect({}, "desktop sustain is channel-local");
    f.reset(); f.chord();
    f.send({{0xB0,64,127},{0x80,60,0},{0x80,64,0},{0x80,67,0}});
    f.expect({60,64,67}, "desktop sustain holds this channel's released keys");
    f.send({{0xB1,64,0}}); f.expect({60,64,67}, "another channel's pedal-up cannot clear desktop held notes");
    f.reset(); f.chord();
    f.send({{0xB0,64,127},{0x80,60,0},{0x80,64,0},{0x80,67,0}});
    f.processor.getAPVTS().getParameter(ChordAnalyzerProcessor::PARAM_RESPECT_SUSTAIN)->setValueNotifyingHost(0.0f);
    f.send({}); f.expect({}, "desktop disabling sustain releases deferred keys without a new MIDI event");
    f.reset(); f.chord(); f.send({{0x91,72,100},{0xB0,120,0}});
    f.expect({72}, "desktop All Sound Off leaves other MIDI channels alone");
    f.reset(); f.chord(); f.send({{0xB0,64,127},{0xB0,123,0}});
    f.expect({60,64,67}, "desktop All Notes Off respects the hold pedal");
    f.send({{0xB0,64,0}}); f.expect({}, "desktop pedal-up completes All Notes Off");
    f.reset(); f.chord();
    f.send({{0xB0,64,127},{0x80,60,0},{0x80,64,0},{0x80,67,0},{0xB0,121,0}});
    f.expect({}, "desktop Reset All Controllers releases deferred keys");

    std::array<uint8_t,1024> sysex{}; sysex.front() = 0xF0; sysex.back() = 0xF7;
    juce::MidiBuffer midi; midi.addEvent(sysex.data(), int(sysex.size()), 37);
    const auto allocations = f.send(midi);
#if defined(CHORD_TEST_WRAP_MALLOC)
    check(allocations == 0, "desktop parsing passes long SysEx without making a heap copy");
#endif
    std::printf("Desktop SysEx callback allocations: %zu\n", allocations);
    f.processor.releaseResources(); f.expect({}, "releaseResources clears all note and pedal state");
    std::printf("Chord desktop MIDI: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

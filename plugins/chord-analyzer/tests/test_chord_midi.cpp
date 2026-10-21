// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #183: exercise the real native wrapper, not an imitation of its MIDI loop.
#include "AllocationProbe.hpp"
#include "../lv2/chord-analyzer-lv2.cpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace test
{
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
struct Packet
{
    LV2_Atom_Event event{};
    std::array<uint8_t, 8> data{};
    Packet(uint8_t status, uint8_t first, uint8_t second, int frame = 0)
    {
        event.time.frames = frame;
        event.body = {3, 2};
        data[0] = status; data[1] = first; data[2] = second;
    }
};
struct Buffer { LV2_Atom_Sequence seq{}; std::array<uint64_t, 1024> storage{}; };
class Fixture
{
public:
    Fixture()
    {
        instance = api->instantiate(api, 48000.0, "", features);
        check(instance != nullptr, "native wrapper instantiates with a real URID map");
        controls.fill(-777.0f);
        controls[2] = controls[3] = 0.0f; controls[4] = 2.0f;
        controls[5] = controls[10] = 1.0f;
        api->connect_port(instance, 0, &input.seq);
        api->connect_port(instance, 1, &output.seq);
        for (uint32_t i = 2; i < 19; ++i) api->connect_port(instance, i, &controls[i]);
        reset();
    }
    ~Fixture() { api->deactivate(instance); api->cleanup(instance); }
    void reset()
    {
        api->activate(instance);
        controls[10] = 1.0f;
        begin(); run();
    }
    void begin()
    {
        input.seq.atom = {sizeof(LV2_Atom_Sequence_Body), 1};
        input.seq.body = {};
        output.seq.atom = {uint32_t(sizeof(Buffer) - sizeof(LV2_Atom)), 1};
    }
    void append(const Packet& p)
    {
        check(lv2_atom_sequence_append_event(&input.seq, sizeof(Buffer) - sizeof(LV2_Atom), &p.event) != nullptr,
              "test host has capacity for every input MIDI event");
    }
    std::size_t run(uint32_t frames = 512, bool completeOutput = true)
    {
        ChordAllocationProbe::allocationCalls = 0;
        ChordAllocationProbe::watchAllocations = true;
        api->run(instance, frames);
        ChordAllocationProbe::watchAllocations = false;
        const auto allocations = ChordAllocationProbe::allocationCalls;
        if (frames != 0)
        {
            if (completeOutput)
            {
                bool identical = output.seq.atom.size == input.seq.atom.size
                    && output.seq.atom.type == input.seq.atom.type && output.seq.body.unit == input.seq.body.unit;
                auto* rendered = lv2_atom_sequence_begin(&output.seq.body);
                LV2_ATOM_SEQUENCE_FOREACH(&input.seq, event)
                {
                    if (lv2_atom_sequence_is_end(&output.seq.body, output.seq.atom.size, rendered))
                    { identical = false; break; }
                    identical &= event->body.size == rendered->body.size
                        && std::memcmp(event, rendered, sizeof(LV2_Atom_Event) + event->body.size) == 0;
                    rendered = lv2_atom_sequence_next(rendered);
                }
                identical &= lv2_atom_sequence_is_end(&output.seq.body, output.seq.atom.size, rendered);
                // Atom padding bytes have no defined payload value.
                check(identical, "MIDI bytes and timestamps are passed through exactly");
            }
            for (int i : {6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18})
                check(std::isfinite(controls[i]), "every detection/suggestion output is finite");
        }
        return allocations;
    }
    void send(std::initializer_list<Packet> packets)
    {
        begin(); for (const auto& p : packets) append(p); run();
    }
    void chord() { send({{0x90,60,100,3},{0x90,64,100,17},{0x90,67,100,400}}); }
    void activateOnly() { api->activate(instance); }
    void aliasControl(int inputPort, int outputPort) { api->connect_port(instance, uint32_t(outputPort), &controls[inputPort]); }
    void restoreControl(int outputPort) { api->connect_port(instance, uint32_t(outputPort), &controls[outputPort]); }
    std::size_t runInPlace()
    {
        const Buffer original = input;
        api->connect_port(instance, 1, &input.seq);
        const auto allocations = run(512, false);
        check(input.seq.atom.size == original.seq.atom.size
                  && std::memcmp(&input.seq, &original.seq, sizeof(LV2_Atom) + original.seq.atom.size) == 0,
              "aliased MIDI input/output preserves the complete incoming sequence");
        api->connect_port(instance, 1, &output.seq);
        return allocations;
    }
    void expect(bool major, const char* message)
    {
        check(controls[6] == (major ? 1.0f : 0.0f) && controls[7] == (major ? 1.0f : 0.0f), message);
    }
    std::array<float,19> controls{};
    Buffer input, output;
private:
    static LV2_URID map(void*, const char* uri)
    {
        if (std::strcmp(uri, LV2_MIDI__MidiEvent) == 0) return 2;
        if (std::strcmp(uri, LV2_ATOM__beatTime) == 0) return 3;
        return 1;
    }
    LV2_URID_Map mapping{nullptr, map};
    LV2_Feature feature{LV2_URID__map, &mapping};
    const LV2_Feature* features[2]{&feature, nullptr};
    const LV2_Descriptor* api = lv2_descriptor(0);
    LV2_Handle instance{};
};

void integration()
{
    check(ChordAllocationProbe::positiveControl(), "allocation probe detects an actual allocation request");
    Fixture f;
    f.begin();
    for (int note = 0; note < 128; ++note) f.append({0x90,uint8_t(note),100,note});
    const auto noteAllocations = f.run();
    check(noteAllocations == 0, "all 128 notes fit without a callback allocation");
    f.send({{0xB0,64,127}});
    f.begin();
    for (int note = 0; note < 128; ++note) f.append({0x80,uint8_t(note),0,note});
    const auto sustainAllocations = f.run();
    check(sustainAllocations == 0, "all 128 deferred releases fit without a callback allocation");
    std::printf("Native MIDI allocations: note-on=%zu sustained-off=%zu\n", noteAllocations, sustainAllocations);
    f.send({{0xB0,64,0}}); f.expect(false, "lifting the pedal releases the full held-note set");

    f.reset(); f.chord(); f.expect(true, "the control chord is actually detected");
    f.send({{0x91,60,100},{0x80,60,0}});
    f.expect(true, "a note-off cannot release the same pitch held on another MIDI channel");
    f.send({{0x81,60,0}}); f.expect(false, "the last channel's note-off really releases that pitch");

    f.reset(); f.chord();
    f.send({{0xB1,64,127},{0x80,60,0},{0x80,64,0},{0x80,67,0}});
    f.expect(false, "another channel's sustain pedal cannot hold this chord");
    f.reset(); f.chord();
    f.send({{0xB0,64,127},{0x80,60,0},{0x80,64,0},{0x80,67,0}});
    f.expect(true, "this channel's sustain pedal does hold released keys");
    f.send({{0xB1,64,0}}); f.expect(true, "another channel's pedal-up cannot release this held chord");
    f.send({{0xB0,64,0}}); f.expect(false, "the matching pedal-up releases the held chord");

    f.reset(); f.chord();
    f.send({{0xB0,64,127},{0x80,60,0},{0x80,64,0},{0x80,67,0}});
    f.controls[10] = 0.0f; f.send({});
    f.expect(false, "disabling Respect Sustain flushes already-released keys without waiting for another event");

    f.reset(); f.chord(); f.send({{0xB0,123,0}});
    f.expect(false, "All Notes Off is handled by the headless product");
    f.reset(); f.chord(); f.send({{0xB0,64,127},{0xB0,123,0}});
    f.expect(true, "All Notes Off respects the hold pedal");
    f.send({{0xB0,64,0}}); f.expect(false, "pedal-up releases keys deferred by All Notes Off");
    f.reset(); f.chord(); f.send({{0xB0,64,127},{0xB0,120,0}});
    f.expect(false, "All Sound Off silences the channel immediately despite sustain");

    f.reset(); f.chord();
    f.send({{0xB0,64,127},{0x80,60,0},{0x80,64,0},{0x80,67,0},{0xB0,121,0}});
    f.expect(false, "Reset All Controllers releases pedal-held notes");

    f.reset(); f.begin();
    for (const auto& p : {Packet{0x90,60,100},Packet{0x90,64,100},Packet{0x90,67,100}}) f.append(p);
    const auto before = f.controls;
    const auto capacity = f.output.seq.atom.size;
    f.run(0);
    check(f.controls == before && f.output.seq.atom.size == capacity, "zero-frame callbacks preserve cached controls and leave queued MIDI untouched");
    f.run(); f.expect(true, "a real block still consumes events skipped by the zero-frame callback");

    f.reset(); f.chord(); f.begin();
    f.append({0x80,60,0}); f.append({0x80,64,0}); f.append({0x80,67,0});
    for (int index : {6,7,8,9,11,12,13,14,15,16,17,18}) f.controls[index] = -777;
    const auto untouchedCapacity = f.output.seq.atom.size;
    f.run(0);
    f.expect(true, "zero-frame calls refresh cached instantaneous controls without consuming queued note-offs");
    check(f.output.seq.atom.size == untouchedCapacity, "zero-frame control refresh never accesses the MIDI output buffer");
    f.run(); f.expect(false, "the following real block consumes the note-offs exactly once");

    f.reset(); f.begin(); f.append({0x90,60,100,3}); f.append({0x90,64,100,17}); f.append({0x90,67,100,400});
    check(f.runInPlace() == 0, "in-place MIDI forwarding cannot allocate a copy of the sequence");
    f.expect(true, "in-place MIDI detection still receives every note event");

    const auto held = [](Fixture& target)
    {
        target.begin();
        for (const auto& p : {Packet{0x90,60,100,0},Packet{0x90,64,100,1},Packet{0x90,67,100,2},
                             Packet{0xb0,64,127,3},Packet{0x80,60,0,4},Packet{0x80,64,0,5},Packet{0x80,67,0,6}})
            target.append(p);
        target.run();
    };
    Fixture reference;
    reference.controls[2] = 7; reference.controls[3] = 1; held(reference);
    for (int input : {2,3,4,5,10})
        for (int output : {6,7,8,9,11,12,13,14,15,16,17,18})
        {
            f.activateOnly();
            f.controls[2] = 7; f.controls[3] = 1; f.controls[4] = 2; f.controls[5] = f.controls[10] = 1;
            f.aliasControl(input, output); held(f);
            bool identical = true;
            for (int port : {6,7,8,9,11,12,13,14,15,16,17,18})
                identical &= f.controls[port == output ? input : port] == reference.controls[port];
            check(identical, "all five input controls are latched before any of the twelve aliased outputs are published");
            f.restoreControl(output);
        }
    f.controls[2] = f.controls[3] = 0;

    f.reset(); f.begin(); f.append({0x90,60,100});
    f.output.seq.body = {777,999}; // hosts advertise capacity, not an initialized result body
    f.run();
    check(f.output.seq.body.unit == f.input.seq.body.unit, "output sequence declares the correct frame timestamp unit");
    f.output.seq.body = {};
    f.reset(); f.begin(); f.input.seq.body.unit = 3;
    Packet beatEvent{0x90,60,100}; beatEvent.event.time.beats = 0.001; f.append(beatEvent);
    f.run();
    check(f.output.seq.body.unit == 3, "beat-domain MIDI timestamps retain their declared unit");
    f.output.seq.body = {};

    for (uint32_t frames : {1u,16u,64u,128u,512u,2048u})
    {
        f.reset(); f.begin();
        f.append({0x90,60,100,0}); f.append({0x90,64,100,int(frames / 2)}); f.append({0x90,67,100,int(frames - 1)});
        f.run(frames); f.expect(true, "valid sample positions survive every tested block size");
        f.begin();
        f.append({0x80,60,0,0}); f.append({0x80,64,0,int(frames / 2)}); f.append({0x80,67,0,int(frames - 1)});
        f.run(frames); f.expect(false, "note-off timing survives every tested block size");
    }
    // A host can offer a small output buffer. Keep the fitting event prefix,
    // drop events that cannot fit, and never grow storage or write past it.
    for (uint32_t capacity : {uint32_t(sizeof(LV2_Atom_Sequence_Body)), 27u, 31u,
                              uint32_t(sizeof(LV2_Atom_Sequence_Body) + sizeof(Packet))})
    {
        f.reset(); f.begin();
        f.append({0x90,60,100,3}); f.append({0x90,64,100,17}); f.append({0x90,67,100,400});
        f.output.storage.fill(UINT64_C(0x5a5a5a5a5a5a5a5a));
        f.output.seq.atom.size = capacity;
        check(f.run(512, false) == 0, "output-capacity exhaustion cannot allocate a larger MIDI buffer");
        const auto expectedSize = capacity >= sizeof(LV2_Atom_Sequence_Body) + sizeof(Packet)
            ? sizeof(LV2_Atom_Sequence_Body) + sizeof(Packet) : sizeof(LV2_Atom_Sequence_Body);
        check(f.output.seq.atom.size == expectedSize, "only whole padded events fitting the advertised capacity are emitted");
        const auto untouched = capacity - sizeof(LV2_Atom_Sequence_Body);
        bool guarded = true;
        const auto* bytes = reinterpret_cast<const uint8_t*>(f.output.storage.data());
        for (size_t i = untouched; i < sizeof(f.output.storage); ++i) guarded &= bytes[i] == 0x5a;
        check(guarded, "the wrapper never writes beyond the advertised MIDI output capacity");
        if (expectedSize > sizeof(LV2_Atom_Sequence_Body))
        {
            const Packet expected{0x90,60,100,3};
            check(std::memcmp(f.output.storage.data(), &expected, sizeof(LV2_Atom_Event) + 3) == 0,
                  "the fitting MIDI event retains its payload and timestamp under output pressure");
        }
        f.expect(true, "input detection still observes every event when MIDI output is full");
    }

    f.reset(); f.expect(false, "reactivation discards every note and pedal history");
}
}

int main()
{
    test::integration();
    std::printf("Chord MIDI integration: %d checks, %d failures\n", test::checks, test::failures);
    return test::failures == 0 ? 0 : 1;
}

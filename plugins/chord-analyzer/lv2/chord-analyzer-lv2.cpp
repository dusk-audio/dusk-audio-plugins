// Native LV2 wrapper — "Chord Analyzer Headless".
//
// Why this exists: JUCE's LV2 wrapper exposes plugin parameters via the
// LV2 patch:Message atom mechanism, not as native lv2:OutputPort + lv2:ControlPort.
// Hosts like Zynthian only display values written to native output control ports
// (the same pattern used by Ardour's a-comp.lv2). This wrapper bypasses JUCE's
// LV2 layer entirely and emits proper output control ports for chord detection
// results so headless LV2 hosts can render the detected chord in their own UI.
//
// Distinct from "Chord Analyzer MIDI" (JUCE-built across VST3/AU/LV2 with the
// custom UI). Use the MIDI variant for desktop hosts that want the visualizer;
// use this one for Zynthian and similar headless setups.

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

#include "../Source/ChordAnalyzer.h"

#define CHORD_ANALYZER_HEADLESS_URI "https://dusk-audio.github.io/plugins/chord-analyzer-headless"

namespace
{
enum PortIndex : uint32_t
{
    PORT_MIDI_IN            = 0,
    PORT_MIDI_OUT           = 1,
    PORT_KEY_ROOT           = 2,
    PORT_KEY_MODE           = 3,
    PORT_SUGGESTION_LEVEL   = 4,
    PORT_SHOW_INVERSIONS    = 5,
    PORT_DETECTED_ROOT      = 6,
    PORT_DETECTED_QUALITY   = 7,
    PORT_DETECTED_BASS      = 8,
    PORT_DETECTED_INVERSION = 9,
};

struct ChordAnalyzerLV2
{
    LV2_URID_Map* map           = nullptr;
    LV2_URID      midiEventURID = 0;

    const LV2_Atom_Sequence* midiIn  = nullptr;
    LV2_Atom_Sequence*       midiOut = nullptr;

    const float* keyRoot         = nullptr;
    const float* keyMode         = nullptr;
    const float* suggestionLevel = nullptr;
    const float* showInversions  = nullptr;

    float* detectedRoot      = nullptr;
    float* detectedQuality   = nullptr;
    float* detectedBass      = nullptr;
    float* detectedInversion = nullptr;

    ChordAnalyzer    analyzer;
    std::vector<int> activeNotes;
    ChordInfo        currentChord;
};

void publishDetectedChord (ChordAnalyzerLV2& self, const ChordInfo& chord)
{
    // Choice index 0 == "no chord / unknown". Indices 1..N follow the same
    // order used by the JUCE-built variant for cross-format consistency.
    const int rootIndex      = (chord.isValid && chord.rootNote >= 0 && chord.rootNote < 12)
                                 ? chord.rootNote + 1 : 0;
    const int bassIndex      = (chord.isValid && chord.bassNote >= 0 && chord.bassNote < 12)
                                 ? chord.bassNote + 1 : 0;
    const int qualityIndex   = (chord.isValid && chord.quality != ChordQuality::Unknown)
                                 ? static_cast<int> (chord.quality) + 1 : 0;
    const int inversionIndex = chord.isValid
                                 ? std::clamp (chord.inversion + 1, 0, 4) : 0;

    if (self.detectedRoot      != nullptr) *self.detectedRoot      = static_cast<float> (rootIndex);
    if (self.detectedQuality   != nullptr) *self.detectedQuality   = static_cast<float> (qualityIndex);
    if (self.detectedBass      != nullptr) *self.detectedBass      = static_cast<float> (bassIndex);
    if (self.detectedInversion != nullptr) *self.detectedInversion = static_cast<float> (inversionIndex);
}

LV2_Handle instantiate (const LV2_Descriptor*,
                        double /*sampleRate*/,
                        const char* /*bundlePath*/,
                        const LV2_Feature* const* features)
{
    auto* self = new (std::nothrow) ChordAnalyzerLV2();
    if (self == nullptr)
        return nullptr;

    for (int i = 0; features[i] != nullptr; ++i)
    {
        if (std::strcmp (features[i]->URI, LV2_URID__map) == 0)
            self->map = static_cast<LV2_URID_Map*> (features[i]->data);
    }

    if (self->map == nullptr)
    {
        delete self;
        return nullptr;
    }

    self->midiEventURID = self->map->map (self->map->handle, LV2_MIDI__MidiEvent);
    self->activeNotes.reserve (16);

    return static_cast<LV2_Handle> (self);
}

void connect_port (LV2_Handle instance, uint32_t port, void* data)
{
    auto* self = static_cast<ChordAnalyzerLV2*> (instance);

    switch (static_cast<PortIndex> (port))
    {
        case PORT_MIDI_IN:            self->midiIn            = static_cast<const LV2_Atom_Sequence*> (data); break;
        case PORT_MIDI_OUT:           self->midiOut           = static_cast<LV2_Atom_Sequence*>       (data); break;
        case PORT_KEY_ROOT:           self->keyRoot           = static_cast<const float*>             (data); break;
        case PORT_KEY_MODE:           self->keyMode           = static_cast<const float*>             (data); break;
        case PORT_SUGGESTION_LEVEL:   self->suggestionLevel   = static_cast<const float*>             (data); break;
        case PORT_SHOW_INVERSIONS:    self->showInversions    = static_cast<const float*>             (data); break;
        case PORT_DETECTED_ROOT:      self->detectedRoot      = static_cast<float*>                   (data); break;
        case PORT_DETECTED_QUALITY:   self->detectedQuality   = static_cast<float*>                   (data); break;
        case PORT_DETECTED_BASS:      self->detectedBass      = static_cast<float*>                   (data); break;
        case PORT_DETECTED_INVERSION: self->detectedInversion = static_cast<float*>                   (data); break;
    }
}

void activate (LV2_Handle) {}

void run (LV2_Handle instance, uint32_t /*nSamples*/)
{
    auto* self = static_cast<ChordAnalyzerLV2*> (instance);

    if (self->midiIn == nullptr || self->midiOut == nullptr)
        return;

    // Refresh key context from input control ports
    if (self->keyRoot != nullptr && self->keyMode != nullptr)
    {
        const int keyRootInt = std::clamp (static_cast<int> (*self->keyRoot), 0, 11);
        const bool isMinor   = (*self->keyMode) > 0.5f;
        self->analyzer.setKey (keyRootInt, isMinor);
    }

    // Prepare output MIDI atom sequence — capacity is the size advertised by the host.
    const uint32_t outCapacity = self->midiOut->atom.size;
    lv2_atom_sequence_clear (self->midiOut);
    self->midiOut->atom.type = self->midiIn->atom.type;

    bool notesChanged = false;

    LV2_ATOM_SEQUENCE_FOREACH (self->midiIn, ev)
    {
        // Forward every event so downstream synths still receive the MIDI stream
        lv2_atom_sequence_append_event (self->midiOut, outCapacity, ev);

        if (ev->body.type != self->midiEventURID)
            continue;

        const auto* msg = reinterpret_cast<const uint8_t*> (ev + 1);
        if (ev->body.size < 3)
            continue;

        const uint8_t status   = static_cast<uint8_t> (msg[0] & 0xF0);
        const int     noteNum  = msg[1] & 0x7F;
        const uint8_t velocity = msg[2] & 0x7F;

        if (status == LV2_MIDI_MSG_NOTE_ON && velocity > 0)
        {
            if (std::find (self->activeNotes.begin(), self->activeNotes.end(), noteNum) == self->activeNotes.end())
            {
                self->activeNotes.push_back (noteNum);
                notesChanged = true;
            }
        }
        else if (status == LV2_MIDI_MSG_NOTE_OFF
                 || (status == LV2_MIDI_MSG_NOTE_ON && velocity == 0))
        {
            auto it = std::find (self->activeNotes.begin(), self->activeNotes.end(), noteNum);
            if (it != self->activeNotes.end())
            {
                self->activeNotes.erase (it);
                notesChanged = true;
            }
        }
    }

    if (! notesChanged)
        return;

    const ChordInfo newChord = self->analyzer.analyze (self->activeNotes);
    if (newChord != self->currentChord)
    {
        self->currentChord = newChord;
        publishDetectedChord (*self, newChord);
    }
}

void deactivate (LV2_Handle) {}

void cleanup (LV2_Handle instance)
{
    delete static_cast<ChordAnalyzerLV2*> (instance);
}

const void* extension_data (const char*) { return nullptr; }

const LV2_Descriptor descriptor = {
    CHORD_ANALYZER_HEADLESS_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data,
};
} // namespace

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor (uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}

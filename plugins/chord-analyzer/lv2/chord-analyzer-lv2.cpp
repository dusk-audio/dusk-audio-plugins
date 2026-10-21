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
#include "../core/ChordAnalyzerCore.hpp"
#include "../core/MidiNoteState.hpp"
#include "../../shared-daf/dsp/DuskDenormals.hpp"

#define CHORD_ANALYZER_HEADLESS_URI "https://dusk-audio.github.io/plugins/chord-analyzer-headless"

namespace
{
using ChordAnalyzer = duskaudio::chord::ChordAnalyzerCore;
using duskaudio::chord::ChordFacts;
using duskaudio::chord::ChordQuality;
using duskaudio::chord::SuggestionCategory;
using duskaudio::chord::SuggestionFacts;
using duskaudio::chord::kInversionSlashBass;

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
    PORT_RESPECT_SUSTAIN    = 10,  // appended after detection ports to keep
                                    // existing port indices stable for hosts
                                    // that already cached the v1.1.0 layout

    // Suggestion slots, two ports each (root then quality), appended for the
    // same reason: a host that saved a session against the older layout must
    // still find every port it knew at the index it knew.
    PORT_SUGGESTIONS_BASE   = 11,
};

// Four slots, which is what the request asked for and as many as a small
// headless screen can show. They are filled in the order the editor lists
// them, so slot 1 is the first suggestion the plugin's own UI would show.
constexpr int kNumSuggestionSlots = 4;
constexpr int kNumSuggestionPorts = 2 * kNumSuggestionSlots;

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
    const float* respectSustain  = nullptr;

    float* detectedRoot      = nullptr;
    float* detectedQuality   = nullptr;
    float* detectedBass      = nullptr;
    float* detectedInversion = nullptr;

    // [slot * 2] root, [slot * 2 + 1] quality
    float* suggestionPorts[kNumSuggestionPorts] = {};

    ChordAnalyzer    analyzer;
    duskaudio::chord::MidiNoteState notes;
    ChordFacts       currentChord;

    // Last key context the suggestions were computed for. The suggestion set
    // depends on the key and the level as well as the chord, so all three are
    // tracked and the ports are refreshed when any of them moves. -1 forces
    // the first run() to publish.
    int  suggestionKeyRoot  = -1;
    bool suggestionKeyMinor = false;
    int  suggestionLevelIndex = -1;

    // Set by activate(): the next run() republishes the detected-chord ports
    // so a reactivated instance never shows a chord it no longer holds.
    bool detectedStale = true;

};

void publishDetectedChord (ChordAnalyzerLV2& self, const ChordFacts& chord)
{
    // Choice index 0 == "no chord / unknown". Indices 1..N follow the same
    // order used by the JUCE-built variant for cross-format consistency.
    const int rootIndex      = (chord.isValid && chord.rootNote >= 0 && chord.rootNote < 12)
                                 ? chord.rootNote + 1 : 0;
    const int bassIndex      = (chord.isValid && chord.bassNote >= 0 && chord.bassNote < 12)
                                 ? chord.bassNote + 1 : 0;
    const int qualityIndex   = (chord.isValid && chord.quality != ChordQuality::Unknown)
                                 ? static_cast<int> (chord.quality) + 1 : 0;
    // 0 == "-", 1 == root position, 2..7 the six inversion ordinals, 8 a bass
    // the matched chord shape does not spell. Mirrors the JUCE variant's
    // "Detected Inversion" choices and the scale points in the .ttl.
    const int inversionIndex = chord.isValid
                                 ? std::clamp (chord.inversion + 1, 0, kInversionSlashBass + 1) : 0;

    if (self.detectedRoot      != nullptr) *self.detectedRoot      = static_cast<float> (rootIndex);
    if (self.detectedQuality   != nullptr) *self.detectedQuality   = static_cast<float> (qualityIndex);
    if (self.detectedBass      != nullptr) *self.detectedBass      = static_cast<float> (bassIndex);
    if (self.detectedInversion != nullptr) *self.detectedInversion = static_cast<float> (inversionIndex);
}

void publishSuggestions (ChordAnalyzerLV2& self)
{
    // Allocation free by construction: getSuggestionFacts writes into this
    // fixed array, builds no strings and takes no lock. run() is the audio
    // thread and this plugin declares lv2:hardRTCapable.
    SuggestionFacts facts[kNumSuggestionSlots];

    const auto level = self.suggestionLevelIndex <= 0 ? SuggestionCategory::Basic
                     : self.suggestionLevelIndex == 1 ? SuggestionCategory::Intermediate
                                                      : SuggestionCategory::Advanced;

    const int count = self.analyzer.getSuggestionFacts (self.currentChord, level,
                                                        facts, kNumSuggestionSlots);

    for (int slot = 0; slot < kNumSuggestionSlots; ++slot)
    {
        const bool filled = slot < count;

        // Same encoding as detected_root and detected_quality, so a host can
        // reuse one renderer for all of them: 0 is the empty slot.
        const int rootIndex = (filled && facts[slot].rootNote >= 0 && facts[slot].rootNote < 12)
                                ? facts[slot].rootNote + 1 : 0;
        const int qualityIndex = (filled && facts[slot].quality != ChordQuality::Unknown)
                                ? static_cast<int> (facts[slot].quality) + 1 : 0;

        if (float* port = self.suggestionPorts[slot * 2])
            *port = static_cast<float> (rootIndex);

        if (float* port = self.suggestionPorts[slot * 2 + 1])
            *port = static_cast<float> (qualityIndex);
    }
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
        case PORT_RESPECT_SUSTAIN:    self->respectSustain    = static_cast<const float*>             (data); break;

        default:
            if (port >= PORT_SUGGESTIONS_BASE
                 && port < PORT_SUGGESTIONS_BASE + static_cast<uint32_t> (kNumSuggestionPorts))
                self->suggestionPorts[port - PORT_SUGGESTIONS_BASE] = static_cast<float*> (data);
            break;
    }
}

void activate (LV2_Handle instance)
{
    // LV2 core: reset every piece of history before reactivation.
    auto* self = static_cast<ChordAnalyzerLV2*> (instance);

    self->notes.reset();
    self->analyzer.reset();
    self->currentChord     = ChordFacts{};

    self->suggestionKeyRoot    = -1;
    self->suggestionKeyMinor   = false;
    self->suggestionLevelIndex = -1;
    self->detectedStale        = true;
}

void run (LV2_Handle instance, uint32_t nSamples)
{
    const duskaudio::ScopedFlushDenormals noDenormals;
    auto* self = static_cast<ChordAnalyzerLV2*> (instance);

    if (nSamples == 0)
    {
        // LV2 requires instantaneous control outputs even for an empty block.
        // Publish cached results, then return without touching MIDI buffers.
        publishDetectedChord (*self, self->currentChord);
        publishSuggestions (*self);
        return;
    }

    if (self->midiIn == nullptr || self->midiOut == nullptr)
        return;

    // Refresh key context from input control ports. The suggestion set depends
    // on the key and on the level, so a change to either has to refresh the
    // suggestion ports even when no note moved.
    bool suggestionsStale = false;

    if (self->keyRoot != nullptr && self->keyMode != nullptr)
    {
        const int keyRootInt = std::clamp (static_cast<int> (*self->keyRoot), 0, 11);
        const bool isMinor   = (*self->keyMode) > 0.5f;
        self->analyzer.setKey (keyRootInt, isMinor);

        if (keyRootInt != self->suggestionKeyRoot || isMinor != self->suggestionKeyMinor)
        {
            self->suggestionKeyRoot  = keyRootInt;
            self->suggestionKeyMinor = isMinor;
            suggestionsStale = true;
        }
    }

    {
        const int level = self->suggestionLevel != nullptr
                            ? std::clamp (static_cast<int> (*self->suggestionLevel), 0, 2) : 2;

        if (level != self->suggestionLevelIndex)
        {
            self->suggestionLevelIndex = level;
            suggestionsStale = true;
        }
    }

    const bool sustainEnabled = (self->respectSustain != nullptr) && (*self->respectSustain) > 0.5f;
    bool notesChanged = self->notes.setRespectSustain(sustainEnabled);

    // Hosts may share storage between any input and output. Read every input
    // control before publishing outputs that could overwrite those inputs.
    if (self->detectedStale)
    {
        publishDetectedChord (*self, self->currentChord);
        self->detectedStale = false;
        suggestionsStale = true;
    }

    // An aliased MIDI sequence already is the exact pass-through result.
    const bool inPlace = self->midiIn == self->midiOut;
    const uint32_t outCapacity = self->midiOut->atom.size;
    if (!inPlace)
    {
        lv2_atom_sequence_clear (self->midiOut);
        self->midiOut->atom.type = self->midiIn->atom.type;
        self->midiOut->body = self->midiIn->body; // preserve frame/beat timestamp units
    }

    LV2_ATOM_SEQUENCE_FOREACH (self->midiIn, ev)
    {
        // The SDK helper checks the unpadded payload, but advances atom.size by
        // the padded length. At an odd host capacity that can overstate size,
        // then unsigned subtraction can admit another out-of-bounds event.
        const uint64_t paddedSize = (uint64_t(sizeof(LV2_Atom_Event)) + ev->body.size + 7u) & ~uint64_t(7u);
        if (!inPlace && self->midiOut->atom.size <= outCapacity
            && paddedSize <= outCapacity - self->midiOut->atom.size)
            lv2_atom_sequence_append_event (self->midiOut, outCapacity, ev);

        if (ev->body.type != self->midiEventURID)
            continue;

        notesChanged = self->notes.process(reinterpret_cast<const uint8_t*> (ev + 1), ev->body.size) || notesChanged;
    }

    if (notesChanged)
    {
        // analyzeFacts rather than analyze: run() is the audio thread and this
        // plugin declares lv2:hardRTCapable. The numeric form allocates
        // nothing, and the display strings analyze() would build are unused
        // here - only the control ports are published.
        std::array<int, 128> notes;
        const int count = self->notes.copyNotes(notes.data(), int(notes.size()));
        const ChordFacts newChord = self->analyzer.analyzeFacts(notes.data(), count);
        if (newChord != self->currentChord)
        {
            self->currentChord = newChord;
            publishDetectedChord (*self, newChord);
            suggestionsStale = true;
        }
    }

    if (suggestionsStale)
        publishSuggestions (*self);
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

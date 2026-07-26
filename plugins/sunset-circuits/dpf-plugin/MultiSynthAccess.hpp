// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiSynthAccess.hpp — UI-side accessors for same-process DSP data (meters,
// scope ring, arp/sequencer step). Uses the shared weak-symbol bridge; see
// DuskAccessBridge.hpp for the single-binary-vs-split-LV2 contract and the
// required UI-side null guard. Strong definitions live in MultiSynthPlugin.cpp.
//
// The scalar accessors below back the Phase-4 UI meters/step LEDs; the DSP
// pointer accessor lets the UI pull the scope ring once per frame via the
// data-race-free MultiSynthDSP::copyScope(dst, maxN) (relaxed atomic loads;
// may tear across the write cursor, which is fine for a visualizer).

#pragma once

#include "DuskAccessBridge.hpp"

#include <cstdint>

namespace msynth { class MultiSynthDSP; }

// Peak output level in dBFS (-60..~+6), ~300 ms release. Null in a split LV2 UI.
DUSK_ACCESS_DECL(float, multiSynthGetOutLevelL);
DUSK_ACCESS_DECL(float, multiSynthGetOutLevelR);
// Current arpeggiator / acid-sequencer step (0..15), -1 when idle.
DUSK_ACCESS_DECL(int,   multiSynthGetArpStep);
// Live DSP instance, used by the UI for the scope ring buffer (read + performance-
// wheel writes: the UI's on-screen pitch-bend / mod-wheel controls call the same
// dsp.pitchBend()/modWheel() setters the MIDI path uses, which are relaxed atomic
// stores and therefore safe from the UI thread). Null in a split LV2 UI.
DUSK_ACCESS_DECL(msynth::MultiSynthDSP*, multiSynthGetDSP);
// Packed (sequence << 8 | program) for a MIDI program change (0xC0) the PLUGIN
// applied to itself. DPF has no plugin->host parameter notification, so a preset
// recalled by MIDI is invisible to the host and to the UI's parameter cache; the
// UI polls this, and on a sequence change re-applies that preset through its own
// (host-visible) preset path so knobs, name display and host automation agree with
// the engine. 0 = no MIDI program change yet. Null in a split LV2 UI, which simply
// does not get the sync.
DUSK_ACCESS_DECL(uint32_t, multiSynthGetMidiProgramSignal);

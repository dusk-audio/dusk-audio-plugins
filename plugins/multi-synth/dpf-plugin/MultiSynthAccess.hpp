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

namespace msynth { class MultiSynthDSP; }

// Peak output level in dBFS (-60..~+6), ~300 ms release. Null in a split LV2 UI.
DUSK_ACCESS_DECL(float, multiSynthGetOutLevelL);
DUSK_ACCESS_DECL(float, multiSynthGetOutLevelR);
// Current arpeggiator / acid-sequencer step (0..15), -1 when idle.
DUSK_ACCESS_DECL(int,   multiSynthGetArpStep);
// Live DSP instance for the scope ring buffer (read-only). Null in split LV2 UI.
DUSK_ACCESS_DECL(msynth::MultiSynthDSP*, multiSynthGetDSP);

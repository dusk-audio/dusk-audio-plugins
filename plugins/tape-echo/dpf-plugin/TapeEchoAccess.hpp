// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeEchoAccess.hpp — UI-side accessors for same-process DSP data (the VU/peak
// meter and effective tempo-synced motor time). Uses the shared weak-symbol
// bridge; see DuskAccessBridge.hpp for the single-binary-vs-split-LV2 contract
// and the required UI-side null guard. Strong definitions live in
// TapeEchoPlugin.cpp.

#pragma once

#include "DuskAccessBridge.hpp"

// Record-path VU and transient peak (0..~3). Null in the split LV2 UI.
DUSK_ACCESS_DECL(float, tapeEchoGetRecordVuLevel);
DUSK_ACCESS_DECL(float, tapeEchoGetRecordPeakLevel);

// Effective head-1 motor time in milliseconds after physical motor-range clamping.
// Null in the split LV2 UI.
DUSK_ACCESS_DECL(float, tapeEchoGetHead1DelayMs);

// True while tempo sync asks for a note the transport cannot reach at the host
// tempo, i.e. the motor-range clamp is active. Drives the blinking head readout.
// Null in the split LV2 UI, which falls back to the captured 120 BPM table.
DUSK_ACCESS_DECL(bool, tapeEchoGetSyncNoteOutOfRange);

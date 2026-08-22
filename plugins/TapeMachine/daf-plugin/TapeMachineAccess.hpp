// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// TapeMachineAccess.hpp — UI-side accessors for same-process DSP data (the two
// VU meters). Uses the shared weak-symbol bridge; see DuskAccessBridge.hpp for
// the single-binary-vs-split-LV2 contract and the required UI-side null guard.
// Strong definitions live in TapeMachinePlugin.cpp.

#pragma once

#include "DuskAccessBridge.hpp"

// Linear output peak per channel (0..~2), ~300 ms release. Null in split LV2 UI.
DUSK_ACCESS_DECL(float, tapeMachineGetVuL);
DUSK_ACCESS_DECL(float, tapeMachineGetVuR);
// Pre-processing input peak per channel (for the UI's In/Out meter switch).
DUSK_ACCESS_DECL(float, tapeMachineGetInVuL);
DUSK_ACCESS_DECL(float, tapeMachineGetInVuR);
// INPUT sample-peak hold per channel — post-gain/pre-tape during normal processing,
// raw passthrough input while bypassed or in Thru mode. Retained as a direct-access
// diagnostic; null in split LV2 UI.
DUSK_ACCESS_DECL(float, tapeMachineGetInPeakL);
DUSK_ACCESS_DECL(float, tapeMachineGetInPeakR);
// OUTPUT (final, post-everything) sample-peak hold per channel. Retained as a
// direct-access diagnostic for the output node. Null in split LV2 UI.
DUSK_ACCESS_DECL(float, tapeMachineGetOutPeakL);
DUSK_ACCESS_DECL(float, tapeMachineGetOutPeakR);

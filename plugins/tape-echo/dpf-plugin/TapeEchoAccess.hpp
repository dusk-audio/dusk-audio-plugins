// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeEchoAccess.hpp — UI-side accessor for same-process DSP data (the VU/peak
// meter). Uses the shared weak-symbol bridge; see DuskAccessBridge.hpp for the
// single-binary-vs-split-LV2 contract and the required UI-side null guard.
// Strong definition lives in TapeEchoPlugin.cpp.

#pragma once

#include "DuskAccessBridge.hpp"

// Linear output peak (0..~3), ~300 ms release. Null in the split LV2 UI.
DUSK_ACCESS_DECL(float, tapeEchoGetOutputLevel);

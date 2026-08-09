// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// Pre35Access.hpp — UI-side accessors for same-process DSP data (the output
// meters). Uses the shared weak-symbol bridge; see DuskAccessBridge.hpp for the
// single-binary-vs-split-LV2 contract and the required UI-side null guard.
// Strong definitions live in Pre35Plugin.cpp.

#pragma once

#include "DuskAccessBridge.hpp"

// Linear peak level (0..~2), ~300 ms release. Null in a split LV2 UI, which
// falls back to the kOutPeakL/kOutPeakR output parameters.
DUSK_ACCESS_DECL(float, pre35GetOutPeakL);
DUSK_ACCESS_DECL(float, pre35GetOutPeakR);

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQAccess.hpp — UI-side same-process DSP accessor bridge for Multi-Q 2.
// The Phase-2 core carries no meters/analyzer yet, so these return 0.0f
// placeholders; enough for the UI bridge to compile and for Phase-3 to wire the
// real spectrum/meter atomics without changing the contract. See
// DuskAccessBridge.hpp for the single-binary-vs-split-LV2 weak-symbol contract
// and the required UI-side null guard. Strong definitions live in MultiQPlugin.cpp.

#pragma once

#include "DuskAccessBridge.hpp"

// Output peak level per channel (placeholder 0.0f until the core exposes meters).
DUSK_ACCESS_DECL(float, multiQGetOutL);
DUSK_ACCESS_DECL(float, multiQGetOutR);

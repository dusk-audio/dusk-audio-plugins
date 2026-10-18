// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// RingOutAccess.hpp — UI-side accessor for same-process DSP data. The UI reads
// the engine (filter table, spectrum, meters, SETUP/ADD status) straight off the
// DSP object; every getter it uses is thread-safe by construction (spinlock,
// seqlock or atomics). Uses the shared weak-symbol bridge; see
// DuskAccessBridge.hpp for the single-binary-vs-split-LV2 contract and the
// required UI-side null guard. The strong definition lives in RingOutPlugin.cpp.

#pragma once

#include "DuskAccessBridge.hpp"

namespace duskaudio { class RingOutDSP; }

// Null in the split LV2 UI, where the display then has nothing to draw: this
// plugin is built MONOLITHIC for exactly that reason. Non-const because reading
// the table may apply a RESET that was deferred under lock contention; every
// member the editor calls is thread-safe by construction.
DUSK_ACCESS_DECL(duskaudio::RingOutDSP*, ringOutGetDSP);

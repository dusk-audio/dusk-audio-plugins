// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// FourKEQAccess.hpp — UI-side accessors for same-process DSP data (meters +
// spectrum). Uses the shared weak-symbol bridge; see DuskAccessBridge.hpp for
// the single-binary-vs-split-LV2 contract and the required UI-side null guard.
// Strong definitions live in FourKEQPlugin.cpp.

#pragma once

#include "DuskAccessBridge.hpp"

namespace duskaudio { class SpectrumRing; }

// Linear peak levels (0..~2), ~300 ms release.
DUSK_ACCESS_DECL(float, fourKEQGetInputPeakL);
DUSK_ACCESS_DECL(float, fourKEQGetInputPeakR);
DUSK_ACCESS_DECL(float, fourKEQGetOutputPeakL);
DUSK_ACCESS_DECL(float, fourKEQGetOutputPeakR);

// Pointers to the DSP's lock-free spectrum rings (null when out-of-process).
DUSK_ACCESS_DECL(const duskaudio::SpectrumRing*, fourKEQGetPreSpectrum);
DUSK_ACCESS_DECL(const duskaudio::SpectrumRing*, fourKEQGetPostSpectrum);
